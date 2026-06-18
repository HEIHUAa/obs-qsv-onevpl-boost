/*

This file is provided under a dual BSD/GPLv2 license.  When using or
redistributing this file, you may do so under either license.

GPL LICENSE SUMMARY

Copyright(c) Oct. 2015 Intel Corporation.

This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

Contact Information:

Seung-Woo Kim, seung-woo.kim@intel.com
705 5th Ave S #500, Seattle, WA 98104

BSD LICENSE

Copyright(c) <date> Intel Corporation.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

* Neither the name of Intel Corporation nor the names of its
contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "helpers/common_utils.hpp"
//#include "obs-qsv-onevpl-encoder.hpp"
#include "obs-qsv-onevpl-plugin-init.hpp"

#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-qsvonevpl", "en-US");
MODULE_EXPORT const char *obs_module_description(void) {
  return "Intel Quick Sync Video support for Windows (oneVPL)";
}

extern obs_encoder_info H264FrameEncoderInfo;
extern obs_encoder_info H264TextureEncoderInfo;

extern obs_encoder_info AV1FrameEncoderInfo;
extern obs_encoder_info AV1TextureEncoderInfo;

extern obs_encoder_info HEVCFrameEncoderInfo;
extern obs_encoder_info HEVCTextureEncoderInfo;

mfxLoader GlobalQSVLoader = nullptr;
std::mutex GlobalLoaderMutex;

void InitGlobalLoader() {
  std::lock_guard<std::mutex> lock(GlobalLoaderMutex);
  if (GlobalQSVLoader)
    return;

  mfxLoader Loader = MFXLoad();
  if (!Loader)
    return;

  mfxConfig Config = MFXCreateConfig(Loader);
  mfxVariant Variant{};
  Variant.Type = MFX_VARIANT_TYPE_U32;
  Variant.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
  MFXSetConfigFilterProperty(
      Config,
      reinterpret_cast<const mfxU8 *>("mfxImplDescription.Impl"),
      Variant);

  Config = MFXCreateConfig(Loader);
  Variant.Type = MFX_VARIANT_TYPE_U32;
  Variant.Data.U32 = static_cast<mfxU32>(0x8086);
  MFXSetConfigFilterProperty(
      Config,
      reinterpret_cast<const mfxU8 *>("mfxImplDescription.VendorID"),
      Variant);

  GlobalQSVLoader = Loader;
}

void ReleaseGlobalLoader() {
  std::lock_guard<std::mutex> lock(GlobalLoaderMutex);
  if (GlobalQSVLoader) {
    MFXUnload(GlobalQSVLoader);
    GlobalQSVLoader = nullptr;
  }
}

// Deep VPL warm-up:
// For each supported codec, create a throwaway VPL session and call
// MFXVideoENCODE_Init to trigger GPU shader JIT compilation. The
// compiled shaders are cached by the driver, so the first real
// recording's Init reuses them — eliminating the ~250 ms delay.
// Surface-level warm-up is handled by per-recording WarmUpEncoder().

static void DeepWarmUpVPL() {
  mfxLoader Loader = nullptr;
  {
    std::lock_guard<std::mutex> lock(GlobalLoaderMutex);
    Loader = GlobalQSVLoader;
  }
  if (!Loader)
    return;

  struct obs_video_info ovi {};
  mfxU16 w = 1920, h = 1080, fpsN = 30, fpsD = 1;
  if (obs_get_video_info(&ovi) == OBS_VIDEO_SUCCESS &&
      ovi.base_width > 0 && ovi.base_height > 0) {
    w    = static_cast<mfxU16>(ovi.base_width);
    h    = static_cast<mfxU16>(ovi.base_height);
    fpsN = static_cast<mfxU16>(ovi.fps_num);
    fpsD = static_cast<mfxU16>(ovi.fps_den);
  }

  static constexpr struct {
    mfxU32 id;
    mfxU16 profile;
    const char *name;
  } Codecs[] = {
    {MFX_CODEC_AVC,  MFX_PROFILE_AVC_HIGH,  "H264"},
    {MFX_CODEC_HEVC, MFX_PROFILE_HEVC_MAIN,  "HEVC"},
    {MFX_CODEC_AV1,  MFX_PROFILE_AV1_MAIN,   "AV1"},
  };

  for (const auto &c : Codecs) {
    mfxSession session = nullptr;
    if (MFXCreateSession(Loader, 0, &session) < MFX_ERR_NONE)
      continue;

    try {
      MFXVideoENCODE encode(session);
      mfxVideoParam params{};
      params.mfx.CodecId       = c.id;
      params.mfx.CodecProfile  = c.profile;
      params.mfx.TargetUsage   = MFX_TARGETUSAGE_4;
      params.mfx.TargetKbps    = 6000;
      params.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
      params.mfx.FrameInfo.FourCC         = MFX_FOURCC_NV12;
      params.mfx.FrameInfo.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;
      params.mfx.FrameInfo.Width          = w;
      params.mfx.FrameInfo.Height         = h;
      params.mfx.FrameInfo.CropW          = w;
      params.mfx.FrameInfo.CropH          = h;
      params.mfx.FrameInfo.FrameRateExtN  = fpsN;
      params.mfx.FrameInfo.FrameRateExtD  = fpsD;
      params.mfx.FrameInfo.PicStruct      = MFX_PICSTRUCT_PROGRESSIVE;
      params.mfx.GopPicSize   = fpsN * 2;
      params.mfx.GopRefDist   = 4;
      params.AsyncDepth       = 4;
      params.mfx.LowPower     = MFX_CODINGOPTION_UNKNOWN;
      params.mfx.BRCParamMultiplier = 1;

      mfxStatus sts = encode.Query(&params, &params);
      if (sts >= MFX_ERR_NONE || sts == MFX_ERR_UNSUPPORTED) {
        if (encode.Init(&params) >= MFX_ERR_NONE) {
          encode.Close();
          info("VPL warm-up: %s OK (%dx%d)", c.name, w, h);
        } else {
          warn("VPL warm-up: %s Init failed", c.name);
        }
      }
    } catch (const std::exception &e) {
      warn("VPL warm-up %s: %s", c.name, e.what());
    }
    MFXClose(session);
  }
}

bool obs_module_load([[maybe_unused]] void) {
  AdaptersCount = MAX_ADAPTERS;
  GetAdaptersInfo(AdaptersInfo, &AdaptersCount);

  bool SupportAVC = false;
  bool SupportAV1 = false;
  bool SupportHEVC = false;
  for (size_t i = 0; i < AdaptersCount; i++) {
    struct adapter_info *AdapterInfo = &AdaptersInfo[i];
    SupportAVC |= AdapterInfo->IsIntel;
    SupportAV1 |= AdapterInfo->SupportAV1;
    SupportHEVC |= AdapterInfo->SupportHEVC;
  }

  if (SupportAVC) {
    obs_register_encoder(&H264FrameEncoderInfo);
    obs_register_encoder(&H264TextureEncoderInfo);
    info( "QSV AVC support");
    InitGlobalLoader();
  }
  if (SupportAV1) {
    obs_register_encoder(&AV1FrameEncoderInfo);
    obs_register_encoder(&AV1TextureEncoderInfo);
    info( "QSV AV1 support");
    InitGlobalLoader();
  }
  if (SupportHEVC) {
    obs_register_encoder(&HEVCFrameEncoderInfo);
    obs_register_encoder(&HEVCTextureEncoderInfo);
    info( "QSV HEVC support");
    InitGlobalLoader();
  }

  if (SupportAVC || SupportAV1 || SupportHEVC) {
    // Pre-warm the platform name cache so the first ParamsVisibilityModifier
    // call does not create a temporary VPL session.
    QueryPlatformCodeName();
    // Deep warm-up: create MFXVideoENCODE pipeline for each supported
    // codec to trigger GPU shader JIT compilation ahead of time.
    DeepWarmUpVPL();
  }

  // Register ROI editor in Tools menu (only when frontend API is available)
  RegisterROIEditor();

  return true;
}

void obs_module_unload(void) {
  ReleaseGlobalLoader();
}
