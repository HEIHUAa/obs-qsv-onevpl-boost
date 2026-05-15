#pragma warning(disable : 4996)
#include "obs-qsv-onevpl-encoder-internal.hpp"

QSVEncoder::QSVEncoder()
    : QSVPlatform(), QSVVersion(), QSVLoader(), QSVLoaderConfig(),
      QSVLoaderVariant(), QSVSession(nullptr), QSVImpl(), QSVEncodeSurface(),
      QSVEncodeRefCount(0), QSVProcessingSurface(), QSVProcessingRefCount(0),
      QSVEncode(nullptr), QSVProcessing(nullptr), QSVVPSBuffer(),
      QSVSPSBuffer(), QSVPPSBuffer(), QSVVPSBufferSize(1024),
      QSVSPSBufferSize(1024), QSVPPSBufferSize(1024), QSVBitstream(),
      QSVTaskPool(), QSVSyncTaskID(), QSVResetParams(),
      QSVResetParamsChanged(false), QSVEncodeParams(), QSVEncodeCtrlParams(),
      QSVProcessingAuxData(), QSVAllocateRequest(), QSVIsTextureEncoder(),
      QSVMemoryInterface(), HWManager(nullptr), QSVProcessingEnable(),
      QSVProcessingSyncPoint(nullptr) {}

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

  for (mfxU16 i = 0; i < QSVSystemMemPoolSize; i++) {
    SystemMemSurface S = {};
    S.Surface.Info = FI;

    mfxU32 bpp = (FI.FourCC == MFX_FOURCC_P010) ? 2 : 1;
    mfxU32 Align = FI.Width * bpp;
    mfxU32 Pitch = Align + ((Align % 16) ? (16 - Align % 16) : 0);
    mfxU32 YSize = Pitch * FI.Height;
    mfxU32 UVSize = Pitch * (FI.Height / 2);
    mfxU8 *Buffer = new mfxU8[YSize + UVSize];
    S.Surface.Data.Y = Buffer;
    S.Surface.Data.UV = Buffer + YSize;
    S.Surface.Data.Pitch = static_cast<mfxU16>(Pitch);
    QSVSystemMemPool.push_back(std::move(S));
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
  QSVLoader = MFXLoad();
  if (QSVLoader == nullptr) {
    throw std::runtime_error("GetVPLSession(): MFXLoad error");
  }
  Status = MFXCreateSession(QSVLoader, 0, &QSVSession);
  if (Status >= MFX_ERR_NONE) {
    MFXQueryVersion(QSVSession, &QSVVersion);
    Version = QSVVersion;
    MFXClose(QSVSession);
    MFXUnload(QSVLoader);
  } else {
    throw std::runtime_error("GetVPLSession(): MFXCreateSession error");
  }

  return Status;
}

mfxStatus QSVEncoder::CreateSession([[maybe_unused]] enum codec_enum Codec,
                                    [[maybe_unused]] void **Data, int GPUNum) {
  mfxStatus Status = MFX_ERR_NONE;
  try {
    // First attempt: basic hardware filters
    {
      QSVLoader = MFXLoad();
      if (QSVLoader == nullptr) {
        return MFX_ERR_UNDEFINED_BEHAVIOR;
      }

      QSVLoaderConfig[0] = MFXCreateConfig(QSVLoader);
      QSVLoaderVariant[0].Type = MFX_VARIANT_TYPE_U32;
      QSVLoaderVariant[0].Data.U32 = MFX_IMPL_TYPE_HARDWARE;
      MFXSetConfigFilterProperty(
          QSVLoaderConfig[0],
          reinterpret_cast<const mfxU8 *>("mfxImplDescription.Impl"),
          QSVLoaderVariant[0]);

      QSVLoaderConfig[1] = MFXCreateConfig(QSVLoader);
      QSVLoaderVariant[1].Type = MFX_VARIANT_TYPE_U32;
      QSVLoaderVariant[1].Data.U32 = static_cast<mfxU32>(0x8086);
      MFXSetConfigFilterProperty(
          QSVLoaderConfig[1],
          reinterpret_cast<const mfxU8 *>("mfxImplDescription.VendorID"),
          QSVLoaderVariant[1]);

      QSVLoaderConfig[2] = MFXCreateConfig(QSVLoader);
      QSVLoaderVariant[2].Type = MFX_VARIANT_TYPE_PTR;
      QSVLoaderVariant[2].Data.Ptr = mfxHDL("mfx-gen");
      MFXSetConfigFilterProperty(
          QSVLoaderConfig[2],
          reinterpret_cast<const mfxU8 *>("mfxImplDescription.ImplName"),
          QSVLoaderVariant[2]);

#if defined(_WIN32) || defined(_WIN64)
      if (QSVIsTextureEncoder) {
        QSVLoaderConfig[3] = MFXCreateConfig(QSVLoader);
        QSVLoaderVariant[3].Type = MFX_VARIANT_TYPE_U32;
        QSVLoaderVariant[3].Data.U32 = MFX_ACCEL_MODE_VIA_D3D11;
        MFXSetConfigFilterProperty(
            QSVLoaderConfig[3],
            reinterpret_cast<const mfxU8 *>(
                "mfxImplDescription.AccelerationMode"),
            QSVLoaderVariant[3]);
      }
#endif

      Status = MFXCreateSession(QSVLoader, GPUNum, &QSVSession);
    }

    if (Status < MFX_ERR_NONE) {
      error("Error code: %d", Status);
      throw std::runtime_error("CreateSession(): MFXCreateSession error");
    }

    MFXQueryIMPL(QSVSession, &QSVImpl);

    MFXVideoCORE_QueryPlatform(QSVSession, &QSVPlatform);
    info("\tAdapter type: %s",
         QSVPlatform.MediaAdapterType == MFX_MEDIA_DISCRETE ? "Discrete"
                                                            : "Integrate");

    return Status;
  } catch (const std::exception &) {
    throw;
  }
}

mfxStatus QSVEncoder::Init(encoder_params *InputParams, enum codec_enum Codec,
                           bool bIsTextureEncoder) {
  mfxStatus Status = MFX_ERR_NONE;

  QSVIsTextureEncoder = std::move(bIsTextureEncoder);
  QSVProcessingEnable = std::move(InputParams->ProcessingEnable);

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

      Status = QSVProcessing->Init(&QSVProcessingParams);
    }

#ifdef QSV_UHD600_SUPPORT
    QSVUseSystemMemoryPath = false;

    // === Step 1: Try original path (IOPattern=VIDEO_MEMORY, no ext buf removal) ===
    Status = SetEncoderParams(InputParams, Codec);
    info("\tSetEncoderParams status:  %d", Status);

    if (Status >= MFX_ERR_NONE) {
      Status = QSVEncode->Query(&QSVEncodeParams, &QSVEncodeParams);
      info("\tMFXVideoENCODE_Query status: %d", Status);

      if (Status == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        Status = MFX_ERR_NONE;
      }

      Status = QSVEncode->Init(&QSVEncodeParams);
      info("\tMFXVideoENCODE_Init status: %d", Status);
    }

    if (Status < MFX_ERR_NONE) {
      auto CO3Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
      if (CO3Params && CO3Params->ScenarioInfo != 0) {
        warn("MFXVideoENCODE_Init failed with ScenarioInfo=%d, retrying without ScenarioInfo",
             CO3Params->ScenarioInfo);
        QSVEncode->Close();
        CO3Params->ScenarioInfo = 0;

        Status = QSVEncode->Init(&QSVEncodeParams);
        info("\tMFXVideoENCODE_Init retry (ScenarioInfo) status: %d", Status);
      }
    }

    if (Status < MFX_ERR_NONE) {
      error("MFXVideoENCODE_Init failed (Status=%d)", Status);
      return Status;
    }

    // === Step 2: Test if GetSurface() works or fallback to system memory ===
    if (!QSVIsTextureEncoder) {
      bool NeedSystemMemoryFallback = false;

      if (Status >= MFX_ERR_NONE) {
        mfxFrameSurface1 *TestSurface = nullptr;
        mfxStatus GetSts = QSVEncode->GetSurface(&TestSurface);
        if (GetSts < MFX_ERR_NONE) {
          info("\tGetSurface() not supported (%d), switch to system memory path",
               GetSts);
          NeedSystemMemoryFallback = true;
        } else {
          info("\tGetSurface() supported, using original VIDEO_MEMORY path");
          TestSurface->FrameInterface->Release(TestSurface);
        }
      } else {
        info("\tOriginal Init failed (%d), switch to system memory path", Status);
        NeedSystemMemoryFallback = true;
      }

      if (NeedSystemMemoryFallback) {
        QSVUseSystemMemoryPath = true;
        if (QSVEncode) {
          QSVEncode->Close();
        }

        // Rebuild with system memory path
        Status = SetEncoderParams(InputParams, Codec);
        info("\tSetEncoderParams (sysmem) status: %d", Status);

        if (Status >= MFX_ERR_NONE) {
          Status = QSVEncode->Query(&QSVEncodeParams, &QSVEncodeParams);
          info("\tMFXVideoENCODE_Query (sysmem) status: %d", Status);

          if (Status == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
            Status = MFX_ERR_NONE;
          }

          Status = QSVEncode->Init(&QSVEncodeParams);
          info("\tMFXVideoENCODE_Init (sysmem) status: %d", Status);
        }

        if (Status < MFX_ERR_NONE) {
          auto CO3Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
          if (CO3Params && CO3Params->ScenarioInfo != 0) {
            warn("MFXVideoENCODE_Init (sysmem) failed with ScenarioInfo=%d, retrying",
                 CO3Params->ScenarioInfo);
            QSVEncode->Close();
            CO3Params->ScenarioInfo = 0;

            Status = QSVEncode->Init(&QSVEncodeParams);
            info("\tMFXVideoENCODE_Init (sysmem) retry (ScenarioInfo) status: %d",
                 Status);
          }
        }

        if (Status < MFX_ERR_NONE) {
          error("MFXVideoENCODE_Init (sysmem) failed after all retries (Status=%d)", Status);
          throw std::runtime_error(
              "Init(): MFXVideoENCODE_Init error after parameter retries");
        }
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
    Status = SetEncoderParams(InputParams, Codec);

    if (Status >= MFX_ERR_NONE) {
      Status = QSVEncode->Query(&QSVEncodeParams, &QSVEncodeParams);
      info("\tMFXVideoENCODE_Query status: %d", Status);
      if (Status == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        Status = MFX_ERR_NONE;
      }
    }

    Status = QSVEncode->Init(&QSVEncodeParams);
    if (Status != MFX_ERR_NONE) {
      auto CO3Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
      if (CO3Params && CO3Params->ScenarioInfo != 0) {
        warn("MFXVideoENCODE_Init failed with ScenarioInfo=%d, "
             "retrying without ScenarioInfo",
             CO3Params->ScenarioInfo);
        QSVEncode->Close();
        CO3Params->ScenarioInfo = 0;

        Status = QSVEncode->Init(&QSVEncodeParams);
        info("\tMFXVideoENCODE_Init retry (ScenarioInfo) status: %d", Status);
      }
    }

    if (Status < MFX_ERR_NONE) {
      auto TemporalLayers =
          QSVEncodeParams.GetExtBuffer<mfxExtTemporalLayers>();
      if (TemporalLayers && TemporalLayers->NumLayers > 0) {
        warn("MFXVideoENCODE_Init failed with temporal layers (err=%d, "
             "NumLayers=%d, B-frames=%d, NumRefFrame=%d), "
             "retrying without temporal layers",
             Status, TemporalLayers->NumLayers,
             QSVEncodeParams.mfx.GopRefDist - 1,
             QSVEncodeParams.mfx.NumRefFrame);
        QSVEncode->Close();
        delete[] QSVLayerArray;
        QSVLayerArray = nullptr;
        QSVEncodeParams.RemoveExtBuffer<mfxExtTemporalLayers>();

        Status = QSVEncode->Init(&QSVEncodeParams);
        info("\tMFXVideoENCODE_Init retry (TemporalLayers removed) status: %d",
             Status);
      }
    }

    if (Status < MFX_ERR_NONE) {
      auto COParams =
          QSVEncodeParams.GetExtBuffer<mfxExtCodingOption>();
      if (COParams) {
        warn("MFXVideoENCODE_Init failed, retrying with CO basic");
        QSVEncode->Close();
        COParams->IntraPredBlockSize = MFX_BLOCKSIZE_UNKNOWN;
        COParams->InterPredBlockSize = MFX_BLOCKSIZE_UNKNOWN;
        COParams->MECostType = 0;
        COParams->MESearchType = 0;
        Status = QSVEncode->Init(&QSVEncodeParams);
        info("\tMFXVideoENCODE_Init retry (CO basic) status: %d", Status);
      }
    }
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
  QSVProcessingParams.vpp.Out.Width =
      static_cast<mfxU16>((((InputParams->Width + 15) >> 4) << 4));
  QSVProcessingParams.vpp.Out.Height =
      static_cast<mfxU16>((((InputParams->Height + 15) >> 4) << 4));
  QSVProcessingParams.vpp.Out.CropW = static_cast<mfxU16>(InputParams->Width);
  QSVProcessingParams.vpp.Out.CropH = static_cast<mfxU16>(InputParams->Height);
  QSVProcessingParams.vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  QSVProcessingParams.vpp.Out.FrameRateExtN =
      static_cast<mfxU32>(InputParams->FpsNum);
  QSVProcessingParams.vpp.Out.FrameRateExtD =
      static_cast<mfxU32>(InputParams->FpsDen);

  if (InputParams->VPPDenoiseMode.has_value()) {
    auto DenoiseParams = QSVProcessingParams.AddExtBuffer<mfxExtVPPDenoise2>();
    DenoiseParams->Header.BufferId = MFX_EXTBUFF_VPP_DENOISE2;
    DenoiseParams->Header.BufferSz = sizeof(mfxExtVPPDenoise2);
    switch (InputParams->VPPDenoiseMode.value()) {
    case 1:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_AUTO_BDRATE;
      info("\tDenoise set: AUTO | BDRATE | PRE ENCODE");
      break;
    case 2:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_AUTO_ADJUST;
      info("\tDenoise set: AUTO | ADJUST | POST ENCODE");
      break;
    case 3:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_AUTO_SUBJECTIVE;
      info("\tDenoise set: AUTO | SUBJECTIVE | PRE ENCODE");
      break;
    case 4:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_PRE_MANUAL;
      DenoiseParams->Strength =
          static_cast<mfxU16>(InputParams->DenoiseStrength);
      info("\tDenoise set: MANUAL | STRENGTH %d | PRE ENCODE",
           DenoiseParams->Strength);
      break;
    case 5:
      DenoiseParams->Mode = MFX_DENOISE_MODE_INTEL_HVS_POST_MANUAL;
      DenoiseParams->Strength =
          static_cast<mfxU16>(InputParams->DenoiseStrength);
      info("\tDenoise set: MANUAL | STRENGTH %d | POST ENCODE",
           DenoiseParams->Strength);
      break;
    default:
      DenoiseParams->Mode = MFX_DENOISE_MODE_DEFAULT;
      info("\tDenoise set: DEFAULT");
      break;
    }
  } else {
    info("\tDenoise set: OFF");
  }

  if (InputParams->VPPDetail.has_value()) {
    auto DetailParams = QSVProcessingParams.AddExtBuffer<mfxExtVPPDetail>();
    DetailParams->Header.BufferId = MFX_EXTBUFF_VPP_DETAIL;
    DetailParams->Header.BufferSz = sizeof(mfxExtVPPDetail);
    DetailParams->DetailFactor =
        static_cast<mfxU16>(InputParams->VPPDetail.value());
    info("\tDetail set: %d", InputParams->VPPDetail.value());
  } else {
    info("\tDetail set: OFF");
  }

  if (InputParams->VPPScalingMode.has_value()) {
    auto ScalingParams = QSVProcessingParams.AddExtBuffer<mfxExtVPPScaling>();
    ScalingParams->Header.BufferId = MFX_EXTBUFF_VPP_SCALING;
    ScalingParams->Header.BufferSz = sizeof(mfxExtVPPScaling);
    switch (InputParams->VPPScalingMode.value()) {
    case 1:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_QUALITY;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_ADVANCED;
      info("\tScaling set: QUALITY + ADVANCED");
      break;
    case 2:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_INTEL_GEN_VEBOX;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_ADVANCED;
      info("\tScaling set: VEBOX + ADVANCED");
      break;
    case 3:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_LOWPOWER;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_NEAREST_NEIGHBOR;
      info("\tScaling set: LOWPOWER + NEAREST NEIGHBOR");
      break;
    case 4:
      ScalingParams->ScalingMode = MFX_SCALING_MODE_LOWPOWER;
      ScalingParams->InterpolationMethod = MFX_INTERPOLATION_ADVANCED;
      info("\tScaling set: LOWPOWER + ADVANCED");
      break;
    default:
      info("\tScaling set: AUTO");
      break;
    }
  } else {
    info("\tScaling set: OFF");
  }

  if (InputParams->VPPImageStabMode.has_value()) {
    auto ImageStabParams =
        QSVProcessingParams.AddExtBuffer<mfxExtVPPImageStab>();
    ImageStabParams->Header.BufferId = MFX_EXTBUFF_VPP_IMAGE_STABILIZATION;
    ImageStabParams->Header.BufferSz = sizeof(mfxExtVPPImageStab);
    switch (InputParams->VPPImageStabMode.value()) {
    case 1:
      ImageStabParams->Mode = MFX_IMAGESTAB_MODE_UPSCALE;
      info("\tImageStab set: UPSCALE");
      break;
    case 2:
      ImageStabParams->Mode = MFX_IMAGESTAB_MODE_BOXING;
      info("\tImageStab set: BOXING");
      break;
    default:
      info("\tImageStab set: AUTO");
      break;
    }
  } else {
    info("\tImageStab set: OFF");
  }

  if (InputParams->PercEncPrefilter == true) {
    auto PercEncPrefilterParams =
        QSVProcessingParams.AddExtBuffer<mfxExtVPPPercEncPrefilter>();
    PercEncPrefilterParams->Header.BufferId =
        MFX_EXTBUFF_VPP_PERC_ENC_PREFILTER;
    PercEncPrefilterParams->Header.BufferSz = sizeof(mfxExtVPPPercEncPrefilter);
    info("\tPercEncPreFilter set: ON");
  } else {
    info("\tPercEncPreFilter set: OFF");
  }

  if (InputParams->VPPMCTFMode.has_value() && InputParams->VPPMCTFMode.value() == 1) {
    auto MCTFParams = QSVProcessingParams.AddExtBuffer<mfxExtVppMctf>();
    MCTFParams->Header.BufferId = MFX_EXTBUFF_VPP_MCTF;
    MCTFParams->Header.BufferSz = sizeof(mfxExtVppMctf);
    MCTFParams->FilterStrength = static_cast<mfxU16>(InputParams->VPPMCTFStrength);
    info("\tMCTF set: ON | Strength %d", MCTFParams->FilterStrength);
  } else {
    info("\tMCTF set: OFF");
  }

  QSVProcessingParams.IOPattern =
      MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;

  mfxVideoParam ValidParams = {};
  memcpy(&ValidParams, &QSVProcessingParams, sizeof(mfxVideoParam));
  mfxStatus Status = QSVProcessing->Query(&QSVProcessingParams, &ValidParams);
  if (Status < MFX_ERR_NONE) {
    throw std::runtime_error("SetProcessingParams(): Query params error");
  }

  QSVProcessingAuxData = QSVEncodeCtrlParams.AddExtBuffer<mfxExtVppAuxData>();

  return Status;
}

mfxStatus QSVEncoder::SetEncoderParams(struct encoder_params *InputParams,
                                       enum codec_enum Codec) {
  /*It's only for debug*/
  bool COEnabled = 1;
  bool CO2Enabled = 1;
  bool CO3Enabled = 1;
  bool CODDIEnabled = 1;

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
  QSVEncodeParams.mfx.FrameInfo.Width =
      static_cast<mfxU16>((((InputParams->Width + 15) >> 4) << 4));
  info("\tWidth: %d", QSVEncodeParams.mfx.FrameInfo.Width);

  QSVEncodeParams.mfx.FrameInfo.Height =
      static_cast<mfxU16>((((InputParams->Height + 15) >> 4) << 4));
  info("\tHeight: %d", QSVEncodeParams.mfx.FrameInfo.Height);

  QSVEncodeParams.mfx.FrameInfo.ChromaFormat =
      static_cast<mfxU16>(InputParams->ChromaFormat);

  QSVEncodeParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

  QSVEncodeParams.mfx.FrameInfo.CropX = 0;

  QSVEncodeParams.mfx.FrameInfo.CropY = 0;

  QSVEncodeParams.mfx.FrameInfo.CropW = static_cast<mfxU16>(InputParams->Width);

  QSVEncodeParams.mfx.FrameInfo.CropH =
      static_cast<mfxU16>(InputParams->Height);

  QSVEncodeParams.mfx.FrameInfo.FrameRateExtN =
      static_cast<mfxU32>(InputParams->FpsNum);

  QSVEncodeParams.mfx.FrameInfo.FrameRateExtD =
      static_cast<mfxU32>(InputParams->FpsDen);

  QSVEncodeParams.mfx.FrameInfo.FourCC =
      static_cast<mfxU32>(InputParams->FourCC);

  QSVEncodeParams.mfx.FrameInfo.BitDepthChroma =
      InputParams->VideoFormat10bit ? 10 : 0;

  QSVEncodeParams.mfx.FrameInfo.BitDepthLuma =
      InputParams->VideoFormat10bit ? 10 : 0;

  QSVEncodeParams.mfx.FrameInfo.Shift =
      InputParams->VideoFormat10bit ? 1 : 0;

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
    mfxU16 combinedProfile = QSVEncodeParams.mfx.CodecProfile |
                             (InputParams->CodecProfileTier << 8);
    QSVEncodeParams.mfx.CodecProfile = combinedProfile;
  }

  QSVEncodeParams.mfx.CodecLevel = InputParams->CodecLevel;

  /*BRCParamMultiplier fixed at 100 for driver compatibility
    (UHD 730 rejects values !=100). Raw values clamped to 65535*100
    to prevent mfxU16 overflow after division.
    Effective max: 65535*100 = 6,553,500 kbps*/
  const mfxU16 BRC_BASELINE = 100;
  const mfxU16 brcMultiplier = BRC_BASELINE;
  QSVEncodeParams.mfx.BRCParamMultiplier = BRC_BASELINE;

  const auto brcClamp = [](mfxU32 v) {
    mfxU32 limit = static_cast<mfxU32>(65535) * 100;
    return v > limit ? limit : v;
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
          static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 1);

      info("\tCBR fix: ON");
    } else {
      QSVEncodeParams.mfx.BufferSizeInKB =
          (InputParams->Lookahead == true)
              ? static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 2)
              : static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 1);
    }

    if (InputParams->CustomBufferSize == true && InputParams->BufferSize > 0) {
      QSVEncodeParams.mfx.BufferSizeInKB =
          static_cast<mfxU16>(brcClamp(InputParams->BufferSize) / brcMultiplier);
      info("\tCustomBufferSize set: ON");
    }
    QSVEncodeParams.mfx.InitialDelayInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.BufferSizeInKB / 2);
    info("\tBufferSize set to: %d KB",
         QSVEncodeParams.mfx.BufferSizeInKB * brcMultiplier);
    break;
  case MFX_RATECONTROL_VBR:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.MaxKbps =
        static_cast<mfxU16>(brcClamp(InputParams->MaxBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.BufferSizeInKB =
        (InputParams->Lookahead == true)
            ? static_cast<mfxU16>(
                  (QSVEncodeParams.mfx.TargetKbps / static_cast<float>(8)) /
                  (static_cast<float>(
                       QSVEncodeParams.mfx.FrameInfo.FrameRateExtN) /
                   QSVEncodeParams.mfx.FrameInfo.FrameRateExtD) *
                  (InputParams->LADepth +
                   (static_cast<float>(
                        QSVEncodeParams.mfx.FrameInfo.FrameRateExtN) /
                    QSVEncodeParams.mfx.FrameInfo.FrameRateExtD)))
            : static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 1);
    if (InputParams->CustomBufferSize == true && InputParams->BufferSize > 0) {
      QSVEncodeParams.mfx.BufferSizeInKB =
          static_cast<mfxU16>(brcClamp(InputParams->BufferSize) / brcMultiplier);
      info("\tCustomBufferSize set: ON");
    }
    QSVEncodeParams.mfx.InitialDelayInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.BufferSizeInKB / 2);
    info("\tBufferSize set to: %d KB",
         QSVEncodeParams.mfx.BufferSizeInKB * brcMultiplier);
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
        static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 1);
    if (InputParams->CustomBufferSize == true && InputParams->BufferSize > 0) {
      QSVEncodeParams.mfx.BufferSizeInKB =
          static_cast<mfxU16>(brcClamp(InputParams->BufferSize) / brcMultiplier);
      info("\tCustomBufferSize set: ON");
    }
    QSVEncodeParams.mfx.InitialDelayInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.BufferSizeInKB / 2);
    info("\tBufferSize set to: %d KB",
         QSVEncodeParams.mfx.BufferSizeInKB * brcMultiplier);
    break;
  case MFX_RATECONTROL_VCM:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.MaxKbps =
        static_cast<mfxU16>(brcClamp(InputParams->MaxBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.BufferSizeInKB =
        static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 2);
    if (InputParams->CustomBufferSize == true && InputParams->BufferSize > 0) {
      QSVEncodeParams.mfx.BufferSizeInKB =
          static_cast<mfxU16>(brcClamp(InputParams->BufferSize) / brcMultiplier);
      info("\tCustomBufferSize set: ON");
    }
    QSVEncodeParams.mfx.InitialDelayInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.BufferSizeInKB / 2);
    info("\tBufferSize set to: %d KB",
         QSVEncodeParams.mfx.BufferSizeInKB * brcMultiplier);
    break;
  case MFX_RATECONTROL_QVBR:
    QSVEncodeParams.mfx.TargetKbps =
        static_cast<mfxU16>(brcClamp(InputParams->TargetBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.MaxKbps =
        static_cast<mfxU16>(brcClamp(InputParams->MaxBitRate) / brcMultiplier);
    QSVEncodeParams.mfx.BufferSizeInKB =
        static_cast<mfxU16>((QSVEncodeParams.mfx.TargetKbps / 8) * 1);
    if (InputParams->CustomBufferSize == true && InputParams->BufferSize > 0) {
      QSVEncodeParams.mfx.BufferSizeInKB =
          static_cast<mfxU16>(brcClamp(InputParams->BufferSize) / brcMultiplier);
      info("\tCustomBufferSize set: ON");
    }
    QSVEncodeParams.mfx.InitialDelayInKB =
        static_cast<mfxU16>(QSVEncodeParams.mfx.BufferSizeInKB / 2);
    info("\tBufferSize set to: %d KB",
         QSVEncodeParams.mfx.BufferSizeInKB * brcMultiplier);
    break;
  }

  QSVEncodeParams.AsyncDepth = static_cast<mfxU16>(InputParams->AsyncDepth);

  QSVEncodeParams.mfx.GopPicSize =
      (InputParams->KeyIntSec > 0)
          ? static_cast<mfxU16>(
                InputParams->KeyIntSec *
                static_cast<float>(QSVEncodeParams.mfx.FrameInfo.FrameRateExtN /
                                   QSVEncodeParams.mfx.FrameInfo.FrameRateExtD))
          : 240;

  if ((!InputParams->AdaptiveI && !InputParams->AdaptiveB) ||
      (InputParams->AdaptiveI == false && InputParams->AdaptiveB == false)) {
    QSVEncodeParams.mfx.GopOptFlag = MFX_GOP_STRICT;
  } else {
    QSVEncodeParams.mfx.GopOptFlag = MFX_GOP_CLOSED;
  }

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

  if (COEnabled == 1) {
    auto COParams = QSVEncodeParams.AddExtBuffer<mfxExtCodingOption>();
    COParams->Header.BufferId = MFX_EXTBUFF_CODING_OPTION;
    COParams->Header.BufferSz = sizeof(mfxExtCodingOption);
    /*Don't touch it!*/
    COParams->CAVLC = MFX_CODINGOPTION_OFF;
    COParams->RefPicListReordering = MFX_CODINGOPTION_ON;
    COParams->RefPicMarkRep = MFX_CODINGOPTION_ON;
    COParams->PicTimingSEI = MFX_CODINGOPTION_ON;
    // COParams->AUDelimiter = MFX_CODINGOPTION_OFF;
    COParams->MaxDecFrameBuffering = InputParams->NumRefFrame;
    COParams->ResetRefList = MFX_CODINGOPTION_ON;
    COParams->FieldOutput = (InputParams->Lowpower == false)
                                ? MFX_CODINGOPTION_OFF
                                : MFX_CODINGOPTION_ON;
    COParams->IntraPredBlockSize = MFX_BLOCKSIZE_MIN_4X4;
    COParams->InterPredBlockSize = MFX_BLOCKSIZE_MIN_4X4;
    COParams->MVPrecision = MFX_MVPRECISION_QUARTERPEL;
    COParams->MECostType = static_cast<mfxU16>(8);
    COParams->MESearchType = static_cast<mfxU16>(16);
    COParams->MVSearchWindow.x = (QSVEncodeParams.mfx.CodecId == MFX_CODEC_AVC)
                                     ? static_cast<mfxI16>(16)
                                     : static_cast<mfxI16>(32);
    COParams->MVSearchWindow.y = (QSVEncodeParams.mfx.CodecId == MFX_CODEC_AVC)
                                     ? static_cast<mfxI16>(16)
                                     : static_cast<mfxI16>(32);

    if (InputParams->IntraRefEncoding == true) {
      COParams->RecoveryPointSEI = MFX_CODINGOPTION_ON;
    }

    COParams->RateDistortionOpt = GetCodingOpt(InputParams->RDO);

    COParams->VuiVclHrdParameters = GetCodingOpt(InputParams->HRDConformance);
    COParams->VuiNalHrdParameters = GetCodingOpt(InputParams->HRDConformance);
    COParams->NalHrdConformance = GetCodingOpt(InputParams->HRDConformance);
  }

  if (CO2Enabled == 1) {
    auto CO2Params = QSVEncodeParams.AddExtBuffer<mfxExtCodingOption2>();
    CO2Params->Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
    CO2Params->Header.BufferSz = sizeof(mfxExtCodingOption2);
    CO2Params->BufferingPeriodSEI = MFX_BPSEI_IFRAME;
    CO2Params->RepeatPPS = MFX_CODINGOPTION_OFF;
    CO2Params->FixedFrameRate = MFX_CODINGOPTION_ON;
    CO2Params->DisableDeblockingIdc = MFX_CODINGOPTION_OFF;
    CO2Params->EnableMAD = MFX_CODINGOPTION_ON;
    // if (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR ||
    //     QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR) {
    //   CO2Params->MaxFrameSize = (QSVEncodeParams.mfx.TargetKbps *
    //                        QSVEncodeParams.mfx.BRCParamMultiplier * 1000 /
    //                        (8 * (QSVEncodeParams.mfx.FrameInfo.FrameRateExtN
    //                        /
    //                              QSVEncodeParams.mfx.FrameInfo.FrameRateExtD)))
    //                              *
    //                       10;
    // }

    CO2Params->ExtBRC = GetCodingOpt(InputParams->ExtBRC);

    if (InputParams->IntraRefEncoding == true) {

      CO2Params->IntRefType = MFX_REFRESH_HORIZONTAL;

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

    if ((QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR ||
         QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR) &&
        QSVEncodeParams.mfx.LowPower == MFX_CODINGOPTION_OFF &&
        InputParams->MBBRC == true) {
      InputParams->MBBRC = false;
      info("\tMBBRC set: OFF (CBR/VBR with Lowpower OFF)");
    }

    CO2Params->MBBRC = GetCodingOpt(InputParams->MBBRC);

    if (InputParams->BFrames > 0) {
      CO2Params->BRefType = MFX_B_REF_PYRAMID;
    } else {
      CO2Params->BRefType = MFX_B_REF_UNKNOWN;
    }

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
      if (InputParams->LookAheadDS.has_value() == true) {
        switch (InputParams->LookAheadDS.value()) {
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

    CO2Params->BitrateLimit = GetCodingOpt(InputParams->BitrateLimit);

    if (InputParams->AdaptiveMaxFrameSize.has_value()) {
      CO2Params->MaxFrameSize = InputParams->AdaptiveMaxFrameSize.value()
                                     ? MFX_CODINGOPTION_ON
                                     : MFX_CODINGOPTION_OFF;
      info("\tAdaptiveMaxFrameSize: %s",
           InputParams->AdaptiveMaxFrameSize.value() ? "ON" : "OFF");
    }
  }

  if (CO3Enabled == 1) {
    auto CO3Params = QSVEncodeParams.AddExtBuffer<mfxExtCodingOption3>();
    CO3Params->Header.BufferId = MFX_EXTBUFF_CODING_OPTION3;
    CO3Params->Header.BufferSz = sizeof(mfxExtCodingOption3);
    info("\tmfxExtCodingOption3 sizeof: %zu, BufferSz: %d",
         sizeof(mfxExtCodingOption3), CO3Params->Header.BufferSz);
    CO3Params->TargetBitDepthLuma = InputParams->VideoFormat10bit ? 10 : 0;
    CO3Params->TargetBitDepthChroma = InputParams->VideoFormat10bit ? 10 : 0;
    CO3Params->TargetChromaFormatPlus1 =
        static_cast<mfxU16>(QSVEncodeParams.mfx.FrameInfo.ChromaFormat + 1);
    CO3Params->TransformSkip = GetCodingOpt(InputParams->TransformSkip);
    CO3Params->EnableMBForceIntra = MFX_CODINGOPTION_ON;
    CO3Params->FadeDetection = GetCodingOpt(InputParams->FadeDetection);

    if (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_QVBR &&
        InputParams->QVBRQuality > 0 && InputParams->QVBRQuality <= 51) {
      CO3Params->QVBRQuality = InputParams->QVBRQuality;
    }

    // if (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR ||
    //     QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR) {
    //   CO3Params->MaxFrameSizeI = (QSVEncodeParams.mfx.TargetKbps *
    //                         QSVEncodeParams.mfx.BRCParamMultiplier * 1000 /
    //                         (8 * (QSVEncodeParams.mfx.FrameInfo.FrameRateExtN
    //                         /
    //                               QSVEncodeParams.mfx.FrameInfo.FrameRateExtD)))
    //                               *
    //                        8;
    //   CO3Params->MaxFrameSizeP = (QSVEncodeParams.mfx.TargetKbps *
    //                         QSVEncodeParams.mfx.BRCParamMultiplier * 1000 /
    //                         (8 * (QSVEncodeParams.mfx.FrameInfo.FrameRateExtN
    //                         /
    //                               QSVEncodeParams.mfx.FrameInfo.FrameRateExtD)))
    //                               *
    //                        5;
    // }

    CO3Params->EnableQPOffset = MFX_CODINGOPTION_ON;

    CO3Params->BitstreamRestriction = MFX_CODINGOPTION_ON;
    CO3Params->AspectRatioInfoPresent = MFX_CODINGOPTION_ON;
    CO3Params->TimingInfoPresent = MFX_CODINGOPTION_ON;
    CO3Params->OverscanInfoPresent = MFX_CODINGOPTION_ON;

    CO3Params->LowDelayHrd = GetCodingOpt(InputParams->LowDelayHRD);

    if (InputParams->WeightedPred.has_value()) {
      CO3Params->WeightedPred = InputParams->WeightedPred.value()
                                     ? MFX_CODINGOPTION_ON
                                     : MFX_CODINGOPTION_OFF;
      info("\tWeightedPred: %s",
           InputParams->WeightedPred.value() ? "ON" : "OFF");
    } else {
      CO3Params->WeightedPred = MFX_WEIGHTED_PRED_DEFAULT;
    }

    if (InputParams->WeightedBiPred.has_value()) {
      CO3Params->WeightedBiPred = InputParams->WeightedBiPred.value()
                                       ? MFX_CODINGOPTION_ON
                                       : MFX_CODINGOPTION_OFF;
      info("\tWeightedBiPred: %s",
           InputParams->WeightedBiPred.value() ? "ON" : "OFF");
    } else {
      CO3Params->WeightedBiPred = MFX_WEIGHTED_PRED_DEFAULT;
    }

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
      switch (InputParams->ScenarioInfo.value()) {
      case 0:
        CO3Params->ScenarioInfo = 0;
        break;
      case 1:
        CO3Params->ScenarioInfo = 1;
        break;
      case 2:
        CO3Params->ScenarioInfo = 2;
        break;
      case 3:
        CO3Params->ScenarioInfo = 3;
        break;
      case 4:
        CO3Params->ScenarioInfo = 4;
        break;
      }
    }

    if (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_CQP) {
      CO3Params->EnableMBQP = MFX_CODINGOPTION_ON;
    }

    if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
      CO3Params->GPB = GetCodingOpt(InputParams->GPB);
    }

    if (InputParams->PPyramid == true) {
      CO3Params->PRefType = MFX_P_REF_PYRAMID;
    } else {
      CO3Params->PRefType = MFX_P_REF_SIMPLE;
    }

    CO3Params->AdaptiveCQM = GetCodingOpt(InputParams->AdaptiveCQM);

    CO3Params->AdaptiveRef = GetCodingOpt(InputParams->AdaptiveRef);

    CO3Params->AdaptiveLTR = GetCodingOpt(InputParams->AdaptiveLTR);
    if (InputParams->ExtBRC == true &&
        QSVEncodeParams.mfx.CodecId == MFX_CODEC_AVC) {
      CO3Params->ExtBrcAdaptiveLTR = GetCodingOpt(InputParams->AdaptiveLTR);
    }

    if (InputParams->WinBRC &&
        (QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_VCM ||
        QSVEncodeParams.mfx.RateControlMethod == MFX_RATECONTROL_QVBR)) {

      if (InputParams->WinBRCMaxAvgKbps > 0) {
        CO3Params->WinBRCMaxAvgKbps = InputParams->WinBRCMaxAvgKbps;
      } else {
        mfxF64 winBRCMultiplier = 1.3;
        if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_AV1)
          winBRCMultiplier = 1.2;

        mfxF64 winBRCMaxKbps =
            winBRCMultiplier * InputParams->TargetBitRate;
        if (winBRCMaxKbps > static_cast<mfxF64>(std::numeric_limits<mfxU16>::max()))
          winBRCMaxKbps = static_cast<mfxF64>(std::numeric_limits<mfxU16>::max());
        CO3Params->WinBRCMaxAvgKbps = static_cast<mfxU16>(winBRCMaxKbps);
      }

      if (InputParams->WinBRCSize > 0) {
        CO3Params->WinBRCSize = InputParams->WinBRCSize;
      } else {
        CO3Params->WinBRCSize =
            static_cast<mfxU16>(QSVEncodeParams.mfx.FrameInfo.FrameRateExtN /
                                QSVEncodeParams.mfx.FrameInfo.FrameRateExtD);
      }
    }

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
  }

  /*Don't touch it! Magic beyond the control of mere mortals takes place
   * here*/
  if (CODDIEnabled == 1 && QSVEncodeParams.mfx.CodecId != MFX_CODEC_AV1) {
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
    // if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_AVC) {
    //   if (InputParams->NumRefActiveP.has_value() &&
    //       InputParams->NumRefActiveP > 0) {
    //     if (InputParams->NumRefActiveP.value() > InputParams->NumRefFrame) {
    //       InputParams->NumRefActiveP = InputParams->NumRefFrame;
    //       warn("\tThe NumActiveRefP value cannot exceed the NumRefFrame
    //       value");
    //     }

    //    CODDIParams->NumActiveRefP = InputParams->NumRefActiveP.value();
    //  }

    //  if (InputParams->NumRefActiveBL0.has_value() &&
    //      InputParams->NumRefActiveBL0 > 0) {
    //    if (InputParams->NumRefActiveBL0.value() > InputParams->NumRefFrame) {
    //      InputParams->NumRefActiveBL0 = InputParams->NumRefFrame;
    //      warn("\tThe NumActiveRefP value cannot exceed the NumRefFrame
    //      value");
    //    }

    //    CODDIParams->NumActiveRefBL0 = InputParams->NumRefActiveBL0.value();
    //  }

    //  if (InputParams->NumRefActiveBL1.has_value() &&
    //      InputParams->NumRefActiveBL1 > 0) {
    //    if (InputParams->NumRefActiveBL1.value() > InputParams->NumRefFrame) {
    //      InputParams->NumRefActiveBL1 = InputParams->NumRefFrame;
    //      warn("\tThe NumActiveRefP value cannot exceed the NumRefFrame
    //      value");
    //    }

    //    CODDIParams->NumActiveRefBL1 = InputParams->NumRefActiveBL1.value();
    //  }
    //}
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

  if (!QSVUseSystemMemoryPath && QSVEncode) {
    mfxVideoParam ValidParams = {};
    memcpy(&ValidParams, &QSVEncodeParams, sizeof(mfxVideoParam));
    mfxStatus Sts = QSVEncode->Query(&QSVEncodeParams, &ValidParams);
    if (Sts == MFX_ERR_UNSUPPORTED || Sts == MFX_ERR_UNDEFINED_BEHAVIOR) {
      auto CO3Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
      if (CO3Params && CO3Params->AdaptiveLTR == MFX_CODINGOPTION_ON) {
        CO3Params->AdaptiveLTR = MFX_CODINGOPTION_OFF;
      }
    } else if (Sts < MFX_ERR_NONE) {
      throw std::runtime_error(
          "SetEncoderParams(): Query params error");
    }
    return Sts;
  }

  return MFX_ERR_NONE;
#else
  QSVEncodeParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

  {
    mfxVideoParam ValidParams = {};
    memcpy(&ValidParams, &QSVEncodeParams, sizeof(mfxVideoParam));
    mfxStatus Sts = QSVEncode->Query(&QSVEncodeParams, &ValidParams);
    if (Sts == MFX_ERR_UNSUPPORTED || Sts == MFX_ERR_UNDEFINED_BEHAVIOR) {
      auto CO3Params = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
      if (CO3Params && CO3Params->AdaptiveLTR == MFX_CODINGOPTION_ON) {
        CO3Params->AdaptiveLTR = MFX_CODINGOPTION_OFF;
      }
    } else if (Sts < MFX_ERR_NONE) {
      throw std::runtime_error(
          "SetEncoderParams(): Query params error");
    }
    return Sts;
  }
#endif
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
      mfxU32 limit = static_cast<mfxU32>(65535) * brcM;
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
    mfxU32 limit = static_cast<mfxU32>(65535) * brcM;
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
  }
  if (QSVResetParamsChanged == true) {
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
  if (QSVResetParamsChanged == true) {
    return QSVEncode->Reset(&QSVResetParams);
  } else {
    return MFX_ERR_NONE;
  }
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
  try {
    QSVBitstream.MaxLength =
        static_cast<mfxU32>(QSVEncodeParams.mfx.BufferSizeInKB * 1000 *
                            QSVEncodeParams.mfx.BRCParamMultiplier);

    QSVBitstream.DataOffset = 0;
    QSVBitstream.DataLength = 0;
#if defined(_WIN32) || defined(_WIN64)
    if (nullptr == (QSVBitstream.Data = static_cast<mfxU8 *>(
                        _aligned_malloc(QSVBitstream.MaxLength, 32)))) {
      throw std::runtime_error(
          "InitBitstreamBuffer(): Bitstream memory allocation error");
    }
#elif defined(__linux__)
    if (nullptr == (QSVBitstream.Data = static_cast<mfxU8 *>(
                        aligned_alloc(32, QSVBitstream.MaxLength)))) {
      throw std::runtime_error(
          "InitBitstreamBuffer(): Bitstream memory allocation error");
    }
#endif

    info("\tBitstream size: %d Kb", QSVBitstream.MaxLength / 1000);
  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &) {
    throw;
  }
  return MFX_ERR_NONE;
}

mfxStatus QSVEncoder::InitTaskPool([[maybe_unused]] enum codec_enum Codec) {
  try {
    QSVSyncTaskID = 0;
    Task NewTask = {};
    QSVTaskPool.reserve(QSVEncodeParams.AsyncDepth);

    for (int i = 0; i < QSVEncodeParams.AsyncDepth; i++) {
      NewTask.Bitstream.MaxLength =
          static_cast<mfxU32>(QSVEncodeParams.mfx.BufferSizeInKB * 1000 *
                              QSVEncodeParams.mfx.BRCParamMultiplier);

      NewTask.Bitstream.DataOffset = 0;
      NewTask.Bitstream.DataLength = 0;
#if defined(_WIN32) || defined(_WIN64)
      if (nullptr == (NewTask.Bitstream.Data = static_cast<mfxU8 *>(
                          _aligned_malloc(NewTask.Bitstream.MaxLength, 32)))) {
        throw std::runtime_error(
            "InitTaskPool(): Task memory allocation error");
      }
#elif defined(__linux__)
      if (nullptr == (NewTask.Bitstream.Data = static_cast<mfxU8 *>(
                          aligned_alloc(32, NewTask.Bitstream.MaxLength)))) {
        throw std::runtime_error(
            "InitTaskPool(): Task memory allocation error");
      }
#endif
      info("\tTask #%d bitstream size: %d Kb", i,
           NewTask.Bitstream.MaxLength / 1000);

      QSVTaskPool.push_back(NewTask);

#ifdef QSV_UHD600_SUPPORT
      if (!QSVIsTextureEncoder && i < static_cast<int>(QSVSystemMemPool.size())) {
        QSVTaskPool[i].Surface = &QSVSystemMemPool[i].Surface;
      }
#endif
    }

    info("\tTaskPool count: %d", QSVTaskPool.size());

  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &) {
    throw;
  }

  return MFX_ERR_NONE;
}

void QSVEncoder::ReleaseBitstream() {
  if (QSVBitstream.Data) {
    try {
#if defined(_WIN32) || defined(_WIN64)
      _aligned_free(QSVBitstream.Data);
#elif defined(__linux__)
      free(QSVBitstream.Data);
#endif
    } catch (const std::bad_alloc &) {
      throw;
    } catch (const std::exception &) {
      throw;
    }
  }
  QSVBitstream.Data = nullptr;
}

void QSVEncoder::ReleaseTask(int TaskID) {
  if (QSVTaskPool[TaskID].Bitstream.Data) {
    try {
#if defined(_WIN32) || defined(_WIN64)
      _aligned_free(QSVTaskPool[TaskID].Bitstream.Data);
#elif defined(__linux__)
      free(QSVTaskPool[TaskID].Bitstream.Data);
#endif
    } catch (const std::bad_alloc &) {
      throw;
    } catch (const std::exception &) {
      throw;
    }
  }
  QSVTaskPool[TaskID].Bitstream.Data = nullptr;
}

void QSVEncoder::ReleaseTaskPool() {
  if (!QSVTaskPool.empty()) {
    try {
      for (int i = 0; i < QSVTaskPool.size(); i++) {
        ReleaseTask(i);
      }

      QSVTaskPool.clear();
      QSVTaskPool.shrink_to_fit();

    } catch (const std::bad_alloc &) {
      throw;
    } catch (const std::exception &) {
      throw;
    }
  }
}

mfxStatus QSVEncoder::ChangeBitstreamSize(mfxU32 NewSize) {
  try {
#if defined(_WIN32) || defined(_WIN64)
    mfxU8 *Data = static_cast<mfxU8 *>(_aligned_malloc(NewSize, 32));
#elif defined(__linux__)
    mfxU8 *Data = static_cast<mfxU8 *>(aligned_alloc(32, NewSize));
#endif
    if (Data == nullptr) {
      throw std::runtime_error(
          "ChangeBitstreamSize(): Bitstream memory allocation error");
    }

    mfxU32 DataLen = std::move(QSVBitstream.DataLength);
    if (QSVBitstream.DataLength) {
      memcpy(Data, QSVBitstream.Data + QSVBitstream.DataOffset,
             std::min(DataLen, NewSize));
    }
    ReleaseBitstream();

    QSVBitstream.Data = std::move(Data);
    QSVBitstream.DataOffset = 0;
    QSVBitstream.DataLength = std::move(static_cast<mfxU32>(DataLen));
    QSVBitstream.MaxLength = std::move(static_cast<mfxU32>(NewSize));

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

    for (int i = 0; i < QSVTaskPool.size(); i++) {
#if defined(_WIN32) || defined(_WIN64)
      mfxU8 *TaskData = static_cast<mfxU8 *>(_aligned_malloc(NewSize, 32));
#elif defined(__linux__)
      mfxU8 *TaskData = static_cast<mfxU8 *>(aligned_alloc(32, NewSize));
#endif
      if (TaskData == nullptr) {
        throw std::runtime_error(
            "ChangeBitstreamSize(): Task memory allocation error");
      }

      mfxU32 TaskDataLen = std::move(QSVTaskPool[i].Bitstream.DataLength);
      if (QSVTaskPool[i].Bitstream.DataLength) {
        memcpy(TaskData,
               QSVTaskPool[i].Bitstream.Data +
                   QSVTaskPool[i].Bitstream.DataOffset,
               std::min(TaskDataLen, NewSize));
      }
      ReleaseTask(i);

      QSVTaskPool[i].Bitstream.Data = std::move(TaskData);
      QSVTaskPool[i].Bitstream.DataOffset = 0;
      QSVTaskPool[i].Bitstream.DataLength =
          std::move(static_cast<mfxU32>(TaskDataLen));
      QSVTaskPool[i].Bitstream.MaxLength =
          std::move(static_cast<mfxU32>(NewSize));
    }

  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &) {
    throw;
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

static mfxU16 hevc_parse_ctb_read_bits(const mfxU8 *data, mfxU16 max_size,
                                        size_t &byte_pos, int &bit_pos,
                                        int n) {
  mfxU16 val = 0;
  for (int i = 0; i < n && byte_pos < max_size; i++) {
    val = static_cast<mfxU16>((val << 1) |
                              ((data[byte_pos] >> bit_pos) & 1));
    bit_pos--;
    if (bit_pos < 0) {
      byte_pos++;
      bit_pos = 7;
    }
  }
  return val;
}

static mfxU16 hevc_parse_ctb_read_uev(const mfxU8 *data, mfxU16 max_size,
                                       size_t &byte_pos, int &bit_pos) {
  int leading_zeros = 0;
  while (byte_pos < max_size) {
    if (hevc_parse_ctb_read_bits(data, max_size, byte_pos, bit_pos, 1) != 0)
      break;
    leading_zeros++;
  }
  if (leading_zeros == 0)
    return 0;
  return static_cast<mfxU16>(
      (1u << leading_zeros) - 1 +
      hevc_parse_ctb_read_bits(data, max_size, byte_pos, bit_pos,
                                leading_zeros));
}

static mfxU16 parse_hevc_sps_ctb_size(const mfxU8 *sps_data,
                                       mfxU16 sps_size) {
  if (!sps_data || sps_size < 2)
    return 0;

  size_t byte_pos = 2;
  int bit_pos = 7;

  mfxU16 sps_vps_id =
      hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 4);
  (void)sps_vps_id;

  mfxU16 max_sub_layers =
      hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 3);

  hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1);

  hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 95);

  if (max_sub_layers > 0) {
    bool sub_layer_profile_present[7] = {false};
    bool sub_layer_level_present[7] = {false};
    for (int j = max_sub_layers - 1; j >= 0; j--) {
      sub_layer_profile_present[j] =
          hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1) != 0;
      sub_layer_level_present[j] =
          hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1) != 0;
    }
    for (int j = max_sub_layers - 1; j >= 0; j--) {
      if (sub_layer_profile_present[j])
        hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 95);
      if (sub_layer_level_present[j])
        hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 8);
    }
  }

  hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  mfxU16 chroma_format_idc =
      hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  if (chroma_format_idc == 3)
    hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1);

  hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  if (hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1)) {
    hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  }

  hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  bool sub_layer_ordering =
      hevc_parse_ctb_read_bits(sps_data, sps_size, byte_pos, bit_pos, 1) != 0;

  mfxU16 num_ordering = sub_layer_ordering ? max_sub_layers + 1 : 1;
  for (mfxU16 i = 0; i < num_ordering; i++) {
    hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
    hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);
  }

  mfxU16 log2_min_cb =
      hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  mfxU16 log2_diff =
      hevc_parse_ctb_read_uev(sps_data, sps_size, byte_pos, bit_pos);

  mfxU16 min_cb_log2 = log2_min_cb + 3;
  mfxU16 ctb_log2 = min_cb_log2 + log2_diff;

  if (ctb_log2 >= 4 && ctb_log2 <= 7)
    return 1 << ctb_log2;

  return 0;
}

void QSVEncoder::LogActualParams() {
  info("\tActual encoder driver params:");

  auto GetCodingOptStatus = [](const mfxU16 &Value) -> std::string {
    if (Value == MFX_CODINGOPTION_ON)
      return "ON";
    if (Value == MFX_CODINGOPTION_OFF)
      return "OFF";
    return "AUTO";
  };

  auto GetTrellisStatus = [](const mfxU16 &Value) -> std::string {
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

  auto GetSAOStatus = [](const mfxU16 &Value) -> std::string {
    if (Value ==
        (MFX_SAO_ENABLE_LUMA | MFX_SAO_ENABLE_CHROMA))
      return "ALL";
    if (Value == MFX_SAO_ENABLE_LUMA) return "LUMA";
    if (Value == MFX_SAO_ENABLE_CHROMA) return "CHROMA";
    if (Value == MFX_SAO_DISABLE) return "DISABLE";
    return "AUTO";
  };

  auto GetInterpFilterName = [](const mfxU8 &Value) -> std::string {
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

  auto GetWeightedPredStatus = [](const mfxU16 &Value) -> std::string {
    if (Value == MFX_CODINGOPTION_ON)
      return "ON";
    if (Value == MFX_CODINGOPTION_OFF)
      return "OFF";
    return "DEFAULT";
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
         tier == MFX_TIER_HEVC_HIGH ? "high" : "main");
  } else {
    info("\tCodecProfile: %d",
         QSVEncodeParams.mfx.CodecProfile);
  }

  if (QSVEncodeParams.mfx.CodecLevel) {
    info("\tCodecLevel: %d",
         QSVEncodeParams.mfx.CodecLevel);
  }

  if (QSVEncodeParams.mfx.BRCParamMultiplier) {
    info("\tBRCParamMultiplier set: %d",
         QSVEncodeParams.mfx.BRCParamMultiplier);
  }

  if (QSVEncodeParams.mfx.GopOptFlag & MFX_GOP_STRICT) {
    info("\tGopOptFlag set: STRICT");
  } else if (QSVEncodeParams.mfx.GopOptFlag & MFX_GOP_CLOSED) {
    info("\tGopOptFlag set: CLOSED");
  }

  auto *CO2 = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption2>();
  if (CO2) {
    info("\tExtBRC set: %s",
         GetCodingOptStatus(CO2->ExtBRC).c_str());
    info("\tLookaheadDepth set to: %d",
         CO2->LookAheadDepth);
    info("\tMBBRC set: %s",
         GetCodingOptStatus(CO2->MBBRC).c_str());
    info("\tBPyramid set: %s",
         GetCodingOptStatus(CO2->BRefType).c_str());
    info("\tTrellis set: %s",
         GetTrellisStatus(CO2->Trellis).c_str());
    info("\tAdaptiveI set: %s",
         GetCodingOptStatus(CO2->AdaptiveI).c_str());
    info("\tAdaptiveB set: %s",
         GetCodingOptStatus(CO2->AdaptiveB).c_str());
    info("\tUseRawRef set: %s",
         GetCodingOptStatus(CO2->UseRawRef).c_str());
    info("\tBitrateLimit set: %s",
         GetCodingOptStatus(CO2->BitrateLimit).c_str());
    info("\tAdaptiveMaxFrameSize set: %s",
         GetCodingOptStatus(CO2->MaxFrameSize).c_str());
  }

  if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_HEVC) {
    auto *SPSPPS = QSVEncodeParams.GetExtBuffer<mfxExtCodingOptionSPSPPS>();
    if (SPSPPS && SPSPPS->SPSBuffer && SPSPPS->SPSBufSize > 0) {
      mfxU16 actual_ctb =
          parse_hevc_sps_ctb_size(SPSPPS->SPSBuffer, SPSPPS->SPSBufSize);
      if (actual_ctb > 0)
        info("\tCTU Size (actual): %d", actual_ctb);
      else
        info("\tCTU Size: could not parse from SPS");
    } else {
      info("\tCTU Size: not available from SPS");
    }
  } else if (QSVEncodeParams.mfx.CodecId == MFX_CODEC_AV1) {
    info("\tCTU Size: N/A (AV1 uses superblocks)");
  } else {
    info("\tCTU Size: N/A (AVC uses 16x16 macroblocks)");
  }

  auto *CO3 = QSVEncodeParams.GetExtBuffer<mfxExtCodingOption3>();
  if (CO3) {
    info("\tFadeDetection set: %s",
         GetCodingOptStatus(CO3->FadeDetection).c_str());
    info("\tLowDelayHRD set: %s",
         GetCodingOptStatus(CO3->LowDelayHrd).c_str());
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
    info("\tGPB set: %s",
         GetCodingOptStatus(CO3->GPB).c_str());
    info("\tPPyramid set: %s",
         CO3->PRefType == MFX_P_REF_PYRAMID ? "PYRAMID" : "SIMPLE");
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
  }

  auto *TuneQuality = QSVEncodeParams.GetExtBuffer<mfxExtTuneEncodeQuality>();
  if (TuneQuality) {
    auto GetTuneQualityName = [](const mfxU16 &Value) -> std::string {
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
    auto *HEVC = QSVEncodeParams.GetExtBuffer<mfxExtHEVCParam>();
    if (HEVC) {
      info("\tSAO set: %s",
           GetSAOStatus(HEVC->SampleAdaptiveOffset).c_str());
    }
  }

  auto *MCTF = QSVEncodeParams.GetExtBuffer<mfxExtVppMctf>();
  if (MCTF) {
    info("\tMCTF set: ON | Strength %d",
         MCTF->FilterStrength);
  } else {
    info("\tMCTF set: OFF");
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
      memcpy(SurfaceData->Y, FrameData[0],
             static_cast<size_t>(Height) * Pitch);
      memcpy(SurfaceData->UV, FrameData[1],
             static_cast<size_t>(Height / 2) * Pitch);
    } else {
      PTR = static_cast<mfxU8 *>(SurfaceData->Y + SurfaceInfo->CropX +
                                 SurfaceInfo->CropY * Pitch);

      for (i = 0; i < Height; i++) {
        memcpy(PTR + i * Pitch, FrameData[0] + i * FrameLinesize[0], Width);
      }

      Height /= 2;
      PTR = static_cast<mfxU8 *>((SurfaceData->UV + SurfaceInfo->CropX +
                                  (SurfaceInfo->CropY / 2) * Pitch));

      for (i = 0; i < Height; i++) {
        memcpy(PTR + i * Pitch, FrameData[1] + i * FrameLinesize[1], Width);
      }
    }
  } else if (Surface->Info.FourCC == MFX_FOURCC_P010) {
    const size_t line_size = static_cast<size_t>(Width) * 2;
    if (Pitch == static_cast<mfxU16>(FrameLinesize[0])) {
      memcpy(SurfaceData->Y, FrameData[0],
             static_cast<size_t>(Height) * Pitch);
      memcpy(SurfaceData->UV, FrameData[1],
             static_cast<size_t>(Height / 2) * Pitch);
    } else {
      PTR = static_cast<mfxU8 *>(SurfaceData->Y + SurfaceInfo->CropX +
                                 SurfaceInfo->CropY * Pitch);

      for (i = 0; i < Height; i++) {
        memcpy(PTR + i * Pitch, FrameData[0] + i * FrameLinesize[0], line_size);
      }

      Height /= 2;
      PTR = static_cast<mfxU8 *>((SurfaceData->UV + SurfaceInfo->CropX +
                                  (SurfaceInfo->CropY / 2) * Pitch));

      for (i = 0; i < Height; i++) {
        memcpy(PTR + i * Pitch, FrameData[1] + i * FrameLinesize[1], line_size);
      }
    }
  } else if (Surface->Info.FourCC == MFX_FOURCC_RGB4) {
    const size_t line_size = static_cast<size_t>(Width) * 4;
    if (Pitch == static_cast<mfxU16>(FrameLinesize[0])) {
      memcpy(SurfaceData->B, FrameData[0],
             static_cast<size_t>(Height) * Pitch);
    } else {
      for (i = 0; i < Height; i++) {
        memcpy(SurfaceData->B + i * Pitch, FrameData[0] + i * FrameLinesize[0],
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
    do {
      if (QSVTaskPool[QSVSyncTaskID].SyncPoint == nullptr) {
        break;
      }
      SyncStatus = MFXVideoCORE_SyncOperation(
          QSVSession, QSVTaskPool[QSVSyncTaskID].SyncPoint, 100);
      if (SyncStatus < MFX_ERR_NONE) {
        error("Encode.EncodeSync error: %d", SyncStatus);
        if (QSVEncodeSurface) {
          QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
          QSVEncodeSurface = nullptr;
        }
        throw std::runtime_error(
            "Encode(): Sync operation failed - unrecoverable error");
      }
    } while (SyncStatus == MFX_WRN_IN_EXECUTION);

    mfxU8 *DataTemp = std::move(QSVBitstream.Data);
    QSVBitstream = std::move(QSVTaskPool[QSVSyncTaskID].Bitstream);

    QSVTaskPool[QSVSyncTaskID].Bitstream.Data = std::move(DataTemp);
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataLength = 0;
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataOffset = 0;
    QSVTaskPool[QSVSyncTaskID].SyncPoint = nullptr;
    TaskID = std::move(QSVSyncTaskID);
    *Bitstream = std::move(&QSVBitstream);
  }

  mfxFrameSurface1 *EncodeSurface = QSVTaskPool[TaskID].Surface;
  if (!EncodeSurface) {
    error("System memory surface is null for task %d", TaskID);
    throw std::runtime_error("Encode(): System memory surface is null");
  }

  EncodeSurface->Data.TimeStamp = TS;
  LoadFrameData(EncodeSurface, std::move(FrameData),
                std::move(FrameLinesize));
  QSVTaskPool[TaskID].Bitstream.TimeStamp = std::move(TS);

  int EncodeRetryCount = 0;
  const int MaxEncodeRetries = 200;

  for (;;) {
    if (++EncodeRetryCount > MaxEncodeRetries) {
      error("Encode retry count exceeded");
      throw std::runtime_error(
          "Encode(): retry count exceeded");
    }

    Status = QSVEncode->EncodeFrameAsync(
        nullptr, EncodeSurface, &QSVTaskPool[TaskID].Bitstream,
        &QSVTaskPool[TaskID].SyncPoint);

    if (MFX_ERR_NONE == Status) {
      break;
    } else if (MFX_ERR_NONE < Status && !QSVTaskPool[TaskID].SyncPoint) {
      if (MFX_WRN_DEVICE_BUSY == Status)
        Sleep(1);
    } else if (MFX_ERR_NONE < Status && QSVTaskPool[TaskID].SyncPoint) {
      Status = MFX_ERR_NONE;
      break;
    } else if (MFX_ERR_NOT_ENOUGH_BUFFER == Status ||
               MFX_ERR_MORE_BITSTREAM == Status) {
      ChangeBitstreamSize(
          static_cast<mfxU32>(QSVBitstream.MaxLength * 2));
      warn("The bitstream buffer size is too small. The size has been "
           "increased by 2 "
           "times. New value: %d KB",
           (QSVBitstream.MaxLength * 2 / 8 / 1000));
    } else if (MFX_ERR_MORE_DATA == Status) {
      break;
    } else {
      error("Encode fatal error: %d", Status);
      throw std::runtime_error(
          "Encode(): EncodeFrameAsync fatal error");
    }
  }

  return Status;
}
#endif

mfxStatus QSVEncoder::GetFreeTaskIndex(int *TaskID) {
  if (!QSVTaskPool.empty()) {
    for (int i = 0; i < QSVTaskPool.size(); i++) {
      if (static_cast<mfxSyncPoint>(nullptr) == QSVTaskPool[i].SyncPoint) {
        QSVSyncTaskID = (i + 1) % static_cast<int>(QSVTaskPool.size());
        *TaskID = i;
        return MFX_ERR_NONE;
      }
    }
  }
  return MFX_ERR_NOT_FOUND;
}

mfxStatus QSVEncoder::EncodeTexture(mfxU64 TS, void *TextureHandle,
                                    uint64_t LockKey, uint64_t *NextKey,
                                    mfxBitstream **Bitstream) {
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
    do {
      if (QSVTaskPool[QSVSyncTaskID].SyncPoint == nullptr) {
        break;
      }
      SyncStatus = MFXVideoCORE_SyncOperation(
          QSVSession, QSVTaskPool[QSVSyncTaskID].SyncPoint, 100);
      if (SyncStatus < MFX_ERR_NONE) {
        error("Error code: %d", SyncStatus);
        throw std::runtime_error("Encode(): Syncronization error");
      }
    } while (SyncStatus == MFX_WRN_IN_EXECUTION);

    mfxU8 *DataTemp = std::move(QSVBitstream.Data);
    QSVBitstream = std::move(QSVTaskPool[QSVSyncTaskID].Bitstream);

    QSVTaskPool[QSVSyncTaskID].Bitstream.Data = std::move(DataTemp);
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataLength = 0;
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataOffset = 0;
    QSVTaskPool[QSVSyncTaskID].SyncPoint = nullptr;
    TaskID = std::move(QSVSyncTaskID);
    *Bitstream = std::move(&QSVBitstream);
  }

  try {
    HWManager->CopyTexture(Texture, std::move(TextureHandle), LockKey,
                           static_cast<mfxU64 *>(NextKey));
  } catch (const std::exception &e) {
    error("Error code: %d. %s", Status, e.what());
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
      mfxSyncPoint VPPSyncPoint = nullptr;
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

      {
        mfxStatus SyncSts;
        do {
          SyncSts = MFXVideoCORE_SyncOperation(
              QSVSession, QSVProcessingSyncPoint, 100);
        } while (SyncSts == MFX_WRN_IN_EXECUTION);
        if (SyncSts < MFX_ERR_NONE) {
          error("VPP sync error: %d", SyncSts);
          throw std::runtime_error("Encode(): VPP sync failed");
        }
      }

      QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
      QSVEncodeSurface = nullptr;
    }

    int EncodeRetryCount = 0;
    const int MaxEncodeRetries = 200;

    for (;;) {
      if (++EncodeRetryCount > MaxEncodeRetries) {
        error("Encode retry count exceeded");
        throw std::runtime_error(
            "Encode(): retry count exceeded");
      }

      Status = QSVEncode->EncodeFrameAsync(
          (QSVProcessingEnable && QSVEncodeParams.mfx.CodecId != MFX_CODEC_AV1
               ? &QSVEncodeCtrlParams
               : nullptr),
          (QSVProcessingEnable ? QSVProcessingSurface : QSVEncodeSurface),
          &QSVTaskPool[TaskID].Bitstream, &QSVTaskPool[TaskID].SyncPoint);

      if (MFX_ERR_NONE == Status) {
        break;
      } else if (MFX_ERR_NONE < Status && !QSVTaskPool[TaskID].SyncPoint) {
        if (MFX_WRN_DEVICE_BUSY == Status)
          Sleep(1);
      } else if (MFX_ERR_NONE < Status && QSVTaskPool[TaskID].SyncPoint) {
        Status = MFX_ERR_NONE;
        break;
      } else if (MFX_ERR_NOT_ENOUGH_BUFFER == Status ||
                 MFX_ERR_MORE_BITSTREAM == Status) {
        ChangeBitstreamSize(static_cast<mfxU32>(QSVBitstream.MaxLength * 2));
        warn("The bitstream buffer size is too small. The size has been "
             "increased by 2 "
             "times. New value: %d KB",
             (QSVBitstream.MaxLength * 2 / 8 / 1000));
      } else if (MFX_ERR_MORE_DATA == Status) {
        break;
      } else {
        error("Encode fatal error: %d", Status);
        throw std::runtime_error(
            "Encode(): EncodeFrameAsync fatal error");
      }
    }

    if (QSVProcessingEnable) {
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
    do {
      if (QSVTaskPool[QSVSyncTaskID].SyncPoint == nullptr) {
        break;
      }
      SyncStatus = MFXVideoCORE_SyncOperation(
          QSVSession, QSVTaskPool[QSVSyncTaskID].SyncPoint, 100);
      if (SyncStatus < MFX_ERR_NONE) {
        error("Encode sync error: %d", SyncStatus);
        if (QSVEncodeSurface) {
          QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
          QSVEncodeSurface = nullptr;
        }
        throw std::runtime_error(
            "Encode(): Sync operation failed - unrecoverable error");
      }
    } while (SyncStatus == MFX_WRN_IN_EXECUTION);

    mfxU8 *DataTemp = std::move(QSVBitstream.Data);
    QSVBitstream = std::move(QSVTaskPool[QSVSyncTaskID].Bitstream);

    QSVTaskPool[QSVSyncTaskID].Bitstream.Data = std::move(DataTemp);
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataLength = 0;
    QSVTaskPool[QSVSyncTaskID].Bitstream.DataOffset = 0;
    QSVTaskPool[QSVSyncTaskID].SyncPoint = nullptr;
    TaskID = std::move(QSVSyncTaskID);
    *Bitstream = std::move(&QSVBitstream);
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
  LoadFrameData(QSVEncodeSurface, std::move(FrameData),
                std::move(FrameLinesize));

  QSVTaskPool[TaskID].Bitstream.TimeStamp = std::move(TS);

  Status = QSVEncodeSurface->FrameInterface->Unmap(QSVEncodeSurface);
  if (Status < MFX_ERR_NONE) {
    warn("Surface.Unmap.Write error: %d", Status);
    QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
    QSVEncodeSurface = nullptr;
    return Status;
  }

  if (QSVProcessingEnable) {
    do {
      // if (QSVProcessingSurface != nullptr) {
      //   QSVProcessingSurface->FrameInterface->GetRefCounter(
      //       QSVProcessingSurface, &QSVProcessingCurRefCount);
      // }

      // if (/*QSVProcessingSurface == nullptr ||*/
      // QSVProcessingCurRefCount == 0 ||
      // Status == MFX_ERR_MORE_SURFACE) {

      Status = QSVProcessing->GetSurfaceOut(&QSVProcessingSurface);

      if (Status < MFX_ERR_NONE) {
        error("Error code: %d", Status);
        QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
        QSVEncodeSurface = nullptr;
        throw std::runtime_error("Encode(): Get processing surface error");
      }

      // QSVProcessingSurface->FrameInterface->GetRefCounter(
      //     QSVProcessingSurface, &QSVProcessingRefCount);
      // error("ProcessingRefCount: %d", QSVProcessingRefCount);
      // }

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

    do {
      SyncStatus = MFXVideoCORE_SyncOperation(
          QSVSession, QSVProcessingSyncPoint, 100);
    } while (SyncStatus == MFX_WRN_IN_EXECUTION);
    if (SyncStatus < MFX_ERR_NONE) {
      error("VPP sync error: %d", SyncStatus);
      QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
      QSVProcessingSurface = nullptr;
      QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
      QSVEncodeSurface = nullptr;
      throw std::runtime_error("Encode(): VPP sync failed");
    }
  }

  /*Encode a frame asynchronously (returns immediately)*/
  int EncodeRetryCount = 0;
  const int MaxEncodeRetries = 200;

  for (;;) {
    if (++EncodeRetryCount > MaxEncodeRetries) {
      error("Encode retry count exceeded");
      if (QSVProcessingEnable && QSVProcessingSurface) {
        QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
        QSVProcessingSurface = nullptr;
      }
      if (QSVEncodeSurface) {
        QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
        QSVEncodeSurface = nullptr;
      }
      throw std::runtime_error(
          "Encode(): retry count exceeded");
    }

    Status = QSVEncode->EncodeFrameAsync(
        nullptr,
        (QSVProcessingEnable ? QSVProcessingSurface : QSVEncodeSurface),
        &QSVTaskPool[TaskID].Bitstream, &QSVTaskPool[TaskID].SyncPoint);

    if (MFX_ERR_NONE == Status) {
      break;
    } else if (MFX_ERR_NONE < Status && !QSVTaskPool[TaskID].SyncPoint) {
      if (MFX_WRN_DEVICE_BUSY == Status)
        Sleep(1);
    } else if (MFX_ERR_NONE < Status && QSVTaskPool[TaskID].SyncPoint) {
      Status = MFX_ERR_NONE;
      break;
    } else if (MFX_ERR_NOT_ENOUGH_BUFFER == Status ||
               MFX_ERR_MORE_BITSTREAM == Status) {
      ChangeBitstreamSize(static_cast<mfxU32>(QSVBitstream.MaxLength * 2));
      warn("The bitstream buffer size is too small. The size has been "
           "increased by 2 "
           "times. New value: %d KB",
           (QSVBitstream.MaxLength * 2 / 8 / 1000));
    } else if (MFX_ERR_MORE_DATA == Status) {
      break;
    } else {
      error("Encode fatal error: %d", Status);
      if (QSVProcessingEnable && QSVProcessingSurface) {
        QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
        QSVProcessingSurface = nullptr;
      }
      if (QSVEncodeSurface) {
        QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
        QSVEncodeSurface = nullptr;
      }
      throw std::runtime_error(
          "Encode(): EncodeFrameAsync fatal error");
    }
  }

  if (QSVProcessingEnable) {
    QSVProcessingSurface->FrameInterface->Release(QSVProcessingSurface);
  }

  QSVEncodeSurface->FrameInterface->Release(QSVEncodeSurface);
  QSVEncodeSurface = nullptr;

  return MFX_ERR_NONE;
}

mfxStatus QSVEncoder::Drain() {
  mfxStatus Status = MFX_ERR_NONE;

  for (auto &Task : QSVTaskPool) {
    if (Task.SyncPoint != nullptr) {
      mfxStatus SyncSts = MFXVideoCORE_SyncOperation(
          QSVSession, Task.SyncPoint, 5000);
      if (SyncSts < MFX_ERR_NONE) {
        warn("Drain sync warning: %d", SyncSts);
      }
      Task.SyncPoint = nullptr;
    }
  }

  return Status;
}

mfxStatus QSVEncoder::ClearData() {
  mfxStatus Status = MFX_ERR_NONE;
  try {

    if (QSVEncode) {
      Drain();
      Status = QSVEncode->Close();
      QSVEncode = nullptr;
    }

    if (QSVProcessing) {
      // QSVProcessingEnableParams.~ExtBufManager();
      Status = QSVProcessing->Close();
      QSVProcessing = nullptr;
    }

    ReleaseTaskPool();
    ReleaseBitstream();

    if (Status >= MFX_ERR_NONE) {
      HWManager::HWEncoderCounter--;
    }

    if (QSVSession) {
      Status = MFXClose(QSVSession);
      if (Status >= MFX_ERR_NONE) {
        MFXDispReleaseImplDescription(QSVLoader, nullptr);
        MFXUnload(QSVLoader);
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
        // delete HWManager;
        HWManager = nullptr;
        HWManager::HWEncoderCounter = 0;
      }
    }
  } catch (const std::exception &) {
    throw;
  }

  return Status;
}
