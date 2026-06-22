#pragma warning(disable : 4996)
#include "obs-qsv-onevpl-encoder-internal.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <span>
#include <string>
#include <string_view>

constexpr mfxU32 BRC_MAX_KBPS_LIMIT = 65535;

QSVEncoder::~QSVEncoder() {
  if (QSVEncode || QSVProcessing) {
    ClearData();
  }
  delete[] QSVLayerArray;
#ifdef QSV_UHD600_SUPPORT
  ReleaseSystemMemorySurfacePool();
#endif
}

#ifdef QSV_UHD600_SUPPORT
void QSVEncoder::InitSystemMemorySurfacePool() {
  if (!QSVEncode || QSVIsTextureEncoder)
    return;

  mfxFrameAllocRequest Request[2] = {};
  mfxStatus Sts = QSVEncode->QueryIOSurf(&QSVEncodeParams, Request);
  if (Sts != MFX_ERR_NONE) {
    warn("QueryIOSurf failed: %d, using default 4 surfaces", Sts);
    QSVSystemMemPoolSize = 4;
  } else {
    QSVSystemMemPoolSize = Request[0].NumFrameSuggested;
    if (QSVSystemMemPoolSize < 4)
      QSVSystemMemPoolSize = 4;
  }

  info("\tSystem memory surface pool size: %d", QSVSystemMemPoolSize);

  mfxFrameInfo &FI = QSVEncodeParams.mfx.FrameInfo;
  auto VideoSignalInfo =
      QSVEncodeParams.GetExtBuffer<mfxExtVideoSignalInfo>();
  if (VideoSignalInfo) {
    info("\tVideoSignalInfo found: VideoFullRange=%d, ColourPrimaries=%d, "
         "TransferCharacteristics=%d, MatrixCoefficients=%d",
         VideoSignalInfo->VideoFullRange,
         VideoSignalInfo->ColourPrimaries,
         VideoSignalInfo->TransferCharacteristics,
         VideoSignalInfo->MatrixCoefficients);
  } else {
    info("\tVideoSignalInfo not found");
  }

  const mfxU32 bpp = (FI.FourCC == MFX_FOURCC_P010) ? 2 : 1;
  const mfxU32 Align = FI.Width * bpp;
  const mfxU32 Pitch = Align + ((Align % 16) ? (16 - Align % 16) : 0);
  const mfxU32 YSize = Pitch * FI.Height;
  const mfxU32 UVSize = Pitch * (FI.Height / 2);

  for (mfxU16 i = 0; i < QSVSystemMemPoolSize; i++) {
    SystemMemSurface S = {};
    S.Surface.Info = FI;

    mfxU8 *Buffer = new mfxU8[YSize + UVSize];
    S.Surface.Data.Y = Buffer;
    S.Surface.Data.UV = Buffer + YSize;
    S.Surface.Data.Pitch = static_cast<mfxU16>(Pitch);
    QSVSystemMemPool.push_back(S);
  }
}
#endif

#ifdef QSV_UHD600_SUPPORT
void QSVEncoder::ReleaseSystemMemorySurfacePool() {
  for (auto &S : QSVSystemMemPool) {
    delete[] S.Surface.Data.Y;
    S.Surface.Data.Y = nullptr;
    S.Surface.Data.UV = nullptr;
  }
  QSVSystemMemPool.clear();
  QSVSystemMemPoolSize = 0;
}
#endif

mfxStatus QSVEncoder::GetVPLVersion(mfxVersion &Version) {
  mfxStatus Status = MFX_ERR_NONE;

  mfxLoader Loader = nullptr;
  bool IsLocalLoader = false;
  {
    std::lock_guard<std::mutex> Lock(GlobalLoaderMutex);
    Loader = GlobalQSVLoader;
  }

  if (!Loader) {
    Loader = MFXLoad();
    if (Loader == nullptr) {
      throw std::runtime_error("GetVPLSession(): MFXLoad error");
    }
    IsLocalLoader = true;
  }

  Status = MFXCreateSession(Loader, 0, &QSVSession);
  if (Status >= MFX_ERR_NONE) {
    MFXQueryVersion(QSVSession, &QSVVersion);
    Version = QSVVersion;
    MFXClose(QSVSession);
    QSVSession = nullptr;
    if (IsLocalLoader) {
      MFXUnload(Loader);
    }
  } else {
    if (IsLocalLoader) {
      MFXUnload(Loader);
    }
    throw std::runtime_error("GetVPLSession(): MFXCreateSession error");
  }

  return Status;
}

mfxStatus QSVEncoder::CreateSession([[maybe_unused]] enum codec_enum Codec,
                                    [[maybe_unused]] void **Data, int GPUNum) {
  mfxStatus Status = MFX_ERR_NONE;
  bool UsingGlobalLoader = false;

  mfxLoader Loader = nullptr;

  {
    std::lock_guard<std::mutex> Lock(GlobalLoaderMutex);
    if (GlobalQSVLoader) {
      Loader = GlobalQSVLoader;
      UsingGlobalLoader = true;
    }
  }

  auto makeConfig = [&](int idx, mfxU32 value, const char *property) {
    QSVLoaderConfig[idx] = MFXCreateConfig(Loader);
    QSVLoaderVariant[idx].Type = MFX_VARIANT_TYPE_U32;
    QSVLoaderVariant[idx].Data.U32 = value;
    MFXSetConfigFilterProperty(
        QSVLoaderConfig[idx],
        reinterpret_cast<const mfxU8 *>(property),
        QSVLoaderVariant[idx]);
  };

  if (!Loader) {
    QSVLoader = MFXLoad();
    if (QSVLoader == nullptr) {
      return MFX_ERR_UNDEFINED_BEHAVIOR;
    }
    Loader = QSVLoader;

    makeConfig(0, MFX_IMPL_TYPE_HARDWARE, "mfxImplDescription.Impl");
    makeConfig(1, static_cast<mfxU32>(0x8086), "mfxImplDescription.VendorID");

  }

#if defined(_WIN32) || defined(_WIN64)
  if (QSVIsTextureEncoder) {
    makeConfig(3, MFX_ACCEL_MODE_VIA_D3D11, "mfxImplDescription.AccelerationMode");
  }
#endif

  Status = MFXCreateSession(Loader, GPUNum, &QSVSession);

  if (Status < MFX_ERR_NONE) {
    error("Error code: %d", Status);
    throw std::runtime_error("CreateSession(): MFXCreateSession error");
  }

  MFXQueryIMPL(QSVSession, &QSVImpl);

  MFXVideoCORE_QueryPlatform(QSVSession, &QSVPlatform);
  info("\tAdapter type: %s",
       QSVPlatform.MediaAdapterType == MFX_MEDIA_DISCRETE ? "Discrete"
                                                          : "Integrate");

  MFXQueryVersion(QSVSession, &QSVVersion);

  return Status;
}

void QSVEncoder::DisableVPP() {
  QSVProcessing->Close();
  QSVProcessing = nullptr;
  QSVProcessingParams.ClearAllBuffers();
  QSVProcessingEnable = false;
}

// Forward declaration — defined after CO_FIELDS tables
static void LogCO2CO3Corrections(
    const char *Prefix,
    MFXVideoParam &Params,
    const mfxExtCodingOption2 *CO2Before,
    const mfxExtCodingOption3 *CO3Before,
    bool HasCO2, bool HasCO3);

mfxStatus QSVEncoder::InitEncoderInternal(encoder_params *InputParams,
                                          enum codec_enum Codec,
                                          const char *log_prefix) {
  FrameQPStats = {};

  mfxStatus Status = SetEncoderParams(InputParams, Codec);
  info("\tSetEncoderParams%s status:  %d", log_prefix, Status);

  if (Status >= MFX_ERR_NONE) {
    if (!InputParams->CustomCodingOptions.empty()) {
      ParseCustomCodingOptions(InputParams->CustomCodingOptions);
    }

    ApplyQPLimits(InputParams);

    mfxExtCodingOption2 CO2Copy = {};
    mfxExtCodingOption3 CO3Copy = {};
    bool HasCO2 = false, HasCO3 = false;
    if (auto p = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption2>()) {
      CO2Copy = *p;
      HasCO2 = true;
    }
    if (auto p = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>()) {
      CO3Copy = *p;
      HasCO3 = true;
    }

    Status = QSVEncode->Query(&QSVEncodeParams, &QSVEncodeParams);
    info("\tMFXVideoENCODE_Query%s status: %d", log_prefix, Status);

    if (Status == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
      LogCO2CO3Corrections(log_prefix, QSVEncodeParams, &CO2Copy, &CO3Copy,
                           HasCO2, HasCO3);
      Status = MFX_ERR_NONE;
    }

    // When Query returns MFX_ERR_UNSUPPORTED (-3) on older hardware
    // (e.g. UHD 600 / Apollo Lake), the driver may not support Query
    // with extended coding option buffers (CO2/CO3).  Init may still
    // succeed with the same parameters, so we attempt it directly.
    if (Status >= MFX_ERR_NONE || Status == MFX_ERR_UNSUPPORTED) {
      if (Status == MFX_ERR_UNSUPPORTED) {
        warn("MFXVideoENCODE_Query%s returned UNSUPPORTED, "
             "attempting Init directly", log_prefix);
      }

      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s status: %d", log_prefix, Status);
    }
  }

  // Fallback retry chain:
  // On older hardware (especially UHD600), certain ext buffers and
  // parameters may not be supported. Each retry removes one feature
  // and re-attempts Init until it succeeds or all fallbacks are exhausted.

  if (Status < MFX_ERR_NONE) {
    auto CO3Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
    if (CO3Params && CO3Params->ScenarioInfo != 0) {
      warn("MFXVideoENCODE_Init%s failed with ScenarioInfo=%d, retrying without ScenarioInfo",
           log_prefix, CO3Params->ScenarioInfo);
      QSVEncode->Close();
      CO3Params->ScenarioInfo = 0;

      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry (ScenarioInfo) status: %d", log_prefix, Status);
    }
  }

  // Retry without Temporal Layers if Init still failed
  if (Status < MFX_ERR_NONE) {
    auto TemporalLayers =
        QSVEncodeParams.GetExtBuffer<mfxExtTemporalLayers>();
    if (TemporalLayers && TemporalLayers->NumLayers > 0) {
      warn("MFXVideoENCODE_Init%s failed with temporal layers (err=%d, "
           "NumLayers=%d, B-frames=%d, NumRefFrame=%d), "
           "retrying without temporal layers",
           log_prefix, Status, TemporalLayers->NumLayers,
           QSVEncodeParams.mfx.GopRefDist - 1,
           QSVEncodeParams.mfx.NumRefFrame);
      QSVEncode->Close();
      delete[] QSVLayerArray;
      QSVLayerArray = nullptr;
      QSVEncodeParams.RemoveExtBuffer<mfxExtTemporalLayers>();

      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry (TemporalLayers removed) status: %d",
           log_prefix, Status);
    }
  }

  // Retry with simplified CO params if still failed
  if (Status < MFX_ERR_NONE) {
    auto COParams =
        QSVEncodeParams.GetExtBuffer<mfxExtCodingOption>();
    if (COParams) {
      warn("MFXVideoENCODE_Init%s failed, retrying with CO basic", log_prefix);
      QSVEncode->Close();
      COParams->IntraPredBlockSize = MFX_BLOCKSIZE_UNKNOWN;
      COParams->InterPredBlockSize = MFX_BLOCKSIZE_UNKNOWN;
      COParams->MECostType = 0;
      COParams->MESearchType = 0;
      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry (CO basic) status: %d", log_prefix,
           Status);
    }
  }

#ifdef QSV_UHD600_SUPPORT
  if (Status < MFX_ERR_NONE &&
      QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto HevcTiles =
        QSVEncodeParams.GetExtBuffer<mfxExtHEVCTiles>();
    if (HevcTiles) {
      warn("MFXVideoENCODE_Init%s failed (err=%d), "
           "retrying without HEVC tiles",
           log_prefix, Status);
      QSVEncode->Close();
      QSVEncodeParams.RemoveExtBuffer<mfxExtHEVCTiles>();
      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry "
           "(without HEVC tiles) status: %d",
           log_prefix, Status);
    }
  }

  if (Status < MFX_ERR_NONE &&
      QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto HevcParam =
        QSVEncodeParams.GetExtBuffer<mfxExtHEVCParam>();
    if (HevcParam) {
      warn("MFXVideoENCODE_Init%s failed (err=%d), "
           "retrying without HEVC param",
           log_prefix, Status);
      QSVEncode->Close();
      QSVEncodeParams.RemoveExtBuffer<mfxExtHEVCParam>();
      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry "
           "(without HEVC param) status: %d",
           log_prefix, Status);
    }
  }

  if (Status < MFX_ERR_NONE &&
      QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto COParams =
        QSVEncodeParams.GetExtBuffer<mfxExtCodingOptionDDI>();
    if (COParams) {
      warn("MFXVideoENCODE_Init%s failed (err=%d), "
           "retrying without CODDI",
           log_prefix, Status);
      QSVEncode->Close();
      QSVEncodeParams.RemoveExtBuffer<mfxExtCodingOptionDDI>();
      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry "
           "(without CODDI) status: %d",
           log_prefix, Status);
    }
  }

  if (Status < MFX_ERR_NONE &&
      QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto CO3 =
        QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
    if (CO3 && CO3->GPB != 0) {
      warn("MFXVideoENCODE_Init%s failed (err=%d), "
           "retrying with default GPB",
           log_prefix, Status);
      QSVEncode->Close();
      CO3->GPB = 0;
      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry "
           "(default GPB) status: %d",
           log_prefix, Status);
    }
  }

  if (Status < MFX_ERR_NONE &&
      QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    warn("MFXVideoENCODE_Init%s failed (err=%d), "
         "retrying with NumSlice=0",
         log_prefix, Status);
    QSVEncode->Close();
    QSVEncodeParams.mfx.NumSlice = 0;
    Status = QSVEncode->Init(&QSVEncodeParams);
    info("\tMFXVideoENCODE_Init%s retry "
         "(NumSlice=0) status: %d",
         log_prefix, Status);
  }

  // Retry without CodingOption3
  if (Status < MFX_ERR_NONE &&
      QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto CO3 =
        QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
    if (CO3) {
      warn("MFXVideoENCODE_Init%s failed (err=%d), "
           "retrying without CodingOption3",
           log_prefix, Status);
      QSVEncode->Close();
      QSVEncodeParams.RemoveExtBuffer<mfxExtCodingOption3>();
      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry "
           "(without CO3) status: %d",
           log_prefix, Status);
    }
  }

  if (Status < MFX_ERR_NONE &&
      QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto ChromaLoc =
        QSVEncodeParams.GetExtBuffer<mfxExtChromaLocInfo>();
    if (ChromaLoc) {
      warn("MFXVideoENCODE_Init%s failed (err=%d), "
           "retrying without chroma loc info",
           log_prefix, Status);
      QSVEncode->Close();
      QSVEncodeParams.RemoveExtBuffer<mfxExtChromaLocInfo>();
      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init%s retry "
           "(without chroma loc info) status: %d",
           log_prefix, Status);
    }
  }
#endif

  if (Status < MFX_ERR_NONE) {
    QSVEncode->Close();
  }

  return Status;
}

mfxStatus QSVEncoder::Init(encoder_params *InputParams, enum codec_enum Codec,
                           bool bIsTextureEncoder) {
  mfxStatus Status = MFX_ERR_NONE;

  QSVIsTextureEncoder = bIsTextureEncoder;
  QSVProcessingEnable = InputParams->ProcessingEnable;

  info("\tEncoder type: %s",
       QSVIsTextureEncoder ? "Texture import" : "Frame import");
  try {
    if (QSVIsTextureEncoder) {
#if defined(_WIN32) || defined(_WIN64)
      HWManager = std::make_unique<class HWManager>();
#endif
    }

    Status = CreateSession(Codec, nullptr, InputParams->GPUNum);

    QSVEncode = std::make_unique<MFXVideoENCODE>(QSVSession);

    if (QSVProcessingEnable) {
      QSVProcessing = std::make_unique<MFXVideoVPP>(QSVSession);

      Status = SetProcessingParams(InputParams, Codec);
      if (Status >= MFX_ERR_NONE) {
        Status = QSVProcessing->Init(&QSVProcessingParams);
        if (Status < MFX_ERR_NONE) {
          warn("VPP Init failed (%d), disabling VPP for this session", Status);
          DisableVPP();
        }
      } else {
        warn("VPP configuration not supported for FourCC=0x%04X, "
             "disabling VPP for this session",
             InputParams->FourCC);
        DisableVPP();
      }
    }

#ifdef QSV_UHD600_SUPPORT
    QSVUseSystemMemoryPath = false;

    Status = InitEncoderInternal(InputParams, Codec, "");

    if (!QSVIsTextureEncoder) {
      if (Status >= MFX_ERR_NONE) {
        mfxFrameSurface1 *TestSurface = nullptr;
        mfxStatus GetSts = QSVEncode->GetSurface(&TestSurface);
        if (GetSts < MFX_ERR_NONE) {
          info("\tGetSurface() not supported (%d), switch to system memory path",
               GetSts);
          QSVEncode->Close();
          if (QSVProcessingEnable) {
            warn("\tVPP processing disabled: system memory path does not support VPP");
            DisableVPP();
          }
          QSVUseSystemMemoryPath = true;
          Status = InitEncoderInternal(InputParams, Codec, " (sysmem)");
        } else {
          info("\tGetSurface() supported, using original VIDEO_MEMORY path");
          TestSurface->FrameInterface->Release(TestSurface);
        }
      } else {
        info("\tOriginal Init failed (%d), switch to system memory path", Status);
        if (QSVProcessingEnable) {
          warn("\tVPP processing disabled: system memory path does not support VPP");
          DisableVPP();
        }
        QSVUseSystemMemoryPath = true;
        Status = InitEncoderInternal(InputParams, Codec, " (sysmem)");
      }
    }

    if (Status < MFX_ERR_NONE) {
      error("MFXVideoENCODE_Init failed after all retries (Status=%d)",
            Status);
      throw std::runtime_error(
          "Init(): MFXVideoENCODE_Init error after parameter retries");
    }

    Status = InitTexturePool();
    info("\tInitTexturePool status:   %d", Status);

    Status = GetVideoParam(Codec);
    LogActualParams();

    if (QSVUseSystemMemoryPath) {
      InitSystemMemorySurfacePool();
    }
#else
    Status = InitEncoderInternal(InputParams, Codec, "");

    if (Status < MFX_ERR_NONE) {
      error("MFXVideoENCODE_Init failed (Status=%d)", Status);
      return Status;
    }

    Status = InitTexturePool();
    info("\tInitTexturePool status:   %d", Status);

    Status = GetVideoParam(Codec);
    LogActualParams();
#endif

    Status = InitBitstreamBuffer(Codec);

    Status = InitTaskPool(Codec);
  } catch (const std::exception &e) {
    error("Error code: %d. %s", Status, e.what());
    throw;
  }

  HWManager::HWEncoderCounter++;

  // Pre-warm the GPU encoder pipeline so the first real frame does not
  // pay the driver-internal initialization cost (shader compilation,
  // command-buffer allocation, etc.) which manifests as a visible stutter.
  try {
    WarmUpEncoder();
  } catch (const std::exception &e) {
    warn("Encoder warm-up skipped: %s", e.what());
  }

  return Status;
}

mfxStatus
QSVEncoder::SetProcessingParams(struct encoder_params *InputParams,
                                [[maybe_unused]] enum codec_enum Codec) {
  QSVProcessingParams.vpp.In.FourCC = static_cast<mfxU32>(InputParams->FourCC);
  QSVProcessingParams.vpp.In.ChromaFormat =
      static_cast<mfxU16>(InputParams->ChromaFormat);
  QSVProcessingParams.vpp.In.Width =
      static_cast<mfxU16>((((InputParams->Width + 15) >> 4) << 4));
  QSVProcessingParams.vpp.In.Height =
      static_cast<mfxU16>((((InputParams->Height + 15) >> 4) << 4));
  QSVProcessingParams.vpp.In.CropW = static_cast<mfxU16>(InputParams->Width);
  QSVProcessingParams.vpp.In.CropH = static_cast<mfxU16>(InputParams->Height);
  QSVProcessingParams.vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  QSVProcessingParams.vpp.In.FrameRateExtN =
      static_cast<mfxU32>(InputParams->FpsNum);
  QSVProcessingParams.vpp.In.FrameRateExtD =
      static_cast<mfxU32>(InputParams->FpsDen);

  QSVProcessingParams.vpp.Out.FourCC = static_cast<mfxU32>(InputParams->FourCC);
  QSVProcessingParams.vpp.Out.ChromaFormat =
      static_cast<mfxU16>(InputParams->ChromaFormat);
  mfxU16 vppOutWidth = InputParams->VPPOutWidth > 0
      ? InputParams->VPPOutWidth.value()
      : InputParams->Width;
  mfxU16 vppOutHeight = InputParams->VPPOutHeight > 0
      ? InputParams->VPPOutHeight.value()
      : InputParams->Height;

  QSVProcessingParams.vpp.Out.Width =
      static_cast<mfxU16>((((vppOutWidth + 15) >> 4) << 4));
  QSVProcessingParams.vpp.Out.Height =
      static_cast<mfxU16>((((vppOutHeight + 15) >> 4) << 4));
  QSVProcessingParams.vpp.Out.CropW = static_cast<mfxU16>(vppOutWidth);
  QSVProcessingParams.vpp.Out.CropH = static_cast<mfxU16>(vppOutHeight);
  QSVProcessingParams.vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  QSVProcessingParams.vpp.Out.FrameRateExtN =
      static_cast<mfxU32>(InputParams->FpsNum);
  QSVProcessingParams.vpp.Out.FrameRateExtD =
      static_cast<mfxU32>(InputParams->FpsDen);

  // Propagate bit-depth and shift to VPP frame info (the encoder init already
  // does this for the encode params, but VPP frame info was left zeroed).
  // Without these fields the VPP Query may reject 10-bit+ FourCC values
  // (e.g. P010) because the SDK sees an inconsistent format description:
  // FourCC implies 10-bit but BitDepth/Shift still read 0 (8-bit, no shift).
  if (InputParams->BitDepth > 0) {
    auto setBitDepth = [&](mfxFrameInfo &fi) {
      fi.BitDepthLuma   = InputParams->BitDepth;
      fi.BitDepthChroma = InputParams->BitDepth;
      fi.Shift          = InputParams->BitDepth > 8 ? 1 : 0;
    };
    setBitDepth(QSVProcessingParams.vpp.In);
    setBitDepth(QSVProcessingParams.vpp.Out);
  }

  if (InputParams->VPPDenoiseMode.has_value()) {
    auto DenoiseParams = QSVProcessingParams.AddExtBuffer<mfxExtVPPDenoise2>();
    DenoiseParams->Header.BufferId = MFX_EXTBUFF_VPP_DENOISE2;
    DenoiseParams->Header.BufferSz = sizeof(mfxExtVPPDenoise2);
    switch (InputParams->VPPDenoiseMode.value()) {
    case 1:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_AUTO_BDRATE;
      break;
    case 2:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_AUTO_ADJUST;
      break;
    case 3:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_AUTO_SUBJECTIVE;
      break;
    case 4:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_PRE_MANUAL;
      DenoiseParams->Strength =
          static_cast<mfxU16>(InputParams->DenoiseStrength);
      break;
    case 5:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_POST_MANUAL;
      DenoiseParams->Strength =
          static_cast<mfxU16>(InputParams->DenoiseStrength);
      break;
    default:
      DenoiseParams->Mode = MFX_DENOISE_MODE_DEFAULT;
      break;
    }
  }

  if (InputParams->VPPDetail.has_value()) {
    auto DetailParams = QSVProcessingParams.AddExtBuffer<mfxExtVPPDetail>();
    DetailParams->Header.BufferId = MFX_EXTBUFF_VPP_DETAIL;
    DetailParams->Header.BufferSz = sizeof(mfxExtVPPDetail);
    DetailParams->DetailFactor =
        static_cast<mfxU16>(InputParams->VPPDetail.value());
  }

  if (InputParams->VPPScalingMode.has_value()) {
    auto ScalingParams = QSVProcessingParams.AddExtBuffer<mfxExtVPPScaling>();
    ScalingParams->Header.BufferId = MFX_EXTBUFF_VPP_SCALING;
    ScalingParams->Header.BufferSz = sizeof(mfxExtVPPScaling);
    switch (InputParams->VPPScalingMode.value()) {
    case 1:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_QUALITY;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_ADVANCED;
      break;
    case 2:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_INTEL_GEN_VEBOX;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_ADVANCED;
      break;
    case 3:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_LOWPOWER;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_NEAREST_NEIGHBOR;
      break;
    case 4:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_LOWPOWER;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_ADVANCED;
      break;
    default:
      break;
    }
  }

  if (InputParams->VPPImageStabMode.has_value()) {
    auto ImageStabParams =
        QSVProcessingParams.AddExtBuffer<mfxExtVPPImageStab>();
    ImageStabParams->Header.BufferId = MFX_EXTBUFF_VPP_IMAGE_STABILIZATION;
    ImageStabParams->Header.BufferSz = sizeof(mfxExtVPPImageStab);
    switch (InputParams->VPPImageStabMode.value()) {
    case 1:
      ImageStabParams->Mode = MFX_IMAGESTAB_MODE_UPSCALE;
      break;
    case 2:
      ImageStabParams->Mode = MFX_IMAGESTAB_MODE_BOXING;
      break;
    default:
      break;
    }
  }

  if (InputParams->PercEncPrefilter == true) {
    auto PercEncPrefilterParams =
        QSVProcessingParams.AddExtBuffer<mfxExtVPPPercEncPrefilter>();
    PercEncPrefilterParams->Header.BufferId =
        MFX_EXTBUFF_VPP_PERC_ENC_PREFILTER;
    PercEncPrefilterParams->Header.BufferSz = sizeof(mfxExtVPPPercEncPrefilter);
  }

  if (InputParams->VPPMCTFMode.has_value() && InputParams->VPPMCTFMode.value() == 1) {
    auto MCTFParams = QSVProcessingParams.AddExtBuffer<mfxExtVppMctf>();
    MCTFParams->Header.BufferId = MFX_EXTBUFF_VPP_MCTF;
    MCTFParams->Header.BufferSz = sizeof(mfxExtVppMctf);
    MCTFParams->FilterStrength = static_cast<mfxU16>(InputParams->VPPMCTFStrength);
  }

  QSVProcessingParams.IOPattern =
      MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;

  mfxVideoParam ValidParams = {};
  memcpy(&ValidParams, &QSVProcessingParams, sizeof(mfxVideoParam));
  mfxStatus Status = QSVProcessing->Query(&QSVProcessingParams, &ValidParams);
  if (Status < MFX_ERR_NONE) {
    error("SetProcessingParams(): VPP Query returned %d for FourCC=0x%04X "
          "BitDepth=%u",
          Status, InputParams->FourCC, InputParams->BitDepth);
    return Status;
  }

  QSVProcessingAuxData = QSVEncodeCtrlParams.AddExtBuffer<mfxExtVppAuxData>();

  return Status;
}

static mfxU16 ParseCodingOptionValue(std::string_view Val) {
  if (Val == "ON" || Val == "on")
    return MFX_CODINGOPTION_ON;
  if (Val == "OFF" || Val == "off")
    return MFX_CODINGOPTION_OFF;
  if (Val == "UNKNOWN" || Val == "unknown")
    return MFX_CODINGOPTION_UNKNOWN;
  return static_cast<mfxU16>(std::stoul(std::string(Val)));
}

static bool iequals(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, [](unsigned char ca, unsigned char cb) {
        return std::tolower(ca) == std::tolower(cb);
    });
}

static std::string FormatFieldValue([[maybe_unused]] std::string_view Field,
                                    mfxU64 Value,
                                    std::string_view RawVal) {
    for (std::string_view keyword : {"ON", "OFF", "UNKNOWN"})
        if (iequals(RawVal, keyword)) return std::string(keyword);
    return std::to_string(Value);
}

// Trim whitespace from both ends of a string
static void TrimWhitespace(std::string &s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.pop_back();
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.erase(0, 1);
}

enum FieldType : uint8_t {
  FT_U16,
  FT_S16,
  FT_U8,
  FT_U32,
};

struct FieldEntry {
  const char *name;
  size_t offset;
  FieldType type;
};

// Read a field value from a struct at the given offset, returned as mfxU64
static mfxU64 ReadFieldValue(const void *base, const FieldEntry &entry) {
  const void *ptr = reinterpret_cast<const char *>(base) + entry.offset;
  switch (entry.type) {
  case FT_U16: return *static_cast<const mfxU16 *>(ptr);
  case FT_S16: return static_cast<mfxU64>(*static_cast<const mfxI16 *>(ptr));
  case FT_U8:  return *static_cast<const mfxU8 *>(ptr);
  case FT_U32: return *static_cast<const mfxU32 *>(ptr);
  }
  return 0;
}

static constexpr std::array<FieldEntry, 25> CO_FIELDS{
  FieldEntry{"RateDistortionOpt", offsetof(mfxExtCodingOption, RateDistortionOpt), FT_U16},
  FieldEntry{"MECostType", offsetof(mfxExtCodingOption, MECostType), FT_U16},
  FieldEntry{"MESearchType", offsetof(mfxExtCodingOption, MESearchType), FT_U16},
  FieldEntry{"MVSearchWindow.x", offsetof(mfxExtCodingOption, MVSearchWindow), FT_S16},
  FieldEntry{"MVSearchWindow.y", offsetof(mfxExtCodingOption, MVSearchWindow) + 2, FT_S16},
  FieldEntry{"EndOfSequence", offsetof(mfxExtCodingOption, EndOfSequence), FT_U16},
  FieldEntry{"FramePicture", offsetof(mfxExtCodingOption, FramePicture), FT_U16},
  FieldEntry{"CAVLC", offsetof(mfxExtCodingOption, CAVLC), FT_U16},
  FieldEntry{"RecoveryPointSEI", offsetof(mfxExtCodingOption, RecoveryPointSEI), FT_U16},
  FieldEntry{"ViewOutput", offsetof(mfxExtCodingOption, ViewOutput), FT_U16},
  FieldEntry{"NalHrdConformance", offsetof(mfxExtCodingOption, NalHrdConformance), FT_U16},
  FieldEntry{"SingleSeiNalUnit", offsetof(mfxExtCodingOption, SingleSeiNalUnit), FT_U16},
  FieldEntry{"VuiVclHrdParameters", offsetof(mfxExtCodingOption, VuiVclHrdParameters), FT_U16},
  FieldEntry{"RefPicListReordering", offsetof(mfxExtCodingOption, RefPicListReordering), FT_U16},
  FieldEntry{"ResetRefList", offsetof(mfxExtCodingOption, ResetRefList), FT_U16},
  FieldEntry{"RefPicMarkRep", offsetof(mfxExtCodingOption, RefPicMarkRep), FT_U16},
  FieldEntry{"FieldOutput", offsetof(mfxExtCodingOption, FieldOutput), FT_U16},
  FieldEntry{"IntraPredBlockSize", offsetof(mfxExtCodingOption, IntraPredBlockSize), FT_U16},
  FieldEntry{"InterPredBlockSize", offsetof(mfxExtCodingOption, InterPredBlockSize), FT_U16},
  FieldEntry{"MVPrecision", offsetof(mfxExtCodingOption, MVPrecision), FT_U16},
  FieldEntry{"MaxDecFrameBuffering", offsetof(mfxExtCodingOption, MaxDecFrameBuffering), FT_U16},
  FieldEntry{"AUDelimiter", offsetof(mfxExtCodingOption, AUDelimiter), FT_U16},
  FieldEntry{"EndOfStream", offsetof(mfxExtCodingOption, EndOfStream), FT_U16},
  FieldEntry{"PicTimingSEI", offsetof(mfxExtCodingOption, PicTimingSEI), FT_U16},
  FieldEntry{"VuiNalHrdParameters", offsetof(mfxExtCodingOption, VuiNalHrdParameters), FT_U16},
};

static constexpr std::array<FieldEntry, 29> CO2_FIELDS{
  FieldEntry{"IntRefType", offsetof(mfxExtCodingOption2, IntRefType), FT_U16},
  FieldEntry{"IntRefCycleSize", offsetof(mfxExtCodingOption2, IntRefCycleSize), FT_U16},
  FieldEntry{"IntRefQPDelta", offsetof(mfxExtCodingOption2, IntRefQPDelta), FT_S16},
  FieldEntry{"MaxFrameSize", offsetof(mfxExtCodingOption2, MaxFrameSize), FT_U32},
  FieldEntry{"MaxSliceSize", offsetof(mfxExtCodingOption2, MaxSliceSize), FT_U32},
  FieldEntry{"BitrateLimit", offsetof(mfxExtCodingOption2, BitrateLimit), FT_U16},
  FieldEntry{"MBBRC", offsetof(mfxExtCodingOption2, MBBRC), FT_U16},
  FieldEntry{"LookAheadDepth", offsetof(mfxExtCodingOption2, LookAheadDepth), FT_U16},
  FieldEntry{"Trellis", offsetof(mfxExtCodingOption2, Trellis), FT_U16},
  FieldEntry{"RepeatPPS", offsetof(mfxExtCodingOption2, RepeatPPS), FT_U16},
  FieldEntry{"BRefType", offsetof(mfxExtCodingOption2, BRefType), FT_U16},
  FieldEntry{"AdaptiveI", offsetof(mfxExtCodingOption2, AdaptiveI), FT_U16},
  FieldEntry{"AdaptiveB", offsetof(mfxExtCodingOption2, AdaptiveB), FT_U16},
  FieldEntry{"LookAheadDS", offsetof(mfxExtCodingOption2, LookAheadDS), FT_U16},
  FieldEntry{"NumMbPerSlice", offsetof(mfxExtCodingOption2, NumMbPerSlice), FT_U16},
  FieldEntry{"SkipFrame", offsetof(mfxExtCodingOption2, SkipFrame), FT_U16},
  FieldEntry{"MinQPI", offsetof(mfxExtCodingOption2, MinQPI), FT_U8},
  FieldEntry{"MaxQPI", offsetof(mfxExtCodingOption2, MaxQPI), FT_U8},
  FieldEntry{"MinQPP", offsetof(mfxExtCodingOption2, MinQPP), FT_U8},
  FieldEntry{"MaxQPP", offsetof(mfxExtCodingOption2, MaxQPP), FT_U8},
  FieldEntry{"MinQPB", offsetof(mfxExtCodingOption2, MinQPB), FT_U8},
  FieldEntry{"MaxQPB", offsetof(mfxExtCodingOption2, MaxQPB), FT_U8},
  FieldEntry{"FixedFrameRate", offsetof(mfxExtCodingOption2, FixedFrameRate), FT_U16},
  FieldEntry{"DisableDeblockingIdc", offsetof(mfxExtCodingOption2, DisableDeblockingIdc), FT_U16},
  FieldEntry{"DisableVUI", offsetof(mfxExtCodingOption2, DisableVUI), FT_U16},
  FieldEntry{"BufferingPeriodSEI", offsetof(mfxExtCodingOption2, BufferingPeriodSEI), FT_U16},
  FieldEntry{"EnableMAD", offsetof(mfxExtCodingOption2, EnableMAD), FT_U16},
  FieldEntry{"UseRawRef", offsetof(mfxExtCodingOption2, UseRawRef), FT_U16},
};

static constexpr std::array<FieldEntry, 76> CO3_FIELDS{
  FieldEntry{"NumSliceI", offsetof(mfxExtCodingOption3, NumSliceI), FT_U16},
  FieldEntry{"NumSliceP", offsetof(mfxExtCodingOption3, NumSliceP), FT_U16},
  FieldEntry{"NumSliceB", offsetof(mfxExtCodingOption3, NumSliceB), FT_U16},
  FieldEntry{"WinBRCMaxAvgKbps", offsetof(mfxExtCodingOption3, WinBRCMaxAvgKbps), FT_U16},
  FieldEntry{"WinBRCSize", offsetof(mfxExtCodingOption3, WinBRCSize), FT_U16},
  FieldEntry{"QVBRQuality", offsetof(mfxExtCodingOption3, QVBRQuality), FT_U16},
  FieldEntry{"EnableMBQP", offsetof(mfxExtCodingOption3, EnableMBQP), FT_U16},
  FieldEntry{"IntRefCycleDist", offsetof(mfxExtCodingOption3, IntRefCycleDist), FT_U16},
  FieldEntry{"DirectBiasAdjustment", offsetof(mfxExtCodingOption3, DirectBiasAdjustment), FT_U16},
  FieldEntry{"GlobalMotionBiasAdjustment", offsetof(mfxExtCodingOption3, GlobalMotionBiasAdjustment), FT_U16},
  FieldEntry{"MVCostScalingFactor", offsetof(mfxExtCodingOption3, MVCostScalingFactor), FT_U16},
  FieldEntry{"MBDisableSkipMap", offsetof(mfxExtCodingOption3, MBDisableSkipMap), FT_U16},
  FieldEntry{"WeightedPred", offsetof(mfxExtCodingOption3, WeightedPred), FT_U16},
  FieldEntry{"WeightedBiPred", offsetof(mfxExtCodingOption3, WeightedBiPred), FT_U16},
  FieldEntry{"AspectRatioInfoPresent", offsetof(mfxExtCodingOption3, AspectRatioInfoPresent), FT_U16},
  FieldEntry{"OverscanInfoPresent", offsetof(mfxExtCodingOption3, OverscanInfoPresent), FT_U16},
  FieldEntry{"OverscanAppropriate", offsetof(mfxExtCodingOption3, OverscanAppropriate), FT_U16},
  FieldEntry{"TimingInfoPresent", offsetof(mfxExtCodingOption3, TimingInfoPresent), FT_U16},
  FieldEntry{"BitstreamRestriction", offsetof(mfxExtCodingOption3, BitstreamRestriction), FT_U16},
  FieldEntry{"LowDelayHrd", offsetof(mfxExtCodingOption3, LowDelayHrd), FT_U16},
  FieldEntry{"MotionVectorsOverPicBoundaries", offsetof(mfxExtCodingOption3, MotionVectorsOverPicBoundaries), FT_U16},
  FieldEntry{"ScenarioInfo", offsetof(mfxExtCodingOption3, ScenarioInfo), FT_U16},
  FieldEntry{"ContentInfo", offsetof(mfxExtCodingOption3, ContentInfo), FT_U16},
  FieldEntry{"PRefType", offsetof(mfxExtCodingOption3, PRefType), FT_U16},
  FieldEntry{"FadeDetection", offsetof(mfxExtCodingOption3, FadeDetection), FT_U16},
  FieldEntry{"GPB", offsetof(mfxExtCodingOption3, GPB), FT_U16},
  FieldEntry{"MaxFrameSizeI", offsetof(mfxExtCodingOption3, MaxFrameSizeI), FT_U32},
  FieldEntry{"MaxFrameSizeP", offsetof(mfxExtCodingOption3, MaxFrameSizeP), FT_U32},
  FieldEntry{"EnableQPOffset", offsetof(mfxExtCodingOption3, EnableQPOffset), FT_U16},
  FieldEntry{"QPOffset[0]", offsetof(mfxExtCodingOption3, QPOffset), FT_S16},
  FieldEntry{"QPOffset[1]", offsetof(mfxExtCodingOption3, QPOffset) + 1 * 2, FT_S16},
  FieldEntry{"QPOffset[2]", offsetof(mfxExtCodingOption3, QPOffset) + 2 * 2, FT_S16},
  FieldEntry{"QPOffset[3]", offsetof(mfxExtCodingOption3, QPOffset) + 3 * 2, FT_S16},
  FieldEntry{"QPOffset[4]", offsetof(mfxExtCodingOption3, QPOffset) + 4 * 2, FT_S16},
  FieldEntry{"QPOffset[5]", offsetof(mfxExtCodingOption3, QPOffset) + 5 * 2, FT_S16},
  FieldEntry{"QPOffset[6]", offsetof(mfxExtCodingOption3, QPOffset) + 6 * 2, FT_S16},
  FieldEntry{"QPOffset[7]", offsetof(mfxExtCodingOption3, QPOffset) + 7 * 2, FT_S16},
  FieldEntry{"NumRefActiveP[0]", offsetof(mfxExtCodingOption3, NumRefActiveP), FT_U16},
  FieldEntry{"NumRefActiveP[1]", offsetof(mfxExtCodingOption3, NumRefActiveP) + 1 * 2, FT_U16},
  FieldEntry{"NumRefActiveP[2]", offsetof(mfxExtCodingOption3, NumRefActiveP) + 2 * 2, FT_U16},
  FieldEntry{"NumRefActiveP[3]", offsetof(mfxExtCodingOption3, NumRefActiveP) + 3 * 2, FT_U16},
  FieldEntry{"NumRefActiveP[4]", offsetof(mfxExtCodingOption3, NumRefActiveP) + 4 * 2, FT_U16},
  FieldEntry{"NumRefActiveP[5]", offsetof(mfxExtCodingOption3, NumRefActiveP) + 5 * 2, FT_U16},
  FieldEntry{"NumRefActiveP[6]", offsetof(mfxExtCodingOption3, NumRefActiveP) + 6 * 2, FT_U16},
  FieldEntry{"NumRefActiveP[7]", offsetof(mfxExtCodingOption3, NumRefActiveP) + 7 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL0[0]", offsetof(mfxExtCodingOption3, NumRefActiveBL0), FT_U16},
  FieldEntry{"NumRefActiveBL0[1]", offsetof(mfxExtCodingOption3, NumRefActiveBL0) + 1 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL0[2]", offsetof(mfxExtCodingOption3, NumRefActiveBL0) + 2 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL0[3]", offsetof(mfxExtCodingOption3, NumRefActiveBL0) + 3 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL0[4]", offsetof(mfxExtCodingOption3, NumRefActiveBL0) + 4 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL0[5]", offsetof(mfxExtCodingOption3, NumRefActiveBL0) + 5 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL0[6]", offsetof(mfxExtCodingOption3, NumRefActiveBL0) + 6 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL0[7]", offsetof(mfxExtCodingOption3, NumRefActiveBL0) + 7 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL1[0]", offsetof(mfxExtCodingOption3, NumRefActiveBL1), FT_U16},
  FieldEntry{"NumRefActiveBL1[1]", offsetof(mfxExtCodingOption3, NumRefActiveBL1) + 1 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL1[2]", offsetof(mfxExtCodingOption3, NumRefActiveBL1) + 2 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL1[3]", offsetof(mfxExtCodingOption3, NumRefActiveBL1) + 3 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL1[4]", offsetof(mfxExtCodingOption3, NumRefActiveBL1) + 4 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL1[5]", offsetof(mfxExtCodingOption3, NumRefActiveBL1) + 5 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL1[6]", offsetof(mfxExtCodingOption3, NumRefActiveBL1) + 6 * 2, FT_U16},
  FieldEntry{"NumRefActiveBL1[7]", offsetof(mfxExtCodingOption3, NumRefActiveBL1) + 7 * 2, FT_U16},
  FieldEntry{"TransformSkip", offsetof(mfxExtCodingOption3, TransformSkip), FT_U16},
  FieldEntry{"TargetChromaFormatPlus1", offsetof(mfxExtCodingOption3, TargetChromaFormatPlus1), FT_U16},
  FieldEntry{"TargetBitDepthLuma", offsetof(mfxExtCodingOption3, TargetBitDepthLuma), FT_U16},
  FieldEntry{"TargetBitDepthChroma", offsetof(mfxExtCodingOption3, TargetBitDepthChroma), FT_U16},
  FieldEntry{"BRCPanicMode", offsetof(mfxExtCodingOption3, BRCPanicMode), FT_U16},
  FieldEntry{"LowDelayBRC", offsetof(mfxExtCodingOption3, LowDelayBRC), FT_U16},
  FieldEntry{"EnableMBForceIntra", offsetof(mfxExtCodingOption3, EnableMBForceIntra), FT_U16},
  FieldEntry{"AdaptiveMaxFrameSize", offsetof(mfxExtCodingOption3, AdaptiveMaxFrameSize), FT_U16},
  FieldEntry{"RepartitionCheckEnable", offsetof(mfxExtCodingOption3, RepartitionCheckEnable), FT_U16},
  FieldEntry{"EncodedUnitsInfo", offsetof(mfxExtCodingOption3, EncodedUnitsInfo), FT_U16},
  FieldEntry{"EnableNalUnitType", offsetof(mfxExtCodingOption3, EnableNalUnitType), FT_U16},
  FieldEntry{"ExtBrcAdaptiveLTR", offsetof(mfxExtCodingOption3, ExtBrcAdaptiveLTR), FT_U16},
  FieldEntry{"AdaptiveLTR", offsetof(mfxExtCodingOption3, AdaptiveLTR), FT_U16},
  FieldEntry{"AdaptiveCQM", offsetof(mfxExtCodingOption3, AdaptiveCQM), FT_U16},
  FieldEntry{"AdaptiveRef", offsetof(mfxExtCodingOption3, AdaptiveRef), FT_U16},
};

static constexpr std::array<FieldEntry, 46> CODDI_FIELDS{
  FieldEntry{"IntraPredCostType", offsetof(mfxExtCodingOptionDDI, IntraPredCostType), FT_U16},
  FieldEntry{"MEInterpolationMethod", offsetof(mfxExtCodingOptionDDI, MEInterpolationMethod), FT_U16},
  FieldEntry{"MEFractionalSearchType", offsetof(mfxExtCodingOptionDDI, MEFractionalSearchType), FT_U16},
  FieldEntry{"MaxMVs", offsetof(mfxExtCodingOptionDDI, MaxMVs), FT_U16},
  FieldEntry{"SkipCheck", offsetof(mfxExtCodingOptionDDI, SkipCheck), FT_U16},
  FieldEntry{"DirectCheck", offsetof(mfxExtCodingOptionDDI, DirectCheck), FT_U16},
  FieldEntry{"BiDirSearch", offsetof(mfxExtCodingOptionDDI, BiDirSearch), FT_U16},
  FieldEntry{"MBAFF", offsetof(mfxExtCodingOptionDDI, MBAFF), FT_U16},
  FieldEntry{"FieldPrediction", offsetof(mfxExtCodingOptionDDI, FieldPrediction), FT_U16},
  FieldEntry{"RefOppositeField", offsetof(mfxExtCodingOptionDDI, RefOppositeField), FT_U16},
  FieldEntry{"ChromaInME", offsetof(mfxExtCodingOptionDDI, ChromaInME), FT_U16},
  FieldEntry{"WeightedPrediction", offsetof(mfxExtCodingOptionDDI, WeightedPrediction), FT_U16},
  FieldEntry{"MVPrediction", offsetof(mfxExtCodingOptionDDI, MVPrediction), FT_U16},
  FieldEntry{"DDI.IntraPredBlockSize", offsetof(mfxExtCodingOptionDDI, DDI.IntraPredBlockSize), FT_U16},
  FieldEntry{"DDI.InterPredBlockSize", offsetof(mfxExtCodingOptionDDI, DDI.InterPredBlockSize), FT_U16},
  FieldEntry{"BRCPrecision", offsetof(mfxExtCodingOptionDDI, BRCPrecision), FT_U16},
  FieldEntry{"RefRaw", offsetof(mfxExtCodingOptionDDI, RefRaw), FT_U16},
  FieldEntry{"ConstQP", offsetof(mfxExtCodingOptionDDI, ConstQP), FT_U16},
  FieldEntry{"GlobalSearch", offsetof(mfxExtCodingOptionDDI, GlobalSearch), FT_U16},
  FieldEntry{"LocalSearch", offsetof(mfxExtCodingOptionDDI, LocalSearch), FT_U16},
  FieldEntry{"EarlySkip", offsetof(mfxExtCodingOptionDDI, EarlySkip), FT_U16},
  FieldEntry{"LaScaleFactor", offsetof(mfxExtCodingOptionDDI, LaScaleFactor), FT_U16},
  FieldEntry{"IBC", offsetof(mfxExtCodingOptionDDI, IBC), FT_U16},
  FieldEntry{"Palette", offsetof(mfxExtCodingOptionDDI, Palette), FT_U16},
  FieldEntry{"StrengthN", offsetof(mfxExtCodingOptionDDI, StrengthN), FT_U16},
  FieldEntry{"FractionalQP", offsetof(mfxExtCodingOptionDDI, FractionalQP), FT_U16},
  FieldEntry{"NumActiveRefP", offsetof(mfxExtCodingOptionDDI, NumActiveRefP), FT_U16},
  FieldEntry{"NumActiveRefBL0", offsetof(mfxExtCodingOptionDDI, NumActiveRefBL0), FT_U16},
  FieldEntry{"DisablePSubMBPartition", offsetof(mfxExtCodingOptionDDI, DisablePSubMBPartition), FT_U16},
  FieldEntry{"DisableBSubMBPartition", offsetof(mfxExtCodingOptionDDI, DisableBSubMBPartition), FT_U16},
  FieldEntry{"WeightedBiPredIdc", offsetof(mfxExtCodingOptionDDI, WeightedBiPredIdc), FT_U16},
  FieldEntry{"DirectSpatialMvPredFlag", offsetof(mfxExtCodingOptionDDI, DirectSpatialMvPredFlag), FT_U16},
  FieldEntry{"Transform8x8Mode", offsetof(mfxExtCodingOptionDDI, Transform8x8Mode), FT_U16},
  FieldEntry{"LongStartCodes", offsetof(mfxExtCodingOptionDDI, LongStartCodes), FT_U16},
  FieldEntry{"CabacInitIdcPlus1", offsetof(mfxExtCodingOptionDDI, CabacInitIdcPlus1), FT_U16},
  FieldEntry{"NumActiveRefBL1", offsetof(mfxExtCodingOptionDDI, NumActiveRefBL1), FT_U16},
  FieldEntry{"QpUpdateRange", offsetof(mfxExtCodingOptionDDI, QpUpdateRange), FT_U16},
  FieldEntry{"RegressionWindow", offsetof(mfxExtCodingOptionDDI, RegressionWindow), FT_U16},
  FieldEntry{"LookAheadDependency", offsetof(mfxExtCodingOptionDDI, LookAheadDependency), FT_U16},
  FieldEntry{"Hme", offsetof(mfxExtCodingOptionDDI, Hme), FT_U16},
  FieldEntry{"WriteIVFHeaders", offsetof(mfxExtCodingOptionDDI, WriteIVFHeaders), FT_U16},
  FieldEntry{"RefreshFrameContext", offsetof(mfxExtCodingOptionDDI, RefreshFrameContext), FT_U16},
  FieldEntry{"ChangeFrameContextIdxForTS", offsetof(mfxExtCodingOptionDDI, ChangeFrameContextIdxForTS), FT_U16},
  FieldEntry{"SuperFrameForTS", offsetof(mfxExtCodingOptionDDI, SuperFrameForTS), FT_U16},
  FieldEntry{"QpAdjust", offsetof(mfxExtCodingOptionDDI, QpAdjust), FT_U16},
  FieldEntry{"TMVP", offsetof(mfxExtCodingOptionDDI, TMVP), FT_U16},
};

// Log driver-corrected fields by diffing before/after state using field tables.
// Only emits output when the driver actually changed something.
static void LogCO2CO3Corrections(
    const char *Prefix,
    MFXVideoParam &Params,
    const mfxExtCodingOption2 *CO2Before,
    const mfxExtCodingOption3 *CO3Before,
    bool HasCO2, bool HasCO3) {
  auto CO2After = Params.GetExtBuffer<mfxExtCodingOption2>();
  auto CO3After = Params.GetExtBuffer<mfxExtCodingOption3>();

  // Collect diffs first; skip the header if nothing changed
  std::vector<std::string> diffs;
  auto collectDiffs = [&](const void *before, const void *after,
                           std::span<const FieldEntry> fields) {
    if (!before || !after) return;
    for (const auto &f : fields) {
      mfxU64 bv = ReadFieldValue(before, f);
      mfxU64 av = ReadFieldValue(after, f);
      if (bv != av) {
        char buf[128];
        snprintf(buf, sizeof(buf), "\t  %s: %llu -> %llu", f.name, bv, av);
        diffs.push_back(buf);
      }
    }
  };
  collectDiffs(CO2Before, CO2After, CO2_FIELDS);
  collectDiffs(CO3Before, CO3After, CO3_FIELDS);

  if (diffs.empty()) return;

  info("\tDriver auto-corrected parameters%s:", Prefix);
  for (const auto &d : diffs)
    info("%s", d.c_str());
}

static std::optional<mfxU64> ApplyField(void *base, std::span<const FieldEntry> entries,
                                        const std::string &field, const std::string &val) {
  for (const auto &e : entries) {
    if (field != e.name)
      continue;
    void *ptr = reinterpret_cast<char *>(base) + e.offset;
    mfxU64 parsed = ParseCodingOptionValue(val);
    switch (e.type) {
    case FT_U16:
      *reinterpret_cast<mfxU16 *>(ptr) = static_cast<mfxU16>(parsed);
      break;
    case FT_S16:
      *reinterpret_cast<mfxI16 *>(ptr) = static_cast<mfxI16>(std::stoi(val));
      break;
    case FT_U8:
      *reinterpret_cast<mfxU8 *>(ptr) = static_cast<mfxU8>(parsed);
      break;
    case FT_U32:
      *reinterpret_cast<mfxU32 *>(ptr) = static_cast<mfxU32>(std::stoul(val));
      break;
    }
    // Read back the actual value stored in the struct (may differ from parsed
    // due to type truncation, e.g. mfxU16 truncating mfxU64 > 65535)
    return ReadFieldValue(base, e);
  }
  return std::nullopt;
}

void QSVEncoder::ParseCustomCodingOptions(const std::string &Options) {
  m_CustomCodingOptions.clear();

  auto *COParams = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption>();
  auto *CO2Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption2>();
  auto *CO3Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
  auto *CODDIParams = QSVEncodeParams.GetExtBuffer<mfxExtCodingOptionDDI>();

  std::istringstream Stream(Options);
  std::string Line;
  int LineNo = 0;

  while (std::getline(Stream, Line)) {
    LineNo++;
    TrimWhitespace(Line);

    if (Line.empty() || Line[0] == '#' || Line[0] == ';')
      continue;

    size_t EqualPos = Line.find('=');
    if (EqualPos == std::string::npos) {
      warn("\tCustomCodingOptions line %d: missing '=', skipping: %s", LineNo,
           Line.c_str());
      continue;
    }

    std::string Key = Line.substr(0, EqualPos);
    std::string Val = Line.substr(EqualPos + 1);
    TrimWhitespace(Key);
    TrimWhitespace(Val);

    size_t DotPos = Key.find('.');
    if (DotPos == std::string::npos) {
      warn("\tCustomCodingOptions line %d: missing scope prefix (CO/CO2/CO3/CODDI), skipping: %s",
           LineNo, Key.c_str());
      continue;
    }

    std::string Scope = Key.substr(0, DotPos);
    std::string Field = Key.substr(DotPos + 1);

    std::optional<mfxU64> Result;

    if (Scope == "CO" && COParams)
      Result = ApplyField(COParams, CO_FIELDS, Field, Val);
    else if (Scope == "CO2" && CO2Params)
      Result = ApplyField(CO2Params, CO2_FIELDS, Field, Val);
    else if (Scope == "CO3" && CO3Params)
      Result = ApplyField(CO3Params, CO3_FIELDS, Field, Val);
    else if (Scope == "CODDI" && CODDIParams)
      Result = ApplyField(CODDIParams, CODDI_FIELDS, Field, Val);

    if (Result.has_value()) {
      // Defer logging to LogActualParams() so the value shown reflects
      // the actual driver state after Init.
      m_CustomCodingOptions.push_back(
          {LineNo, Scope, Field, Val});
    } else {
      warn("\tCustomCodingOptions line %d: unknown field '%s.%s' or buffer not available",
           LineNo, Scope.c_str(), Field.c_str());
    }
  }
}

void QSVEncoder::ApplyQPLimits(struct encoder_params *InputParams) {
  auto *CO2 = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption2>();
  if (!CO2)
    return;

  mfxU16 bitDepth = InputParams->BitDepth;
  if (bitDepth == 0)
    bitDepth = 8;
  mfxU8 maxQP = static_cast<mfxU8>((std::min)(255, 51 + 6 * (bitDepth - 8)));

  auto ParseQPString = [&](const std::string &qpStr, mfxU8 &qpi,
                           mfxU8 &qpp, mfxU8 &qpb,
                           bool &skip) -> bool {
    skip = false;
    if (qpStr.empty() || qpStr == "-1") {
      skip = true;
      return true;
    }

    size_t comma1 = qpStr.find(',');
    if (comma1 == std::string::npos) {
      int val = std::atoi(qpStr.c_str());
      if (val < 0 || val > 255)
        return false;
      mfxU8 clamped = static_cast<mfxU8>((std::min)(val, (int)maxQP));
      qpi = qpp = qpb = clamped;
      return true;
    }

    size_t comma2 = qpStr.find(',', comma1 + 1);
    if (comma2 == std::string::npos)
      return false;

    int v1 = std::atoi(qpStr.substr(0, comma1).c_str());
    int v2 = std::atoi(qpStr.substr(comma1 + 1, comma2 - comma1 - 1).c_str());
    int v3 = std::atoi(qpStr.substr(comma2 + 1).c_str());

    if (v1 < 0 || v1 > 255 || v2 < 0 || v2 > 255 || v3 < 0 || v3 > 255)
      return false;

    qpi = static_cast<mfxU8>((std::min)(v1, (int)maxQP));
    qpp = static_cast<mfxU8>((std::min)(v2, (int)maxQP));
    qpb = static_cast<mfxU8>((std::min)(v3, (int)maxQP));
    return true;
  };

  {
    bool skipMin = false;
    mfxU8 minQPI = 0, minQPP = 0, minQPB = 0;
    if (ParseQPString(InputParams->MinQP, minQPI, minQPP, minQPB, skipMin)) {
      if (!skipMin) {
        CO2->MinQPI = minQPI;
        CO2->MinQPP = minQPP;
        CO2->MinQPB = minQPB;
        info("\tMinQP applied: I=%d, P=%d, B=%d (maxQP=%u)",
             minQPI, minQPP, minQPB, maxQP);
      }
    }
  }

  {
    bool skipMax = false;
    mfxU8 maxQPI = 0, maxQPP = 0, maxQPB = 0;
    if (ParseQPString(InputParams->MaxQP, maxQPI, maxQPP, maxQPB, skipMax)) {
      if (!skipMax) {
        CO2->MaxQPI = maxQPI;
        CO2->MaxQPP = maxQPP;
        CO2->MaxQPB = maxQPB;
        info("\tMaxQP applied: I=%d, P=%d, B=%d (maxQP=%u)",
             maxQPI, maxQPP, maxQPB, maxQP);
      }
    }
  }
}

mfxStatus QSVEncoder::SetEncoderParams(struct encoder_params *InputParams,
                                       enum codec_enum Codec) {
  switch (Codec) {
  case QSV_CODEC_AV1:
    QSVEncodeParams.mfx.CodecId = MFX_CODEC_AV1;
    break;
  case QSV_CODEC_HEVC:
    QSVEncodeParams.mfx.CodecId = MFX_CODEC_HEVC;
    break;
  case QSV_CODEC_AVC:
  default:
    QSVEncodeParams.mfx.CodecId = MFX_CODEC_AVC;
    break;
  }

  // Width must be a multiple of 16
  // Height must be a multiple of 16 in case of frame picture and a
  // multiple of 32 in case of field picture
  mfxU16 encodeWidth = InputParams->VPPOutWidth > 0
      ? InputParams->VPPOutWidth.value()
      : InputParams->Width;
  mfxU16 encodeHeight = InputParams->VPPOutHeight > 0
      ? InputParams->VPPOutHeight.value()
      : InputParams->Height;

  if (InputParams->VPPOutWidth > 0 && InputParams->VPPOutHeight > 0) {
    info("\tVPP scaling active, output resolution: %dx%d",
         encodeWidth, encodeHeight);
  }

  QSVEncodeParams.mfx.FrameInfo.Width =
      static_cast<mfxU16>((((encodeWidth + 15) >> 4) << 4));
  info("\tWidth: %d", QSVEncodeParams.mfx.FrameInfo.Width);

  QSVEncodeParams.mfx.FrameInfo.Height =
      static_cast<mfxU16>((((encodeHeight + 15) >> 4) << 4));
  info("\tHeight: %d", QSVEncodeParams.mfx.FrameInfo.Height);

  QSVEncodeParams.mfx.FrameInfo.ChromaFormat =
      static_cast<mfxU16>(InputParams->ChromaFormat);

  QSVEncodeParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

  QSVEncodeParams.mfx.FrameInfo.CropX = 0;

  QSVEncodeParams.mfx.FrameInfo.CropY = 0;

  QSVEncodeParams.mfx.FrameInfo.CropW = static_cast<mfxU16>(encodeWidth);

  QSVEncodeParams.mfx.FrameInfo.CropH =
      static_cast<mfxU16>(encodeHeight);

  QSVEncodeParams.mfx.FrameInfo.FrameRateExtN =
      static_cast<mfxU32>(InputParams->FpsNum);

  QSVEncodeParams.mfx.FrameInfo.FrameRateExtD =
      static_cast<mfxU32>(InputParams->FpsDen);

  QSVEncodeParams.mfx.FrameInfo.FourCC =
      static_cast<mfxU32>(InputParams->FourCC);

  QSVEncodeParams.mfx.FrameInfo.BitDepthChroma =
      InputParams->BitDepth > 0 ? InputParams->BitDepth : 0;

  QSVEncodeParams.mfx.FrameInfo.BitDepthLuma =
      InputParams->BitDepth > 0 ? InputParams->BitDepth : 0;

  QSVEncodeParams.mfx.FrameInfo.Shift =
      InputParams->BitDepth > 8 ? 1 : 0;

  QSVEncodeParams.mfx.LowPower = GetCodingOpt(InputParams->Lowpower);

  QSVEncodeParams.mfx.RateControlMethod =
      static_cast<mfxU16>(InputParams->RateControl);

  if (InputParams->NumRefFrame > 0) {
    QSVEncodeParams.mfx.NumRefFrame =
        static_cast<mfxU16>(InputParams->NumRefFrame);
  }

  QSVEncodeParams.mfx.TargetUsage =
      static_cast<mfxU16>(InputParams->TargetUsage);
  QSVEncodeParams.mfx.CodecProfile =
      static_cast<mfxU16>(InputParams->CodecProfile);
  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    // MFX_TIER_HEVC_HIGH (0x100) already lives in the upper byte,
    // so OR directly without shifting:
    mfxU16 combinedProfile = QSVEncodeParams.mfx.CodecProfile |
                             InputParams->CodecProfileTier;
    QSVEncodeParams.mfx.CodecProfile = combinedProfile;
  }

  QSVEncodeParams.mfx.CodecLevel = InputParams->CodecLevel;
  const mfxU16 BRC_BASELINE = 100;
  const mfxU16 brcMultiplier = BRC_BASELINE;
  QSVEncodeParams.mfx.BRCParamMultiplier = BRC_BASELINE;

  const auto brcClamp = [](mfxU32 v) {
    mfxU32 limit = static_cast<mfxU32>(BRC_MAX_KBPS_LIMIT) * 100;
    return v > limit ? limit : v;
  };

  // Common buffer/init-delay boilerplate for all RC modes
  auto ApplyBufferSettings = [&]() {
    if (InputParams->CustomBufferSize == true && InputParams->BufferSize > 0) {
      QSVEncodeParams.mfx.BufferSizeInKB =
          static_cast<mfxU16>(brcClamp(InputParams->BufferSize) / brcMultiplier);
      info("\tCustomBufferSize set: ON");
    }
    QSVEncodeParams.mfx.InitialDelayInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.BufferSizeInKB / 2);
    info("\tBufferSize set to: %d KB",
         QSVEncodeParams.mfx.BufferSizeInKB * brcMultiplier);
  };

  switch (InputParams->RateControl) {
  case MFX_RATECONTROL_CBR:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);

    if ((QSVPlatform.CodeName >= MFX_PLATFORM_BATTLEMAGE &&
         QSVPlatform.CodeName != MFX_PLATFORM_ALDERLAKE_N) &&
        ((QSVVersion.Minor >= 2 && QSVVersion.Major >= 13) ||
         QSVVersion.Major > 2)) {
      QSVEncodeParams.mfx.BufferSizeInKB =
          static_cast<mfxU16>(QSVEncodeParams.mfx.TargetKbps / 4);

      info("\tCBR fix: ON");
    } else {
      QSVEncodeParams.mfx.BufferSizeInKB =
          InputParams->Lookahead
              ? static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 2)
              : static_cast<mfxU16>(QSVEncodeParams.mfx.TargetKbps / 4);
    }

    ApplyBufferSettings();
    break;
  case MFX_RATECONTROL_VBR:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.MaxKbps =
        static_cast<mfxU16>(brcClamp(InputParams->MaxBitRate) / brcMultiplier);
    {
      const float vbrFps =
          static_cast<float>(QSVEncodeParams.mfx.FrameInfo.FrameRateExtN) /
          QSVEncodeParams.mfx.FrameInfo.FrameRateExtD;
      const float vbrKbps = static_cast<float>(QSVEncodeParams.mfx.TargetKbps);
      QSVEncodeParams.mfx.BufferSizeInKB =
          InputParams->Lookahead
              ? static_cast<mfxU16>((vbrKbps / 8.0f) / vbrFps *
                                    (InputParams->LADepth + vbrFps))
              : static_cast<mfxU16>(vbrKbps / 4.0f);
    }
    ApplyBufferSettings();
    break;
  case MFX_RATECONTROL_CQP:
    QSVEncodeParams.mfx.QPI = static_cast<mfxU16>(InputParams->QPI);
    QSVEncodeParams.mfx.QPB = static_cast<mfxU16>(InputParams->QPB);
    QSVEncodeParams.mfx.QPP = static_cast<mfxU16>(InputParams->QPP);
    break;
  case MFX_RATECONTROL_ICQ:
    QSVEncodeParams.mfx.ICQQuality =
        static_cast<mfxU16>(InputParams->ICQQuality);
    break;
  case MFX_RATECONTROL_AVBR:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.BufferSizeInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.TargetKbps / 4);
    ApplyBufferSettings();
    break;
  case MFX_RATECONTROL_VCM:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.MaxKbps =
        static_cast<mfxU16>(brcClamp(InputParams->MaxBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.BufferSizeInKB =
        static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 2);
    ApplyBufferSettings();
    break;
  case MFX_RATECONTROL_QVBR:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.MaxKbps =
        static_cast<mfxU16>(brcClamp(InputParams->MaxBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.BufferSizeInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.TargetKbps / 4);
    ApplyBufferSettings();
    break;
  }

  QSVEncodeParams.AsyncDepth = static_cast<mfxU16>(InputParams->AsyncDepth);

  const float fps = static_cast<float>(QSVEncodeParams.mfx.FrameInfo.FrameRateExtN) /
                    QSVEncodeParams.mfx.FrameInfo.FrameRateExtD;
  QSVEncodeParams.mfx.GopPicSize = (InputParams->KeyIntSec > 0)
      ? static_cast<mfxU16>(InputParams->KeyIntSec * fps)
      : static_cast<mfxU16>(10 * fps);

  const bool adaptiveIOn = InputParams->AdaptiveI == true;
  const bool adaptiveBOn = InputParams->AdaptiveB == true;
  QSVEncodeParams.mfx.GopOptFlag =
      (!adaptiveIOn && !adaptiveBOn) ? MFX_GOP_STRICT : MFX_GOP_CLOSED;

  switch (QSVEncodeParams.mfx.CodecId) {
  case MFX_CODEC_HEVC:
    QSVEncodeParams.mfx.IdrInterval = 1;
    break;
  default:
    QSVEncodeParams.mfx.IdrInterval = 0;
    break;
  }

  QSVEncodeParams.mfx.NumSlice = static_cast<mfxU16>(1);

  QSVEncodeParams.mfx.GopRefDist = InputParams->BFrames > 0 ? static_cast<mfxU16>(InputParams->BFrames + 1) : 0;

  {
    auto COParams = QSVEncodeParams.AddExtBuffer<mfxExtCodingOption>();
    COParams->Header.BufferId = MFX_EXTBUFF_CODING_OPTION;
    COParams->Header.BufferSz = sizeof(mfxExtCodingOption);
    /*Don't touch it!*/
    COParams->CAVLC = MFX_CODINGOPTION_OFF;
    COParams->RefPicListReordering = MFX_CODINGOPTION_ON;
    COParams->RefPicMarkRep = MFX_CODINGOPTION_ON;
    COParams->PicTimingSEI = MFX_CODINGOPTION_ON;
    COParams->MaxDecFrameBuffering = InputParams->NumRefFrame;
    COParams->ResetRefList = MFX_CODINGOPTION_ON;
    COParams->FieldOutput = (InputParams->Lowpower == false)
                                ? MFX_CODINGOPTION_OFF
                                : MFX_CODINGOPTION_ON;

    if (InputParams->IntraRefEncoding == true) {
      COParams->RecoveryPointSEI = MFX_CODINGOPTION_ON;
    }

    COParams->RateDistortionOpt = GetCodingOpt(InputParams->RDO);

    COParams->VuiVclHrdParameters = GetCodingOpt(InputParams->HRDConformance);
    COParams->VuiNalHrdParameters = GetCodingOpt(InputParams->HRDConformance);
    COParams->NalHrdConformance = GetCodingOpt(InputParams->HRDConformance);
  }

  {
    auto CO2Params = QSVEncodeParams.AddExtBuffer<mfxExtCodingOption2>();
    CO2Params->Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
    CO2Params->Header.BufferSz = sizeof(mfxExtCodingOption2);
    CO2Params->BufferingPeriodSEI = MFX_BPSEI_IFRAME;
    CO2Params->RepeatPPS = MFX_CODINGOPTION_OFF;
    CO2Params->FixedFrameRate = MFX_CODINGOPTION_ON;
    CO2Params->DisableDeblockingIdc = 0; // enable deblocking filter

    if (InputParams->IntraRefEncoding == true) {
      CO2Params->IntRefType =
          static_cast<mfxU16>(InputParams->IntraRefType > 0
                                  ? InputParams->IntraRefType
                                  : MFX_REFRESH_HORIZONTAL);

      CO2Params->IntRefCycleSize =
          static_cast<mfxU16>(InputParams->IntraRefCycleSize > 1
                                  ? InputParams->IntraRefCycleSize
                                  : (QSVEncodeParams.mfx.GopRefDist > 1
                                         ? QSVEncodeParams.mfx.GopRefDist
                                         : 2));
      info("\tIntraRefCycleSize set: %d", CO2Params->IntRefCycleSize);
      if (InputParams->IntraRefQPDelta > -52 &&
          InputParams->IntraRefQPDelta < 52) {
        CO2Params->IntRefQPDelta =
            static_cast<mfxU16>(InputParams->IntraRefQPDelta);
        info("\tIntraRefQPDelta set: %d", CO2Params->IntRefQPDelta);
      }
    }

    if (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_ICQ ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_QVBR) {
      if (InputParams->Lookahead == true) {
        CO2Params->LookAheadDepth = InputParams->LADepth;
      }
    }

    CO2Params->MBBRC = GetCodingOpt(InputParams->MBBRC);

    if (InputParams->Trellis.has_value()) {
      switch (InputParams->Trellis.value()) {
      case 0:
        CO2Params->Trellis = MFX_TRELLIS_OFF;
        break;
      case 1:
        CO2Params->Trellis = MFX_TRELLIS_I;
        break;
      case 2:
        CO2Params->Trellis = MFX_TRELLIS_I | MFX_TRELLIS_P;
        break;
      case 3:
        CO2Params->Trellis = MFX_TRELLIS_I | MFX_TRELLIS_P | MFX_TRELLIS_B;
        break;
      case 4:
        CO2Params->Trellis = MFX_TRELLIS_I | MFX_TRELLIS_B;
        break;
      case 5:
        CO2Params->Trellis = MFX_TRELLIS_P;
        break;
      case 6:
        CO2Params->Trellis = MFX_TRELLIS_P | MFX_TRELLIS_B;
        break;
      case 7:
        CO2Params->Trellis = MFX_TRELLIS_B;
        break;
      }
    }

    CO2Params->AdaptiveI = GetCodingOpt(InputParams->AdaptiveI);

    CO2Params->AdaptiveB = GetCodingOpt(InputParams->AdaptiveB);

    if (InputParams->RateControl == MFX_RATECONTROL_CBR ||
        InputParams->RateControl == MFX_RATECONTROL_VBR ||
        InputParams->RateControl == MFX_RATECONTROL_AVBR ||
        InputParams->RateControl == MFX_RATECONTROL_ICQ ||
        InputParams->RateControl == MFX_RATECONTROL_QVBR) {
      CO2Params->LookAheadDS = MFX_LOOKAHEAD_DS_OFF;
      if (InputParams->LookAheadDS) {
        switch (*InputParams->LookAheadDS) {
        case 0:
          CO2Params->LookAheadDS = MFX_LOOKAHEAD_DS_OFF;
          break;
        case 1:
          CO2Params->LookAheadDS = MFX_LOOKAHEAD_DS_2x;
          break;
        case 2:
          CO2Params->LookAheadDS = MFX_LOOKAHEAD_DS_4x;
          break;
        }
      }
    }

    CO2Params->UseRawRef = GetCodingOpt(InputParams->RawRef);

    CO2Params->MaxFrameSize = InputParams->AdaptiveMaxFrameSize;
  }

  {
    auto CO3Params = QSVEncodeParams.AddExtBuffer<mfxExtCodingOption3>();
    CO3Params->Header.BufferId = MFX_EXTBUFF_CODING_OPTION3;
    CO3Params->Header.BufferSz = sizeof(mfxExtCodingOption3);
    info("\tmfxExtCodingOption3 sizeof: %zu, BufferSz: %d",
         sizeof(mfxExtCodingOption3), CO3Params->Header.BufferSz);
    CO3Params->TargetBitDepthLuma = InputParams->BitDepth > 0 ? InputParams->BitDepth : 0;
    CO3Params->TargetBitDepthChroma = InputParams->BitDepth > 0 ? InputParams->BitDepth : 0;
    CO3Params->TargetChromaFormatPlus1 =
        static_cast<mfxU16>(QSVEncodeParams.mfx.FrameInfo.ChromaFormat + 1);
    CO3Params->TransformSkip = GetCodingOpt(InputParams->TransformSkip);
    CO3Params->EnableMBForceIntra = MFX_CODINGOPTION_ON;
    CO3Params->FadeDetection = GetCodingOpt(InputParams->FadeDetection);

    if (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_QVBR &&
        InputParams->QVBRQuality > 0 && InputParams->QVBRQuality <= 51) {
      CO3Params->QVBRQuality = InputParams->QVBRQuality;
    }

    CO3Params->EnableQPOffset = MFX_CODINGOPTION_ON;

    CO3Params->BitstreamRestriction = MFX_CODINGOPTION_ON;
    CO3Params->AspectRatioInfoPresent = MFX_CODINGOPTION_ON;
    CO3Params->TimingInfoPresent = MFX_CODINGOPTION_ON;
    CO3Params->OverscanInfoPresent = MFX_CODINGOPTION_ON;

    CO3Params->LowDelayHrd = GetCodingOpt(InputParams->LowDelayHRD);

    CO3Params->LowDelayBRC = GetCodingOpt(InputParams->LowDelayBRC);

    CO3Params->WeightedPred =
        InputParams->WeightedPred.value_or(MFX_WEIGHTED_PRED_DEFAULT);
    CO3Params->WeightedBiPred =
        InputParams->WeightedBiPred.value_or(MFX_WEIGHTED_PRED_DEFAULT);

    CO3Params->RepartitionCheckEnable = MFX_CODINGOPTION_ON;

    if (InputParams->NumRefActiveP.has_value() &&
        InputParams->NumRefActiveP > 0) {
      std::fill(CO3Params->NumRefActiveP, CO3Params->NumRefActiveP + 8,
                InputParams->NumRefActiveP.value());
    }

    if (InputParams->NumRefActiveBL0.has_value() &&
        InputParams->NumRefActiveBL0 > 0) {
      std::fill(CO3Params->NumRefActiveBL0, CO3Params->NumRefActiveBL0 + 8,
                InputParams->NumRefActiveBL0.value());
    }

    if (InputParams->NumRefActiveBL1.has_value() &&
        InputParams->NumRefActiveBL1 > 0) {
      std::fill(CO3Params->NumRefActiveBL1, CO3Params->NumRefActiveBL1 + 8,
                InputParams->NumRefActiveBL1.value());
    }

    if (InputParams->IntraRefEncoding == true) {
      CO3Params->IntRefCycleDist = 0;
    }

    if (InputParams->ContentInfo.has_value()) {
      CO3Params->ContentInfo = static_cast<mfxU16>(InputParams->ContentInfo.value());
    }

    if (InputParams->ScenarioInfo.has_value()) {
      CO3Params->ScenarioInfo = static_cast<mfxU16>(InputParams->ScenarioInfo.value());
    }

    if (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_CQP) {
      CO3Params->EnableMBQP = MFX_CODINGOPTION_ON;
    }

    if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
      CO3Params->GPB = GetCodingOpt(InputParams->GPB);
    }

    auto *CO2Pyramid =
        QSVEncodeParams.GetExtBuffer<mfxExtCodingOption2>();
    if (InputParams->PPyramid == true) {
      CO3Params->PRefType = MFX_P_REF_PYRAMID;
      if (CO2Pyramid) CO2Pyramid->BRefType = MFX_B_REF_PYRAMID;
    } else {
      CO3Params->PRefType = MFX_P_REF_SIMPLE;
      if (CO2Pyramid) CO2Pyramid->BRefType = MFX_B_REF_UNKNOWN;
    }

    CO3Params->AdaptiveCQM = GetCodingOpt(InputParams->AdaptiveCQM);

    CO3Params->AdaptiveRef = GetCodingOpt(InputParams->AdaptiveRef);

    CO3Params->AdaptiveLTR = GetCodingOpt(InputParams->AdaptiveLTR);

    CO3Params->MotionVectorsOverPicBoundaries =
        GetCodingOpt(InputParams->MotionVectorsOverPicBoundaries);

    if (InputParams->GlobalMotionBiasAdjustment.has_value() &&
        InputParams->GlobalMotionBiasAdjustment.value() == true) {
      CO3Params->GlobalMotionBiasAdjustment = MFX_CODINGOPTION_ON;
      if (InputParams->MVCostScalingFactor.has_value()) {
        switch (InputParams->MVCostScalingFactor.value()) {
        case 1:
          CO3Params->MVCostScalingFactor = 1;
          break;
        case 2:
          CO3Params->MVCostScalingFactor = 2;
          break;
        case 3:
          CO3Params->MVCostScalingFactor = 3;
          break;
        }
      }
    } else {
      CO3Params->GlobalMotionBiasAdjustment = MFX_CODINGOPTION_OFF;
    }

    CO3Params->DirectBiasAdjustment =
        GetCodingOpt(InputParams->DirectBiasAdjustment);
  }

#if defined(_WIN32) || defined(_WIN64)
  if (InputParams->EncTools == true) {
    auto EncToolsParams = QSVEncodeParams.AddExtBuffer<mfxExtEncToolsConfig>();
    EncToolsParams->Header.BufferId = MFX_EXTBUFF_ENCTOOLS_CONFIG;
    EncToolsParams->Header.BufferSz = sizeof(mfxExtEncToolsConfig);
    EncToolsParams->AdaptiveI = GetCodingOpt(InputParams->AdaptiveI);
    EncToolsParams->AdaptiveB = GetCodingOpt(InputParams->AdaptiveB);
    EncToolsParams->SceneChange = GetCodingOpt(InputParams->EncToolsSceneChange);
    EncToolsParams->AdaptiveRefP = GetCodingOpt(InputParams->EncToolsAdaptiveRefP);
    EncToolsParams->AdaptiveRefB = GetCodingOpt(InputParams->EncToolsAdaptiveRefB);
    EncToolsParams->AdaptiveLTR = GetCodingOpt(InputParams->AdaptiveLTR);
    EncToolsParams->AdaptivePyramidQuantP = GetCodingOpt(InputParams->EncToolsAdaptivePyramidQuantP);
    EncToolsParams->AdaptivePyramidQuantB = GetCodingOpt(InputParams->EncToolsAdaptivePyramidQuantB);
    EncToolsParams->AdaptiveQuantMatrices = GetCodingOpt(InputParams->AdaptiveCQM);
    // ExtBRC is always OFF now, so AdaptiveMBQP and BRC can freely use user
    // settings — they work with the driver's built-in BRC, not ExtBRC.
    EncToolsParams->AdaptiveMBQP = GetCodingOpt(InputParams->EncToolsAdaptiveMBQP);
    EncToolsParams->BRC = GetCodingOpt(InputParams->EncToolsBRC);
    EncToolsParams->BRCBufferHints = GetCodingOpt(InputParams->EncToolsBRCBufferHints);
    EncToolsParams->SaliencyMapHint = GetCodingOpt(InputParams->EncToolsSaliencyMapHint);
  }

  /*Don't touch it! Magic beyond the control of mere mortals takes place
   * here*/
  if (QSVEncodeParams.mfx.CodecId != MFX_CODEC_AV1) {
    auto CODDIParams = QSVEncodeParams.AddExtBuffer<mfxExtCodingOptionDDI>();
    CODDIParams->Header.BufferId = MFX_EXTBUFF_DDI;
    CODDIParams->Header.BufferSz = sizeof(mfxExtCodingOptionDDI);
    CODDIParams->WriteIVFHeaders = MFX_CODINGOPTION_OFF;
    CODDIParams->IBC = MFX_CODINGOPTION_ON;
    CODDIParams->Palette = MFX_CODINGOPTION_ON;
    CODDIParams->BRCPrecision = 3;
    CODDIParams->BiDirSearch = MFX_CODINGOPTION_ON;
    CODDIParams->DirectSpatialMvPredFlag = MFX_CODINGOPTION_ON;
    CODDIParams->GlobalSearch = 1;
    CODDIParams->IntraPredCostType = 8;
    CODDIParams->MEFractionalSearchType = 16;
    CODDIParams->MEInterpolationMethod = 8;
    CODDIParams->MVPrediction = MFX_CODINGOPTION_ON;
    CODDIParams->WeightedBiPredIdc = 2;
    CODDIParams->WeightedPrediction = MFX_CODINGOPTION_ON;
    CODDIParams->FieldPrediction = MFX_CODINGOPTION_ON;
    CODDIParams->DirectCheck = MFX_CODINGOPTION_ON;
    CODDIParams->FractionalQP = 1;
    CODDIParams->Hme = MFX_CODINGOPTION_ON;
    CODDIParams->LocalSearch = 6;
    CODDIParams->MBAFF = MFX_CODINGOPTION_ON;
    CODDIParams->DDI.InterPredBlockSize = 64;
    CODDIParams->DDI.IntraPredBlockSize = 1;
    CODDIParams->RefOppositeField = MFX_CODINGOPTION_ON;
    CODDIParams->RefRaw = GetCodingOpt(InputParams->RawRef);
    CODDIParams->TMVP = MFX_CODINGOPTION_ON;
    CODDIParams->DisablePSubMBPartition = MFX_CODINGOPTION_OFF;
    CODDIParams->DisableBSubMBPartition = MFX_CODINGOPTION_OFF;
    CODDIParams->QpAdjust = MFX_CODINGOPTION_ON;
    CODDIParams->Transform8x8Mode = MFX_CODINGOPTION_ON;
    CODDIParams->EarlySkip = 0;
    CODDIParams->RefreshFrameContext = MFX_CODINGOPTION_ON;
    CODDIParams->ChangeFrameContextIdxForTS = MFX_CODINGOPTION_ON;
    CODDIParams->SuperFrameForTS = MFX_CODINGOPTION_ON;
  }
#endif

  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto ChromaLocParams = QSVEncodeParams.AddExtBuffer<mfxExtChromaLocInfo>();
    ChromaLocParams->Header.BufferId = MFX_EXTBUFF_CHROMA_LOC_INFO;
    ChromaLocParams->Header.BufferSz = sizeof(mfxExtChromaLocInfo);
    ChromaLocParams->ChromaLocInfoPresentFlag = 1;
    ChromaLocParams->ChromaSampleLocTypeTopField =
        static_cast<mfxU16>(InputParams->ChromaSampleLocTypeTopField);
    ChromaLocParams->ChromaSampleLocTypeBottomField =
        static_cast<mfxU16>(InputParams->ChromaSampleLocTypeBottomField);

    auto HevcParams = QSVEncodeParams.AddExtBuffer<mfxExtHEVCParam>();
    HevcParams->Header.BufferId = MFX_EXTBUFF_HEVC_PARAM;
    HevcParams->Header.BufferSz = sizeof(mfxExtHEVCParam);
    HevcParams->PicWidthInLumaSamples = QSVEncodeParams.mfx.FrameInfo.Width;
    HevcParams->PicHeightInLumaSamples = QSVEncodeParams.mfx.FrameInfo.Height;

    if (InputParams->SAO.has_value()) {
      switch (InputParams->SAO.value()) {
      case 0:
        HevcParams->SampleAdaptiveOffset = MFX_SAO_DISABLE;
        break;
      case 1:
        HevcParams->SampleAdaptiveOffset = MFX_SAO_ENABLE_LUMA;
        break;
      case 2:
        HevcParams->SampleAdaptiveOffset = MFX_SAO_ENABLE_CHROMA;
        break;
      case 3:
        HevcParams->SampleAdaptiveOffset =
            MFX_SAO_ENABLE_LUMA | MFX_SAO_ENABLE_CHROMA;
        break;
      }
    }

    auto HevcTilesParams = QSVEncodeParams.AddExtBuffer<mfxExtHEVCTiles>();
    HevcTilesParams->Header.BufferId = MFX_EXTBUFF_HEVC_TILES;
    HevcTilesParams->Header.BufferSz = sizeof(mfxExtHEVCTiles);
    HevcTilesParams->NumTileColumns = 1;
    HevcTilesParams->NumTileRows = 1;
  }

  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_AV1) {
    if (InputParams->ScreenContentTools == 0) {
      if (QSVVersion.Major >= 2 && QSVVersion.Minor >= 12 ||
          QSVVersion.Major > 2) {
        if (QSVPlatform.CodeName >= MFX_PLATFORM_LUNARLAKE &&
            QSVPlatform.CodeName != MFX_PLATFORM_ALDERLAKE_N &&
            QSVPlatform.CodeName != MFX_PLATFORM_ARROWLAKE) {
          auto AV1ScreenContentTools =
              QSVEncodeParams.AddExtBuffer<mfxExtAV1ScreenContentTools>();
          AV1ScreenContentTools->Header.BufferId =
              MFX_EXTBUFF_AV1_SCREEN_CONTENT_TOOLS;
          AV1ScreenContentTools->Header.BufferSz =
              sizeof(mfxExtAV1ScreenContentTools);
          AV1ScreenContentTools->Palette = MFX_CODINGOPTION_ON;
          AV1ScreenContentTools->IntraBlockCopy = MFX_CODINGOPTION_ON;
        }
      }
    } else {
      auto AV1ScreenContentTools =
          QSVEncodeParams.AddExtBuffer<mfxExtAV1ScreenContentTools>();
      AV1ScreenContentTools->Header.BufferId =
          MFX_EXTBUFF_AV1_SCREEN_CONTENT_TOOLS;
      AV1ScreenContentTools->Header.BufferSz =
          sizeof(mfxExtAV1ScreenContentTools);
      AV1ScreenContentTools->Palette =
          (InputParams->ScreenContentTools == 2) ? MFX_CODINGOPTION_ON
                                                  : MFX_CODINGOPTION_OFF;
      AV1ScreenContentTools->IntraBlockCopy =
          (InputParams->ScreenContentTools == 2) ? MFX_CODINGOPTION_ON
                                                  : MFX_CODINGOPTION_OFF;
      info("\tAV1ScreenContentTools: %s",
           InputParams->ScreenContentTools == 2 ? "ON" : "OFF");
    }

    auto AV1BitstreamParams =
        QSVEncodeParams.AddExtBuffer<mfxExtAV1BitstreamParam>();
    AV1BitstreamParams->Header.BufferId = MFX_EXTBUFF_AV1_BITSTREAM_PARAM;
    AV1BitstreamParams->Header.BufferSz = sizeof(mfxExtAV1BitstreamParam);
    AV1BitstreamParams->WriteIVFHeaders = MFX_CODINGOPTION_OFF;

    auto AV1TileParams = QSVEncodeParams.AddExtBuffer<mfxExtAV1TileParam>();
    AV1TileParams->Header.BufferId = MFX_EXTBUFF_AV1_TILE_PARAM;
    AV1TileParams->Header.BufferSz = sizeof(mfxExtAV1TileParam);
    AV1TileParams->NumTileGroups = 1;
    if ((InputParams->Height * InputParams->Width) >= 8294400) {
      AV1TileParams->NumTileColumns = 2;
      AV1TileParams->NumTileRows = 2;
    } else {
      AV1TileParams->NumTileColumns = 1;
      AV1TileParams->NumTileRows = 1;
    }

    if (InputParams->TuneQualityMode.has_value()) {
      auto TuneQualityParams =
          QSVEncodeParams.AddExtBuffer<mfxExtTuneEncodeQuality>();
      TuneQualityParams->Header.BufferId = MFX_EXTBUFF_TUNE_ENCODE_QUALITY;
      TuneQualityParams->Header.BufferSz = sizeof(mfxExtTuneEncodeQuality);
      switch ((int)InputParams->TuneQualityMode.value()) {
      default:
      case 0:
        TuneQualityParams->TuneQuality = MFX_ENCODE_TUNE_OFF;
        break;
      case 1:
        TuneQualityParams->TuneQuality = MFX_ENCODE_TUNE_PSNR;
        break;
      case 2:
        TuneQualityParams->TuneQuality = MFX_ENCODE_TUNE_SSIM;
        break;
      case 3:
        TuneQualityParams->TuneQuality = MFX_ENCODE_TUNE_MS_SSIM;
        break;
      case 4:
        TuneQualityParams->TuneQuality = MFX_ENCODE_TUNE_VMAF;
        break;
      case 5:
        TuneQualityParams->TuneQuality = MFX_ENCODE_TUNE_PERCEPTUAL;
        break;
      }
    }

    if (InputParams->AV1CDEF.has_value() || InputParams->AV1Restoration.has_value() ||
        InputParams->AV1LoopFilter.has_value() || InputParams->AV1SuperRes.has_value() ||
        InputParams->AV1InterpFilter.has_value() || InputParams->AV1ErrorResilient.has_value()) {
      auto AV1AuxDataParams = QSVEncodeParams.AddExtBuffer<mfxExtAV1AuxData>();
      AV1AuxDataParams->Header.BufferId = MFX_EXTBUFF_AV1_AUXDATA;
      AV1AuxDataParams->Header.BufferSz = sizeof(mfxExtAV1AuxData);

      if (InputParams->AV1CDEF.has_value()) {
        switch (InputParams->AV1CDEF.value()) {
        case 0:
          AV1AuxDataParams->EnableCdef = MFX_CODINGOPTION_OFF;
          info("\tAV1 CDEF: OFF");
          break;
        case 1:
          AV1AuxDataParams->EnableCdef = MFX_CODINGOPTION_ON;
          info("\tAV1 CDEF: ON");
          break;
        case 2:
          AV1AuxDataParams->EnableCdef = MFX_CODINGOPTION_UNKNOWN;
          info("\tAV1 CDEF: AUTO");
          break;
        }
      }

      if (InputParams->AV1Restoration.has_value()) {
        switch (InputParams->AV1Restoration.value()) {
        case 0:
          AV1AuxDataParams->EnableRestoration = MFX_CODINGOPTION_OFF;
          info("\tAV1 Restoration: OFF");
          break;
        case 1:
          AV1AuxDataParams->EnableRestoration = MFX_CODINGOPTION_ON;
          info("\tAV1 Restoration: ON");
          break;
        case 2:
          AV1AuxDataParams->EnableRestoration = MFX_CODINGOPTION_UNKNOWN;
          info("\tAV1 Restoration: AUTO");
          break;
        }
      }

      if (InputParams->AV1LoopFilter.has_value()) {
        switch (InputParams->AV1LoopFilter.value()) {
        case 0:
          AV1AuxDataParams->EnableLoopFilter = MFX_CODINGOPTION_OFF;
          info("\tAV1 LoopFilter: OFF");
          break;
        case 1:
          AV1AuxDataParams->EnableLoopFilter = MFX_CODINGOPTION_ON;
          info("\tAV1 LoopFilter: ON");
          break;
        case 2:
          AV1AuxDataParams->EnableLoopFilter = MFX_CODINGOPTION_UNKNOWN;
          info("\tAV1 LoopFilter: AUTO");
          break;
        }
      }

      if (InputParams->AV1SuperRes.has_value()) {
        switch (InputParams->AV1SuperRes.value()) {
        case 0:
          AV1AuxDataParams->EnableSuperres = MFX_CODINGOPTION_OFF;
          info("\tAV1 SuperRes: OFF");
          break;
        case 1:
          AV1AuxDataParams->EnableSuperres = MFX_CODINGOPTION_ON;
          info("\tAV1 SuperRes: ON");
          break;
        case 2:
          AV1AuxDataParams->EnableSuperres = MFX_CODINGOPTION_UNKNOWN;
          info("\tAV1 SuperRes: AUTO");
          break;
        }
      }

      if (InputParams->AV1InterpFilter.has_value()) {
        AV1AuxDataParams->InterpFilter = static_cast<mfxU8>(InputParams->AV1InterpFilter.value());
        info("\tAV1 InterpFilter: %d", InputParams->AV1InterpFilter.value());
      }

      if (InputParams->AV1ErrorResilient.has_value()) {
        switch (InputParams->AV1ErrorResilient.value()) {
        case 0:
          AV1AuxDataParams->ErrorResilientMode = MFX_CODINGOPTION_OFF;
          info("\tAV1 ErrorResilient: OFF");
          break;
        case 1:
          AV1AuxDataParams->ErrorResilientMode = MFX_CODINGOPTION_ON;
          info("\tAV1 ErrorResilient: ON");
          break;
        case 2:
          AV1AuxDataParams->ErrorResilientMode = MFX_CODINGOPTION_UNKNOWN;
          info("\tAV1 ErrorResilient: AUTO");
          break;
        }
      }
    }
  }

#if defined(_WIN32) || defined(_WIN64)
  auto VideoSignalParams =
      QSVEncodeParams.AddExtBuffer<mfxExtVideoSignalInfo>();
  VideoSignalParams->Header.BufferId = MFX_EXTBUFF_VIDEO_SIGNAL_INFO;
  VideoSignalParams->Header.BufferSz = sizeof(mfxExtVideoSignalInfo);
  VideoSignalParams->VideoFormat =
      static_cast<mfxU16>(InputParams->VideoFormat);
  VideoSignalParams->VideoFullRange =
      static_cast<mfxU16>(InputParams->VideoFullRange);
  VideoSignalParams->ColourDescriptionPresent = 1;
  VideoSignalParams->ColourPrimaries =
      static_cast<mfxU16>(InputParams->ColourPrimaries);
  VideoSignalParams->TransferCharacteristics =
      static_cast<mfxU16>(InputParams->TransferCharacteristics);
  VideoSignalParams->MatrixCoefficients =
      static_cast<mfxU16>(InputParams->MatrixCoefficients);
#endif

  if (InputParams->MaxContentLightLevel > 0) {
    auto ColourVolumeParams =
        QSVEncodeParams.AddExtBuffer<mfxExtMasteringDisplayColourVolume>();
    ColourVolumeParams->Header.BufferId =
        MFX_EXTBUFF_MASTERING_DISPLAY_COLOUR_VOLUME;
    ColourVolumeParams->Header.BufferSz =
        sizeof(mfxExtMasteringDisplayColourVolume);
    ColourVolumeParams->InsertPayloadToggle = MFX_PAYLOAD_IDR;
    ColourVolumeParams->DisplayPrimariesX[0] =
        static_cast<mfxU16>(InputParams->DisplayPrimariesX[0]);
    ColourVolumeParams->DisplayPrimariesX[1] =
        static_cast<mfxU16>(InputParams->DisplayPrimariesX[1]);
    ColourVolumeParams->DisplayPrimariesX[2] =
        static_cast<mfxU16>(InputParams->DisplayPrimariesX[2]);
    ColourVolumeParams->DisplayPrimariesY[0] =
        static_cast<mfxU16>(InputParams->DisplayPrimariesY[0]);
    ColourVolumeParams->DisplayPrimariesY[1] =
        static_cast<mfxU16>(InputParams->DisplayPrimariesY[1]);
    ColourVolumeParams->DisplayPrimariesY[2] =
        static_cast<mfxU16>(InputParams->DisplayPrimariesY[2]);
    ColourVolumeParams->WhitePointX =
        static_cast<mfxU16>(InputParams->WhitePointX);
    ColourVolumeParams->WhitePointY =
        static_cast<mfxU16>(InputParams->WhitePointY);
    ColourVolumeParams->MaxDisplayMasteringLuminance =
        static_cast<mfxU32>(InputParams->MaxDisplayMasteringLuminance);
    ColourVolumeParams->MinDisplayMasteringLuminance =
        static_cast<mfxU32>(InputParams->MinDisplayMasteringLuminance);

    auto ContentLightLevelParams =
        QSVEncodeParams.AddExtBuffer<mfxExtContentLightLevelInfo>();
    ContentLightLevelParams->Header.BufferId =
        MFX_EXTBUFF_CONTENT_LIGHT_LEVEL_INFO;
    ContentLightLevelParams->Header.BufferSz =
        sizeof(mfxExtContentLightLevelInfo);
    ContentLightLevelParams->InsertPayloadToggle = MFX_PAYLOAD_IDR;
    ContentLightLevelParams->MaxContentLightLevel =
        static_cast<mfxU16>(InputParams->MaxContentLightLevel);
    ContentLightLevelParams->MaxPicAverageLightLevel =
        static_cast<mfxU16>(InputParams->MaxPicAverageLightLevel);
  }

  if (InputParams->TemporalLayersNum > 1) {
    mfxU16 minGopDist = 1 << (InputParams->TemporalLayersNum - 1);
    if (QSVEncodeParams.mfx.GopRefDist < minGopDist) {
      info("\tB-frames adjusted from %d to %d for temporal layers (%d layers require B-frames>=%d)",
           QSVEncodeParams.mfx.GopRefDist - 1, minGopDist - 1,
           InputParams->TemporalLayersNum, minGopDist - 1);
      QSVEncodeParams.mfx.GopRefDist = minGopDist;
    } else {
      info("\tB-frames=%d is sufficient for temporal layers=%d (requires B-frames>=%d)",
           QSVEncodeParams.mfx.GopRefDist - 1,
           InputParams->TemporalLayersNum, minGopDist - 1);
    }

    if (QSVEncodeParams.mfx.NumRefFrame < QSVEncodeParams.mfx.GopRefDist) {
      warn("\tNumRefFrame=%d may be too low for B-frames=%d with temporal layers=%d, encoder Init may fail",
           QSVEncodeParams.mfx.NumRefFrame,
           QSVEncodeParams.mfx.GopRefDist - 1,
           InputParams->TemporalLayersNum);
    }

    auto TemporalLayersParams =
        QSVEncodeParams.AddExtBuffer<mfxExtTemporalLayers>();
    TemporalLayersParams->Header.BufferId =
        MFX_EXTBUFF_UNIVERSAL_TEMPORAL_LAYERS;
    TemporalLayersParams->Header.BufferSz = sizeof(mfxExtTemporalLayers);
    TemporalLayersParams->NumLayers =
        static_cast<mfxU16>(InputParams->TemporalLayersNum);
    TemporalLayersParams->BaseLayerPID = 0;
    delete[] QSVLayerArray;
    QSVLayerArray =
        new mfxTemporalLayer[InputParams->TemporalLayersNum]();

    {
      mfxU16 baseQPForLayer = 0;
      int qpStep = 0;
      if (InputParams->RateControl == MFX_RATECONTROL_CQP) {
        baseQPForLayer = static_cast<mfxU16>(std::min({InputParams->QPI, InputParams->QPP, InputParams->QPB}));
        qpStep = 4;
      }
      for (int i = 0; i < InputParams->TemporalLayersNum; i++) {
        QSVLayerArray[i].FrameRateScale =
            1 << (InputParams->TemporalLayersNum - 1 - i);
        QSVLayerArray[i].QPI = static_cast<mfxU16>(baseQPForLayer + i * qpStep);
        QSVLayerArray[i].QPP = static_cast<mfxU16>(baseQPForLayer + i * qpStep);
        QSVLayerArray[i].QPB = static_cast<mfxU16>(baseQPForLayer + i * qpStep);
      }
    }
    TemporalLayersParams->Layers = QSVLayerArray;
    info("\tTemporalLayers: %d layers enabled",
         InputParams->TemporalLayersNum);
  }

#ifdef QSV_UHD600_SUPPORT
  QSVEncodeParams.IOPattern = QSVUseSystemMemoryPath
                                 ? MFX_IOPATTERN_IN_SYSTEM_MEMORY
                                 : MFX_IOPATTERN_IN_VIDEO_MEMORY;
#else
  QSVEncodeParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
#endif

  // Cache ROI data for per-frame use, only for AVC and HEVC (AV1 not supported)
  if (Codec != QSV_CODEC_AV1 && InputParams->ROIEnabled) {
    CachedROIRegions = InputParams->ROIRegions;
    CachedROIMode = InputParams->ROIMode;
  } else {
    CachedROIRegions.clear();
    CachedROIMode = 0;
  }

  return MFX_ERR_NONE;
}

bool QSVEncoder::UpdateParams(struct encoder_params *InputParams) {
  QSVResetParamsChanged = false;

  mfxStatus Status = QSVEncode->GetVideoParam(&QSVResetParams);
  if (Status < MFX_ERR_NONE) {
    return false;
  }

  QSVResetParams.NumExtParam = 0;
  switch (InputParams->RateControl) {
  case MFX_RATECONTROL_CBR:
  case MFX_RATECONTROL_AVBR: {
    mfxU16 brcM = QSVResetParams.mfx.BRCParamMultiplier;
    if (brcM == 0) brcM = 1;
    mfxU32 clampedTarget = InputParams->TargetBitRate;
    {
      mfxU32 limit = static_cast<mfxU32>(BRC_MAX_KBPS_LIMIT) * brcM;
      if (clampedTarget > limit) clampedTarget = limit;
    }
    mfxU16 resetTargetKbps =
        static_cast<mfxU16>(clampedTarget / brcM);
    if (QSVResetParams.mfx.TargetKbps != resetTargetKbps) {
      QSVResetParams.mfx.TargetKbps = resetTargetKbps;
      QSVResetParamsChanged = true;
    }
    break;
  }
  case MFX_RATECONTROL_VBR:
  case MFX_RATECONTROL_VCM:
  case MFX_RATECONTROL_QVBR: {
    mfxU16 brcM = QSVResetParams.mfx.BRCParamMultiplier;
    if (brcM == 0) brcM = 1;
    mfxU32 limit = static_cast<mfxU32>(BRC_MAX_KBPS_LIMIT) * brcM;
    mfxU32 clampedTarget = InputParams->TargetBitRate;
    if (clampedTarget > limit) clampedTarget = limit;
    mfxU32 clampedMax = InputParams->MaxBitRate;
    if (clampedMax > limit) clampedMax = limit;
    mfxU16 resetTargetKbps =
        static_cast<mfxU16>(clampedTarget / brcM);
    if (QSVResetParams.mfx.TargetKbps != resetTargetKbps) {
      QSVResetParams.mfx.TargetKbps = resetTargetKbps;
      QSVResetParamsChanged = true;
    }
    mfxU16 resetMaxKbps =
        static_cast<mfxU16>(clampedMax / brcM);
    if (QSVResetParams.mfx.MaxKbps != resetMaxKbps) {
      QSVResetParams.mfx.MaxKbps = resetMaxKbps;
      QSVResetParamsChanged = true;
    }
    if (QSVResetParams.mfx.MaxKbps < QSVResetParams.mfx.TargetKbps) {
      QSVResetParams.mfx.MaxKbps = QSVResetParams.mfx.TargetKbps;
      QSVResetParamsChanged = true;
    }
    break;
  }
  case MFX_RATECONTROL_CQP:
    if (QSVResetParams.mfx.QPI != InputParams->QPI) {
      QSVResetParams.mfx.QPI = static_cast<mfxU16>(InputParams->QPI);
      QSVResetParams.mfx.QPB = static_cast<mfxU16>(InputParams->QPB);
      QSVResetParams.mfx.QPP = static_cast<mfxU16>(InputParams->QPP);
      QSVResetParamsChanged = true;
    }
    break;
  case MFX_RATECONTROL_ICQ:
    if (QSVResetParams.mfx.ICQQuality != InputParams->ICQQuality) {
      QSVResetParams.mfx.ICQQuality =
          static_cast<mfxU16>(InputParams->ICQQuality);
      QSVResetParamsChanged = true;
    }
    break;
  }
  if (QSVResetParamsChanged) {
    auto ResetParams = QSVEncodeParams.AddExtBuffer<mfxExtEncoderResetOption>();
    ResetParams->Header.BufferId = MFX_EXTBUFF_ENCODER_RESET_OPTION;
    ResetParams->Header.BufferSz = sizeof(mfxExtEncoderResetOption);
    ResetParams->StartNewSequence = MFX_CODINGOPTION_ON;
    QSVEncode->Query(&QSVResetParams, &QSVResetParams);
    return true;
  } else {
    return false;
  }
}

mfxStatus QSVEncoder::ReconfigureEncoder() {
  if (QSVResetParamsChanged) {
    return QSVEncode->Reset(&QSVResetParams);
  } else {
    return MFX_ERR_NONE;
  }
}

void QSVEncoder::UpdateROIRegions(
    const std::vector<encoder_params::roi_region> &Regions, mfxU16 Mode) {
  CachedROIRegions = Regions;
  CachedROIMode = Mode;
  info("\tROI updated: %zu regions, mode=%d", Regions.size(), Mode);
}

mfxStatus QSVEncoder::InitTexturePool() {
  mfxStatus Status = MFX_ERR_NONE;

  if (QSVIsTextureEncoder) {
    Status = HWManager->AllocateTexturePool(QSVEncodeParams);
    if (Status < MFX_ERR_NONE) {
      error("Error code: %d", Status);
      throw std::runtime_error(
          "InitTexturePool(): AllocateTexturePool error");
    }

    Status = MFXGetMemoryInterface(QSVSession, &QSVMemoryInterface);
    if (Status < MFX_ERR_NONE) {
      error("Error code: %d", Status);
      throw std::runtime_error(
          "InitTexturePool(): MFXGetMemoryInterface error");
    }
  }

  return Status;
}

mfxStatus
QSVEncoder::InitBitstreamBuffer([[maybe_unused]] enum codec_enum Codec) {
  QSVBitstream.MaxLength =
      static_cast<mfxU32>(QSVEncodeParams.mfx.BufferSizeInKB * 1000 *
                          QSVEncodeParams.mfx.BRCParamMultiplier);

  QSVBitstream.DataOffset = 0;
  QSVBitstream.DataLength = 0;
  QSVBitstream.Data = static_cast<mfxU8 *>(
      AlignedMalloc(QSVBitstream.MaxLength, 32));
  if (nullptr == QSVBitstream.Data) {
    throw std::runtime_error(
        "InitBitstreamBuffer(): Bitstream memory allocation error");
  }

  info("\tBitstream size: %d Kb", QSVBitstream.MaxLength / 1000);
  return MFX_ERR_NONE;
}

mfxStatus QSVEncoder::InitTaskPool([[maybe_unused]] enum codec_enum Codec) {
  QSVSyncTaskID = 0;
  Task NewTask = {};
  QSVTaskPool.reserve(QSVEncodeParams.AsyncDepth);

  // Allocate one mfxExtEncodedFrameInfo per task to retrieve per-frame QP
  QSVTaskEncodedInfo.resize(QSVEncodeParams.AsyncDepth);
  QSVTaskEncodedExtPtr.resize(QSVEncodeParams.AsyncDepth);

  for (int i = 0; i < QSVEncodeParams.AsyncDepth; i++) {
    NewTask.Bitstream.MaxLength =
        static_cast<mfxU32>(QSVEncodeParams.mfx.BufferSizeInKB * 1000 *
                            QSVEncodeParams.mfx.BRCParamMultiplier) +
        QSV_SEI_EXTRA;

    NewTask.Bitstream.DataOffset = 0;
    NewTask.Bitstream.DataLength = 0;
    NewTask.Bitstream.Data = static_cast<mfxU8 *>(
        AlignedMalloc(NewTask.Bitstream.MaxLength, 32));
    if (nullptr == NewTask.Bitstream.Data) {
      throw std::runtime_error(
          "InitTaskPool(): Task memory allocation error");
    }
    // Attach mfxExtEncodedFrameInfo to each task's bitstream so the
    // encoder reports back the frame-level QP after EncodeFrameAsync.
    auto &encInfo = QSVTaskEncodedInfo[i];
    memset(&encInfo, 0, sizeof(encInfo));
    encInfo.Header.BufferId = MFX_EXTBUFF_ENCODED_FRAME_INFO;
    encInfo.Header.BufferSz = sizeof(encInfo);
    NewTask.Bitstream.ExtParam = &QSVTaskEncodedExtPtr[i];
    QSVTaskEncodedExtPtr[i] = reinterpret_cast<mfxExtBuffer *>(&encInfo);
    NewTask.Bitstream.NumExtParam = 1;

    QSVTaskPool.push_back(NewTask);

#ifdef QSV_UHD600_SUPPORT
    if (QSVUseSystemMemoryPath && !QSVIsTextureEncoder &&
        i < static_cast<int>(QSVSystemMemPool.size())) {
      QSVTaskPool[i].Surface = &QSVSystemMemPool[i].Surface;
    }
#endif
  }

  info("\tTaskPool count: %d", QSVTaskPool.size());

  return MFX_ERR_NONE;
}

void QSVEncoder::ReleaseBitstream() {
  if (QSVBitstream.Data) {
    AlignedFree(QSVBitstream.Data);
  }
  QSVBitstream.Data = nullptr;
}

void QSVEncoder::ReleaseTask(int TaskID) {
  if (QSVTaskPool[TaskID].Bitstream.Data) {
    AlignedFree(QSVTaskPool[TaskID].Bitstream.Data);
  }
  QSVTaskPool[TaskID].Bitstream.Data = nullptr;
}

void QSVEncoder::ReleaseTaskPool() {
  std::lock_guard<std::mutex> lock(QSVTaskPoolMutex);
  if (!QSVTaskPool.empty()) {
    for (int i = 0; i < QSVTaskPool.size(); i++) {
      ReleaseTask(i);
    }

    QSVTaskPool.clear();
    QSVTaskPool.shrink_to_fit();
  }
}

mfxStatus QSVEncoder::ChangeBitstreamSize(mfxU32 NewSize) {
  // Reallocate the main bitstream buffer (allocate new first, then swap)
  mfxU8 *Data = static_cast<mfxU8 *>(AlignedMalloc(NewSize, 32));
  if (Data == nullptr) {
    throw std::runtime_error(
        "ChangeBitstreamSize(): Bitstream memory allocation error");
  }

  mfxU32 DataLen = QSVBitstream.DataLength;
  if (QSVBitstream.DataLength) {
    // If NewSize is smaller than existing data length, it is a caller error;
    // report it rather than silently truncating
    if (NewSize < DataLen) {
      AlignedFree(Data);
      throw std::runtime_error(
          "ChangeBitstreamSize(): NewSize smaller than existing DataLength");
    }
    memcpy(Data, QSVBitstream.Data + QSVBitstream.DataOffset, DataLen);
  }
  ReleaseBitstream();

  QSVBitstream.Data = Data;
  QSVBitstream.DataOffset = 0;
  QSVBitstream.DataLength = static_cast<mfxU32>(DataLen);
  QSVBitstream.MaxLength = NewSize;

  // Sync all pending tasks before reallocating task buffers
  for (int i = 0; i < QSVTaskPool.size(); i++) {
    if (QSVTaskPool[i].SyncPoint != nullptr) {
      mfxStatus SyncSts;
      do {
        SyncSts = MFXVideoCORE_SyncOperation(
            QSVSession, QSVTaskPool[i].SyncPoint, 100);
      } while (SyncSts == MFX_WRN_IN_EXECUTION);
      if (SyncSts < MFX_ERR_NONE) {
        throw std::runtime_error(
            "ChangeBitstreamSize(): Sync pending task error");
      }
      QSVTaskPool[i].SyncPoint = nullptr;
    }
  }

  // Two-phase commit: first allocate all new buffers, only release old ones
  // after all allocations succeed.  This guarantees QSVTaskPool remains in a
  // consistent state (old Data not freed) if any single allocation fails.
  std::vector<mfxU8 *> newTaskData;
  newTaskData.reserve(QSVTaskPool.size());
  try {
    for (int i = 0; i < QSVTaskPool.size(); i++) {
      mfxU8 *TaskData = static_cast<mfxU8 *>(AlignedMalloc(NewSize, 32));
      if (TaskData == nullptr) {
        throw std::runtime_error(
            "ChangeBitstreamSize(): Task memory allocation error");
      }
      newTaskData.push_back(TaskData);
    }
  } catch (...) {
    // Allocation failed: free newly allocated buffers, keep old ones intact,
    // QSVTaskPool remains usable
    for (auto *p : newTaskData) {
      AlignedFree(p);
    }
    throw;
  }

  // All allocations succeeded, copy data and switch to new buffers
  for (size_t i = 0; i < QSVTaskPool.size(); i++) {
    mfxU32 TaskDataLen = QSVTaskPool[i].Bitstream.DataLength;
    if (TaskDataLen) {
      if (NewSize < TaskDataLen) {
        for (auto *p : newTaskData) {
          AlignedFree(p);
        }
        throw std::runtime_error(
            "ChangeBitstreamSize(): NewSize smaller than task DataLength");
      }
      memcpy(newTaskData[i],
             QSVTaskPool[i].Bitstream.Data +
                 QSVTaskPool[i].Bitstream.DataOffset,
             TaskDataLen);
    }
    ReleaseTask(static_cast<int>(i));

    QSVTaskPool[i].Bitstream.Data = newTaskData[i];
    QSVTaskPool[i].Bitstream.DataOffset = 0;
    QSVTaskPool[i].Bitstream.DataLength =
        static_cast<mfxU32>(TaskDataLen);
    QSVTaskPool[i].Bitstream.MaxLength =
        NewSize;
  }

  return MFX_ERR_NONE;
}

mfxStatus QSVEncoder::GetVideoParam([[maybe_unused]] enum codec_enum Codec) {
  auto SPSPPSParams = QSVEncodeParams.AddExtBuffer<mfxExtCodingOptionSPSPPS>();
  SPSPPSParams->Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS;
  SPSPPSParams->Header.BufferSz = sizeof(mfxExtCodingOptionSPSPPS);
  SPSPPSParams->SPSBuffer = QSVSPSBuffer;
  SPSPPSParams->PPSBuffer = QSVPPSBuffer;
  SPSPPSParams->SPSBufSize = 1024;
  SPSPPSParams->PPSBufSize = 1024;

  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto VPSParams = QSVEncodeParams.AddExtBuffer<mfxExtCodingOptionVPS>();
    VPSParams->Header.BufferId = MFX_EXTBUFF_CODING_OPTION_VPS;
    VPSParams->Header.BufferSz = sizeof(mfxExtCodingOptionVPS);
    VPSParams->VPSBuffer = QSVVPSBuffer;
    VPSParams->VPSBufSize = 1024;
  }

  mfxStatus Status = QSVEncode->GetVideoParam(&QSVEncodeParams);

  if (Status < MFX_ERR_NONE) {
    error("Error code: %d", Status);
    throw std::runtime_error("GetVideoParam(): Get video parameters error");
  }

  return Status;
}

/* ── HEVC SPS parser: extract actual CTU size from driver-generated SPS ── */

static mfxU16 parse_hevc_sps_ctb_size(const mfxU8 *sps_data,
                                       mfxU16 sps_size) {
  if (!sps_data || sps_size < 2)
    return 0;

  size_t byte_pos = 2;
  int bit_pos = 7;

  mfxU16 sps_vps_id =
      hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 4);
  (void)sps_vps_id;

  mfxU16 max_sub_layers =
      hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 3);

  hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1);

  hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 96);

  if (max_sub_layers > 0) {
    bool sub_layer_profile_present[7] = {false};
    bool sub_layer_level_present[7] = {false};
    for (int j = max_sub_layers - 1; j >= 0; j--) {
      sub_layer_profile_present[j] =
          hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1) != 0;
      sub_layer_level_present[j] =
          hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1) != 0;
    }
    for (int j = max_sub_layers - 1; j >= 0; j--) {
      if (sub_layer_profile_present[j])
        hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 96);
      if (sub_layer_level_present[j])
        hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 8);
    }
  }

  hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  mfxU16 chroma_format_idc =
      hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  if (chroma_format_idc == 3)
    hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1);

  hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  if (hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1)) {
    hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  }

  hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  bool sub_layer_ordering =
      hevc_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1) != 0;

  mfxU16 num_ordering = sub_layer_ordering ? max_sub_layers + 1 : 1;
  for (mfxU16 i = 0; i < num_ordering; i++) {
    hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  }

  mfxU16 log2_min_cb =
      hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  mfxU16 log2_diff =
      hevc_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  mfxU16 min_cb_log2 = log2_min_cb + 3;
  mfxU16 ctb_log2 = min_cb_log2 + log2_diff;

  if (ctb_log2 >= 4 && ctb_log2 <= 7)
    return 1 << ctb_log2;

  return 0;
}

void QSVEncoder::LogActualParams() {
  info("\tActual encoder driver params:");

  auto GetTrellisStatus = [](mfxU16 Value) -> std::string {
    if (Value == MFX_TRELLIS_OFF) return "OFF";
    if (Value == (MFX_TRELLIS_I | MFX_TRELLIS_P | MFX_TRELLIS_B))
      return "IPB";
    if (Value == (MFX_TRELLIS_I | MFX_TRELLIS_P)) return "IP";
    if (Value == (MFX_TRELLIS_I | MFX_TRELLIS_B)) return "IB";
    if (Value == (MFX_TRELLIS_P | MFX_TRELLIS_B)) return "PB";
    if (Value == MFX_TRELLIS_I) return "I";
    if (Value == MFX_TRELLIS_P) return "P";
    if (Value == MFX_TRELLIS_B) return "B";
    return "AUTO";
  };

  auto GetSAOStatus = [](mfxU16 Value) -> std::string {
    if (Value ==
        (MFX_SAO_ENABLE_LUMA | MFX_SAO_ENABLE_CHROMA))
      return "ALL";
    if (Value == MFX_SAO_ENABLE_LUMA) return "LUMA";
    if (Value == MFX_SAO_ENABLE_CHROMA) return "CHROMA";
    if (Value == MFX_SAO_DISABLE) return "DISABLE";
    return "AUTO";
  };

  auto GetInterpFilterName = [](mfxU8 Value) -> std::string {
    switch (Value) {
    case 0: return "DEFAULT";
    case 1: return "EIGHTTAP";
    case 2: return "EIGHTTAP_SMOOTH";
    case 3: return "EIGHTTAP_SHARP";
    case 4: return "BILINEAR";
    case 5: return "SWITCHABLE";
    default: return "UNKNOWN";
    }
  };

  auto GetWeightedPredStatus = [](mfxU16 Value) -> std::string {
    switch (Value) {
    case 0:  return "OFF";
    case 1:  return "DEFAULT";
    case 2:  return "EXPLICIT";
    case 3:  return "IMPLICIT";
    case 16: return "ON (ddi)";
    case 32: return "OFF (ddi)";
    default: return "UNKNOWN";
    }
  };

  info("\tLowpower set: %s",
       GetCodingOptStatus(QSVEncodeParams.mfx.LowPower).c_str());
  info("\tNumRefFrame set to: %d",
       QSVEncodeParams.mfx.NumRefFrame);
  info("\tB-frames: %d",
       QSVEncodeParams.mfx.GopRefDist - 1);

  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    mfxU16 profileBase = QSVEncodeParams.mfx.CodecProfile & 0x00FF;
    mfxU16 tier = (QSVEncodeParams.mfx.CodecProfile >> 8) & 0xFF;
    info("\tCodecProfile: %d (tier %s)", profileBase,
         tier == (MFX_TIER_HEVC_HIGH >> 8) ? "high" : "main");
  } else {
    info("\tCodecProfile: %d",
         QSVEncodeParams.mfx.CodecProfile);
  }

  if (QSVEncodeParams.mfx.CodecLevel) {
    info("\tCodecLevel: %d",
         QSVEncodeParams.mfx.CodecLevel);
  }

  if (QSVEncodeParams.mfx.GopOptFlag & MFX_GOP_STRICT) {
    info("\tGopOptFlag set: STRICT");
  } else if (QSVEncodeParams.mfx.GopOptFlag & MFX_GOP_CLOSED) {
    info("\tGopOptFlag set: CLOSED");
  }

  // ─ CO (mfxExtCodingOption) ─
  auto *CO = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption>();
  if (CO) {
    info("\tRDO set: %s",
         GetCodingOptStatus(CO->RateDistortionOpt).c_str());
    info("\tHRDConformance set: %s",
         GetCodingOptStatus(CO->VuiVclHrdParameters).c_str());
  }

  // ─ CO2 (mfxExtCodingOption2) ─
  auto *CO2 = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption2>();
  if (CO2) {
    info("\tMBBRC set: %s",
         GetCodingOptStatus(CO2->MBBRC).c_str());
    info("\tTrellis set: %s",
         GetTrellisStatus(CO2->Trellis).c_str());
    info("\tAdaptiveI set: %s",
         GetCodingOptStatus(CO2->AdaptiveI).c_str());
    info("\tAdaptiveB set: %s",
         GetCodingOptStatus(CO2->AdaptiveB).c_str());
    info("\tUseRawRef set: %s",
         GetCodingOptStatus(CO2->UseRawRef).c_str());
    if (CO2->MaxFrameSize > 0) {
      info("\tAdaptiveMaxFrameSize set: %d bytes", CO2->MaxFrameSize);
    } else {
      info("\tAdaptiveMaxFrameSize set: AUTO");
    }
    {
      static constexpr std::string_view kLookaheadDSNames[] = {
        "UNKNOWN", "1X", "2X", "4X"
      };
      auto ds_idx = CO2->LookAheadDS < 4 ? CO2->LookAheadDS : 0;
      info("\tLookAheadDS set: %s (%d)",
           kLookaheadDSNames[ds_idx].data(), CO2->LookAheadDS);
    }
    info("\tLookaheadDepth set to: %d", CO2->LookAheadDepth);

    if (QSVEncodeParams.mfx.CodecId != MFX_CODEC_AV1 &&
        CO2->IntRefType > 0) {
      info("\tIntraRefresh: type=%d, cycle=%d, QP delta=%d",
           CO2->IntRefType, CO2->IntRefCycleSize, CO2->IntRefQPDelta);
    }

    // Min/Max QP constraints
    if (CO2->MinQPI > 0 || CO2->MinQPP > 0 || CO2->MinQPB > 0 ||
        CO2->MaxQPI > 0 || CO2->MaxQPP > 0 || CO2->MaxQPB > 0) {
      info("\tQP limits ─ Min: I=%d, P=%d, B=%d │ Max: I=%d, P=%d, B=%d",
           CO2->MinQPI, CO2->MinQPP, CO2->MinQPB,
           CO2->MaxQPI, CO2->MaxQPP, CO2->MaxQPB);
    }
  }

  auto *CO3 = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
  if (CO3) {
    info("\tFadeDetection set: %s",
         GetCodingOptStatus(CO3->FadeDetection).c_str());
    info("\tLowDelayHRD set: %s",
         GetCodingOptStatus(CO3->LowDelayHrd).c_str());
    info("\tLowDelayBRC set: %s",
         GetCodingOptStatus(CO3->LowDelayBRC).c_str());
    if (CO3->NumRefActiveP[0]) {
      info("\tNumRefActiveP set: %d",
           CO3->NumRefActiveP[0]);
    }
    if (CO3->NumRefActiveBL0[0]) {
      info("\tNumRefActiveBL0 set: %d",
           CO3->NumRefActiveBL0[0]);
    }
    if (CO3->NumRefActiveBL1[0]) {
      info("\tNumRefActiveBL1 set: %d",
           CO3->NumRefActiveBL1[0]);
    }
    if (CO3->ContentInfo) {
      info("\tContentInfo set: %d",
           CO3->ContentInfo);
    } else {
      info("\tContentInfo: AUTO");
    }
    if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
      info("\tGPB set: %s",
           GetCodingOptStatus(CO3->GPB).c_str());
    }
    {
      auto *CO2Pyramid =
          QSVEncodeParams.GetExtBuffer<mfxExtCodingOption2>();
      info("\tPyramid: P: %s | B: %s",
           CO3->PRefType == MFX_P_REF_PYRAMID
               ? "PYRAMID"
           : CO3->PRefType == MFX_P_REF_SIMPLE ? "SIMPLE"
                                               : "AUTO",
           CO2Pyramid && CO2Pyramid->BRefType == MFX_B_REF_PYRAMID
               ? "PYRAMID"
           : CO2Pyramid && CO2Pyramid->BRefType == MFX_B_REF_OFF
               ? "OFF"
               : "AUTO");
    }
    info("\tAdaptiveCQM set: %s",
         GetCodingOptStatus(CO3->AdaptiveCQM).c_str());
    info("\tAdaptiveRef set: %s",
         GetCodingOptStatus(CO3->AdaptiveRef).c_str());
    info("\tAdaptiveLTR set: %s",
         GetCodingOptStatus(CO3->AdaptiveLTR).c_str());
    info("\tMotionVectorsOverPicBoundaries set: %s",
         GetCodingOptStatus(CO3->MotionVectorsOverPicBoundaries).c_str());
    info("\tGlobalMotionBiasAdjustment set: %s",
         GetCodingOptStatus(CO3->GlobalMotionBiasAdjustment).c_str());
    if (CO3->GlobalMotionBiasAdjustment == MFX_CODINGOPTION_ON &&
        CO3->MVCostScalingFactor) {
      info("\tMVCostScalingFactor set: 1/%d",
           1 << CO3->MVCostScalingFactor);
    }
    info("\tDirectBiasAdjustment set: %s",
         GetCodingOptStatus(CO3->DirectBiasAdjustment).c_str());
    info("\tWeightedPred set: %s",
         GetWeightedPredStatus(CO3->WeightedPred).c_str());
    info("\tWeightedBiPred set: %s",
         GetWeightedPredStatus(CO3->WeightedBiPred).c_str());
    if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
      info("\tTransformSkip set: %s",
           GetCodingOptStatus(CO3->TransformSkip).c_str());
    }
    if (CO3->ScenarioInfo) {
      info("\tScenarioInfo set: %d", CO3->ScenarioInfo);
    }
  }

  auto *EncTools = QSVEncodeParams.GetExtBuffer<mfxExtEncToolsConfig>();
  if (EncTools) {
    info("\tEncTools sub-options:");
    info("\t  SceneChange: %s",
         GetCodingOptStatus(EncTools->SceneChange).c_str());
    info("\t  AdaptiveRefP: %s",
         GetCodingOptStatus(EncTools->AdaptiveRefP).c_str());
    info("\t  AdaptiveRefB: %s",
         GetCodingOptStatus(EncTools->AdaptiveRefB).c_str());
    info("\t  AdaptivePyramidQuantP: %s",
         GetCodingOptStatus(EncTools->AdaptivePyramidQuantP).c_str());
    info("\t  AdaptivePyramidQuantB: %s",
         GetCodingOptStatus(EncTools->AdaptivePyramidQuantB).c_str());
    info("\t  AdaptiveMBQP: %s",
         GetCodingOptStatus(EncTools->AdaptiveMBQP).c_str());
    info("\t  BRCBufferHints: %s",
         GetCodingOptStatus(EncTools->BRCBufferHints).c_str());
    info("\t  BRC: %s",
         GetCodingOptStatus(EncTools->BRC).c_str());
    info("\t  SaliencyMapHint: %s",
         GetCodingOptStatus(EncTools->SaliencyMapHint).c_str());
  }

  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_AV1) {
    auto *AV1AuxData = QSVEncodeParams.GetExtBuffer<mfxExtAV1AuxData>();
    if (AV1AuxData) {
      info("\tAV1 CDEF set: %s",
           GetCodingOptStatus(AV1AuxData->EnableCdef).c_str());
      info("\tAV1 Restoration set: %s",
           GetCodingOptStatus(AV1AuxData->EnableRestoration).c_str());
      info("\tAV1 LoopFilter set: %s",
           GetCodingOptStatus(AV1AuxData->EnableLoopFilter).c_str());
      info("\tAV1 SuperRes set: %s",
           GetCodingOptStatus(AV1AuxData->EnableSuperres).c_str());
      info("\tAV1 InterpFilter set: %s",
           GetInterpFilterName(AV1AuxData->InterpFilter).c_str());
      info("\tAV1 ErrorResilient set: %s",
           GetCodingOptStatus(AV1AuxData->ErrorResilientMode).c_str());
    }

    auto *AV1ScreenTools =
        QSVEncodeParams.GetExtBuffer<mfxExtAV1ScreenContentTools>();
    if (AV1ScreenTools) {
      info("\tAV1 ScreenContentTools: Palette=%s, IntraBlockCopy=%s",
           GetCodingOptStatus(AV1ScreenTools->Palette).c_str(),
           GetCodingOptStatus(AV1ScreenTools->IntraBlockCopy).c_str());
    }
  }

  auto *TuneQuality = QSVEncodeParams.GetExtBuffer<mfxExtTuneEncodeQuality>();
  if (TuneQuality) {
    auto GetTuneQualityName = [](mfxU16 Value) -> std::string {
      switch (Value) {
      case MFX_ENCODE_TUNE_OFF: return "OFF";
      case MFX_ENCODE_TUNE_PSNR: return "PSNR";
      case MFX_ENCODE_TUNE_SSIM: return "SSIM";
      case MFX_ENCODE_TUNE_MS_SSIM: return "MS SSIM";
      case MFX_ENCODE_TUNE_VMAF: return "VMAF";
      case MFX_ENCODE_TUNE_PERCEPTUAL: return "PERCEPTUAL";
      default: return "DEFAULT";
      }
    };
    info("\tTune quality: %s",
         GetTuneQualityName(TuneQuality->TuneQuality).c_str());
  }

  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto *SPSPPS = QSVEncodeParams.GetExtBuffer<mfxExtCodingOptionSPSPPS>();
    if (SPSPPS && SPSPPS->SPSBuffer && SPSPPS->SPSBufSize > 0) {
      mfxU16 actual_ctb =
          parse_hevc_sps_ctb_size(SPSPPS->SPSBuffer, SPSPPS->SPSBufSize);
      if (actual_ctb > 0) {
        info("\tCTU Size (actual): %d", actual_ctb);
      } else {
        info("\tCTU Size: could not parse from SPS");
      }
    } else {
      info("\tCTU Size: not available from SPS");
    }

    auto *HEVC = QSVEncodeParams.GetExtBuffer<mfxExtHEVCParam>();
    if (HEVC) {
      info("\tSAO set: %s",
           GetSAOStatus(HEVC->SampleAdaptiveOffset).c_str());
    }
  }

  auto *TemporalLayers =
      QSVEncodeParams.GetExtBuffer<mfxExtTemporalLayers>();
  if (TemporalLayers && TemporalLayers->NumLayers > 0) {
    info("\tTemporalLayers: %d layers", TemporalLayers->NumLayers);
  }

  auto *MCTF = QSVProcessingParams.GetExtBuffer<mfxExtVppMctf>();
  if (MCTF) {
    info("\tMCTF set: ON | Strength %d",
         MCTF->FilterStrength);
  } else {
    info("\tMCTF set: OFF");
  }

  // ─ Custom Coding Options (deferred from ParseCustomCodingOptions) ─
  if (!m_CustomCodingOptions.empty()) {
    info("\tCustom Coding Options:");
    auto *CODDI = QSVEncodeParams.GetExtBuffer<mfxExtCodingOptionDDI>();

    for (const auto &entry : m_CustomCodingOptions) {
      void *base = nullptr;
      std::span<const FieldEntry> entries;
      if (entry.Scope == "CO") {
        base = CO;
        entries = CO_FIELDS;
      } else if (entry.Scope == "CO2") {
        base = CO2;
        entries = CO2_FIELDS;
      } else if (entry.Scope == "CO3") {
        base = CO3;
        entries = CO3_FIELDS;
      } else if (entry.Scope == "CODDI") {
        base = CODDI;
        entries = CODDI_FIELDS;
      }

      mfxU64 actualVal = 0;
      bool found = false;
      if (base) {
        for (const auto &e : entries) {
          if (entry.Field == e.name) {
            actualVal = ReadFieldValue(base, e);
            found = true;
            break;
          }
        }
      }

      if (found) {
        info("\t  CustomCodingOptions[%d]: %s.%s = %s (%s)", entry.LineNo,
             entry.Scope.c_str(), entry.Field.c_str(),
             entry.RawVal.c_str(),
             FormatFieldValue(entry.Field, actualVal, entry.RawVal).c_str());
      } else {
        warn("\t  CustomCodingOptions[%d]: %s.%s buffer not available",
             entry.LineNo, entry.Scope.c_str(), entry.Field.c_str());
      }
    }
  }
}

void QSVEncoder::LoadFrameData(mfxFrameSurface1 *&Surface, uint8_t **FrameData,
                               uint32_t *FrameLinesize) {
  mfxU16 Width, Height, i, Pitch;
  mfxU8 *PTR;
  const mfxFrameInfo *SurfaceInfo = &Surface->Info;
  const mfxFrameData *SurfaceData = &Surface->Data;

  if (SurfaceInfo->CropH > 0 && SurfaceInfo->CropW > 0) {
    Width = SurfaceInfo->CropW;
    Height = SurfaceInfo->CropH;
  } else {
    Width = SurfaceInfo->Width;
    Height = SurfaceInfo->Height;
  }
  Pitch = SurfaceData->Pitch;

  if (Surface->Info.FourCC == MFX_FOURCC_NV12) {
    if (Pitch == static_cast<mfxU16>(FrameLinesize[0])) {
      avx2_memcpy(SurfaceData->Y, FrameData[0],
             static_cast<size_t>(Height) * Pitch);
      avx2_memcpy(SurfaceData->UV, FrameData[1],
             static_cast<size_t>(Height / 2) * Pitch);
    } else {
      PTR = static_cast<mfxU8 *>(SurfaceData->Y + SurfaceInfo->CropX +
                                 SurfaceInfo->CropY * Pitch);

      for (i = 0; i < Height; i++) {
        avx2_memcpy(PTR + i * Pitch, FrameData[0] + i * FrameLinesize[0], Width);
      }

      Height /= 2;
      PTR = static_cast<mfxU8 *>((SurfaceData->UV + SurfaceInfo->CropX +
                                  (SurfaceInfo->CropY / 2) * Pitch));

      for (i = 0; i < Height; i++) {
        avx2_memcpy(PTR + i * Pitch, FrameData[1] + i * FrameLinesize[1], Width);
      }
    }
  } else if (Surface->Info.FourCC == MFX_FOURCC_P010) {
    const size_t line_size = static_cast<size_t>(Width) * 2;
    if (Pitch == static_cast<mfxU16>(FrameLinesize[0])) {
      avx2_memcpy(SurfaceData->Y, FrameData[0],
             static_cast<size_t>(Height) * Pitch);
      avx2_memcpy(SurfaceData->UV, FrameData[1],
             static_cast<size_t>(Height / 2) * Pitch);
    } else {
      PTR = static_cast<mfxU8 *>(SurfaceData->Y + SurfaceInfo->CropX +
                                 SurfaceInfo->CropY * Pitch);

      for (i = 0; i < Height; i++) {
        avx2_memcpy(PTR + i * Pitch, FrameData[0] + i * FrameLinesize[0], line_size);
      }

      Height /= 2;
      PTR = static_cast<mfxU8 *>((SurfaceData->UV + SurfaceInfo->CropX +
                                  (SurfaceInfo->CropY / 2) * Pitch));

      for (i = 0; i < Height; i++) {
        avx2_memcpy(PTR + i * Pitch, FrameData[1] + i * FrameLinesize[1], line_size);
      }
    }
  } else if (Surface->Info.FourCC == MFX_FOURCC_RGB4) {
    const size_t line_size = static_cast<size_t>(Width) * 4;
    if (Pitch == static_cast<mfxU16>(FrameLinesize[0])) {
      avx2_memcpy(SurfaceData->B, FrameData[0],
             static_cast<size_t>(Height) * Pitch);
    } else {
      for (i = 0; i < Height; i++) {
        avx2_memcpy(SurfaceData->B + i * Pitch, FrameData[0] + i * FrameLinesize[0],
               line_size);
      }
    }
  }
}

#ifdef QSV_UHD600_SUPPORT
mfxStatus QSVEncoder::EncodeFrameSystemMemory(mfxU64 TS, uint8_t **FrameData,
                                              uint32_t *FrameLinesize,
                                              mfxBitstream **Bitstream) {
  mfxStatus Status = MFX_ERR_NONE, SyncStatus = MFX_ERR_NONE;
  *Bitstream = nullptr;
  int TaskID = 0;

  while (GetFreeTaskIndex(&TaskID) == MFX_ERR_NOT_FOUND) {
    SyncStatus = SyncAndSwapPendingTask(Bitstream);
    if (SyncStatus < MFX_ERR_NONE) {
      error("Encode.EncodeSync error: %d", SyncStatus);
      if (QSVEncodeSurface) {
        QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
        QSVEncodeSurface = nullptr;
      }
      throw std::runtime_error(
          "Encode(): Sync operation failed - unrecoverable error");
    }
  }

  mfxFrameSurface1 *EncodeSurface = QSVTaskPool[TaskID].Surface;
  if (!EncodeSurface) {
    error("System memory surface is null for task %d", TaskID);
    throw std::runtime_error("Encode(): System memory surface is null");
  }

  EncodeSurface->Data.TimeStamp = TS;
  LoadFrameData(EncodeSurface, FrameData, FrameLinesize);
  QSVTaskPool[TaskID].Bitstream.TimeStamp = TS;

  bool roiActive = !CachedROIRegions.empty();
  if (roiActive)
    SetupROIEncodeCtrl();
  Status = EncodeFrameRetryLoop(EncodeSurface,
                                roiActive ? &QSVEncodeCtrlParams : nullptr,
                                TaskID, 200);

  return Status;
}
#endif

mfxStatus QSVEncoder::GetFreeTaskIndex(int *TaskID) {
  std::lock_guard<std::mutex> lock(QSVTaskPoolMutex);
  if (!QSVTaskPool.empty()) {
    const int PoolSize = static_cast<int>(QSVTaskPool.size());
    int StartIdx = QSVSyncTaskID;
    for (int i = 0; i < PoolSize; i++) {
      int Idx = (StartIdx + i) % PoolSize;
      if (static_cast<mfxSyncPoint>(nullptr) == QSVTaskPool[Idx].SyncPoint) {
        QSVSyncTaskID = (Idx + 1) % PoolSize;
        *TaskID = Idx;
        return MFX_ERR_NONE;
      }
    }
  }
  return MFX_ERR_NOT_FOUND;
}

mfxStatus QSVEncoder::SyncAndSwapPendingTask(mfxBitstream **Bitstream) {
  mfxStatus SyncStatus = MFX_ERR_NONE;
  profile_start("qsv_sync_task");

  {
    std::lock_guard<std::mutex> lock(QSVTaskPoolMutex);

    while (QSVTaskPool[QSVSyncTaskID].SyncPoint != nullptr) {
      SyncStatus = MFXVideoCORE_SyncOperation(
          QSVSession, QSVTaskPool[QSVSyncTaskID].SyncPoint, 100);
      debug("SyncAndSwap: task=%d SyncOperation sts=%d", QSVSyncTaskID, SyncStatus);
      if (SyncStatus < MFX_ERR_NONE) {
        const auto &bs = QSVTaskPool[QSVSyncTaskID].Bitstream;
        warn("SyncAndSwap FAILED: sts=%d task=%d syncID=%d "
             "bs[MaxLength=%u DataLen=%u DataOff=%u] poolSize=%zu",
             SyncStatus, QSVSyncTaskID, QSVSyncTaskID,
             bs.MaxLength, bs.DataLength, bs.DataOffset,
             QSVTaskPool.size());
        int pending = 0;
        for (const auto &t : QSVTaskPool) {
          if (t.SyncPoint != nullptr) pending++;
        }
        warn("SyncAndSwap FAILED: pending tasks=%d/%zu AsyncDepth=%d "
             "BufferSizeInKB=%u BRCMult=%u",
             pending, QSVTaskPool.size(),
             QSVEncodeParams.AsyncDepth,
             QSVEncodeParams.mfx.BufferSizeInKB,
             QSVEncodeParams.mfx.BRCParamMultiplier);
        profile_end("qsv_sync_task");
        return SyncStatus;
      }
      if (SyncStatus != MFX_WRN_IN_EXECUTION) {
        break;
      }
    }

    // ─ Extract per-frame QP from the synced bitstream ─
    {
      auto &taskBS = QSVTaskPool[QSVSyncTaskID].Bitstream;
      if (taskBS.ExtParam && taskBS.NumExtParam > 0) {
        auto *encInfo =
            reinterpret_cast<mfxExtEncodedFrameInfo *>(taskBS.ExtParam[0]);
        if (encInfo &&
            encInfo->Header.BufferId == MFX_EXTBUFF_ENCODED_FRAME_INFO) {
          UpdateFrameQPStats(taskBS.FrameType, encInfo->QP);
        }
      }
    }

    mfxU8 *DataTemp = QSVBitstream.Data;
    QSVBitstream = QSVTaskPool[QSVSyncTaskID].Bitstream;

    QSVTaskPool[QSVSyncTaskID].Bitstream.Data = DataTemp;
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataLength = 0;
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataOffset = 0;
    QSVTaskPool[QSVSyncTaskID].SyncPoint = nullptr;
  }

  *Bitstream = &QSVBitstream;

  profile_end("qsv_sync_task");
  return MFX_ERR_NONE;
}

mfxStatus QSVEncoder::EncodeFrameRetryLoop(mfxFrameSurface1 *Surface,
                                            mfxEncodeCtrl *Ctrl, int TaskID,
                                            mfxU32 MaxRetries) {
  // Backoff constants when device is busy
  constexpr mfxU32 YIELD_THRESHOLD = 10;       // yield timeslice for first 10 attempts
  constexpr mfxU32 MAX_BACKOFF_MS = 64;        // max backoff 64ms
  constexpr mfxU32 BITSTREAM_GROW_FACTOR = 2;  // bitstream buffer growth factor

  mfxU32 EncodeRetryCount = 0;
  for (;;) {
    if (++EncodeRetryCount > MaxRetries) {
      error("Encode retry count exceeded");
      throw std::runtime_error("Encode(): retry count exceeded");
    }

    // Clear SyncPoint before each retry: if the driver partially sets the
    // sync point and then returns an error, a stale/invalid SyncPoint could
    // remain in the task pool and cause MFX_ERR_NULL_PTR when synced later.
    QSVTaskPool[TaskID].SyncPoint = nullptr;

    mfxStatus Status = QSVEncode->EncodeFrameAsync(
        Ctrl, Surface, &QSVTaskPool[TaskID].Bitstream,
        &QSVTaskPool[TaskID].SyncPoint);

    if (MFX_ERR_NONE == Status) [[likely]] {
      break;
    }

    // ── MFX_WRN_DEVICE_BUSY: always retry, regardless of sync point ──
    // Some drivers (especially with EncTools) may set the sync
    // point pointer even when returning DEVICE_BUSY.  The old code
    // required !SyncPoint to enter the DEVICE_BUSY path, so a non-null
    // sync point would fall through to the "async submit" branch below
    // and break out of the retry loop with a stale/invalid sync point.
    // That later caused MFX_ERR_NULL_PTR in SyncAndSwapPendingTask.
    if (MFX_WRN_DEVICE_BUSY == Status) [[unlikely]] {
      QSVTaskPool[TaskID].SyncPoint = nullptr;
      // Exponential backoff: yield for YIELD_THRESHOLD attempts, then
      // sleep for (1,2,4,8,...,MAX_BACKOFF_MS) to avoid busy-waiting
      if (EncodeRetryCount <= YIELD_THRESHOLD) {
        Sleep(0);
      } else {
        mfxU32 shift = std::min<mfxU32>(
            (EncodeRetryCount - YIELD_THRESHOLD - 1) / 2, 6);
        mfxU32 backoffMs = std::min<mfxU32>(
            static_cast<mfxU32>(1) << shift, MAX_BACKOFF_MS);
        Sleep(backoffMs);
      }
      continue;
    }

    // ── Warnings (> MFX_ERR_NONE) other than DEVICE_BUSY with sync point → async submit ──
    if (MFX_ERR_NONE < Status && QSVTaskPool[TaskID].SyncPoint) [[likely]] {
      debug("EncodeFrameAsync[%d] sync=0x%p warning=0x%x (async submit)", TaskID,
            (void *)QSVTaskPool[TaskID].SyncPoint, Status);
      Status = MFX_ERR_NONE;
      break;
    }

    // Non-BUSY warning without sync point – driver should not do this, but
    // handle gracefully: reset sync point and retry.
    if (MFX_ERR_NONE < Status) [[unlikely]] {
      debug("EncodeFrameAsync[%d] warning=0x%x no sync point, retrying", TaskID, Status);
      QSVTaskPool[TaskID].SyncPoint = nullptr;
      continue;
    }

    // ── Buffer too small → grow and retry ──
    if (MFX_ERR_NOT_ENOUGH_BUFFER == Status ||
        MFX_ERR_MORE_BITSTREAM == Status) [[unlikely]] {
      // ChangeBitstreamSize modifies QSVBitstream which is shared state
      // (swapped between QSVBitstream and task bitstreams in
      // SyncAndSwapPendingTask). Lock to prevent concurrent swap.
      mfxU32 newSize;
      {
        std::lock_guard<std::mutex> lock(QSVTaskPoolMutex);
        newSize = static_cast<mfxU32>(
            QSVBitstream.MaxLength * BITSTREAM_GROW_FACTOR);
      }
      ChangeBitstreamSize(newSize);
      warn("The bitstream buffer size is too small. The size has been "
           "increased by %d times. New value: %d KB",
           BITSTREAM_GROW_FACTOR, (newSize / 8 / 1000));
    } else if (MFX_ERR_MORE_DATA == Status) [[unlikely]] {
      break;
    } else [[unlikely]] {
      const auto &bs = QSVTaskPool[TaskID].Bitstream;
      error("EncodeFrameAsync FATAL: sts=%d task=%d retry=%u/%u "
            "bs[MaxLength=%u DataLen=%u DataOff=%u]",
            Status, TaskID, EncodeRetryCount, MaxRetries,
            bs.MaxLength, bs.DataLength, bs.DataOffset);
      error("EncodeFrameAsync FATAL: RC=%u TargetKbps=%u MaxKbps=%u "
            "CodecId=0x%08X LowPower=%d AsyncDepth=%d",
            QSVEncodeParams.mfx.RateControlMethod,
            QSVEncodeParams.mfx.TargetKbps,
            QSVEncodeParams.mfx.MaxKbps,
            QSVEncodeParams.mfx.CodecId,
            QSVEncodeParams.mfx.LowPower,
            QSVEncodeParams.AsyncDepth);
      throw std::runtime_error("Encode(): EncodeFrameAsync fatal error");
    }
  }
  return MFX_ERR_NONE;
}

mfxStatus QSVEncoder::EncodeTexture(mfxU64 TS, void *TextureHandle,
                                    uint64_t LockKey, uint64_t *NextKey,
                                    mfxBitstream **Bitstream) {
  profile_start("qsv_encode_tex");
  mfxStatus Status = MFX_ERR_NONE, SyncStatus = MFX_ERR_NONE;
  *Bitstream = nullptr;
  int TaskID = 0;

#if defined(_WIN32) || defined(_WIN64)
  mfxSurfaceD3D11Tex2D Texture = {};
  Texture.SurfaceInterface.Header.SurfaceType = MFX_SURFACE_TYPE_D3D11_TEX2D;
  Texture.SurfaceInterface.Header.SurfaceFlags = MFX_SURFACE_FLAG_IMPORT_SHARED;
  Texture.SurfaceInterface.Header.StructSize = sizeof(mfxSurfaceD3D11Tex2D);
#else
  mfxSurfaceVAAPI Texture{};
  Texture.SurfaceInterface.Header.SurfaceType = MFXSURFACE_TYPE_VAAPI;
  Texture.SurfaceInterface.Header.SurfaceFlags = MFXSURFACE_FLAG_IMPORT_COPY;
  Texture.SurfaceInterface.Header.StructSize = sizeof(mfxSurfaceVAAPI);
#endif

  while (GetFreeTaskIndex(&TaskID) == MFX_ERR_NOT_FOUND) {
    SyncStatus = SyncAndSwapPendingTask(Bitstream);
    if (SyncStatus < MFX_ERR_NONE) {
      error("Encode[%d].SyncAndSwap error: %d (pool=%zu syncID=%d)",
            TaskID, SyncStatus,
            QSVTaskPool.size(), QSVSyncTaskID);
      throw std::runtime_error(
          "Encode(): Sync operation failed - unrecoverable error");
    }
  }

  try {
    HWManager->CopyTexture(Texture, TextureHandle, LockKey,
                           static_cast<mfxU64 *>(NextKey));
  } catch (const std::exception &e) {
    // Note: the exception occurred during CopyTexture, Status has not been
    // assigned yet, so do not output a meaningless "Error code: 0"
    error("Encode(): CopyTexture failed: %s", e.what());
    throw;
  }

  Status = QSVMemoryInterface->ImportFrameSurface(
      QSVMemoryInterface, MFX_SURFACE_COMPONENT_ENCODE,
      reinterpret_cast<mfxSurfaceHeader *>(&Texture), &QSVEncodeSurface);
  if (Status < MFX_ERR_NONE) {
    error("Error code: %d", Status);
    throw std::runtime_error("Encode(): Texture import error");
  }

  QSVTaskPool[TaskID].Bitstream.TimeStamp = TS;
  QSVEncodeSurface->Data.TimeStamp = TS;

  try {
    if (QSVProcessingEnable) {
      Status = QSVProcessing->GetSurfaceOut(&QSVProcessingSurface);

      for (;;) {
        Status = QSVProcessing->RunFrameVPPAsync(
            QSVEncodeSurface, QSVProcessingSurface, QSVProcessingAuxData,
            &QSVProcessingSyncPoint);
        if (MFX_ERR_NONE == Status) {
          break;
        } else if (Status < MFX_ERR_NONE) {
          error("Error code: %d", Status);
          throw std::runtime_error("Encode(): VPP processing error");
        }
      }

      // Release VPP input surface immediately – RunFrameVPPAsync holds
      // its own internal reference.
      QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
      QSVEncodeSurface = nullptr;
    }

    bool roiActive = !CachedROIRegions.empty();
    if (roiActive)
      SetupROIEncodeCtrl();
    EncodeFrameRetryLoop(
        (QSVProcessingEnable ? QSVProcessingSurface : QSVEncodeSurface),
        roiActive ? &QSVEncodeCtrlParams : nullptr,
        TaskID, 200);

    // Defer VPP sync until after encode submission so VPP and Encode
    // overlap in the GPU pipeline.
    if (QSVProcessingEnable) {
      mfxStatus SyncSts;
      do {
        SyncSts = MFXVideoCORE_SyncOperation(
            QSVSession, QSVProcessingSyncPoint, 100);
      } while (SyncSts == MFX_WRN_IN_EXECUTION);
      if (SyncSts < MFX_ERR_NONE) {
        error("VPP sync error: %d", SyncSts);
        throw std::runtime_error("Encode(): VPP sync failed");
      }

      QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
    } else {
      QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
    }

  } catch (...) {
    if (QSVProcessingEnable && QSVProcessingSurface) {
      QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
      QSVProcessingSurface = nullptr;
    }
    if (QSVEncodeSurface) {
      QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
    }
    QSVEncodeSurface = nullptr;
    throw;
  }

  profile_end("qsv_encode_tex");
  return Status;
}

mfxStatus QSVEncoder::EncodeFrame(mfxU64 TS, uint8_t **FrameData,
                                  uint32_t *FrameLinesize,
                                  mfxBitstream **Bitstream) {
#ifdef QSV_UHD600_SUPPORT
  if (QSVUseSystemMemoryPath) {
    return EncodeFrameSystemMemory(TS, FrameData, FrameLinesize, Bitstream);
  }
#endif

  mfxStatus Status = MFX_ERR_NONE, SyncStatus = MFX_ERR_NONE;
  *Bitstream = nullptr;
  int TaskID = 0;

  Status = QSVEncode->GetSurface(&QSVEncodeSurface);
  if (Status < MFX_ERR_NONE) {
    error("Error code: %d", Status);
    throw std::runtime_error("Encode(): Get encode surface error");
  }

  while (GetFreeTaskIndex(&TaskID) == MFX_ERR_NOT_FOUND) {
    SyncStatus = SyncAndSwapPendingTask(Bitstream);
    if (SyncStatus < MFX_ERR_NONE) {
      error("EncodeFrame[%d].SyncAndSwap error: %d (pool=%zu syncID=%d)",
            TaskID, SyncStatus, QSVTaskPool.size(), QSVSyncTaskID);
      if (QSVEncodeSurface) {
        QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
        QSVEncodeSurface = nullptr;
      }
      throw std::runtime_error(
          "Encode(): Sync operation failed - unrecoverable error");
    }
  }

  Status =
      QSVEncodeSurface->FrameInterface->Map(QSVEncodeSurface, MFX_MAP_WRITE);
  if (Status < MFX_ERR_NONE) {
    warn("Surface.Map.Write error: %d", Status);
    QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
    QSVEncodeSurface = nullptr;
    return Status;
  }

  QSVEncodeSurface->Data.TimeStamp = TS;
  LoadFrameData(QSVEncodeSurface, FrameData, FrameLinesize);

  QSVTaskPool[TaskID].Bitstream.TimeStamp = TS;

  Status = QSVEncodeSurface->FrameInterface->Unmap(QSVEncodeSurface);
  if (Status < MFX_ERR_NONE) {
    warn("Surface.Unmap.Write error: %d", Status);
    QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
    QSVEncodeSurface = nullptr;
    return Status;
  }

  if (QSVProcessingEnable) {
    do {
      Status = QSVProcessing->GetSurfaceOut(&QSVProcessingSurface);

      if (Status < MFX_ERR_NONE) {
        error("Error code: %d", Status);
        QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
        QSVEncodeSurface = nullptr;
        throw std::runtime_error("Encode(): Get processing surface error");
      }

      Status = QSVProcessing->RunFrameVPPAsync(QSVEncodeSurface,
                                               QSVProcessingSurface, nullptr,
                                               &QSVProcessingSyncPoint);
      if (Status < MFX_ERR_NONE && Status != MFX_ERR_MORE_SURFACE) {
        error("Processing error: %d", Status);
        QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
        QSVProcessingSurface = nullptr;
        QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
        QSVEncodeSurface = nullptr;
        throw std::runtime_error("Encode(): Processing error");
      }
    } while (Status == MFX_ERR_MORE_SURFACE);

    // Release VPP input surface immediately – VPP holds its own reference.
    QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
    QSVEncodeSurface = nullptr;
  }

  /*Encode a frame asynchronously (returns immediately)*/
  bool roiActive = !CachedROIRegions.empty();
  if (roiActive)
    SetupROIEncodeCtrl();
  EncodeFrameRetryLoop(
      (QSVProcessingEnable ? QSVProcessingSurface : QSVEncodeSurface),
      roiActive ? &QSVEncodeCtrlParams : nullptr,
      TaskID, 200);

  // Defer VPP sync until after encode submission so VPP and Encode
  // overlap in the GPU pipeline.
  if (QSVProcessingEnable) {
    do {
      SyncStatus = MFXVideoCORE_SyncOperation(
          QSVSession, QSVProcessingSyncPoint, 100);
    } while (SyncStatus == MFX_WRN_IN_EXECUTION);
    if (SyncStatus < MFX_ERR_NONE) {
      error("VPP sync error: %d", SyncStatus);
      QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
      QSVProcessingSurface = nullptr;
      throw std::runtime_error("Encode(): VPP sync failed");
    }

    QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
  } else {
    QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
    QSVEncodeSurface = nullptr;
  }

  return MFX_ERR_NONE;
}

void QSVEncoder::SetupROIEncodeCtrl() {
  if (CachedROIRegions.empty())
    return;

  auto roiCount = static_cast<mfxU16>(
      std::min(CachedROIRegions.size(), static_cast<size_t>(256)));

  // mfxExtEncoderROI ends with ROI[1] (flexible array member).
  // sizeof() only accounts for 1 ROI slot. We must allocate extra space
  // for the remaining ROIs, otherwise writing ROI[1..N] corrupts the heap.
  // Note: mfxROI may not be a named type in all oneVPL versions (some define
  // the ROI member as anonymous struct), so derive entry size from the member.
  constexpr mfxU32 roiEntrySize =
      sizeof(((mfxExtEncoderROI *)nullptr)->ROI[0]);
  mfxU32 requiredSize = sizeof(mfxExtEncoderROI) +
                        (roiCount > 1 ? (roiCount - 1) * roiEntrySize : 0);

  mfxExtEncoderROI *roiParams = nullptr;

  // Remove existing ROI buffer so AddExtBuffer re-allocates at the correct size
  auto *existing = QSVEncodeCtrlParams.GetExtBuffer<mfxExtEncoderROI>();
  if (existing) {
    if (existing->Header.BufferSz >= requiredSize) {
      // Reuse existing buffer: zero and reset header fields
      memset(existing, 0, existing->Header.BufferSz);
      existing->Header.BufferId = MFX_EXTBUFF_ENCODER_ROI;
      existing->Header.BufferSz = requiredSize;
      roiParams = existing;
    } else {
      QSVEncodeCtrlParams.RemoveExtBuffer<mfxExtEncoderROI>();
    }
  }

  if (!roiParams) {
    auto *raw = QSVEncodeCtrlParams.AddExtBuffer(MFX_EXTBUFF_ENCODER_ROI,
                                                  requiredSize);
    if (!raw) {
      warn("[QSV VPL] SetupROIEncodeCtrl: AddExtBuffer failed!");
      return;
    }
    // AddExtBuffer already zeroed the memory; just set the header
    raw->BufferId = MFX_EXTBUFF_ENCODER_ROI;
    raw->BufferSz = requiredSize;
    roiParams = reinterpret_cast<mfxExtEncoderROI *>(raw);
  }

  roiParams->NumROI = roiCount;
  roiParams->ROIMode = CachedROIMode;

  for (mfxU16 i = 0; i < roiCount; i++) {
    roiParams->ROI[i].Left   = CachedROIRegions[i].Left;
    roiParams->ROI[i].Top    = CachedROIRegions[i].Top;
    roiParams->ROI[i].Right  = CachedROIRegions[i].Right;
    roiParams->ROI[i].Bottom = CachedROIRegions[i].Bottom;
    roiParams->ROI[i].DeltaQP = CachedROIRegions[i].DeltaQP;
  }

  // Only log on the first call per instance (avoid log spam)
  if (!ROIFirstLogDone) {
    ROIFirstLogDone = true;
    blog(LOG_INFO,
         "[QSV VPL] SetupROIEncodeCtrl: NumROI=%d, ROIMode=%d, "
         "BufferSz=%u, NumExtParam=%d, regions=%zu",
         (int)roiParams->NumROI, (int)roiParams->ROIMode,
         requiredSize,
         (int)QSVEncodeCtrlParams.NumExtParam,
         CachedROIRegions.size());
    for (int i = 0; i < roiParams->NumROI && i < 4; i++) {
      blog(LOG_INFO,
           "[QSV VPL]   ROI[%d]: Left=%u Top=%u Right=%u Bottom=%u DeltaQP=%d",
           i, roiParams->ROI[i].Left, roiParams->ROI[i].Top,
           roiParams->ROI[i].Right, roiParams->ROI[i].Bottom,
           (int)roiParams->ROI[i].DeltaQP);
    }
  }
}

// Per-frame QP tracking

void QSVEncoder::UpdateFrameQPStats(mfxU16 frameType, mfxU16 qp) {
  FrameQPStats.totalFrames++;

  // MFX_FRAMETYPE flags can be combined (e.g. I+REF), check primary type first.
  QPFrameTypeStats *bucket = nullptr;

  if (frameType & MFX_FRAMETYPE_I || frameType & MFX_FRAMETYPE_IDR ||
      frameType & MFX_FRAMETYPE_xI || frameType & MFX_FRAMETYPE_xIDR) {
    bucket = &FrameQPStats.i;
  } else if (frameType & MFX_FRAMETYPE_P || frameType & MFX_FRAMETYPE_xP) {
    bucket = &FrameQPStats.p;
  } else if (frameType & MFX_FRAMETYPE_B || frameType & MFX_FRAMETYPE_xB) {
    bucket = &FrameQPStats.b;
  } else {
    // Unknown frame type
    return;
  }

  bucket->count++;
  bucket->sumQP += qp;
  if (qp < bucket->minQP) bucket->minQP = qp;
  if (qp > bucket->maxQP) bucket->maxQP = qp;
  if (qp < QP_HISTOGRAM_SIZE) bucket->histogram[qp]++;
}

void QSVEncoder::LogQPStats() {
  auto logType = [](const char *label, const QPFrameTypeStats &s) {
    if (s.count == 0) {
      blog(LOG_INFO, "[QSV VPL] QPStats: %s — no frames encoded", label);
      return;
    }
    double avg = static_cast<double>(s.sumQP) / static_cast<double>(s.count);
    // Compute median from histogram
    mfxU16 median = 0;
    {
      uint64_t target = (s.count + 1) / 2;
      uint64_t cumulative = 0;
      for (size_t qp = 0; qp < QP_HISTOGRAM_SIZE; qp++) {
        cumulative += s.histogram[qp];
        if (cumulative >= target) {
          median = static_cast<mfxU16>(qp);
          break;
        }
      }
    }
    blog(LOG_INFO,
         "[QSV VPL] QPStats: %s  count=%llu  min=%u  max=%u  avg=%.2f  median=%u",
         label,
         static_cast<unsigned long long>(s.count),
         s.minQP, s.maxQP, avg, median);
  };

  blog(LOG_INFO,
       "[QSV VPL] QPStats: === Per-frame QP summary (total %llu frames) ===",
       static_cast<unsigned long long>(FrameQPStats.totalFrames));
  logType("I-frames", FrameQPStats.i);
  logType("P-frames", FrameQPStats.p);
  logType("B-frames", FrameQPStats.b);
}

// Append a User Data Unregistered SEI NAL with cumulative QP stats.
// Every frame gets one so the last frame carries the final summary.
//
// AVC:  [00 00 00 01] 06 05 <size> <uuid> <data> 80
// HEVC: [00 00 00 01] 4E 01 05 <size> <uuid> <data> 80

void QSVEncoder::AppendQpSeiToBitstream(mfxBitstream &bs) {
  mfxU32 freeSpace = bs.MaxLength - bs.DataOffset - bs.DataLength;
  if (freeSpace < QSV_SEI_EXTRA)
    return;

  auto fmtType = [](const QPFrameTypeStats &s, char label,
                    std::string &out) {
    if (s.count == 0)
      return;
    double avg = static_cast<double>(s.sumQP) /
                 static_cast<double>(s.count);
    // Compute median from histogram (O(QP_HISTOGRAM_SIZE) = O(1))
    mfxU16 median = 0;
    {
      uint64_t target = (s.count + 1) / 2;
      uint64_t cumulative = 0;
      for (size_t qp = 0; qp < QP_HISTOGRAM_SIZE; qp++) {
        cumulative += s.histogram[qp];
        if (cumulative >= target) {
          median = static_cast<mfxU16>(qp);
          break;
        }
      }
    }
    // Format: "I:cnt,min,max,avg,med|"
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%c:%llu,%u,%u,%.2f,%u|",
                     label,
                     static_cast<unsigned long long>(s.count),
                     s.minQP, s.maxQP, avg, median);
    out.append(buf, n);
  };

  std::string &payload = QpSeiPayload;
  if (payload.capacity() < 128) {
    payload.reserve(128);
  }
  payload.clear();
  payload += "QSVQP|";
  fmtType(FrameQPStats.i, 'I', payload);
  fmtType(FrameQPStats.p, 'P', payload);
  fmtType(FrameQPStats.b, 'B', payload);
  // Remove trailing '|' if any
  if (!payload.empty() && payload.back() == '|')
    payload.pop_back();

  // Determine codec and build the SEI NAL
  mfxU32 codecId = QSVEncodeParams.mfx.CodecId;
  bool isHEVC = (codecId == MFX_CODEC_HEVC);
  if (codecId == MFX_CODEC_AV1)
    return;

  // start code + NAL header + SEI type + size + uuid + data + trailing
  uint8_t buf[QSV_SEI_EXTRA];
  size_t pos = 0;

  buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x01;

  if (isHEVC) {
    buf[pos++] = 0x4E; // nal_unit_type = 39 (SEI), nuh_layer_id = 0
    buf[pos++] = 0x01; // nuh_temporal_id_plus1 = 1
  } else {
    buf[pos++] = 0x06; // nal_unit_type = 6 (SEI)
  }

  buf[pos++] = 0x05; // SEI payload type: user_data_unregistered (5)

  // SEI payload size: multi-byte encoding for sizes > 255
  const size_t payload_size = 16 + payload.size(); // UUID + text
  size_t remaining = payload_size;
  while (remaining >= 255) {
    if (pos + 1 > QSV_SEI_EXTRA - payload_size) {
      // Buffer too small for size encoding + payload, skip injection
      return;
    }
    buf[pos++] = 0xFF;
    remaining -= 255;
  }
  if (pos + 1 + remaining > QSV_SEI_EXTRA) {
    return;
  }
  buf[pos++] = static_cast<uint8_t>(remaining & 0xFF);

  // UUID
  memcpy(buf + pos, QP_SEI_UUID, 16);
  pos += 16;

  // User data (the stats string)
  memcpy(buf + pos, payload.data(), payload.size());
  pos += payload.size();

  // RBSP trailing bits
  buf[pos++] = 0x80;

  // Append to the bitstream buffer
  uint8_t *dst = bs.Data + bs.DataOffset + bs.DataLength;
  memcpy(dst, buf, pos);
  bs.DataLength += static_cast<mfxU32>(pos);

  // Keep a copy for external retrieval
  QpStatsSeiBuffer.assign(buf, buf + pos);
}

void QSVEncoder::GetQpStatsSei(uint8_t **data, size_t *size) {
  if (QpStatsSeiBuffer.empty()) {
    *data = nullptr;
    *size = 0;
    return;
  }
  // Allocate a copy that the caller must free
  *data = static_cast<uint8_t *>(malloc(QpStatsSeiBuffer.size()));
  if (*data) {
    memcpy(*data, QpStatsSeiBuffer.data(), QpStatsSeiBuffer.size());
    *size = QpStatsSeiBuffer.size();
  } else {
    *size = 0;
  }
}

mfxStatus QSVEncoder::Drain() {
  mfxStatus Status = MFX_ERR_NONE;

  // Drain the encoder: repeatedly submit nullptr surface until MFX_ERR_MORE_DATA
  // (OneVPL spec: in drain mode, MORE_DATA means no more buffered frames)
  constexpr int MAX_DRAIN_ITERS = 1024;
  int iter = 0;
  while (Status >= MFX_ERR_NONE && iter++ < MAX_DRAIN_ITERS) {
    mfxSyncPoint SyncPoint = nullptr;
    Status = QSVEncode->EncodeFrameAsync(
        nullptr, nullptr, nullptr, &SyncPoint);
    if (Status == MFX_ERR_NONE && SyncPoint != nullptr) {
      mfxStatus SyncSts = MFXVideoCORE_SyncOperation(QSVSession, SyncPoint, 5000);
      // SyncOperation may return MFX_ERR_NULL_PTR on some drivers when the
      // sync point is a no-op during drain. This is benign.
      if (SyncSts < MFX_ERR_NONE) {
        warn("Drain sync warning: %d", SyncSts);
      }
    }
  }

  if (iter >= MAX_DRAIN_ITERS) {
    warn("Drain: exceeded max iterations (%d), driver may be stuck",
         MAX_DRAIN_ITERS);
  }

  // MFX_ERR_MORE_DATA is the normal drain exit condition
  if (Status != MFX_ERR_MORE_DATA && Status != MFX_ERR_NULL_PTR) {
    warn("Drain: unexpected exit status: %d", Status);
  }
  Status = MFX_ERR_NONE;

  // Sync and extract QP from any remaining pending tasks
  for (auto &Task : QSVTaskPool) {
    if (Task.SyncPoint != nullptr) {
      mfxStatus SyncSts = MFXVideoCORE_SyncOperation(
          QSVSession, Task.SyncPoint, 5000);
      if (SyncSts >= MFX_ERR_NONE) {
        // Extract QP from this task's bitstream
        if (Task.Bitstream.ExtParam && Task.Bitstream.NumExtParam > 0) {
          auto *encInfo =
              reinterpret_cast<mfxExtEncodedFrameInfo *>(
                  Task.Bitstream.ExtParam[0]);
          if (encInfo &&
              encInfo->Header.BufferId == MFX_EXTBUFF_ENCODED_FRAME_INFO) {
            UpdateFrameQPStats(Task.Bitstream.FrameType, encInfo->QP);
          }
        }
      } else {
        warn("Drain sync warning: %d", SyncSts);
      }
      Task.SyncPoint = nullptr;
    }
  }

  LogQPStats();

  // Rebuild the SEI buffer with the final cumulative stats
  AppendQpSeiToBitstream(QSVBitstream);

  return Status;
}

// Submit a dummy frame surface during initialization so the GPU driver
// allocates internal resources (shaders, command buffers, HW state) now
// rather than on the first real frame, eliminating the visible stutter.
//
// IMPORTANT: Must NOT call EncodeFrameAsync. Doing so would consume the
// first IDR slot (with SPS/PPS), causing the first real frame to be a
// P/B-frame without headers — the decoder sees a green screen.
void QSVEncoder::WarmUpEncoder() {
  // Texture-encoder path
  if (QSVIsTextureEncoder && HWManager) {
    const auto &Pool = HWManager->GetTexturePool();
    if (Pool.empty() || Pool[0] == nullptr)
      return;

    // Import one pool texture so VPL pre-allocates internal tracking
    mfxSurfaceD3D11Tex2D DummyTex = {};
    DummyTex.SurfaceInterface.Header.SurfaceType =
        MFX_SURFACE_TYPE_D3D11_TEX2D;
    DummyTex.SurfaceInterface.Header.SurfaceFlags =
        MFX_SURFACE_FLAG_IMPORT_SHARED;
    DummyTex.SurfaceInterface.Header.StructSize =
        sizeof(mfxSurfaceD3D11Tex2D);
    DummyTex.texture2D = Pool[0];

    mfxFrameSurface1 *Surf = nullptr;
    mfxStatus sts = QSVMemoryInterface->ImportFrameSurface(
        QSVMemoryInterface, MFX_SURFACE_COMPONENT_ENCODE,
        reinterpret_cast<mfxSurfaceHeader *>(&DummyTex), &Surf);
    if (sts >= MFX_ERR_NONE) {
      Surf->FrameInterface->Release(Surf);
      info("Encoder warm-up (texture) — ImportFrameSurface done");
    } else {
      warn("WarmUpEncoder: ImportFrameSurface failed (sts=%d)", sts);
    }
    return;
  }

  // Frame-encoder path (video / system memory)
  if (!QSVEncode)
    return;
#ifdef QSV_UHD600_SUPPORT
  if (QSVUseSystemMemoryPath)
    return;
#endif
  mfxFrameSurface1 *Surf = nullptr;
  mfxStatus sts = QSVEncode->GetSurface(&Surf);
  if (sts < MFX_ERR_NONE) {
    warn("WarmUpEncoder: GetSurface failed (sts=%d)", sts);
    return;
  }

  // Map + write + unmap forces the driver to set up internal
  // page-table / DMA mappings that are otherwise lazily deferred.
  const mfxFrameInfo &fi = Surf->Info;
  if (fi.FourCC == MFX_FOURCC_NV12 || fi.FourCC == MFX_FOURCC_P010) {
    sts = Surf->FrameInterface->Map(Surf, MFX_MAP_WRITE);
    if (sts >= MFX_ERR_NONE) {
      mfxU16 h = fi.CropH > 0 ? fi.CropH : fi.Height;
      mfxU32 pitch = Surf->Data.Pitch;
      bool is10bit = (fi.FourCC == MFX_FOURCC_P010);
      memset(Surf->Data.Y, is10bit ? 64 : 16,
             static_cast<size_t>(h) * pitch);
      memset(Surf->Data.UV, is10bit ? 512 : 128,
             static_cast<size_t>(h / 2) * pitch);
      Surf->FrameInterface->Unmap(Surf);
    }
  }

  Surf->FrameInterface->Release(Surf);
}

mfxStatus QSVEncoder::ClearData() {
  mfxStatus Status = MFX_ERR_NONE;

  if (QSVEncode) {
    Drain();
    Status = QSVEncode->Close();
    QSVEncode = nullptr;
  }

  if (QSVProcessing) {
    Status = QSVProcessing->Close();
    QSVProcessing = nullptr;
  }

  ReleaseTaskPool();
  ReleaseBitstream();

#ifdef QSV_UHD600_SUPPORT
  ReleaseSystemMemorySurfacePool();
#endif

  if (Status >= MFX_ERR_NONE) {
    HWManager::HWEncoderCounter--;
  }

  if (QSVSession) {
    Status = MFXClose(QSVSession);
    if (Status >= MFX_ERR_NONE) {
      if (QSVLoader) {
        MFXDispReleaseImplDescription(QSVLoader, nullptr);
        MFXUnload(QSVLoader);
      }
      QSVSession = nullptr;
      QSVLoader = nullptr;
    }
  }

#if defined(__linux__)
  ReleaseSessionData(QSVSessionData);
  QSVSessionData = nullptr;
#endif

  if (QSVIsTextureEncoder) {
    HWManager->ReleaseTexturePool();

    if (HWManager::HWEncoderCounter <= 0) {
      HWManager = nullptr;
      HWManager::HWEncoderCounter = 0;
    }
  }

  return Status;
}
