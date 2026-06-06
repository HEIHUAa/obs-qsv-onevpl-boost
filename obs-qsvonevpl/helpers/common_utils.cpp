#include "common_utils.hpp"
#include "../obs-qsv-onevpl-encoder.hpp"

#include <sstream>

struct adapter_info AdaptersInfo[MAX_ADAPTERS] = {};
size_t AdaptersCount = 0;

// Encoder data registry
std::map<obs_encoder_t *, plugin_context *> EncoderDataMap;
std::mutex EncoderDataMapMutex;

// Pending ROI config for encoder types not yet instantiated
std::map<std::string, pending_roi_config> PendingROIConfig;
std::mutex PendingROIMutex;

// Single global ROI config (used by the ROI Editor dialog)
pending_roi_config GlobalROIConfig;
std::mutex GlobalROIConfigMutex;

// Convert 0-1 normalized ROI coords → pixel values
std::vector<encoder_params::roi_region> NormalizeROIToPixel(
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 OutWidth, mfxU16 OutHeight) {
  std::vector<encoder_params::roi_region> result;
  result.reserve(NormRegions.size());
  for (auto &nr : NormRegions) {
    encoder_params::roi_region pr;
    pr.Left   = (mfxU16)(nr.Left   * OutWidth);
    pr.Top    = (mfxU16)(nr.Top    * OutHeight);
    pr.Right  = (mfxU16)(nr.Right  * OutWidth);
    pr.Bottom = (mfxU16)(nr.Bottom * OutHeight);
    pr.DeltaQP = nr.DeltaQP;
    result.push_back(pr);
  }
  return result;
}

void RegisterEncoderData(obs_encoder_t *Encoder, plugin_context *Context) {
  {
    std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
    EncoderDataMap[Encoder] = Context;
  }

  // Check if there is a pending ROI config for this encoder type
  const char *enc_id = obs_encoder_get_id(Encoder);
  if (!enc_id)
    return;

  std::lock_guard<std::mutex> lock(PendingROIMutex);
  auto it = PendingROIConfig.find(enc_id);
  if (it != PendingROIConfig.end()) {
    UpdateEncoderROI(Context, it->second.Regions, it->second.Mode,
                     it->second.Enabled);
    PendingROIConfig.erase(it);
    blog(LOG_INFO,
         "[QSV VPL] Applied pending ROI config for encoder type: %s",
         enc_id);
  }

  // (2) Global ROI config — always applied, never consumed
  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    if (GlobalROIConfig.Enabled && !GlobalROIConfig.NormalizedRegions.empty()) {
      // Convert 0-1 normalized → pixel using this encoder's output resolution
      auto pixelRegions = NormalizeROIToPixel(
          GlobalROIConfig.NormalizedRegions,
          Context->EncoderParams.Width,
          Context->EncoderParams.Height);
      // Force Priority mode if QP Delta is selected but encoder is not CQP
      mfxU16 effectiveMode = GlobalROIConfig.Mode;
      if (effectiveMode == 0 &&
          Context->EncoderParams.RateControl != MFX_RATECONTROL_CQP) {
        effectiveMode = 1;
        blog(LOG_INFO,
             "[QSV VPL] Non-CQP rate control detected, using Priority mode "
             "for encoder: %s",
             enc_id);
      }

      UpdateEncoderROI(Context, pixelRegions,
                       effectiveMode, GlobalROIConfig.Enabled);
      blog(LOG_INFO,
           "[QSV VPL] Applied global ROI config (normalized) to encoder: %s, dims=%dx%d",
           enc_id, Context->EncoderParams.Width, Context->EncoderParams.Height);
      for (size_t i = 0; i < pixelRegions.size(); i++) {
        blog(LOG_INFO,
             "[QSV VPL]   PixelRegion[%zu]: Left=%u Top=%u Right=%u Bottom=%u DeltaQP=%d",
             i, pixelRegions[i].Left, pixelRegions[i].Top,
             pixelRegions[i].Right, pixelRegions[i].Bottom,
             (int)pixelRegions[i].DeltaQP);
      }
    }
  }
}

// Serialize helper: format a double without scientific notation and with minimal precision
static std::string FormatROIDouble(double Value) {
  std::ostringstream ss;
  ss.precision(6);
  ss << std::fixed << Value;
  std::string s = ss.str();
  auto dot = s.find('.');
  if (dot != std::string::npos) {
    auto last = s.find_last_not_of('0');
    if (last > dot)
      s = s.substr(0, last + 1);
    else
      s = s.substr(0, dot + 2);
  }
  return s;
}

void SavePersistentROIConfig() {
  obs_data_t *config = obs_get_module_config();
  if (!config)
    return;

  std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
  obs_data_set_bool(config, "roi_enabled", GlobalROIConfig.Enabled);
  obs_data_set_int(config, "roi_mode", GlobalROIConfig.Mode);

  // Serialize normalized regions: pipe-separated, each is "left,top,right,bottom,deltaqp"
  std::string regionStr;
  for (auto &r : GlobalROIConfig.NormalizedRegions) {
    if (!regionStr.empty())
      regionStr += "|";
    regionStr += FormatROIDouble(r.Left) + "," +
                 FormatROIDouble(r.Top) + "," +
                 FormatROIDouble(r.Right) + "," +
                 FormatROIDouble(r.Bottom) + "," +
                 std::to_string(r.DeltaQP);
  }
  obs_data_set_string(config, "roi_regions", regionStr.c_str());

  blog(LOG_INFO,
       "[QSV VPL] Persistent ROI config saved: enabled=%d, mode=%d, regions=%zu",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size());
}

void LoadPersistentROIConfig() {
  obs_data_t *config = obs_get_module_config();
  if (!config)
    return;

  if (!obs_data_has_user_value(config, "roi_enabled"))
    return;

  std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
  GlobalROIConfig.Enabled = obs_data_get_bool(config, "roi_enabled");
  GlobalROIConfig.Mode = (mfxU16)obs_data_get_int(config, "roi_mode");

  // Deserialize regions
  GlobalROIConfig.NormalizedRegions.clear();
  const char *regionStr = obs_data_get_string(config, "roi_regions");
  if (regionStr && regionStr[0]) {
    std::istringstream stream(regionStr);
    std::string segment;
    while (std::getline(stream, segment, '|')) {
      if (segment.empty())
        continue;
      double l = 0, t = 0, r = 0, b = 0;
      int dqp = 0;
      if (std::sscanf(segment.c_str(), "%lf,%lf,%lf,%lf,%d",
                       &l, &t, &r, &b, &dqp) == 5) {
        encoder_params::normalized_roi_region nr = {};
        nr.Left = l;
        nr.Top = t;
        nr.Right = r;
        nr.Bottom = b;
        nr.DeltaQP = (mfxI16)dqp;
        GlobalROIConfig.NormalizedRegions.push_back(nr);
      }
    }
  }

  blog(LOG_INFO,
       "[QSV VPL] Persistent ROI config loaded: enabled=%d, mode=%d, regions=%zu",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size());
  for (size_t i = 0; i < GlobalROIConfig.NormalizedRegions.size(); i++) {
    blog(LOG_INFO,
         "[QSV VPL]   Persisted[%zu]: Left=%.4f Top=%.4f Right=%.4f Bottom=%.4f DeltaQP=%d",
         i,
         GlobalROIConfig.NormalizedRegions[i].Left,
         GlobalROIConfig.NormalizedRegions[i].Top,
         GlobalROIConfig.NormalizedRegions[i].Right,
         GlobalROIConfig.NormalizedRegions[i].Bottom,
         (int)GlobalROIConfig.NormalizedRegions[i].DeltaQP);
  }
}

void UnregisterEncoderData(obs_encoder_t *Encoder) {
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
  EncoderDataMap.erase(Encoder);
}

plugin_context *LookupEncoderData(obs_encoder_t *Encoder) {
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
  auto it = EncoderDataMap.find(Encoder);
  return (it != EncoderDataMap.end()) ? it->second : nullptr;
}

