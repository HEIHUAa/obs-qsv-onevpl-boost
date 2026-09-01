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

#include "hw_d3d11.hpp"
#include "qsv_params.hpp"

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
  bool SupportVP9;
};

extern struct adapter_info AdaptersInfo[MAX_ADAPTERS];
extern size_t AdaptersCount;

enum codec_enum { QSV_CODEC_AVC, QSV_CODEC_AV1, QSV_CODEC_HEVC, QSV_CODEC_VP9 };

void ReleaseSessionData(void *);

extern void QSVLog(const int LogLevel, const char *Format, ...);

extern mfxLoader GlobalQSVLoader;
extern std::mutex GlobalLoaderMutex;

void InitGlobalLoader();
void ReleaseGlobalLoader();

bool PlatformSupportsIntraRefreshEncode(enum codec_enum Codec);
bool PlatformSupportsImageStabVPP();
bool PlatformSupportsFRCVPP();
bool PlatformSupportsMirrorVPP();
bool PlatformSupportsPercEncVPP();

struct plugin_context;
extern std::unordered_map<obs_encoder_t *, plugin_context *> EncoderDataMap;
extern std::mutex EncoderDataMapMutex;

void RegisterEncoderData(obs_encoder_t *Encoder, plugin_context *Context);
void UnregisterEncoderData(obs_encoder_t *Encoder);
plugin_context *LookupEncoderData(obs_encoder_t *Encoder);

// Pending ROI config, applied when a matching encoder type is created
struct pending_roi_config {
  std::vector<encoder_params::roi_region> Regions;   // pixel values (legacy / cache)
  std::vector<encoder_params::normalized_roi_region> NormalizedRegions; // 0-1 fractions
  mfxU16 Mode;
  bool Enabled = false;
};
extern std::unordered_map<std::string, pending_roi_config> PendingROIConfig;
extern std::mutex PendingROIMutex;

extern pending_roi_config GlobalROIConfig;
extern std::mutex GlobalROIConfigMutex;

// Format a double with minimal precision, no scientific notation, trimmed trailing zeros
std::string FormatROIDouble(double Value);

// Serialize/deserialize normalized ROI regions: "l,t,r,b,dqp|l,t,r,b,dqp"
std::string SerializeROIRegions(
    const std::vector<encoder_params::normalized_roi_region> &Regions);

std::vector<encoder_params::normalized_roi_region> DeserializeROIRegions(
    const std::string &Str);

// Apply normalized ROI config to a single encoder. Handles QP Delta→Priority fallback for non-CQP.
void ApplyROIConfigToEncoder(
    plugin_context *Context,
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 Mode, bool Enabled);

// Convert 0-1 normalized ROI coords to pixel values. If Alignment>0, rounds to nearest boundary.
std::vector<encoder_params::roi_region> NormalizeROIToPixel(
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 OutWidth, mfxU16 OutHeight,
    mfxU16 Alignment = 0);

// Expand gradient ROI regions into sub-rectangles with interpolated DeltaQP falloff
std::vector<encoder_params::roi_region> ExpandGradientRegions(
    const std::vector<encoder_params::roi_region> &Input,
    mfxU16 OutWidth, mfxU16 OutHeight);

// ROI coordinate alignment: AVC=16px (macroblock), HEVC=32px, AV1=16px
constexpr inline mfxU16 GetCodecAlignment(enum codec_enum Codec) {
  switch (Codec) {
  case QSV_CODEC_HEVC:
    return 32;
  case QSV_CODEC_AVC:
  case QSV_CODEC_AV1:
  default:
    return 16;
  }
}

void SaveROIToEncoderSettings(plugin_context *Context);
void LoadROIFromEncoderSettings(plugin_context *Context);

// Save/load GlobalROIConfig to/from per-profile INI (<profile>/obs-qsv-onevpl-roi.ini)
void SaveROIConfigToFile();
bool LoadROIConfigFromFile();

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#endif

void *AlignedMalloc(size_t Size, size_t Alignment = 32);
void AlignedFree(void *Ptr);

static inline void ParseOptionalBool(std::string_view Data,
                                     std::optional<bool> &Param) {
  if (Data == "AUTO") {
    Param = std::nullopt;
  } else if (Data == "ON") {
    Param = true;
  } else if (Data == "OFF") {
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