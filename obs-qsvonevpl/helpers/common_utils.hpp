#pragma once
#pragma warning(disable : 4201)

#ifndef __QSV_VPL_COMMON_UTILS_H__
#define __QSV_VPL_COMMON_UTILS_H__
#endif

#ifndef __QSV_VPL_WINDOWS_DEFS_H__
#include "../bits/windows_defs.hpp"
#endif
#ifndef __QSV_VPL_LINUX_DEFS_H__
#include "../bits/linux_defs.hpp"
#endif

#ifndef _INTTYPES
#include <inttypes.h>
#endif
#ifndef _INC_STDIO
#include <stdio.h>
#endif
#ifndef _CSTDIO_
#include <cstdio>
#endif
#ifndef _CONDITION_VARIABLE_
#include <condition_variable>
#endif
#ifndef _VECTOR_
#include <vector>
#endif
#ifndef _ARRAY_
#include <array>
#endif
#ifndef _STRING_
#include <string>
#endif
#ifndef _OPTIONAL_
#include <optional>
#endif
#ifndef _CINTTYPES_
#include <cinttypes>
#endif
#ifndef _CSTDLIB_
#include <cstdlib>
#endif
#include <cstring>
#if defined(_WIN32) || defined(_WIN64)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#ifndef _INC_STDLIB
#include <stdlib.h>
#endif
#ifndef _MEMORY_
#include <memory>
#endif
#ifndef _THREAD_
#include <thread>
#endif
#ifndef _BIT_
#include <bit>
#endif
#ifndef _CSTDDEF_
#include <cstddef>
#endif
#ifndef _CSTDINT_
#include <cstdint>
#endif
#ifndef _ATOMIC_
#include <atomic>
#endif
#ifndef _NEW_
#include <new>
#endif
#ifndef _ALGORITHM_
#include <algorithm>
#endif
#ifndef _ITERATOR_
#include <iterator>
#endif
#ifndef _MUTEX_
#include <mutex>
#endif
#ifndef _MAP_
#include <map>
#endif

#ifndef __MFX_H__
#include <vpl/mfx.h>
#endif
#ifndef __MFXVIDEOPLUSPLUS_H
#include <vpl/mfxvideo++.h>
#endif

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
extern std::map<obs_encoder_t *, plugin_context *> EncoderDataMap;
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
extern std::map<std::string, pending_roi_config> PendingROIConfig;
extern std::mutex PendingROIMutex;

// Single global ROI config (replaces per-type/PendingROIConfig[""] indirection)
extern pending_roi_config GlobalROIConfig;
extern std::mutex GlobalROIConfigMutex;

// Convert 0-1 normalized ROI coordinates to pixel values using given output dimensions.
// If Alignment > 0, coordinates are rounded to the nearest Alignment boundary.
std::vector<encoder_params::roi_region> NormalizeROIToPixel(
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 OutWidth, mfxU16 OutHeight,
    mfxU16 Alignment = 0);

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

static inline void *AlignedMalloc(size_t Size, size_t Alignment = 32) {
#if defined(_WIN32) || defined(_WIN64)
  return _aligned_malloc(Size, Alignment);
#elif defined(__linux__)
  return aligned_alloc(Alignment, Size);
#else
  return malloc(Size);
#endif
}

static inline void AlignedFree(void *Ptr) {
#if defined(_WIN32) || defined(_WIN64)
  _aligned_free(Ptr);
#else
  free(Ptr);
#endif
}

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

inline static void avx2_memcpy(uint8_t *Dst, const uint8_t *Src,
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