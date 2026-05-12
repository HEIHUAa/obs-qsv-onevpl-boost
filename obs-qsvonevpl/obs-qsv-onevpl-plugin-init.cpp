

#pragma warning(disable : 4996)

#ifndef __QSV_VPL_ENCODER_H__
#include "obs-qsv-onevpl-encoder.hpp"
#endif

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
    {"enc_tools", MFX_PLATFORM_DG2},
    {"tune_quality", MFX_PLATFORM_TIGERLAKE},
    {"transform_skip", MFX_PLATFORM_TIGERLAKE},
    {"win_brc", MFX_PLATFORM_TIGERLAKE},
    {nullptr, 0}};

static mfxU16 QueryPlatformCodeName();

static bool IsFeatureSupported(const char *PropertyName) {
    mfxU16 platformCode = QueryPlatformCodeName();
    if (platformCode == 0) {
        return true;
    }
    const struct qsv_feature_info *info = qsv_feature_info_list;
    while (info->property_name) {
        if (std::strcmp(info->property_name, PropertyName) == 0) {
            return platformCode >= info->min_platform;
        }
        info++;
  }
  return true;
}

static mfxPlatform CachedQSVPlatform{};
static bool CachedQSVPlatformValid = false;

static bool TryQueryPlatformCodeName(mfxLoader Loader,
                                     const char *ImplName) {
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

    if (ImplName != nullptr) {
        Config = MFXCreateConfig(Loader);
        Variant.Type = MFX_VARIANT_TYPE_PTR;
        Variant.Data.Ptr = mfxHDL(ImplName);
        MFXSetConfigFilterProperty(
            Config,
            reinterpret_cast<const mfxU8 *>("mfxImplDescription.ImplName"),
            Variant);
    }

    mfxSession Session{};
    mfxStatus Status = MFXCreateSession(Loader, 0, &Session);
    if (Status >= MFX_ERR_NONE) {
        MFXVideoCORE_QueryPlatform(Session, &CachedQSVPlatform);
        MFXClose(Session);
        CachedQSVPlatformValid = true;
        return true;
    }
    return false;
}

static mfxU16 QueryPlatformCodeName() {
    if (CachedQSVPlatformValid) {
        return CachedQSVPlatform.CodeName;
    }

    // Try mfx-gen first (newer hardware)
    {
        mfxLoader Loader = MFXLoad();
        if (Loader != nullptr) {
            if (TryQueryPlatformCodeName(Loader, "mfx-gen")) {
                MFXUnload(Loader);
                return CachedQSVPlatform.CodeName;
            }
            MFXUnload(Loader);
        }
    }

    // Fallback: try mfx-msdk for legacy hardware
    {
        mfxLoader Loader = MFXLoad();
        if (Loader != nullptr) {
            if (TryQueryPlatformCodeName(Loader, "mfx-msdk")) {
                MFXUnload(Loader);
                return CachedQSVPlatform.CodeName;
            }
            MFXUnload(Loader);
        }
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
  obs_data_set_default_string(Settings, "profile",
                              Codec == QSV_CODEC_AVC ? "high" : "main");
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
  obs_data_set_default_int(Settings, "gop_ref_dist", 4);
  obs_data_set_default_int(Settings, "async_depth", 4);

  obs_data_set_default_string(Settings, "intra_ref_encoding", "OFF");
  obs_data_set_default_string(Settings, "low_delay_brc", "OFF");
  obs_data_set_default_string(Settings, "low_delay_hrd", "OFF");

  obs_data_set_default_string(Settings, "tune_quality", "OFF");
  obs_data_set_default_string(Settings, "adaptive_i", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_b", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_ref", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_cqm", "ON");
  obs_data_set_default_string(Settings, "adaptive_ltr", "OFF");
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

  obs_data_set_default_string(Settings, "win_brc", "ON");
  obs_data_set_default_int(Settings, "win_brc_max_avg_size", 0);
  obs_data_set_default_int(Settings, "win_brc_size", 0);

  obs_data_set_default_int(Settings, "qvbr_quality", 0);

  obs_data_set_default_string(Settings, "lookahead", "OFF");
  obs_data_set_default_string(Settings, "lookahead_latency", "NORMAL");
  obs_data_set_default_string(Settings, "lookahead_ds", "MEDIUM");
  obs_data_set_default_string(Settings, "extbrc", "OFF");
  obs_data_set_default_string(Settings, "enctools", "OFF");
  obs_data_set_default_string(Settings, "hevc_sao", "AUTO");
  obs_data_set_default_string(Settings, "hevc_gpb", "AUTO");

  obs_data_set_default_string(Settings, "intra_ref_encoding", "OFF");
  obs_data_set_default_string(Settings, "intra_ref_type", "VERTICAL");
  obs_data_set_default_int(Settings, "intra_ref_cycle_size", 2);
  obs_data_set_default_int(Settings, "intra_ref_qp_delta", 0);

  obs_data_set_default_string(Settings, "vpp", "OFF");
  obs_data_set_default_string(Settings, "vpp_mode", "PRE ENC");
  obs_data_set_default_string(Settings, "denoise_mode", "OFF");
  obs_data_set_default_int(Settings, "denoise_strength", 50);
  obs_data_set_default_string(Settings, "detail", "OFF");
  obs_data_set_default_int(Settings, "detail_factor", 50);
  obs_data_set_default_string(Settings, "image_stab_mode", "OFF");
  obs_data_set_default_string(Settings, "scaling_mode", "OFF");
  obs_data_set_default_string(Settings, "perc_enc_prefilter", "OFF");

  obs_data_set_default_string(Settings, "scenario_info", "AUTO");
  obs_data_set_default_string(Settings, "content_info", "AUTO");
  obs_data_set_default_string(Settings, "transform_skip", "OFF");
  obs_data_set_default_string(Settings, "fade_detection", "ON");
  obs_data_set_default_string(Settings, "bitrate_limit", "ON");

  obs_data_set_default_string(Settings, "screen_content_tools", "AUTO");

  obs_data_set_default_int(Settings, "temporal_layers", 0);

  obs_data_set_default_int(Settings, "gpu_number", 0);
}

static inline const char *LocaleKey(const char *str) {
  static char buf[128];
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
  Prop = obs_properties_get(Properties, "cqp_separate_ipb");
  if (Prop)
    obs_property_set_visible(Prop, bVisible);

  bVisible = bIsICQ;
  Prop = obs_properties_get(Properties, "icq_quality");
  obs_property_set_visible(Prop, bVisible);

  bVisible = (bIsCBR || bIsVBR || bIsAVBR || bIsVCM || bIsQVBR);
  Prop = obs_properties_get(Properties, "enctools");
  if (bVisible) bVisible = IsFeatureSupported("enc_tools");
  obs_property_set_visible(Prop, bVisible);
  Prop = obs_properties_get(Properties, "extbrc");
  obs_property_set_visible(Prop, bVisible);

  bVisible = (bIsCBR || bIsVBR || bIsAVBR || bIsVCM || bIsQVBR) &&
             IsFeatureSupported("win_brc");
  const char *win_brc = obs_data_get_string(Settings, "win_brc");
  bool bVisibleWinBRC = (std::strcmp(win_brc, "ON") == 0);

  Prop = obs_properties_get(Properties, "win_brc");
  if (Prop) obs_property_set_visible(Prop, bVisible);

  bVisible = bVisible && bVisibleWinBRC;
  Prop = obs_properties_get(Properties, "win_brc_max_avg_size");
  obs_property_set_visible(Prop, bVisible);
  Prop = obs_properties_get(Properties, "win_brc_size");
  obs_property_set_visible(Prop, bVisible);

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
  obs_property_set_visible(
      Prop, bVisible && (bVisible_lookahead_hq || bVisible_lookahead_lp));

  Prop = obs_properties_get(Properties, "lookahead_latency");
  obs_property_set_visible(Prop, bVisible && bVisible_lookahead_hq);

  Prop = obs_properties_get(Properties, "la_depth");
  obs_property_set_visible(Prop, bVisible && bVisible_lookahead_hq);

  if (bVisible_lookahead_lp) {
    obs_data_set_string(Settings, "enctools", "OFF");
  }

  bVisible = bIsCBR || bIsVBR || bIsAVBR || bIsVCM || bIsQVBR || bIsICQ;
  Prop = obs_properties_get(Properties, "mbbrc");
  obs_property_set_visible(Prop, bVisible);
  if (!bVisible) {
    obs_data_set_string(Settings, "mbbrc", "OFF");
  }

  bool use_bufsize = obs_data_get_bool(Settings, "custom_buffer_size");
  Prop = obs_properties_get(Properties, "buffer_size");
  obs_property_set_visible(Prop, use_bufsize);

  const char *hrd_conformance =
      obs_data_get_string(Settings, "hrd_conformance");
  bVisible = std::strcmp(hrd_conformance, "ON") == 0 ||
             std::strcmp(hrd_conformance, "AUTO") == 0;
  Prop = obs_properties_get(Properties, "low_delay_hrd");
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
  Prop = obs_properties_get(Properties, "vpp_mode");
  obs_property_set_visible(Prop, bVisibleVPP);
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

  //const char *extbrc = obs_data_get_string(Settings, "extbrc");
  //const char *enctools = obs_data_get_string(Settings, "enctools");
  //bool bVisible_extbrc = std::strcmp(extbrc, "OFF") == 0;
  //bool bVisible_enctools = std::strcmp(enctools, "OFF") == 0;

  //Prop = obs_properties_get(Properties, "num_ref_active_p");
  //obs_property_set_visible(Prop, bVisible_extbrc && bVisible_enctools);
  //Prop = obs_properties_get(Properties, "num_ref_active_bl0");
  //obs_property_set_visible(Prop, bVisible_extbrc && bVisible_enctools);
  //Prop = obs_properties_get(Properties, "num_ref_active_bl1");
  //obs_property_set_visible(Prop, bVisible_extbrc && bVisible_enctools);

  mfxU16 platformCode = QueryPlatformCodeName();
  bool hasHighTier = platformCode == 0 ||
                     platformCode >= MFX_PLATFORM_TIGERLAKE;
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

  Prop = obs_properties_add_list(Props, "rate_control", TEXT_RATE_CONTROL,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

  mfxU16 platformCode = QueryPlatformCodeName();
  const struct qsv_rate_control_info *rcInfo = qsv_rate_control_info_list;
  while (rcInfo->name) {
    if (platformCode == 0 || platformCode >= rcInfo->min_platform) {
      bool skipForAV1 = Codec == QSV_CODEC_AV1 &&
                        std::strcmp(rcInfo->name, "VCM") == 0;
      if (!skipForAV1) {
        obs_property_list_add_string(Prop, rcInfo->name, rcInfo->name);
      }
    }
    rcInfo++;
  }

  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "target_usage", TEXT_SPEED,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_usage_names);

  // ── Profile ─────────────────────────────────────────────────
  Prop = obs_properties_add_list(Props, "profile", TEXT_PROFILE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

  if (Codec == QSV_CODEC_AVC) {
    mfxU16 platformCode = QueryPlatformCodeName();
    const char *const *profileEntryH264 = qsv_profile_names_h264;
    while (*profileEntryH264) {
      bool showProfileH264 = true;
      if (platformCode != 0) {
        bool isHigh10 = std::strcmp(*profileEntryH264, "high10") == 0;
        bool isHigh422 = std::strcmp(*profileEntryH264, "high422") == 0;
        if (isHigh10 || isHigh422) {
          showProfileH264 = false;
        }
      }
      if (showProfileH264) {
        obs_property_list_add_string(Prop, *profileEntryH264,
                                     *profileEntryH264);
      }
      profileEntryH264++;
    }
  } else if (Codec == QSV_CODEC_AV1) {
    AddStrings(Prop, qsv_profile_names_av1);
  } else if (Codec == QSV_CODEC_HEVC) {
    mfxU16 platformCode = QueryPlatformCodeName();
    const char *const *profileEntryHEVC = qsv_profile_names_hevc;
    while (*profileEntryHEVC) {
      bool showProfileHEVC = true;
      if (platformCode != 0) {
        bool isRext = std::strcmp(*profileEntryHEVC, "rext") == 0;
        bool isSCC = std::strcmp(*profileEntryHEVC, "scc") == 0;
        if ((isRext || isSCC) && platformCode < MFX_PLATFORM_ICELAKE) {
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
    Prop =
        obs_properties_add_list(Props, "hevc_tier", TEXT_HEVC_TIER,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    mfxU16 platformCode = QueryPlatformCodeName();
    bool hasHighTier = platformCode == 0 ||
                       platformCode >= MFX_PLATFORM_TIGERLAKE;
    const char *const *tierEntry = qsv_profile_tiers_hevc;
    while (*tierEntry) {
      bool isHigh = std::strcmp(*tierEntry, "high") == 0;
      if (!isHigh || hasHighTier) {
        obs_property_list_add_string(Prop, *tierEntry, *tierEntry);
      }
      tierEntry++;
    }

    Prop =
        obs_properties_add_list(Props, "hevc_level", TEXT_HEVC_LEVEL,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    const char *const *levelEntry = qsv_levels_hevc;
    while (*levelEntry) {
      obs_property_list_add_string(Prop, *levelEntry, *levelEntry);
      levelEntry++;
    }
  }

  if (Codec == QSV_CODEC_AVC) {
    Prop =
        obs_properties_add_list(Props, "avc_level", TEXT_HEVC_LEVEL,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    const char *const *levelEntry = qsv_levels_avc;
    while (*levelEntry) {
      obs_property_list_add_string(Prop, *levelEntry, *levelEntry);
      levelEntry++;
    }
  }

  if (Codec == QSV_CODEC_AV1) {
    Prop =
        obs_properties_add_list(Props, "av1_level", TEXT_HEVC_LEVEL,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    const char *const *levelEntry = qsv_levels_av1;
    while (*levelEntry) {
      obs_property_list_add_string(Prop, *levelEntry, *levelEntry);
      levelEntry++;
    }
  }

  // ── Quality settings ────────────────────────────────────────
  Prop = obs_properties_add_int_slider(Props, "qvbr_quality", TEXT_QVBR_QUALITY, 0, 51,
                                1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("QVBRQuality.Tooltip"));

  obs_properties_add_int_slider(Props, "icq_quality", TEXT_ICQ_QUALITY, 1, 51, 1);

  obs_properties_add_int_slider(Props, "cqp", "CQP", 1,
                         Codec == QSV_CODEC_AV1 ? 63 : 51, 1);

  Prop = obs_properties_add_bool(Props, "cqp_separate_ipb",
                                 "Separate I/P/B QP");
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  obs_properties_add_int_slider(Props, "qpi", "QPI", 1,
                         Codec == QSV_CODEC_AV1 ? 63 : 51, 1);
  obs_properties_add_int_slider(Props, "qpp", "QPP", 1,
                         Codec == QSV_CODEC_AV1 ? 63 : 51, 1);
  obs_properties_add_int_slider(Props, "qpb", "QPB", 1,
                         Codec == QSV_CODEC_AV1 ? 63 : 51, 1);

  // ── Bitrate ─────────────────────────────────────────────────
  Prop = obs_properties_add_int(Props, "bitrate", TEXT_TARGET_BITRATE, 50,
                                2147483647, 5000);
  obs_property_int_set_suffix(Prop, " Kbps");

  Prop = obs_properties_add_int(Props, "max_bitrate", TEXT_MAX_BITRATE, 50,
                                2147483647, 5000);
  obs_property_int_set_suffix(Prop, " Kbps");

  Prop = obs_properties_add_bool(Props, "custom_buffer_size",
                                 TEXT_CUSTOM_BUFFER_SIZE);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  Prop = obs_properties_add_int(Props, "buffer_size", TEXT_BUFFER_SIZE, 0,
                                2147483647, 5000);
  obs_property_int_set_suffix(Prop, " KB");

  // ── Frame structure ─────────────────────────────────────────
  Prop = obs_properties_add_int(Props, "keyint_sec", TEXT_KEYINT_SEC, 0, 20, 1);
  obs_property_int_set_suffix(Prop, " s");

  obs_properties_add_int_slider(Props, "num_ref_frame", TEXT_NUM_REF_FRAME, 0,
                                ((Codec == QSV_CODEC_AV1)   ? 16
                                 : (Codec == QSV_CODEC_AVC) ? 15
                                                            : 16),
                                1);

  Prop =
      obs_properties_add_int_slider(Props, "gop_ref_dist", TEXT_GOP_REF_DIST, 0,
                                    32, 1);
  obs_property_set_long_description(
      Prop, TEXT_GOP_REF_DIST_DESC);
  obs_property_long_description(Prop);

  // ── Lookahead ───────────────────────────────────────────────
  Prop = obs_properties_add_list(Props, "lookahead", TEXT_LA,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_lookahead_mode);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "lookahead_ds", TEXT_LA_DS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_lookahead_ds);
  obs_property_set_long_description(
      Prop, TEXT_LA_DS_DESC);

  obs_properties_add_int_slider(Props, "la_depth", TEXT_LA_DEPTH, 1, 100, 1);

  Prop = obs_properties_add_list(Props, "lookahead_latency", TEXT_LA_LATENCY,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_lookahead_latency);

  // ── WinBRC ──────────────────────────────────────────────────
  Prop = obs_properties_add_list(Props, "win_brc", TEXT_WINBRC,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(
      Prop, obs_module_text("WinBRC.Tooltip"));
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int(Props, "win_brc_max_avg_size",
                                TEXT_WINBRC_MAX_AVG_SIZE, 0, 65535, 1);
  obs_property_int_set_suffix(Prop, " kbps");
  obs_property_set_long_description(Prop,
                                    obs_module_text("WinBRCMaxAvgSize.Tooltip"));

  Prop = obs_properties_add_int(Props, "win_brc_size", TEXT_WINBRC_SIZE, 0, 1000,
                                1);
  obs_property_int_set_suffix(Prop, " frames");
  obs_property_set_long_description(Prop,
                                    obs_module_text("WinBRCSize.Tooltip"));

  // ── Rate control refinements ────────────────────────────────
  Prop = obs_properties_add_list(Props, "hrd_conformance", TEXT_HRD_CONFORMANCE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_HRD_CONFORMANCE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "low_delay_hrd", TEXT_LOW_DELAY_HRD,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "mbbrc", TEXT_MBBRC,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_long_description(
      Prop, TEXT_MBBRC_DESC);

  Prop = obs_properties_add_list(Props, "extbrc", TEXT_EXT_BRC,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(
      Prop, TEXT_EXT_BRC_DESC);
  AddStrings(Prop, qsv_params_condition_extbrc);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "enctools", TEXT_ENC_TOOLS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_DESC);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_visible(Prop, IsFeatureSupported("enc_tools"));
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  if (Codec == QSV_CODEC_AV1) {
    Prop =
        obs_properties_add_list(Props, "tune_quality", TEXT_TUNE_QUALITY_MODE,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tune_quality);
    obs_property_set_long_description(
        Prop, TEXT_TUNE_QUALITY_DESC);
    obs_property_set_visible(Prop, IsFeatureSupported("tune_quality"));
  }

  // ── Encoder hardware ────────────────────────────────────────
  Prop = obs_properties_add_list(Props, "low_power", TEXT_LOW_POWER,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_long_description(
      Prop, TEXT_LOW_POWER_DESC);

  obs_properties_add_int(Props, "async_depth", TEXT_ASYNC_DEPTH, 1, 1000, 1);

  // ── Advanced features ───────────────────────────────────────
  Prop = obs_properties_add_list(Props, "adaptive_i", TEXT_ADAPTIVE_I,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_ADAPTIVE_I_DESC);

  Prop = obs_properties_add_list(Props, "adaptive_b", TEXT_ADAPTIVE_B,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_ADAPTIVE_B_DESC);

  Prop = obs_properties_add_list(Props, "adaptive_ref", TEXT_ADAPTIVE_REF,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_ADAPTIVE_REF_DESC);

  Prop = obs_properties_add_list(Props, "adaptive_cqm", TEXT_ADAPTIVE_CQM,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_ADAPTIVE_CQM_DESC);

  Prop = obs_properties_add_list(Props, "adaptive_ltr", TEXT_ADAPTIVE_LTR,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_ADAPTIVE_LTR_DESC);

  Prop = obs_properties_add_list(Props, "p_pyramid", TEXT_P_PYRAMID,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_p_pyramid);
  obs_property_set_long_description(
      Prop, TEXT_P_PYRAMID_DESC);

  Prop = obs_properties_add_list(Props, "use_raw_ref", TEXT_USE_RAW_REF,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_USE_RAW_REF_DESC);

  Prop = obs_properties_add_list(Props, "global_motion_bias_adjustment",
                                 TEXT_GLOBAL_MOTION_BIAS_ADJUSTMENT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_long_description(
      Prop, TEXT_GLOBAL_MOTION_BIAS_DESC);

  Prop = obs_properties_add_list(Props, "mv_cost_scaling_factor",
                                 TEXT_MV_COST_SCALING_FACTOR,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_mv_cost_scaling);

  Prop = obs_properties_add_list(Props, "direct_bias_adjustment",
                                 TEXT_DIRECT_BIAS_ADJUSTMENT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_DIRECT_BIAS_DESC);

  Prop = obs_properties_add_list(Props, "mv_overpic_boundaries",
                                 TEXT_MV_OVER_PIC_BOUNDARIES,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_MV_OVER_PIC_BOUNDARIES_DESC);

  Prop = obs_properties_add_list(Props, "trellis", TEXT_TRELLIS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_trellis);
  obs_property_set_long_description(
      Prop, TEXT_TRELLIS_DESC);

  Prop = obs_properties_add_list(Props, "rdo", TEXT_RDO, OBS_COMBO_TYPE_LIST,
                                 OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_RDO_DESC);

  Prop = obs_properties_add_list(Props, "fade_detection", TEXT_FADE_DETECTION,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_FADE_DETECTION_DESC);

  Prop = obs_properties_add_list(Props, "bitrate_limit", TEXT_BITRATE_LIMIT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_BITRATE_LIMIT_DESC);

  Prop = obs_properties_add_list(Props, "transform_skip", TEXT_TRANSFORM_SKIP,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_TRANSFORM_SKIP_DESC);
  obs_property_set_visible(Prop, IsFeatureSupported("transform_skip"));

  // ── Reference controls ──────────────────────────────────────
  Prop = obs_properties_add_int_slider(Props, "num_ref_active_p",
                                       TEXT_NUM_REF_ACTIVE_P, 0,
                                       ((Codec == QSV_CODEC_AV1)   ? 16
                                        : (Codec == QSV_CODEC_AVC) ? 15
                                                                   : 16),
                                       1);
  obs_property_set_long_description(
      Prop, TEXT_NUM_REF_ACTIVE_P_DESC);

  Prop = obs_properties_add_int_slider(Props, "num_ref_active_bl0",
                                       TEXT_NUM_REF_ACTIVE_BL0, 0,
                                       ((Codec == QSV_CODEC_AV1)   ? 16
                                        : (Codec == QSV_CODEC_AVC) ? 15
                                                                   : 16),
                                       1);
  obs_property_set_long_description(
      Prop, TEXT_NUM_REF_ACTIVE_BL0_DESC);

  Prop = obs_properties_add_int_slider(Props, "num_ref_active_bl1",
                                       TEXT_NUM_REF_ACTIVE_BL1, 0,
                                       ((Codec == QSV_CODEC_AV1)   ? 16
                                        : (Codec == QSV_CODEC_AVC) ? 15
                                                                   : 16),
                                       1);
  obs_property_set_long_description(
      Prop, TEXT_NUM_REF_ACTIVE_BL1_DESC);

  // ── Codec-specific ──────────────────────────────────────────
  if (Codec == QSV_CODEC_HEVC) {
    Prop =
        obs_properties_add_list(Props, "hevc_gpb", TEXT_HEVC_GPB,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(
        Prop, TEXT_HEVC_GPB_DESC);
  }

  if (Codec == QSV_CODEC_HEVC) {
    Prop =
        obs_properties_add_list(Props, "hevc_sao", TEXT_HEVC_SAO,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_hevc_sao);
  }

  if (Codec == QSV_CODEC_AV1) {
    Prop = obs_properties_add_list(Props, "screen_content_tools",
                                   TEXT_SCREEN_CONTENT_TOOLS,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_screen_content_tools);
  }

  if (Codec != QSV_CODEC_AV1) {
    Prop = obs_properties_add_list(Props, "intra_ref_encoding",
                                   TEXT_INTRA_REF_ENCODING, OBS_COMBO_TYPE_LIST,
                                   OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition);
    obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

    Prop = obs_properties_add_int(Props, "intra_ref_cycle_size",
                                  TEXT_INTRA_REF_CYCLE_SIZE, 2, 1000, 1);
    obs_property_set_long_description(
        Prop, TEXT_INTRA_REF_CYCLE_SIZE_DESC);

    Prop = obs_properties_add_int(Props, "intra_ref_qp_delta",
                                  TEXT_INTRA_REF_QP_DELTA, -51, 51, 1);
    obs_property_set_long_description(
        Prop, TEXT_INTRA_REF_QP_DELTA_DESC);
  }

  // ── VPP / Post-processing ───────────────────────────────────
  Prop = obs_properties_add_list(Props, "vpp", TEXT_VPP, OBS_COMBO_TYPE_LIST,
                                 OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(
      Prop, TEXT_VPP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "vpp_mode", TEXT_VPP_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_vpp);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "denoise_mode", TEXT_DENOISE_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_denoise_mode);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  obs_properties_add_int_slider(Props, "denoise_strength",
                                TEXT_DENOISE_STRENGTH, 1, 100, 1);

  Prop = obs_properties_add_list(Props, "scaling_mode", TEXT_SCALING_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_scaling_mode);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "detail", TEXT_DETAIL,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  obs_properties_add_int_slider(Props, "detail_factor", TEXT_DETAIL_FACTOR, 1,
                                100, 1);

  Prop = obs_properties_add_list(Props, "image_stab_mode", TEXT_IMAGE_STAB_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_image_stab_mode);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(Props, "perc_enc_prefilter",
                                 TEXT_PERC_ENC_PREFILTER, OBS_COMBO_TYPE_LIST,
                                 OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  // ── Miscellaneous ───────────────────────────────────────────
  Prop = obs_properties_add_list(Props, "scenario_info", TEXT_SCENARIO_INFO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_scenario_info);
  obs_property_set_long_description(
      Prop, TEXT_SCENARIO_INFO_DESC);

  Prop = obs_properties_add_list(Props, "content_info", TEXT_CONTENT_INFO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_content_info);
  obs_property_set_long_description(
      Prop, TEXT_CONTENT_INFO_DESC);

  Prop = obs_properties_add_int_slider(Props, "temporal_layers",
                                       TEXT_TEMPORAL_LAYERS, 0, 4, 1);

  Prop = obs_properties_add_int(Props, "gpu_number", TEXT_GPU_NUMBER, 0, 4, 1);
  obs_property_set_long_description(
      Prop, TEXT_GPU_NUMBER_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  return Props;
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
  int KeyIntervalData =
      static_cast<int>(obs_data_get_int(Settings, "keyint_sec"));
  int GopRefDistData =
      static_cast<int>(obs_data_get_int(Settings, "gop_ref_dist"));

  const char *HRDConformanceData =
      obs_data_get_string(Settings, "hrd_conformance");

  const char *LowDelayHRDData = obs_data_get_string(Settings, "low_delay_hrd");

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
  const char *LookaheadLatencyData =
      obs_data_get_string(Settings, "lookahead_latency");
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
  const char *BitrateLimitData =
      obs_data_get_string(Settings, "bitrate_limit");
  const char *TuneQualityData = obs_data_get_string(Settings, "tune_quality");
  const char *PPyramidData = obs_data_get_string(Settings, "p_pyramid");
  const char *ExtBRCData = obs_data_get_string(Settings, "extbrc");
  const char *EncToolsData = obs_data_get_string(Settings, "enctools");
  const char *IntraRefEncodingData =
      obs_data_get_string(Settings, "intra_ref_encoding");
  int IntraRefCycleSizeData =
      static_cast<int>(obs_data_get_int(Settings, "intra_ref_cycle_size"));
  int IntraRefQPDeltaData =
      static_cast<int>(obs_data_get_int(Settings, "intra_ref_qp_delta"));

  const char *ScreenContentToolsData =
      obs_data_get_string(Settings, "screen_content_tools");

  int TemporalLayersData =
      static_cast<int>(obs_data_get_int(Settings, "temporal_layers"));

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

  if (std::strcmp(TargetUsageData, "TU1 (Veryslow)") == 0) {
    Context->EncoderParams.TargetUsage = MFX_TARGETUSAGE_1;
    info("\tTarget usage set: TU1 (Veryslow)");
  } else if (std::strcmp(TargetUsageData, "TU2 (Slower)") == 0) {
    Context->EncoderParams.TargetUsage = MFX_TARGETUSAGE_2;
    info("\tTarget usage set: TU2 (Slower)");
  } else if (std::strcmp(TargetUsageData, "TU3 (Slow)") == 0) {
    Context->EncoderParams.TargetUsage = MFX_TARGETUSAGE_3;
    info("\tTarget usage set: TU3 (Slow)");
  } else if (std::strcmp(TargetUsageData, "TU4 (Balanced)") == 0) {
    Context->EncoderParams.TargetUsage = MFX_TARGETUSAGE_4;
    info("\tTarget usage set: TU4 (Balanced)");
  } else if (std::strcmp(TargetUsageData, "TU5 (Fast)") == 0) {
    Context->EncoderParams.TargetUsage = MFX_TARGETUSAGE_5;
    info("\tTarget usage set: TU5 (Fast)");
  } else if (std::strcmp(TargetUsageData, "TU6 (Faster)") == 0) {
    Context->EncoderParams.TargetUsage = MFX_TARGETUSAGE_6;
    info("\tTarget usage set: TU6 (Faster)");
  } else if (std::strcmp(TargetUsageData, "TU7 (Veryfast)") == 0) {
    Context->EncoderParams.TargetUsage = MFX_TARGETUSAGE_7;
    info("\tTarget usage set: TU7 (Veryfast)");
  }

  if (std::strcmp(TuneQualityData, "PSNR") == 0) {
    Context->EncoderParams.TuneQualityMode = 1;
  } else if (std::strcmp(TuneQualityData, "SSIM") == 0) {
    Context->EncoderParams.TuneQualityMode = 2;
  } else if (std::strcmp(TuneQualityData, "MS SSIM") == 0) {
    Context->EncoderParams.TuneQualityMode = 3;
  } else if (std::strcmp(TuneQualityData, "VMAF") == 0) {
    Context->EncoderParams.TuneQualityMode = 4;
  } else if (std::strcmp(TuneQualityData, "PERCEPTUAL") == 0) {
    Context->EncoderParams.TuneQualityMode = 5;
  } else if (std::strcmp(TuneQualityData, "DEFAULT") == 0) {
    Context->EncoderParams.TuneQualityMode = 0;
  }

  switch (Context->Codec) {
  case QSV_CODEC_AVC:
    Codec = "H.264";
    if (std::strcmp(CodecProfileData, "baseline") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_BASELINE;
      if (obs_p010_tex_active()) {
        blog(LOG_WARNING, "[qsv encoder] baseline->high10 for P010");
        Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH10;
      }
    } else if (std::strcmp(CodecProfileData, "main") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_MAIN;
      if (obs_p010_tex_active()) {
        blog(LOG_WARNING, "[qsv encoder] main->high10 for P010");
        Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH10;
      }
    } else if (std::strcmp(CodecProfileData, "high") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH;
      if (obs_p010_tex_active()) {
        blog(LOG_WARNING, "[qsv encoder] high->high10 for P010");
        Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH10;
      }
    } else if (std::strcmp(CodecProfileData, "extended") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_EXTENDED;
      if (obs_p010_tex_active()) {
        blog(LOG_WARNING, "[qsv encoder] extended->high10 for P010");
        Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH10;
      }
    } else if (std::strcmp(CodecProfileData, "high10") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH10;
    } else if (std::strcmp(CodecProfileData, "high422") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH_422;
    } else if (std::strcmp(CodecProfileData, "constrained_baseline") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_CONSTRAINED_BASELINE;
      if (obs_p010_tex_active()) {
        blog(LOG_WARNING, "[qsv encoder] constrained_baseline->high10 for P010");
        Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH10;
      }
    } else if (std::strcmp(CodecProfileData, "constrained_high") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_CONSTRAINED_HIGH;
      if (obs_p010_tex_active()) {
        blog(LOG_WARNING, "[qsv encoder] constrained_high->high10 for P010");
        Context->EncoderParams.CodecProfile = MFX_PROFILE_AVC_HIGH10;
      }
    }

    if (std::strcmp(CodecLevelDataAVC, "auto") == 0) {
      Context->EncoderParams.CodecLevel = 0;
    } else if (std::strcmp(CodecLevelDataAVC, "1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_1;
    } else if (std::strcmp(CodecLevelDataAVC, "1b") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_1b;
    } else if (std::strcmp(CodecLevelDataAVC, "1.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_11;
    } else if (std::strcmp(CodecLevelDataAVC, "1.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_12;
    } else if (std::strcmp(CodecLevelDataAVC, "1.3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_13;
    } else if (std::strcmp(CodecLevelDataAVC, "2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_2;
    } else if (std::strcmp(CodecLevelDataAVC, "2.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_21;
    } else if (std::strcmp(CodecLevelDataAVC, "2.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_22;
    } else if (std::strcmp(CodecLevelDataAVC, "3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_3;
    } else if (std::strcmp(CodecLevelDataAVC, "3.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_31;
    } else if (std::strcmp(CodecLevelDataAVC, "3.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_32;
    } else if (std::strcmp(CodecLevelDataAVC, "4") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_4;
    } else if (std::strcmp(CodecLevelDataAVC, "4.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_41;
    } else if (std::strcmp(CodecLevelDataAVC, "4.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_42;
    } else if (std::strcmp(CodecLevelDataAVC, "5") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_5;
    } else if (std::strcmp(CodecLevelDataAVC, "5.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_51;
    } else if (std::strcmp(CodecLevelDataAVC, "5.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_52;
    } else if (std::strcmp(CodecLevelDataAVC, "6") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_6;
    } else if (std::strcmp(CodecLevelDataAVC, "6.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_61;
    } else if (std::strcmp(CodecLevelDataAVC, "6.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AVC_62;
    }
    break;
  case QSV_CODEC_HEVC:
    Codec = "HEVC";
    if (std::strcmp(CodecProfileData, "main") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_HEVC_MAIN;

      if (obs_p010_tex_active()) {
        blog(LOG_WARNING, "[qsv encoder] Forcing main10 for P010");
        Context->EncoderParams.CodecProfile = MFX_PROFILE_HEVC_MAIN10;
      }

    } else if (std::strcmp(CodecProfileData, "main10") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_HEVC_MAIN10;
    } else if (std::strcmp(CodecProfileData, "rext") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_HEVC_REXT;
    } else if (std::strcmp(CodecProfileData, "mainsp") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_HEVC_MAINSP;
    } else if (std::strcmp(CodecProfileData, "scc") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_HEVC_SCC;
    }

    if (std::strcmp(CodecProfileTierData, "main") == 0) {
      Context->EncoderParams.CodecProfileTier = MFX_TIER_HEVC_MAIN;
    } else {
      mfxU16 platformCode = QueryPlatformCodeName();
      bool highTierUnsupported = platformCode != 0 &&
                                 platformCode < MFX_PLATFORM_TIGERLAKE;
      if (highTierUnsupported) {
        info("\tHEVC High Tier not supported on this GPU, "
             "falling back to Main Tier");
        Context->EncoderParams.CodecProfileTier = MFX_TIER_HEVC_MAIN;
      } else {
        Context->EncoderParams.CodecProfileTier = MFX_TIER_HEVC_HIGH;
      }
    }

    if (std::strcmp(CodecLevelData, "auto") == 0) {
      Context->EncoderParams.CodecLevel = 0;
    } else if (std::strcmp(CodecLevelData, "1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_1;
    } else if (std::strcmp(CodecLevelData, "2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_2;
    } else if (std::strcmp(CodecLevelData, "2.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_21;
    } else if (std::strcmp(CodecLevelData, "3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_3;
    } else if (std::strcmp(CodecLevelData, "3.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_31;
    } else if (std::strcmp(CodecLevelData, "4") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_4;
    } else if (std::strcmp(CodecLevelData, "4.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_41;
    } else if (std::strcmp(CodecLevelData, "5") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_5;
    } else if (std::strcmp(CodecLevelData, "5.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_51;
    } else if (std::strcmp(CodecLevelData, "5.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_52;
    } else if (std::strcmp(CodecLevelData, "6") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_6;
    } else if (std::strcmp(CodecLevelData, "6.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_61;
    } else if (std::strcmp(CodecLevelData, "6.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_HEVC_62;
    }
    break;
  case QSV_CODEC_AV1:
    Codec = "AV1";
    if (std::strcmp(CodecProfileData, "main") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AV1_MAIN;
    } else if (std::strcmp(CodecProfileData, "high") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AV1_HIGH;
    } else if (std::strcmp(CodecProfileData, "pro") == 0) {
      Context->EncoderParams.CodecProfile = MFX_PROFILE_AV1_PRO;
    }

    if (std::strcmp(CodecLevelDataAV1, "auto") == 0) {
      Context->EncoderParams.CodecLevel = 0;
    } else if (std::strcmp(CodecLevelDataAV1, "2.0") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_2;
    } else if (std::strcmp(CodecLevelDataAV1, "2.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_21;
    } else if (std::strcmp(CodecLevelDataAV1, "2.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_22;
    } else if (std::strcmp(CodecLevelDataAV1, "2.3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_23;
    } else if (std::strcmp(CodecLevelDataAV1, "3.0") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_3;
    } else if (std::strcmp(CodecLevelDataAV1, "3.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_31;
    } else if (std::strcmp(CodecLevelDataAV1, "3.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_32;
    } else if (std::strcmp(CodecLevelDataAV1, "3.3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_33;
    } else if (std::strcmp(CodecLevelDataAV1, "4.0") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_4;
    } else if (std::strcmp(CodecLevelDataAV1, "4.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_41;
    } else if (std::strcmp(CodecLevelDataAV1, "4.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_42;
    } else if (std::strcmp(CodecLevelDataAV1, "4.3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_43;
    } else if (std::strcmp(CodecLevelDataAV1, "5.0") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_5;
    } else if (std::strcmp(CodecLevelDataAV1, "5.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_51;
    } else if (std::strcmp(CodecLevelDataAV1, "5.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_52;
    } else if (std::strcmp(CodecLevelDataAV1, "5.3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_53;
    } else if (std::strcmp(CodecLevelDataAV1, "6.0") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_6;
    } else if (std::strcmp(CodecLevelDataAV1, "6.1") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_61;
    } else if (std::strcmp(CodecLevelDataAV1, "6.2") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_62;
    } else if (std::strcmp(CodecLevelDataAV1, "6.3") == 0) {
      Context->EncoderParams.CodecLevel = MFX_LEVEL_AV1_63;
    }
    break;
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

  if (std::strcmp(LowDelayHRDData, "AUTO") == 0) {
    Context->EncoderParams.LowDelayHRD = std::nullopt;
  } else if (std::strcmp(LowDelayHRDData, "ON") == 0) {
    Context->EncoderParams.LowDelayHRD = true;
  } else if (std::strcmp(LowDelayHRDData, "OFF") == 0) {
    Context->EncoderParams.LowDelayHRD = false;
  }

  if (std::strcmp(MVOverPicBoundariesData, "AUTO") == 0) {
    Context->EncoderParams.MotionVectorsOverPicBoundaries = std::nullopt;
  } else if (std::strcmp(MVOverPicBoundariesData, "ON") == 0) {
    Context->EncoderParams.MotionVectorsOverPicBoundaries = true;
  } else if (std::strcmp(MVOverPicBoundariesData, "OFF") == 0) {
    Context->EncoderParams.MotionVectorsOverPicBoundaries = false;
  }

  if (std::strcmp(HRDConformanceData, "AUTO") == 0) {
    Context->EncoderParams.HRDConformance = std::nullopt;
  } else if (std::strcmp(HRDConformanceData, "ON") == 0) {
    Context->EncoderParams.HRDConformance = true;
  } else if (std::strcmp(HRDConformanceData, "OFF") == 0) {
    Context->EncoderParams.HRDConformance = false;
  }

  if (std::strcmp(MBBRCData, "AUTO") == 0) {
    Context->EncoderParams.MBBRC = std::nullopt;
  } else if (std::strcmp(MBBRCData, "ON") == 0) {
    Context->EncoderParams.MBBRC = true;
  } else if (std::strcmp(MBBRCData, "OFF") == 0) {
    Context->EncoderParams.MBBRC = false;
  }

  if (std::strcmp(ExtBRCData, "ON") == 0) {
    Context->EncoderParams.ExtBRC = 1;
  }

  if (std::strcmp(EncToolsData, "ON") == 0) {
    Context->EncoderParams.EncTools = true;
  } else {
    Context->EncoderParams.EncTools = false;
  }

  if (std::strcmp(DirectBiasAdjustmentData, "AUTO") == 0) {
    Context->EncoderParams.DirectBiasAdjustment = std::nullopt;
  } else if (std::strcmp(DirectBiasAdjustmentData, "ON") == 0) {
    Context->EncoderParams.DirectBiasAdjustment = true;
  } else if (std::strcmp(DirectBiasAdjustmentData, "OFF") == 0) {
    Context->EncoderParams.DirectBiasAdjustment = false;
  }

  if (std::strcmp(MVCostScalingFactorData, "OFF") == 0) {
    Context->EncoderParams.MVCostScalingFactor = 0;
  } else if (std::strcmp(MVCostScalingFactorData, "1/2") == 0) {
    Context->EncoderParams.MVCostScalingFactor = 1;
  } else if (std::strcmp(MVCostScalingFactorData, "1/4") == 0) {
    Context->EncoderParams.MVCostScalingFactor = 2;
  } else if (std::strcmp(MVCostScalingFactorData, "1/8") == 0) {
    Context->EncoderParams.MVCostScalingFactor = 3;
  }

  if (std::strcmp(UseRawRefData, "AUTO") == 0) {
    Context->EncoderParams.RawRef = std::nullopt;
  } else if (std::strcmp(UseRawRefData, "ON") == 0) {
    Context->EncoderParams.RawRef = true;
  } else if (std::strcmp(UseRawRefData, "OFF") == 0) {
    Context->EncoderParams.RawRef = false;
  }

  if (std::strcmp(PPyramidData, "PYRAMID") == 0) {
    Context->EncoderParams.PPyramid = 1;
  } else if (std::strcmp(PPyramidData, "SIMPLE") == 0) {
    Context->EncoderParams.PPyramid = 0;
  }

  if (std::strcmp(GlobalMotionBiasAdjustmentData, "AUTO") == 0) {
    Context->EncoderParams.GlobalMotionBiasAdjustment = std::nullopt;
  } else if (std::strcmp(GlobalMotionBiasAdjustmentData, "ON") == 0) {
    Context->EncoderParams.GlobalMotionBiasAdjustment = true;
  } else if (std::strcmp(GlobalMotionBiasAdjustmentData, "OFF") == 0) {
    Context->EncoderParams.GlobalMotionBiasAdjustment = false;
  }

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
      info("\tLookaheadDepth set: %d", Context->EncoderParams.LADepth);
    }

    if (std::strcmp(LookaheadDSData, "SLOW") == 0) {
      Context->EncoderParams.LookAheadDS = 0;
    } else if (std::strcmp(LookaheadDSData, "MEDIUM") == 0) {
      Context->EncoderParams.LookAheadDS = 1;
    } else if (std::strcmp(LookaheadDSData, "FAST") == 0) {
      Context->EncoderParams.LookAheadDS = 2;
    }
  } else if (std::strcmp(LookaheadData, "LP") == 0) {
    if (GopRefDistData > 0 && GopRefDistData < 17) {
      Context->EncoderParams.Lookahead = true;
      Context->EncoderParams.LookaheadLP = true;
      if (GopRefDistData > 8) {
        Context->EncoderParams.LADepth = 8;
      } else {
        Context->EncoderParams.LADepth = static_cast<mfxU16>(GopRefDistData);
      }
    }
  } else {
    Context->EncoderParams.Lookahead = false;
  }

  if (std::strcmp(IntraRefEncodingData, "ON") == 0) {
    Context->EncoderParams.IntraRefEncoding = 1;
  } else if (std::strcmp(IntraRefEncodingData, "OFF") == 0) {
    Context->EncoderParams.IntraRefEncoding = 0;
  }

  if (std::strcmp(AdaptiveCQMData, "AUTO") == 0) {
    Context->EncoderParams.AdaptiveCQM = std::nullopt;
  } else if (std::strcmp(AdaptiveCQMData, "ON") == 0) {
    Context->EncoderParams.AdaptiveCQM = true;
  } else if (std::strcmp(AdaptiveCQMData, "OFF") == 0) {
    Context->EncoderParams.AdaptiveCQM = false;
  }

  if (std::strcmp(AdaptiveLTRData, "AUTO") == 0) {
    Context->EncoderParams.AdaptiveLTR = std::nullopt;
  } else if (std::strcmp(AdaptiveLTRData, "ON") == 0) {
    Context->EncoderParams.AdaptiveLTR = true;
  } else if (std::strcmp(AdaptiveLTRData, "OFF") == 0) {
    Context->EncoderParams.AdaptiveLTR = false;
  }

  if (std::strcmp(AdaptiveIData, "AUTO") == 0) {
    Context->EncoderParams.AdaptiveI = std::nullopt;
  } else if (std::strcmp(AdaptiveIData, "ON") == 0) {
    Context->EncoderParams.AdaptiveI = true;
  } else if (std::strcmp(AdaptiveIData, "OFF") == 0) {
    Context->EncoderParams.AdaptiveI = false;
  }

  if (std::strcmp(AdaptiveBData, "AUTO") == 0) {
    Context->EncoderParams.AdaptiveB = std::nullopt;
  } else if (std::strcmp(AdaptiveBData, "ON") == 0) {
    Context->EncoderParams.AdaptiveB = true;
  } else if (std::strcmp(AdaptiveBData, "OFF") == 0) {
    Context->EncoderParams.AdaptiveB = false;
  }

  if (std::strcmp(AdaptiveRefData, "AUTO") == 0) {
    Context->EncoderParams.AdaptiveRef = std::nullopt;
  } else if (std::strcmp(AdaptiveRefData, "ON") == 0) {
    Context->EncoderParams.AdaptiveRef = true;
  } else if (std::strcmp(AdaptiveRefData, "OFF") == 0) {
    Context->EncoderParams.AdaptiveRef = false;
  }

  if (std::strcmp(LowPowerData, "ON") == 0) {
    Context->EncoderParams.Lowpower = true;
  } else if (std::strcmp(LowPowerData, "OFF") == 0) {
    Context->EncoderParams.Lowpower = false;
  }

  if (std::strcmp(RDOData, "AUTO") == 0) {
    Context->EncoderParams.RDO = std::nullopt;
  } else if (std::strcmp(RDOData, "ON") == 0) {
    Context->EncoderParams.RDO = true;
  } else if (std::strcmp(RDOData, "OFF") == 0) {
    Context->EncoderParams.RDO = false;
  }

  if (std::strcmp(TrellisData, "I") == 0) {
    Context->EncoderParams.Trellis = 1;
  } else if (std::strcmp(TrellisData, "IP") == 0) {
    Context->EncoderParams.Trellis = 2;
  } else if (std::strcmp(TrellisData, "IPB") == 0) {
    Context->EncoderParams.Trellis = 3;
  } else if (std::strcmp(TrellisData, "IB") == 0) {
    Context->EncoderParams.Trellis = 4;
  } else if (std::strcmp(TrellisData, "P") == 0) {
    Context->EncoderParams.Trellis = 5;
  } else if (std::strcmp(TrellisData, "PB") == 0) {
    Context->EncoderParams.Trellis = 6;
  } else if (std::strcmp(TrellisData, "B") == 0) {
    Context->EncoderParams.Trellis = 7;
  }

  if (std::strcmp(SAOData, "DISABLE") == 0) {
    Context->EncoderParams.SAO = 0;
  } else if (std::strcmp(SAOData, "LUMA") == 0) {
    Context->EncoderParams.SAO = 1;
  } else if (std::strcmp(SAOData, "CHROMA") == 0) {
    Context->EncoderParams.SAO = 2;
  } else if (std::strcmp(SAOData, "ALL") == 0) {
    Context->EncoderParams.SAO = 3;
  }

  if (std::strcmp(GPBData, "AUTO") == 0) {
    Context->EncoderParams.GPB = std::nullopt;
  } else if (std::strcmp(GPBData, "ON") == 0) {
    Context->EncoderParams.GPB = true;
  } else if (std::strcmp(GPBData, "OFF") == 0) {
    Context->EncoderParams.GPB = false;
  }

  if (std::strcmp(ScenarioInfoData, "OFF") == 0) {
    Context->EncoderParams.ScenarioInfo = std::nullopt;
  } else if (std::strcmp(ScenarioInfoData, "AUTO") == 0) {
    Context->EncoderParams.ScenarioInfo = 0;
  } else if (std::strcmp(ScenarioInfoData, "ARCHIVE") == 0) {
    Context->EncoderParams.ScenarioInfo = 1;
  } else if (std::strcmp(ScenarioInfoData, "LIVE") == 0) {
    Context->EncoderParams.ScenarioInfo = 2;
  } else if (std::strcmp(ScenarioInfoData, "REMOTE_GAMING") == 0) {
    Context->EncoderParams.ScenarioInfo = 3;
  } else if (std::strcmp(ScenarioInfoData, "GAME_STREAMING") == 0) {
    Context->EncoderParams.ScenarioInfo = 4;
  }

  if (std::strcmp(ContentInfoData, "OFF") == 0) {
    Context->EncoderParams.ContentInfo = std::nullopt;
  } else if (std::strcmp(ContentInfoData, "AUTO") == 0) {
    Context->EncoderParams.ContentInfo = 0;
  } else if (std::strcmp(ContentInfoData, "NOISY_VIDEO") == 0) {
    Context->EncoderParams.ContentInfo = 2;
  } else if (std::strcmp(ContentInfoData, "GAME") == 0) {
    Context->EncoderParams.ContentInfo = 4;
  } else if (std::strcmp(ContentInfoData, "CAMERA_SCENE") == 0) {
    Context->EncoderParams.ContentInfo = 1;
  } else if (std::strcmp(ContentInfoData, "CLEAN_CAMERA_SCENE") == 0) {
    Context->EncoderParams.ContentInfo = 7;
  } else if (std::strcmp(ContentInfoData, "ANIMATED_GRAPHICS") == 0) {
    Context->EncoderParams.ContentInfo = 6;
  } else if (std::strcmp(ContentInfoData, "COMPUTER_DISPLAY") == 0) {
    Context->EncoderParams.ContentInfo = 10;
  } else if (std::strcmp(ContentInfoData, "PROGRESSIVE_VIDEO") == 0) {
    Context->EncoderParams.ContentInfo = 9;
  } else if (std::strcmp(ContentInfoData, "STILL_IMAGE") == 0) {
    Context->EncoderParams.ContentInfo = 8;
  } else if (std::strcmp(ContentInfoData, "VIDEO_CONFERENCE") == 0) {
    Context->EncoderParams.ContentInfo = 5;
  }

  if (std::strcmp(TransformSkipData, "AUTO") == 0) {
    Context->EncoderParams.TransformSkip = std::nullopt;
  } else if (std::strcmp(TransformSkipData, "ON") == 0) {
    Context->EncoderParams.TransformSkip = true;
  } else if (std::strcmp(TransformSkipData, "OFF") == 0) {
    Context->EncoderParams.TransformSkip = false;
  }

  if (std::strcmp(FadeDetectionData, "AUTO") == 0) {
    Context->EncoderParams.FadeDetection = std::nullopt;
  } else if (std::strcmp(FadeDetectionData, "ON") == 0) {
    Context->EncoderParams.FadeDetection = true;
  } else if (std::strcmp(FadeDetectionData, "OFF") == 0) {
    Context->EncoderParams.FadeDetection = false;
  }

  if (std::strcmp(BitrateLimitData, "AUTO") == 0) {
    Context->EncoderParams.BitrateLimit = std::nullopt;
  } else if (std::strcmp(BitrateLimitData, "ON") == 0) {
    Context->EncoderParams.BitrateLimit = true;
  } else if (std::strcmp(BitrateLimitData, "OFF") == 0) {
    Context->EncoderParams.BitrateLimit = false;
  }

  if (std::strcmp(RateControlData, "CBR") == 0) {
    Context->EncoderParams.RateControl = MFX_RATECONTROL_CBR;
  } else if (std::strcmp(RateControlData, "VBR") == 0) {
    Context->EncoderParams.RateControl = MFX_RATECONTROL_VBR;
  } else if (std::strcmp(RateControlData, "CQP") == 0) {
    Context->EncoderParams.RateControl = MFX_RATECONTROL_CQP;
  } else if (std::strcmp(RateControlData, "AVBR") == 0) {
    Context->EncoderParams.RateControl = MFX_RATECONTROL_AVBR;
  } else if (std::strcmp(RateControlData, "ICQ") == 0) {
    Context->EncoderParams.RateControl = MFX_RATECONTROL_ICQ;
  } else if (std::strcmp(RateControlData, "VCM") == 0) {
    Context->EncoderParams.RateControl = MFX_RATECONTROL_VCM;
  } else if (std::strcmp(RateControlData, "QVBR") == 0) {
    Context->EncoderParams.RateControl = MFX_RATECONTROL_QVBR;
  }

  if (std::strcmp(DenoiseModeData, "DEFAULT") == 0) {
    Context->EncoderParams.VPPDenoiseMode = 0;
  } else if (std::strcmp(DenoiseModeData, "AUTO | BDRATE | PRE ENCODE") == 0) {
    Context->EncoderParams.VPPDenoiseMode = 1;
  } else if (std::strcmp(DenoiseModeData, "AUTO | ADJUST | POST ENCODE") == 0) {
    Context->EncoderParams.VPPDenoiseMode = 2;
  } else if (std::strcmp(DenoiseModeData, "AUTO | SUBJECTIVE | PRE ENCODE") ==
             0) {
    Context->EncoderParams.VPPDenoiseMode = 3;
  } else if (std::strcmp(DenoiseModeData, "MANUAL | PRE ENCODE") == 0) {
    Context->EncoderParams.VPPDenoiseMode = 4;
    Context->EncoderParams.DenoiseStrength =
        static_cast<mfxU16>(DenoiseStrengthData);
  } else if (std::strcmp(DenoiseModeData, "MANUAL | POST ENCODE") == 0) {
    Context->EncoderParams.VPPDenoiseMode = 5;
    Context->EncoderParams.DenoiseStrength =
        static_cast<mfxU16>(DenoiseStrengthData);
  }

  if (std::strcmp(ScalingModeData, "QUALITY | ADVANCED") == 0) {
    Context->EncoderParams.VPPScalingMode = 1;
  } else if (std::strcmp(ScalingModeData, "VEBOX | ADVANCED") == 0) {
    Context->EncoderParams.VPPScalingMode = 2;
  } else if (std::strcmp(ScalingModeData, "LOWPOWER | NEAREST NEIGHBOR") == 0) {
    Context->EncoderParams.VPPScalingMode = 3;
  } else if (std::strcmp(ScalingModeData, "LOWPOWER | ADVANCED") == 0) {
    Context->EncoderParams.VPPScalingMode = 4;
  } else if (std::strcmp(ScalingModeData, "AUTO") == 0) {
    Context->EncoderParams.VPPScalingMode = 0;
  }

  if (std::strcmp(ImageStabModeData, "UPSCALE") == 0) {
    Context->EncoderParams.VPPImageStabMode = 1;
  } else if (std::strcmp(ImageStabModeData, "BOXING") == 0) {
    Context->EncoderParams.VPPImageStabMode = 2;
  } else if (std::strcmp(ImageStabModeData, "AUTO") == 0) {
    Context->EncoderParams.VPPScalingMode = 0;
  }

  if (std::strcmp(DetailData, "ON") == 0) {
    Context->EncoderParams.VPPDetail = DetailFactorData;
  } else if (std::strcmp(DetailData, "OFF") == 0) {
    Context->EncoderParams.VPPDetail = 0;
  }

  if (std::strcmp(PercEncPrefilterData, "ON") == 0) {
    Context->EncoderParams.PercEncPrefilter = 1;
  } else if (std::strcmp(PercEncPrefilterData, "OFF") == 0) {
    Context->EncoderParams.PercEncPrefilter = 0;
  }

  Context->EncoderParams.AsyncDepth =
      static_cast<mfxU16>(obs_data_get_int(Settings, "async_depth"));

  auto ActualCQPData = CQPData;
  bool CQPSeparateIPB = obs_data_get_bool(Settings, "cqp_separate_ipb");
  if (CQPSeparateIPB) {
    int QPIData = static_cast<int>(obs_data_get_int(Settings, "qpi"));
    int QPPData = static_cast<int>(obs_data_get_int(Settings, "qpp"));
    int QPBData = static_cast<int>(obs_data_get_int(Settings, "qpb"));
    if (Context->Codec == QSV_CODEC_AV1) {
      QPIData *= 4;
      QPPData *= 4;
      QPBData *= 4;
    }
    Context->EncoderParams.QPI = static_cast<mfxU16>(QPIData);
    Context->EncoderParams.QPP = static_cast<mfxU16>(QPPData);
    Context->EncoderParams.QPB = static_cast<mfxU16>(QPBData);
  } else {
    if (Context->Codec == QSV_CODEC_AV1) {
      ActualCQPData *= 4;
    }
    Context->EncoderParams.QPI = static_cast<mfxU16>(ActualCQPData);
    Context->EncoderParams.QPP = static_cast<mfxU16>(ActualCQPData);
    Context->EncoderParams.QPB = static_cast<mfxU16>(ActualCQPData);
  }

  Context->EncoderParams.TargetBitRate = TargetBitrateData;
  Context->EncoderParams.CustomBufferSize =
      static_cast<bool>(CustomBufferSizeData);
  Context->EncoderParams.BufferSize = BufferSizeData;
  Context->EncoderParams.MaxBitRate = MaxBitrateData;
  Context->EncoderParams.Width = static_cast<mfxU16>(VideoWidth);
  Context->EncoderParams.Height = static_cast<mfxU16>(VideoHeight);
  Context->EncoderParams.FpsNum = static_cast<mfxU32>(VOI->fps_num);
  Context->EncoderParams.FpsDen = static_cast<mfxU32>(VOI->fps_den);

  Context->EncoderParams.GOPRefDist = static_cast<mfxU16>(GopRefDistData);
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

  const char *WinBRCData = obs_data_get_string(Settings, "win_brc");
  Context->EncoderParams.WinBRC = (std::strcmp(WinBRCData, "ON") == 0);
  Context->EncoderParams.WinBRCMaxAvgKbps =
      static_cast<mfxU16>(obs_data_get_int(Settings, "win_brc_max_avg_size"));
  Context->EncoderParams.WinBRCSize =
      static_cast<mfxU16>(obs_data_get_int(Settings, "win_brc_size"));

  Context->EncoderParams.QVBRQuality =
      static_cast<mfxU16>(obs_data_get_int(Settings, "qvbr_quality"));

  if (std::strcmp(ScreenContentToolsData, "AUTO") == 0) {
    Context->EncoderParams.ScreenContentTools = 0;
  } else if (std::strcmp(ScreenContentToolsData, "OFF") == 0) {
    Context->EncoderParams.ScreenContentTools = 1;
  } else if (std::strcmp(ScreenContentToolsData, "ON") == 0) {
    Context->EncoderParams.ScreenContentTools = 2;
  }

  Context->EncoderParams.TemporalLayersNum =
      static_cast<mfxU16>(TemporalLayersData);

  Context->EncoderParams.ProcessingEnable = false;
  if ((Context->EncoderParams.VPPDenoiseMode.has_value() ||
       Context->EncoderParams.VPPDetail.has_value() ||
       Context->EncoderParams.VPPScalingMode.has_value() ||
       Context->EncoderParams.VPPImageStabMode.has_value() ||
       Context->EncoderParams.PercEncPrefilter == true) &&
      std::strcmp(VideoProcessingStatusData, "ON") == 0) {
    if (VOI->format == VIDEO_FORMAT_NV12) {
      Context->EncoderParams.ProcessingEnable = true;
    } else {
      warn("VPP is only available with NV12 color format");
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
  }

  info("\tDebug info:");
  info("\tCodec: %s", Codec);
  info("\tRate control: %s\n", RateControlData);

  if (Context->EncoderParams.RateControl != MFX_RATECONTROL_ICQ &&
      Context->EncoderParams.RateControl != MFX_RATECONTROL_CQP)
    info("\tTarget bitrate: %d", Context->EncoderParams.TargetBitRate);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_VBR ||
      Context->EncoderParams.RateControl == MFX_RATECONTROL_VCM ||
      Context->EncoderParams.RateControl == MFX_RATECONTROL_QVBR)
    info("\tMax bitrate: %d", Context->EncoderParams.MaxBitRate);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_ICQ &&
      std::strcmp(RateControlData, "ICQ") == 0)
    info("\tICQ Quality: %d", Context->EncoderParams.ICQQuality);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_CQP)
    info("\tCQP: %d", ActualCQPData);

  info("\tFPS numerator: %d", VOI->fps_num);
  info("\tFPS denominator: %d", VOI->fps_den);
  info("\tOutput width: %d", VideoWidth);
  info("\tOutput height: %d", VideoHeight);
}

static obs_properties_t *GetH264ParamProps([[maybe_unused]] void *) {
  return GetParamProps(QSV_CODEC_AVC);
}

static obs_properties_t *GetAV1ParamProps([[maybe_unused]] void *) {

  return GetParamProps(QSV_CODEC_AV1);
}

static obs_properties_t *GetHEVCParamProps([[maybe_unused]] void *) {
  return GetParamProps(QSV_CODEC_HEVC);
}

plugin_context *InitPluginContext(enum codec_enum Codec, obs_data_t *Settings,
                                  obs_encoder_t *EncoderData,
                                  bool IsTextureEncoder) {

  plugin_context *Context = nullptr;
  try {
    Context = new plugin_context;
    // throw std::exception("test");
  } catch (const std::exception &e) {
    blog(LOG_WARNING, "QSV init failed. %s", e.what());

    delete Context;

    return nullptr;
  }

  Context->EncoderData = std::move(EncoderData);
  Context->Codec = std::move(Codec);

  auto Video = std::move(obs_encoder_video(Context->EncoderData));
  auto VOI = std::move(video_output_get_info(std::move(Video)));
  switch (VOI->format) {
  case VIDEO_FORMAT_I010:
  case VIDEO_FORMAT_P010:
    Context->EncoderParams.VideoFormat10bit = true;
    break;
  default:
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

    std::lock_guard<std::mutex> lock(Mutex);
    OpenEncoder(Context->EncoderPTR, &Context->EncoderParams, Context->Codec,
                IsTextureEncoder);

    GetEncoderVersion(&VPLVersionMajor, &VPLVersionMinor);

    info("\tLibVPL version: %d.%d", VPLVersionMajor, VPLVersionMinor);

    Context->PerformanceToken = os_request_high_performance("qsv encoding");

    return Context;
  } catch (const std::exception &e) {
    blog(LOG_WARNING, "QSV failed to load. %s", e.what());

    delete Context;

    return nullptr;
  }
}

static void *InitH264FrameEncoder(obs_data_t *Settings,
                                  obs_encoder_t *EncoderData) {
  return InitPluginContext(QSV_CODEC_AVC, Settings, EncoderData, false);
}

static void *InitAV1FrameEncoder(obs_data_t *Settings,
                                 obs_encoder_t *EncoderData) {
  return InitPluginContext(QSV_CODEC_AV1, Settings, EncoderData, false);
}

static void *InitHEVCFrameEncoder(obs_data_t *Settings,
                                  obs_encoder_t *EncoderData) {
  return InitPluginContext(QSV_CODEC_HEVC, Settings, EncoderData, false);
}

static void *InitTextureEncoder(enum codec_enum Codec, obs_data_t *Settings,
                                obs_encoder_t *EncoderData,
                                const char *FallbackID) {
  struct obs_video_info OVI {};
  obs_get_video_info(&OVI);

  if (!AdaptersInfo[OVI.adapter].IsIntel) {
    info(">>> app not on intel GPU, fall back to non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       static_cast<const char *>(FallbackID));
  }

  if (static_cast<int>(obs_data_get_int(Settings, "gpu_number")) > 0) {
    info(">>> custom GPU is selected. OBS Studio does not support "
         "transferring textures to third-party adapters, fall back to "
         "non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       static_cast<const char *>(FallbackID));
  }

#if !defined(_WIN32) || !defined(_WIN64)
  info(">>> unsupported platform for texture encode");
  return obs_encoder_create_rerouted(EncoderData,
                                     static_cast<const char *>(FallbackID));
#endif

  if (Codec == QSV_CODEC_AV1 && !AdaptersInfo[OVI.adapter].SupportAV1) {
    info(">>> cap on different device, fall back to non-texture "
         "sharing AV1 qsv encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       static_cast<const char *>(FallbackID));
  }

  bool TextureEncodeSupport = obs_nv12_tex_active();

  if (Codec != QSV_CODEC_AVC)
    TextureEncodeSupport = TextureEncodeSupport || obs_p010_tex_active();

  if (!TextureEncodeSupport) {
    info(">>> gpu tex not active, fall back to non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       static_cast<const char *>(FallbackID));
  }

  if (obs_encoder_scaling_enabled(EncoderData)) {
    if (!obs_encoder_gpu_scaling_enabled(EncoderData)) {
      info(">>> encoder CPU scaling active, fall back to non-texture encoder");
      return obs_encoder_create_rerouted(EncoderData,
                                         static_cast<const char *>(FallbackID));
    }
    info(">>> encoder GPU scaling active");
  }

  info(">>> Texture encoder");
  plugin_context *Context = InitPluginContext(Codec, Settings, EncoderData, true);
  if (!Context) {
    info(">>> texture encoder init failed, fall back to non-texture encoder");
    return obs_encoder_create_rerouted(EncoderData,
                                       static_cast<const char *>(FallbackID));
  }
  return Context;
}

static void *InitH264TextureEncoder(obs_data_t *Settings,
                                    obs_encoder_t *EncoderData) {
  return InitTextureEncoder(QSV_CODEC_AVC, Settings, EncoderData,
                            "obs_qsv_vpl_h264");
}

static void *InitAV1TextureEncoder(obs_data_t *Settings,
                                   obs_encoder_t *EncoderData) {
  return InitTextureEncoder(QSV_CODEC_AV1, Settings, EncoderData,
                            "obs_qsv_vpl_av1");
}

static void *InitHEVCTextureEncoder(obs_data_t *Settings,
                                    obs_encoder_t *EncoderData) {
  return InitTextureEncoder(QSV_CODEC_HEVC, Settings, EncoderData,
                            "obs_qsv_vpl_hevc");
}

static const char *GetH264EncoderName([[maybe_unused]] void *) {
  return "QuickSync oneVPL H.264";
}

static const char *GetAV1EncoderName([[maybe_unused]] void *) {
  return "QuickSync oneVPL AV1";
}

static const char *GetHEVCEncoderName([[maybe_unused]] void *) {
  return "QuickSync oneVPL HEVC";
}

static void SetH264DefaultParams(obs_data_t *Settings) {
  SetDefaultEncoderParams(Settings, QSV_CODEC_AVC);
}

static void SetAV1DefaultParams(obs_data_t *Settings) {
  SetDefaultEncoderParams(Settings, QSV_CODEC_AV1);
}

static void SetHEVCDefaultParams(obs_data_t *Settings) {
  SetDefaultEncoderParams(Settings, QSV_CODEC_HEVC);
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
                                                 OBS_ENCODER_CAP_INTERNAL};

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
                                                   OBS_ENCODER_CAP_PASS_TEXTURE,
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
                                                OBS_ENCODER_CAP_INTERNAL};

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
                                                  OBS_ENCODER_CAP_PASS_TEXTURE,
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
                                                 OBS_ENCODER_CAP_INTERNAL};

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
                                                   OBS_ENCODER_CAP_PASS_TEXTURE,
                                           .encode_texture2 = EncodeTexture};