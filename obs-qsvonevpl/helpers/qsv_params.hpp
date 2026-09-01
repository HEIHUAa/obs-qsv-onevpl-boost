#pragma once

struct encoder_params {
  mfxU16 TargetUsage; /* 1 through 7, 1 being best quality and 7
                                  being the best speed */
  mfxU16 Width;       /* source picture width */
  mfxU16 Height;      /* source picture height */
  mfxU16 AsyncDepth;
  mfxU32 FpsNum;
  mfxU32 FpsDen;
  uint32_t TargetBitRate;
  uint32_t MaxBitRate;
  uint32_t BufferSize;
  mfxU16 CodecProfile;
  mfxU16 CodecProfileTier;
  mfxU16 CodecLevel;
  mfxU16 RateControl;
  mfxU16 QPI;
  mfxU16 QPP;
  mfxU16 QPB;
  mfxU16 LADepth;
  mfxU16 KeyIntSec;
  mfxU16 BFrames;
  mfxU16 ICQQuality;
  mfxU16 VideoFormat;
  mfxU16 VideoFullRange;
  mfxU16 ColourPrimaries;
  mfxU16 TransferCharacteristics;
  mfxU16 MatrixCoefficients;
  mfxU16 ChromaSampleLocTypeTopField;
  mfxU16 ChromaSampleLocTypeBottomField;
  mfxU16 DisplayPrimariesX[3]{};
  mfxU16 DisplayPrimariesY[3]{};
  mfxU16 WhitePointX{};
  mfxU16 WhitePointY{};
  mfxU32 MaxDisplayMasteringLuminance{};
  mfxU32 MinDisplayMasteringLuminance{};
  mfxU16 MaxContentLightLevel{};
  mfxU16 MaxPicAverageLightLevel{};
  mfxU16 IntraRefCycleSize;
  mfxU16 NumRefFrame;
  mfxU16 DenoiseStrength;
  mfxU16 QVBRQuality;
  mfxU16 Accuracy;     // AVBR: tenth of percent (0=driver default)
  mfxU16 Convergence;  // AVBR: unit of 100 frames (0=driver default)

  mfxI16 IntraRefQPDelta;

  std::optional<bool> QualityEnchance;
  std::optional<bool> MBBRC;
  std::optional<bool> AdaptiveI;
  std::optional<bool> AdaptiveB;
#ifndef QSV_UHD600_SUPPORT
  std::optional<bool> AdaptiveCQM;
#endif
  mfxU32 AdaptiveMaxFrameSize;
  mfxU32 MaxFrameSizeMode; // 0=auto(driver), 1=all, 2=per_type
  mfxU32 MaxFrameSizeI;
  mfxU32 MaxFrameSizeP;
  std::optional<bool> BRCPanicMode;
  std::optional<bool> RDO;
  std::optional<bool> RawRef;
  std::optional<bool> GPB;
  std::optional<bool> DirectBiasAdjustment;
  std::optional<mfxU16> GopOptFlag;
  std::optional<mfxU16> WeightedPred;
  std::optional<bool> GlobalMotionBiasAdjustment;
  std::optional<bool> HRDConformance;
  std::optional<bool> LowDelayHRD;
  std::optional<bool> LowDelayBRC;
  std::optional<bool> TransformSkip;
  std::optional<int> ScenarioInfo;

  std::optional<int> ContentInfo;

  bool Lookahead;
  bool PPyramid;
  bool IntraRefEncoding;
  mfxU16 IntraRefType;
  // Debug group toggles
  bool QPStatistics = true; // default ON to preserve existing behavior
  bool VideoHeaderHexDump = false;
  bool FrameStatistics = false;
  bool EncTools;
  std::optional<bool> EncToolsSceneChange;
  std::optional<bool> EncToolsAdaptiveRefP;
  std::optional<bool> EncToolsAdaptiveRefB;
  std::optional<bool> EncToolsAdaptivePyramidQuantP;
  std::optional<bool> EncToolsAdaptivePyramidQuantB;
  std::optional<bool> EncToolsAdaptiveMBQP;
  std::optional<bool> EncToolsBRCBufferHints;
  std::optional<bool> EncToolsBRC;
  std::optional<bool> EncToolsSaliencyMapHint;
  mfxU16 BitDepth = 0;   /* 0=unspecified(8bit), 8, 10, 12, 16 */
  bool ResetAllowed;
  bool Lowpower;
  bool PercEncPrefilter;
  bool ProcessingEnable;
  bool ColorConversion;

  std::optional<int> Trellis;
  std::optional<int> VPPDenoiseMode;
  std::optional<int> VPPScalingMode;
  std::optional<int> VPPImageStabMode;
  std::optional<int> VPPDetail;
  std::optional<int> MVCostScalingFactor;
  std::optional<int> LookAheadDS;
  std::optional<bool> MotionVectorsOverPicBoundaries;
  std::optional<int> NumRefFrameLayers;
  #ifndef QSV_UHD600_SUPPORT
  std::optional<int> VPPMCTFMode;
  mfxU16 VPPMCTFStrength;
#endif
  std::optional<int> SAO;
  std::optional<int> AV1CDEF;
  std::optional<int> AV1Restoration;
  std::optional<int> AV1LoopFilter;
  std::optional<int> AV1SuperRes;
  std::optional<int> AV1InterpFilter;
  std::optional<int> AV1ErrorResilient;
  std::optional<int> SkipFrame;
  std::optional<bool> RepartitionCheckEnable;
  std::optional<int> AV1Segmentation;
  std::optional<mfxU16> DisableDeblockingIdc; // 0=all enabled, 1=partial(slice), 2=all disabled
  mfxU32 TuneQuality = 0;
  std::optional<mfxU16> VPPOutWidth;
  std::optional<mfxU16> VPPOutHeight;

  // ProcAmp (color adjustment)
  std::optional<int> VPPProcAmpMode;     // 0=OFF, 1=ON
  mfxF64 VPPProcAmpBrightness{};        // [-100.0, 100.0]
  mfxF64 VPPProcAmpContrast{1.0};       // [0.0, 10.0]
  mfxF64 VPPProcAmpHue{};               // [-180.0, 180.0]
  mfxF64 VPPProcAmpSaturation{1.0};     // [0.0, 10.0]

  // Rotation
  std::optional<int> VPPRotation;       // 0=OFF, 90, 180, 270

  // Mirroring
  std::optional<int> VPPMirroring;      // 0=OFF, 1=HORIZONTAL, 2=VERTICAL, 3=BOTH

  // Frame Rate Conversion
  std::optional<int> VPPFRCMode;        // FRC algorithm
  std::optional<mfxU32> VPPOutFpsNum;   // output frame rate numerator (for FRC)
  std::optional<mfxU32> VPPOutFpsDen;   // output frame rate denominator

  mfxU32 FourCC;
  mfxU16 ChromaFormat;

  int GPUNum;
  int dxgiAdapterIndex = 0; // actual DXGI adapter index, not adjusted for non-Intel GPUs

  int ScreenContentTools;

  std::string CustomCodingOptions;

  // Min/Max QP constraints: "-1" = driver default, "12" = all IPB=12,
  // "12,12,12" = per-type (I,P,B). Applied after custom coding options
  // in InitEncoderInternal.
  std::string MinQP;
  std::string MaxQP;

  // ROI (Region of Interest) settings
  struct roi_region {
    mfxU16 Left = 0;
    mfxU16 Top = 0;
    mfxU16 Right = 0;
    mfxU16 Bottom = 0;
    mfxI16 DeltaQP = 0;
    bool HasGradient = false;
    mfxI16 GradLeft   = 0; // pixel values (positive = outward, negative = inward)
    mfxI16 GradTop    = 0;
    mfxI16 GradRight  = 0;
    mfxI16 GradBottom = 0;
    int GradientSteps = 3;  // subdivision count per side (default 3 → 7×7 grid)
  };
  struct normalized_roi_region {
    double Left = 0.0;    // 0.0 ~ 1.0 (fraction of output width)
    double Top = 0.0;     // 0.0 ~ 1.0 (fraction of output height)
    double Right = 0.0;   // 0.0 ~ 1.0
    double Bottom = 0.0;  // 0.0 ~ 1.0
    mfxI16 DeltaQP = 0;
    // Optional gradient / falloff
    bool HasGradient = false;
    double GradLeft   = 0; // expansion outward (+), inward (-)
    double GradTop    = 0;
    double GradRight  = 0;
    double GradBottom = 0;
    int GradientSteps = 3;  // subdivision count per side
  };
  std::vector<roi_region> ROIRegions;
  std::vector<normalized_roi_region> NormalizedROIRegions;
  mfxU16 ROIMode; // 0 = MFX_ROI_MODE_PRIORITY, 1 = MFX_ROI_MODE_QP_DELTA (matches VPL API)
  bool ROIEnabled = false;
};

