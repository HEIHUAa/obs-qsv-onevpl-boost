#pragma warning(disable : 4996)

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "helpers/encoder_params_parser.hpp"
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
const char *const qsv_profile_names_hevc[] = {"main", "main10", "rext", "scc", 0};
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
const char *const qsv_usage_names_simple[] = {
    "Best Quality (TU1-TU2)", "Balanced (TU3-TU5)", "Fastest (TU6-TU7)", 0};
const char *const qsv_usage_names_five[] = {
    "TU1 (Veryslow)", "TU2 (Slower)", "TU4 (Balanced)",
    "TU6 (Faster)",   "TU7 (Veryfast)", 0};
const char *const qsv_latency_names[] = {"ultra-low", "low", "normal", 0};
const char *const qsv_params_condition[] = {"ON", "OFF", 0};
const char *const qsv_params_condition_tristate[] = {"ON", "OFF", "AUTO", 0};
const char *const qsv_params_gop_opt_flag[] = {"AUTO", "OPEN", "CLOSED", "STRICT", 0};
const char *const qsv_params_weighted_pred_options[] = {"AUTO", "OFF",
    "DEFAULT", "EXPLICIT", "IMPLICIT", 0};
const char *const qsv_params_condition_scaling_mode[] = {
    "OFF", "QUALITY | ADVANCED", "VEBOX | ADVANCED",
    "LOWPOWER | NEAREST NEIGHBOR", "LOWPOWER | ADVANCED", "AUTO", 0};
const char *const qsv_params_condition_image_stab_mode[] = {
    "OFF", "UPSCALE", "BOXING", "AUTO", 0};
const char *const qsv_params_condition_screen_content_tools[] = {
    "AUTO", "OFF", "ON", 0};
const char *const qsv_params_skip_frame_mode[] = {
    "NO_SKIP", "INSERT_DUMMY", "INSERT_NOTHING", "BRC_ONLY", "AUTO", 0};
const char *const qsv_params_condition_intra_ref_encoding[] = {
    "VERTICAL", "HORIZONTAL", 0};
const char *const qsv_params_condition_mv_cost_scaling[] = {
    "AGGRESSIVE_0", "AGGRESSIVE_1", "MODERATE_2", "CONSERVATIVE_3", "AUTO", 0};
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
const char *const qsv_params_condition_denoise_mode_legacy[] = {
    "OFF", "MANUAL | PRE ENCODE", 0};
const char *const qsv_params_condition_procamp[] = {
    "OFF", "ON", 0};
const char *const qsv_params_condition_rotation[] = {
    "OFF", "90", "180", "270", 0};
const char *const qsv_params_condition_mirroring[] = {
    "OFF", "HORIZONTAL", "VERTICAL", "BOTH", 0};
const char *const qsv_params_condition_frc[] = {
    "OFF", "PRESERVE_TIMESTAMP", "DISTRIBUTED_TIMESTAMP",
    "FRAME_INTERPOLATION", "PRESERVE_TIMESTAMP + INTERPOLATION",
    "DISTRIBUTED_TIMESTAMP + INTERPOLATION", 0};
const char *const qsv_params_condition_av1_interp_filter[] = {
    "DEFAULT", "EIGHTTAP", "EIGHTTAP_SMOOTH", "EIGHTTAP_SHARP", "BILINEAR",
    "SWITCHABLE", 0};
#ifdef ONEVPL_EXPERIMENTAL
const char *const qsv_params_tune_quality[] = {
    "OFF", "VMAF", "PERCEPTUAL", "VMAF+PERCEPTUAL", 0};
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
  // Unlike std::call_once (which caches a failed probe forever), a failed
  // probe is retried on the next call — the driver may still be loading when
  // OBS first asks.  ProbeMutex also serializes cache writes (probe) against
  // reads, so callers never race CachedQSVPlatform.
  static std::mutex ProbeMutex;
  static std::atomic<bool> Probing{false};

  std::lock_guard<std::mutex> Lock(ProbeMutex);
  if (!Probing.exchange(true)) {
    bool ok = false;
    mfxLoader GlobalLoader = nullptr;
    {
      std::lock_guard<std::mutex> LoaderLock(GlobalLoaderMutex);
      GlobalLoader = GlobalQSVLoader;
    }

    if (GlobalLoader != nullptr) {
      ok = TryQueryPlatformCodeName(GlobalLoader);
    } else {
      mfxLoader Loader = MFXLoad();
      if (Loader != nullptr) {
        ok = TryQueryPlatformCodeName(Loader);
        MFXUnload(Loader);
      }
    }
    Probing.store(false);
    return ok ? CachedQSVPlatform.CodeName : 0;
  }

  // Another thread is probing right now (or a previous probe succeeded).
  return CachedQSVPlatformValid ? CachedQSVPlatform.CodeName : 0;
}

static bool PlatformSupportsDenoise2VPP() {
  static std::mutex ProbeMutex;
  static bool Probed = false;
  static bool Supported = true;

  std::lock_guard<std::mutex> Lock(ProbeMutex);
  if (Probed)
    return Supported;

  mfxLoader Loader = nullptr;
  {
    std::lock_guard<std::mutex> LoaderLock(GlobalLoaderMutex);
    Loader = GlobalQSVLoader;
  }
  if (Loader == nullptr)
    return true; // not initialized yet, retry next UI refresh

  bool ok = false;
  mfxSession Session{};
  if (MFXCreateSession(Loader, 0, &Session) >= MFX_ERR_NONE) {
    try {
      MFXVideoVPP VPP(Session);
      mfxVideoParam Params = {};
      Params.vpp.In.FourCC = MFX_FOURCC_NV12;
      Params.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
      Params.vpp.In.Width = 1920;
      Params.vpp.In.Height = 1080;
      Params.vpp.In.CropW = 1920;
      Params.vpp.In.CropH = 1080;
      Params.vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
      Params.vpp.In.FrameRateExtN = 30;
      Params.vpp.In.FrameRateExtD = 1;
      Params.vpp.Out = Params.vpp.In;
      Params.IOPattern =
          MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;

      mfxExtVPPDenoise2 Denoise2 = {};
      Denoise2.Header.BufferId = MFX_EXTBUFF_VPP_DENOISE2;
      Denoise2.Header.BufferSz = sizeof(Denoise2);
      Denoise2.Mode = MFX_DENOISE_MODE_INTEL_HVS_PRE_MANUAL;
      Denoise2.Strength = 25;
      mfxExtBuffer *InExt[1] = {&Denoise2.Header};
      Params.NumExtParam = 1;
      Params.ExtParam = InExt;

      mfxExtBuffer *OutExt[1] = {&Denoise2.Header};
      mfxVideoParam Out = {};
      Out.NumExtParam = 1;
      Out.ExtParam = OutExt;

      mfxStatus sts = VPP.Query(&Params, &Out);
      ok = (sts == MFX_ERR_NONE || sts == MFX_WRN_PARTIAL_ACCELERATION);
    } catch (...) {
      ok = false;
    }
    MFXClose(Session);
  }

  Supported = ok;
  Probed = true;
  return Supported;
}


// Is the VPP filter with `BufferId` accepted by MFXVideoVPP_Query?
static bool ProbeVPPFilterSupport(mfxU32 BufferId) {
  static std::mutex ProbeMutex;
  static std::unordered_map<mfxU32, std::optional<bool>> Cache;

  {
    std::lock_guard<std::mutex> Lock(ProbeMutex);
    if (auto it = Cache.find(BufferId); it != Cache.end())
      return it->second.value_or(true);
  }

  mfxLoader Loader = nullptr;
  {
    std::lock_guard<std::mutex> LoaderLock(GlobalLoaderMutex);
    Loader = GlobalQSVLoader;
  }
  if (Loader == nullptr)
    return true; // loader not up yet — retry next UI refresh

  bool ok = false;
  mfxSession Session{};
  if (MFXCreateSession(Loader, 0, &Session) >= MFX_ERR_NONE) {
    try {
      MFXVideoVPP VPP(Session);
      mfxVideoParam Params = {};
      Params.vpp.In.FourCC = MFX_FOURCC_NV12;
      Params.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
      Params.vpp.In.Width = 1920;
      Params.vpp.In.Height = 1080;
      Params.vpp.In.CropW = 1920;
      Params.vpp.In.CropH = 1080;
      Params.vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
      Params.vpp.In.FrameRateExtN = 30;
      Params.vpp.In.FrameRateExtD = 1;
      Params.vpp.Out = Params.vpp.In;
      Params.IOPattern =
          MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;

      mfxExtVPPImageStab ImageStab = {};
      mfxExtVPPFrameRateConversion FRC = {};
      mfxExtVPPMirroring Mirror = {};
#ifdef ONEVPL_EXPERIMENTAL
      mfxExtVPPPercEncPrefilter PercEnc = {};
#endif
      mfxExtBuffer *InExt[1] = {nullptr};
      if (BufferId == MFX_EXTBUFF_VPP_IMAGE_STABILIZATION) {
        ImageStab.Header.BufferId = BufferId;
        ImageStab.Header.BufferSz = sizeof(ImageStab);
        ImageStab.Mode = MFX_IMAGESTAB_MODE_UPSCALE;
        InExt[0] = &ImageStab.Header;
      } else if (BufferId == MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION) {
        FRC.Header.BufferId = BufferId;
        FRC.Header.BufferSz = sizeof(FRC);
        FRC.Algorithm = MFX_FRCALGM_FRAME_INTERPOLATION;
        InExt[0] = &FRC.Header;
      } else if (BufferId == MFX_EXTBUFF_VPP_MIRRORING) {
        Mirror.Header.BufferId = BufferId;
        Mirror.Header.BufferSz = sizeof(Mirror);
        Mirror.Type = 1; /* MFX_MIRRORING_HORIZONTAL */
        InExt[0] = &Mirror.Header;
      }
#ifdef ONEVPL_EXPERIMENTAL
      else if (BufferId == MFX_EXTBUFF_VPP_PERC_ENC_PREFILTER) {
        PercEnc.Header.BufferId = BufferId;
        PercEnc.Header.BufferSz = sizeof(PercEnc);
        InExt[0] = &PercEnc.Header;
      }
#endif
      if (InExt[0]) {
        Params.NumExtParam = 1;
        Params.ExtParam = InExt;

        mfxVideoParam Out = {};
        mfxExtBuffer *OutExt[1] = {InExt[0]};
        Out.NumExtParam = 1;
        Out.ExtParam = OutExt;

        mfxStatus sts = VPP.Query(&Params, &Out);
        ok = (sts == MFX_ERR_NONE || sts == MFX_WRN_PARTIAL_ACCELERATION);
      }
    } catch (...) {
      ok = false;
    }
    MFXClose(Session);
  }

  {
    std::lock_guard<std::mutex> Lock(ProbeMutex);
    Cache[BufferId] = ok;
  }
  return ok;
}

bool PlatformSupportsImageStabVPP() {
  return ProbeVPPFilterSupport(MFX_EXTBUFF_VPP_IMAGE_STABILIZATION);
}

bool PlatformSupportsFRCVPP() {
  return ProbeVPPFilterSupport(MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
}

bool PlatformSupportsMirrorVPP() {
  return ProbeVPPFilterSupport(MFX_EXTBUFF_VPP_MIRRORING);
}

bool PlatformSupportsPercEncVPP() {
  return ProbeVPPFilterSupport(MFX_EXTBUFF_VPP_PERC_ENC_PREFILTER);
}

bool PlatformSupportsIntraRefreshEncode(codec_enum Codec) {
  static std::mutex ProbeMutex;
  static bool Probed[2] = {false, false}; // 0 = AVC, 1 = HEVC
  static bool Supported[2] = {false, false};

  const int idx = (Codec == QSV_CODEC_HEVC) ? 1 : 0;
  {
    std::lock_guard<std::mutex> Lock(ProbeMutex);
    if (Probed[idx])
      return Supported[idx];
  }

  mfxLoader Loader = nullptr;
  {
    std::lock_guard<std::mutex> LoaderLock(GlobalLoaderMutex);
    Loader = GlobalQSVLoader;
  }
  if (Loader == nullptr)
    return true; // not ready yet — retry next call

  bool ok = false;
  mfxSession Session = nullptr;
  if (MFXCreateSession(Loader, 0, &Session) >= MFX_ERR_NONE) {
    try {
      MFXVideoENCODE Encode(Session);
      mfxVideoParam Params = {};
      Params.mfx.CodecId =
          (Codec == QSV_CODEC_HEVC) ? MFX_CODEC_HEVC : MFX_CODEC_AVC;
      Params.mfx.CodecProfile = (Codec == QSV_CODEC_HEVC)
                                    ? MFX_PROFILE_HEVC_MAIN
                                    : MFX_PROFILE_AVC_HIGH;
      Params.mfx.TargetUsage = MFX_TARGETUSAGE_4;
      Params.mfx.TargetKbps = 6000;
      Params.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
      Params.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
      Params.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
      Params.mfx.FrameInfo.Width = 1280;
      Params.mfx.FrameInfo.Height = 720;
      Params.mfx.FrameInfo.CropW = 1280;
      Params.mfx.FrameInfo.CropH = 720;
      Params.mfx.FrameInfo.FrameRateExtN = 30;
      Params.mfx.FrameInfo.FrameRateExtD = 1;
      Params.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
      Params.mfx.GopPicSize = 60;
      Params.mfx.GopRefDist = 1;  // intra refresh needs no B-frames
      Params.mfx.NumRefFrame = 1; // ...and a single reference
      Params.AsyncDepth = 4;
      Params.mfx.LowPower = MFX_CODINGOPTION_UNKNOWN;
      Params.mfx.BRCParamMultiplier = 1;

      mfxExtCodingOption2 CO2 = {};
      CO2.Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
      CO2.Header.BufferSz = sizeof(CO2);
      CO2.IntRefType = MFX_REFRESH_VERTICAL;
      CO2.IntRefCycleSize = 30;
      mfxExtBuffer *Ext[1] = {&CO2.Header};
      Params.NumExtParam = 1;
      Params.ExtParam = Ext;

      mfxStatus Sts = Encode.Query(&Params, &Params);
      bool kept = false;
      for (mfxU16 i = 0; i < Params.NumExtParam; ++i) {
        if (Params.ExtParam[i] &&
            Params.ExtParam[i]->BufferId == MFX_EXTBUFF_CODING_OPTION2) {
          auto *co2 = reinterpret_cast<mfxExtCodingOption2 *>(Params.ExtParam[i]);
          kept = (co2->IntRefType == MFX_REFRESH_VERTICAL);
          break;
        }
      }
      ok = (Sts >= MFX_ERR_NONE) && kept;
      Encode.Close();
    } catch (...) {
      ok = false;
    }
    MFXClose(Session);
  }

  {
    std::lock_guard<std::mutex> Lock(ProbeMutex);
    Probed[idx] = true;
    Supported[idx] = ok;
  }
  return ok;
}

enum class TargetUsageUIMode {
    Full,   // all 7 TU entries
    Three,  // collapsed to effective TU1/TU4/TU7
    Five,   // extended HEVC: effective TU1/TU2/TU4/TU6/TU7
};

// Decide how many TargetUsage choices the UI should expose so that every
// visible option maps to a distinct internal quality level.
static TargetUsageUIMode GetTargetUsageUIMode(enum codec_enum Codec) {
    mfxU16 platformCode = QueryPlatformCodeName();
    if (platformCode == 0)
        return TargetUsageUIMode::Full;

    switch (Codec) {
    case QSV_CODEC_VP9:
        // VP9 encoder maps 1-2->1, 3-5->4, 6-7->7 for every platform.
        return TargetUsageUIMode::Three;
    case QSV_CODEC_HEVC: {
        bool isBmgOrNewer = platformCode >= MFX_PLATFORM_BATTLEMAGE &&
                            platformCode != MFX_PLATFORM_ALDERLAKE_N;
        if (platformCode < MFX_PLATFORM_TIGERLAKE)
            return TargetUsageUIMode::Full;
        if (isBmgOrNewer)
            return TargetUsageUIMode::Five;
        return TargetUsageUIMode::Three;
    }
    default:
        // AV1 does not collapse. AVC behavior is not exposed in this GPU RT source;
        // stay conservative and show the full 7-option list.
        return TargetUsageUIMode::Full;
    }
}

static void SetDefaultEncoderParams(obs_data_t *Settings,
                                    enum codec_enum Codec) {
  auto mode = GetTargetUsageUIMode(Codec);
  if (mode == TargetUsageUIMode::Three) {
    obs_data_set_default_string(Settings, "target_usage",
                                "Balanced (TU3-TU5)");
  } else {
    obs_data_set_default_string(Settings, "target_usage", "TU4 (Balanced)");
  }
  obs_data_set_default_int(Settings, "bitrate", 6000);
  obs_data_set_default_int(Settings, "max_bitrate", 6000);
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
  obs_data_set_default_double(Settings, "accuracy", 10.0);  // 10.0% = driver default 100 (tenth-of-percent)
  obs_data_set_default_int(Settings, "convergence", 0);     // 0 = driver default → 65535

  if (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) {
    obs_data_set_default_double(Settings, "cqp", 23.0);
    obs_data_set_default_double(Settings, "qpi", 23.0);
    obs_data_set_default_double(Settings, "qpp", 23.0);
    obs_data_set_default_double(Settings, "qpb", 23.0);
  } else {
    obs_data_set_default_int(Settings, "cqp", 23);
    obs_data_set_default_int(Settings, "qpi", 23);
    obs_data_set_default_int(Settings, "qpp", 23);
    obs_data_set_default_int(Settings, "qpb", 23);
  }
  obs_data_set_default_bool(Settings, "cqp_separate_ipb", false);
  obs_data_set_default_int(Settings, "icq_quality", 23);

  obs_data_set_default_int(Settings, "keyint_sec", 4);
  obs_data_set_default_int(Settings, "b_frames", Codec == QSV_CODEC_VP9 ? 0 : 4);
  obs_data_set_default_int(Settings, "async_depth", 4);

  obs_data_set_default_string(Settings, "intra_ref_encoding", "OFF");
  obs_data_set_default_string(Settings, "low_delay_brc", "OFF");
  obs_data_set_default_string(Settings, "low_delay_hrd", "OFF");
  obs_data_set_default_string(Settings, "skip_frame", "AUTO");
  obs_data_set_default_string(Settings, "repartition_check", "AUTO");

  obs_data_set_default_string(Settings, "adaptive_i", "AUTO");
  obs_data_set_default_string(Settings, "adaptive_b", "AUTO");
#ifndef QSV_UHD600_SUPPORT
  obs_data_set_default_string(Settings, "adaptive_cqm", "AUTO");
#endif
  obs_data_set_default_string(Settings, "use_raw_ref", "AUTO");
  obs_data_set_default_string(Settings, "rdo", "AUTO");
  obs_data_set_default_string(Settings, "hrd_conformance", "AUTO");
  obs_data_set_default_string(Settings, "mbbrc", "AUTO");
  obs_data_set_default_string(Settings, "trellis", "AUTO");
  obs_data_set_default_int(Settings, "num_ref_frame", Codec == QSV_CODEC_VP9 ? 1 : 4);
  #ifndef QSV_UHD600_SUPPORT
  obs_data_set_default_string(Settings, "global_motion_bias_adjustment",
                              "AUTO");
  obs_data_set_default_string(Settings, "mv_cost_scaling_factor", "AUTO");
  obs_data_set_default_string(Settings, "direct_bias_adjustment", "AUTO");
#endif
  obs_data_set_default_string(Settings, "mv_overpic_boundaries", "AUTO");
  obs_data_set_default_int(Settings, "la_depth", 60);

  obs_data_set_default_int(Settings, "qvbr_quality", 0);

  obs_data_set_default_string(Settings, "lookahead", "OFF");
  obs_data_set_default_string(Settings, "lookahead_ds", "AUTO");
#ifdef ONEVPL_EXPERIMENTAL
  obs_data_set_default_string(Settings, "tune_quality", "OFF");
#endif
  obs_data_set_default_string(Settings, "enctools", "OFF");
  obs_data_set_default_string(Settings, "enc_tools_scene_change", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_ref_p", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_ref_b", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_ltr", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_pyramid_quant_p", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_pyramid_quant_b", "ON");
  obs_data_set_default_string(Settings, "enc_tools_adaptive_mbqp", "ON");
  obs_data_set_default_string(Settings, "enc_tools_brc_buffer_hints", "ON");
  obs_data_set_default_string(Settings, "enc_tools_brc", "ON");
  obs_data_set_default_string(Settings, "enc_tools_saliency_map_hint", "ON");
  #ifndef QSV_UHD600_SUPPORT
  obs_data_set_default_string(Settings, "hevc_sao", "AUTO");
#endif
  obs_data_set_default_string(Settings, "hevc_gpb", "AUTO");
  obs_data_set_default_string(Settings, "deblocking", "OFF");

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
#ifndef QSV_UHD600_SUPPORT
  obs_data_set_default_string(Settings, "vpp_mctf", "OFF");
  obs_data_set_default_int(Settings, "vpp_mctf_strength", 6);
#endif
  obs_data_set_default_int(Settings, "vpp_out_width", 0);
  obs_data_set_default_int(Settings, "vpp_out_height", 0);
  obs_data_set_default_string(Settings, "perc_enc_prefilter", "OFF");

  // New VPP filters defaults
  obs_data_set_default_string(Settings, "vpp_procamp", "OFF");
  obs_data_set_default_double(Settings, "vpp_procamp_brightness", 0.0);
  obs_data_set_default_double(Settings, "vpp_procamp_contrast", 1.0);
  obs_data_set_default_double(Settings, "vpp_procamp_hue", 0.0);
  obs_data_set_default_double(Settings, "vpp_procamp_saturation", 1.0);
  obs_data_set_default_string(Settings, "vpp_rotation", "OFF");
  obs_data_set_default_string(Settings, "vpp_mirroring", "OFF");
  obs_data_set_default_string(Settings, "vpp_frc", "OFF");
  obs_data_set_default_int(Settings, "vpp_frc_out_fps", 60);

  obs_data_set_default_string(Settings, "scenario_info", "AUTO");
  obs_data_set_default_string(Settings, "content_info", "AUTO");
  obs_data_set_default_string(Settings, "transform_skip", "AUTO");
  obs_data_set_default_string(Settings, "screen_content_tools", "AUTO");

  obs_data_set_default_int(Settings, "gpu_number", 0);

  obs_data_set_default_string(Settings, "min_qp", "-1");
  obs_data_set_default_string(Settings, "max_qp", "-1");

  obs_data_set_default_string(Settings, "weighted_pred", "AUTO");

  // AV1 coding options default to AUTO (driver decides)
  obs_data_set_default_string(Settings, "av1_cdef", "AUTO");
  obs_data_set_default_string(Settings, "av1_restoration", "AUTO");
  obs_data_set_default_string(Settings, "av1_loop_filter", "AUTO");
  obs_data_set_default_string(Settings, "av1_super_res", "AUTO");
  obs_data_set_default_string(Settings, "av1_error_resilient", "AUTO");
  obs_data_set_default_string(Settings, "av1_segmentation", "OFF");
  obs_data_set_default_string(Settings, "av1_interp_filter", "DEFAULT");

  // Debug group defaults
  obs_data_set_default_bool(Settings, "qp_statistics", true);
  obs_data_set_default_bool(Settings, "video_header_hex_dump", false);
  obs_data_set_default_bool(Settings, "frame_statistics", false);
}

static inline const char *LocaleKey(const char *str) {
  static thread_local char buf[128];
  size_t i;
  if (std::string_view(str) == "AUTO")
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
  // quick helper to set a property's visibility (null-safe)
  auto SetVisible = [&](const char *name, bool visible) {
    if (auto *p = obs_properties_get(Properties, name))
      obs_property_set_visible(p, visible);
  };

  auto sv = [](const char *s) { return std::string_view(s); };
  const char *rate_control = obs_data_get_string(Settings, "rate_control");

  bool bIsCBR  = sv(rate_control) == "CBR";
  bool bIsVBR  = sv(rate_control) == "VBR";
  bool bIsAVBR = sv(rate_control) == "AVBR";
  bool bIsCQP  = sv(rate_control) == "CQP";
  bool bIsICQ  = sv(rate_control) == "ICQ";
  bool bIsVCM  = sv(rate_control) == "VCM";
  bool bIsQVBR = sv(rate_control) == "QVBR";

  // Retrieve codec stored by GetParamProps (needed early for VCM codec gates)
  auto codec = static_cast<codec_enum>(
      reinterpret_cast<intptr_t>(obs_properties_get_param(Properties)));

  SetVisible("max_bitrate", bIsVBR || bIsVCM);
  SetVisible("bitrate", !(bIsCQP || bIsICQ));
  SetVisible("accuracy", bIsAVBR);
  SetVisible("convergence", bIsAVBR);
  // VCM is IPPP-only, no B-frames (H264 GopRefDist forced to 1;
  // HEVC uses VA_RC_VCM without MB BRC, conceptually IPPP)
  SetVisible("b_frames", !bIsVCM && codec != QSV_CODEC_VP9);
#ifdef QSV_UHD600_SUPPORT
  // UHD600 family: adaptive I/B is HEVC-rejected, keep it for H.264 only.
  SetVisible("adaptive_b", !bIsVCM && codec != QSV_CODEC_VP9 &&
                           codec != QSV_CODEC_HEVC);
#else
  SetVisible("adaptive_b", !bIsVCM && codec != QSV_CODEC_VP9);
#endif
  SetVisible("cqp_separate_ipb", bIsCQP);

  bool separateIPB = obs_data_get_bool(Settings, "cqp_separate_ipb");
  SetVisible("qpi", bIsCQP && separateIPB);
  SetVisible("qpb", bIsCQP && separateIPB);
  SetVisible("qpp", bIsCQP && separateIPB);
  SetVisible("cqp", bIsCQP && !separateIPB);

  SetVisible("icq_quality", bIsICQ && codec != QSV_CODEC_VP9);

  const char *low_power = obs_data_get_string(Settings, "low_power");
  const bool lowPowerOn = sv(low_power) == "ON";
  SetVisible("adaptive_cqm", codec == QSV_CODEC_AVC && lowPowerOn);

  // EncTools visibility: VP9 has no EncTools; other codecs share the same
  // rate-control visibility + platform gate.  Even though EncTools BRC
  // features (BRC, BRCBufferHints, AdaptiveMBQP) only work with CBR/VBR in
  // the oneVPL driver's SetDefaultConfig, the non-BRC features (SceneChange,
  // AdaptiveI, AdaptiveRef, PyramidQuant, etc.) are rate-control agnostic, so
  // we keep the existing broad RC visibility.
  bool bEncToolsVisible = (bIsCBR || bIsVBR || bIsAVBR || bIsQVBR)
                         && !bIsVCM
                         && codec != QSV_CODEC_VP9;
  bool bVisible = bEncToolsVisible;
  if (bVisible) bVisible = IsFeatureSupported("enc_tools");
  SetVisible("enctools", bVisible);

  const char *enctools = obs_data_get_string(Settings, "enctools");
  bool bVisibleEnctools = (sv(enctools) == "ON") && bVisible;

  // EncTools sub-options visibility (only when enc_tools is ON)
  for (const char *opt : {
    "enc_tools_scene_change", "enc_tools_adaptive_ref_p", "enc_tools_adaptive_ref_b",
    "enc_tools_adaptive_ltr",
    "enc_tools_adaptive_pyramid_quant_p", "enc_tools_adaptive_pyramid_quant_b",
    "enc_tools_adaptive_mbqp", "enc_tools_brc_buffer_hints", "enc_tools_brc",
    "enc_tools_saliency_map_hint"
  }) SetVisible(opt, bVisibleEnctools);

  SetVisible("qvbr_quality", bIsQVBR);

  const char *lookahead = obs_data_get_string(Settings, "lookahead");

  // Lookahead support per codec (verified against oneVPL vpl-gpu-rt 26.1.5):
  //   AVC: CBR/VBR/ICQ. VBR/ICQ are promoted to LA/LA_ICQ/LA_HRD (SW BRC).
  //        CBR keeps its RC mode but sets LookAheadDepth, which triggers
  //        EncTools LAGS hardware lookahead (IsEnctoolsLAGS in mfx_enc_common).
  //   HEVC/AV1: CBR/VBR only – lookahead works via GAME_STREAMING hardware
  //        EncTools, which requires EncTools platform support (TigerLake+).
  //   VP9: no lookahead mechanism at all.
  switch (codec) {
  case QSV_CODEC_AVC:
    bVisible = bIsCBR || bIsVBR || bIsICQ;
    break;
  case QSV_CODEC_HEVC:
  case QSV_CODEC_AV1:
    bVisible = (bIsCBR || bIsVBR) && IsFeatureSupported("enc_tools");
    break;
  case QSV_CODEC_VP9:
  default:
    bVisible = false;
    break;
  }
  SetVisible("lookahead", bVisible);
  // Force OFF when not visible so obsolete values don't linger
  if (!bVisible) obs_data_set_string(Settings, "lookahead", "OFF");

  bool bVisible_lookahead_hq = sv(lookahead) == "HQ";
  bool bVisible_lookahead_lp = sv(lookahead) == "LP";

  SetVisible("lookahead_ds", bVisible && bVisible_lookahead_hq);
  SetVisible("la_depth", bVisible && bVisible_lookahead_hq);

  if (bVisible_lookahead_lp) {
    obs_data_set_string(Settings, "enctools", "OFF");
  }

  bVisible = bIsCBR || bIsVBR || bIsAVBR || bIsQVBR || bIsICQ;
  // "mbbrc" control is not created for VP9 (codec disables MBBRC), so guard
  // against nullptr here.
  if (auto *mbbrc = obs_properties_get(Properties, "mbbrc")) {
    obs_property_set_visible(mbbrc, bVisible);
    if (!bVisible)
      obs_data_set_string(Settings, "mbbrc", "OFF");
  }

  bool bRateControlVisible = !bIsICQ && !bIsCQP;
  SetVisible("buffer_size", bRateControlVisible);

  const char *hrd_conformance =
      obs_data_get_string(Settings, "hrd_conformance");
  SetVisible("hrd_conformance", bRateControlVisible);
  if (!bRateControlVisible)
    obs_data_set_string(Settings, "hrd_conformance", "OFF");

  bVisible = bRateControlVisible && (sv(hrd_conformance) == "ON" ||
             sv(hrd_conformance) == "AUTO");
  SetVisible("low_delay_hrd", bVisible);

  bVisible = bIsVBR || bIsVCM || bIsQVBR;
  SetVisible("low_delay_brc", bVisible);
  if (!bVisible)
    obs_data_set_string(Settings, "low_delay_brc", "OFF");

  bool bMaxFrameSizeVisible = !(bIsCQP || bIsICQ);
  SetVisible("max_frame_size_mode", bMaxFrameSizeVisible);
  const char *MaxFrameSizeMode = obs_data_get_string(Settings, "max_frame_size_mode");
  // Backward compat: migrate old adaptive_max_frame_size to new mode
  if (MaxFrameSizeMode[0] == '\0' && bMaxFrameSizeVisible) {
    int64_t oldVal = obs_data_get_int(Settings, "adaptive_max_frame_size");
    if (oldVal > 0) {
      obs_data_set_string(Settings, "max_frame_size_mode", "all");
      obs_data_set_int(Settings, "max_frame_size_all", oldVal);
      MaxFrameSizeMode = "all";
    } else {
      obs_data_set_string(Settings, "max_frame_size_mode", "auto");
      MaxFrameSizeMode = "auto";
    }
  }
  bool bMaxFrameSizeAll = bMaxFrameSizeVisible && strcmp(MaxFrameSizeMode, "all") == 0;
  bool bMaxFrameSizePerType = bMaxFrameSizeVisible && strcmp(MaxFrameSizeMode, "per_type") == 0;
  SetVisible("max_frame_size_all", bMaxFrameSizeAll);
  SetVisible("max_frame_size_i", bMaxFrameSizePerType);
  SetVisible("max_frame_size_p", bMaxFrameSizePerType);
  if (!bMaxFrameSizeVisible) {
    obs_data_set_string(Settings, "max_frame_size_mode", "auto");
    obs_data_set_int(Settings, "max_frame_size_all", 0);
    obs_data_set_int(Settings, "max_frame_size_i", 0);
    obs_data_set_int(Settings, "max_frame_size_p", 0);
  }

  bVisible = !(bIsCQP || bIsICQ);
  SetVisible("min_qp", bVisible);
  SetVisible("max_qp", bVisible);

  #ifndef QSV_UHD600_SUPPORT
  const char *global_motion_bias_adjustment_enable =
      obs_data_get_string(Settings, "global_motion_bias_adjustment");
  bVisible = sv(global_motion_bias_adjustment_enable) == "ON";
  SetVisible("mv_cost_scaling_factor", bVisible);
  // Keep the stored value even when hidden: MVCostScalingFactor is only applied
  // when GlobalMotionBiasAdjustment is ON (see internal.cpp), and erasing it
  // here would permanently delete the user's configured choice from the profile.
#endif

  const char *vpp = obs_data_get_string(Settings, "vpp");
  bool bVisibleVPP = sv(vpp) == "ON";
  SetVisible("detail", bVisibleVPP);
  // ImageStab / FRC / Mirror / PercEnc are HW-dependent — probe the driver.
  SetVisible("image_stab_mode", bVisibleVPP && PlatformSupportsImageStabVPP());
  SetVisible("perc_enc_prefilter",
             bVisibleVPP && PlatformSupportsPercEncVPP());
  SetVisible("denoise_mode", bVisibleVPP);
  SetVisible("scaling_mode", bVisibleVPP);
  SetVisible("vpp_procamp", bVisibleVPP);
  SetVisible("vpp_rotation", bVisibleVPP);
  SetVisible("vpp_mirroring", bVisibleVPP && PlatformSupportsMirrorVPP());
  SetVisible("vpp_frc", bVisibleVPP && PlatformSupportsFRCVPP());
  const char *scaling_mode = obs_data_get_string(Settings, "scaling_mode");
  bool bScalingModeActive = sv(scaling_mode) != "OFF";
  SetVisible("vpp_out_width", bVisibleVPP && bScalingModeActive);
  SetVisible("vpp_out_height", bVisibleVPP && bScalingModeActive);
#ifndef QSV_UHD600_SUPPORT
  SetVisible("vpp_mctf", bVisibleVPP);

  const char *vpp_mctf_val = obs_data_get_string(Settings, "vpp_mctf");
  bool vpp_mctf_strength_visible = bVisibleVPP && sv(vpp_mctf_val) == "ON";
  SetVisible("vpp_mctf_strength", vpp_mctf_strength_visible);
#endif

  const char *denoise_mode = obs_data_get_string(Settings, "denoise_mode");
  if (PlatformSupportsDenoise2VPP()) {
    bVisible = sv(denoise_mode) == "MANUAL | PRE ENCODE" ||
               sv(denoise_mode) == "MANUAL | POST ENCODE";
    SetVisible("denoise_strength", bVisible && bVisibleVPP);
  } else {
    if (sv(denoise_mode) != "OFF" &&
        sv(denoise_mode) != "MANUAL | PRE ENCODE") {
      obs_data_set_string(Settings, "denoise_mode", "MANUAL | PRE ENCODE");
      denoise_mode = "MANUAL | PRE ENCODE";
    }
    SetVisible("denoise_strength", bVisibleVPP && sv(denoise_mode) != "OFF");
  }

  const char *detail = obs_data_get_string(Settings, "detail");
  bVisible = sv(detail) == "ON";
  SetVisible("detail_factor", bVisible && bVisibleVPP);

  // ProcAmp sub-controls: show when ProcAmp is ON
  const char *vpp_procamp = obs_data_get_string(Settings, "vpp_procamp");
  bool bProcAmpActive = bVisibleVPP && sv(vpp_procamp) == "ON";
  SetVisible("vpp_procamp_brightness", bProcAmpActive);
  SetVisible("vpp_procamp_contrast", bProcAmpActive);
  SetVisible("vpp_procamp_hue", bProcAmpActive);
  SetVisible("vpp_procamp_saturation", bProcAmpActive);

  const char *vpp_frc = obs_data_get_string(Settings, "vpp_frc");
  bool bFRCActive = bVisibleVPP && PlatformSupportsFRCVPP() &&
                    sv(vpp_frc) != "OFF";
  SetVisible("vpp_frc_out_fps", bFRCActive);

  const char *intra_ref_encoding =
      obs_data_get_string(Settings, "intra_ref_encoding");
  bVisible = sv(intra_ref_encoding) == "ON";
  SetVisible("intra_ref_type", bVisible);
  SetVisible("intra_ref_cycle_size", bVisible);
  SetVisible("intra_ref_qp_delta", bVisible);

  mfxU16 platformCode = QueryPlatformCodeName();
  // HEVC High Tier is supported on SKL+ (subject to level >= 4 spec constraint)
  bool hasHighTier = platformCode == 0 ||
                     platformCode >= MFX_PLATFORM_SKYLAKE;
  bool showTierList = hasHighTier;
  if (auto *tier = obs_properties_get(Properties, "hevc_tier")) {
    obs_property_set_visible(tier, showTierList);
    if (!showTierList)
      obs_data_set_string(Settings, "hevc_tier", "main");
  }

  // Custom quant matrix cascade: quant_matrix == "custom" -> granularity ->
  // the matching per-list input boxes (all H.264 only, boxes hidden by
  // default in GetParamProps).
  const char *qmSel = obs_data_get_string(Settings, "quant_matrix");
  const bool qmCustom = sv(qmSel) == "custom" && codec == QSV_CODEC_AVC;
  SetVisible("qm_granularity", qmCustom);
  const int qmGran = static_cast<int>(obs_data_get_int(Settings, "qm_granularity"));
  const char *boxes2[2] = {"qm_4x4", "qm_8x8"};
  const char *boxes4[4] = {"qm_i4", "qm_p4", "qm_i8", "qm_p8"};
  const char *boxesFull[6] = {"qm_i4y", "qm_p4y", "qm_i8y",
                              "qm_p8y", "qm_ci4", "qm_cp4"};
  for (const char *b : boxes2) SetVisible(b, qmCustom && qmGran <= 0);
  for (const char *b : boxes4) SetVisible(b, qmCustom && qmGran == 1);
  for (const char *b : boxesFull) SetVisible(b, qmCustom && qmGran >= 2);

  return true;
}

static obs_properties_t *GetParamProps(enum codec_enum Codec) {

  obs_properties_t *Props = obs_properties_create();
  // Store codec so ParamsVisibilityModifier can branch per-codec
  obs_properties_set_param(
      Props, reinterpret_cast<void *>(static_cast<intptr_t>(Codec)), nullptr);
  obs_property_t *Prop;
  mfxU16 platformCode = QueryPlatformCodeName();

  obs_properties_t *RCGroup = obs_properties_create();

  Prop = obs_properties_add_list(RCGroup, "target_usage", TEXT_SPEED,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  switch (GetTargetUsageUIMode(Codec)) {
  case TargetUsageUIMode::Three:
    AddStrings(Prop, qsv_usage_names_simple);
    break;
  case TargetUsageUIMode::Five:
    AddStrings(Prop, qsv_usage_names_five);
    break;
  default:
    AddStrings(Prop, qsv_usage_names);
  }
  obs_property_set_long_description(Prop, TEXT_TARGET_USAGE_DESC);

  Prop = obs_properties_add_list(RCGroup, "rate_control", TEXT_RATE_CONTROL,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(Prop, TEXT_RATE_CONTROL_DESC);
  {
    const struct qsv_rate_control_info *rcInfo = qsv_rate_control_info_list;
    while (rcInfo->name) {
      if (platformCode == 0 || platformCode >= rcInfo->min_platform) {
      // AV1 only supports CBR/VBR/CQP/ICQ (AVBR silently falls back to VBR,
      // QVBR/VCM not in driver's CheckRateControl at all)
      // HEVC encoder does not support AVBR; VP9 only supports CBR/VBR/CQP/ICQ
      auto sv = [](const char *s) { return std::string_view(s); };
      bool skipForAV1 = Codec == QSV_CODEC_AV1 &&
                        (sv(rcInfo->name) == "AVBR" ||
                         sv(rcInfo->name) == "VCM" ||
                         sv(rcInfo->name) == "QVBR");
      bool skipForHEVC = Codec == QSV_CODEC_HEVC &&
                         (sv(rcInfo->name) == "AVBR"
#ifdef QSV_UHD600_SUPPORT
                          // UHD620 HEVC rejects QVBR (QSVEncC: x); 730 accepts.
                          || sv(rcInfo->name) == "QVBR"
#endif
                         );
      bool skipForVP9 = Codec == QSV_CODEC_VP9 &&
                        (sv(rcInfo->name) == "AVBR" ||
                         sv(rcInfo->name) == "QVBR" ||
                         sv(rcInfo->name) == "VCM");
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

  // VP9 ICQQuality slider is hidden — VP9 driver's VAAPI layer ignores
  // ICQ_quality_factor entirely (driver bug), so the value has no effect.
  {
    int icqMax = (Codec == QSV_CODEC_VP9) ? 63 : 51;
    Prop = obs_properties_add_int_slider(RCGroup, "icq_quality",
                                         TEXT_ICQ_QUALITY, 1, icqMax, 1);
    obs_property_set_long_description(Prop, TEXT_ICQ_QUALITY_DESC);
    obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);
  }

  Prop = obs_properties_add_bool(RCGroup, "cqp_separate_ipb",
                                 TEXT_SEPARATE_IPB_QP);
  obs_property_set_long_description(Prop, TEXT_SEPARATE_IPB_QP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  if (Codec == QSV_CODEC_AV1 || Codec == QSV_CODEC_VP9) {
    // VP9/AV1 support fractional QP (0.25 increments). Internal base_q_idx
    // range 0-255; UI exposes 0.0-63.0 with 0.25 step → *4 gives 0-252.
    // QP=0 means lossless in AV1 (base_q_idx=0).
    Prop = obs_properties_add_float_slider(RCGroup, "qpi", TEXT_QPI, 0.0, 63.0, 0.25);
    obs_property_set_long_description(Prop, TEXT_QP_DESC);
    Prop = obs_properties_add_float_slider(RCGroup, "qpp", TEXT_QPP, 0.0, 63.0, 0.25);
    obs_property_set_long_description(Prop, TEXT_QP_DESC);
    Prop = obs_properties_add_float_slider(RCGroup, "qpb", TEXT_QPB, 0.0, 63.0, 0.25);
    obs_property_set_long_description(Prop, TEXT_QP_DESC);

    Prop = obs_properties_add_float_slider(RCGroup, "cqp", TEXT_CQP, 0.0, 63.0, 0.25);
    obs_property_set_long_description(Prop, TEXT_CQP_DESC);
  } else {
    Prop = obs_properties_add_int_slider(RCGroup, "qpi", TEXT_QPI, 1, 51, 1);
    obs_property_set_long_description(Prop, TEXT_QP_DESC);
    Prop = obs_properties_add_int_slider(RCGroup, "qpp", TEXT_QPP, 1, 51, 1);
    obs_property_set_long_description(Prop, TEXT_QP_DESC);
    Prop = obs_properties_add_int_slider(RCGroup, "qpb", TEXT_QPB, 1, 51, 1);
    obs_property_set_long_description(Prop, TEXT_QP_DESC);

    Prop = obs_properties_add_int_slider(RCGroup, "cqp", TEXT_CQP, 1, 51, 1);
    obs_property_set_long_description(Prop, TEXT_CQP_DESC);
  }

  Prop = obs_properties_add_int(RCGroup, "bitrate", TEXT_TARGET_BITRATE, 50,
                                6553500, 1000);
  obs_property_int_set_suffix(Prop, " Kbps");
  obs_property_set_long_description(Prop, TEXT_BITRATE_DESC);

  Prop = obs_properties_add_int(RCGroup, "max_bitrate", TEXT_MAX_BITRATE, 50,
                                6553500, 1000);
  obs_property_int_set_suffix(Prop, " Kbps");
  obs_property_set_long_description(Prop, TEXT_MAX_BITRATE_DESC);

  // AVBR-specific: Convergence (0~65535, 1 = 100 frames, 0 = driver default)
  Prop = obs_properties_add_int(RCGroup, "convergence", TEXT_CONVERGENCE,
                               0, 65535, 1);
  obs_property_set_long_description(Prop, TEXT_CONVERGENCE_DESC);
  // Accuracy (0.0~100.0%, UI float, *10 → tenth-of-percent for driver)
  Prop = obs_properties_add_float_slider(RCGroup, "accuracy", TEXT_ACCURACY,
                                        0.0, 100.0, 0.1);
  obs_property_set_long_description(Prop, TEXT_ACCURACY_DESC);

  Prop = obs_properties_add_int(RCGroup, "buffer_size", TEXT_BUFFER_SIZE, 0,
                                6553500, 1000);
  obs_property_int_set_suffix(Prop, " KB");
  obs_property_set_long_description(Prop, TEXT_BUFFER_SIZE_DESC);

  Prop = obs_properties_add_text(RCGroup, "min_qp", TEXT_MIN_QP,
                                 OBS_TEXT_DEFAULT);
  obs_property_set_long_description(Prop, TEXT_MIN_QP_DESC);

  Prop = obs_properties_add_text(RCGroup, "max_qp", TEXT_MAX_QP,
                                 OBS_TEXT_DEFAULT);
  obs_property_set_long_description(Prop, TEXT_MAX_QP_DESC);

  Prop = obs_properties_add_list(RCGroup, "max_frame_size_mode",
                                TEXT_MAX_FRAME_SIZE_MODE,
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(Prop, TEXT_MAX_FRAME_SIZE_MODE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_list_add_string(Prop, TEXT_MAX_FRAME_SIZE_MODE_AUTO, "auto");
  obs_property_list_add_string(Prop, TEXT_MAX_FRAME_SIZE_MODE_ALL, "all");
  obs_property_list_add_string(Prop, TEXT_MAX_FRAME_SIZE_MODE_PER_TYPE, "per_type");

  Prop = obs_properties_add_int(RCGroup, "max_frame_size_all",
                                TEXT_MAX_FRAME_SIZE_ALL, 0, 2147483647, 100);
  obs_property_set_long_description(Prop, TEXT_MAX_FRAME_SIZE_ALL_DESC);
  obs_property_int_set_suffix(Prop, " bytes");

  Prop = obs_properties_add_int(RCGroup, "max_frame_size_i",
                                TEXT_MAX_FRAME_SIZE_I, 0, 2147483647, 100);
  obs_property_set_long_description(Prop, TEXT_MAX_FRAME_SIZE_I_DESC);
  obs_property_int_set_suffix(Prop, " bytes");

  Prop = obs_properties_add_int(RCGroup, "max_frame_size_p",
                                TEXT_MAX_FRAME_SIZE_P, 0, 2147483647, 100);
  obs_property_set_long_description(Prop, TEXT_MAX_FRAME_SIZE_P_DESC);
  obs_property_int_set_suffix(Prop, " bytes");

  // VBV settings at bottom of Rate Control group
  Prop = obs_properties_add_list(RCGroup, "hrd_conformance",
                                 TEXT_HRD_CONFORMANCE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_HRD_CONFORMANCE_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);

  Prop = obs_properties_add_list(RCGroup, "low_delay_hrd", TEXT_LOW_DELAY_HRD,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_LOW_DELAY_HRD_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);

  Prop = obs_properties_add_list(RCGroup, "low_delay_brc", TEXT_LOW_DELAY_BRC,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_LOW_DELAY_BRC_DESC);
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);

  Prop = obs_properties_add_list(RCGroup, "brc_panic_mode", TEXT_BRC_PANIC_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_BRC_PANIC_MODE_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);

  Prop = obs_properties_add_list(RCGroup, "skip_frame", TEXT_SKIP_FRAME,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_skip_frame_mode);
  obs_property_set_long_description(Prop, TEXT_SKIP_FRAME_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC ||
                           Codec == QSV_CODEC_HEVC);

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

  // Codec-specific NumRefFrame max (per driver caps):
  //   VP9: 3, AV1: 8, AVC/HEVC: 16
  int ref_max = (Codec == QSV_CODEC_VP9) ? 3 : (Codec == QSV_CODEC_AV1) ? 8 : 16;
  Prop = obs_properties_add_int(IFGroup, "num_ref_frame", TEXT_NUM_REF_FRAME,
                                0, ref_max, 1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("NumRefFrame.Tooltip"));

  // AdaptiveB sits directly above the B-frame input: when ON the encoder
  // forces GAME_STREAMING (see SetEncoderParams), which keeps CO2.AdaptiveB
  // active for any B-frame count.
  Prop = obs_properties_add_list(IFGroup, "adaptive_b", TEXT_ADAPTIVE_B,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_B_DESC);
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9 &&
#ifdef QSV_UHD600_SUPPORT
                                     Codec != QSV_CODEC_HEVC
#else
                                     true
#endif
  );

  Prop = obs_properties_add_int(IFGroup, "b_frames", TEXT_B_FRAMES, 0,
                                65534, 1);
  obs_property_set_long_description(Prop, TEXT_B_FRAMES_DESC);
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);

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
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC ||
                           Codec == QSV_CODEC_HEVC);

  Prop = obs_properties_add_list(IFGroup, "use_raw_ref", TEXT_USE_RAW_REF,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_USE_RAW_REF_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC ||
                           Codec == QSV_CODEC_HEVC);

  obs_properties_t *ETGroup = obs_properties_create();

#ifdef ONEVPL_EXPERIMENTAL
  Prop = obs_properties_add_list(ETGroup, "tune_quality", TEXT_TUNE_QUALITY,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_tune_quality);
  obs_property_set_long_description(Prop, TEXT_TUNE_QUALITY_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AV1);
#endif

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

  Prop = obs_properties_add_list(ETGroup, "enc_tools_adaptive_ltr",
                                 TEXT_ENC_TOOLS_ADAPTIVE_LTR,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_ENC_TOOLS_ADAPTIVE_LTR_DESC);

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

  Prop = obs_properties_add_list(ETGroup, "gop_opt_flag", TEXT_GOP_OPT_FLAG,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_gop_opt_flag);
  obs_property_set_long_description(Prop, TEXT_GOP_OPT_FLAG_DESC);
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);

  Prop = obs_properties_add_list(ETGroup, "adaptive_i", TEXT_ADAPTIVE_I,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_I_DESC);
  // UHD600 GPU family: adaptive I/B only exists on H.264 (FF CBR/VBR);
  // HEVC rejects them (QSVEncC on UHD620: all x).
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9 &&
#ifdef QSV_UHD600_SUPPORT
                                 Codec != QSV_CODEC_HEVC
#else
                                 true
#endif
  );

#ifndef QSV_UHD600_SUPPORT
  Prop = obs_properties_add_list(ETGroup, "adaptive_cqm", TEXT_ADAPTIVE_CQM,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_ADAPTIVE_CQM_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC); // H264 only; LowPower gate in ParamsVisibilityModifier
#endif

  Prop = obs_properties_add_list(ETGroup, "trellis", TEXT_TRELLIS,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_trellis);
  obs_property_set_long_description(Prop, TEXT_TRELLIS_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);

  Prop = obs_properties_add_list(ETGroup, "repartition_check",
                                 TEXT_REPARTITION_CHECK,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_REPARTITION_CHECK_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);

  Prop = obs_properties_add_list(ETGroup, "rdo", TEXT_RDO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_RDO_DESC);
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);

  Prop = obs_properties_add_list(ETGroup, "transform_skip",
                                 TEXT_TRANSFORM_SKIP,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_TRANSFORM_SKIP_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_HEVC &&
                           IsFeatureSupported("transform_skip"));

  if (Codec == QSV_CODEC_AVC || Codec == QSV_CODEC_HEVC) {
    Prop = obs_properties_add_list(ETGroup, "deblocking", TEXT_DEBLOCKING,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition);
    obs_property_set_long_description(Prop, TEXT_DEBLOCKING_DESC);
  }

  obs_properties_t *RMGroup = obs_properties_create();

  const bool bIsAVCOrHEVC = Codec == QSV_CODEC_AVC || Codec == QSV_CODEC_HEVC;

  #ifndef QSV_UHD600_SUPPORT
  Prop = obs_properties_add_list(RMGroup, "global_motion_bias_adjustment",
                                 TEXT_GLOBAL_MOTION_BIAS_ADJUSTMENT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_long_description(Prop, TEXT_GLOBAL_MOTION_BIAS_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC); // HME only for AVC

  Prop = obs_properties_add_list(RMGroup, "mv_cost_scaling_factor",
                                 TEXT_MV_COST_SCALING_FACTOR,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_mv_cost_scaling);
  obs_property_set_long_description(Prop,
                                    obs_module_text("MVCostScalingFactor.Tooltip"));
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC); // HME only for AVC

  Prop = obs_properties_add_list(RMGroup, "direct_bias_adjustment",
                                 TEXT_DIRECT_BIAS_ADJUSTMENT,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(Prop, TEXT_DIRECT_BIAS_DESC);
  // H.264 only: no HEVC encoder in vpl-gpu-rt implements DirectBiasAdjustment.
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);
#endif

  Prop = obs_properties_add_list(RMGroup, "mv_overpic_boundaries",
                                 TEXT_MV_OVER_PIC_BOUNDARIES,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_tristate);
  obs_property_set_long_description(
      Prop, TEXT_MV_OVER_PIC_BOUNDARIES_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);

  Prop = obs_properties_add_list(RMGroup, "weighted_pred",
                                 TEXT_WEIGHTED_PRED,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_weighted_pred_options);
  obs_property_set_long_description(Prop, TEXT_WEIGHTED_PRED_DESC);
  obs_property_set_visible(Prop, bIsAVCOrHEVC);

  obs_properties_t *VFGroup = obs_properties_create();

  Prop = obs_properties_add_list(VFGroup, "vpp", TEXT_VPP,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_VPP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_list(VFGroup, "denoise_mode", TEXT_DENOISE_MODE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  if (PlatformSupportsDenoise2VPP()) {
    AddStrings(Prop, qsv_params_condition_denoise_mode);
    obs_property_set_long_description(Prop, TEXT_DENOISE_MODE_DESC);
  } else {
    AddStrings(Prop, qsv_params_condition_denoise_mode_legacy);
    obs_property_set_long_description(Prop, TEXT_DENOISE_MODE_LEGACY_DESC);
  }
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int_slider(VFGroup, "denoise_strength",
                                TEXT_DENOISE_STRENGTH, 1, 100, 1);
  obs_property_set_long_description(
      Prop, PlatformSupportsDenoise2VPP() ? TEXT_DENOISE_STRENGTH_DESC
                                          : TEXT_DENOISE_STRENGTH_LEGACY_DESC);

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

#ifndef QSV_UHD600_SUPPORT
  Prop = obs_properties_add_list(VFGroup, "vpp_mctf", TEXT_VPP_MCTF,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition);
  obs_property_set_long_description(Prop, TEXT_VPP_MCTF_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int_slider(VFGroup, "vpp_mctf_strength",
                                       TEXT_VPP_MCTF_STRENGTH, 0, 20, 1);
  obs_property_set_long_description(Prop, TEXT_VPP_MCTF_STRENGTH_DESC);
#endif

  // ProcAmp (color adjustment)
  Prop = obs_properties_add_list(VFGroup, "vpp_procamp", TEXT_VPP_PROCAMP,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_procamp);
  obs_property_set_long_description(Prop, TEXT_VPP_PROCAMP_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_float_slider(VFGroup, "vpp_procamp_brightness",
                                         TEXT_VPP_PROCAMP_BRIGHTNESS, -100.0, 100.0, 0.5);
  obs_property_set_long_description(Prop, TEXT_VPP_PROCAMP_BRIGHTNESS_DESC);
  obs_property_set_visible(Prop, false);

  Prop = obs_properties_add_float_slider(VFGroup, "vpp_procamp_contrast",
                                         TEXT_VPP_PROCAMP_CONTRAST, 0.0, 10.0, 0.05);
  obs_property_set_long_description(Prop, TEXT_VPP_PROCAMP_CONTRAST_DESC);
  obs_property_set_visible(Prop, false);

  Prop = obs_properties_add_float_slider(VFGroup, "vpp_procamp_hue",
                                         TEXT_VPP_PROCAMP_HUE, -180.0, 180.0, 1.0);
  obs_property_set_long_description(Prop, TEXT_VPP_PROCAMP_HUE_DESC);
  obs_property_set_visible(Prop, false);

  Prop = obs_properties_add_float_slider(VFGroup, "vpp_procamp_saturation",
                                         TEXT_VPP_PROCAMP_SATURATION, 0.0, 10.0, 0.05);
  obs_property_set_long_description(Prop, TEXT_VPP_PROCAMP_SATURATION_DESC);
  obs_property_set_visible(Prop, false);

  // Rotation
  Prop = obs_properties_add_list(VFGroup, "vpp_rotation", TEXT_VPP_ROTATION,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_rotation);
  obs_property_set_long_description(Prop, TEXT_VPP_ROTATION_DESC);

  // Mirroring
  Prop = obs_properties_add_list(VFGroup, "vpp_mirroring", TEXT_VPP_MIRRORING,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_mirroring);
  obs_property_set_long_description(Prop, TEXT_VPP_MIRRORING_DESC);

  // Frame Rate Conversion
  Prop = obs_properties_add_list(VFGroup, "vpp_frc", TEXT_VPP_FRC,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_frc);
  obs_property_set_long_description(Prop, TEXT_VPP_FRC_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  Prop = obs_properties_add_int(VFGroup, "vpp_frc_out_fps",
                                TEXT_VPP_FRC_OUT_FPS, 1, 1000, 1);
  obs_property_set_long_description(Prop, TEXT_VPP_FRC_OUT_FPS_DESC);
  obs_property_set_visible(Prop, false);

  obs_properties_t *CSGroup = obs_properties_create();

  Prop = obs_properties_add_list(CSGroup, "profile", TEXT_PROFILE,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

  if (Codec == QSV_CODEC_AVC) {
    obs_property_set_long_description(Prop, TEXT_PROFILE_DESC_AVC);
    const char *const *profileEntryH264 = qsv_profile_names_h264;
    while (*profileEntryH264) {
      obs_property_list_add_string(Prop, *profileEntryH264,
                                   *profileEntryH264);
      profileEntryH264++;
    }
  } else if (Codec == QSV_CODEC_AV1) {
    obs_property_set_long_description(Prop, TEXT_PROFILE_DESC_AV1);
    AddStrings(Prop, qsv_profile_names_av1);
  } else if (Codec == QSV_CODEC_VP9) {
    obs_property_set_long_description(Prop, TEXT_PROFILE_DESC_VP9);
    AddStrings(Prop, qsv_profile_names_vp9);
  } else if (Codec == QSV_CODEC_HEVC) {
    obs_property_set_long_description(Prop, TEXT_PROFILE_DESC_HEVC);
    const char *const *profileEntryHEVC = qsv_profile_names_hevc;
    while (*profileEntryHEVC) {
      bool showProfileHEVC = true;
      if (platformCode != 0) {
        bool isRext = std::string_view(*profileEntryHEVC) == "rext";
        bool isSCC = std::string_view(*profileEntryHEVC) == "scc";
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
      bool isHigh = std::string_view(*tierEntry) == "high";
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

    #ifndef QSV_UHD600_SUPPORT
    Prop = obs_properties_add_list(CSGroup, "hevc_sao", TEXT_HEVC_SAO,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_hevc_sao);
    obs_property_set_long_description(Prop, TEXT_HEVC_SAO_DESC);
#endif
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

    Prop = obs_properties_add_list(CSGroup, "av1_segmentation",
                                   TEXT_AV1_SEGMENTATION,
                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    AddStrings(Prop, qsv_params_condition_tristate);
    obs_property_set_long_description(Prop, TEXT_AV1_SEGMENTATION_DESC);
  }

  obs_properties_t *IRGroup = obs_properties_create();

  // IntraRefresh is a rolling HW capability (RollingIntraRefresh).  Skip the
  // whole group when the driver would zero it out / fail Init over it.
  if (Codec != QSV_CODEC_AV1 && Codec != QSV_CODEC_VP9 &&
      PlatformSupportsIntraRefreshEncode(Codec)) {
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
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);

  Prop = obs_properties_add_int(MXGroup, "async_depth", TEXT_ASYNC_DEPTH,
                                1, 1000, 1);
  obs_property_set_long_description(Prop,
                                    obs_module_text("AsyncDepth.Tooltip"));

  Prop = obs_properties_add_list(MXGroup, "scenario_info", TEXT_SCENARIO_INFO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_scenario_info);
  obs_property_set_long_description(Prop, TEXT_SCENARIO_INFO_DESC);
#ifdef QSV_UHD600_SUPPORT
  // UHD620 rejects CO3 ScenarioInfo on HEVC (all x); H.264 still accepts it.
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC ||
                                 Codec == QSV_CODEC_AV1);
#endif

  Prop = obs_properties_add_list(MXGroup, "content_info", TEXT_CONTENT_INFO,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  AddStrings(Prop, qsv_params_condition_content_info);
  obs_property_set_long_description(Prop, TEXT_CONTENT_INFO_DESC);
  obs_property_set_visible(Prop, Codec != QSV_CODEC_VP9);

  Prop = obs_properties_add_int(MXGroup, "gpu_number", TEXT_GPU_NUMBER,
                                0, 4, 1);
  obs_property_set_long_description(Prop, TEXT_GPU_NUMBER_DESC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);

  // Chroma QP offset (H.264 only): shifts chroma quantization relative to
  // luma (applied via SPS/PPS injection at encoder init).
  Prop = obs_properties_add_int_slider(MXGroup, "chroma_qp_offset",
                                       TEXT_CHROMA_QP_OFFSET, -12, 12, 1);
  obs_property_set_long_description(Prop, TEXT_CHROMA_QP_OFFSET_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);

  // Custom quant matrix (H.264 only): rewrites SPS/PPS scaling lists at init.
  Prop = obs_properties_add_list(MXGroup, "quant_matrix",
                                 TEXT_QUANT_MATRIX,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_set_long_description(Prop, TEXT_QUANT_MATRIX_DESC);
  obs_property_set_visible(Prop, Codec == QSV_CODEC_AVC);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_DEFAULT, "default");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_DRV_FLAT, "drv_flat");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_DRV_WEAK, "drv_weak");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_DRV_MEDIUM,
                               "drv_medium");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_DRV_STRONG,
                               "drv_strong");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_DRV_EXTREME,
                               "drv_extreme");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_TEX_DETAIL,
                               "tex_detail");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_TEX_STRONG,
                               "tex_strong");
  obs_property_list_add_string(Prop, TEXT_QUANT_MATRIX_CUSTOM, "custom");

  // Custom granularity selector + per-list input boxes. Only shown when
  // quant_matrix == "custom" (visibility handled in ParamsVisibilityModifier).
  Prop = obs_properties_add_list(MXGroup, "qm_granularity",
                                 TEXT_QM_GRANULARITY,
                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
  obs_property_set_modified_callback(Prop, ParamsVisibilityModifier);
  obs_property_set_visible(Prop, false);
  obs_property_list_add_int(Prop, TEXT_QM_GRAN_2, 0);
  obs_property_list_add_int(Prop, TEXT_QM_GRAN_4, 1);
  obs_property_list_add_int(Prop, TEXT_QM_GRAN_FULL, 2);

  auto AddMatrixBox = [&](const char *Id, const char *Label) {
    auto *BoxProp =
        obs_properties_add_text(MXGroup, Id, Label, OBS_TEXT_MULTILINE);
    obs_property_set_long_description(BoxProp, TEXT_QM_BOX_DESC);
    obs_property_set_visible(BoxProp, false);
    return BoxProp;
  };
  // 2-list mode: one 4x4 (I/P & chroma shared) + one 8x8 (I/P shared)
  AddMatrixBox("qm_4x4", TEXT_QM_4X4);
  AddMatrixBox("qm_8x8", TEXT_QM_8X8);
  // 4-list mode: I/P separated, chroma copies luma
  AddMatrixBox("qm_i4", TEXT_QM_I4);
  AddMatrixBox("qm_p4", TEXT_QM_P4);
  AddMatrixBox("qm_i8", TEXT_QM_I8);
  AddMatrixBox("qm_p8", TEXT_QM_P8);
  // Full mode: luma 4x4/8x8 by I/P + chroma 4x4 by I/P
  AddMatrixBox("qm_i4y", TEXT_QM_I4Y);
  AddMatrixBox("qm_p4y", TEXT_QM_P4Y);
  AddMatrixBox("qm_i8y", TEXT_QM_I8Y);
  AddMatrixBox("qm_p8y", TEXT_QM_P8Y);
  AddMatrixBox("qm_ci4", TEXT_QM_CI4);
  AddMatrixBox("qm_cp4", TEXT_QM_CP4);

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

  // Debug group (bottom)
  obs_properties_t *DBGGroup = obs_properties_create();
  Prop = obs_properties_add_bool(DBGGroup, "qp_statistics", TEXT_QP_STATS);
  obs_property_set_long_description(Prop, TEXT_QP_STATS_DESC);
  Prop = obs_properties_add_bool(DBGGroup, "video_header_hex_dump",
                                 TEXT_VIDEO_HEADER_DUMP);
  obs_property_set_long_description(Prop, TEXT_VIDEO_HEADER_DUMP_DESC);
  Prop = obs_properties_add_bool(DBGGroup, "frame_statistics",
                                 TEXT_FRAME_STATS);
  obs_property_set_long_description(Prop, TEXT_FRAME_STATS_DESC);
  obs_properties_add_group(Props, "group_debug",
                           TEXT_GROUP_DEBUG,
                           OBS_GROUP_NORMAL, DBGGroup);

  return Props;
}

// UI-to-encoder_params parsing is shared with the offline re-encoder via
// helpers/encoder_params_parser.hpp.  This function only adds OBS video-output
// derived fields (resolution, fps, color info, format) on top of it.
static void GetEncoderParams(plugin_context *Context, obs_data_t *Settings) {
  video_t *Video = obs_encoder_video(Context->EncoderData);
  if (!Video)
    return; // no video output attached yet — keep encoder params at defaults
  const video_output_info *VOI = video_output_get_info(Video);
  const char *Codec = "";

  switch (Context->Codec) {
  case QSV_CODEC_AVC:
    Codec = "H.264";
    break;
  case QSV_CODEC_HEVC:
    Codec = "HEVC";
    break;
  case QSV_CODEC_AV1:
    Codec = "AV1";
    break;
  case QSV_CODEC_VP9:
    Codec = "VP9";
    break;
  }

  // All UI-configurable fields are parsed by the shared parser in
  // helpers/encoder_params_parser.hpp — the single source of truth.
  // This function only adds the OBS video-output derived fields below.
  ParseEncoderParamsFromObsData(Settings, Context->Codec,
                                Context->EncoderParams);

  Context->CachedFpsNum = static_cast<mfxU32>(VOI->fps_num);
  Context->CachedFpsDen = static_cast<mfxU32>(VOI->fps_den);
  Context->CachedTSDiv = 90000 * static_cast<int64_t>(VOI->fps_den);

  Context->EncoderParams.Width =
      static_cast<mfxU16>(obs_encoder_get_width(Context->EncoderData));
  Context->EncoderParams.Height =
      static_cast<mfxU16>(obs_encoder_get_height(Context->EncoderData));
  Context->EncoderParams.FpsNum = static_cast<mfxU32>(VOI->fps_num);
  Context->EncoderParams.FpsDen = static_cast<mfxU32>(VOI->fps_den);

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

  switch (VOI->format) {
  default:
  case VIDEO_FORMAT_NV12:
    Context->EncoderParams.FourCC = MFX_FOURCC_NV12;
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    break;
  case VIDEO_FORMAT_P010:
    Context->EncoderParams.FourCC = MFX_FOURCC_P010;
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    Context->EncoderParams.BitDepth = 10;
    break;
  case VIDEO_FORMAT_AYUV:
  case VIDEO_FORMAT_I444:
    // oneVPL 4:4:4 8-bit is packed AYUV.  I444 is planar, so GetVideoInfo
    // keeps I444 and LoadFrameData packs the three planes into VUYA.
    Context->EncoderParams.FourCC = MFX_FOURCC_AYUV;
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV444;
    break;
  case VIDEO_FORMAT_YUY2:
    Context->EncoderParams.FourCC = MFX_FOURCC_YUY2;
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV422;
    break;
  case VIDEO_FORMAT_P216:
    // OBS P216 is two-plane 4:2:2 10-bit (stored as 16-bit samples);
    // oneVPL wants packed Y216. LoadFrameData performs the layout conversion.
    Context->EncoderParams.FourCC = MFX_FOURCC_Y216;
    Context->EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV422;
    Context->EncoderParams.BitDepth = 10;
    break;
  case VIDEO_FORMAT_P416:
  case VIDEO_FORMAT_I412:
    // OBS P416 is two-plane 4:4:4 16-bit; oneVPL wants packed Y416.
    // I412 is planar 12-bit and will be converted to P416 by OBS first.
    Context->EncoderParams.FourCC = MFX_FOURCC_Y416;
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

  const char *VideoProcessingStatusData = obs_data_get_string(Settings, "vpp");
  Context->EncoderParams.ProcessingEnable = false;
  if ((Context->EncoderParams.VPPDenoiseMode.has_value() ||
       Context->EncoderParams.VPPDetail.has_value() ||
       Context->EncoderParams.VPPScalingMode.has_value() ||
       Context->EncoderParams.VPPImageStabMode.has_value() ||
       Context->EncoderParams.PercEncPrefilter == true ||
       Context->EncoderParams.VPPProcAmpMode.has_value() ||
       Context->EncoderParams.VPPRotation.has_value() ||
       Context->EncoderParams.VPPMirroring.has_value() ||
       Context->EncoderParams.VPPFRCMode.has_value()
#ifndef QSV_UHD600_SUPPORT
       || Context->EncoderParams.VPPMCTFMode.has_value()
#endif
      ) &&
      std::string_view(VideoProcessingStatusData) == "ON") {
    if (VOI->format == VIDEO_FORMAT_NV12) {
      Context->EncoderParams.ProcessingEnable = true;
    } else if (VOI->format == VIDEO_FORMAT_P010 ||
               VOI->format == VIDEO_FORMAT_AYUV) {
      // P010 and AYUV (8-bit 4:4:4) are supported on all platforms
      Context->EncoderParams.ProcessingEnable = true;
    } else if (VOI->format == VIDEO_FORMAT_P416) {
      // 12/16-bit 4:4:4 (Y416) requires TGL_LP (Gen12)+
      mfxU16 platformCode = QueryPlatformCodeName();
      bool highBitDepth444Supported = platformCode == 0 ||
                                      platformCode >= MFX_PLATFORM_TIGERLAKE;
      if (highBitDepth444Supported) {
        Context->EncoderParams.ProcessingEnable = true;
      } else {
        warn("VPP with P416 is only supported on Tiger Lake+");
      }
    } else {
      warn("VPP is only available with NV12, P010, AYUV, or P416 color format");
    }
  }

  const char *RateControlData = obs_data_get_string(Settings, "rate_control");
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
      std::string_view(RateControlData) == "ICQ")
    info("\tICQ Quality: %d", Context->EncoderParams.ICQQuality);

  if (Context->EncoderParams.RateControl == MFX_RATECONTROL_CQP) {
    if (obs_data_get_bool(Settings, "cqp_separate_ipb")) {
      info("\tQPI: %d, QPP: %d, QPB: %d",
           Context->EncoderParams.QPI,
           Context->EncoderParams.QPP,
           Context->EncoderParams.QPB);
    } else {
      // Print the applied (scaled) QP value
      info("\tCQP: %d", Context->EncoderParams.QPI);
    }
  }

  info("\tFPS numerator: %d", VOI->fps_num);
  info("\tFPS denominator: %d", VOI->fps_den);
  info("\tOutput width: %d", Context->EncoderParams.Width);
  info("\tOutput height: %d", Context->EncoderParams.Height);
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

  // The encoder can be created before a video output is attached (reroute /
  // non-video output scenarios) — match GetVideoInfo()'s null-safe pattern.
  video_t *Video = obs_encoder_video(Context->EncoderData);
  if (!Video) {
    delete Context;
    return nullptr;
  }
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
    // No global init mutex here: loader pointer reads are already guarded by
    // GlobalLoaderMutex (see GetVPLSession/CreateSession in
    // obs-qsv-onevpl-encoder-internal.cpp) and MFXLoad/MFXCreateSession are
    // thread-safe in oneVPL.  A per-init serialization mutex used to make
    // dual-output setups (stream + record) start sequentially for no reason.
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

  // AdaptersInfo is a fixed-size table (MAX_ADAPTERS); a DXGI adapter index
  // beyond that would be an out-of-bounds read.
  if (OVI.adapter >= MAX_ADAPTERS) {
    info(">>> adapter index %u out of probe table, fall back to non-texture "
         "encoder",
         OVI.adapter);
    return obs_encoder_create_rerouted(EncoderData, FallbackID);
  }

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