#pragma once
#include "helpers/common_utils.hpp"
#include "obs-qsv-onevpl-encoder-internal.hpp"
#include "obs-qsv-onevpl-plugin-init.hpp"

inline unsigned short VPLVersionMajor;
inline unsigned short VPLVersionMinor;

void GetEncoderVersion(unsigned short *Major, unsigned short *Minor);
bool OpenEncoder(std::unique_ptr<class QSVEncoder> &EncoderPTR,
                 encoder_params *EncoderParams, enum codec_enum Codec,
                 bool IsTextureEncoder);
void DestroyPluginContext(void *);
bool UpdateEncoderParams(void *Data, obs_data_t *Params);

mfxU64 ConvertTSOBSMFX(int64_t TS, mfxU32 FpsNum);

int64_t ConvertTSMFXOBS(mfxI64 TS, mfxU32 FpsNum, mfxU32 FpsDen, int64_t Div);

bool GetExtraData(void *Data, uint8_t **ExtraData, size_t *Size);

void UpdateEncoderROI(void *Data,
                       const std::vector<encoder_params::roi_region> &Regions,
                       mfxU16 Mode, bool Enabled);

struct plugin_context;

void ParseEncodedPacket(plugin_context *Context, encoder_packet *Packet,
                  mfxBitstream *Bitstream, bool *ReceivedPacketStatus);

bool EncodeTexture(void *Data, encoder_texture *Texture, int64_t PTS,
                        uint64_t LockKey, uint64_t *NextKey, encoder_packet *Packet,
                   bool *ReceivedPacketStatus);

bool EncodeFrame(void *Data, encoder_frame *Frame, encoder_packet *Packet,
                 bool *ReceivedPacketStatus);

bool GetSEIData(void *Data, uint8_t **SEI, size_t *Size);

void GetVideoInfo(void *Data, video_scale_info *Info);
