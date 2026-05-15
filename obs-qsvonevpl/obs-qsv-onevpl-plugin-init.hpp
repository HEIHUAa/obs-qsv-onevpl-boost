#pragma once

#ifndef __QSV_VPL_PLUGIN_INIT_H__
#define __QSV_VPL_PLUGIN_INIT_H__
#endif

#ifndef __QSV_VPL_ENCODER_H__
#include "obs-qsv-onevpl-encoder.hpp"
#endif

struct plugin_context {
  obs_encoder_t *EncoderData;

  enum codec_enum Codec;

  struct encoder_params EncoderParams;

  std::unique_ptr<class QSVEncoder> EncoderPTR;

  std::vector<uint8_t> PacketData;

  std::pair<uint8_t*, size_t> ExtraData;
  std::pair<uint8_t*, size_t> SEI;

  os_performance_token_t *PerformanceToken;

  uint32_t roi_increment;

  mfxU32 CachedFpsNum;
  mfxU32 CachedFpsDen;
  int64_t CachedTSDiv;
};

#define TEXT_SPEED obs_module_text("TargetUsage")
#define TEXT_TARGET_BITRATE obs_module_text("Bitrate")
#define TEXT_CUSTOM_BUFFER_SIZE obs_module_text("CustomBufferSize")
#define TEXT_BUFFER_SIZE obs_module_text("BufferSize")
#define TEXT_MAX_BITRATE obs_module_text("MaxBitrate")
#define TEXT_PROFILE obs_module_text("Profile")
#define TEXT_HEVC_TIER obs_module_text("Tier")
#define TEXT_HEVC_LEVEL obs_module_text("Level")
#define TEXT_RATE_CONTROL obs_module_text("RateControl")
#define TEXT_ICQ_QUALITY obs_module_text("ICQQuality")
#define TEXT_QVBR_QUALITY obs_module_text("QVBRQuality")
#define TEXT_KEYINT_SEC obs_module_text("KeyframeIntervalSec")
#define TEXT_B_FRAMES obs_module_text("BFrames")
#define TEXT_MBBRC obs_module_text("MBBRC")
#define TEXT_NUM_REF_FRAME obs_module_text("NumRefFrame")
#define TEXT_NUM_REF_ACTIVE_P obs_module_text("NumRefActiveP")
#define TEXT_NUM_REF_ACTIVE_BL0 obs_module_text("NumRefActiveBL0")
#define TEXT_NUM_REF_ACTIVE_BL1 obs_module_text("NumRefActiveBL1")
#define TEXT_LA_DS obs_module_text("LookaheadDownSampling")
#define TEXT_GLOBAL_MOTION_BIAS_ADJUSTMENT obs_module_text("GlobalMotionBiasAdjustment")
#define TEXT_DIRECT_BIAS_ADJUSTMENT obs_module_text("DirectBiasAdjusment")
#define TEXT_ADAPTIVE_I obs_module_text("AdaptiveI")
#define TEXT_ADAPTIVE_B obs_module_text("AdaptiveB")
#define TEXT_ADAPTIVE_REF obs_module_text("AdaptiveRef")
#define TEXT_ADAPTIVE_CQM obs_module_text("AdaptiveCQM")
#define TEXT_P_PYRAMID obs_module_text("PPyramid")
#define TEXT_TRELLIS obs_module_text("Trellis")
#define TEXT_LA obs_module_text("Lookahead_Mode")
#define TEXT_LA_DEPTH obs_module_text("LookaheadDepth")
#define TEXT_LA_LATENCY obs_module_text("Lookahead_Latency")
#define TEXT_MV_OVER_PIC_BOUNDARIES obs_module_text("MotionVectorsOverpicBoundaries")
#define TEXT_USE_RAW_REF obs_module_text("UseRawRef")
#define TEXT_MV_COST_SCALING_FACTOR obs_module_text("MVCostScalingFactor")
#define TEXT_RDO obs_module_text("RDO")
#define TEXT_HRD_CONFORMANCE obs_module_text("HRDConformance")
#define TEXT_LOW_DELAY_BRC obs_module_text("LowDelayBRC")
#define TEXT_LOW_DELAY_HRD obs_module_text("LowDelayHRD")
#define TEXT_ASYNC_DEPTH obs_module_text("AsyncDepth")
#define TEXT_WINBRC obs_module_text("WinBRC")
#define TEXT_WINBRC_MAX_AVG_SIZE obs_module_text("WinBRCMaxAvgSize")
#define TEXT_WINBRC_SIZE obs_module_text("WinBRCSize")
#define TEXT_ADAPTIVE_LTR obs_module_text("AdaptiveLTR")
#define TEXT_HEVC_SAO obs_module_text("SampleAdaptiveOffset")
#define TEXT_HEVC_GPB obs_module_text("GPB")
#define TEXT_TUNE_QUALITY_MODE obs_module_text("TuneQualityMode")
#define TEXT_EXT_BRC obs_module_text("ExtBRC")
#define TEXT_ENC_TOOLS obs_module_text("EncTools")
#define TEXT_LOW_POWER obs_module_text("LowPower_Mode")

#define TEXT_SCENARIO_INFO obs_module_text("ScenarioInfo")
#define TEXT_CONTENT_INFO obs_module_text("ContentInfo")
#define TEXT_TRANSFORM_SKIP obs_module_text("TransformSkip")
#define TEXT_FADE_DETECTION obs_module_text("FadeDetection")
#define TEXT_BITRATE_LIMIT obs_module_text("BitrateLimit")

#define TEXT_VPP obs_module_text("VideoProcessing_Filters")
#define TEXT_VPP_MODE obs_module_text("VideoProcessing_Mode")
#define TEXT_DENOISE_STRENGTH obs_module_text("Denoise_Strength")
#define TEXT_DENOISE_MODE obs_module_text("Denoise_Mode")
#define TEXT_SCALING_MODE obs_module_text("Scaling_Mode")
#define TEXT_IMAGE_STAB_MODE obs_module_text("ImageStab_Mode")
#define TEXT_DETAIL obs_module_text("Detail_Enhancement")
#define TEXT_DETAIL_FACTOR obs_module_text("Detail_Factor")
#define TEXT_PERC_ENC_PREFILTER obs_module_text("PercEncPrefilter")

#define TEXT_INTRA_REF_ENCODING obs_module_text("IntraRefEncoding")
#define TEXT_INTRA_REF_CYCLE_SIZE obs_module_text("IntraRefCycleSize")
#define TEXT_INTRA_REF_QP_DELTA obs_module_text("IntraRefQPDelta")

#define TEXT_GPU_NUMBER obs_module_text("Select_GPU")

#define TEXT_SCREEN_CONTENT_TOOLS obs_module_text("ScreenContentTools")

#define TEXT_TEMPORAL_LAYERS obs_module_text("TemporalLayers")

#define TEXT_SEPARATE_IPB_QP obs_module_text("SeparateIPBQP")
#define TEXT_CQP obs_module_text("CQP")
#define TEXT_QPI obs_module_text("QPI")
#define TEXT_QPP obs_module_text("QPP")
#define TEXT_QPB obs_module_text("QPB")

#define TEXT_ENC_TOOLS_DESC obs_module_text("EncTools.Tooltip")
#define TEXT_TUNE_QUALITY_DESC obs_module_text("TuneQuality.Tooltip")
#define TEXT_HEVC_GPB_DESC obs_module_text("HEVCGPB.Tooltip")
#define TEXT_B_FRAMES_DESC obs_module_text("BFramesDesc")
#define TEXT_HRD_CONFORMANCE_DESC obs_module_text("HRDConformanceDesc")
#define TEXT_MBBRC_DESC obs_module_text("MBBRCDesc")
#define TEXT_RDO_DESC obs_module_text("RDODesc")
#define TEXT_ADAPTIVE_I_DESC obs_module_text("AdaptiveIDesc")
#define TEXT_ADAPTIVE_B_DESC obs_module_text("AdaptiveBDesc")
#define TEXT_ADAPTIVE_REF_DESC obs_module_text("AdaptiveRefDesc")
#define TEXT_ADAPTIVE_LTR_DESC obs_module_text("AdaptiveLTRDesc")
#define TEXT_ADAPTIVE_CQM_DESC obs_module_text("AdaptiveCQMDesc")
#define TEXT_P_PYRAMID_DESC obs_module_text("PPyramidDesc")
#define TEXT_USE_RAW_REF_DESC obs_module_text("UseRawRefDesc")
#define TEXT_GLOBAL_MOTION_BIAS_DESC obs_module_text("GlobalMotionBiasDesc")
#define TEXT_DIRECT_BIAS_DESC obs_module_text("DirectBiasDesc")
#define TEXT_MV_OVER_PIC_BOUNDARIES_DESC obs_module_text("MVOverpicBoundariesDesc")
#define TEXT_TRELLIS_DESC obs_module_text("TrellisDesc")
#define TEXT_LA_DS_DESC obs_module_text("LookaheadDSDesc")
#define TEXT_VPP_DESC obs_module_text("VPPDesc")
#define TEXT_LOW_POWER_DESC obs_module_text("LowPowerDesc")
#define TEXT_INTRA_REF_CYCLE_SIZE_DESC obs_module_text("IntraRefCycleSizeDesc")
#define TEXT_INTRA_REF_QP_DELTA_DESC obs_module_text("IntraRefQPDeltaDesc")
#define TEXT_NUM_REF_ACTIVE_P_DESC obs_module_text("NumRefActivePDesc")
#define TEXT_NUM_REF_ACTIVE_BL0_DESC obs_module_text("NumRefActiveBL0Desc")
#define TEXT_NUM_REF_ACTIVE_BL1_DESC obs_module_text("NumRefActiveBL1Desc")
#define TEXT_SCENARIO_INFO_DESC obs_module_text("ScenarioInfoDesc")
#define TEXT_CONTENT_INFO_DESC obs_module_text("ContentInfoDesc")
#define TEXT_TRANSFORM_SKIP_DESC obs_module_text("TransformSkipDesc")
#define TEXT_FADE_DETECTION_DESC obs_module_text("FadeDetectionDesc")
#define TEXT_BITRATE_LIMIT_DESC obs_module_text("BitrateLimitDesc")

#define TEXT_EXT_BRC_DESC obs_module_text("ExtBRCDesc")
#define TEXT_GPU_NUMBER_DESC obs_module_text("GPUNumberDesc")

#define TEXT_AV1_CDEF obs_module_text("AV1CDEF")
#define TEXT_AV1_RESTORATION obs_module_text("AV1Restoration")
#define TEXT_AV1_LOOP_FILTER obs_module_text("AV1LoopFilter")
#define TEXT_AV1_SUPER_RES obs_module_text("AV1SuperRes")
#define TEXT_AV1_INTERP_FILTER obs_module_text("AV1InterpFilter")
#define TEXT_AV1_ERROR_RESILIENT obs_module_text("AV1ErrorResilient")
#define TEXT_AV1_CDEF_DESC obs_module_text("AV1CDEFDesc")
#define TEXT_AV1_RESTORATION_DESC obs_module_text("AV1RestorationDesc")
#define TEXT_AV1_LOOP_FILTER_DESC obs_module_text("AV1LoopFilterDesc")
#define TEXT_AV1_SUPER_RES_DESC obs_module_text("AV1SuperResDesc")
#define TEXT_AV1_INTERP_FILTER_DESC obs_module_text("AV1InterpFilterDesc")
#define TEXT_AV1_ERROR_RESILIENT_DESC obs_module_text("AV1ErrorResilientDesc")

#define TEXT_VPP_MCTF obs_module_text("VPPMCTF")
#define TEXT_VPP_MCTF_STRENGTH obs_module_text("VPPMCTFStrength")
#define TEXT_VPP_MCTF_DESC obs_module_text("VPPMCTFDesc")
#define TEXT_VPP_MCTF_STRENGTH_DESC obs_module_text("VPPMCTFStrengthDesc")

#define TEXT_WEIGHTED_PRED obs_module_text("WeightedPred")
#define TEXT_WEIGHTED_BI_PRED obs_module_text("WeightedBiPred")
#define TEXT_WEIGHTED_PRED_DESC obs_module_text("WeightedPredDesc")
#define TEXT_WEIGHTED_BI_PRED_DESC obs_module_text("WeightedBiPredDesc")

#define TEXT_ADAPTIVE_MAX_FRAME_SIZE obs_module_text("AdaptiveMaxFrameSize")
#define TEXT_ADAPTIVE_MAX_FRAME_SIZE_DESC obs_module_text("AdaptiveMaxFrameSizeDesc")

#define TEXT_CTU obs_module_text("CTU")
#define TEXT_CTU_DESC obs_module_text("CTUDesc")

static const char *const qsv_profile_names_av1[] = {"main", "high", "pro", 0};

static const char *const qsv_profile_names_h264[] = {
    "high10", "high", "main", "baseline", "extended", "high422",
    "constrained_baseline", "constrained_high", 0};

static const char *const qsv_profile_names_hevc[] = {"main", "main10", "mainsp", "rext", "scc", 0};

static const char *const qsv_profile_tiers_hevc[] = {"main", "high", 0};

static const char *const qsv_levels_hevc[] = {
    "auto", "1", "2", "2.1", "3", "3.1", "4", "4.1",
    "5", "5.1", "5.2", "6", "6.1", "6.2", 0};

static const char *const qsv_levels_avc[] = {
    "auto", "1", "1b", "1.1", "1.2", "1.3", "2", "2.1", "2.2",
    "3", "3.1", "3.2", "4", "4.1", "4.2", "5", "5.1", "5.2",
    "6", "6.1", "6.2", 0};

static const char *const qsv_levels_av1[] = {
    "auto", "2.0", "2.1", "2.2", "2.3", "3.0", "3.1", "3.2", "3.3",
    "4.0", "4.1", "4.2", "4.3", "5.0", "5.1", "5.2", "5.3",
    "6.0", "6.1", "6.2", "6.3", 0};

static const char *const qsv_usage_names[] = {
    "TU1 (Veryslow)", "TU2 (Slower)", "TU3 (Slow)",     "TU4 (Balanced)",
    "TU5 (Fast)",     "TU6 (Faster)", "TU7 (Veryfast)", 0};

static const char *const qsv_latency_names[] = {"ultra-low", "low", "normal",
                                                0};

static const char *const qsv_params_condition[] = {"ON", "OFF", 0};

static const char *const qsv_params_condition_tristate[] = {"ON", "OFF", "AUTO",
                                                            0};

static const char *const qsv_params_condition_p_pyramid[] = {"SIMPLE",
                                                             "PYRAMID", 0};

static const char *const qsv_params_condition_vpp[] = {"PRE ENC", "POST ENC",
                                                       "PRE ENC | POST ENC", 0};

static const char *const qsv_params_condition_scaling_mode[] = {
    "OFF",
    "QUALITY | ADVANCED",
    "VEBOX | ADVANCED",
    "LOWPOWER | NEAREST NEIGHBOR",
    "LOWPOWER | ADVANCED",
    "AUTO",
    0};

static const char *const qsv_params_condition_image_stab_mode[] = {
    "OFF", "UPSCALE", "BOXING", "AUTO", 0};

static const char *const qsv_params_condition_extbrc[] = {"ON", "OFF", 0};

static const char *const qsv_params_condition_screen_content_tools[] = {
    "AUTO", "OFF", "ON", 0};

static const char *const qsv_params_condition_intra_ref_encoding[] = {
    "VERTICAL", "HORIZONTAL", 0};

static const char *const qsv_params_condition_mv_cost_scaling[] = {
    "DEFAULT", "1/2", "1/4", "1/8", "AUTO", 0};

static const char *const qsv_params_condition_lookahead_mode[] = {"HQ", "LP",
                                                                  "OFF", 0};

static const char *const qsv_params_condition_lookahead_latency[] = {
    "NORMAL", "HIGH", "LOW", "VERYLOW", 0};

static const char *const qsv_params_condition_lookahead_ds[] = {
    "SLOW", "MEDIUM", "FAST", "AUTO", 0};

static const char *const qsv_params_condition_trellis[] = {
    "OFF", "I", "IP", "IPB", "IB", "P", "PB", "B", "AUTO", 0};

static const char *const qsv_params_condition_hevc_sao[] = {
    "AUTO", "DISABLE", "LUMA", "CHROMA", "ALL", 0};

static const char *const qsv_params_condition_scenario_info[] = {
    "OFF", "AUTO", "ARCHIVE", "LIVE", "REMOTE_GAMING", "GAME_STREAMING", 0};

static const char *const qsv_params_condition_content_info[] = {
    "OFF", "AUTO", "NOISY_VIDEO", "GAME", "CAMERA_SCENE",
    "CLEAN_CAMERA_SCENE", "ANIMATED_GRAPHICS", "COMPUTER_DISPLAY",
    "PROGRESSIVE_VIDEO", "STILL_IMAGE", "VIDEO_CONFERENCE", 0};

static const char *const qsv_params_condition_tune_quality[] = {
    "DEFAULT", "PSNR", "SSIM", "MS SSIM", "VMAF", "PERCEPTUAL", "OFF", 0};

static const char *const qsv_params_condition_denoise_mode[] = {
    "DEFAULT",
    "AUTO | BDRATE | PRE ENCODE",
    "AUTO | ADJUST | POST ENCODE",
    "AUTO | SUBJECTIVE | PRE ENCODE",
    "MANUAL | PRE ENCODE",
    "MANUAL | POST ENCODE",
    "OFF",
    0};

static const char *const qsv_params_condition_av1_interp_filter[] = {
    "DEFAULT", "EIGHTTAP", "EIGHTTAP_SMOOTH", "EIGHTTAP_SHARP", "BILINEAR",
    "SWITCHABLE", 0};

static const char *const qsv_params_condition_ctu_size[] = {
    "AUTO", "16", "32", "64", "128", 0};

static void SetDefaultEncoderParams(obs_data_t *, enum codec_enum);

static bool ParamsVisibilityModifier(obs_properties_t *, obs_property_t *,
                             obs_data_t *);

static obs_properties_t *GetParamProps(enum codec_enum Codec);

static void GetEncoderParams(plugin_context *Context, obs_data_t *Settings);

static obs_properties_t *GetH264ParamProps(void *);

static obs_properties_t *GetAV1ParamProps(void *);

static obs_properties_t *GetHEVCParamProps(void *);

plugin_context *InitPluginContext(enum codec_enum Codec, obs_data_t *Settings,
                            obs_encoder_t *EncoderData, bool IsTextureEncoder);

static void *InitTextureEncoder(enum codec_enum Codec, obs_data_t *Settings,
                                obs_encoder_t *EncoderData,
                                const char *FallbackID);

static void *InitH264FrameEncoder(obs_data_t *Settings,
                                  obs_encoder_t *EncoderData);

static void *InitAV1FrameEncoder(obs_data_t *Settings,
                                 obs_encoder_t *EncoderData);

static void *InitHEVCFrameEncoder(obs_data_t *Settings,
                                  obs_encoder_t *EncoderData);

static void *InitH264TextureEncoder(obs_data_t *Settings,
                                    obs_encoder_t *EncoderData);

static void *InitAV1TextureEncoder(obs_data_t *Settings,
                                   obs_encoder_t *EncoderData);

static void *InitHEVCTextureEncoder(obs_data_t *Settings,
                                    obs_encoder_t *EncoderData);

static const char *GetH264EncoderName(void *);

static const char *GetAV1EncoderName(void *);

static const char *GetHEVCEncoderName(void *);

static void SetH264DefaultParams(obs_data_t *Settings);

static void SetAV1DefaultParams(obs_data_t *Settings);

static void SetHEVCDefaultParams(obs_data_t *Settings);
