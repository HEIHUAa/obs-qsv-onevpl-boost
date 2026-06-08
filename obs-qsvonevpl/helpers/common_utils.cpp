#include "common_utils.hpp"
#include "../obs-qsv-onevpl-encoder.hpp"

#include <sstream>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/dstr.h>

#include <cstring>
#include <cstdint>
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#include <immintrin.h>

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

  // (3) Apply GlobalROIConfig to this encoder if populated.
  // This handles the case where ROI was previously set via editor / loaded from
  // file, but this encoder's obs_data_t does not persist custom keys.  Without
  // this, a new encoder instance created *after* the editor was used would
  // never receive the ROI config (see the "GlobalROIConfig already populated,
  // skipping file fallback" bug - the file fallback is rightfully skipped
  // because we already have data in memory; we just fail to apply it).
  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    if (GlobalROIConfig.Enabled &&
        !GlobalROIConfig.NormalizedRegions.empty()) {
      ApplyROIConfigToEncoder(Context, GlobalROIConfig.NormalizedRegions,
                              GlobalROIConfig.Mode, true);
      blog(LOG_INFO,
           "[QSV VPL] Applied GlobalROIConfig to encoder: %s "
           "(enabled=%d, regions=%zu)",
           enc_id ? enc_id : "null", (int)GlobalROIConfig.Enabled,
           GlobalROIConfig.NormalizedRegions.size());

    } else {
      // GlobalROIConfig is empty - try loading from file
      blog(LOG_INFO,
           "[QSV VPL] RegisterEncoderData: GlobalROIConfig empty, trying "
           "file fallback for encoder: %s",
           enc_id ? enc_id : "null");

      // Release the mutex before calling LoadROIConfigFromFile (which takes
      // its own lock) to avoid deadlock
    }
  }

  // (4) File fallback (only reached if GlobalROIConfig was empty above)
  // We re-check inside the mutex-free zone by calling LoadROIConfigFromFile
  // which takes GlobalROIConfigMutex internally.
  {
    bool stillEmpty = false;
    {
      std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
      stillEmpty = (!GlobalROIConfig.Enabled ||
                    GlobalROIConfig.NormalizedRegions.empty());
    }
    if (stillEmpty) {
      if (LoadROIConfigFromFile()) {
        // File was loaded, now apply to this encoder
        std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
        if (GlobalROIConfig.Enabled &&
            !GlobalROIConfig.NormalizedRegions.empty()) {
          ApplyROIConfigToEncoder(Context, GlobalROIConfig.NormalizedRegions,
                                  GlobalROIConfig.Mode, true);
          blog(LOG_INFO,
               "[QSV VPL] Fallback: applied ROI from file to encoder: %s",
               enc_id ? enc_id : "null");
        }
      }
    } else {
      blog(LOG_INFO,
           "[QSV VPL] RegisterEncoderData: file fallback not needed for encoder: %s",
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
std::string FormatROIDouble(double Value) {
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

// ── ROI serialization helpers ────────────────────────────────────────

std::string SerializeROIRegions(
    const std::vector<encoder_params::normalized_roi_region> &Regions) {
  std::string result;
  for (auto &r : Regions) {
    if (!result.empty())
      result += "|";
    result += FormatROIDouble(r.Left) + "," +
              FormatROIDouble(r.Top) + "," +
              FormatROIDouble(r.Right) + "," +
              FormatROIDouble(r.Bottom) + "," +
              std::to_string(r.DeltaQP);
  }
  return result;
}

std::vector<encoder_params::normalized_roi_region> DeserializeROIRegions(
    const std::string &Str) {
  std::vector<encoder_params::normalized_roi_region> result;
  std::istringstream stream(Str);
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
      result.push_back(nr);
    }
  }
  return result;
}

// ── Apply ROI to a single encoder ────────────────────────────────────

void ApplyROIConfigToEncoder(
    plugin_context *Context,
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 Mode, bool Enabled) {
  // QP Delta mode requires CQP rate control; fall back to Priority if not
  mfxU16 effectiveMode = Mode;
  if (Mode == 0 &&
      Context->EncoderParams.RateControl != MFX_RATECONTROL_CQP) {
    effectiveMode = 1;
    blog(LOG_INFO,
         "[QSV VPL] Encoder uses non-CQP rate control (%d), "
         "forcing ROI Priority mode for ROI to take effect",
         Context->EncoderParams.RateControl);
  }

  auto pixelRegions = NormalizeROIToPixel(
      NormRegions,
      Context->EncoderParams.Width,
      Context->EncoderParams.Height,
      GetCodecAlignment(Context->Codec));
  UpdateEncoderROI(Context, pixelRegions, effectiveMode, Enabled);
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

  // Serialize normalized regions using the common helper
  std::string regionStr = SerializeROIRegions(GlobalROIConfig.NormalizedRegions);
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

    // Deserialize regions using the common helper
    const char *regionStr = obs_data_get_string(settings, "roi_regions");
    GlobalROIConfig.NormalizedRegions =
        regionStr ? DeserializeROIRegions(regionStr)
                  : std::vector<encoder_params::normalized_roi_region>();
  }

  blog(LOG_INFO,
       "[QSV VPL] ROI loaded from encoder settings: enabled=%d, mode=%d, regions=%zu, "
       "region_str='%s'",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size(),
       obs_data_get_string(settings, "roi_regions"));

  // Apply to this encoder if enabled and there are active regions
  if (GlobalROIConfig.Enabled && !GlobalROIConfig.NormalizedRegions.empty()) {
    ApplyROIConfigToEncoder(Context, GlobalROIConfig.NormalizedRegions,
                            GlobalROIConfig.Mode, true);
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

static config_t *OpenROIConfig(bool ForWrite) {
  // First try per-profile path
  char *profile_path = obs_frontend_get_current_profile_path();
  if (profile_path) {
    struct dstr config_path = {0};
    dstr_copy(&config_path, profile_path);
    dstr_cat(&config_path, "/");
    dstr_cat(&config_path, kROIConfigFile);

    if (ForWrite) {
      os_mkdirs(profile_path);
    } else {
      if (!os_file_exists(config_path.array)) {
        blog(LOG_INFO,
             "[QSV VPL] OpenROIConfig: profile file not found '%s'",
             config_path.array);
        bfree(profile_path);
        dstr_free(&config_path);
        goto try_module_path;
      }
    }

    config_t *config = nullptr;
    int ret = config_open(&config, config_path.array, CONFIG_OPEN_ALWAYS);
    bfree(profile_path);
    dstr_free(&config_path);

    if (ret == CONFIG_SUCCESS)
      return config;

    blog(LOG_WARNING,
         "[QSV VPL] OpenROIConfig: profile config_open failed, ret=%d", ret);
  }

try_module_path:
  // Fallback to module config path
  char *config_path = obs_module_config_path(kROIConfigFile);
  if (!config_path) {
    blog(LOG_WARNING,
         "[QSV VPL] OpenROIConfig: obs_module_config_path returned null!");
    return nullptr;
  }

  if (!ForWrite && !os_file_exists(config_path)) {
    blog(LOG_INFO,
         "[QSV VPL] OpenROIConfig: no module config file at '%s'",
         config_path);
    bfree(config_path);
    return nullptr;
  }

  if (ForWrite) {
    char *config_dir = obs_module_config_path("");
    if (config_dir) {
      os_mkdirs(config_dir);
      bfree(config_dir);
    }
  }

  config_t *config = nullptr;
  int ret = config_open(&config, config_path, CONFIG_OPEN_ALWAYS);
  bfree(config_path);

  if (ret != CONFIG_SUCCESS) {
    blog(LOG_WARNING,
         "[QSV VPL] OpenROIConfig: module config_open failed, ret=%d", ret);
    return nullptr;
  }

  return config;
}

void SaveROIConfigToFile() {
  config_t *config = OpenROIConfig(true);
  if (!config) {
    blog(LOG_WARNING,
         "[QSV VPL] SaveROIConfigToFile: could not open config for writing");
    return;
  }

  std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);

  config_set_bool(config, "roi", "enabled", GlobalROIConfig.Enabled);
  config_set_int(config, "roi", "mode", GlobalROIConfig.Mode);

  std::string regionStr = SerializeROIRegions(GlobalROIConfig.NormalizedRegions);
  config_set_string(config, "roi", "regions", regionStr.c_str());

  int saved = config_save(config);
  blog(LOG_INFO,
       "[QSV VPL] ROI config saved: enabled=%d, mode=%d, regions=%zu, "
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
  config_t *config = OpenROIConfig(false);
  if (!config)
    return false;

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

    const char *regionStr = config_get_string(config, "roi", "regions");
    GlobalROIConfig.NormalizedRegions =
        regionStr ? DeserializeROIRegions(regionStr)
                  : std::vector<encoder_params::normalized_roi_region>();
  }

  blog(LOG_INFO,
       "[QSV VPL] ROI config loaded from file: enabled=%d, mode=%d, regions=%zu",
       (int)GlobalROIConfig.Enabled, (int)GlobalROIConfig.Mode,
       GlobalROIConfig.NormalizedRegions.size());

  config_close(config);
  return true;
}

void *AlignedMalloc(size_t Size, size_t Alignment) {
#if defined(_WIN32) || defined(_WIN64)
  return _aligned_malloc(Size, Alignment);
#elif defined(__linux__)
  return aligned_alloc(Alignment, Size);
#else
  return malloc(Size);
#endif
}

void AlignedFree(void *Ptr) {
#if defined(_WIN32) || defined(_WIN64)
  _aligned_free(Ptr);
#else
  free(Ptr);
#endif
}

void avx2_memcpy(uint8_t *Dst, const uint8_t *Src,
                 unsigned long long Size) {
  static const bool HasAVX2 = [] {
#if defined(_WIN32) || defined(_WIN64)
    int cpuInfo[4] = {};
    __cpuidex(cpuInfo, 7, 0);
    return (cpuInfo[1] >> 5) & 1;
#elif defined(__x86_64__) || defined(__i386__)
    unsigned int eax = 7, ecx = 0;
    unsigned int cpuInfo[4] = {};
    __get_cpuid_count(eax, ecx, &cpuInfo[0], &cpuInfo[1], &cpuInfo[2], &cpuInfo[3]);
    return (cpuInfo[1] >> 5) & 1;
#else
    return false;
#endif
  }();

  if (!HasAVX2 || Size < 128) {
    memcpy(Dst, Src, Size);
    return;
  }
  uint8_t *DstFin = Dst + Size;
  const uint8_t *DstAlignedFin = reinterpret_cast<uint8_t *>(
      (reinterpret_cast<size_t>(DstFin + 31) & ~31) - 128);
  __m256i Y0, Y1, Y2, Y3;
  const int StartAlignDiff =
      static_cast<int>(reinterpret_cast<size_t>(Dst) & 31);
  if (StartAlignDiff) {
    Y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(Dst), Y0);
    Dst += 32 - StartAlignDiff;
    Src += 32 - StartAlignDiff;
  }
  for (; Dst < DstAlignedFin; Dst += 128, Src += 128) {
    Y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 0));
    Y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 32));
    Y2 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 64));
    Y3 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 96));
    _mm256_stream_si256(reinterpret_cast<__m256i *>(Dst + 0), Y0);
    _mm256_stream_si256(reinterpret_cast<__m256i *>(Dst + 32), Y1);
    _mm256_stream_si256(reinterpret_cast<__m256i *>(Dst + 64), Y2);
    _mm256_stream_si256(reinterpret_cast<__m256i *>(Dst + 96), Y3);
  }
  uint8_t *DstTmpl = DstFin - 128;
  Src -= (Dst - DstTmpl);
  Y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 0));
  Y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 32));
  Y2 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 64));
  Y3 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(Src + 96));
  _mm256_storeu_si256(reinterpret_cast<__m256i *>(DstTmpl + 0), Y0);
  _mm256_storeu_si256(reinterpret_cast<__m256i *>(DstTmpl + 32), Y1);
  _mm256_storeu_si256(reinterpret_cast<__m256i *>(DstTmpl + 64), Y2);
  _mm256_storeu_si256(reinterpret_cast<__m256i *>(DstTmpl + 96), Y3);
  _mm256_zeroupper();
}

