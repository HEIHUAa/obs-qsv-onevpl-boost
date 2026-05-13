// #define MFXDEPRECATED_OFF

#ifndef __QSV_VPL_ENCODER_H__
#include "obs-qsv-onevpl-encoder.hpp"
#endif
#include <cstring>

mfxVersion VPLVersion = {{0, 1}}; // for backward compatibility
std::atomic<bool> IsActive{false};

void GetEncoderVersion(unsigned short *Major, unsigned short *Minor) {
  *Major = VPLVersion.Major;
  *Minor = VPLVersion.Minor;
}

bool OpenEncoder(std::unique_ptr<QSVEncoder> &EncoderPTR,
                 encoder_params *EncoderParams, enum codec_enum Codec,
                 bool IsTextureEncoder) {
  try {
    EncoderPTR = std::make_unique<QSVEncoder>();
    // throw std::exception("test");
    if (EncoderParams->GPUNum == 0) {
      obs_video_info OVI;
      obs_get_video_info(&OVI);
      mfxU32 AdapterID = OVI.adapter;
      mfxU32 AdapterIDAdjustment = 0;
      // Select current adapter - will be iGPU if exiStatus due to adapter
      // reordering
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

    if (EncoderParams->GPUNum > 0) {
      IsTextureEncoder = false;
    }

    EncoderPTR->GetVPLVersion(VPLVersion);
    if (EncoderPTR->Init(EncoderParams, Codec, IsTextureEncoder) < MFX_ERR_NONE) {
      error("QSV encoder init failed");
      IsActive.store(false);
      return false;
    }

    IsActive.store(true);

    return true;

  } catch (const std::exception &e) {
    error("QSV ERROR: %s", e.what());
    IsActive.store(false);
    throw;
  }
}

void DestroyPluginContext(void *Data) {
  plugin_context *Context = static_cast<plugin_context *>(Data);

  if (Context) {
    os_end_high_performance(Context->PerformanceToken);
    if (Context->EncoderPTR) {
      try {

        {
          std::lock_guard<std::mutex> lock(Mutex);

          Context->EncoderPTR->ClearData();

          IsActive.store(false);
          Context->EncoderPTR = nullptr;
        }

        Context->SEI.first = nullptr;
        Context->SEI.second = 0;
        Context->ExtraData.first = nullptr;
        Context->ExtraData.second = 0;
      } catch (const std::exception &e) {
        error("QSV ERROR: %s", e.what());
      }
    }

    delete Context;
    // bfree(Context);
  }
}

bool UpdateEncoderParams(void *Data, obs_data_t *Params) {
  plugin_context *Context = static_cast<plugin_context *>(Data);
  const char *bitrate_control = obs_data_get_string(Params, "rate_control");
  if (std::strcmp(bitrate_control, "CBR") == 0) {
    Context->EncoderParams.TargetBitRate =
        static_cast<mfxU16>(obs_data_get_int(Params, "bitrate"));
  } else if (std::strcmp(bitrate_control, "VBR") == 0) {
    Context->EncoderParams.TargetBitRate =
        static_cast<mfxU16>(obs_data_get_int(Params, "bitrate"));
    Context->EncoderParams.MaxBitRate =
        static_cast<mfxU16>(obs_data_get_int(Params, "max_bitrate"));
  } else if (std::strcmp(bitrate_control, "CQP") == 0) {
    Context->EncoderParams.QPI =
        static_cast<mfxU16>(obs_data_get_int(Params, "cqp"));
    Context->EncoderParams.QPP =
        static_cast<mfxU16>(obs_data_get_int(Params, "cqp"));
    Context->EncoderParams.QPB =
        static_cast<mfxU16>(obs_data_get_int(Params, "cqp"));
  } else if (std::strcmp(bitrate_control, "ICQ") == 0) {
    Context->EncoderParams.ICQQuality =
        static_cast<mfxU16>(obs_data_get_int(Params, "icq_quality"));
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

static int qsv_encoder_reconfig(QSVEncoder *EncoderPTR,
                                encoder_params *EncoderParams) {

  EncoderPTR->UpdateParams(EncoderParams);

  if (EncoderPTR->ReconfigureEncoder() < MFX_ERR_NONE) {

    return false;
  }
  return true;
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
  auto pref_format =
      obs_encoder_get_preferred_video_format(Context->EncoderData);
  if (!(pref_format == VIDEO_FORMAT_NV12 ||
        pref_format == VIDEO_FORMAT_P010)) {
    pref_format = (Info->format == VIDEO_FORMAT_NV12 ||
                   Info->format == VIDEO_FORMAT_P010)
                      ? Info->format
                      : VIDEO_FORMAT_NV12;
  }
  Info->format = pref_format;
}

mfxU64 ConvertTSOBSMFX(int64_t TS, mfxU32 FpsNum) {
  return static_cast<mfxU64>(TS * 90000 / FpsNum);
}

int64_t ConvertTSMFXOBS(mfxI64 TS, mfxU32 FpsNum, mfxU32 FpsDen, int64_t Div) {
  if (TS < 0) {
    return (TS * FpsNum - Div / 2) / Div * FpsDen;
  }
  else {
    return (TS * FpsNum + Div / 2) / Div * FpsDen;
  }
}

static size_t hevc_extract_rbsp(uint8_t *dst, const uint8_t *src,
                                 size_t src_size) {
  size_t dst_pos = 0;
  size_t i = 0;
  while (i < src_size) {
    if (i + 2 < src_size && src[i] == 0 && src[i + 1] == 0 &&
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

static size_t hevc_add_emulation_prevention(uint8_t *dst, const uint8_t *src,
                                             size_t src_size) {
  size_t dst_pos = 0;
  int zero_count = 0;
  for (size_t i = 0; i < src_size; i++) {
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

static uint32_t hevc_read_bits(const uint8_t *data, size_t max_size,
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

static uint32_t hevc_read_uev(const uint8_t *data, size_t max_size,
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

static size_t StripHEVCNALTemporalLayer(uint8_t *dst, const uint8_t *src,
                                         size_t src_size) {
  if (src_size < 3)
    return 0;

  uint8_t nal_type = (src[0] >> 1) & 0x3F;

  if (nal_type != 32) {
    memcpy(dst, src, src_size);
    return src_size;
  }

  uint8_t *rbsp = (uint8_t *)alloca(src_size + 4);
  size_t rbsp_size = hevc_extract_rbsp(rbsp, src, src_size);

  if (rbsp_size < 6) {
    memcpy(dst, src, src_size);
    return src_size;
  }

  nal_type = (rbsp[0] >> 1) & 0x3F;
  if (nal_type != 32) {
    memcpy(dst, src, src_size);
    return src_size;
  }

  // VPS header layout in RBSP (after 2-byte NAL header):
  // byte 2: vps_video_parameter_set_id(4) | vps_reserved_three_2bits(2) | vps_max_layers_minus1(2 MSB)
  // byte 3: vps_max_layers_minus1(1 LSB) | vps_max_sub_layers_minus1(3) | vps_temporal_id_nesting_flag(1) | vps_reserved_0xffff[2:0](3)
  // byte 4-5: vps_reserved_0xffff[15:3](13 bits)
  uint8_t old_max_sublayers = (rbsp[3] >> 4) & 0x7;

  blog(LOG_INFO,
       "[QSV VPS] old_max_sublayers=%d, byte3=0x%02x, src_size=%zu, "
       "rbsp_size=%zu",
       old_max_sublayers, rbsp[3], src_size, rbsp_size);
  blog(LOG_INFO,
       "[QSV VPS] rbsp[0-15]: %02x %02x %02x %02x %02x %02x %02x %02x "
       "%02x %02x %02x %02x %02x %02x %02x %02x",
       rbsp[0], rbsp[1], rbsp[2], rbsp[3], rbsp[4], rbsp[5], rbsp[6],
       rbsp[7], rbsp[8], rbsp[9], rbsp[10], rbsp[11], rbsp[12], rbsp[13],
       rbsp[14], rbsp[15]);

  if (old_max_sublayers == 0) {
    size_t out_size = hevc_add_emulation_prevention(dst, rbsp, rbsp_size);
    return out_size;
  }

  size_t rbsp_out_alloc = rbsp_size + rbsp_size / 2 + 64;
  uint8_t *rbsp_out = (uint8_t *)alloca(rbsp_out_alloc);
  memset(rbsp_out, 0, rbsp_out_alloc);

  memcpy(rbsp_out, rbsp, 6);
  size_t out_byte = 6;
  int out_bit = 7;

  size_t sl_byte = 6;
  int sl_bit = 7;

  for (int i = 0; i < 96; i++) {
    uint8_t bit = hevc_read_bits(rbsp, rbsp_size, sl_byte, sl_bit, 1);
    hevc_write_bits(rbsp_out, out_byte, out_bit, bit, 1);
  }
  hevc_flush_byte(rbsp_out, out_byte, out_bit);

  int max_sublayers_to_process = old_max_sublayers;
  if (max_sublayers_to_process > 7)
    max_sublayers_to_process = 7;

  uint8_t flags[14] = {0};
  for (int i = 0; i < max_sublayers_to_process; i++) {
    flags[i * 2] = hevc_read_bits(rbsp, rbsp_size, sl_byte, sl_bit, 1);
    flags[i * 2 + 1] = hevc_read_bits(rbsp, rbsp_size, sl_byte, sl_bit, 1);
  }

  for (int i = max_sublayers_to_process; i < 8; i++) {
    hevc_read_bits(rbsp, rbsp_size, sl_byte, sl_bit, 2);
  }

  for (int i = 0; i < max_sublayers_to_process; i++) {
    if (flags[i * 2]) {
      for (int j = 0; j < 96; j++)
        hevc_read_bits(rbsp, rbsp_size, sl_byte, sl_bit, 1);
    }
    if (flags[i * 2 + 1]) {
      hevc_read_bits(rbsp, rbsp_size, sl_byte, sl_bit, 8);
    }
  }

  // Set vps_max_sub_layers_minus1 = 0 in output header
  // & 0x8F: preserve bit 7 (vps_max_layers_minus1 MSB) and bits 3-0
  // | 0x09: set bit 3 (vps_temporal_id_nesting_flag=1) and bit 0 (vps_reserved_0xffff LSB=1)
  // This sets bits 6-4 (vps_max_sub_layers_minus1) to 0
  rbsp_out[3] = (rbsp_out[3] & 0x8F) | 0x09;

  blog(LOG_INFO,
       "[QSV VPS] OUT old_max_sublayers=%d, byte3=0x%02x, "
       "out_rbsp_size=%zu",
       old_max_sublayers, rbsp_out[3], out_byte);
  blog(LOG_INFO,
       "[QSV VPS] OUT rbsp[0-15]: %02x %02x %02x %02x %02x %02x %02x %02x "
       "%02x %02x %02x %02x %02x %02x %02x %02x",
       rbsp_out[0], rbsp_out[1], rbsp_out[2], rbsp_out[3], rbsp_out[4],
       rbsp_out[5], rbsp_out[6], rbsp_out[7], rbsp_out[8], rbsp_out[9],
       rbsp_out[10], rbsp_out[11], rbsp_out[12], rbsp_out[13], rbsp_out[14],
       rbsp_out[15]);

  size_t remaining_bits = (rbsp_size - sl_byte) * 8 - (7 - sl_bit);

  for (size_t i = 0; i < remaining_bits; i++) {
    uint8_t bit = hevc_read_bits(rbsp, rbsp_size, sl_byte, sl_bit, 1);
    hevc_write_bits(rbsp_out, out_byte, out_bit, bit, 1);
  }
  hevc_flush_byte(rbsp_out, out_byte, out_bit);

  size_t out_size =
      hevc_add_emulation_prevention(dst, rbsp_out, out_byte);

  return out_size;
}

static void StripHEVCExtraDataTemporalLayer(uint8_t *data, size_t *size) {
  if (!data || !size || *size < 8)
    return;

  size_t tmp_alloc = *size + *size / 2 + 256;
  uint8_t *tmp = new uint8_t[tmp_alloc]();
  size_t tmp_pos = 0;

  size_t offset = 0;
  while (offset < *size) {
    // Find start code (4-byte or 3-byte)
    size_t sc_len = 0;
    if (offset + 4 <= *size && data[offset] == 0 && data[offset + 1] == 0 &&
        data[offset + 2] == 0 && data[offset + 3] == 1) {
      sc_len = 4;
    } else if (offset + 3 <= *size && data[offset] == 0 &&
               data[offset + 1] == 0 && data[offset + 2] == 1) {
      sc_len = 3;
    } else {
      tmp[tmp_pos++] = data[offset++];
      continue;
    }

    // Find NAL data end
    size_t nal_start = offset + sc_len;
    size_t nal_end = nal_start;
    while (nal_end < *size) {
      if (nal_end + 4 <= *size && data[nal_end] == 0 &&
          data[nal_end + 1] == 0 && data[nal_end + 2] == 0 &&
          data[nal_end + 3] == 1)
        break;
      if (nal_end + 3 <= *size && data[nal_end] == 0 &&
          data[nal_end + 1] == 0 && data[nal_end + 2] == 1)
        break;
      nal_end++;
    }

    size_t nal_size = nal_end - nal_start;

    // Copy start code
    memcpy(tmp + tmp_pos, data + offset, sc_len);
    tmp_pos += sc_len;

    if (nal_size > 0) {
      size_t processed = StripHEVCNALTemporalLayer(tmp + tmp_pos, data + nal_start, nal_size);
      tmp_pos += processed;
    }

    offset = nal_end;
  }

  // Replace original with processed extradata
  if (tmp_pos <= *size) {
    memcpy(data, tmp, tmp_pos);
    *size = tmp_pos;
  }

  delete[] tmp;
}

void ParseEncodedPacket(plugin_context *Context, encoder_packet *Packet,
                        mfxBitstream *Bitstream, bool *ReceivedPacketStatus) {
  if (Bitstream == nullptr || Bitstream->DataLength == 0) {
    *ReceivedPacketStatus = false;
    return;
  }

  Context->PacketData.resize(0);

  if (!Context->ExtraData.first || Context->ExtraData.second == 0) {
    uint8_t *NewPacket = 0;
    size_t NewPacketSize = 0;
    if (Context->Codec == QSV_CODEC_AVC) {
      obs_extract_avc_headers(Bitstream->Data + Bitstream->DataOffset,
                              Bitstream->DataLength, &NewPacket,
                              &NewPacketSize, &Context->ExtraData.first,
                              &Context->ExtraData.second, &Context->SEI.first,
                              &Context->SEI.second);
    } else if (Context->Codec == QSV_CODEC_HEVC) {
      // Strip temporal sub-layer info from raw bitstream BEFORE calling
      // obs_extract_hevc_headers to prevent crash in obs_parse_hevc_header
      // when VPS contains vps_max_sub_layers_minus1 > 0 (temporal layers 2-4)
      size_t bitstream_size = Bitstream->DataLength;
      size_t tmp_alloc = bitstream_size + bitstream_size / 2 + 256;
      uint8_t *tmp_bitstream = new uint8_t[tmp_alloc]();
      memcpy(tmp_bitstream, Bitstream->Data + Bitstream->DataOffset,
             bitstream_size);
      StripHEVCExtraDataTemporalLayer(tmp_bitstream, &bitstream_size);

      obs_extract_hevc_headers(tmp_bitstream, bitstream_size, &NewPacket,
                               &NewPacketSize, &Context->ExtraData.first,
                               &Context->ExtraData.second, &Context->SEI.first,
                               &Context->SEI.second);

      delete[] tmp_bitstream;

      // Also strip temporal sub-layer info from extradata as a safety net
      if (Context->ExtraData.first && Context->ExtraData.second > 0) {
        StripHEVCExtraDataTemporalLayer(Context->ExtraData.first,
                                         &Context->ExtraData.second);
      }
    } else if (Context->Codec == QSV_CODEC_AV1) {
      obs_extract_av1_headers(Bitstream->Data + Bitstream->DataOffset,
                              Bitstream->DataLength, &NewPacket,
                              &NewPacketSize, &Context->ExtraData.first,
                              &Context->ExtraData.second);
    }

    Context->PacketData.insert(Context->PacketData.end(), NewPacket,
                               NewPacket + NewPacketSize);
  } else {
    Context->PacketData.insert(Context->PacketData.end(),
                               Bitstream->Data + Bitstream->DataOffset,
                               Bitstream->Data + Bitstream->DataOffset +
                                   Bitstream->DataLength);
  }

  Packet->data = Context->PacketData.data();
  Packet->size = Context->PacketData.size();

  Packet->type = OBS_ENCODER_VIDEO;
  Packet->pts =
      ConvertTSMFXOBS(static_cast<mfxI64>(Bitstream->TimeStamp),
                      Context->CachedFpsNum, Context->CachedFpsDen,
                      Context->CachedTSDiv);
  Packet->dts =
      (Context->Codec == QSV_CODEC_AV1)
          ? Packet->pts
          : ConvertTSMFXOBS(Bitstream->DecodeTimeStamp,
                            Context->CachedFpsNum, Context->CachedFpsDen,
                            Context->CachedTSDiv);
  Packet->keyframe = ((Bitstream->FrameType & MFX_FRAMETYPE_I) ||
                      (Bitstream->FrameType & MFX_FRAMETYPE_IDR) ||
                      (Bitstream->FrameType & MFX_FRAMETYPE_S) ||
                      (Bitstream->FrameType & MFX_FRAMETYPE_xI) ||
                      (Bitstream->FrameType & MFX_FRAMETYPE_xIDR) ||
                      (Bitstream->FrameType & MFX_FRAMETYPE_xS));

  if ((Bitstream->FrameType & MFX_FRAMETYPE_I) ||
      (Bitstream->FrameType & MFX_FRAMETYPE_IDR) ||
      (Bitstream->FrameType & MFX_FRAMETYPE_S) ||
      (Bitstream->FrameType & MFX_FRAMETYPE_xI) ||
      (Bitstream->FrameType & MFX_FRAMETYPE_xIDR) ||
      (Bitstream->FrameType & MFX_FRAMETYPE_xS)) {
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

  *ReceivedPacketStatus = true;

  *Bitstream->Data = 0;
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

  {
    std::lock_guard<std::mutex> lock(Mutex);

    auto *Bitstream = static_cast<mfxBitstream *>(nullptr);

    try {
      Context->EncoderPTR->EncodeTexture(
          ConvertTSOBSMFX(PTS, Context->CachedFpsNum),
          static_cast<void *>(Texture), LockKey, NextKey,
          &Bitstream);
    } catch (const std::exception &e) {
      error("%s", e.what());
      error("encode failed");

      return false;
    }

    ParseEncodedPacket(Context, Packet, Bitstream,
                       ReceivedPacketStatus);
  }

  return true;
}

bool EncodeFrame(void *Data, encoder_frame *Frame, encoder_packet *Packet,
                 bool *ReceivedPacketStatus) {

  plugin_context *Context = static_cast<plugin_context *>(Data);

  if (!Frame || !Packet || !ReceivedPacketStatus) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(Mutex);

    auto *Bitstream = static_cast<mfxBitstream *>(nullptr);

    try {
      if (Frame->data[0]) {
        Context->EncoderPTR->EncodeFrame(
            ConvertTSOBSMFX(Frame->pts, Context->CachedFpsNum), Frame->data,
            Frame->linesize, &Bitstream);
      } else {
        Context->EncoderPTR->EncodeFrame(
            ConvertTSOBSMFX(Frame->pts, Context->CachedFpsNum), nullptr, 0,
            &Bitstream);
      }
    } catch (const std::exception &e) {
      error("%s", e.what());
      error("encode failed");

      return false;
    }

    ParseEncodedPacket(Context, Packet, Bitstream,
                       ReceivedPacketStatus);
  }

  return true;
}
