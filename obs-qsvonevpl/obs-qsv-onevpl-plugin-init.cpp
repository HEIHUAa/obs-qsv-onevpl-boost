

#pragma warning(disable : 4996)

#include <algorithm>
#include <string_view>

#include "obs-qsv-onevpl-encoder.hpp"

// Extern array definitions (declared in obs-qsv-onevpl-plugin-init.hpp)
const char *const qsv_profile_names_av1[] = {"main", "high", "pro", 0};
// VP9 profiles: 0=8bit420, 1=8bit444, 2=10bit420, 3=10bit444
const char *const qsv_profile_names_vp9[] = {
    "0 (8-bit 4:2:0)", "1 (8-bit 4:4:4)",
    "2 (10-bit 4:2:0)", "3 (10-bit 4:4:4)", 0};
const char *const qsv_profile_names_h264[] = {
    "high10", "high", "main", "baseline", "extended",
    "constrained_baseline", "constrained_high", 0};
const char *const qsv_profile_names_hevc[] = {"main", "main10", "mainsp", "rext", "scc", 0};
const char *const qsv_profile_tiers_hevc[] = {"main", "high", 0};
const char *const qsv_levels_hevc[] = {
    "auto", "1", "2", "2.1", "3", "3.1", "4", "4.1",
    "5", "5.1", "5.2", "6", "6.1", "6.2", "8.5", 0};
const char *const qsv_levels_avc[] = {
    "auto", "1", "1b", "1.1", "1.2", "1.3", "2", "2.1", "2.2",
    "3", "3.1", "3.2", "4", "4.1", "4.2", "5", "5.1", "5.2",
    "6", "6.1", "6.2", 0};
const char *const qsv_levels_av1[] = {
    "auto", "2.0", "2.1", "2.2", "2.3", "3.0", "3.1", "3.2", "3.3",
    "4.0", "4.1", "4.2", "4.3", "5.0", "5.1", "5.2", "5.3",
    "6.0", "6.1", "6.2", "6.3", "7.0", "7.1", "7.2", "7.3", 0};
const char *const qsv_usage_names[] = {
    "TU1 (Veryslow)", "TU2 (Slower)", "TU3 (Slow)",     "TU4 (Balanced)",
    "TU5 (Fast)",     "TU6 (Faster)", "TU7 (Veryfast)", 0};
const char *const qsv_latency_names[] = {"ultra-low", "low", "normal", 0};
const char *const qsv_params_condition[] = {"ON", "OFF", 0};
const char *const qsv_params_condition_tristate[] = {"ON", "OFF", "AUTO", 0};
const char *const qsv_params_weighted_pred_options[] = {"AUTO", "OFF",
    "DEFAULT", "EXPLICIT", "IMPLICIT", 0};
const char *const qsv_params_condition_scaling_mode[] = {
    "OFF", "QUALITY | ADVANCED", "VEBOX | ADVANCED",
    "LOWPOWER | NEAREST NEIGHBOR", "LOWPOWER | ADVANCED", "AUTO", 0};
const char *const qsv_params_condition_image_stab_mode[] = {
    "OFF", "UPSCALE", "BOXING", "AUTO", 0};
const char *const qsv_params_condition_screen_content_tools[] = {
    "AUTO", "OFF", "ON", 0};
const char *const qsv_params_condition_intra_ref_encoding[] = {
    "VERTICAL", "HORIZONTAL", 0};
const char *const qsv_params_condition_mv_cost_scaling[] = {
    "DEFAULT", "1/2", "1/4", "1/8", "AUTO", 0};
const char *const qsv_params_condition_lookahead_mode[] = {"HQ", "LP", "OFF", 0};
const char *const qsv_params_condition_lookahead_ds[] = {
    "1X", "2X", "4X", "AUTO", 0};
const char *const qsv_params_condition_trellis[] = {
    "OFF", "I", "IP", "IPB", "IB", "P", "PB", "B", "AUTO", 0};
const char *const qsv_params_condition_hevc_sao[] = {
    "AUTO", "DISABLE", "LUMA", "CHROMA", "ALL", 0};
const char *const qsv_params_condition_scenario_info[] = {
    "OFF", "AUTO", "DISPLAY_REMOTING", "VIDEO_CONFERENCE", "ARCHIVE",
    "LIVE_STREAMING", "CAMERA_CAPTURE", "VIDEO_SURVEILLANCE",
    "GAME_STREAMING", "REMOTE_GAMING", 0};
const char *const qsv_params_condition_content_info[] = {
    "OFF", "AUTO", "FULL_SCREEN_VIDEO", "NON_VIDEO_SCREEN", "NOISY_VIDEO", 0};
const char *const qsv_params_condition_denoise_mode[] = {
    "DEFAULT", "AUTO | BDRATE | PRE ENCODE", "AUTO | ADJUST | POST ENCODE",
    "AUTO | SUBJECTIVE | PRE ENCODE", "MANUAL | PRE ENCODE",
    "MANUAL | POST ENCODE", "OFF", 0};
const char *const qsv_params_condition_av1_interp_filter[] = {
    "DEFAULT", "EIGHTTAP", "EIGHTTAP_SMOOTH", "EIGHTTAP_SHARP", "BILINEAR",
    "SWITCHABLE", 0};

struct qsv_rate_control_info {
    const char *name;
    mfxU16 min_platform;
};

static const struct qsv_rate_control_info qsv_rate_control_info_list[] = {
    {"CBR", 0},
    {"VBR", 0},
    {"CQP", 0},
    {"AVBR", MFX_PLATFORM_HASWELL},
    {"ICQ", MFX_PLATFORM_HASWELL},
    {"VCM", MFX_PLATFORM_SKYLAKE},
    {"QVBR", MFX_PLATFORM_HASWELL},
    {nullptr, 0}};

struct qsv_feature_info {
    const char *property_name;
    mfxU16 min_platform;
};

static const struct qsv_feature_info qsv_feature_info_list[] = {
    {"enc_tools", MFX_PLATFORM_TIGERLAKE},
    {"transform_skip", MFX_PLATFORM_ICELAKE},
    {nullptr, 0}};

mfxU16 QueryPlatformCodeName();

static bool IsFeatureSupported(const char *PropertyName) {
    mfxU16 platformCode = QueryPlatformCodeName();
    if (platformCode == 0) {
        return true;
    }
    const std::string_view prop{PropertyName};
    const auto it = std::ranges::find_if(
        qsv_feature_info_list,
        [prop](const qsv_feature_info &info) {
            return info.property_name && prop == info.property_name;
        });
    if (it != std::end(qsv_feature_info_list)) {
        return platformCode >= it->min_platform;
    }
    return true;
}

static mfxPlatform CachedQSVPlatform{};
static bool CachedQSVPlatformValid = false;
static std::once_flag QueryPlatformOnceFlag;

static bool TryQueryPlatformCodeName(mfxLoader Loader) {
    mfxConfig Config = MFXCreateConfig(Loader);
    mfxVariant Variant{};
    Variant.Type = MFX_VARIANT_TYPE_U32;
    Variant.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
    MFXSetConfigFilterProperty(
        Config,
        reinterpret_cast<const mfxU8 *>("mfxImplDescription.Impl.mfxImplType"),
        Variant);

    Config = MFXCreateConfig(Loader);
    Variant.Type = MFX_VARIANT_TYPE_U32;
    Variant.Data.U32 = static_cast<mfxU32>(0x8086);
    MFXSetConfigFilterProperty(
        Config,
        reinterpret_cast<const mfxU8 *>("mfxImplDescription.VendorID"),
        Variant);

    mfxSession Session{};
    mfxStatus Status = MFXCreateSession(Loader, 0, &Session);
    if (Status >= MFX_ERR_NONE) {
        mfxPlatform platform{};
        mfxStatus qStatus = MFXVideoCORE_QueryPlatform(Session, &platform);
        MFXClose(Session);
        if (qStatus >= MFX_ERR_NONE) {
            CachedQSVPlatform = platform;
            CachedQSVPlatformValid = true;
            return true;
        }
        return false;
    }
    return false;
}

mfxU16 QueryPlatformCodeName() {
    std::call_once(QueryPlatformOnceFlag, []() {
        mfxLoader GlobalLoader = nullptr;
        {
            std::lock_guard<std::mutex> Lock(GlobalLoaderMutex);
            GlobalLoader = GlobalQSVLoader;
        }

        if (GlobalLoader != nullptr) {
            if (TryQueryPlatformCodeName(GlobalLoader)) {
                return;
            }
        } else {
            mfxLoader Loader = MFXLoad();
            if (Loader != nullptr) {
                bool ok = TryQueryPlatformCodeName(Loader);
                MFXUnload(Loader);
                if (ok) {
                    return;
                }
            }
        }
    });

    if (CachedQSVPlatformValid) {
        return CachedQSVPlatform.CodeName;
    }
    return 0;
}

static void SetDefaultEncoderParams(obs_data_t *Settings,
                                    enum codec_enum Codec) {
  obs_data_set_default_string(Settings, "target_usage", "TU4 (Balanced)");
  obs_data_set_default_int(Settings, "bitrate", 6000);
  obs_data_set_default_int(Settings, "max_bitrate", 6000);
  obs_data_set_default_bool(Settings, "custom_buffer_size", false);
  obs_data_set_default_int(Settings, "buffer_size", 0);
  obs_data_set_default_string(
      Settings, "profile",
      Codec == QSV_CODEC_AVC ? "high"
      : Codec == QSV_CODEC_VP9 ? "0 (8-bit 4:2:0)"
                               : "main");
  obs_data_set_default_string(Settings, "hevc_tier", "main");
  obs_data_set_default_string(Settings, "hevc_level", "auto");
  obs_data_set_default_string(Settings, "avc_level", "auto");
  obs_data_set_default_string(Settings, "av1_level", "auto");
  obs_data_set_default_string(Settings, "rate_control", "CBR");

  obs_data_set_default_int(Settings, "cqp", 23);
  obs_data_set_default_bool(Settings, "cqp_separate_ipb", false);
  obs_data_set_default_int(Settings, "qpi", 23);
  obs_data_set_default_int(Settings, "qpp", 23);
  obs_data_set_default_int(Settings, "qpb", 23);
  obs_data_set_default_int(Settings, "icq_quality", 23);

  obs_data_set_default_int(Settings, "keyint_sec", 2);
  obs_data_set_default_int(Settings, "b_frames", 3);
  obs_data_set_default_int(Settings, "async_depth", 4);

  obs_data_set_default_string(Settings, "intra_ref_encoding", "OFF");
  obs_data_set_default_string(Settings, "low_delay_brc", "OFF");
  obs_data_set_default_string(Settings, "low_delay_hrd", "OFF");

  obs_data_set_default_string(Settings, "adaptive_i", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_b", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_ref", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_cqm", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_ltr", "AUTO");
  obs_data_set_default_string(Settings, "use_raw_ref", "AUTO");
  obs_data_set_default_string(Settings, "rdo", "AUTO");
  obs_data_set_default_string(Settings, "hrd_conformance", "AUTO");
  obs_data_set_default_string(Settings, "mbbrc", "AUTO");
  obs_data_set_default_string(Settings, "trellis", "AUTO");
  obs_data_set_default_int(Settings, "num_ref_frame", 0);
  obs_data_set_default_string(Settings, "global_motion_bias_adjustment",
                              "AUTO");
  obs_data_set_default_string(Settings, "mv_cost_scaling_factor", "AUTO");
  obs_data_set_default_string(Settings, "direct_bias_adjustment", "AUTO");
  obs_data_set_default_string(Settings, "mv_overpic_boundaries", "AUTO");
  obs_data_set_default_int(Settings, "la_depth", 60);

  obs_data_set_default_int(Settings, "qvbr_quality", 0);

  obs_data_set_default_string(Settings, "lookahead", "OFF");
  obs_data_set_default_string(Settings, "lookahead_ds", "AUTO");
  obs_data_set_default_string(Settings, "enctools", "OFF");
  obs_data_set_default_string(Settings, "enc_tools_scene_change", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_ref_p", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_ref_b", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_pyramid_quant_p", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_pyramid_quant_b", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_mbqp", "ON");
  obs_data_set_default_string(Settings, "enc_tools_brc_buffer_hints", "ON");
  obs_data_set_default_string(Settings, "enc_tools_brc", "ON");
  obs_data_set_default_string(Settings, "enc_tools_saliency_map_hint", "ON");
  obs_data_set_default_string(Settings, "hevc_sao", "AUTO");
  obs_data_set_default_string(Settings, "hevc_gpb", "AUTO");

  obs_data_set_default_string(Settings, "intra_ref_type", "VERTICAL");
  obs_data_set_default_int(Settings, "intra_ref_cycle_size", 2);
  obs_data_set_default_int(Settings, "intra_ref_qp_delta", 0);

  obs_data_set_default_string(Settings, "vpp", "OFF");
  obs_data_set_default_string(Settings, "denoise_mode", "OFF");
  obs_data_set_default_int(Settings, "denoise_strength", 50);
  obs_data_set_default_string(Settings, "detail", "OFF");
  obs_data_set_default_int(Settings, "detail_factor", 50);
  obs_data_set_default_string(Settings, "image_stab_mode", "OFF");
  obs_data_set_default_string(Settings, "scaling_mode", "OFF");
  obs_data_set_default_int(Settings, "vpp_out_width", 0);
  obs_data_set_default_int(Settings, "vpp_out_height", 0);
  obs_data_set_default_string(Settings, "perc_enc_prefilter", "OFF");

  obs_data_set_default_string(Settings, "scenario_info", "AUTO");
  obs_data_set_default_string(Settings, "content_info", "AUTO");
  obs_data_set_default_string(Settings, "transform_skip", "AUTO");
  obs_data_set_default_string(Settings, "fade_detection", "AUTO");

  obs_data_set_default_string(Settings, "screen_content_tools", "AUTO");

  obs_data_set_default_int(Settings, "temporal_layers", 0);

  obs_data_set_default_int(Settings, "gpu_number", 0);

  obs_data_set_default_string(Settings, "min_qp", "-1");
  obs_data_set_default_string(Settings, "max_qp", "-1");

  obs_data_set_default_string(Settings, "weighted_pred", "AUTO");
  obs_data_set_default_string(Settings, "weighted_bi_pred", "AUTO");

  // AV1 coding options default to AUTO (driver decides)
  obs_data_set_default_string(Settings, "av1_cdef", "AUTO");
  obs_data_set_default_string(Settings, "av1_restoration", "AUTO");
  obs_data_set_default_string(Settings, "av1_loop_filter", "AUTO");
  obs_data_set_default_string(Settings, "av1_super_res", "AUTO");
  obs_data_set_default_string(Settings, "av1_error_resilient", "AUTO");
  obs_data_set_default_string(Settings, "av1_interp_filter", "DEFAULT");
}

static inline const char *LocaleKey(const char *str) {
  static thread_local char buf[128];
  size_t i;
  if (strcmp(str, "AUTO") == 0)
    return "AUTO_";
  for (i = 0; str[i] && i < sizeof(buf) - 1; i++) {
    char c = str[i];
    if (c == ' ' || c == '|' || c == '(' || c == ')' || c == '/')
      buf[i] = '_';
    else
      buf[i] = c;
  }
  buf[i] = '\0';
  return buf;
}

static inline void AddStrings(obs_property_t *List,
                              const char *const *Strings) {
  while (*Strings) {
    obs_property_list_add_string(List, obs_module_text(LocaleKey(*Strings)),
                                *Strings);
    Strings++;
  }
}

static bool ParamsVisibilityModifier(obs_properties_t *Properties,
                                     obs_property_t *Prop,
                                     obs_data_t *Settings) {
  const char *rate_control = obs_data_get_string(Settings, "rate_control");

  bool bIsCBR = std::strcmp(rate_control, "CBR") == 0;
  bool bIsVBR = std::strcmp(rate_control, "VBR") == 0;
  bool bIsAVBR = std::strcmp(rate_control, "AVBR") == 0;
  bool bIsCQP = std::strcmp(rate_control, "CQP") == 0;
  bool bIsICQ = std::strcmp(rate_control, "ICQ") == 0;
  bool bIsVCM = std::strcmp(rate_control, "VCM") == 0;
  bool bIsQVBR = std::strcmp(rate_control, "QVBR") == 0;

  bool bVisible = bIsVBR || bIsVCM;
  Prop = obs_properties_get(Properties, "max_bitrate");
  obs_property_set_visible(Prop, bVisible);

  bVisible = bIsCQP || bIsICQ;
  Prop = obs_properties_get(Properties, "bitrate");
  obs_property_set_visible(Prop, !bVisible);

  bVisible = bIsCQP;
  Prop = obs_properties_get(Properties, "cqp_separate_ipb");
  if (Prop)
    obs_property_set_visible(Prop, bVisible);

  bool separateIPB = obs_data_get_bool(Settings, "cqp_separate_ipb");
  Prop = obs_properties_get(Properties, "qpi");
  if (Prop)
    obs_property_set_visible(Prop, bVisible && separateIPB);
  Prop = obs_properties_get(Properties, "qpb");
  if (Prop)
    obs_property_set_visible(Prop, bVisible && separateIPB);
  Prop = obs_properties_get(Properties, "qpp");
  if (Prop)
    obs_property_set_visible(Prop, bVisible && separateIPB);
  Prop = obs_properties_get(Properties, "cqp");
  if (Prop)
    obs_property_set_visible(Prop, bVisible && !separateIPB);

  bVisible = bIsICQ;
  Prop = obs_properties_get(Properties, "icq_quality");
  obs_property_set_visible(Prop, bVisible);

  // EncTools visibility: CBR/VBR/AVBR/VCM/QVBR modes with feature support
  bool bEncToolsVisible = (bIsCBR || bIsVBR || bIsAVBR || bIsVCM || bIsQVBR);
  Prop = obs_properties_get(Properties, "enctools");
  bVisible = bEncToolsVisible;
  if (bVisible) bVisible = IsFeatureSupported("enc_tools");
  obs_property_set_visible(Prop, bVisible);

  const char *enctools = obs_data_get_string(Settings, "enctools");
  bool bVisibleEnctools = (std::strcmp(enctools, "ON") == 0) && bVisible;

  // EncTools sub-options visibility (only when enc_tools is ON)
  const char *enc_tools_sub_opts[] = {
    "enc_tools_scene_change", "enc_tools_adaptive_ref_p", "enc_tools_adaptive_ref_b",
    "enc_tools_adaptive_pyramid_quant_p", "enc_tools_adaptive_pyramid_quant_b",
    "enc_tools_adaptive_mbqp", "enc_tools_brc_buffer_hints", "enc_tools_brc",
    "enc_tools_saliency_map_hint", nullptr
  };
  for (const char **opt = enc_tools_sub_opts; *opt; opt++) {
    Prop = obs_properties_get(Properties, *opt);
    if (Prop) obs_property_set_visible(Prop, bVisibleEnctools);
  }

  bVisible = bIsQVBR;
  Prop = obs_properties_get(Properties, "qvbr_quality");
  obs_property_set_visible(Prop, bVisible);

  const char *lookahead = obs_data_get_string(Settings, "lookahead");

  bVisible = bIsCBR || bIsVBR || bIsAVBR || bIsQVBR || bIsICQ;
  Prop = obs_properties_get(Properties, "lookahead");
  obs_property_set_visible(Prop, bVisible);

  bool bVisible_lookahead_hq = std::strcmp(lookahead, "HQ") == 0;
  bool bVisible_lookahead_lp = std::strcmp(lookahead, "LP") == 0;

  Prop = obs_properties_get(Properties, "lookahead_ds");
  obs_property_set_visible(Prop, bVisible && bVisible_lookahead_hq);

  Prop = obs_properties_get(Properties, "la_depth");
  obs_property_set_visible(Prop, bVisible && bVisible_lookahead_hq);

  if (bVisible_lookahead_lp) {
    obs_data_set_string(Settings, "enctools", "OFF");
  }

  bVisible = bIsCBR || bIsVBR || bIsAVBR || bIsVCM || bIsQVBR || bIsICQ;
  // "mbbrc" control is not created for VP9 (codec disables MBBRC), so guard
  // against nullptr here.
  Prop = obs_properties_get(Properties, "mbbrc");
  if (Prop) {
    obs_property_set_visible(Prop, bVisible);
    if (!bVisible) {
      obs_data_set_string(Settings, "mbbrc", "OFF");
    }
  }

  bool bRateControlVisible = !bIsICQ && !bIsCQP;
  bool use_bufsize = obs_data_get_bool(Settings, "custom_buffer_size");
  Prop = obs_properties_get(Properties, "custom_buffer_size");
  obs_property_set_visible(Prop, bRateControlVisible);
  Prop = obs_properties_get(Properties, "buffer_size");
  obs_property_set_visible(Prop, bRateControlVisible && use_bufsize);
  if (!bRateControlVisible) {
    obs_data_set_bool(Settings, "custom_buffer_size", false);
  }

  const char *hrd_conformance =
      obs_data_get_string(Settings, "hrd_conformance");
  Prop = obs_properties_get(Properties, "hrd_conformance");
  obs_property_set_visible(Prop, bRateControlVisible);
  if (!bRateControlVisible) {
    obs_data_set_string(Settings, "hrd_conformance", "OFF");
  }
  bVisible = bRateControlVisible && (std::strcmp(hrd_conformance, "ON") == 0 ||
             std::strcmp(hrd_conformance, "AUTO") == 0);
  Prop = obs_properties_get(Properties, "low_delay_hrd");
  obs_property_set_visible(Prop, bVisible);

  bVisible = bIsVBR || bIsVCM || bIsQVBR;
  Prop = obs_properties_get(Properties, "low_delay_brc");
  obs_property_set_visible(Prop, bVisible);
  if (!bVisible) {
    obs_data_set_string(Settings, "low_delay_brc", "OFF");
  }

  bool bMaxFrameSizeVisible = !(bIsCQP || bIsICQ);
  Prop = obs_properties_get(Properties, "adaptive_max_frame_size");
  obs_property_set_visible(Prop, bMaxFrameSizeVisible);
  if (!bMaxFrameSizeVisible) {
    obs_data_set_int(Settings, "adaptive_max_frame_size", 0);
  }

  bVisible = !(bIsCQP || bIsICQ);
  Prop = obs_properties_get(Properties, "min_qp");
  obs_property_set_visible(Prop, bVisible);
  Prop = obs_properties_get(Properties, "max_qp");
  obs_property_set_visible(Prop, bVisible);

  const char *global_motion_bias_adjustment_enable =
      obs_data_get_string(Settings, "global_motion_bias_adjustment");
  bVisible = ((std::strcmp(global_motion_bias_adjustment_enable, "ON") == 0));
  Prop = obs_properties_get(Properties, "mv_cost_scaling_factor");
  obs_property_set_visible(Prop, bVisible);
  if (!bVisible) {
    obs_data_erase(Settings, "mv_cost_scaling_factor");
  }

  const char *vpp = obs_data_get_string(Settings, "vpp");
  bool bVisibleVPP = std::strcmp(vpp, "ON") == 0;
  Prop = obs_properties_get(Properties, "detail");
  obs_property_set_visible(Prop, bVisibleVPP);
  Prop = obs_properties_get(Properties, "image_stab_mode");
  obs_property_set_visible(Prop, bVisibleVPP);
  Prop = obs_properties_get(Properties, "perc_enc_prefilter");
  obs_property_set_visible(Prop, bVisibleVPP);
  Prop = obs_properties_get(Properties, "denoise_mode");
  obs_property_set_visible(Prop, bVisibleVPP);
  Prop = obs_properties_get(Properties, "scaling_mode");
  obs_property_set_visible(Prop, bVisibleVPP);
  const char *scaling_mode = obs_data_get_string(Settings, "scaling_mode");
  bool bScalingModeActive = std::strcmp(scaling_mode, "OFF") != 0;
  Prop = obs_properties_get(Properties, "vpp_out_width");
  obs_property_set_visible(Prop, bVisibleVPP && bScalingModeActive);
  Prop = obs_properties_get(Properties, "vpp_out_height");
  obs_property_set_visible(Prop, bVisibleVPP && bScalingModeActive);
  Prop = obs_properties_get(Properties, "vpp_mctf");
  obs_property_set_visible(Prop, bVisibleVPP);

  const char *vpp_mctf_val = obs_data_get_string(Settings, "vpp_mctf");
  bool vpp_mctf_strength_visible = bVisibleVPP && (std::strcmp(vpp_mctf_val, "ON") == 0);
  obs_property_set_visible(obs_properties_get(Properties, "vpp_mctf_strength"), vpp_mctf_strength_visible);

  const char *denoise_mode = obs_data_get_string(Settings, "denoise_mode");
  bVisible = std::strcmp(denoise_mode, "MANUAL | PRE ENCODE") == 0 ||
             std::strcmp(denoise_mode, "MANUAL | POST ENCODE") == 0;
  Prop = obs_properties_get(Properties, "denoise_strength");
  obs_property_set_visible(Prop, bVisible && bVisibleVPP);

  const char *detail = obs_data_get_string(Settings, "detail");
  bVisible = std::strcmp(detail, "ON") == 0;
  Prop = obs_properties_get(Properties, "detail_factor");
  obs_property_set_visible(Prop, bVisible && bVisibleVPP);

  const char *intra_ref_encoding =
      obs_data_get_string(Settings, "intra_ref_encoding");
  bVisible = std::strcmp(intra_ref_encoding, "ON") == 0;
  Prop = obs_properties_get(Properties, "intra_ref_type");
  obs_property_set_visible(Prop, bVisible);
  Prop = obs_properties_get(Properties, "intra_ref_cycle_size");
  obs_property_set_visible(Prop, bVisible);
  Prop = obs_properties_get(Properties, "intra_ref_qp_delta");
  obs_property_set_visible(Prop, bVisible);

  mfxU16 platformCode = QueryPlatformCodeName();
  // HEVC High Tier is supported on SKL+ (subject to level >= 4 spec constraint)
  bool hasHighTier = platformCode == 0 ||
                     platformCode >= MFX_PLATFORM_SKYLAKE;
  bool showTierList = hasHighTier;
  Prop = obs_properties_get(Properties, "hevc_tier");
  if (Prop) {
    obs_property_set_visible(Prop, showTierList);
    if (!showTierList) {
      obs_data_set_string(Settings, "hevc_tier", "main");
    }
  }

  return true;
}

static obs_properties_t *GetParamProps(enum codec_enum Codec) {

  obs_properties_t *Props = obs_properties_create();
  obs_property_t *Prop;
  mfxU16 platformCode = QueryPlatformCodeName();

  obs_properties_t *RCGroup = obs_properties_create();

  Prop = obs_properties_add_list(RCGroup, "target_usage", TEXT_SPEED,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_usage_names);
  obs_property_set_long_description(Prop, TEXT_TARGET_USAGE_DESC);

  Prop = obs_properties_add_list(RCGroup, "rate_control", TEXT_RATE_CONTROL,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(Prop, TEXT_RATE_CONTROL_DESC);
  {
    const struct qsv_rate_control_info *rcInfo = qsv_rate_control_info_list;
    while (rcInfo->name) {
      if (platformCode == 0 || platformCode >= rcInfo->min_platform) {
      // AV1/AVCM do not support VCM; HEVC encoder does not support AVBR;
      // VP9 only supports CBR/VBR/CQP/ICQ (no AVBR/QVBR/VCM)
      bool skipForAV1 = Codec == QSV_CODEC_AV1 &&
                        std::strcmp(rcInfo->name, "VCM") == 0;
      bool skipForHEVC = Codec == QSV_CODEC_HEVC &&
                         std::strcmp(rcInfo->name, "AVBR") == 0;
      bool skipForVP9 = Codec == QSV_CODEC_VP9 &&
                        (std::strcmp(rcInfo->name, "AVBR") == 0 ||
                         std::strcmp(rcInfo->name, "QVBR") == 0 ||
                         std::strcmp(rcInfo->name, "VCM") == 0);
      if (!skipForAV1 && !skipForHEVC && !skipForVP9) {
        obs_property_list_add_string(Prop, rcInfo->name, rcInfo->name);
      }
    }
      rcInfo++;
    }
  }
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int_slider(RCGroup, "qvbr_quality",
                                       TEXT_QVBR_QUALITY, 0, 51, 1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("QVBRQuality.Tooltip"));

  // VP9 ICQQuality internally uses 0-255 range (matches base_q_idx).
  // Expose 1-63 in UI (same as CQP QP) and scale x4 internally to 0-252.
  // Higher value = worse quality (same direction as H264/HEVC ICQ).
  {
    int icqMax = (Codec == QSV_CODEC_VP9) ? 63 : 51;
    Prop = obs_properties_add_int_slider(RCGroup, "icq_quality",
                                         TEXT_ICQ_QUALITY, 1, icqMax, 1);
    obs_property_set_long_description(Prop, TEXT_ICQ_QUALITY_DESC);
  }

  Prop = obs_properties_add_bool(RCGroup, "cqp_separate_ipb",
                                 TEXT_SEPARATE_IPB_QP);
  obs_property_set_long_description(Prop, TEXT_SEPARATE_IPB_QP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int_slider(RCGroup, "qpi", TEXT_QPI, 1,
                         (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) ? 63 : 51, 1);
  obs_property_set_long_description(Prop, TEXT_QP_DESC);
  Prop = obs_properties_add_int_slider(RCGroup, "qpp", TEXT_QPP, 1,
                         (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) ? 63 : 51, 1);
  obs_property_set_long_description(Prop, TEXT_QP_DESC);
  Prop = obs_properties_add_int_slider(RCGroup, "qpb", TEXT_QPB, 1,
                         (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) ? 63 : 51, 1);
  obs_property_set_long_description(Prop, TEXT_QP_DESC);

  Prop = obs_properties_add_int_slider(RCGroup, "cqp", TEXT_CQP, 1,
                         (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) ? 63 : 51, 1);
  obs_property_set_long_description(Prop, TEXT_CQP_DESC);

  Prop = obs_properties_add_int(RCGroup, "bitrate", TEXT_TARGET_BITRATE, 50,
                                6553500, 1000);
  obs_property_int_set_suffix(Prop, " Kbps");
  obs_property_set_long_description(Prop, TEXT_BITRATE_DESC);

  Prop = obs_properties_add_int(RCGroup, "max_bitrate", TEXT_MAX_BITRATE, 50,
                                6553500, 1000);
  obs_property_int_set_suffix(Prop, " Kbps");
  obs_property_set_long_description(Prop, TEXT_MAX_BITRATE_DESC);

  Prop = obs_properties_add_bool(RCGroup, "custom_buffer_size",
                                 TEXT_CUSTOM_BUFFER_SIZE);
  obs_property_set_long_description(Prop, TEXT_CUSTOM_BUFFER_SIZE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  Prop = obs_properties_add_int(RCGroup, "buffer_size", TEXT_BUFFER_SIZE, 0,
                                6553500, 1000);
  obs_property_int_set_suffix(Prop, " KB");
  obs_property_set_long_description(Prop, TEXT_BUFFER_SIZE_DESC);

  Prop = obs_properties_add_text(RCGroup, "min_qp", TEXT_MIN_QP,
                                 OBS_TEXT_DEFAULT);
  obs_property_set_long_description(Prop, TEXT_MIN_QP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  Prop = obs_properties_add_text(RCGroup, "max_qp", TEXT_MAX_QP,
                                 OBS_TEXT_DEFAULT);
  obs_property_set_long_description(Prop, TEXT_MAX_QP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int(RCGroup, "adaptive_max_frame_size",
                                TEXT_ADAPTIVE_MAX_FRAME_SIZE, 0, 2147483647, 100);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_MAX_FRAME_SIZE_DESC);
  obs_property_int_set_suffix(Prop, " bytes");
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  // VBV settings at bottom of Rate Control group
  Prop = obs_properties_add_list(RCGroup, "hrd_conformance",
                                 TEXT_HRD_CONFORMANCE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_HRD_CONFORMANCE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(RCGroup, "low_delay_hrd", TEXT_LOW_DELAY_HRD,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_LOW_DELAY_HRD_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(RCGroup, "low_delay_brc", TEXT_LOW_DELAY_BRC,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_LOW_DELAY_BRC_DESC);

  // MBBRC (macroblock-level BRC, aka auto-segmentation) is disabled for VP9:
  // oneVPL VP9 encoder defaults it OFF for all platforms and enabling it
  // triggers BRC_SEGMENTATION which corrupts the picture. Skip the control
  // entirely for VP9 so users can't turn it on.
  if (Codec != QSV_CODEC_VP9) {
    Prop = obs_properties_add_list(RCGroup, "mbbrc", TEXT_MBBRC,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
    obs_property_set_long_description(Prop, TEXT_MBBRC_DESC);
  }

  obs_properties_t *IFGroup = obs_properties_create();

  Prop = obs_properties_add_int(IFGroup, "keyint_sec", TEXT_KEYINT_SEC, 0,
                                65535, 1);
  obs_property_int_set_suffix(Prop, " s");
  obs_property_set_long_description(Prop, TEXT_KEYFRAME_INTERVAL_SEC_DESC);

  Prop = obs_properties_add_int(IFGroup, "num_ref_frame", TEXT_NUM_REF_FRAME,
                                0, 15, 1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("NumRefFrame.Tooltip"));

  Prop = obs_properties_add_int(IFGroup, "b_frames", TEXT_B_FRAMES, 0,
                                65534, 1);
  obs_property_set_long_description(Prop, TEXT_B_FRAMES_DESC);

  Prop = obs_properties_add_list(IFGroup, "lookahead", TEXT_LA,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_lookahead_mode);
  obs_property_set_long_description(Prop, TEXT_LOOKAHEAD_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(IFGroup, "lookahead_ds", TEXT_LA_DS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_lookahead_ds);
  obs_property_set_long_description(Prop, TEXT_LA_DS_DESC);

  Prop = obs_properties_add_int_slider(IFGroup, "la_depth", TEXT_LA_DEPTH,
                                       1, 100, 1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("LookaheadDepth.Tooltip"));

  Prop = obs_properties_add_list(IFGroup, "p_pyramid", TEXT_PYRAMID,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_PYRAMID_DESC);

  Prop = obs_properties_add_list(IFGroup, "use_raw_ref", TEXT_USE_RAW_REF,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_USE_RAW_REF_DESC);

  obs_properties_t *ETGroup = obs_properties_create();

  Prop = obs_properties_add_list(ETGroup, "enctools", TEXT_ENC_TOOLS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_DESC);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_visible(Prop, IsFeatureSupported("enc_tools"));
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_scene_change",
                                 TEXT_ENC_TOOLS_SCENE_CHANGE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_SCENE_CHANGE_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_adaptive_ref_p",
                                 TEXT_ENC_TOOLS_ADAPTIVE_REF_P,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_ADAPTIVE_REF_P_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_adaptive_ref_b",
                                 TEXT_ENC_TOOLS_ADAPTIVE_REF_B,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_ADAPTIVE_REF_B_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_adaptive_pyramid_quant_p",
                                 TEXT_ENC_TOOLS_ADAPTIVE_PYRAMID_QUANT_P,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_ADAPTIVE_PYRAMID_QUANT_P_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_adaptive_pyramid_quant_b",
                                 TEXT_ENC_TOOLS_ADAPTIVE_PYRAMID_QUANT_B,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_ADAPTIVE_PYRAMID_QUANT_B_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_adaptive_mbqp",
                                 TEXT_ENC_TOOLS_ADAPTIVE_MBQP,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_ADAPTIVE_MBQP_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_brc_buffer_hints",
                                 TEXT_ENC_TOOLS_BRC_BUFFER_HINTS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_BRC_BUFFER_HINTS_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_brc",
                                 TEXT_ENC_TOOLS_BRC,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_BRC_DESC);

  Prop = obs_properties_add_list(ETGroup, "enc_tools_saliency_map_hint",
                                 TEXT_ENC_TOOLS_SALIENCY_MAP_HINT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_SALIENCY_MAP_HINT_DESC);

  Prop = obs_properties_add_list(ETGroup, "adaptive_i", TEXT_ADAPTIVE_I,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_I_DESC);

  Prop = obs_properties_add_list(ETGroup, "adaptive_b", TEXT_ADAPTIVE_B,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_B_DESC);

  Prop = obs_properties_add_list(ETGroup, "adaptive_ref", TEXT_ADAPTIVE_REF,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_REF_DESC);

  Prop = obs_properties_add_list(ETGroup, "adaptive_cqm", TEXT_ADAPTIVE_CQM,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_CQM_DESC);

  Prop = obs_properties_add_list(ETGroup, "adaptive_ltr", TEXT_ADAPTIVE_LTR,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_LTR_DESC);

  Prop = obs_properties_add_list(ETGroup, "trellis", TEXT_TRELLIS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_trellis);
  obs_property_set_long_description(Prop, TEXT_TRELLIS_DESC);

  Prop = obs_properties_add_list(ETGroup, "rdo", TEXT_RDO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_RDO_DESC);

  Prop = obs_properties_add_list(ETGroup, "fade_detection",
                                 TEXT_FADE_DETECTION,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_FADE_DETECTION_DESC);

  Prop = obs_properties_add_list(ETGroup, "transform_skip",
                                 TEXT_TRANSFORM_SKIP,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_TRANSFORM_SKIP_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_HEVC &&
                           IsFeatureSupported("transform_skip"));

  obs_properties_t *RMGroup = obs_properties_create();

  Prop = obs_properties_add_list(RMGroup, "global_motion_bias_adjustment",
                                 TEXT_GLOBAL_MOTION_BIAS_ADJUSTMENT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_long_description(Prop, TEXT_GLOBAL_MOTION_BIAS_DESC);

  Prop = obs_properties_add_list(RMGroup, "mv_cost_scaling_factor",
                                 TEXT_MV_COST_SCALING_FACTOR,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_mv_cost_scaling);
  obs_property_set_long_description(Prop,
                                    obs_module_text("MVCostScalingFactor.Tooltip"));

  Prop = obs_properties_add_list(RMGroup, "direct_bias_adjustment",
                                 TEXT_DIRECT_BIAS_ADJUSTMENT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_DIRECT_BIAS_DESC);

  Prop = obs_properties_add_list(RMGroup, "mv_overpic_boundaries",
                                 TEXT_MV_OVER_PIC_BOUNDARIES,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_MV_OVER_PIC_BOUNDARIES_DESC);

  Prop = obs_properties_add_list(RMGroup, "weighted_pred",
                                 TEXT_WEIGHTED_PRED,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_weighted_pred_options);
  obs_property_set_long_description(Prop, TEXT_WEIGHTED_PRED_DESC);

  Prop = obs_properties_add_list(RMGroup, "weighted_bi_pred",
                                 TEXT_WEIGHTED_BI_PRED,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_weighted_pred_options);
  obs_property_set_long_description(Prop, TEXT_WEIGHTED_BI_PRED_DESC);

  Prop = obs_properties_add_int(RMGroup, "num_ref_active_p",
                                TEXT_NUM_REF_ACTIVE_P, 0, 65535, 1);
  obs_property_set_long_description(Prop, TEXT_NUM_REF_ACTIVE_P_DESC);

  Prop = obs_properties_add_int(RMGroup, "num_ref_active_bl0",
                                TEXT_NUM_REF_ACTIVE_BL0, 0, 65535, 1);
  obs_property_set_long_description(Prop, TEXT_NUM_REF_ACTIVE_BL0_DESC);

  Prop = obs_properties_add_int(RMGroup, "num_ref_active_bl1",
                                TEXT_NUM_REF_ACTIVE_BL1, 0, 65535, 1);
  obs_property_set_long_description(Prop, TEXT_NUM_REF_ACTIVE_BL1_DESC);

  obs_properties_t *VFGroup = obs_properties_create();

  Prop = obs_properties_add_list(VFGroup, "vpp", TEXT_VPP,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_VPP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(VFGroup, "denoise_mode", TEXT_DENOISE_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_denoise_mode);
  obs_property_set_long_description(Prop, TEXT_DENOISE_MODE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int_slider(VFGroup, "denoise_strength",
                                TEXT_DENOISE_STRENGTH, 1, 100, 1);
  obs_property_set_long_description(Prop, TEXT_DENOISE_STRENGTH_DESC);

  Prop = obs_properties_add_list(VFGroup, "scaling_mode", TEXT_SCALING_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_scaling_mode);
  obs_property_set_long_description(Prop, TEXT_SCALING_MODE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int(VFGroup, "vpp_out_width", TEXT_VPP_OUT_WIDTH,
                                0, 8192, 2);
  obs_property_set_long_description(Prop, TEXT_VPP_OUT_WIDTH_DESC);
  obs_property_set_visible(Prop, false);

  Prop = obs_properties_add_int(VFGroup, "vpp_out_height", TEXT_VPP_OUT_HEIGHT,
                                0, 8192, 4);
  obs_property_set_long_description(Prop, TEXT_VPP_OUT_HEIGHT_DESC);
  obs_property_set_visible(Prop, false);

  Prop = obs_properties_add_list(VFGroup, "detail", TEXT_DETAIL,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_DETAIL_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int_slider(VFGroup, "detail_factor",
                                       TEXT_DETAIL_FACTOR, 1, 100, 1);
  obs_property_set_long_description(Prop, TEXT_DETAIL_FACTOR_DESC);

  Prop = obs_properties_add_list(VFGroup, "image_stab_mode",
                                 TEXT_IMAGE_STAB_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_image_stab_mode);
  obs_property_set_long_description(Prop, TEXT_IMAGE_STAB_MODE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(VFGroup, "perc_enc_prefilter",
                                 TEXT_PERC_ENC_PREFILTER,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_PERC_ENC_PREFILTER_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(VFGroup, "vpp_mctf", TEXT_VPP_MCTF,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_VPP_MCTF_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int_slider(VFGroup, "vpp_mctf_strength",
                                       TEXT_VPP_MCTF_STRENGTH, 0, 20, 1);
  obs_property_set_long_description(Prop, TEXT_VPP_MCTF_STRENGTH_DESC);

  obs_properties_t *CSGroup = obs_properties_create();

  Prop = obs_properties_add_list(CSGroup, "profile", TEXT_PROFILE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(Prop, TEXT_PROFILE_DESC);

  if (Codec == QSV_CODEC_AVC) {
    const char *const *profileEntryH264 = qsv_profile_names_h264;
    while (*profileEntryH264) {
      obs_property_list_add_string(Prop, *profileEntryH264,
                                   *profileEntryH264);
      profileEntryH264++;
    }
  } else if (Codec == QSV_CODEC_AV1) {
    AddStrings(Prop, qsv_profile_names_av1);
  } else if (Codec == QSV_CODEC_VP9) {
    AddStrings(Prop, qsv_profile_names_vp9);
  } else if (Codec == QSV_CODEC_HEVC) {
    const char *const *profileEntryHEVC = qsv_profile_names_hevc;
    while (*profileEntryHEVC) {
      bool showProfileHEVC = true;
      if (platformCode != 0) {
        bool isRext = std::strcmp(*profileEntryHEVC, "rext") == 0;
        bool isSCC = std::strcmp(*profileEntryHEVC, "scc") == 0;
        // REXT encoding (12-bit/4:2:2/4:4:4) and SCC encoding require TGL_LP (Gen12)+
        if ((isRext || isSCC) && platformCode < MFX_PLATFORM_TIGERLAKE) {
          showProfileHEVC = false;
        }
      }
      if (showProfileHEVC) {
        obs_property_list_add_string(Prop, *profileEntryHEVC,
                                     *profileEntryHEVC);
      }
      profileEntryHEVC++;
    }
    obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  }

  if (Codec == QSV_CODEC_HEVC) {
    Prop = obs_properties_add_list(CSGroup, "hevc_tier", TEXT_HEVC_TIER,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(Prop, TEXT_TIER_DESC);

    bool hasHighTier = platformCode == 0 ||
                       platformCode >= MFX_PLATFORM_SKYLAKE;
    const char *const *tierEntry = qsv_profile_tiers_hevc;
    while (*tierEntry) {
      bool isHigh = std::strcmp(*tierEntry, "high") == 0;
      if (!isHigh || hasHighTier) {
        obs_property_list_add_string(Prop, *tierEntry, *tierEntry);
      }
      tierEntry++;
    }

    Prop = obs_properties_add_list(CSGroup, "hevc_level", TEXT_HEVC_LEVEL,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(Prop, TEXT_LEVEL_DESC);
    const char *const *levelEntry = qsv_levels_hevc;
    while (*levelEntry) {
      obs_property_list_add_string(Prop, *levelEntry, *levelEntry);
      levelEntry++;
    }
  }

  if (Codec == QSV_CODEC_AVC) {
    Prop = obs_properties_add_list(CSGroup, "avc_level", TEXT_HEVC_LEVEL,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(Prop, TEXT_LEVEL_DESC);
    const char *const *levelEntry = qsv_levels_avc;
    while (*levelEntry) {
      obs_property_list_add_string(Prop, *levelEntry, *levelEntry);
      levelEntry++;
    }
  }

  if (Codec == QSV_CODEC_AV1) {
    Prop = obs_properties_add_list(CSGroup, "av1_level", TEXT_HEVC_LEVEL,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(Prop, TEXT_LEVEL_DESC);
    const char *const *levelEntry = qsv_levels_av1;
    while (*levelEntry) {
      obs_property_list_add_string(Prop, *levelEntry, *levelEntry);
      levelEntry++;
    }
  }

  if (Codec == QSV_CODEC_HEVC) {
    Prop = obs_properties_add_list(CSGroup, "hevc_gpb", TEXT_HEVC_GPB,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(Prop, TEXT_HEVC_GPB_DESC);

    Prop = obs_properties_add_list(CSGroup, "hevc_sao", TEXT_HEVC_SAO,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_hevc_sao);
    obs_property_set_long_description(Prop, TEXT_HEVC_SAO_DESC);
  }

  if (Codec == QSV_CODEC_AV1) {
    Prop = obs_properties_add_list(CSGroup, "screen_content_tools",
                                   TEXT_SCREEN_CONTENT_TOOLS,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_screen_content_tools);
    obs_property_set_long_description(Prop,
                                      obs_module_text("ScreenContentTools.Tooltip"));

    Prop = obs_properties_add_list(CSGroup, "av1_cdef", TEXT_AV1_CDEF,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(Prop, TEXT_AV1_CDEF_DESC);

    Prop = obs_properties_add_list(CSGroup, "av1_restoration",
                                   TEXT_AV1_RESTORATION,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(Prop, TEXT_AV1_RESTORATION_DESC);

    Prop = obs_properties_add_list(CSGroup, "av1_loop_filter",
                                   TEXT_AV1_LOOP_FILTER,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(Prop, TEXT_AV1_LOOP_FILTER_DESC);

    Prop = obs_properties_add_list(CSGroup, "av1_super_res", TEXT_AV1_SUPER_RES,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(Prop, TEXT_AV1_SUPER_RES_DESC);

    Prop = obs_properties_add_list(CSGroup, "av1_interp_filter",
                                   TEXT_AV1_INTERP_FILTER,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_av1_interp_filter);
    obs_property_set_long_description(Prop, TEXT_AV1_INTERP_FILTER_DESC);

    Prop = obs_properties_add_list(CSGroup, "av1_error_resilient",
                                   TEXT_AV1_ERROR_RESILIENT,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(Prop, TEXT_AV1_ERROR_RESILIENT_DESC);
  }

  obs_properties_t *IRGroup = obs_properties_create();

  if (Codec != QSV_CODEC_AV1 && Codec != QSV_CODEC_VP9) {
    Prop = obs_properties_add_list(IRGroup, "intra_ref_encoding",
                                   TEXT_INTRA_REF_ENCODING,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition);
    obs_property_set_long_description(Prop, TEXT_INTRA_REF_ENCODING_DESC);
    obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

    Prop = obs_properties_add_list(IRGroup, "intra_ref_type",
                                   TEXT_INTRA_REF_TYPE,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_intra_ref_encoding);
    obs_property_set_long_description(Prop, TEXT_INTRA_REF_TYPE_DESC);

    Prop = obs_properties_add_int(IRGroup, "intra_ref_cycle_size",
                                  TEXT_INTRA_REF_CYCLE_SIZE, 2, 1000, 1);
    obs_property_set_long_description(Prop, TEXT_INTRA_REF_CYCLE_SIZE_DESC);

    Prop = obs_properties_add_int(IRGroup, "intra_ref_qp_delta",
                                  TEXT_INTRA_REF_QP_DELTA, -51, 51, 1);
    obs_property_set_long_description(Prop, TEXT_INTRA_REF_QP_DELTA_DESC);
  }

  obs_properties_t *MXGroup = obs_properties_create();

  Prop = obs_properties_add_list(MXGroup, "low_power", TEXT_LOW_POWER,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_long_description(Prop, TEXT_LOW_POWER_DESC);

  Prop = obs_properties_add_int(MXGroup, "async_depth", TEXT_ASYNC_DEPTH,
                                1, 1000, 1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("AsyncDepth.Tooltip"));

  Prop = obs_properties_add_int(MXGroup, "gpu_number", TEXT_GPU_NUMBER,
                                0, 4, 1);
  obs_property_set_long_description(Prop, TEXT_GPU_NUMBER_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(MXGroup, "scenario_info", TEXT_SCENARIO_INFO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_scenario_info);
  obs_property_set_long_description(Prop, TEXT_SCENARIO_INFO_DESC);

  Prop = obs_properties_add_list(MXGroup, "content_info", TEXT_CONTENT_INFO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_content_info);
  obs_property_set_long_description(Prop, TEXT_CONTENT_INFO_DESC);

  Prop = obs_properties_add_int_slider(MXGroup, "temporal_layers",
                                       TEXT_TEMPORAL_LAYERS, 0, 8, 1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("TemporalLayers.Desc"));

  Prop = obs_properties_add_text(MXGroup, "custom_coding_options",
                                 TEXT_CUSTOM_CODING_OPTIONS,
                                 OBS_TEXT_MULTILINE);
  obs_property_set_long_description(Prop,
                                    TEXT_CUSTOM_CODING_OPTIONS_DESC);

  obs_properties_add_group(Props, "group_rate_control",
                           TEXT_GROUP_RATE_CONTROL,
                           OBS_GROUP_NORMAL, RCGroup);
  obs_properties_add_group(Props, "group_codec_specific",
                           TEXT_GROUP_CODEC_SPECIFIC,
                           OBS_GROUP_NORMAL, CSGroup);
  obs_properties_add_group(Props, "group_inter_frame",
                           TEXT_GROUP_INTER_FRAME,
                           OBS_GROUP_NORMAL, IFGroup);
  obs_properties_add_group(Props, "group_enc_tools",
                           TEXT_GROUP_ENC_TOOLS,
                           OBS_GROUP_NORMAL, ETGroup);
  obs_properties_add_group(Props, "group_ref_motion",
                           TEXT_GROUP_REF_MOTION,
                           OBS_GROUP_NORMAL, RMGroup);
  obs_properties_add_group(Props, "group_vpp_filters",
                           TEXT_GROUP_VPP_FILTERS,
                           OBS_GROUP_NORMAL, VFGroup);
  obs_properties_add_group(Props, "group_intra_refresh",
                           TEXT_GROUP_INTRA_REFRESH,
                           OBS_GROUP_NORMAL, IRGroup);
  obs_properties_add_group(Props, "group_misc",
                           TEXT_GROUP_MISC,
                           OBS_GROUP_NORMAL, MXGroup);

  return Props;
}

// Forward declaration for MapString used below
template <typename T, size_t N>
static std::optional<T> MapString(std::string_view key,
                                   const std::pair<std::string_view, T> (&map)[N]);

// Helper: parse AV1 ON/OFF/AUTO three-state string.
// 2 -> MFX_CODINGOPTION_UNKNOWN (let driver decide)
static constexpr std::pair<std::string_view, mfxU16> kAV1TernaryMap[] = {
    {"AUTO", 2},
    {"ON",   1},
    {"OFF",  0},
};
static inline mfxU16 ParseAV1Ternary(const char *Data) {
  return MapString(Data, kAV1TernaryMap).value_or(2);
}

// Helper: parse WeightedPred/BiPred string. "AUTO" lets the driver decide
// (MFX_WEIGHTED_PRED_UNKNOWN); unmapped strings also fall back to AUTO.
static constexpr std::pair<std::string_view, mfxU16> kWeightedPredModeMap[] = {
    {"AUTO",     MFX_WEIGHTED_PRED_UNKNOWN},
    {"OFF",      MFX_WEIGHTED_PRED_UNKNOWN},
    {"DEFAULT",  MFX_WEIGHTED_PRED_DEFAULT},
    {"EXPLICIT", MFX_WEIGHTED_PRED_EXPLICIT},
    {"IMPLICIT", MFX_WEIGHTED_PRED_IMPLICIT},
};
static inline mfxU16 ParseWeightedPredMode(const char *Data) {
  return MapString(Data, kWeightedPredModeMap).value_or(MFX_WEIGHTED_PRED_UNKNOWN);
}

// Codec level lookup tables
// Replace large if-else chains with data-driven lookup.

struct LevelEntry {
  const char *name;
  mfxU16 value;
};

static mfxU16 ParseCodecLevel(std::string_view LevelStr,
                               const LevelEntry *Table, size_t Count) {
  auto it = std::ranges::find(Table, Table + Count, LevelStr, &LevelEntry::name);
  return it != Table + Count ? it->value : 0;
}

static const LevelEntry kAVCLevels[] = {
    {"auto", 0},   {"1", MFX_LEVEL_AVC_1},      {"1b", MFX_LEVEL_AVC_1b},
    {"1.1", MFX_LEVEL_AVC_11}, {"1.2", MFX_LEVEL_AVC_12},
    {"1.3", MFX_LEVEL_AVC_13}, {"2", MFX_LEVEL_AVC_2},
    {"2.1", MFX_LEVEL_AVC_21}, {"2.2", MFX_LEVEL_AVC_22},
    {"3", MFX_LEVEL_AVC_3},    {"3.1", MFX_LEVEL_AVC_31},
    {"3.2", MFX_LEVEL_AVC_32}, {"4", MFX_LEVEL_AVC_4},
    {"4.1", MFX_LEVEL_AVC_41}, {"4.2", MFX_LEVEL_AVC_42},
    {"5", MFX_LEVEL_AVC_5},    {"5.1", MFX_LEVEL_AVC_51},
    {"5.2", MFX_LEVEL_AVC_52}, {"6", MFX_LEVEL_AVC_6},
    {"6.1", MFX_LEVEL_AVC_61}, {"6.2", MFX_LEVEL_AVC_62},
};

static const LevelEntry kHEVCLevels[] = {
    {"auto", 0},   {"1", MFX_LEVEL_HEVC_1},     {"2", MFX_LEVEL_HEVC_2},
    {"2.1", MFX_LEVEL_HEVC_21}, {"3", MFX_LEVEL_HEVC_3},
    {"3.1", MFX_LEVEL_HEVC_31}, {"4", MFX_LEVEL_HEVC_4},
    {"4.1", MFX_LEVEL_HEVC_41}, {"5", MFX_LEVEL_HEVC_5},
    {"5.1", MFX_LEVEL_HEVC_51}, {"5.2", MFX_LEVEL_HEVC_52},
    {"6", MFX_LEVEL_HEVC_6},    {"6.1", MFX_LEVEL_HEVC_61},
    {"6.2", MFX_LEVEL_HEVC_62}, {"8.5", MFX_LEVEL_HEVC_85},
};

static const LevelEntry kAV1Levels[] = {
    {"auto", 0},   {"2.0", MFX_LEVEL_AV1_2},    {"2.1", MFX_LEVEL_AV1_21},
    {"2.2", MFX_LEVEL_AV1_22}, {"2.3", MFX_LEVEL_AV1_23},
    {"3.0", MFX_LEVEL_AV1_3},  {"3.1", MFX_LEVEL_AV1_31},
    {"3.2", MFX_LEVEL_AV1_32}, {"3.3", MFX_LEVEL_AV1_33},
    {"4.0", MFX_LEVEL_AV1_4},  {"4.1", MFX_LEVEL_AV1_41},
    {"4.2", MFX_LEVEL_AV1_42}, {"4.3", MFX_LEVEL_AV1_43},
    {"5.0", MFX_LEVEL_AV1_5},  {"5.1", MFX_LEVEL_AV1_51},
    {"5.2", MFX_LEVEL_AV1_52}, {"5.3", MFX_LEVEL_AV1_53},
    {"6.0", MFX_LEVEL_AV1_6},  {"6.1", MFX_LEVEL_AV1_61},
    {"6.2", MFX_LEVEL_AV1_62}, {"6.3", MFX_LEVEL_AV1_63},
    {"7.0", MFX_LEVEL_AV1_7},  {"7.1", MFX_LEVEL_AV1_71},
    {"7.2", MFX_LEVEL_AV1_72}, {"7.3", MFX_LEVEL_AV1_73},
};

// Map string to value via compile-time lookup table
template <typename T, size_t N>
static std::optional<T> MapString(std::string_view key,
                                   const std::pair<std::string_view, T> (&map)[N]) {
  for (const auto &[str, val] : map) {
    if (key == str)
      return val;
  }
  return std::nullopt;
}

static void GetEncoderParams(plugin_context *Context, obs_data_t *Settings) {
  video_t *Video = obs_encoder_video(Context->EncoderData);
  const video_output_info *VOI = video_output_get_info(Video);
  const char *Codec = "";

  const char *TargetUsageData = obs_data_get_string(Settings, "target_usage");
  const char *CodecProfileData = obs_data_get_string(Settings, "profile");
  const char *CodecProfileTierData = obs_data_get_string(Settings, "hevc_tier");
  const char *CodecLevelData = obs_data_get_string(Settings, "hevc_level");
  const char *CodecLevelDataAVC = obs_data_get_string(Settings, "avc_level");
  const char *CodecLevelDataAV1 = obs_data_get_string(Settings, "av1_level");
  const char *RateControlData = obs_data_get_string(Settings, "rate_control");
  int TargetBitrateData =
      static_cast<int>(obs_data_get_int(Settings, "bitrate"));
  bool CustomBufferSizeData = obs_data_get_bool(Settings, "custom_buffer_size");
  int BufferSizeData =
      static_cast<int>(obs_data_get_int(Settings, "buffer_size"));
  int MaxBitrateData =
      static_cast<int>(obs_data_get_int(Settings, "max_bitrate"));
  int CQPData = static_cast<int>(obs_data_get_int(Settings, "cqp"));
  int ICQQualityData =
      static_cast<int>(obs_data_get_int(Settings, "icq_quality"));
  // VP9 ICQQuality internally uses 0-255 range (MAX_ICQ_QUALITY_INDEX).
  // UI exposes 1-63, scale x4 to match the internal range.
  if (Context->Codec == QSV_CODEC_VP9) {
    ICQQualityData *= 4;
  }
  int KeyIntervalData =
      static_cast<int>(obs_data_get_int(Settings, "keyint_sec"));
  int BFramesData =
      static_cast<int>(obs_data_get_int(Settings, "b_frames"));

  const char *HRDConformanceData =
      obs_data_get_string(Settings, "hrd_conformance");

  const char *LowDelayHRDData = obs_data_get_string(Settings, "low_delay_hrd");
  const char *LowDelayBRCData = obs_data_get_string(Settings, "low_delay_brc");

  const char *MBBRCData = obs_data_get_string(Settings, "mbbrc");

  const char *AdaptiveIData = obs_data_get_string(Settings, "adaptive_i");
  const char *AdaptiveBData = obs_data_get_string(Settings, "adaptive_b");
  const char *AdaptiveRefData = obs_data_get_string(Settings, "adaptive_ref");
  const char *AdaptiveCQMData = obs_data_get_string(Settings, "adaptive_cqm");
  const char *AdaptiveLTRData = obs_data_get_string(Settings, "adaptive_ltr");
  const char *LowPowerData = obs_data_get_string(Settings, "low_power");
  const char *UseRawRefData = obs_data_get_string(Settings, "use_raw_ref");
  const char *RDOData = obs_data_get_string(Settings, "rdo");
  const char *TrellisData = obs_data_get_string(Settings, "trellis");
  int NumRefFrameData =
      static_cast<int>(obs_data_get_int(Settings, "num_ref_frame"));
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
  const char *AV1RestorationData = obs_data_get_string(Settings, "av1_restoration");
  const char *AV1LoopFilterData = obs_data_get_string(Settings, "av1_loop_filter");
  const char *AV1SuperResData = obs_data_get_string(Settings, "av1_super_res");
  const char *AV1InterpFilterData = obs_data_get_string(Settings, "av1_interp_filter");
  const char *AV1ErrorResilientData = obs_data_get_string(Settings, "av1_error_resilient");
  const char *WeightedPredData = obs_data_get_string(Settings, "weighted_pred");
  const char *WeightedBiPredData = obs_data_get_string(Settings, "weighted_bi_pred");
  int AdaptiveMaxFrameSizeData = static_cast<int>(obs_data_get_int(Settings, "adaptive_max_frame_size"));
  const char *VPPMCTFData = obs_data_get_string(Settings, "vpp_mctf");
  int VPPMCTFStrengthData = static_cast<int>(obs_data_get_int(Settings, "vpp_mctf_strength"));
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

  int TemporalLayersData =
      static_cast<int>(obs_data_get_int(Settings, "temporal_layers"));

  const char *MinQPData = obs_data_get_string(Settings, "min_qp");
  const char *MaxQPData = obs_data_get_string(Settings, "max_qp");

  int VideoWidth =
      static_cast<int>(obs_encoder_get_width(Context->EncoderData));
  int VideoHeight =
      static_cast<int>(obs_encoder_get_height(Context->EncoderData));

  const char *VideoProcessingStatusData = obs_data_get_string(Settings, "vpp");
  int DenoiseStrengthData =
      static_cast<int>(obs_data_get_int(Settings, "denoise_strength"));
  const char *DenoiseModeData = obs_data_get_string(Settings, "denoise_mode");
  const char *DetailData = obs_data_get_string(Settings, "detail");
  int DetailFactorData =
      static_cast<int>(obs_data_get_int(Settings, "detail_factor"));
  const char *ScalingModeData = obs_data_get_string(Settings, "scaling_mode");
  const char *ImageStabModeData =
      obs_data_get_string(Settings, "image_stab_mode");
  const char *PercEncPrefilterData =
      obs_data_get_string(Settings, "perc_enc_prefilter");

  int GPUNumData = static_cast<int>(obs_data_get_int(Settings, "gpu_number"));

  Context->EncoderParams.GPUNum = GPUNumData;

  Context->CachedFpsNum = static_cast<mfxU32>(VOI->fps_num);
  Context->CachedFpsDen = static_cast<mfxU32>(VOI->fps_den);
  Context->CachedTSDiv = 90000 * static_cast<int64_t>(VOI->fps_den);

  // 1. TargetUsage
  static constexpr std::pair<std::string_view, mfxU16> kTargetUsageMap[] = {
    {"TU1 (Veryslow)", MFX_TARGETUSAGE_1},
    {"TU2 (Slower)",   MFX_TARGETUSAGE_2},
    {"TU3 (Slow)",     MFX_TARGETUSAGE_3},
    {"TU4 (Balanced)", MFX_TARGETUSAGE_4},
    {"TU5 (Fast)",     MFX_TARGETUSAGE_5},
    {"TU6 (Faster)",   MFX_TARGETUSAGE_6},
    {"TU7 (Veryfast)", MFX_TARGETUSAGE_7},
  };
  if (auto v = MapString(TargetUsageData, kTargetUsageMap)) {
    Context->EncoderParams.TargetUsage = *v;
  }

  Context->EncoderParams.AV1CDEF = ParseAV1Ternary(AV1CDEFData);
  Context->EncoderParams.AV1Restoration = ParseAV1Ternary(AV1RestorationData);
  Context->EncoderParams.AV1LoopFilter = ParseAV1Ternary(AV1LoopFilterData);
  Context->EncoderParams.AV1SuperRes = ParseAV1Ternary(AV1SuperResData);
  Context->EncoderParams.AV1ErrorResilient = ParseAV1Ternary(AV1ErrorResilientData);

  // 3. AV1InterpFilter
  static constexpr std::pair<std::string_view, mfxU16> kAV1InterpFilterMap[] = {
    {"DEFAULT",          0},
    {"EIGHTTAP",         1},
    {"EIGHTTAP_SMOOTH",  2},
    {"EIGHTTAP_SHARP",   3},
    {"BILINEAR",         4},
    {"SWITCHABLE",       5},
  };
  if (auto v = MapString(AV1InterpFilterData, kAV1InterpFilterMap)) {
    Context->EncoderParams.AV1InterpFilter = *v;
  }

  Context->EncoderParams.WeightedPred = ParseWeightedPredMode(WeightedPredData);
  Context->EncoderParams.WeightedBiPred = ParseWeightedPredMode(WeightedBiPredData);

  Context->EncoderParams.AdaptiveMaxFrameSize = AdaptiveMaxFrameSizeData;

  if (strcmp(VPPMCTFData, "ON") == 0)
    Context->EncoderParams.VPPMCTFMode = 1;
  else
    Context->EncoderParams.VPPMCTFMode = 0;
  Context->EncoderParams.VPPMCTFStrength = static_cast<mfxU16>(VPPMCTFStrengthData);

  switch (Context->Codec) {
  case QSV_CODEC_AVC: {
    Codec = "H.264";
    // 4. CodecProfile AVC
    static constexpr std::pair<std::string_view, mfxU16> kCodecProfileAVCMap[] = {
      {"baseline",               MFX_PROFILE_AVC_BASELINE},
      {"main",                   MFX_PROFILE_AVC_MAIN},
      {"high",                   MFX_PROFILE_AVC_HIGH},
      {"extended",               MFX_PROFILE_AVC_EXTENDED},
      {"high10",                 MFX_PROFILE_AVC_HIGH10},
      {"constrained_baseline",   MFX_PROFILE_AVC_CONSTRAINED_BASELINE},
      {"constrained_high",       MFX_PROFILE_AVC_CONSTRAINED_HIGH},
    };
    if (auto v = MapString(CodecProfileData, kCodecProfileAVCMap)) {
      Context->EncoderParams.CodecProfile = *v;
    }

    Context->EncoderParams.CodecLevel =
        ParseCodecLevel(CodecLevelDataAVC, kAVCLevels,
                        sizeof(kAVCLevels) / sizeof(kAVCLevels[0]));
    break;
  }
  case QSV_CODEC_HEVC: {
    Codec = "HEVC";
    // 5. CodecProfile HEVC
    static constexpr std::pair<std::string_view, mfxU16> kCodecProfileHEVCMap[] = {
      {"main",    MFX_PROFILE_HEVC_MAIN},
      {"main10",  MFX_PROFILE_HEVC_MAIN10},
      {"rext",    MFX_PROFILE_HEVC_REXT},
      {"mainsp",  MFX_PROFILE_HEVC_MAINSP},
      {"scc",     MFX_PROFILE_HEVC_SCC},
    };
    if (auto v = MapString(CodecProfileData, kCodecProfileHEVCMap)) {
      Context->EncoderParams.CodecProfile = *v;
    }

    if (std::strcmp(CodecProfileTierData, "main") == 0) {
      Context->EncoderParams.CodecProfileTier = MFX_TIER_HEVC_MAIN;
    } else {
      mfxU16 platformCode = QueryPlatformCodeName();
      bool highTierUnsupported = platformCode != 0 &&
                                 platformCode < MFX_PLATFORM_SKYLAKE;
      if (highTierUnsupported) {
        info("\tHEVC High Tier not supported on this GPU "
             "(platform < Skylake), falling back to Main Tier");
        Context->EncoderParams.CodecProfileTier = MFX_TIER_HEVC_MAIN;
      } else {
        Context->EncoderParams.CodecProfileTier = MFX_TIER_HEVC_HIGH;
      }
    }

    Context->EncoderParams.CodecLevel =
        ParseCodecLevel(CodecLevelData, kHEVCLevels,
                        sizeof(kHEVCLevels) / sizeof(kHEVCLevels[0]));
    break;
  }
  case QSV_CODEC_AV1: {
    Codec = "AV1";
    // 6. CodecProfile AV1
    static constexpr std::pair<std::string_view, mfxU16> kCodecProfileAV1Map[] = {
      {"main",  MFX_PROFILE_AV1_MAIN},
      {"high",  MFX_PROFILE_AV1_HIGH},
      {"pro",   MFX_PROFILE_AV1_PRO},
    };
    if (auto v = MapString(CodecProfileData, kCodecProfileAV1Map)) {
      Context->EncoderParams.CodecProfile = *v;
    }

    Context->EncoderParams.CodecLevel =
        ParseCodecLevel(CodecLevelDataAV1, kAV1Levels,
                        sizeof(kAV1Levels) / sizeof(kAV1Levels[0]));
    break;
  }
  case QSV_CODEC_VP9: {
    Codec = "VP9";
    // VP9 profiles: 0=8bit420, 1=8bit444, 2=10bit420, 3=10bit444
    static constexpr std::pair<std::string_view, mfxU16> kCodecProfileVP9Map[] = {
      {"0 (8-bit 4:2:0)", MFX_PROFILE_VP9_0},
      {"1 (8-bit 4:4:4)", MFX_PROFILE_VP9_1},
      {"2 (10-bit 4:2:0)", MFX_PROFILE_VP9_2},
      {"3 (10-bit 4:4:4)", MFX_PROFILE_VP9_3},
    };
    if (auto v = MapString(CodecProfileData, kCodecProfileVP9Map)) {
      Context->EncoderParams.CodecProfile = *v;
    }
    // VP9 has no codec level concept in OneVPL
    break;
  }
  }
  Context->EncoderParams.VideoFormat = 5;
  Context->EncoderParams.VideoFullRange = VOI->range == VIDEO_RANGE_FULL;

  switch (VOI->colorspace) {
  case VIDEO_CS_601:
    Context->EncoderParams.ColourPrimaries = 6;
    Context->EncoderParams.TransferCharacteristics = 6;
    Context->EncoderParams.MatrixCoefficients = 6;
    Context->EncoderParams.ChromaSampleLocTypeTopField = 0;
    Context->EncoderParams.ChromaSampleLocTypeBottomField = 0;
    break;
  case VIDEO_CS_DEFAULT:
  case VIDEO_CS_709:
    Context->EncoderParams.ColourPrimaries = 1;
    Context->EncoderParams.TransferCharacteristics = 1;
    Context->EncoderParams.MatrixCoefficients = 1;
    Context->EncoderParams.ChromaSampleLocTypeTopField = 0;
    Context->EncoderParams.ChromaSampleLocTypeBottomField = 0;
    break;
  case VIDEO_CS_SRGB:
    Context->EncoderParams.ColourPrimaries = 1;
    Context->EncoderParams.TransferCharacteristics = 13;
    Context->EncoderParams.MatrixCoefficients = 1;
    Context->EncoderParams.ChromaSampleLocTypeTopField = 0;
    Context->EncoderParams.ChromaSampleLocTypeBottomField = 0;
    break;
  case VIDEO_CS_2100_PQ:
    Context->EncoderParams.ColourPrimaries = 9;
    Context->EncoderParams.TransferCharacteristics = 16;
    Context->EncoderParams.MatrixCoefficients = 9;
    Context->EncoderParams.ChromaSampleLocTypeTopField = 2;
    Context->EncoderParams.ChromaSampleLocTypeBottomField = 2;
    break;
  case VIDEO_CS_2100_HLG:
    Context->EncoderParams.ColourPrimaries = 9;
    Context->EncoderParams.TransferCharacteristics = 18;
    Context->EncoderParams.MatrixCoefficients = 9;
    Context->EncoderParams.ChromaSampleLocTypeTopField = 2;
    Context->EncoderParams.ChromaSampleLocTypeBottomField = 2;
  }

  const auto PQ = VOI->colorspace == VIDEO_CS_2100_PQ;
  const auto HLG = VOI->colorspace == VIDEO_CS_2100_HLG;
  if (PQ || HLG) {
    const int HRDNominalPeakLevel =
        PQ ? static_cast<int>(obs_get_video_hdr_nominal_peak_level())
           : (HLG ? 1000 : 0);

    Context->EncoderParams.DisplayPrimariesX[0] = 13250;
    Context->EncoderParams.DisplayPrimariesX[1] = 7500;
    Context->EncoderParams.DisplayPrimariesX[2] = 34000;
    Context->EncoderParams.DisplayPrimariesY[0] = 34500;
    Context->EncoderParams.DisplayPrimariesY[1] = 3000;
    Context->EncoderParams.DisplayPrimariesY[2] = 16000;
    Context->EncoderParams.WhitePointX = 15635;
    Context->EncoderParams.WhitePointY = 16450;
    Context->EncoderParams.MaxDisplayMasteringLuminance =
        static_cast<mfxU32>(HRDNominalPeakLevel * 10000);
    Context->EncoderParams.MinDisplayMasteringLuminance =
        Context->Codec == QSV_CODEC_AV1 ? 0 : 1;

    Context->EncoderParams.MaxContentLightLevel =
        static_cast<mfxU16>(HRDNominalPeakLevel);
    Context->EncoderParams.MaxPicAverageLightLevel =
        static_cast<mfxU16>(HRDNominalPeakLevel);
  }

  ParseOptionalBool(LowDelayHRDData, Context->EncoderParams.LowDelayHRD);

  ParseOptionalBool(LowDelayBRCData, Context->EncoderParams.LowDelayBRC);

  ParseOptionalBool(MVOverPicBoundariesData,
                    Context->EncoderParams.MotionVectorsOverPicBoundaries);

  ParseOptionalBool(HRDConformanceData, Context->EncoderParams.HRDConformance);

  ParseOptionalBool(MBBRCData, Context->EncoderParams.MBBRC);

  static constexpr std::pair<std::string_view, bool> kEncToolsMap[] = {
    {"ON", true},
  };
  Context->EncoderParams.EncTools = MapString(EncToolsData, kEncToolsMap).value_or(false);

  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_scene_change"),
                    Context->EncoderParams.EncToolsSceneChange);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_ref_p"),
                    Context->EncoderParams.EncToolsAdaptiveRefP);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_ref_b"),
                    Context->EncoderParams.EncToolsAdaptiveRefB);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_pyramid_quant_p"),
                    Context->EncoderParams.EncToolsAdaptivePyramidQuantP);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_pyramid_quant_b"),
                    Context->EncoderParams.EncToolsAdaptivePyramidQuantB);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_adaptive_mbqp"),
                    Context->EncoderParams.EncToolsAdaptiveMBQP);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_brc_buffer_hints"),
                    Context->EncoderParams.EncToolsBRCBufferHints);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_brc"),
                    Context->EncoderParams.EncToolsBRC);
  ParseOptionalBool(obs_data_get_string(Settings, "enc_tools_saliency_map_hint"),
                    Context->EncoderParams.EncToolsSaliencyMapHint);

  ParseOptionalBool(DirectBiasAdjustmentData,
                    Context->EncoderParams.DirectBiasAdjustment);

  // 7. MVCostScalingFactor. "DEFAULT" sets MV cost to 0; "AUTO" (unmapped)
  static constexpr std::pair<std::string_view, int> kMVCostScalingFactorMap[] = {
    {"DEFAULT", 0},
    {"1/2",     1},
    {"1/4",     2},
    {"1/8",     3},
  };
  if (auto v = MapString(MVCostScalingFactorData, kMVCostScalingFactorMap)) {
    Context->EncoderParams.MVCostScalingFactor = *v;
  }

  ParseOptionalBool(UseRawRefData, Context->EncoderParams.RawRef);

  Context->EncoderParams.PPyramid = (std::strcmp(PPyramidData, "ON") == 0);

  ParseOptionalBool(GlobalMotionBiasAdjustmentData,
                    Context->EncoderParams.GlobalMotionBiasAdjustment);

  if (std::strcmp(LookaheadData, "HQ") == 0) {
    Context->EncoderParams.Lookahead = true;

    {
      int Depth =
          static_cast<int>(obs_data_get_int(Settings, "la_depth"));
      if (Depth < 1)
        Depth = 60;
      else if (Depth > 100)
        Depth = 100;
      Context->EncoderParams.LADepth = static_cast<mfxU16>(Depth);
    }
  } else if (std::strcmp(LookaheadData, "LP") == 0) {
    if (BFramesData > 0) {
      Context->EncoderParams.Lookahead = true;
      Context->EncoderParams.LADepth =
          BFramesData > 7 ? 8 : static_cast<mfxU16>(BFramesData + 1);
    }
  } else {
    Context->EncoderParams.Lookahead = false;
  }

  // LookAheadDS is parsed in all modes so the user setting is honored in
  static constexpr std::pair<std::string_view, int> kLookaheadDSMap[] = {
    {"1X",    0},
    {"2X",    1},
    {"4X",    2},
  };
  if (auto v = MapString(LookaheadDSData, kLookaheadDSMap)) {
    Context->EncoderParams.LookAheadDS = *v;
  }

  static constexpr std::pair<std::string_view, int> kIntraRefEncodingMap[] = {
    {"ON",  1},
    {"OFF", 0},
  };
  if (auto v = MapString(IntraRefEncodingData, kIntraRefEncodingMap)) {
    Context->EncoderParams.IntraRefEncoding = *v;
  }

  if (std::strcmp(IntraRefTypeData, "VERTICAL") == 0) {
    Context->EncoderParams.IntraRefType = MFX_REFRESH_VERTICAL;
  } else {
    Context->EncoderParams.IntraRefType = MFX_REFRESH_HORIZONTAL;
  }

  ParseOptionalBool(AdaptiveCQMData, Context->EncoderParams.AdaptiveCQM);

  ParseOptionalBool(AdaptiveLTRData, Context->EncoderParams.AdaptiveLTR);

  ParseOptionalBool(AdaptiveIData, Context->EncoderParams.AdaptiveI);

  ParseOptionalBool(AdaptiveBData, Context->EncoderParams.AdaptiveB);

  ParseOptionalBool(AdaptiveRefData, Context->EncoderParams.AdaptiveRef);

  static constexpr std::pair<std::string_view, bool> kLowPowerMap[] = {
    {"ON",  true},
    {"OFF", false},
  };
  if (auto v = MapString(LowPowerData, kLowPowerMap)) {
    Context->EncoderParams.Lowpower = *v;
  }

  ParseOptionalBool(RDOData, Context->EncoderParams.RDO);

  // 9. Trellis. "AUTO" stays unmapped so Trellis remains nullopt and the
  static constexpr std::pair<std::string_view, int> kTrellisMap[] = {
    {"OFF", 0},
    {"I",   1},
    {"IP",  2},
    {"IPB", 3},
    {"IB",  4},
    {"P",   5},
    {"PB",  6},
    {"B",   7},
  };
  if (auto v = MapString(TrellisData, kTrellisMap)) {
    Context->EncoderParams.Trellis = *v;
  }

  // 10. SAO
  static constexpr std::pair<std::string_view, int> kSAOMap[] = {
    {"DISABLE", 0},
    {"LUMA",    1},
    {"CHROMA",  2},
    {"ALL",     3},
  };
  if (auto v = MapString(SAOData, kSAOMap)) {
    Context->EncoderParams.SAO = *v;
  }

  ParseOptionalBool(GPBData, Context->EncoderParams.GPB);

  // 11. ScenarioInfo (special: OFF -> nullopt, AUTO -> 0)
  static constexpr std::pair<std::string_view, std::optional<mfxU16>> kScenarioInfoMap[] = {
    {"OFF",                std::nullopt},
    {"AUTO",               std::optional<mfxU16>(0)},
    {"DISPLAY_REMOTING",   std::optional<mfxU16>(1)},
    {"VIDEO_CONFERENCE",   std::optional<mfxU16>(2)},
    {"ARCHIVE",            std::optional<mfxU16>(3)},
    {"LIVE_STREAMING",     std::optional<mfxU16>(4)},
    {"CAMERA_CAPTURE",     std::optional<mfxU16>(5)},
    {"VIDEO_SURVEILLANCE", std::optional<mfxU16>(6)},
    {"GAME_STREAMING",     std::optional<mfxU16>(7)},
    {"REMOTE_GAMING",      std::optional<mfxU16>(8)},
  };
  if (auto v = MapString(ScenarioInfoData, kScenarioInfoMap)) {
    Context->EncoderParams.ScenarioInfo = *v;
  }

  // 12. ContentInfo (special: OFF -> nullopt, AUTO -> 0)
  // Uses mfxExtCodingOption3::ContentInfo values from API
  static constexpr std::pair<std::string_view, std::optional<mfxU16>> kContentInfoMap[] = {
    {"OFF",                std::nullopt},
    {"AUTO",               std::optional<mfxU16>(MFX_CONTENT_UNKNOWN)},
    {"FULL_SCREEN_VIDEO",  std::optional<mfxU16>(MFX_CONTENT_FULL_SCREEN_VIDEO)},
    {"NON_VIDEO_SCREEN",   std::optional<mfxU16>(MFX_CONTENT_NON_VIDEO_SCREEN)},
    {"NOISY_VIDEO",        std::optional<mfxU16>(MFX_CONTENT_NOISY_VIDEO)},
  };
  if (auto v = MapString(ContentInfoData, kContentInfoMap)) {
    Context->EncoderParams.ContentInfo = *v;
  }

  static constexpr std::pair<std::string_view, std::optional<bool>> kTransformSkipMap[] = {
    {"AUTO", std::nullopt},
    {"ON",   true},
    {"OFF",  false},
  };
  if (auto v = MapString(TransformSkipData, kTransformSkipMap)) {
    Context->EncoderParams.TransformSkip = *v;
  }

  static constexpr std::pair<std::string_view, std::optional<bool>> kFadeDetectionMap[] = {
    {"AUTO", std::nullopt},
    {"ON",   true},
    {"OFF",  false},
  };
  if (auto v = MapString(FadeDetectionData, kFadeDetectionMap)) {
    Context->EncoderParams.FadeDetection = *v;
  }

  // 13. RateControl
  static constexpr std::pair<std::string_view, mfxU16> kRateControlMap[] = {
    {"CBR",  MFX_RATECONTROL_CBR},
    {"VBR",  MFX_RATECONTROL_VBR},
    {"CQP",  MFX_RATECONTROL_CQP},
    {"AVBR", MFX_RATECONTROL_AVBR},
    {"ICQ",  MFX_RATECONTROL_ICQ},
    {"VCM",  MFX_RATECONTROL_VCM},
    {"QVBR", MFX_RATECONTROL_QVBR},
  };
  if (auto v = MapString(RateControlData, kRateControlMap)) {
    Context->EncoderParams.RateControl = *v;
  }

  // 14. DenoiseMode
  static constexpr std::pair<std::string_view, int> kDenoiseModeMap[] = {
    {"DEFAULT",                        0},
    {"AUTO | BDRATE | PRE ENCODE",     1},
    {"AUTO | ADJUST | POST ENCODE",    2},
    {"AUTO | SUBJECTIVE | PRE ENCODE", 3},
    {"MANUAL | PRE ENCODE",            4},
    {"MANUAL | POST ENCODE",           5},
  };
  if (auto v = MapString(DenoiseModeData, kDenoiseModeMap)) {
    Context->EncoderParams.VPPDenoiseMode = *v;
  }
  // MANUAL modes: set DenoiseStrength
  if (std::strcmp(DenoiseModeData, "MANUAL | PRE ENCODE") == 0 ||
      std::strcmp(DenoiseModeData, "MANUAL | POST ENCODE") == 0) {
    Context->EncoderParams.DenoiseStrength =
        static_cast<mfxU16>(DenoiseStrengthData);
  }

  // 15. ScalingMode
  static constexpr std::pair<std::string_view, std::optional<int>> kScalingModeMap[] = {
    {"OFF",                           std::nullopt},
    {"QUALITY | ADVANCED",            std::optional<int>(1)},
    {"VEBOX | ADVANCED",              std::optional<int>(2)},
    {"LOWPOWER | NEAREST NEIGHBOR",   std::optional<int>(3)},
    {"LOWPOWER | ADVANCED",           std::optional<int>(4)},
    {"AUTO",                          std::optional<int>(0)},
  };
  if (auto v = MapString(ScalingModeData, kScalingModeMap)) {
    Context->EncoderParams.VPPScalingMode = *v;
  }

  int64_t VPPOutWidthData = obs_data_get_int(Settings, "vpp_out_width");
  int64_t VPPOutHeightData = obs_data_get_int(Settings, "vpp_out_height");
  if (VPPOutWidthData > 0 && VPPOutHeightData > 0) {
    Context->EncoderParams.VPPOutWidth =
        static_cast<mfxU16>(VPPOutWidthData);
    Context->EncoderParams.VPPOutHeight =
        static_cast<mfxU16>(VPPOutHeightData);
  }

  // 16. ImageStabMode
  static constexpr std::pair<std::string_view, int> kImageStabModeMap[] = {
    {"UPSCALE", 1},
    {"BOXING",  2},
    {"AUTO",    0},
  };
  if (auto v = MapString(ImageStabModeData, kImageStabModeMap)) {
    Context->EncoderParams.VPPImageStabMode = *v;
  }

  std::string_view DetailSV(DetailData);
  if (DetailSV == "ON") {
    Context->EncoderParams.VPPDetail = DetailFactorData;
  } else if (DetailSV == "OFF") {
    Context->EncoderParams.VPPDetail = 0;
  }

  static constexpr std::pair<std::string_view, int> kPercEncPrefilterMap[] = {
    {"ON",  1},
    {"OFF", 0},
  };
  if (auto v = MapString(PercEncPrefilterData, kPercEncPrefilterMap)) {
    Context->EncoderParams.PercEncPrefilter = *v;
  }

  Context->EncoderParams.AsyncDepth =
      static_cast<mfxU16>(obs_data_get_int(Settings, "async_depth"));

  auto ActualCQPData = CQPData;
  bool CQPSeparateIPB = obs_data_get_bool(Settings, "cqp_separate_ipb");
  if (CQPSeparateIPB) {
    int QPIData = static_cast<int>(obs_data_get_int(Settings, "qpi"));
    int QPPData = static_cast<int>(obs_data_get_int(Settings, "qpp"));
    int QPBData = static_cast<int>(obs_data_get_int(Settings, "qpb"));
    if (Context->Codec == QSV_CODEC_VP9 || Context->Codec == QSV_CODEC_AV1) {
      QPIData *= 4;
      QPPData *= 4;
      QPBData *= 4;
    }
    Context->EncoderParams.QPI = static_cast<mfxU16>(QPIData);
    Context->EncoderParams.QPP = static_cast<mfxU16>(QPPData);
    Context->EncoderParams.QPB = static_cast<mfxU16>(QPBData);
  } else {
    if (Context->Codec == QSV_CODEC_VP9 || Context->Codec == QSV_CODEC_AV1) {
      ActualCQPData *= 4;
    }
    Context->EncoderParams.QPI = static_cast<mfxU16>(ActualCQPData);
    Context->EncoderParams.QPP = static_cast<mfxU16>(ActualCQPData);
    Context->EncoderParams.QPB = static_cast<mfxU16>(ActualCQPData);
  }

  Context->EncoderParams.TargetBitRate = TargetBitrateData;
  Context->EncoderParams.CustomBufferSize = CustomBufferSizeData;
  Context->EncoderParams.BufferSize = BufferSizeData;
  Context->EncoderParams.MaxBitRate = MaxBitrateData;
  Context->EncoderParams.Width = static_cast<mfxU16>(VideoWidth);
  Context->EncoderParams.Height = static_cast<mfxU16>(VideoHeight);
  Context->EncoderParams.FpsNum = static_cast<mfxU32>(VOI->fps_num);
  Context->EncoderParams.FpsDen = static_cast<mfxU32>(VOI->fps_den);

  Context->EncoderParams.BFrames = static_cast<mfxU16>(BFramesData);
  Context->EncoderParams.KeyIntSec = static_cast<mfxU16>(KeyIntervalData);
  Context->EncoderParams.ICQQuality = static_cast<mfxU16>(ICQQualityData);
  Context->EncoderParams.NumRefFrame = static_cast<mfxU16>(NumRefFrameData);
  Context->EncoderParams.NumRefActiveP = static_cast<mfxU16>(NumRefActivePData);
  Context->EncoderParams.NumRefActiveBL0 =
      static_cast<mfxU16>(NumRefActiveBL0Data);
  Context->EncoderParams.NumRefActiveBL1 =
      static_cast<mfxU16>(NumRefActiveBL1Data);

  Context->EncoderParams.IntraRefCycleSize =
      static_cast<mfxU16>(IntraRefCycleSizeData);
  Context->EncoderParams.IntraRefQPDelta =
      static_cast<mfxU16>(IntraRefQPDeltaData);

  Context->EncoderParams.QVBRQuality =
      static_cast<mfxU16>(obs_data_get_int(Settings, "qvbr_quality"));

  static constexpr std::pair<std::string_view, int> kScreenContentToolsMap[] = {
    {"AUTO", 0},
    {"OFF",  1},
    {"ON",   2},
  };
  if (auto v = MapString(ScreenContentToolsData, kScreenContentToolsMap)) {
    Context->EncoderParams.ScreenContentTools = *v;
  }

  Context->EncoderParams.TemporalLayersNum =
      static_cast<mfxU16>(TemporalLayersData);

  const char *CustomCodingOptionsData =
      obs_data_get_string(Settings, "custom_coding_options");
  if (CustomCodingOptionsData) {
    Context->EncoderParams.CustomCodingOptions = CustomCodingOptionsData;
  }

  Context->EncoderParams.MinQP = MinQPData ? MinQPData : "-1";
  Context->EncoderParams.MaxQP = MaxQPData ? MaxQPData : "-1";

  Context->EncoderParams.ProcessingEnable = false;
  if ((Context->EncoderParams.VPPDenoiseMode.has_value() ||
       Context->EncoderParams.VPPDetail.has_value() ||
       Context->EncoderParams.VPPScalingMode.has_value() ||
       Context->EncoderParams.VPPImageStabMode.has_value() ||
       Context->EncoderParams.PercEncPrefilter == true) &&
      std::strcmp(VideoProcessingStatusData, "ON") == 0) {
    if (VOI->format == VIDEO_FORMAT_NV12) {
      Context->EncoderParams.ProcessingEnable = true;
    } else if (VOI->format == VIDEO_FORMAT_P010 ||
               VOI->format == VIDEO_FORMAT_I444) {
      // P010 and AYUV(8-bit 4:4:4) are supported on all platforms
      Context->EncoderParams.ProcessingEnable = true;
    } else if (VOI->format == VIDEO_FORMAT_I412 ||
               VOI->format == VIDEO_FORMAT_P416) {
      // 12/16-bit 4:4:4 (Y410/Y416) requires TGL_LP (Gen12)+
      mfxU16 platformCode = QueryPlatformCodeName();
      bool highBitDepth444Supported = platformCode == 0 ||
                                      platformCode >= MFX_PLATFORM_TIGERLAKE;
      if (highBitDepth444Supported) {
        Context->EncoderParams.ProcessingEnable = true;
      } else {
        warn("VPP with %s is only supported on Tiger Lake+",
             VOI->format == VIDEO_FORMAT_I412 ? "I412" : "P416");
      }
    } else {
      warn("VPP is only available with NV12, P010, or 4:4:4 color format");
    }
  }

  switch (VOI->format) {
  default:
  case VIDEO_FORMAT_NV12:
    Context->EncoderParams.FourCC = MFX_FOURCC_NV12;
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    break;
  case VIDEO_FORMAT_P010:
    Context->EncoderParams.FourCC = MFX_FOURCC_P010;
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    break;
  case VIDEO_FORMAT_I444:
    Context->EncoderParams.FourCC = MFX_MAKEFOURCC('4','4','4','P');
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV444;
    break;
  case VIDEO_FORMAT_I412:
  case VIDEO_FORMAT_P416:
    Context->EncoderParams.FourCC = MFX_MAKEFOURCC('4','4','4','P');
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV444;
    Context->EncoderParams.BitDepth = 12;
    break;
  }

  // VP9 profile is tightly bound to input bit depth + chroma subsampling.
  // A mismatch (e.g. Profile 3 selected but input is 8-bit 4:2:0) makes the
  // encoder produce garbled bitstream with wrong chroma layout. Auto-correct
  // the profile to match the actual input format.
  if (Context->Codec == QSV_CODEC_VP9) {
    const bool vp9Is10bit = (Context->EncoderParams.BitDepth >= 10);
    const bool vp9Is444 =
        (Context->EncoderParams.ChromaFormat == MFX_CHROMAFORMAT_YUV444);
    mfxU16 vp9CorrectProfile = MFX_PROFILE_VP9_0;
    if (!vp9Is10bit && !vp9Is444) vp9CorrectProfile = MFX_PROFILE_VP9_0;
    else if (!vp9Is10bit && vp9Is444) vp9CorrectProfile = MFX_PROFILE_VP9_1;
    else if (vp9Is10bit && !vp9Is444) vp9CorrectProfile = MFX_PROFILE_VP9_2;
    else vp9CorrectProfile = MFX_PROFILE_VP9_3;

    if (Context->EncoderParams.CodecProfile != vp9CorrectProfile) {
      warn("\tVP9 profile auto-corrected: input is %u-bit %s, "
           "profile %u -> %u",
           vp9Is10bit ? 10u : 8u,
           vp9Is444 ? "4:4:4" : "4:2:0",
           Context->EncoderParams.CodecProfile, vp9CorrectProfile);
      Context->EncoderParams.CodecProfile = vp9CorrectProfile;
    }
  }

  info("\tDebug info:");
  info("\tCodec: %s", Codec);
  info("\tRate control: %s", RateControlData);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_QVBR)
    info("\tQVBR Quality: %d", Context->EncoderParams.QVBRQuality);

  if (Context->EncoderParams.RateControl != MFX_RATECONTROL_ICQ &&
      Context->EncoderParams.RateControl != MFX_RATECONTROL_CQP)
    info("\tTarget bitrate: %d", Context->EncoderParams.TargetBitRate);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_VBR ||
      Context->EncoderParams.RateControl == MFX_RATECONTROL_VCM)
    info("\tMax bitrate: %d", Context->EncoderParams.MaxBitRate);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_ICQ &&
      std::strcmp(RateControlData, "ICQ") == 0)
    info("\tICQ Quality: %d", Context->EncoderParams.ICQQuality);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_CQP) {
    if (CQPSeparateIPB) {
      info("\tQPI: %d, QPP: %d, QPB: %d",
           Context->EncoderParams.QPI,
           Context->EncoderParams.QPP,
           Context->EncoderParams.QPB);
    } else {
      info("\tCQP: %d", ActualCQPData);
    }
  }

  info("\tFPS numerator: %d", VOI->fps_num);
  info("\tFPS denominator: %d", VOI->fps_den);
  info("\tOutput width: %d", VideoWidth);
  info("\tOutput height: %d", VideoHeight);
}

// Forwarding function macros
// Reduce boilerplate for encoder-info function pointers that forward a codec enum
// to the shared implementation.

#define FORWARD_PARAM_PROPS(name, codec)                                        \
  static obs_properties_t *Get##name##ParamProps([[maybe_unused]] void *) {     \
    return GetParamProps(codec);                                                \
  }

#define FORWARD_FRAME_ENCODER(name, codec)                                      \
  static void *Init##name##FrameEncoder(obs_data_t *Settings,                   \
                                        obs_encoder_t *EncoderData) {           \
    return InitPluginContext(codec, Settings, EncoderData, false);               \
  }

#define FORWARD_TEXTURE_ENCODER(name, codec, fallback_id)                       \
  static void *Init##name##TextureEncoder(obs_data_t *Settings,                 \
                                          obs_encoder_t *EncoderData) {         \
    return InitTextureEncoder(codec, Settings, EncoderData, fallback_id);       \
  }

#define FORWARD_ENCODER_NAME(name, display)                                     \
  static const char *Get##name##EncoderName([[maybe_unused]] void *) {          \
    return display;                                                             \
  }

#define FORWARD_DEFAULT_PARAMS(name, codec)                                     \
  static void Set##name##DefaultParams(obs_data_t *Settings) {                  \
    SetDefaultEncoderParams(Settings, codec);                                   \
  }

FORWARD_PARAM_PROPS(H264, QSV_CODEC_AVC)
FORWARD_PARAM_PROPS(AV1, QSV_CODEC_AV1)
FORWARD_PARAM_PROPS(HEVC, QSV_CODEC_HEVC)
FORWARD_PARAM_PROPS(VP9, QSV_CODEC_VP9)

FORWARD_FRAME_ENCODER(H264, QSV_CODEC_AVC)
FORWARD_FRAME_ENCODER(AV1, QSV_CODEC_AV1)
FORWARD_FRAME_ENCODER(HEVC, QSV_CODEC_HEVC)
FORWARD_FRAME_ENCODER(VP9, QSV_CODEC_VP9)

FORWARD_TEXTURE_ENCODER(H264, QSV_CODEC_AVC, "obs_qsv_vpl_h264")
FORWARD_TEXTURE_ENCODER(AV1, QSV_CODEC_AV1, "obs_qsv_vpl_av1")
FORWARD_TEXTURE_ENCODER(HEVC, QSV_CODEC_HEVC, "obs_qsv_vpl_hevc")
FORWARD_TEXTURE_ENCODER(VP9, QSV_CODEC_VP9, "obs_qsv_vpl_vp9")

FORWARD_ENCODER_NAME(H264, "QuickSync oneVPL H.264")
FORWARD_ENCODER_NAME(AV1, "QuickSync oneVPL AV1")
FORWARD_ENCODER_NAME(HEVC, "QuickSync oneVPL HEVC")
FORWARD_ENCODER_NAME(VP9, "QuickSync oneVPL VP9")

FORWARD_DEFAULT_PARAMS(H264, QSV_CODEC_AVC)
FORWARD_DEFAULT_PARAMS(AV1, QSV_CODEC_AV1)
FORWARD_DEFAULT_PARAMS(HEVC, QSV_CODEC_HEVC)
FORWARD_DEFAULT_PARAMS(VP9, QSV_CODEC_VP9)

plugin_context *InitPluginContext(enum codec_enum Codec, obs_data_t *Settings,
                                  obs_encoder_t *EncoderData,
                                  bool IsTextureEncoder) {

  plugin_context *Context = new plugin_context;

  Context->EncoderData = EncoderData;
  Context->Codec = Codec;

  video_t *Video = obs_encoder_video(Context->EncoderData);
  const video_output_info *VOI = video_output_get_info(Video);
  switch (VOI->format) {
  case VIDEO_FORMAT_I010:
  case VIDEO_FORMAT_P010:
  case VIDEO_FORMAT_I210:
    Context->EncoderParams.BitDepth = 10;
    break;
  case VIDEO_FORMAT_I412:
    Context->EncoderParams.BitDepth = 12;
    break;
  case VIDEO_FORMAT_P416:
    Context->EncoderParams.BitDepth = 16;
    break;
  default:
    Context->EncoderParams.BitDepth = 0;
    if (VOI->colorspace == VIDEO_CS_2100_PQ ||
        VOI->colorspace == VIDEO_CS_2100_HLG) {
      auto ErrorText = obs_module_text("8bitUnsupportedHdr");
      obs_encoder_set_last_error(Context->EncoderData, ErrorText);
      blog(LOG_ERROR, "%s", ErrorText);

      delete Context;

      return nullptr;
    }
  }

  GetEncoderParams(Context, Settings);

  try {

    static std::mutex InitMutex;
    std::lock_guard<std::mutex> lock(InitMutex);
    if (!OpenEncoder(Context->EncoderPTR, &Context->EncoderParams,
                     Context->Codec, IsTextureEncoder)) {
      blog(LOG_WARNING, "QSV failed to init encoder.");

      delete Context;

      return nullptr;
    }

    GetEncoderVersion(&VPLVersionMajor, &VPLVersionMinor);

    info("\tLibVPL version: %d.%d", VPLVersionMajor, VPLVersionMinor);

    // Register encoder AFTER params are initialized, so ROI global config
    // can be applied with the correct output resolution (Width/Height).
    // Must also be after OpenEncoder to avoid SetEncoderParams inside Init()
    // from clearing CachedROIRegions (which happens when ROIEnabled is false).
    RegisterEncoderData(Context->EncoderData, Context);

    Context->PerformanceToken = os_request_high_performance("qsv encoding");

    return Context;
  } catch (const std::exception &e) {
    blog(LOG_WARNING, "QSV failed to load. %s", e.what());

    delete Context;

    return nullptr;
  }
}

static void *InitTextureEncoder(enum codec_enum Codec, obs_data_t *Settings,
                                obs_encoder_t *EncoderData,
                                const char *FallbackID) {
  struct obs_video_info OVI {};
  obs_get_video_info(&OVI);

  if (!AdaptersInfo[OVI.adapter].IsIntel) {
    info(">>> app not on intel GPU, fall back to non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       FallbackID);
  }

  if (static_cast<int>(obs_data_get_int(Settings, "gpu_number")) > 0) {
    info(">>> custom GPU is selected. OBS Studio does not support "
         "transferring textures to third-party adapters, fall back to "
         "non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       FallbackID);
  }

#if !defined(_WIN32) || !defined(_WIN64)
  info(">>> unsupported platform for texture encode");
  return obs_encoder_create_rerouted(EncoderData,
                                     FallbackID);
#endif

  if (Codec == QSV_CODEC_AV1 && !AdaptersInfo[OVI.adapter].SupportAV1) {
    info(">>> cap on different device, fall back to non-texture "
         "sharing AV1 qsv encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       FallbackID);
  }

  if (Codec == QSV_CODEC_VP9 && !AdaptersInfo[OVI.adapter].SupportVP9) {
    info(">>> cap on different device, fall back to non-texture "
         "sharing VP9 qsv encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       FallbackID);
  }

  bool TextureEncodeSupport = obs_nv12_tex_active();

  if (Codec != QSV_CODEC_AVC)
    TextureEncodeSupport = TextureEncodeSupport || obs_p010_tex_active();

  if (!TextureEncodeSupport) {
    info(">>> gpu tex not active, fall back to non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       FallbackID);
  }

  if (obs_encoder_scaling_enabled(EncoderData)) {
    if (!obs_encoder_gpu_scaling_enabled(EncoderData)) {
      info(">>> encoder CPU scaling active, fall back to non-texture encoder");
      return obs_encoder_create_rerouted(EncoderData,
                                         FallbackID);
    }
    info(">>> encoder GPU scaling active");
  }

  info(">>> Texture encoder");
  plugin_context *Context = InitPluginContext(Codec, Settings, EncoderData, true);
  if (!Context) {
    info(">>> texture encoder init failed, fall back to non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       FallbackID);
  }
  return Context;
}

obs_encoder_info H264FrameEncoderInfo = {.id = "obs_qsv_vpl_h264",
                                         .type = OBS_ENCODER_VIDEO,
                                         .codec = "h264",
                                         .get_name = GetH264EncoderName,
                                         .create = InitH264FrameEncoder,
                                         .destroy = DestroyPluginContext,
                                         .encode = EncodeFrame,
                                         .get_defaults = SetH264DefaultParams,
                                         .get_properties = GetH264ParamProps,
                                         .update = UpdateEncoderParams,
                                         .get_extra_data = GetExtraData,
                                         .get_sei_data = GetSEIData,
                                         .get_video_info = GetVideoInfo,
                                         .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                 OBS_ENCODER_CAP_INTERNAL |
                                                 OBS_ENCODER_CAP_ROI};

obs_encoder_info H264TextureEncoderInfo = {.id = "obs_qsv_vpl_h264_tex",
                                           .type = OBS_ENCODER_VIDEO,
                                           .codec = "h264",
                                           .get_name = GetH264EncoderName,
                                           .create = InitH264TextureEncoder,
                                           .destroy = DestroyPluginContext,
                                           .get_defaults = SetH264DefaultParams,
                                           .get_properties = GetH264ParamProps,
                                           .update = UpdateEncoderParams,
                                           .get_extra_data = GetExtraData,
                                           .get_sei_data = GetSEIData,
                                           .get_video_info = GetVideoInfo,
                                           .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                   OBS_ENCODER_CAP_PASS_TEXTURE |
                                                   OBS_ENCODER_CAP_ROI,
                                           .encode_texture2 = EncodeTexture};

obs_encoder_info AV1FrameEncoderInfo = {.id = "obs_qsv_vpl_av1",
                                        .type = OBS_ENCODER_VIDEO,
                                        .codec = "av1",
                                        .get_name = GetAV1EncoderName,
                                        .create = InitAV1FrameEncoder,
                                        .destroy = DestroyPluginContext,
                                        .encode = EncodeFrame,
                                        .get_defaults = SetAV1DefaultParams,
                                        .get_properties = GetAV1ParamProps,
                                        .update = UpdateEncoderParams,
                                        .get_extra_data = GetExtraData,
                                        .get_sei_data = GetSEIData,
                                        .get_video_info = GetVideoInfo,
                                        .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                OBS_ENCODER_CAP_INTERNAL |
                                                OBS_ENCODER_CAP_ROI};

obs_encoder_info AV1TextureEncoderInfo = {.id = "obs_qsv_vpl_av1_tex",
                                          .type = OBS_ENCODER_VIDEO,
                                          .codec = "av1",
                                          .get_name = GetAV1EncoderName,
                                          .create = InitAV1TextureEncoder,
                                          .destroy = DestroyPluginContext,
                                          .get_defaults = SetAV1DefaultParams,
                                          .get_properties = GetAV1ParamProps,
                                          .update = UpdateEncoderParams,
                                          .get_extra_data = GetExtraData,
                                          .get_sei_data = GetSEIData,
                                          .get_video_info = GetVideoInfo,
                                          .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                  OBS_ENCODER_CAP_PASS_TEXTURE |
                                                  OBS_ENCODER_CAP_ROI,
                                          .encode_texture2 = EncodeTexture};

obs_encoder_info HEVCFrameEncoderInfo = {.id = "obs_qsv_vpl_hevc",
                                         .type = OBS_ENCODER_VIDEO,
                                         .codec = "hevc",
                                         .get_name = GetHEVCEncoderName,
                                         .create = InitHEVCFrameEncoder,
                                         .destroy = DestroyPluginContext,
                                         .encode = EncodeFrame,
                                         .get_defaults = SetHEVCDefaultParams,
                                         .get_properties = GetHEVCParamProps,
                                         .update = UpdateEncoderParams,
                                         .get_extra_data = GetExtraData,
                                         .get_sei_data = GetSEIData,
                                         .get_video_info = GetVideoInfo,
                                         .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                 OBS_ENCODER_CAP_INTERNAL |
                                                 OBS_ENCODER_CAP_ROI};

obs_encoder_info HEVCTextureEncoderInfo = {.id = "obs_qsv_vpl_hevc_tex",
                                           .type = OBS_ENCODER_VIDEO,
                                           .codec = "hevc",
                                           .get_name = GetHEVCEncoderName,
                                           .create = InitHEVCTextureEncoder,
                                           .destroy = DestroyPluginContext,
                                           .get_defaults = SetHEVCDefaultParams,
                                           .get_properties = GetHEVCParamProps,
                                           .update = UpdateEncoderParams,
                                           .get_extra_data = GetExtraData,
                                           .get_sei_data = GetSEIData,
                                           .get_video_info = GetVideoInfo,
                                           .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                   OBS_ENCODER_CAP_PASS_TEXTURE |
                                                   OBS_ENCODER_CAP_ROI,
                                           .encode_texture2 = EncodeTexture};

// VP9 has no ROI support; omit OBS_ENCODER_CAP_ROI from its caps.
obs_encoder_info VP9FrameEncoderInfo = {.id = "obs_qsv_vpl_vp9",
                                        .type = OBS_ENCODER_VIDEO,
                                        .codec = "vp9",
                                        .get_name = GetVP9EncoderName,
                                        .create = InitVP9FrameEncoder,
                                        .destroy = DestroyPluginContext,
                                        .encode = EncodeFrame,
                                        .get_defaults = SetVP9DefaultParams,
                                        .get_properties = GetVP9ParamProps,
                                        .update = UpdateEncoderParams,
                                        .get_extra_data = GetExtraData,
                                        .get_sei_data = GetSEIData,
                                        .get_video_info = GetVideoInfo,
                                        .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                OBS_ENCODER_CAP_INTERNAL};

obs_encoder_info VP9TextureEncoderInfo = {.id = "obs_qsv_vpl_vp9_tex",
                                          .type = OBS_ENCODER_VIDEO,
                                          .codec = "vp9",
                                          .get_name = GetVP9EncoderName,
                                          .create = InitVP9TextureEncoder,
                                          .destroy = DestroyPluginContext,
                                          .get_defaults = SetVP9DefaultParams,
                                          .get_properties = GetVP9ParamProps,
                                          .update = UpdateEncoderParams,
                                          .get_extra_data = GetExtraData,
                                          .get_sei_data = GetSEIData,
                                          .get_video_info = GetVideoInfo,
                                          .caps = OBS_ENCODER_CAP_DYN_BITRATE |
                                                  OBS_ENCODER_CAP_PASS_TEXTURE,
                                          .encode_texture2 = EncodeTexture};