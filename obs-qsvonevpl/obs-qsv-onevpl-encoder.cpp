// #define MFXDEPRECATED_OFF

#include "obs-qsv-onevpl-encoder.hpp"
#include <cstring>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

mfxVersion VPLVersion = {{0, 1}}; // for backward compatibility
void GetEncoderVersion(unsigned short *Major, unsigned short *Minor) {
  *Major = VPLVersion.Major;
  *Minor = VPLVersion.Minor;
}

bool OpenEncoder(std::unique_ptr<QSVEncoder> &EncoderPTR,
                 encoder_params *EncoderParams, enum codec_enum Codec,
                 bool IsTextureEncoder) {
  try {
    EncoderPTR = std::make_unique<QSVEncoder>();
    if (EncoderParams->GPUNum == 0) {
      obs_video_info OVI;
      obs_get_video_info(&OVI);
      EncoderParams->dxgiAdapterIndex = OVI.adapter; // save raw DXGI index before adjustment
      mfxU32 AdapterID = OVI.adapter;
      mfxU32 AdapterIDAdjustment = 0;
      // Select current adapter; handle adapter reordering
      if (Codec == QSV_CODEC_AV1 && !AdaptersInfo[AdapterID].SupportAV1) {
        for (mfxU32 i = 0; i < MAX_ADAPTERS; i++) {
          if (!AdaptersInfo[i].IsIntel) {
            AdapterIDAdjustment++;
            continue;
          }
          if (AdaptersInfo[i].SupportAV1) {
            AdapterID = i;
            break;
          }
        }
      } else if (Codec == QSV_CODEC_VP9 &&
                 !AdaptersInfo[AdapterID].SupportVP9) {
        for (mfxU32 i = 0; i < MAX_ADAPTERS; i++) {
          if (!AdaptersInfo[i].IsIntel) {
            AdapterIDAdjustment++;
            continue;
          }
          if (AdaptersInfo[i].SupportVP9) {
            AdapterID = i;
            break;
          }
        }
      } else if (!AdaptersInfo[AdapterID].IsIntel) {
        for (mfxU32 i = 0; i < MAX_ADAPTERS; i++) {
          if (AdaptersInfo[i].IsIntel) {
            AdapterID = i;
            break;
          }
          AdapterIDAdjustment++;
        }
      }

      AdapterID -= AdapterIDAdjustment;

      EncoderParams->GPUNum = AdapterID;
    }

    if (Codec == QSV_CODEC_VP9) {
      // VP9: try texture mode first, fall back to non-texture path
      // (VIDEO_MEMORY, or system memory if VIDEO_MEMORY fails)
      // for multi-GPU setups where SetHandle fails with -16
      bool VP9InitSuccess = false;
      try {
        IsTextureEncoder = true;
        mfxStatus VP9Sts = EncoderPTR->Init(EncoderParams, Codec, true);
        if (VP9Sts >= MFX_ERR_NONE)
          VP9InitSuccess = true;
      } catch (const std::exception &e) {
        warn("VP9 texture mode init failed: %s, falling back to "
             "non-texture path",
             e.what());
      }

      if (!VP9InitSuccess) {
        EncoderPTR = std::make_unique<QSVEncoder>();
        IsTextureEncoder = false;
        mfxStatus VP9NonTexSts = EncoderPTR->Init(EncoderParams, Codec, false);
        if (VP9NonTexSts < MFX_ERR_NONE) {
          error("VP9 encoder init failed (non-texture fallback, sts=%d)",
                VP9NonTexSts);
          return false;
        }
      }
    } else if (EncoderParams->GPUNum > 0) {
      IsTextureEncoder = false;
      if (EncoderPTR->Init(EncoderParams, Codec, false) < MFX_ERR_NONE) {
        error("QSV encoder init failed");
        return false;
      }
    } else {
      if (EncoderPTR->Init(EncoderParams, Codec, IsTextureEncoder) <
          MFX_ERR_NONE) {
        error("QSV encoder init failed");
        return false;
      }
    }

    VPLVersion = EncoderPTR->GetCachedVPLVersion();

    return true;

  } catch (const std::exception &e) {
    error("QSV ERROR: %s", e.what());
    throw;
  }
}

void DestroyPluginContext(void *Data) {
  plugin_context *Context = static_cast<plugin_context *>(Data);

  if (Context) {
    // Unregister from the global encoder data map
    UnregisterEncoderData(Context->EncoderData);

    // Wait for in-progress encodes to finish before ending high-performance mode,
    {
      std::unique_lock<std::mutex> lock(Context->EncoderMutex);
      Context->EncodingCV.wait_for(lock, std::chrono::milliseconds(10),
        [&Context]() {
          return Context->EncodingCount.load(std::memory_order_acquire) == 0;
        });

      if (Context->EncoderPTR) {
        try {
          Context->EncoderPTR->ClearData();
          Context->EncoderPTR = nullptr;
        } catch (const std::exception &e) {
          error("QSV ERROR: %s", e.what());
        }
      }

      delete Context;
    }

    os_end_high_performance(Context->PerformanceToken);
  }
}

bool UpdateEncoderParams(void *Data, obs_data_t *Params) {
  plugin_context *Context = static_cast<plugin_context *>(Data);
  const std::string_view bitrate_control = obs_data_get_string(Params, "rate_control");
  const bool isCQP = bitrate_control == "CQP";
  const bool isICQ = bitrate_control == "ICQ";

  std::lock_guard<std::mutex> lock(Context->EncoderMutex);

  if (bitrate_control == "CBR") {
    Context->EncoderParams.TargetBitRate =
        static_cast<mfxU16>(obs_data_get_int(Params, "bitrate"));
  } else if (bitrate_control == "VBR") {
    Context->EncoderParams.TargetBitRate =
        static_cast<mfxU16>(obs_data_get_int(Params, "bitrate"));
    Context->EncoderParams.MaxBitRate =
        static_cast<mfxU16>(obs_data_get_int(Params, "max_bitrate"));
  } else if (isCQP) {
    bool separateIPB = obs_data_get_bool(Params, "cqp_separate_ipb");
    if (separateIPB) {
      double qpi, qpp, qpb;
      if (Context->Codec == QSV_CODEC_AV1 || Context->Codec == QSV_CODEC_VP9) {
        qpi = obs_data_get_double(Params, "qpi");
        qpp = obs_data_get_double(Params, "qpp");
        qpb = obs_data_get_double(Params, "qpb");
      } else {
        qpi = static_cast<double>(obs_data_get_int(Params, "qpi"));
        qpp = static_cast<double>(obs_data_get_int(Params, "qpp"));
        qpb = static_cast<double>(obs_data_get_int(Params, "qpb"));
      }
      // VP9/AV1 uses 0-255 QP range internally; UI 0.0-63.0, scale x4.
      if (Context->Codec == QSV_CODEC_AV1 || Context->Codec == QSV_CODEC_VP9) {
        qpi *= 4.0;
        qpp *= 4.0;
        qpb *= 4.0;
      }
      Context->EncoderParams.QPI = static_cast<mfxU16>(qpi);
      Context->EncoderParams.QPP = static_cast<mfxU16>(qpp);
      Context->EncoderParams.QPB = static_cast<mfxU16>(qpb);
    } else {
      double cqp;
      if (Context->Codec == QSV_CODEC_AV1 || Context->Codec == QSV_CODEC_VP9) {
        cqp = obs_data_get_double(Params, "cqp");
      } else {
        cqp = static_cast<double>(obs_data_get_int(Params, "cqp"));
      }
      // VP9/AV1 uses 0-255 QP range internally; UI 0.0-63.0, scale x4.
      if (Context->Codec == QSV_CODEC_AV1 || Context->Codec == QSV_CODEC_VP9) {
        cqp *= 4.0;
      }
      Context->EncoderParams.QPI = static_cast<mfxU16>(cqp);
      Context->EncoderParams.QPP = static_cast<mfxU16>(cqp);
      Context->EncoderParams.QPB = static_cast<mfxU16>(cqp);
    }
  } else if (isICQ) {
    int icq = static_cast<int>(obs_data_get_int(Params, "icq_quality"));
    // VP9 ICQQuality internally uses 0-255 range (MAX_ICQ_QUALITY_INDEX).
    // UI exposes 1-63, scale x4 to match the internal range.
    if (Context->Codec == QSV_CODEC_VP9) {
      icq *= 4;
    }
    Context->EncoderParams.ICQQuality = static_cast<mfxU16>(icq);
  }

  if (Context->EncoderPTR->UpdateParams(&Context->EncoderParams)) {
    mfxStatus Status = Context->EncoderPTR->ReconfigureEncoder();

    if (Status < MFX_ERR_NONE) {
      warn("Failed to reconfigure \nReset status: %d", Status);
      return false;
    }
  }

  return true;
}

void UpdateEncoderROI(void *Data,
                       const std::vector<encoder_params::roi_region> &Regions,
                       mfxU16 Mode, bool Enabled) {
  plugin_context *Context = static_cast<plugin_context *>(Data);
  if (!Context || !Context->EncoderPTR)
    return;

  std::lock_guard<std::mutex> lock(Context->EncoderMutex);
  Context->EncoderParams.ROIRegions = Regions;
  Context->EncoderParams.ROIMode = Mode;
  Context->EncoderParams.ROIEnabled = Enabled;

  if (Enabled) {
    Context->EncoderPTR->UpdateROIRegions(Regions, Mode);
  } else {
    // Disabled: clear cached regions so no ROI is applied
    Context->EncoderPTR->UpdateROIRegions({}, Mode);
  }

  blog(LOG_INFO,
       "[QSV VPL] UpdateEncoderROI: enabled=%d, regions=%zu, mode=%d",
       (int)Enabled, Regions.size(), (int)Mode);
}

bool GetExtraData(void *Data, uint8_t **ExtraData, size_t *Size) {
  plugin_context *Context = static_cast<plugin_context *>(Data);

  if (!Context->EncoderPTR)
    return false;

  *ExtraData = Context->ExtraData.first;
  *Size = Context->ExtraData.second;
  return true;
}

bool GetSEIData(void *Data, uint8_t **SEI, size_t *Size) {
  plugin_context *Context = static_cast<plugin_context *>(Data);

  if (!Context->EncoderPTR)
    return false;

  *SEI = Context->SEI.first;
  *Size = Context->SEI.second;
  return true;
}

void GetVideoInfo(void *Data, video_scale_info *Info) {
  plugin_context *Context = static_cast<plugin_context *>(Data);

  obs_data_t *settings = obs_encoder_get_settings(Context->EncoderData);
  const char *profile = obs_data_get_string(settings, "profile");
  auto svProf = std::string_view(profile);

  // Ask OBS what the current video output format is so we can avoid
  // unnecessary conversions when the encoder can eat it directly.
  video_t *video = obs_encoder_video(Context->EncoderData);
  const video_output_info *voi = video ? video_output_get_info(video) : nullptr;
  video_format current = voi ? voi->format : VIDEO_FORMAT_NV12;

  auto pick_format = [&](std::initializer_list<video_format> preferred,
                         video_format fallback) {
    for (video_format f : preferred) {
      if (current == f)
        return f;
    }
    return fallback;
  };

  switch (Context->Codec) {
  case QSV_CODEC_HEVC: {
    if (svProf == "main10") {
      Info->format = VIDEO_FORMAT_P010;
    } else if (svProf == "rext") {
      // HEVC RExt: 4:2:0 (NV12/P010), 4:2:2 (YUY2/P216),
      // 4:4:4 (AYUV/I444/P416).  Only request a format when the current OBS
      // format is in the same family; cross bit-depth or planar->packed
      // conversions often fail with "Bad scale conversion type".
      // AYUV has no OBS scaler mapping at all, so keep planar I444 and
      // convert to AYUV inside LoadFrameData.
      switch (current) {
      case VIDEO_FORMAT_AYUV:
        Info->format = VIDEO_FORMAT_AYUV;
        break;
      case VIDEO_FORMAT_I444:
        Info->format = VIDEO_FORMAT_I444;
        break;
      case VIDEO_FORMAT_YUY2:
      case VIDEO_FORMAT_I422:
        Info->format = VIDEO_FORMAT_YUY2;
        break;
      case VIDEO_FORMAT_P010:
      case VIDEO_FORMAT_I010:
        Info->format = VIDEO_FORMAT_P010;
        break;
      case VIDEO_FORMAT_P216:
        // Keep packed 4:2:2 10-bit only if OBS is already in that form.
        Info->format = VIDEO_FORMAT_P216;
        break;
      case VIDEO_FORMAT_P416:
      case VIDEO_FORMAT_I412:
        // Keep packed 4:4:4 12-bit only if OBS is already in that form.
        Info->format = VIDEO_FORMAT_P416;
        break;
      default:
        Info->format = VIDEO_FORMAT_NV12;
        break;
      }
    } else if (svProf == "scc") {
      // HEVC SCC: 4:2:0 8/10-bit and 4:4:4 8-bit.  Avoid cross-family
      // conversions that OBS scaler can't handle.
      switch (current) {
      case VIDEO_FORMAT_AYUV:
        Info->format = VIDEO_FORMAT_AYUV;
        break;
      case VIDEO_FORMAT_I444:
        Info->format = VIDEO_FORMAT_I444;
        break;
      case VIDEO_FORMAT_P010:
      case VIDEO_FORMAT_I010:
        Info->format = VIDEO_FORMAT_P010;
        break;
      default:
        Info->format = VIDEO_FORMAT_NV12;
        break;
      }
    } else {
      // main / default
      Info->format = VIDEO_FORMAT_NV12;
    }
    break;
  }
  case QSV_CODEC_AV1: {
    if (svProf == "high") {
      // AV1 High: 4:4:4 8-bit (AYUV) and 10-bit (Y410).  OBS has no Y410,
      // and AYUV has no OBS scaler mapping, so keep the 4:4:4 format OBS
      // already feeds us and convert internally; otherwise fall back to NV12.
      switch (current) {
      case VIDEO_FORMAT_AYUV:
        Info->format = VIDEO_FORMAT_AYUV;
        break;
      case VIDEO_FORMAT_I444:
        Info->format = VIDEO_FORMAT_I444;
        break;
      default:
        Info->format = VIDEO_FORMAT_NV12;
        break;
      }
    } else {
      Info->format = pick_format({VIDEO_FORMAT_P010, VIDEO_FORMAT_NV12},
                                 VIDEO_FORMAT_NV12);
    }
    break;
  }
  case QSV_CODEC_VP9: {
    // VP9 profiles: "0 (8-bit 4:2:0)", "1 (8-bit 4:4:4)",
    //               "2 (10-bit 4:2:0)", "3 (10-bit 4:4:4)"
    const char vp9p = profile[0];
    bool vp9_10bit = (vp9p == '2' || vp9p == '3');
    bool vp9_444 = (vp9p == '1' || vp9p == '3');
    if (vp9_444) {
      // AYUV has no OBS scaler mapping, so keep planar I444 and convert to
      // packed AYUV internally.  For anything else fall back to 4:2:0.
      switch (current) {
      case VIDEO_FORMAT_AYUV:
        Info->format = VIDEO_FORMAT_AYUV;
        break;
      case VIDEO_FORMAT_I444:
        Info->format = VIDEO_FORMAT_I444;
        break;
      default:
        Info->format = vp9_10bit ? VIDEO_FORMAT_P010 : VIDEO_FORMAT_NV12;
        break;
      }
    } else {
      Info->format = vp9_10bit ? VIDEO_FORMAT_P010 : VIDEO_FORMAT_NV12;
    }
    break;
  }
  case QSV_CODEC_AVC: {
    if (svProf == "high10") {
      Info->format = VIDEO_FORMAT_P010;
    } else {
      Info->format = VIDEO_FORMAT_NV12;
    }
    break;
  }
  }

  // VP9 encoder hardcodes colorRange=0 (limited range) in the bitstream and
  // doesn't accept mfxExtVideoSignalInfo. Force OBS to convert input to
  // limited range so the YUV values match the colorRange bit written by VPL.
  if (Context->Codec == QSV_CODEC_VP9)
    Info->range = VIDEO_RANGE_PARTIAL;

  obs_data_release(settings);
}

mfxU64 ConvertTSOBSMFX(int64_t TS, mfxU32 FpsNum) {
  return static_cast<mfxU64>(TS * 90000 / FpsNum);
}

int64_t ConvertTSMFXOBS(mfxI64 TS, mfxU32 FpsNum, mfxU32 FpsDen, int64_t Div) {
  // Integer rounding: symmetric for +/- TS
  int64_t numerator = TS * static_cast<int64_t>(FpsNum);
  int64_t rounding = (numerator >= 0) ? (Div / 2) : -(Div / 2);
  return (numerator + rounding) / Div * static_cast<int64_t>(FpsDen);
}

static size_t hevc_extract_rbsp(uint8_t *dst, std::span<const uint8_t> src) {
  size_t dst_pos = 0;
  size_t i = 0;
  while (i < src.size()) {
    if (i + 2 < src.size() && src[i] == 0 && src[i + 1] == 0 &&
        src[i + 2] == 3) {
      dst[dst_pos++] = 0;
      dst[dst_pos++] = 0;
      i += 3;
    } else {
      dst[dst_pos++] = src[i++];
    }
  }
  return dst_pos;
}

static size_t hevc_add_emulation_prevention(uint8_t *dst,
                                             std::span<const uint8_t> src) {
  size_t dst_pos = 0;
  int zero_count = 0;
  for (size_t i = 0; i < src.size(); i++) {
    if (zero_count >= 2 && src[i] <= 3) {
      dst[dst_pos++] = 3;
      zero_count = 0;
    }
    dst[dst_pos++] = src[i];
    if (src[i] == 0) {
      zero_count++;
    } else {
      zero_count = 0;
    }
  }
  return dst_pos;
}

uint32_t hevc_read_bits(const uint8_t *data, size_t max_size,
                                size_t &byte_pos, int &bit_pos, int n) {
  uint32_t val = 0;
  for (int i = 0; i < n && byte_pos < max_size; i++) {
    val = (val << 1) | ((data[byte_pos] >> bit_pos) & 1);
    bit_pos--;
    if (bit_pos < 0) {
      byte_pos++;
      bit_pos = 7;
    }
  }
  return val;
}

uint32_t hevc_read_uev(const uint8_t *data, size_t max_size,
                               size_t &byte_pos, int &bit_pos) {
  int leading_zeros = 0;
  while (byte_pos < max_size) {
    if (hevc_read_bits(data, max_size, byte_pos, bit_pos, 1) != 0)
      break;
    leading_zeros++;
  }
  if (leading_zeros == 0)
    return 0;
  return (1u << leading_zeros) - 1 +
         hevc_read_bits(data, max_size, byte_pos, bit_pos, leading_zeros);
}

static void hevc_skip_bits(size_t &byte_pos, int &bit_pos, int n) {
  bit_pos -= n;
  while (bit_pos < 0) {
    byte_pos++;
    bit_pos += 8;
  }
}

static size_t hevc_current_bit(const size_t &byte_pos, const int &bit_pos) {
  return byte_pos * 8 + (7 - bit_pos);
}

static void hevc_write_bits(uint8_t *data, size_t &byte_pos, int &bit_pos,
                             uint32_t val, int n) {
  for (int i = n - 1; i >= 0; i--) {
    if (bit_pos < 0) {
      byte_pos++;
      bit_pos = 7;
    }
    data[byte_pos] = (data[byte_pos] & ~(1 << bit_pos)) |
                     (((val >> i) & 1) << bit_pos);
    bit_pos--;
  }
}

static void hevc_flush_byte(uint8_t *data, size_t &byte_pos, int &bit_pos) {
  if (bit_pos < 7) {
    byte_pos++;
    bit_pos = 7;
  }
}

static void hevc_write_uev(uint8_t *data, size_t &byte_pos, int &bit_pos,
                            uint32_t val) {
  if (val == 0) {
    hevc_write_bits(data, byte_pos, bit_pos, 1, 1);
    return;
  }
  int leading_zeros = 0;
  uint32_t tmp = val + 1;
  while (tmp >>= 1)
    leading_zeros++;
  for (int i = 0; i < leading_zeros; i++)
    hevc_write_bits(data, byte_pos, bit_pos, 0, 1);
  hevc_write_bits(data, byte_pos, bit_pos, val + 1, leading_zeros + 1);
}

void ParseEncodedPacket(plugin_context *Context, encoder_packet *Packet,
                        mfxBitstream *Bitstream, bool *ReceivedPacketStatus) {
  if (Bitstream == nullptr || Bitstream->DataLength == 0) {
    *ReceivedPacketStatus = false;
    return;
  }

  if (!Context->ExtraData.first || Context->ExtraData.second == 0) {
    uint8_t *NewPacket = nullptr;
    size_t NewPacketSize = 0;
    if (Context->Codec == QSV_CODEC_AVC) {
      obs_extract_avc_headers(Bitstream->Data + Bitstream->DataOffset,
                              Bitstream->DataLength, &NewPacket,
                              &NewPacketSize, &Context->ExtraData.first,
                              &Context->ExtraData.second, &Context->SEI.first,
                              &Context->SEI.second);
    } else if (Context->Codec == QSV_CODEC_HEVC) {
      obs_extract_hevc_headers(Bitstream->Data + Bitstream->DataOffset,
                               Bitstream->DataLength, &NewPacket,
                               &NewPacketSize, &Context->ExtraData.first,
                               &Context->ExtraData.second, &Context->SEI.first,
                               &Context->SEI.second);
    } else if (Context->Codec == QSV_CODEC_AV1) {
      obs_extract_av1_headers(Bitstream->Data + Bitstream->DataOffset,
                              Bitstream->DataLength, &NewPacket,
                              &NewPacketSize, &Context->ExtraData.first,
                              &Context->ExtraData.second);
    } else if (Context->Codec == QSV_CODEC_VP9) {
      // VP9 has no parameter sets; bitstream is already raw frames.
      // Just copy through; no extradata needed (mkv/webm containers
      // don't require VP9 codec private data).
      NewPacketSize = Bitstream->DataLength;
      NewPacket = static_cast<uint8_t *>(
          bmemdup(Bitstream->Data + Bitstream->DataOffset, NewPacketSize));
    }

    Context->PacketData.resize(NewPacketSize);
    std::memcpy(Context->PacketData.data(), NewPacket, NewPacketSize);
    bfree(NewPacket);
  } else {
    Context->PacketData.resize(Bitstream->DataLength);
    std::memcpy(Context->PacketData.data(),
                Bitstream->Data + Bitstream->DataOffset, Bitstream->DataLength);
  }

  Packet->data = Context->PacketData.data();
  Packet->size = Context->PacketData.size();

  Packet->type = OBS_ENCODER_VIDEO;
  Packet->pts =
      ConvertTSMFXOBS(static_cast<mfxI64>(Bitstream->TimeStamp),
                      Context->CachedFpsNum, Context->CachedFpsDen,
                      Context->CachedTSDiv);
  Packet->dts =
      (Context->Codec == QSV_CODEC_AV1 ||
       Context->Codec == QSV_CODEC_VP9)
          ? Packet->pts
          : ConvertTSMFXOBS(Bitstream->DecodeTimeStamp,
                            Context->CachedFpsNum, Context->CachedFpsDen,
                            Context->CachedTSDiv);
  bool isKeyframe = ((Bitstream->FrameType & MFX_FRAMETYPE_I) ||
                     (Bitstream->FrameType & MFX_FRAMETYPE_IDR) ||
                     (Bitstream->FrameType & MFX_FRAMETYPE_S) ||
                     (Bitstream->FrameType & MFX_FRAMETYPE_xI) ||
                     (Bitstream->FrameType & MFX_FRAMETYPE_xIDR) ||
                     (Bitstream->FrameType & MFX_FRAMETYPE_xS));
  Packet->keyframe = isKeyframe;

  if (isKeyframe) {
    Packet->priority = static_cast<int>(OBS_NAL_PRIORITY_HIGHEST);
    Packet->drop_priority = static_cast<int>(OBS_NAL_PRIORITY_HIGH);
  } else if ((Bitstream->FrameType & MFX_FRAMETYPE_REF) ||
             (Bitstream->FrameType & MFX_FRAMETYPE_xREF)) {
    Packet->priority = static_cast<int>(OBS_NAL_PRIORITY_HIGH);
    Packet->drop_priority = static_cast<int>(OBS_NAL_PRIORITY_HIGH);
  } else if ((Bitstream->FrameType & MFX_FRAMETYPE_P) ||
             (Bitstream->FrameType & MFX_FRAMETYPE_xP)) {
    Packet->priority = static_cast<int>(OBS_NAL_PRIORITY_LOW);
    Packet->drop_priority = static_cast<int>(OBS_NAL_PRIORITY_HIGH);
  } else {
    Packet->priority = static_cast<int>(OBS_NAL_PRIORITY_DISPOSABLE);
    Packet->drop_priority = static_cast<int>(OBS_NAL_PRIORITY_HIGH);
  }

  // VP9 encoder hardcodes colorSpace=UNKNOWN (0) in the bitstream (see
  // InitVp9SeqLevelParam in mfx_vp9_encode_hw_utils.cpp). Patch the colorSpace
  // bits in keyframe headers so decoders apply the correct YUV->RGB matrix.
  // colorRange=0 is correct because GetVideoInfo forces limited-range input.
  if (Context->Codec == QSV_CODEC_VP9 && Packet->keyframe &&
      Context->PacketData.size() >= 5) {
    uint8_t *data = Context->PacketData.data();
    // Verify VP9 keyframe: frame_marker (bit 7-6) == 0b10,
    // frame_type (bit 2) == 0 (keyframe), sync code bytes 1-3.
    if ((data[0] & 0xC0) == 0x80 && (data[0] & 0x04) == 0 &&
        data[1] == 0x49 && data[2] == 0x83 && data[3] == 0x42) {
      // Map CICP MatrixCoefficients to VP9 colorSpace
      // 1=BT.709->2, 6=BT.601->1, 9=BT.2020->5, others->0 (UNKNOWN)
      uint8_t vp9ColorSpace = 0;
      switch (Context->EncoderParams.MatrixCoefficients) {
        case 1:  vp9ColorSpace = 2; break;  // BT.709
        case 6:  vp9ColorSpace = 1; break;  // BT.601
        case 9:  vp9ColorSpace = 5; break;  // BT.2020
        default: break;
      }
      if (vp9ColorSpace != 0) {
        const mfxU16 profile = Context->EncoderParams.CodecProfile;
        if (profile == MFX_PROFILE_VP9_0 || profile == MFX_PROFILE_VP9_1) {
          // Profile 0/1: colorSpace at byte 4 bits 7-5, colorRange at bit 4
          uint8_t curColorSpace = (data[4] >> 5) & 0x07;
          if (curColorSpace != 6 && curColorSpace != vp9ColorSpace)
            data[4] = (data[4] & 0x1F) | (vp9ColorSpace << 5);
        } else if (profile == MFX_PROFILE_VP9_2) {
          // Profile 2: bitDepth at byte 4 bit 7, colorSpace at bits 6-4
          uint8_t curColorSpace = (data[4] >> 4) & 0x07;
          if (curColorSpace != 6 && curColorSpace != vp9ColorSpace)
            data[4] = (data[4] & 0x8F) | (vp9ColorSpace << 4);
        }
        // Profile 3 writes a 3-bit profile field causing byte misalignment;
        // skip patching (rarely used).
      }
    }
  }

  *ReceivedPacketStatus = true;

  Bitstream->DataLength = 0;
  Bitstream->DataOffset = 0;
}

bool EncodeTexture(void *Data, encoder_texture *Texture, int64_t PTS,
                   uint64_t LockKey, uint64_t *NextKey, encoder_packet *Packet,
                   bool *ReceivedPacketStatus) {
  plugin_context *Context = static_cast<plugin_context *>(Data);

#if defined(_WIN32) || defined(_WIN64)
  if (!Texture || Texture->handle == static_cast<uint32_t>(-1)) {
#else
  if (!Texture || !Texture->tex[0] || !Texture->tex[1]) {
#endif
    warn("Encode failed: bad texture handle");
    *NextKey = LockKey;
    return false;
  }

  if (!Packet || !ReceivedPacketStatus)
    return false;

  // Quick snapshot under mutex, then release so encode does not block
  // concurrent parameter/ROI updates or plugin destruction.
  mfxU32 fpsNum;
  {
    std::lock_guard<std::mutex> lock(Context->EncoderMutex);
    if (!Context->EncoderPTR)
      return false;
    fpsNum = Context->CachedFpsNum;
    Context->EncodingCount.fetch_add(1, std::memory_order_acquire);
  }

  mfxBitstream *Bitstream = nullptr;
  bool success = true;

  try {
    Context->EncoderPTR->EncodeTexture(
        ConvertTSOBSMFX(PTS, fpsNum), static_cast<void *>(Texture), LockKey,
        NextKey, &Bitstream);
  } catch (const std::exception &e) {
    error("%s", e.what());
    error("encode failed");
    success = false;
  }

  if (success) {
    std::lock_guard<std::mutex> lock(Context->EncoderMutex);
    ParseEncodedPacket(Context, Packet, Bitstream,
                       ReceivedPacketStatus);
  }

  {
    std::lock_guard<std::mutex> lock(Context->EncoderMutex);
    if (Context->EncodingCount.fetch_sub(1, std::memory_order_release) == 1)
      Context->EncodingCV.notify_one();
  }
  return success;
}

bool EncodeFrame(void *Data, encoder_frame *Frame, encoder_packet *Packet,
                 bool *ReceivedPacketStatus) {

  plugin_context *Context = static_cast<plugin_context *>(Data);

  if (!Frame || !Packet || !ReceivedPacketStatus) {
    return false;
  }

  // Quick snapshot under mutex, then release for the actual encode.
  mfxU32 fpsNum;
  {
    std::lock_guard<std::mutex> lock(Context->EncoderMutex);
    if (!Context->EncoderPTR)
      return false;
    fpsNum = Context->CachedFpsNum;
    Context->EncodingCount.fetch_add(1, std::memory_order_acquire);
  }

  mfxBitstream *Bitstream = nullptr;
  bool success = true;

  try {
    if (Frame->data[0]) {
      Context->EncoderPTR->EncodeFrame(
          ConvertTSOBSMFX(Frame->pts, fpsNum), Frame->data,
          Frame->linesize, &Bitstream);
    } else {
      Context->EncoderPTR->EncodeFrame(
          ConvertTSOBSMFX(Frame->pts, fpsNum), nullptr, 0,
          &Bitstream);
    }
  } catch (const std::exception &e) {
    error("%s", e.what());
    error("encode failed");
    success = false;
  }

  if (success) {
    std::lock_guard<std::mutex> lock(Context->EncoderMutex);
    ParseEncodedPacket(Context, Packet, Bitstream,
                       ReceivedPacketStatus);
  }

  {
    std::lock_guard<std::mutex> lock(Context->EncoderMutex);
    if (Context->EncodingCount.fetch_sub(1, std::memory_order_release) == 1)
      Context->EncodingCV.notify_one();
  }
  return success;
}
