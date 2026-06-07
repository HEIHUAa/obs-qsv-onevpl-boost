#include "common_utils.hpp"
#include "../obs-qsv-onevpl-encoder.hpp"

#include <sstream>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/dstr.h>

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
    mfxU16 OutWidth, mfxU16 OutHeight,
    mfxU16 Alignment) {
  std::vector<encoder_params::roi_region> result;
  result.reserve(NormRegions.size());
  for (auto &nr : NormRegions) {
    encoder_params::roi_region pr;
    pr.Left   = (mfxU16)(nr.Left   * OutWidth);
    pr.Top    = (mfxU16)(nr.Top    * OutHeight);
    pr.Right  = (mfxU16)(nr.Right  * OutWidth);
    pr.Bottom = (mfxU16)(nr.Bottom * OutHeight);
    pr.DeltaQP = nr.DeltaQP;

    // Round to nearest codec-specific block boundary so oneVPL does not
    // silently ignore the ROI (the SDK requires alignment for valid ROI).
    // See: https://intel.github.io/libvpl/latest/API_ref/VPL_structs_encode.html#mfxextencoderroi
    if (Alignment > 0) {
      auto RoundAlign = [Alignment](mfxU16 Val) -> mfxU16 {
        return static_cast<mfxU16>(
            (Val + Alignment / 2) / Alignment) * Alignment;
      };
      pr.Left   = RoundAlign(pr.Left);
      pr.Top    = RoundAlign(pr.Top);
      pr.Right  = RoundAlign(pr.Right);
      pr.Bottom = RoundAlign(pr.Bottom);

      // Clamp to frame boundaries (Right/Bottom are exclusive)
      if (pr.Left >= OutWidth)
        pr.Left = (mfxU16)(OutWidth / Alignment * Alignment);
      if (pr.Top >= OutHeight)
        pr.Top = (mfxU16)(OutHeight / Alignment * Alignment);
      if (pr.Right > OutWidth)
        pr.Right = OutWidth;
      if (pr.Bottom > OutHeight)
        pr.Bottom = OutHeight;
    }

    result.push_back(pr);
  }

  // Log the conversion result for debugging
  if (!NormRegions.empty()) {
    blog(LOG_INFO,
         "[QSV VPL] NormalizeROIToPixel: aligned %zu regions to %d-pixel boundaries"
         " (output %dx%d)",
         NormRegions.size(), Alignment, OutWidth, OutHeight);
    for (size_t i = 0; i < result.size(); i++) {
      blog(LOG_INFO,
           "[QSV VPL]   Pixel[%zu]: Left=%u Top=%u Right=%u Bottom=%u DeltaQP=%d",
           i, result[i].Left, result[i].Top,
           result[i].Right, result[i].Bottom,
           (int)result[i].DeltaQP);
    }
  }

  return result;
}

void RegisterEncoderData(obs_encoder_t *Encoder, plugin_context *Context) {
  const char *enc_id = obs_encoder_get_id(Encoder);

  blog(LOG_INFO,
       "[QSV VPL] RegisterEncoderData: encoder=%s, width=%d, height=%d, rc=%d",
       enc_id ? enc_id : "null",
       Context->EncoderParams.Width,
       Context->EncoderParams.Height,
       Context->EncoderParams.RateControl);

  {
    std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
    EncoderDataMap[Encoder] = Context;
  }

  // Check if there is a pending ROI config for this encoder type
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

  // (2) Try loading from this encoder's own persistent settings (per-profile)
  // This also updates GlobalROIConfig and applies to the encoder
  LoadROIFromEncoderSettings(Context);

  // (3) Fallback: if encoder settings didn't have ROI, try loading from file
  // This is more reliable because obs_data_t custom keys may not be persisted.
  {
    // Check if we need the fallback (outside the GlobalROIConfigMutex to avoid
    // deadlock with LoadROIConfigFromFile which takes its own lock)
    bool needsFallback = false;
    {
      std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
      needsFallback = (!GlobalROIConfig.Enabled ||
                       GlobalROIConfig.NormalizedRegions.empty());
    }

    if (needsFallback) {
      blog(LOG_INFO,
           "[QSV VPL] RegisterEncoderData: GlobalROIConfig empty, trying file fallback for encoder: %s",
           enc_id ? enc_id : "null");

      if (LoadROIConfigFromFile()) {
        // File was loaded, now apply to this encoder
        std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
        if (GlobalROIConfig.Enabled &&
            !GlobalROIConfig.NormalizedRegions.empty()) {
          mfxU16 effectiveMode = GlobalROIConfig.Mode;
          if (effectiveMode == 0 &&
              Context->EncoderParams.RateControl != MFX_RATECONTROL_CQP) {
            effectiveMode = 1;
            blog(LOG_INFO,
                 "[QSV VPL] Non-CQP rate control (%d), using Priority mode for encoder: %s",
                 Context->EncoderParams.RateControl, enc_id ? enc_id : "null");
          }
          auto pixelRegions = NormalizeROIToPixel(
              GlobalROIConfig.NormalizedRegions,
              Context->EncoderParams.Width,
              Context->EncoderParams.Height,
              GetCodecAlignment(Context->Codec));
          UpdateEncoderROI(Context, pixelRegions, effectiveMode, true);
          blog(LOG_INFO,
               "[QSV VPL] Fallback: applied ROI from file to encoder: %s",
               enc_id ? enc_id : "null");
        }
      }
    } else {
      blog(LOG_INFO,
           "[QSV VPL] RegisterEncoderData: GlobalROIConfig already populated (enabled=%d, regions=%zu), skipping file fallback for encoder: %s",
           (int)GlobalROIConfig.Enabled,
           GlobalROIConfig.NormalizedRegions.size(),
           enc_id ? enc_id : "null");
    }
  }

  // Log final ROI state for this encoder
  blog(LOG_INFO,
       "[QSV VPL] RegisterEncoderData: done for encoder=%s, roiEnabled=%d",
       enc_id ? enc_id : "null",
       (int)Context->EncoderParams.ROIEnabled);
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

void SaveROIToEncoderSettings(plugin_context *Context) {
  obs_data_t *settings = obs_encoder_get_settings(Context->EncoderData);
  if (!settings) {
    blog(LOG_WARNING,
         "[QSV VPL] SaveROIToEncoderSettings: obs_encoder_get_settings returned null!");
    return;
  }

  std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
  obs_data_set_bool(settings, "roi_enabled", GlobalROIConfig.Enabled);
  obs_data_set_int(settings, "roi_mode", GlobalROIConfig.Mode);

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
  obs_data_set_string(settings, "roi_regions", regionStr.c_str());

  blog(LOG_INFO,
       "[QSV VPL] ROI saved to encoder settings: enabled=%d, mode=%d, regions=%zu, "
       "region_str='%s'",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size(), regionStr.c_str());

  obs_data_release(settings);
}

void LoadROIFromEncoderSettings(plugin_context *Context) {
  obs_data_t *settings = obs_encoder_get_settings(Context->EncoderData);
  if (!settings) {
    blog(LOG_WARNING,
         "[QSV VPL] LoadROIFromEncoderSettings: obs_encoder_get_settings returned null!");
    return;
  }

  const char *enc_id = obs_encoder_get_id(Context->EncoderData);
  blog(LOG_INFO,
       "[QSV VPL] LoadROIFromEncoderSettings: checking encoder=%s, has_roi_enabled=%d",
       enc_id ? enc_id : "null",
       obs_data_has_user_value(settings, "roi_enabled"));

  if (!obs_data_has_user_value(settings, "roi_enabled")) {
    obs_data_release(settings);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    GlobalROIConfig.Enabled = obs_data_get_bool(settings, "roi_enabled");
    GlobalROIConfig.Mode = (mfxU16)obs_data_get_int(settings, "roi_mode");

    // Deserialize regions
    GlobalROIConfig.NormalizedRegions.clear();
    const char *regionStr = obs_data_get_string(settings, "roi_regions");
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
  }

  blog(LOG_INFO,
       "[QSV VPL] ROI loaded from encoder settings: enabled=%d, mode=%d, regions=%zu, "
       "region_str='%s'",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size(),
       obs_data_get_string(settings, "roi_regions"));

  // Apply to this encoder if enabled and there are active regions
  if (GlobalROIConfig.Enabled && !GlobalROIConfig.NormalizedRegions.empty()) {
    mfxU16 effectiveMode = GlobalROIConfig.Mode;
    if (effectiveMode == 0 &&
        Context->EncoderParams.RateControl != MFX_RATECONTROL_CQP) {
      effectiveMode = 1;
      blog(LOG_INFO,
           "[QSV VPL] Non-CQP rate control (%d), using Priority mode for encoder: %s",
           Context->EncoderParams.RateControl,
           enc_id ? enc_id : "null");
    }
    auto pixelRegions = NormalizeROIToPixel(
        GlobalROIConfig.NormalizedRegions,
        Context->EncoderParams.Width,
        Context->EncoderParams.Height,
        GetCodecAlignment(Context->Codec));
    UpdateEncoderROI(Context, pixelRegions, effectiveMode, true);
  }

  obs_data_release(settings);
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

// ── Per-profile ROI config persistence ─────────────────────────────────
// Uses obs_frontend_get_current_profile_path() to store ROI settings
// in each OBS profile's own config file, so different profiles can have
// different ROI configurations.
// Location: <profile_path>/obs-qsv-onevpl-roi.ini

static const char *kROIConfigFile = "obs-qsv-onevpl-roi.ini";

static config_t *OpenProfileConfig(bool ForWrite) {
  char *profile_path = obs_frontend_get_current_profile_path();
  if (!profile_path) {
    blog(LOG_WARNING,
         "[QSV VPL] OpenProfileConfig: obs_frontend_get_current_profile_path "
         "returned NULL (frontend not loaded?), cannot %s per-profile config.",
         ForWrite ? "save" : "load");
    return nullptr;
  }

  struct dstr config_path = {0};
  dstr_copy(&config_path, profile_path);
  dstr_cat(&config_path, "/");
  dstr_cat(&config_path, kROIConfigFile);

  if (ForWrite) {
    // Ensure parent directory exists (should always be there for a profile)
    os_mkdirs(profile_path);
  } else {
    // For reading, just check existence
    if (!os_file_exists(config_path.array)) {
      blog(LOG_INFO,
           "[QSV VPL] OpenProfileConfig: file not found '%s'",
           config_path.array);
      bfree(profile_path);
      dstr_free(&config_path);
      return nullptr;
    }
  }

  config_t *config = nullptr;
  int ret = config_open(&config, config_path.array, CONFIG_OPEN_ALWAYS);
  bfree(profile_path);
  dstr_free(&config_path);

  if (ret != CONFIG_SUCCESS) {
    blog(LOG_WARNING,
         "[QSV VPL] OpenProfileConfig: config_open failed, ret=%d", ret);
    return nullptr;
  }

  return config;
}

void SaveROIConfigToFile() {
  config_t *config = OpenProfileConfig(true);
  if (!config) {
    // If we can't open per-profile config, try the old module path as fallback
    blog(LOG_INFO,
         "[QSV VPL] SaveROIConfigToFile: per-profile config failed, "
         "trying module path fallback");

    char *config_path = obs_module_config_path(kROIConfigFile);
    if (!config_path) {
      blog(LOG_WARNING,
           "[QSV VPL] SaveROIConfigToFile: obs_module_config_path returned null!");
      return;
    }

    char *config_dir = obs_module_config_path("");
    if (config_dir) {
      os_mkdirs(config_dir);
      bfree(config_dir);
    }

    int ret = config_open(&config, config_path, CONFIG_OPEN_ALWAYS);
    bfree(config_path);
    if (ret != CONFIG_SUCCESS) {
      blog(LOG_WARNING,
           "[QSV VPL] SaveROIConfigToFile: fallback config_open failed, ret=%d",
           ret);
      return;
    }
  }

  std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);

  config_set_bool(config, "roi", "enabled", GlobalROIConfig.Enabled);
  config_set_int(config, "roi", "mode", GlobalROIConfig.Mode);

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
  config_set_string(config, "roi", "regions", regionStr.c_str());

  int saved = config_save(config);
  blog(LOG_INFO,
       "[QSV VPL] ROI config saved to profile: enabled=%d, mode=%d, regions=%zu, "
       "success=%d",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size(), saved);

  if (!saved) {
    blog(LOG_WARNING,
         "[QSV VPL] SaveROIConfigToFile: config_save FAILED!");
  }

  config_close(config);
}

bool LoadROIConfigFromFile() {
  config_t *config = OpenProfileConfig(false);
  if (!config) {
    // Try module path fallback (old location)
    char *config_path = obs_module_config_path(kROIConfigFile);
    if (!config_path) {
      return false;
    }
    if (!os_file_exists(config_path)) {
      blog(LOG_INFO,
           "[QSV VPL] LoadROIConfigFromFile: no config file found at '%s'",
           config_path);
      bfree(config_path);
      return false;
    }
    int ret = config_open(&config, config_path, CONFIG_OPEN_ALWAYS);
    bfree(config_path);
    if (ret != CONFIG_SUCCESS) {
      return false;
    }
  }

  if (!config_get_string(config, "roi", "enabled")) {
    blog(LOG_INFO,
         "[QSV VPL] LoadROIConfigFromFile: no roi.enabled in config");
    config_close(config);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    GlobalROIConfig.Enabled = config_get_bool(config, "roi", "enabled");
    GlobalROIConfig.Mode = (mfxU16)config_get_int(config, "roi", "mode");

    GlobalROIConfig.NormalizedRegions.clear();
    const char *regionStr = config_get_string(config, "roi", "regions");
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
  }

  blog(LOG_INFO,
       "[QSV VPL] ROI config loaded from profile: enabled=%d, mode=%d, regions=%zu",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size());

  config_close(config);
  return true;
}

