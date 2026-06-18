#pragma once
#pragma warning(disable : 4201)

#include "../bits/windows_defs.hpp"
#include "../bits/linux_defs.hpp"

#include <inttypes.h>
#include <cstdio>
#include <condition_variable>
#include <vector>
#include <array>
#include <string>
#include <optional>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <bit>
#include <atomic>
#include <new>
#include <algorithm>
#include <iterator>
#include <mutex>
#include <map>
#include <unordered_map>

#if defined(_WIN32) || defined(_WIN64)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#include <vpl/mfx.h>
#include <vpl/mfxvideo++.h>

extern "C" {
#include <media-io/video-io.h>
#include <obs-av1.h>
#include <obs-avc.h>
#include <obs-hevc.h>
#include <obs-module.h>
#include <obs.h>

#include <util/base.h>
#include <util/config-file.h>
#include <util/pipe.h>
#include <util/platform.h>
}

#ifndef __QSV_VPL_HWManager_H__
#include "hw_d3d11.hpp"
#endif

#ifndef __QSV_VPL_ENCODER_PARAMS_H__
#include "qsv_params.hpp"
#endif

#ifndef do_log
#define do_log(level, format, ...)                                             \
  blog(level, "[QSV encoder: '%s'] " format, "libvpl", ##__VA_ARGS__);
#endif
#ifndef error
#define error(format, ...) do_log(LOG_ERROR, format, ##__VA_ARGS__)
#endif
#ifndef warn
#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#endif
#ifndef info
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)
#endif
#ifndef debug
#define debug(format, ...) do_log(LOG_DEBUG, format, ##__VA_ARGS__)
#endif
#ifndef error_hr
#define error_hr(msg)                                                          \
  warn("%s: %s: 0x%08lX", __FUNCTION__, msg, static_cast<uint32_t>(hr));
#endif

constexpr int MAX_ADAPTERS = 10;

void GetAdaptersInfo(struct adapter_info *Adapters, size_t *AdaptersCount);

struct adapter_info {
  bool IsIntel;
  bool IsDGPU;
  bool SupportAV1;
  bool SupportHEVC;
};

extern struct adapter_info AdaptersInfo[MAX_ADAPTERS];
extern size_t AdaptersCount;

enum codec_enum { QSV_CODEC_AVC, QSV_CODEC_AV1, QSV_CODEC_HEVC };

void ReleaseSessionData(void *);

extern void QSVLog(const int LogLevel, const char *Format, ...);

extern mfxLoader GlobalQSVLoader;
extern std::mutex GlobalLoaderMutex;

void InitGlobalLoader();
void ReleaseGlobalLoader();

// Encoder data registry - maps obs_encoder_t* to plugin_context*
// Used by ROI editor to look up encoder data without using internal OBS APIs
struct plugin_context;
extern std::unordered_map<obs_encoder_t *, plugin_context *> EncoderDataMap;
extern std::mutex EncoderDataMapMutex;

void RegisterEncoderData(obs_encoder_t *Encoder, plugin_context *Context);
void UnregisterEncoderData(obs_encoder_t *Encoder);
plugin_context *LookupEncoderData(obs_encoder_t *Encoder);

// Pending ROI config - applied when an encoder of matching type is created
// Key: encoder type ID (e.g. "obs_qsv_vpl_h264")
struct pending_roi_config {
  std::vector<encoder_params::roi_region> Regions;   // pixel values (legacy / cache)
  std::vector<encoder_params::normalized_roi_region> NormalizedRegions; // 0-1 fractions
  mfxU16 Mode;
  bool Enabled = false;
};
extern std::unordered_map<std::string, pending_roi_config> PendingROIConfig;
extern std::mutex PendingROIMutex;

// Single global ROI config (replaces per-type/PendingROIConfig[""] indirection)
extern pending_roi_config GlobalROIConfig;
extern std::mutex GlobalROIConfigMutex;

// Format a double with minimal precision (no scientific notation, trimmed trailing zeros)
std::string FormatROIDouble(double Value);

// Serialize normalized ROI regions to pipe+comma format: "l,t,r,b,dqp|l,t,r,b,dqp"
// This is the format used for persistent storage (file & encoder settings).
std::string SerializeROIRegions(
    const std::vector<encoder_params::normalized_roi_region> &Regions);

// Deserialize normalized ROI regions from pipe+comma format.
std::vector<encoder_params::normalized_roi_region> DeserializeROIRegions(
    const std::string &Str);

// Apply normalized ROI config to a single encoder instance.
// Handles QP Delta→Priority mode fallback for non-CQP rate control internally.
void ApplyROIConfigToEncoder(
    plugin_context *Context,
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 Mode, bool Enabled);

// Convert 0-1 normalized ROI coordinates to pixel values using given output dimensions.
// If Alignment > 0, coordinates are rounded to the nearest Alignment boundary.
std::vector<encoder_params::roi_region> NormalizeROIToPixel(
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 OutWidth, mfxU16 OutHeight,
    mfxU16 Alignment = 0);

// Expand gradient ROI regions into multiple non-overlapping sub-rectangles.
// Each sub-rectangle gets interpolated DeltaQP for a smooth falloff.
// Non-gradient regions are passed through unchanged.
// Each region's GradientSteps field controls subdivision count per side.
std::vector<encoder_params::roi_region> ExpandGradientRegions(
    const std::vector<encoder_params::roi_region> &Input,
    mfxU16 OutWidth, mfxU16 OutHeight);

// Return the ROI coordinate alignment requirement for the given codec.
// AVC/H.264 requires 16-pixel (macroblock) alignment; HEVC requires 32-pixel alignment.
inline mfxU16 GetCodecAlignment(enum codec_enum Codec) {
  switch (Codec) {
  case QSV_CODEC_HEVC:
    return 32;
  case QSV_CODEC_AVC:
  case QSV_CODEC_AV1:
  default:
    return 16;
  }
}

// Save GlobalROIConfig to a specific encoder's obs_data_t settings
void SaveROIToEncoderSettings(plugin_context *Context);
// Load ROI from encoder's obs_data_t settings into GlobalROIConfig and apply
void LoadROIFromEncoderSettings(plugin_context *Context);

// Save GlobalROIConfig to per-profile INI config file in the current
// OBS profile directory (<profile>/obs-qsv-onevpl-roi.ini).
// Falls back to obs_module_config_path if frontend API is unavailable.
void SaveROIConfigToFile();
// Load GlobalROIConfig from the per-profile INI config file.
// Returns true if a config was loaded, false otherwise.
bool LoadROIConfigFromFile();

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#endif

void *AlignedMalloc(size_t Size, size_t Alignment = 32);
void AlignedFree(void *Ptr);

static inline void ParseOptionalBool(const char *Data,
                                     std::optional<bool> &Param) {
  if (std::strcmp(Data, "AUTO") == 0) {
    Param = std::nullopt;
  } else if (std::strcmp(Data, "ON") == 0) {
    Param = true;
  } else if (std::strcmp(Data, "OFF") == 0) {
    Param = false;
  }
}

void avx2_memcpy(uint8_t *Dst, const uint8_t *Src,
                 unsigned long long Size);

// HEVC bitstream parsing helpers
uint32_t hevc_read_bits(const uint8_t *data, size_t max_size,
                        size_t &byte_pos, int &bit_pos, int n);
uint32_t hevc_read_uev(const uint8_t *data, size_t max_size,
                       size_t &byte_pos, int &bit_pos);