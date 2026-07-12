#pragma once
#pragma warning(disable : 4996)

#include "common_utils.hpp"

extern "C" {
#include <obs-module.h>
}

// Forward declaration from obs-qsv-onevpl-plugin-init.hpp
mfxU16 QueryPlatformCodeName();

#include <algorithm>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

// Map string to value via compile-time lookup table.
template <typename T, size_t N>
static inline std::optional<T> MapString(
    std::string_view key, const std::pair<std::string_view, T> (&map)[N]) {
  for (const auto &[str, val] : map) {
    if (key == str)
      return val;
  }
  return std::nullopt;
}

struct LevelEntry {
  const char *name;
  mfxU16 value;
};

static inline mfxU16 ParseCodecLevel(std::string_view LevelStr,
                                     const LevelEntry *Table, size_t Count) {
  auto it = std::ranges::find(Table, Table + Count, LevelStr, &LevelEntry::name);
  return it != Table + Count ? it->value : 0;
}

static constexpr std::pair<std::string_view, mfxU16> kAV1TernaryMap[] = {
    {"AUTO", 2},
    {"ON", 1},
    {"OFF", 0},
};

static inline mfxU16 ParseAV1Ternary(const char *Data) {
  return MapString(Data, kAV1TernaryMap).value_or(2);
}

static constexpr std::pair<std::string_view, mfxU16> kWeightedPredModeMap[] = {
    {"AUTO", MFX_WEIGHTED_PRED_UNKNOWN},
    {"OFF", MFX_WEIGHTED_PRED_UNKNOWN},
    {"DEFAULT", MFX_WEIGHTED_PRED_DEFAULT},
    {"EXPLICIT", MFX_WEIGHTED_PRED_EXPLICIT},
    {"IMPLICIT", MFX_WEIGHTED_PRED_IMPLICIT},
};

static inline mfxU16 ParseWeightedPredMode(const char *Data) {
  return MapString(Data, kWeightedPredModeMap).value_or(MFX_WEIGHTED_PRED_UNKNOWN);
}

#ifdef ONEVPL_EXPERIMENTAL
static constexpr std::pair<std::string_view, mfxU32> kTuneQualityMap[] = {
    {"OFF", 0},
    {"VMAF", MFX_ENCODE_TUNE_VMAF},
    {"PERCEPTUAL", MFX_ENCODE_TUNE_PERCEPTUAL},
    {"VMAF+PERCEPTUAL", MFX_ENCODE_TUNE_VMAF | MFX_ENCODE_TUNE_PERCEPTUAL},
};

static inline mfxU32 ParseTuneQuality(const char *Data) {
  return MapString(Data, kTuneQualityMap).value_or(0);
}
#endif

static constexpr LevelEntry kAVCLevels[] = {
    {"auto", 0},
    {"1", MFX_LEVEL_AVC_1},
    {"1b", MFX_LEVEL_AVC_1b},
    {"1.1", MFX_LEVEL_AVC_11},
    {"1.2", MFX_LEVEL_AVC_12},
    {"1.3", MFX_LEVEL_AVC_13},
    {"2", MFX_LEVEL_AVC_2},
    {"2.1", MFX_LEVEL_AVC_21},
    {"2.2", MFX_LEVEL_AVC_22},
    {"3", MFX_LEVEL_AVC_3},
    {"3.1", MFX_LEVEL_AVC_31},
    {"3.2", MFX_LEVEL_AVC_32},
    {"4", MFX_LEVEL_AVC_4},
    {"4.1", MFX_LEVEL_AVC_41},
    {"4.2", MFX_LEVEL_AVC_42},
    {"5", MFX_LEVEL_AVC_5},
    {"5.1", MFX_LEVEL_AVC_51},
    {"5.2", MFX_LEVEL_AVC_52},
    {"6", MFX_LEVEL_AVC_6},
    {"6.1", MFX_LEVEL_AVC_61},
    {"6.2", MFX_LEVEL_AVC_62},
};

static constexpr LevelEntry kHEVCLevels[] = {
    {"auto", 0},
    {"1", MFX_LEVEL_HEVC_1},
    {"2", MFX_LEVEL_HEVC_2},
    {"2.1", MFX_LEVEL_HEVC_21},
    {"3", MFX_LEVEL_HEVC_3},
    {"3.1", MFX_LEVEL_HEVC_31},
    {"4", MFX_LEVEL_HEVC_4},
    {"4.1", MFX_LEVEL_HEVC_41},
    {"5", MFX_LEVEL_HEVC_5},
    {"5.1", MFX_LEVEL_HEVC_51},
    {"5.2", MFX_LEVEL_HEVC_52},
    {"6", MFX_LEVEL_HEVC_6},
    {"6.1", MFX_LEVEL_HEVC_61},
    {"6.2", MFX_LEVEL_HEVC_62},
    {"8.5", MFX_LEVEL_HEVC_85},
};

static constexpr LevelEntry kAV1Levels[] = {
    {"auto", 0},
    {"2.0", MFX_LEVEL_AV1_2},
    {"2.1", MFX_LEVEL_AV1_21},
    {"2.2", MFX_LEVEL_AV1_22},
    {"2.3", MFX_LEVEL_AV1_23},
    {"3.0", MFX_LEVEL_AV1_3},
    {"3.1", MFX_LEVEL_AV1_31},
    {"3.2", MFX_LEVEL_AV1_32},
    {"3.3", MFX_LEVEL_AV1_33},
    {"4.0", MFX_LEVEL_AV1_4},
    {"4.1", MFX_LEVEL_AV1_41},
    {"4.2", MFX_LEVEL_AV1_42},
    {"4.3", MFX_LEVEL_AV1_43},
    {"5.0", MFX_LEVEL_AV1_5},
    {"5.1", MFX_LEVEL_AV1_51},
    {"5.2", MFX_LEVEL_AV1_52},
    {"5.3", MFX_LEVEL_AV1_53},
    {"6.0", MFX_LEVEL_AV1_6},
    {"6.1", MFX_LEVEL_AV1_61},
    {"6.2", MFX_LEVEL_AV1_62},
    {"6.3", MFX_LEVEL_AV1_63},
    {"7.0", MFX_LEVEL_AV1_7},
    {"7.1", MFX_LEVEL_AV1_71},
    {"7.2", MFX_LEVEL_AV1_72},
    {"7.3", MFX_LEVEL_AV1_73},
};

static constexpr std::pair<std::string_view, mfxU16> kTargetUsageMap[] = {
    {"TU1 (Veryslow)", MFX_TARGETUSAGE_1},
    {"TU2 (Slower)", MFX_TARGETUSAGE_2},
    {"TU3 (Slow)", MFX_TARGETUSAGE_3},
    {"TU4 (Balanced)", MFX_TARGETUSAGE_4},
    {"TU5 (Fast)", MFX_TARGETUSAGE_5},
    {"TU6 (Faster)", MFX_TARGETUSAGE_6},
    {"TU7 (Veryfast)", MFX_TARGETUSAGE_7},
    {"Best Quality (TU1-TU2)", MFX_TARGETUSAGE_1},
    {"Balanced (TU3-TU5)", MFX_TARGETUSAGE_4},
    {"Fastest (TU6-TU7)", MFX_TARGETUSAGE_7},
};

static constexpr std::pair<std::string_view, mfxU16> kRateControlMap[] = {
    {"CBR", MFX_RATECONTROL_CBR},
    {"VBR", MFX_RATECONTROL_VBR},
    {"CQP", MFX_RATECONTROL_CQP},
    {"AVBR", MFX_RATECONTROL_AVBR},
    {"ICQ", MFX_RATECONTROL_ICQ},
    {"VCM", MFX_RATECONTROL_VCM},
    {"QVBR", MFX_RATECONTROL_QVBR},
};

static constexpr std::pair<std::string_view, mfxU16> kAV1InterpFilterMap[] = {
    {"DEFAULT", 0},
    {"EIGHTTAP", 1},
    {"EIGHTTAP_SMOOTH", 2},
    {"EIGHTTAP_SHARP", 3},
    {"BILINEAR", 4},
    {"SWITCHABLE", 5},
};

static constexpr std::pair<std::string_view, bool> kEncToolsMap[] = {
    {"ON", true},
};

static constexpr std::pair<std::string_view, int> kMVCostScalingFactorMap[] = {
    {"AGGRESSIVE_0", 0},
    {"AGGRESSIVE_1", 1},
    {"MODERATE_2", 2},
    {"CONSERVATIVE_3", 3},
};

static constexpr std::pair<std::string_view, int> kLookaheadDSMap[] = {
    {"1X", 0},
    {"2X", 1},
    {"4X", 2},
};

static constexpr std::pair<std::string_view, bool> kLowPowerMap[] = {
    {"ON", true},
    {"OFF", false},
};

static constexpr std::pair<std::string_view, int> kTrellisMap[] = {
    {"OFF", 0},
    {"I", 1},
    {"IP", 2},
    {"IPB", 3},
    {"IB", 4},
    {"P", 5},
    {"PB", 6},
    {"B", 7},
};

static constexpr std::pair<std::string_view, int> kSAOMap[] = {
    {"DISABLE", 0},
    {"LUMA", 1},
    {"CHROMA", 2},
    {"ALL", 3},
};

static constexpr std::pair<std::string_view, std::optional<mfxU16>>
    kScenarioInfoMap[] = {
        {"OFF", std::nullopt},
        {"AUTO", std::optional<mfxU16>(0)},
        {"DISPLAY_REMOTING", std::optional<mfxU16>(1)},
        {"VIDEO_CONFERENCE", std::optional<mfxU16>(2)},
        {"ARCHIVE", std::optional<mfxU16>(3)},
        {"LIVE_STREAMING", std::optional<mfxU16>(4)},
        {"CAMERA_CAPTURE", std::optional<mfxU16>(5)},
        {"VIDEO_SURVEILLANCE", std::optional<mfxU16>(6)},
        {"GAME_STREAMING", std::optional<mfxU16>(7)},
        {"REMOTE_GAMING", std::optional<mfxU16>(8)},
};

static constexpr std::pair<std::string_view, std::optional<mfxU16>>
    kContentInfoMap[] = {
        {"OFF", std::nullopt},
        {"AUTO", std::optional<mfxU16>(MFX_CONTENT_UNKNOWN)},
        {"FULL_SCREEN_VIDEO",
         std::optional<mfxU16>(MFX_CONTENT_FULL_SCREEN_VIDEO)},
        {"NON_VIDEO_SCREEN",
         std::optional<mfxU16>(MFX_CONTENT_NON_VIDEO_SCREEN)},
        {"NOISY_VIDEO", std::optional<mfxU16>(MFX_CONTENT_NOISY_VIDEO)},
};

static constexpr std::pair<std::string_view, std::optional<bool>>
    kTransformSkipMap[] = {
        {"AUTO", std::nullopt},
        {"ON", true},
        {"OFF", false},
};

static constexpr std::pair<std::string_view, std::optional<bool>>
    kFadeDetectionMap[] = {
        {"AUTO", std::nullopt},
        {"ON", true},
        {"OFF", false},
};

static constexpr std::pair<std::string_view, int> kDenoiseModeMap[] = {
    {"DEFAULT", 0},
    {"AUTO | BDRATE | PRE ENCODE", 1},
    {"AUTO | ADJUST | POST ENCODE", 2},
    {"AUTO | SUBJECTIVE | PRE ENCODE", 3},
    {"MANUAL | PRE ENCODE", 4},
    {"MANUAL | POST ENCODE", 5},
};

static constexpr std::pair<std::string_view, std::optional<int>> kScalingModeMap[] = {
    {"OFF", std::nullopt},
    {"QUALITY | ADVANCED", std::optional<int>(1)},
    {"VEBOX | ADVANCED", std::optional<int>(2)},
    {"LOWPOWER | NEAREST NEIGHBOR", std::optional<int>(3)},
    {"LOWPOWER | ADVANCED", std::optional<int>(4)},
    {"AUTO", std::optional<int>(0)},
};

static constexpr std::pair<std::string_view, int> kImageStabModeMap[] = {
    {"UPSCALE", 1},
    {"BOXING", 2},
    {"AUTO", 0},
};

static constexpr std::pair<std::string_view, int> kPercEncPrefilterMap[] = {
    {"ON", 1},
    {"OFF", 0},
};

static constexpr std::pair<std::string_view, int> kScreenContentToolsMap[] = {
    {"AUTO", 0},
    {"OFF", 1},
    {"ON", 2},
};

static constexpr std::pair<std::string_view, mfxU16> kCodecProfileAVCMap[] = {
    {"baseline", MFX_PROFILE_AVC_BASELINE},
    {"main", MFX_PROFILE_AVC_MAIN},
    {"high", MFX_PROFILE_AVC_HIGH},
    {"extended", MFX_PROFILE_AVC_EXTENDED},
    {"high10", MFX_PROFILE_AVC_HIGH10},
    {"constrained_baseline", MFX_PROFILE_AVC_CONSTRAINED_BASELINE},
    {"constrained_high", MFX_PROFILE_AVC_CONSTRAINED_HIGH},
};

static constexpr std::pair<std::string_view, mfxU16> kCodecProfileHEVCMap[] = {
    {"main", MFX_PROFILE_HEVC_MAIN},
    {"main10", MFX_PROFILE_HEVC_MAIN10},
    {"rext", MFX_PROFILE_HEVC_REXT},
    {"scc", MFX_PROFILE_HEVC_SCC},
};

static constexpr std::pair<std::string_view, mfxU16> kCodecProfileAV1Map[] = {
    {"main", MFX_PROFILE_AV1_MAIN},
    {"high", MFX_PROFILE_AV1_HIGH},
    {"pro", MFX_PROFILE_AV1_PRO},
};

static constexpr std::pair<std::string_view, mfxU16> kCodecProfileVP9Map[] = {
    {"0 (8-bit 4:2:0)", MFX_PROFILE_VP9_0},
    {"1 (8-bit 4:4:4)", MFX_PROFILE_VP9_1},
    {"2 (10-bit 4:2:0)", MFX_PROFILE_VP9_2},
    {"3 (10-bit 4:4:4)", MFX_PROFILE_VP9_3},
};

// Parse all UI-configurable encoder params from an obs_data_t object.
// This mirrors GetEncoderParams() but does NOT touch fields that come from
// the OBS video output (width/height/fps, color info, FourCC, bit depth, HDR).
// The caller is responsible for setting those after this call.
static inline void ParseEncoderParamsFromObsData(obs_data_t *Settings,
                                                 codec_enum Codec,
                                                 encoder_params &Params) {
  const char *TargetUsageData = obs_data_get_string(Settings, "target_usage");
  const char *CodecProfileData = obs_data_get_string(Settings, "profile");
  const char *CodecProfileTierData = obs_data_get_string(Settings, "hevc_tier");
  const char *CodecLevelDataHEVC = obs_data_get_string(Settings, "hevc_level");
  const char *CodecLevelDataAVC = obs_data_get_string(Settings, "avc_level");
  const char *CodecLevelDataAV1 = obs_data_get_string(Settings, "av1_level");
  const char *RateControlData = obs_data_get_string(Settings, "rate_control");

  int TargetBitrateData = static_cast<int>(obs_data_get_int(Settings, "bitrate"));
  bool CustomBufferSizeData = obs_data_get_bool(Settings, "custom_buffer_size");
  int BufferSizeData = static_cast<int>(obs_data_get_int(Settings, "buffer_size"));
  int MaxBitrateData = static_cast<int>(obs_data_get_int(Settings, "max_bitrate"));

  double CQPData;
  if (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) {
    CQPData = obs_data_get_double(Settings, "cqp");
  } else {
    CQPData = static_cast<double>(obs_data_get_int(Settings, "cqp"));
  }

  int ICQQualityData = static_cast<int>(obs_data_get_int(Settings, "icq_quality"));
  if (Codec == QSV_CODEC_VP9)
    ICQQualityData *= 4;

  int KeyIntervalData = static_cast<int>(obs_data_get_int(Settings, "keyint_sec"));
  int BFramesData = static_cast<int>(obs_data_get_int(Settings, "b_frames"));

  const char *HRDConformanceData =
      obs_data_get_string(Settings, "hrd_conformance");
  const char *LowDelayHRDData = obs_data_get_string(Settings, "low_delay_hrd");
  const char *LowDelayBRCData = obs_data_get_string(Settings, "low_delay_brc");
  const char *SkipFrameData = obs_data_get_string(Settings, "skip_frame");
  const char *RepartitionCheckData =
      obs_data_get_string(Settings, "repartition_check");

  const char *MBBRCData = obs_data_get_string(Settings, "mbbrc");
  const char *AdaptiveIData = obs_data_get_string(Settings, "adaptive_i");
  const char *AdaptiveBData = obs_data_get_string(Settings, "adaptive_b");
#ifndef QSV_UHD600_SUPPORT
  const char *AdaptiveRefData = obs_data_get_string(Settings, "adaptive_ref");
  const char *AdaptiveCQMData = obs_data_get_string(Settings, "adaptive_cqm");
  const char *AdaptiveLTRData = obs_data_get_string(Settings, "adaptive_ltr");
#endif
  const char *LowPowerData = obs_data_get_string(Settings, "low_power");
  const char *UseRawRefData = obs_data_get_string(Settings, "use_raw_ref");
  const char *RDOData = obs_data_get_string(Settings, "rdo");
  const char *TrellisData = obs_data_get_string(Settings, "trellis");
  int NumRefFrameData = static_cast<int>(obs_data_get_int(Settings, "num_ref_frame"));
  int NumRefActivePData =
      static_cast<int>(obs_data_get_int(Settings, "num_ref_active_p"));
  int NumRefActiveBL0Data =
      static_cast<int>(obs_data_get_int(Settings, "num_ref_active_bl0"));
  int NumRefActiveBL1Data =
      static_cast<int>(obs_data_get_int(Settings, "num_ref_active_bl1"));
  const char *GlobalMotionBiasAdjustmentData =
      obs_data_get_string(Settings, "global_motion_bias_adjustment");
  const char *MVCostScalingFactorData =
      obs_data_get_string(Settings, "mv_cost_scaling_factor");
  const char *LookaheadData = obs_data_get_string(Settings, "lookahead");
  const char *LookaheadDSData = obs_data_get_string(Settings, "lookahead_ds");
  const char *DirectBiasAdjustmentData =
      obs_data_get_string(Settings, "direct_bias_adjustment");
  const char *MVOverPicBoundariesData =
      obs_data_get_string(Settings, "mv_overpic_boundaries");
  const char *SAOData = obs_data_get_string(Settings, "hevc_sao");
  const char *GPBData = obs_data_get_string(Settings, "hevc_gpb");
  const char *ScenarioInfoData =
      obs_data_get_string(Settings, "scenario_info");
  const char *ContentInfoData =
      obs_data_get_string(Settings, "content_info");
  const char *TransformSkipData =
      obs_data_get_string(Settings, "transform_skip");
  const char *FadeDetectionData =
      obs_data_get_string(Settings, "fade_detection");
  const char *AV1CDEFData = obs_data_get_string(Settings, "av1_cdef");
  const char *AV1RestorationData =
      obs_data_get_string(Settings, "av1_restoration");
  const char *AV1LoopFilterData =
      obs_data_get_string(Settings, "av1_loop_filter");
  const char *AV1SuperResData = obs_data_get_string(Settings, "av1_super_res");
  const char *AV1InterpFilterData =
      obs_data_get_string(Settings, "av1_interp_filter");
  const char *AV1ErrorResilientData =
      obs_data_get_string(Settings, "av1_error_resilient");
  const char *AV1SegmentationData =
      obs_data_get_string(Settings, "av1_segmentation");
#ifdef ONEVPL_EXPERIMENTAL
  const char *TuneQualityData = obs_data_get_string(Settings, "tune_quality");
#endif
  const char *WeightedPredData = obs_data_get_string(Settings, "weighted_pred");
  const char *WeightedBiPredData =
      obs_data_get_string(Settings, "weighted_bi_pred");
  int AdaptiveMaxFrameSizeData =
      static_cast<int>(obs_data_get_int(Settings, "adaptive_max_frame_size"));
#ifndef QSV_UHD600_SUPPORT
  const char *VPPMCTFData = obs_data_get_string(Settings, "vpp_mctf");
  int VPPMCTFStrengthData =
      static_cast<int>(obs_data_get_int(Settings, "vpp_mctf_strength"));
#endif
  const char *PPyramidData = obs_data_get_string(Settings, "p_pyramid");
  const char *EncToolsData = obs_data_get_string(Settings, "enctools");
  const char *IntraRefEncodingData =
      obs_data_get_string(Settings, "intra_ref_encoding");
  const char *IntraRefTypeData =
      obs_data_get_string(Settings, "intra_ref_type");
  int IntraRefCycleSizeData =
      static_cast<int>(obs_data_get_int(Settings, "intra_ref_cycle_size"));
  int IntraRefQPDeltaData =
      static_cast<int>(obs_data_get_int(Settings, "intra_ref_qp_delta"));

  const char *ScreenContentToolsData =
      obs_data_get_string(Settings, "screen_content_tools");

  const char *MinQPData = obs_data_get_string(Settings, "min_qp");
  const char *MaxQPData = obs_data_get_string(Settings, "max_qp");

  const char *VideoProcessingStatusData =
      obs_data_get_string(Settings, "vpp");
  int DenoiseStrengthData =
      static_cast<int>(obs_data_get_int(Settings, "denoise_strength"));
  const char *DenoiseModeData =
      obs_data_get_string(Settings, "denoise_mode");
  const char *DetailData = obs_data_get_string(Settings, "detail");
  int DetailFactorData =
      static_cast<int>(obs_data_get_int(Settings, "detail_factor"));
  const char *ScalingModeData =
      obs_data_get_string(Settings, "scaling_mode");
  const char *ImageStabModeData =
      obs_data_get_string(Settings, "image_stab_mode");
  const char *PercEncPrefilterData =
      obs_data_get_string(Settings, "perc_enc_prefilter");

  const char *VPPProcAmpData =
      obs_data_get_string(Settings, "vpp_procamp");
  const char *VPPRotationData =
      obs_data_get_string(Settings, "vpp_rotation");
  const char *VPPMirroringData =
      obs_data_get_string(Settings, "vpp_mirroring");
  const char *VPPFRCData =
      obs_data_get_string(Settings, "vpp_frc");

  int GPUNumData = static_cast<int>(obs_data_get_int(Settings, "gpu_number"));

  // 1. TargetUsage
  if (auto v = MapString(TargetUsageData, kTargetUsageMap))
    Params.TargetUsage = *v;

  // 2. AV1 ternary options
  Params.AV1CDEF = ParseAV1Ternary(AV1CDEFData);
  Params.AV1Restoration = ParseAV1Ternary(AV1RestorationData);
  Params.AV1LoopFilter = ParseAV1Ternary(AV1LoopFilterData);
  Params.AV1SuperRes = ParseAV1Ternary(AV1SuperResData);
  Params.AV1ErrorResilient = ParseAV1Ternary(AV1ErrorResilientData);

  auto svSeg = std::string_view(AV1SegmentationData);
  if (svSeg == "ON")
    Params.AV1Segmentation = 1;
  else if (svSeg == "OFF")
    Params.AV1Segmentation = 0;

#ifdef ONEVPL_EXPERIMENTAL
  Params.TuneQuality =
      (Codec == QSV_CODEC_AV1) ? ParseTuneQuality(TuneQualityData) : 0;
#endif

  // 3. AV1InterpFilter
  if (auto v = MapString(AV1InterpFilterData, kAV1InterpFilterMap))
    Params.AV1InterpFilter = *v;

  Params.WeightedPred = ParseWeightedPredMode(WeightedPredData);
  Params.WeightedBiPred = ParseWeightedPredMode(WeightedBiPredData);

  Params.AdaptiveMaxFrameSize = static_cast<mfxU32>(AdaptiveMaxFrameSizeData);

  // SkipFrame
  auto svSkip = std::string_view(SkipFrameData);
  if (svSkip == "NO_SKIP")
    Params.SkipFrame = MFX_SKIPFRAME_NO_SKIP;
  else if (svSkip == "INSERT_DUMMY")
    Params.SkipFrame = MFX_SKIPFRAME_INSERT_DUMMY;
  else if (svSkip == "INSERT_NOTHING")
    Params.SkipFrame = MFX_SKIPFRAME_INSERT_NOTHING;
  else if (svSkip == "BRC_ONLY")
    Params.SkipFrame = MFX_SKIPFRAME_BRC_ONLY;

  // RepartitionCheckEnable
  auto svRepart = std::string_view(RepartitionCheckData);
  if (svRepart == "OFF")
    Params.RepartitionCheckEnable = false;
  else if (svRepart == "ON")
    Params.RepartitionCheckEnable = true;

#ifndef QSV_UHD600_SUPPORT
  if (std::string_view(VPPMCTFData) == "ON")
    Params.VPPMCTFMode = 1;
  else
    Params.VPPMCTFMode = 0;
  Params.VPPMCTFStrength = static_cast<mfxU16>(VPPMCTFStrengthData);
#endif

  // Codec profile/level
  switch (Codec) {
  case QSV_CODEC_AVC: {
    if (auto v = MapString(CodecProfileData, kCodecProfileAVCMap))
      Params.CodecProfile = *v;
    Params.CodecLevel =
        ParseCodecLevel(CodecLevelDataAVC, kAVCLevels,
                        sizeof(kAVCLevels) / sizeof(kAVCLevels[0]));
    break;
  }
  case QSV_CODEC_HEVC: {
    if (auto v = MapString(CodecProfileData, kCodecProfileHEVCMap))
      Params.CodecProfile = *v;

    if (std::string_view(CodecProfileTierData) == "main") {
      Params.CodecProfileTier = MFX_TIER_HEVC_MAIN;
    } else {
      // Mirror the live encoder logic: fall back to Main Tier on pre-Skylake
      // platforms where High Tier is not supported.
      mfxU16 platformCode = QueryPlatformCodeName();
      bool highTierUnsupported = platformCode != 0 &&
                                 platformCode < MFX_PLATFORM_SKYLAKE;
      Params.CodecProfileTier = highTierUnsupported ? MFX_TIER_HEVC_MAIN
                                                    : MFX_TIER_HEVC_HIGH;
    }

    Params.CodecLevel =
        ParseCodecLevel(CodecLevelDataHEVC, kHEVCLevels,
                        sizeof(kHEVCLevels) / sizeof(kHEVCLevels[0]));
    break;
  }
  case QSV_CODEC_AV1: {
    if (auto v = MapString(CodecProfileData, kCodecProfileAV1Map))
      Params.CodecProfile = *v;
    Params.CodecLevel =
        ParseCodecLevel(CodecLevelDataAV1, kAV1Levels,
                        sizeof(kAV1Levels) / sizeof(kAV1Levels[0]));
    break;
  }
  case QSV_CODEC_VP9: {
    if (auto v = MapString(CodecProfileData, kCodecProfileVP9Map))
      Params.CodecProfile = *v;
    break;
  }
  }

  // VideoFormat is always unspecified in the UI.
  Params.VideoFormat = 5;

  ParseOptionalBool(LowDelayHRDData, Params.LowDelayHRD);
  ParseOptionalBool(LowDelayBRCData, Params.LowDelayBRC);
  ParseOptionalBool(MVOverPicBoundariesData,
                    Params.MotionVectorsOverPicBoundaries);
  ParseOptionalBool(HRDConformanceData, Params.HRDConformance);
  ParseOptionalBool(MBBRCData, Params.MBBRC);

  Params.EncTools = MapString(EncToolsData, kEncToolsMap).value_or(false);

  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_scene_change"),
                    Params.EncToolsSceneChange);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_ref_p"),
                    Params.EncToolsAdaptiveRefP);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_ref_b"),
                    Params.EncToolsAdaptiveRefB);
  ParseOptionalBool(
      obs_data_get_string(Settings, "enc_tools_adaptive_pyramid_quant_p"),
      Params.EncToolsAdaptivePyramidQuantP);
  ParseOptionalBool(
      obs_data_get_string(Settings, "enc_tools_adaptive_pyramid_quant_b"),
      Params.EncToolsAdaptivePyramidQuantB);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_mbqp"),
                    Params.EncToolsAdaptiveMBQP);
  ParseOptionalBool(
      obs_data_get_string(Settings, "enc_tools_brc_buffer_hints"),
      Params.EncToolsBRCBufferHints);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_brc"),
                    Params.EncToolsBRC);
  ParseOptionalBool(
      obs_data_get_string(Settings, "enc_tools_saliency_map_hint"),
      Params.EncToolsSaliencyMapHint);

  ParseOptionalBool(DirectBiasAdjustmentData,
                    Params.DirectBiasAdjustment);

  if (auto v = MapString(MVCostScalingFactorData, kMVCostScalingFactorMap))
    Params.MVCostScalingFactor = *v;

  ParseOptionalBool(UseRawRefData, Params.RawRef);

  Params.PPyramid = (std::string_view(PPyramidData) == "ON");

  ParseOptionalBool(GlobalMotionBiasAdjustmentData,
                    Params.GlobalMotionBiasAdjustment);

  // Lookahead
  auto svLookahead = std::string_view(LookaheadData);
  if (svLookahead == "HQ") {
    Params.Lookahead = true;
    int Depth = static_cast<int>(obs_data_get_int(Settings, "la_depth"));
    if (Depth < 1)
      Depth = 60;
    else if (Depth > 100)
      Depth = 100;
    Params.LADepth = static_cast<mfxU16>(Depth);
  } else if (svLookahead == "LP") {
    if (BFramesData > 0) {
      Params.Lookahead = true;
      Params.LADepth =
          BFramesData > 7 ? 8 : static_cast<mfxU16>(BFramesData + 1);
    }
  } else {
    Params.Lookahead = false;
  }

  if (auto v = MapString(LookaheadDSData, kLookaheadDSMap))
    Params.LookAheadDS = *v;

  // IntraRefEncoding
  static constexpr std::pair<std::string_view, bool> kIntraRefEncodingMap[] = {
      {"ON", true},
      {"OFF", false},
  };
  if (auto v = MapString(IntraRefEncodingData, kIntraRefEncodingMap))
    Params.IntraRefEncoding = *v;

  if (std::string_view(IntraRefTypeData) == "VERTICAL")
    Params.IntraRefType = MFX_REFRESH_VERTICAL;
  else
    Params.IntraRefType = MFX_REFRESH_HORIZONTAL;

  ParseOptionalBool(AdaptiveIData, Params.AdaptiveI);
  ParseOptionalBool(AdaptiveBData, Params.AdaptiveB);
#ifndef QSV_UHD600_SUPPORT
  ParseOptionalBool(AdaptiveRefData, Params.AdaptiveRef);
  ParseOptionalBool(AdaptiveCQMData, Params.AdaptiveCQM);
  ParseOptionalBool(AdaptiveLTRData, Params.AdaptiveLTR);
#endif

  if (auto v = MapString(LowPowerData, kLowPowerMap))
    Params.Lowpower = *v;

  ParseOptionalBool(RDOData, Params.RDO);

  if (auto v = MapString(TrellisData, kTrellisMap))
    Params.Trellis = *v;

  if (auto v = MapString(SAOData, kSAOMap))
    Params.SAO = *v;

  ParseOptionalBool(GPBData, Params.GPB);

  if (auto v = MapString(ScenarioInfoData, kScenarioInfoMap))
    Params.ScenarioInfo = *v;

  if (auto v = MapString(ContentInfoData, kContentInfoMap))
    Params.ContentInfo = *v;

  if (auto v = MapString(TransformSkipData, kTransformSkipMap))
    Params.TransformSkip = *v;

  if (auto v = MapString(FadeDetectionData, kFadeDetectionMap))
    Params.FadeDetection = *v;

  // RateControl
  if (auto v = MapString(RateControlData, kRateControlMap))
    Params.RateControl = *v;

  // DenoiseMode
  if (auto v = MapString(DenoiseModeData, kDenoiseModeMap))
    Params.VPPDenoiseMode = *v;
  auto svDenoise = std::string_view(DenoiseModeData);
  if (svDenoise == "MANUAL | PRE ENCODE" ||
      svDenoise == "MANUAL | POST ENCODE") {
    Params.DenoiseStrength = static_cast<mfxU16>(DenoiseStrengthData);
  }

  if (auto v = MapString(ScalingModeData, kScalingModeMap))
    Params.VPPScalingMode = *v;

  int64_t VPPOutWidthData = obs_data_get_int(Settings, "vpp_out_width");
  int64_t VPPOutHeightData = obs_data_get_int(Settings, "vpp_out_height");
  if (VPPOutWidthData > 0 && VPPOutHeightData > 0) {
    Params.VPPOutWidth = static_cast<mfxU16>(VPPOutWidthData);
    Params.VPPOutHeight = static_cast<mfxU16>(VPPOutHeightData);
  }

  if (auto v = MapString(ImageStabModeData, kImageStabModeMap))
    Params.VPPImageStabMode = *v;

  std::string_view DetailSV(DetailData);
  if (DetailSV == "ON")
    Params.VPPDetail = DetailFactorData;
  else if (DetailSV == "OFF")
    Params.VPPDetail = 0;

  if (auto v = MapString(PercEncPrefilterData, kPercEncPrefilterMap))
    Params.PercEncPrefilter = *v;

  // ProcAmp
  if (std::string_view(VPPProcAmpData) == "ON") {
    Params.VPPProcAmpMode = 1;
    Params.VPPProcAmpBrightness =
        obs_data_get_double(Settings, "vpp_procamp_brightness");
    Params.VPPProcAmpContrast =
        obs_data_get_double(Settings, "vpp_procamp_contrast");
    Params.VPPProcAmpHue =
        obs_data_get_double(Settings, "vpp_procamp_hue");
    Params.VPPProcAmpSaturation =
        obs_data_get_double(Settings, "vpp_procamp_saturation");
  }

  // Rotation
  if (std::string_view(VPPRotationData) == "90")
    Params.VPPRotation = 90;
  else if (std::string_view(VPPRotationData) == "180")
    Params.VPPRotation = 180;
  else if (std::string_view(VPPRotationData) == "270")
    Params.VPPRotation = 270;

  // Mirroring
  if (std::string_view(VPPMirroringData) == "HORIZONTAL")
    Params.VPPMirroring = 1;
  else if (std::string_view(VPPMirroringData) == "VERTICAL")
    Params.VPPMirroring = 2;
  else if (std::string_view(VPPMirroringData) == "BOTH")
    Params.VPPMirroring = 3;

  // Frame Rate Conversion
  static constexpr std::pair<std::string_view, int> kFRCModeMap[] = {
    {"PRESERVE_TIMESTAMP",                    0},
    {"DISTRIBUTED_TIMESTAMP",                 1},
    {"FRAME_INTERPOLATION",                   2},
    {"PRESERVE_TIMESTAMP + INTERPOLATION",    3},
    {"DISTRIBUTED_TIMESTAMP + INTERPOLATION", 4},
  };
  if (auto v = MapString(VPPFRCData, kFRCModeMap)) {
    Params.VPPFRCMode = *v;
    Params.VPPOutFpsNum =
        static_cast<mfxU32>(obs_data_get_int(Settings, "vpp_frc_out_fps"));
    Params.VPPOutFpsDen = 1;
  }

  Params.AsyncDepth =
      static_cast<mfxU16>(obs_data_get_int(Settings, "async_depth"));

  // CQP / QPI/QPP/QPB
  bool CQPSeparateIPB = obs_data_get_bool(Settings, "cqp_separate_ipb");
  if (CQPSeparateIPB) {
    double QPIData, QPPData, QPBData;
    if (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) {
      QPIData = obs_data_get_double(Settings, "qpi");
      QPPData = obs_data_get_double(Settings, "qpp");
      QPBData = obs_data_get_double(Settings, "qpb");
    } else {
      QPIData = static_cast<double>(obs_data_get_int(Settings, "qpi"));
      QPPData = static_cast<double>(obs_data_get_int(Settings, "qpp"));
      QPBData = static_cast<double>(obs_data_get_int(Settings, "qpb"));
    }
    if (Codec == QSV_CODEC_VP9 || Codec == QSV_CODEC_AV1) {
      QPIData *= 4.0;
      QPPData *= 4.0;
      QPBData *= 4.0;
    }
    Params.QPI = static_cast<mfxU16>(QPIData);
    Params.QPP = static_cast<mfxU16>(QPPData);
    Params.QPB = static_cast<mfxU16>(QPBData);
  } else {
    double ActualCQPData = CQPData;
    if (Codec == QSV_CODEC_VP9 || Codec == QSV_CODEC_AV1)
      ActualCQPData *= 4.0;
    Params.QPI = static_cast<mfxU16>(ActualCQPData);
    Params.QPP = static_cast<mfxU16>(ActualCQPData);
    Params.QPB = static_cast<mfxU16>(ActualCQPData);
  }

  Params.TargetBitRate = static_cast<uint32_t>(TargetBitrateData);
  Params.CustomBufferSize = CustomBufferSizeData;
  Params.BufferSize = static_cast<uint32_t>(BufferSizeData);
  Params.MaxBitRate = static_cast<uint32_t>(MaxBitrateData);

  Params.BFrames = static_cast<mfxU16>(BFramesData);
  Params.KeyIntSec = static_cast<mfxU16>(KeyIntervalData);
  Params.ICQQuality = static_cast<mfxU16>(ICQQualityData);
  Params.NumRefFrame = static_cast<mfxU16>(NumRefFrameData);
  Params.NumRefActiveP = static_cast<mfxU16>(NumRefActivePData);
  Params.NumRefActiveBL0 = static_cast<mfxU16>(NumRefActiveBL0Data);
  Params.NumRefActiveBL1 = static_cast<mfxU16>(NumRefActiveBL1Data);

  Params.IntraRefCycleSize = static_cast<mfxU16>(IntraRefCycleSizeData);
  Params.IntraRefQPDelta = static_cast<mfxI16>(IntraRefQPDeltaData);

  Params.QVBRQuality =
      static_cast<mfxU16>(obs_data_get_int(Settings, "qvbr_quality"));

  if (auto v = MapString(ScreenContentToolsData, kScreenContentToolsMap))
    Params.ScreenContentTools = *v;

  const char *CustomCodingOptionsData =
      obs_data_get_string(Settings, "custom_coding_options");
  if (CustomCodingOptionsData)
    Params.CustomCodingOptions = CustomCodingOptionsData;

  Params.MinQP = MinQPData ? MinQPData : "-1";
  Params.MaxQP = MaxQPData ? MaxQPData : "-1";

  Params.QPStatistics = obs_data_get_bool(Settings, "qp_statistics");
  Params.VideoHeaderHexDump =
      obs_data_get_bool(Settings, "video_header_hex_dump");

  Params.GPUNum = GPUNumData;

  // ProcessingEnable is derived from VPP settings + input format, so leave it
  // to the caller.
  Params.ProcessingEnable = false;
}
