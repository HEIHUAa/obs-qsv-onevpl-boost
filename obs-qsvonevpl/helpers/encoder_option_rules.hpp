// encoder_option_rules.hpp -- declarative option dependency table.
// Consumers: obs-qsv-onevpl-plugin-init.cpp (drives obs_property visible/enabled
// state) and encoder_params_parser.hpp (SanitizeConflicts neutralizes conflicting
// values before the mfx params are built).  Every rule cites its source in the
// oneVPL GPU RT runtime tree in docs/option-dependency-matrix.md -- update the
// doc first, then this table.
// Policy: codec-driven / rate-control-driven gating and platform capability
// gates (probed features) are Hide; other-option and driver-level conflicts are
// Gray (control stays dimmed, value kept, optional `reason` appended to the "?"
// tooltip).  `neutral` rows are also sanitized at parse time -- combos that
// would hard-fail Init or that the old UI snapped back.
#pragma once

#include "common_utils.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace qsv_rules {

enum class Op : uint8_t {
  StrEq,    // string value of opt equals a
  StrNe,    // string value of opt differs from a
  StrIn,    // string value of opt is one of a/b/c
  StrNotIn, // string value of opt is none of a/b/c
  IntEq,    // int value of opt == num
  IntGt,    // int value of opt >  num
  IntLt,    // int value of opt <  num
  FieldGt,  // int value of opt >  int value of rhs (cross-option compare)
  BoolIs,   // opt is an obs bool item; holds when get_bool == (num != 0)
  Feature,  // opt names a platform feature, resolved via the callback
  FeatureNot, // holds when the feature is NOT available
  Never,    // always false -- for "this option never applies to this codec"
};

struct Cond {
  Op op;
  const char *opt = nullptr;
  const char *a = nullptr;
  const char *b = nullptr;
  const char *c = nullptr;
  int num = 0;
  const char *rhs = nullptr; // FieldGt right-hand side option
};

constexpr Cond Is(const char *opt, const char *v) {
  return Cond{.op = Op::StrEq, .opt = opt, .a = v};
}
constexpr Cond IsNot(const char *opt, const char *v) {
  return Cond{.op = Op::StrNe, .opt = opt, .a = v};
}
constexpr Cond OneOf(const char *opt, const char *a, const char *b = nullptr,
                     const char *c = nullptr) {
  return Cond{.op = Op::StrIn, .opt = opt, .a = a, .b = b, .c = c};
}
constexpr Cond NoneOf(const char *opt, const char *a, const char *b = nullptr,
                      const char *c = nullptr) {
  return Cond{.op = Op::StrNotIn, .opt = opt, .a = a, .b = b, .c = c};
}
constexpr Cond IntIs(const char *opt, int n) {
  return Cond{.op = Op::IntEq, .opt = opt, .num = n};
}
constexpr Cond IntOver(const char *opt, int n) {
  return Cond{.op = Op::IntGt, .opt = opt, .num = n};
}
constexpr Cond IntUnder(const char *opt, int n) {
  return Cond{.op = Op::IntLt, .opt = opt, .num = n};
}
constexpr Cond IntFieldOver(const char *opt, const char *rhs) {
  return Cond{.op = Op::FieldGt, .opt = opt, .rhs = rhs};
}
constexpr Cond BoolIs(const char *opt, bool v) {
  return Cond{.op = Op::BoolIs, .opt = opt, .num = v ? 1 : 0};
}
constexpr Cond Feat(const char *feature) {
  return Cond{.op = Op::Feature, .opt = feature};
}
constexpr Cond FeatNot(const char *feature) {
  return Cond{.op = Op::FeatureNot, .opt = feature};
}
constexpr Cond Never() { return Cond{.op = Op::Never}; }

enum class Action : uint8_t { Hide, Gray };

// codec_enum is { AVC, AV1, HEVC, VP9 } (common_utils.hpp); pin it here so a
// re-order breaks the build instead of silently corrupting the masks.
static_assert(QSV_CODEC_AVC == 0 && QSV_CODEC_AV1 == 1 &&
                  QSV_CODEC_HEVC == 2 && QSV_CODEC_VP9 == 3,
              "codec_enum layout changed, revisit kCodecs* below");

constexpr uint8_t CodecBit(enum codec_enum Codec) {
  return static_cast<uint8_t>(1u << static_cast<unsigned>(Codec));
}
inline constexpr uint8_t kCodecsAll = 0;
inline constexpr uint8_t kAVC = CodecBit(QSV_CODEC_AVC);
inline constexpr uint8_t kAV1 = CodecBit(QSV_CODEC_AV1);
inline constexpr uint8_t kHEVC = CodecBit(QSV_CODEC_HEVC);
inline constexpr uint8_t kVP9 = CodecBit(QSV_CODEC_VP9);
inline constexpr uint8_t kNoVP9 = static_cast<uint8_t>(kAVC | kAV1 | kHEVC);
inline constexpr uint8_t kNoAVC = static_cast<uint8_t>(kAV1 | kHEVC | kVP9);

struct Rule {
  const char *option;              // target obs property name
  uint8_t codecs;                  // bitmask, 0 = every codec
  Action action;
  const char *neutral;             // parse-time sanitize value, nullptr = none
  const char *reason;              // locale key appended to the "?" tooltip
                                   // while the control is grayed, nullptr = none
  bool conflict;                   // false: usable when ALL conds hold (default)
                                   // true:  conds describe the CONFLICT state --
                                   //        grayed/hidden when ALL of them hold
                                   //        (lets a row express "A and B together
                                   //        break it" without an OR of usable conds)
  std::array<Cond, 6> conds;       // see `conflict` for semantics
  uint8_t nconds;
};

template <typename... Cs>
  requires(sizeof...(Cs) <= 6 && (std::is_same_v<Cs, Cond> && ...))
consteval Rule MakeRule(const char *option, uint8_t codecs, Action action,
                        const char *neutral, Cs... conds) {
  return Rule{.option = option,
              .codecs = codecs,
              .action = action,
              .neutral = neutral,
              .reason = nullptr,
              .conflict = false,
              .conds = {conds...},
              .nconds = static_cast<uint8_t>(sizeof...(conds))};
}

// attach a localized tooltip reason key to a rule
consteval Rule WithReason(Rule R, const char *reason) {
  R.reason = reason;
  return R;
}

// flip to conflict semantics: conds describe when the option is BROKEN
// (unusable when all hold).  Needed when the usable state is an OR -- e.g.
// b_frames is fine when low-power is off OR the GPU has VDEnc B-frames.
consteval Rule WithConflict(Rule R) {
  R.conflict = true;
  return R;
}

// the table, grouped roughly like the settings UI
inline constexpr Rule kRules[] = {
    // rate control magnitudes (RC-driven -> Hide)
    MakeRule("bitrate", kCodecsAll, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "ICQ")),
    // MaxKbps is consumed by VBR/VCM/QVBR (defaults + level calc,
    // h264_enc_common_hw.cpp:6032-6044,430-441); AVC AVBR drops it on
    // write-back and CBR only defaults it to target when unset.
    MakeRule("max_bitrate", kCodecsAll, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "VCM", "QVBR")),
    MakeRule("accuracy", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "AVBR")),
    MakeRule("convergence", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "AVBR")),
    MakeRule("qvbr_quality", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "QVBR")),
    MakeRule("icq_quality", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "ICQ")),
    MakeRule("buffer_size", kCodecsAll, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "ICQ")),
    MakeRule("min_qp", kNoVP9, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "ICQ")),
    MakeRule("max_qp", kNoVP9, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "ICQ")),

    MakeRule("cqp_separate_ipb", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "CQP")),
    // qpi/qpp/qpb are sub-options of the separate-IPB toggle -> Hide (cascade).
    // cqp_separate_ipb is an obs bool checkbox, hence BoolIs (string compares
    // can never match a bool item).
    MakeRule("qpi", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "CQP"), BoolIs("cqp_separate_ipb", true)),
    MakeRule("qpp", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "CQP"), BoolIs("cqp_separate_ipb", true)),
    MakeRule("qpb", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "CQP"), BoolIs("cqp_separate_ipb", true)),
    MakeRule("cqp", kCodecsAll, Action::Hide, nullptr,
             Is("rate_control", "CQP"), BoolIs("cqp_separate_ipb", false)),

    // HRD conformance only exists on HRD-capable BRC modes; AVC additionally
    // force-offs NalHrd under AVBR (h264_enc_common_hw.cpp:3339-3351, only
    // CBR/VBR/QVBR/LA_HRD/VCM allowed).  LowDelayHrd also needs an HRD switch
    // ON (runtime forces both off).  HEVC only blocks CQP/ICQ
    // (hevcehw_base_legacy.cpp:3746-3763); AVBR doesn't exist there, so the
    // shared row can include it safely.
    MakeRule("hrd_conformance", kCodecsAll, Action::Hide, "OFF",
             NoneOf("rate_control", "CQP", "ICQ", "AVBR")),
    MakeRule("low_delay_hrd", kCodecsAll, Action::Hide, "OFF",
             NoneOf("rate_control", "CQP", "ICQ"),
             OneOf("hrd_conformance", "ON", "AUTO")),
    // LowDelayBRC only on VBR-ish modes.  On AVC/HEVC the runtime forces
    // GopRefDist=1 and drops the sliding window (conflicts with B-frames and
    // lookahead); AV1's TCBRC has no such coupling (av1ehw_base_general.cpp:4372-4376).
    MakeRule("low_delay_brc", kCodecsAll, Action::Hide, "OFF",
             OneOf("rate_control", "VBR", "VCM", "QVBR")),
    WithReason(MakeRule("low_delay_brc", static_cast<uint8_t>(kAVC | kHEVC),
                        Action::Gray, "OFF", IntIs("b_frames", 0),
                        Is("lookahead", "OFF")),
               "RuleReason_LowDelayBRC"),

    // MBBRC: hidden on CQP/VCM (runtime forces it off); on AVC the ICQ/QVBR
    // paths force it ON regardless of the user value. h264_enc_common_hw.cpp:3946-3953
    MakeRule("mbbrc", kCodecsAll, Action::Hide, "OFF",
             NoneOf("rate_control", "CQP", "VCM")),
    WithReason(MakeRule("mbbrc", kAVC, Action::Gray, nullptr,
                        NoneOf("rate_control", "ICQ", "QVBR")),
               "RuleReason_MBBRC"),

    // MaxFrameSize survival matrices differ per codec (driver-side
    // UserMaxFrameSizeSupport/SW-BRC gating is left to the driver):
    // AVC clears it unconditionally under CBR/CQP (h264_enc_common_hw.cpp:752-760),
    // HEVC keeps it only for VBR/QVBR (hevcehw_base_max_frame_size.cpp:57-85),
    // AV1 keeps it for every RC except VBR -- TCBRC owns frame-size control
    // there (av1ehw_base_max_frame_size.cpp:53-54) -- and the VP9 runtime
    // ignores it entirely.  The GAME_STREAMING wipe is AVC-only (:796-804).
    MakeRule("max_frame_size_mode", kAVC, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "CBR")),
    MakeRule("max_frame_size_mode", kHEVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "QVBR")),
    MakeRule("max_frame_size_mode", kAV1, Action::Hide, nullptr,
             IsNot("rate_control", "VBR")),
    MakeRule("max_frame_size_mode", kVP9, Action::Hide, nullptr, Never()),
    WithReason(MakeRule("max_frame_size_mode", kAVC, Action::Gray, nullptr,
                        IsNot("scenario_info", "GAME_STREAMING")),
               "RuleReason_GSMaxFrame"),
    MakeRule("max_frame_size_all", kAVC, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "CBR"),
             Is("max_frame_size_mode", "all")),
    MakeRule("max_frame_size_all", kHEVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "QVBR"),
             Is("max_frame_size_mode", "all")),
    MakeRule("max_frame_size_all", kAV1, Action::Hide, nullptr,
             IsNot("rate_control", "VBR"),
             Is("max_frame_size_mode", "all")),
    MakeRule("max_frame_size_all", kVP9, Action::Hide, nullptr, Never()),
    WithReason(MakeRule("max_frame_size_all", kAVC, Action::Gray, nullptr,
                        IsNot("scenario_info", "GAME_STREAMING")),
               "RuleReason_GSMaxFrame"),
    MakeRule("max_frame_size_i", kAVC, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "CBR"),
             Is("max_frame_size_mode", "per_type")),
    MakeRule("max_frame_size_i", kHEVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "QVBR"),
             Is("max_frame_size_mode", "per_type")),
    MakeRule("max_frame_size_i", kAV1, Action::Hide, nullptr,
             IsNot("rate_control", "VBR"),
             Is("max_frame_size_mode", "per_type")),
    MakeRule("max_frame_size_i", kVP9, Action::Hide, nullptr, Never()),
    WithReason(MakeRule("max_frame_size_i", kAVC, Action::Gray, nullptr,
                        IsNot("scenario_info", "GAME_STREAMING")),
               "RuleReason_GSMaxFrame"),
    MakeRule("max_frame_size_p", kAVC, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "CBR"),
             Is("max_frame_size_mode", "per_type")),
    MakeRule("max_frame_size_p", kHEVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "QVBR"),
             Is("max_frame_size_mode", "per_type")),
    MakeRule("max_frame_size_p", kAV1, Action::Hide, nullptr,
             IsNot("rate_control", "VBR"),
             Is("max_frame_size_mode", "per_type")),
    MakeRule("max_frame_size_p", kVP9, Action::Hide, nullptr, Never()),
    WithReason(MakeRule("max_frame_size_p", kAVC, Action::Gray, nullptr,
                        IsNot("scenario_info", "GAME_STREAMING")),
               "RuleReason_GSMaxFrame"),

    // SkipFrame needs no RC rule: the runtime only drops it when the DDI
    // lacks support AND RC != CQP (h264_enc_common_hw.cpp:4588 -- CQP is the
    // EXEMPTED case), and HEVC validates the value domain only
    // (hevcehw_base_legacy.cpp CheckSkipFrame).

    // B-frames are meaningless under VCM (IPPP only) -> Hide.  AdaptiveI/B
    // only work through EncTools and are suppressed by GOP_STRICT -> Gray.
    // h264_enc_common_hw.cpp:4777-4825, hevcehw_base_enctools_com.h:59
    MakeRule("b_frames", kNoVP9, Action::Hide, nullptr,
             IsNot("rate_control", "VCM")),
    MakeRule("adaptive_b", kNoVP9, Action::Hide, nullptr,
             IsNot("rate_control", "VCM")),
    WithReason(MakeRule("adaptive_b", kNoVP9, Action::Gray, nullptr,
                        Is("enctools", "ON"), IsNot("gop_opt_flag", "STRICT")),
               "RuleReason_AdaptiveIPB"),
    WithReason(MakeRule("adaptive_i", kNoVP9, Action::Gray, nullptr,
                        Is("enctools", "ON"), IsNot("gop_opt_flag", "STRICT")),
               "RuleReason_AdaptiveIPB"),

    // B-Pyramid is force-disabled by the runtime when GopRefDist < 3, i.e.
    // the CONFLICT state is 0 < b_frames < 2.  h264_enc_common_hw.cpp:3227
    WithConflict(WithReason(MakeRule("p_pyramid", kAVC, Action::Gray, nullptr,
                                     IntOver("b_frames", 0),
                                     IntUnder("b_frames", 2)),
                            "RuleReason_BPyramid")),

    // lookahead: AVC on CBR/VBR/ICQ (VBR/ICQ promoted to LA variants, CBR uses
    // EncTools LAGS).  HEVC/AV1 hardware EncTools lookahead is gated in the
    // EncTools layer -- GS scenario (LPLA) or, without it, GopRefDist in
    // {2,4,8,16} + ExtBRC + scenario UNKNOWN (SW LA,
    // hevcehw_base_enctools.cpp:272-285).  There is deliberately NO low-power
    // gray row: the runtime silently forces LowPower=ON for HEVC
    // (hevcehw_base_caps.cpp:51) and AV1 (av1ehw_base_general.cpp:493), so
    // "low_power must be ON" can never fail.
    MakeRule("lookahead", kAVC, Action::Hide, "OFF",
             OneOf("rate_control", "CBR", "VBR", "ICQ")),
    MakeRule("lookahead", static_cast<uint8_t>(kHEVC | kAV1), Action::Hide,
             "OFF", OneOf("rate_control", "CBR", "VBR"), Feat("enc_tools")),
    // LookAheadDS is AVC-only (mfxExtCodingOptionDDI); HEVC/AV1 pick the LA
    // scale internally, VP9 has none.  Sub-option of the lookahead switch -> Hide.
    MakeRule("lookahead_ds", kAVC, Action::Hide, nullptr,
             Is("lookahead", "HQ")),
    MakeRule("lookahead_ds", kNoAVC, Action::Hide, nullptr, Never()),
    MakeRule("la_depth", kCodecsAll, Action::Hide, nullptr,
             Is("lookahead", "HQ")),

    // exists for every codec but must never show on VP9 (codec-driven -> Hide)
    MakeRule("lookahead", kVP9, Action::Hide, "OFF", Never()),
    MakeRule("min_qp", kVP9, Action::Hide, nullptr, Never()),
    MakeRule("max_qp", kVP9, Action::Hide, nullptr, Never()),

    // enctools: master switch gated by RC mode + platform (TGL+); the config
    // sub-options additionally need Windows (mfxExtEncToolsConfig is only
    // attached there).  Children of the switch -> Hide; PEER conds stay Gray.
    MakeRule("enctools", kNoVP9, Action::Hide, nullptr,
             NoneOf("rate_control", "CQP", "ICQ", "VCM"),
             Feat("enc_tools_config")),
    MakeRule("enc_tools_scene_change", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_adaptive_ref_p", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_adaptive_ref_b", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_adaptive_ltr", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_adaptive_pyramid_quant_p", kNoVP9, Action::Hide,
             nullptr, Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_adaptive_pyramid_quant_b", kNoVP9, Action::Hide,
             nullptr, Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_adaptive_mbqp", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_brc_buffer_hints", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_brc", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    MakeRule("enc_tools_saliency_map_hint", kNoVP9, Action::Hide, nullptr,
             Is("enctools", "ON"), Feat("enc_tools_config")),
    // runtime supported-config whitelist: BRC only fires for CBR/VBR,
    // BRCBufferHints/AdaptiveMBQP need LookAheadDepth > GopRefDist (MBQP also
    // MBBRC; both ride the BRC flag, so CBR/VBR too); HEVC additionally
    // blocks adaptive-ref at TU7 or GOP_STRICT.  The b_frames>0 cond on
    // adaptive_ref_b is a semantic proxy -- no B frames, nothing to adapt.
    // enctools/src/mfx_enctools_common.cpp:253-277, hevcehw_base_enctools.cpp:186-196
    WithReason(MakeRule("enc_tools_brc", kNoVP9, Action::Gray, nullptr,
                        OneOf("rate_control", "CBR", "VBR")),
               "RuleReason_ETBRC"),
    WithReason(MakeRule("enc_tools_brc_buffer_hints", kNoVP9, Action::Gray,
                        nullptr, OneOf("rate_control", "CBR", "VBR"),
                        IntFieldOver("la_depth", "b_frames")),
               "RuleReason_ETBufferHints"),
    WithReason(MakeRule("enc_tools_adaptive_mbqp", kNoVP9, Action::Gray,
                        nullptr, OneOf("rate_control", "CBR", "VBR"),
                        IntFieldOver("la_depth", "b_frames"),
                        IsNot("mbbrc", "OFF")),
               "RuleReason_ETMBQP"),
    WithReason(MakeRule("enc_tools_adaptive_ref_b", kNoVP9, Action::Gray,
                        nullptr, IntOver("b_frames", 0)),
               "RuleReason_ETRefB"),
    // AV1: adaptive reference is disabled in EVERY scenario -- the SW path
    // force-offs it ("not supported for now",
    // av1ehw_base_enctools.cpp:344-347) and CorrectVideoParams gates it with a
    // hardwired `bAdaptiveRef = false` (av1ehw_base_enctools.cpp:391).
    WithReason(MakeRule("enc_tools_adaptive_ref_p", kAV1, Action::Gray,
                        nullptr, Never()),
               "RuleReason_AV1Ref"),
    WithReason(MakeRule("enc_tools_adaptive_ref_b", kAV1, Action::Gray,
                        nullptr, Never()),
               "RuleReason_AV1Ref"),
    // HEVC IsAdaptiveRefAllowed: TargetUsage != 7 AND not GOP_STRICT
    // (hevcehw_base_enctools.cpp:186-196).
    WithReason(MakeRule("enc_tools_adaptive_ref_p", kHEVC, Action::Gray,
                        nullptr, IsNot("target_usage", "TU7 (Veryfast)"),
                        IsNot("target_usage", "Fastest (TU6-TU7)"),
                        IsNot("gop_opt_flag", "STRICT")),
               "RuleReason_ETTU7"),
    WithReason(MakeRule("enc_tools_adaptive_ref_b", kHEVC, Action::Gray,
                        nullptr, IsNot("target_usage", "TU7 (Veryfast)"),
                        IsNot("target_usage", "Fastest (TU6-TU7)"),
                        IsNot("gop_opt_flag", "STRICT")),
               "RuleReason_ETTU7"),

    // intra refresh x B-frames is a hard mutex (runtime zeroes IntRefType); AVC
    // also needs NumRefFrame <= 1; VDEnc only does HORIZONTAL refresh.
    // h264_enc_common_hw.cpp:4283-4306, hevcehw_base_legacy.cpp:3874
    WithReason(MakeRule("intra_ref_encoding", kAVC, Action::Gray, "OFF",
                        IntIs("b_frames", 0), IntUnder("num_ref_frame", 2)),
               "RuleReason_IntRef"),
    WithReason(MakeRule("intra_ref_encoding", kHEVC, Action::Gray, "OFF",
                        IntIs("b_frames", 0)),
               "RuleReason_IntRef"),
    // type/cycle_size/qp_delta are sub-options of the intra-ref switch -> Hide
    MakeRule("intra_ref_type", kNoVP9, Action::Hide, nullptr,
             Is("intra_ref_encoding", "ON")),
    // VDEnc AVC only does HORIZONTAL refresh (h264_enc_common_hw.cpp:4283
    // clips SLICE down to it); HEVC passes VERTICAL through with a full code
    // path (hevcehw_base_legacy.cpp:3853-3855,2151-2153), so the restriction
    // is AVC-only.  AV1 has no intra refresh at all (group not created).
    WithReason(MakeRule("intra_ref_type", kAVC, Action::Gray, nullptr,
                        IsNot("low_power", "ON")),
               "RuleReason_IntRefType"),
    MakeRule("intra_ref_cycle_size", kNoVP9, Action::Hide, nullptr,
             Is("intra_ref_encoding", "ON")),
    MakeRule("intra_ref_qp_delta", kNoVP9, Action::Hide, nullptr,
             Is("intra_ref_encoding", "ON")),

    // vpp: filter sub-options are children of the vpp switch -> Hide; platform gates too
    MakeRule("detail", kCodecsAll, Action::Hide, nullptr, Is("vpp", "ON")),
    MakeRule("detail_factor", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Is("detail", "ON")),
    MakeRule("denoise_mode", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON")),
    MakeRule("denoise_strength", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), OneOf("denoise_mode", "MANUAL | PRE ENCODE",
                                    "MANUAL | POST ENCODE")),
    MakeRule("scaling_mode", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON")),
    MakeRule("vpp_out_width", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), IsNot("scaling_mode", "OFF")),
    MakeRule("vpp_out_height", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), IsNot("scaling_mode", "OFF")),
    MakeRule("image_stab_mode", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Feat("vpp_image_stab")),
    MakeRule("perc_enc_prefilter", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Feat("vpp_percenc")),
    MakeRule("vpp_procamp", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON")),
    MakeRule("vpp_procamp_brightness", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Is("vpp_procamp", "ON")),
    MakeRule("vpp_procamp_contrast", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Is("vpp_procamp", "ON")),
    MakeRule("vpp_procamp_hue", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Is("vpp_procamp", "ON")),
    MakeRule("vpp_procamp_saturation", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Is("vpp_procamp", "ON")),
    MakeRule("vpp_rotation", kCodecsAll, Action::Hide, "OFF",
             Is("vpp", "ON"), Feat("vpp_rotation")),
    MakeRule("vpp_mirroring", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Feat("vpp_mirror")),
    MakeRule("vpp_frc", kCodecsAll, Action::Hide, nullptr, Is("vpp", "ON"),
             Feat("vpp_frc")),
    MakeRule("vpp_frc_out_fps", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Feat("vpp_frc"), IsNot("vpp_frc", "OFF")),
    // MCTF CM kernels only ship for Gen12-LP (feature resolver checks the
    // platform window incl. the ADL-N special case).
    MakeRule("vpp_mctf", kCodecsAll, Action::Hide, "OFF", Is("vpp", "ON"),
             Feat("vpp_mctf")),
    MakeRule("vpp_mctf_strength", kCodecsAll, Action::Hide, nullptr,
             Is("vpp", "ON"), Feat("vpp_mctf"), Is("vpp_mctf", "ON")),

    // AVC extras: mv_cost_scaling_factor is the parameter panel of the GMBA switch -> Hide
    MakeRule("mv_cost_scaling_factor", kAVC, Action::Hide, nullptr,
             Is("global_motion_bias_adjustment", "ON")),
    // Trellis has NO LowPower coupling in the runtime (the old "VDEnc ignores
    // it" citation traced to SetLowPowerDefault, h264_enc_common_hw.cpp:1439,
    // and the EnhancedEncInput gate is commented out at :4394-4398) -- keep
    // only the platform floor: Sandy Bridge lacks trellis entirely
    // (feature resolver).
    MakeRule("trellis", kAVC, Action::Hide, nullptr, FeatNot("trellis")),
    // AdaptiveCQM is only supported for GAME_STREAMING/REMOTE_GAMING on VDEnc;
    // the runtime force-disables it otherwise.
    // h264_enc_common_hw.cpp:5500-5503,6181-6187
    WithReason(MakeRule("adaptive_cqm", kAVC, Action::Gray, nullptr,
                        Is("low_power", "ON"),
                        OneOf("scenario_info", "GAME_STREAMING",
                              "REMOTE_GAMING")),
               "RuleReason_AdaptiveCQM"),
    // quant matrix rewrite only applies on VBR/ICQ today (RC-driven -> Hide;
    // the runtime itself does not gate it per RC).  Custom cascade -> Gray.
    MakeRule("quant_matrix", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ")),
    MakeRule("chroma_qp_offset", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ")),
    MakeRule("qm_granularity", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom")),
    MakeRule("qm_4x4", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntUnder("qm_granularity", 1)),
    MakeRule("qm_8x8", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntUnder("qm_granularity", 1)),
    MakeRule("qm_i4", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntIs("qm_granularity", 1)),
    MakeRule("qm_p4", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntIs("qm_granularity", 1)),
    MakeRule("qm_i8", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntIs("qm_granularity", 1)),
    MakeRule("qm_p8", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntIs("qm_granularity", 1)),
    MakeRule("qm_i4y", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntOver("qm_granularity", 1)),
    MakeRule("qm_p4y", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntOver("qm_granularity", 1)),
    MakeRule("qm_i8y", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntOver("qm_granularity", 1)),
    MakeRule("qm_p8y", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntOver("qm_granularity", 1)),
    MakeRule("qm_ci4", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntOver("qm_granularity", 1)),
    MakeRule("qm_cp4", kAVC, Action::Hide, nullptr,
             OneOf("rate_control", "VBR", "ICQ"),
             Is("quant_matrix", "custom"), IntOver("qm_granularity", 1)),

    // HEVC: WeightedPred=EXPLICIT x SAO are mutually exclusive (driver returns
    // UNSUPPORTED).  hevcehw_base_legacy_defaults.cpp:2724
    WithReason(MakeRule("hevc_sao", kHEVC, Action::Gray, "DISABLE",
                        IsNot("weighted_pred", "EXPLICIT")),
               "RuleReason_WPSAO"),

    // AVC VDEnc B-frames (probed at runtime): only break when LowPower is ON
    // *and* the GPU's VDEnc pipeline lacks B-frame support (probe, <DG2);
    // with low-power off the VME path is fine -- hence conflict semantics.
    WithConflict(WithReason(MakeRule("b_frames", kAVC, Action::Gray, nullptr,
                                     Is("low_power", "ON"),
                                     FeatNot("lp_b_frames")),
                            "RuleReason_LPBFrames")),
};

// evaluation helpers

inline bool CondHolds(const Cond &C, obs_data_t *Settings,
                      bool (*FeatureFn)(const char *)) {
  switch (C.op) {
  case Op::StrEq:
    return std::string_view(obs_data_get_string(Settings, C.opt)) == C.a;
  case Op::StrNe:
    return std::string_view(obs_data_get_string(Settings, C.opt)) != C.a;
  case Op::StrIn: {
    const std::string_view v = obs_data_get_string(Settings, C.opt);
    return (C.a && v == C.a) || (C.b && v == C.b) || (C.c && v == C.c);
  }
  case Op::StrNotIn: {
    const std::string_view v = obs_data_get_string(Settings, C.opt);
    return !((C.a && v == C.a) || (C.b && v == C.b) || (C.c && v == C.c));
  }
  case Op::IntEq:
    return obs_data_get_int(Settings, C.opt) == C.num;
  case Op::IntGt:
    return obs_data_get_int(Settings, C.opt) > C.num;
  case Op::IntLt:
    return obs_data_get_int(Settings, C.opt) < C.num;
  case Op::FieldGt:
    return obs_data_get_int(Settings, C.opt) > obs_data_get_int(Settings, C.rhs);
  case Op::BoolIs:
    // obs bool items store true/false and obs_data_get_string returns "" for
    // them, so string comparisons can never match a checkbox
    return obs_data_get_bool(Settings, C.opt) == (C.num != 0);
  case Op::Feature:
    // without a resolver (parse-side sanitization) assume features exist;
    // the driver corrects anything we got wrong at Init time.
    return FeatureFn ? FeatureFn(C.opt) : true;
  case Op::FeatureNot:
    return FeatureFn ? !FeatureFn(C.opt) : false;
  case Op::Never:
    return false;
  }
  return true;
}

inline bool RuleUsable(const Rule &R, obs_data_t *Settings,
                       bool (*FeatureFn)(const char *)) {
  bool all = true;
  for (uint8_t i = 0; i < R.nconds; ++i) {
    if (!CondHolds(R.conds[i], Settings, FeatureFn)) {
      all = false;
      break;
    }
  }
  // conflict rows describe the BROKEN state: unusable exactly when all conds hold
  return R.conflict ? !all : all;
}

// UI side: Hide rows drive visibility, Gray rows drive the enabled state
// (controls stay visible but dimmed, user value untouched); reason-bearing Gray
// rows append the localized reason to the "?" tooltip while disabled.  FeatureFn
// resolves Op::Feature conds.  Returns a signature of the resulting visual state
// (per-row verdicts plus active tooltip reasons) so the caller can skip OBS's
// expensive full properties rebuild when nothing actually changed.
inline std::string ApplyToProperties(obs_properties_t *Props,
                                     obs_data_t *Settings,
                                     enum codec_enum Codec,
                                     bool (*FeatureFn)(const char *)) {
  const uint8_t bit = CodecBit(Codec);
  std::string sig;
  sig.reserve(512);

  // collect active reasons per option first so options with several conflict
  // rows keep every applicable reason, not just the last one
  std::unordered_map<std::string_view, std::string> active;
  std::unordered_set<std::string_view> reason_options;
  for (const Rule &R : kRules) {
    if (R.action != Action::Gray || !R.reason)
      continue;
    if (R.codecs && !(R.codecs & bit))
      continue;
    auto *p = obs_properties_get(Props, R.option);
    if (!p)
      continue;
    reason_options.insert(R.option);
    if (RuleUsable(R, Settings, FeatureFn))
      continue;
    std::string &s = active[R.option];
    if (!s.empty())
      s += "\n";
    s += obs_module_text(R.reason);
  }

  for (const Rule &R : kRules) {
    if (R.codecs && !(R.codecs & bit))
      continue;
    auto *p = obs_properties_get(Props, R.option);
    if (!p)
      continue; // option not created for this codec / build
    const bool usable = RuleUsable(R, Settings, FeatureFn);
    sig += R.option;
    sig += R.action == Action::Hide ? 'v' : 'e';
    sig += usable ? '1' : '0';
    sig += ';';
    if (R.action == Action::Hide) {
      obs_property_set_visible(p, usable);
      if (!usable && R.neutral)
        obs_data_set_string(Settings, R.option, R.neutral);
    } else {
      obs_property_set_enabled(p, usable);
    }
  }

  // strip every reason line the table could have appended earlier, then
  // re-append the active ones
  for (const auto option : reason_options) {
    // the views all come from string literals, data() is null-terminated
    auto *p = obs_properties_get(Props, option.data());
    if (!p)
      continue;
    const char *cur = obs_property_long_description(p);
    std::string base = cur ? cur : "";
    for (const Rule &R2 : kRules) {
      if (R2.action != Action::Gray || !R2.reason ||
          std::string_view(R2.option) != option)
        continue;
      if (R2.codecs && !(R2.codecs & bit))
        continue;
      const std::string suffix =
          std::string("\n\n") + obs_module_text(R2.reason);
      for (auto pos = base.rfind(suffix); pos != std::string::npos &&
           pos + suffix.size() == base.size();
           base.resize(pos), pos = base.rfind(suffix)) {
      }
    }
    if (auto it = active.find(option); it != active.end()) {
      base += "\n\n" + it->second;
      sig += option;
      sig += "=r;";
    } else {
      sig += option;
      sig += "=b;";
    }
    const char *curAfter = obs_property_long_description(p);
    // only touch the widget when the text actually changed -- rewriting the
    // same tooltip churns the properties view (and piles up orphaned bubbles)
    if (!curAfter || base != curAfter)
      obs_property_set_long_description(p, base.c_str());
  }
  return sig;
}

// Parse side: snap unusable options with a `neutral` to that value before the
// mfx params are built -- stale profiles can't trip MFX_ERR_UNSUPPORTED at Init
// and combos the driver would silently ignore never reach it.  Covers both row
// kinds (Hide = old-UI snap-back, Gray = hard-failure conflicts); options
// without a neutral are left to the driver, which corrects them and logs it.
inline void SanitizeConflicts(obs_data_t *Settings, enum codec_enum Codec,
                              bool (*FeatureFn)(const char *)) {
  const uint8_t bit = CodecBit(Codec);
  for (const Rule &R : kRules) {
    if (!R.neutral)
      continue;
    if (R.codecs && !(R.codecs & bit))
      continue;
    if (RuleUsable(R, Settings, FeatureFn))
      continue;
    obs_data_set_string(Settings, R.option, R.neutral);
  }
}

} // namespace qsv_rules
