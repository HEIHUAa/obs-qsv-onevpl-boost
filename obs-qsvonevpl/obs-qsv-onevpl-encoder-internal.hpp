#pragma once
#include <chrono>
#include "helpers/common_utils.hpp"
#include "obs-qsv-onevpl-encoder.hpp"
#include "helpers/ext_buf_manager.hpp"
#include "helpers/qsv_params.hpp"

class QSVEncoder {
public:
  QSVEncoder() = default;
  ~QSVEncoder();

  mfxStatus GetVPLVersion(mfxVersion &);
  mfxVersion GetCachedVPLVersion() const { return QSVVersion; }
  mfxStatus Init(struct encoder_params *InputParams, enum codec_enum Codec,
                 bool IsTextureEncoder);
mfxStatus EncodeFrameSystemMemory(mfxU64 TS, uint8_t **FrameData,
                                    uint32_t *FrameLinesize,
                                    mfxBitstream **Bitstream);
  mfxStatus EncodeFrame(mfxU64 TS, uint8_t **FrameData, uint32_t *FrameLinesize,
                        mfxBitstream **Bitstream);
  mfxStatus EncodeTexture(mfxU64 TS, void *TextureHandle, uint64_t LockKey,
                          uint64_t *NextKey, mfxBitstream **Bitstream);
  mfxStatus ClearData();
  mfxStatus ReconfigureEncoder();
  bool UpdateParams(struct encoder_params *InputParams);
  void UpdateROIRegions(const std::vector<encoder_params::roi_region> &Regions,
                         mfxU16 Mode);

  // Exposed for the offline re-encoder so it can pull encoded bitstream
  // frames from the async pipeline without changing the normal OBS path.
  mfxStatus SyncAndSwapPendingTask(mfxBitstream **Bitstream);
  

  // Drain the encoder and return each completed bitstream one by one.
  // Returns MFX_ERR_NONE with *Bitstream set, or MFX_ERR_MORE_DATA when done.
  mfxStatus DrainAndRetrieveBitstream(mfxBitstream **Bitstream);

  protected:
  struct Task {
    mfxBitstream Bitstream{};
    mfxSyncPoint SyncPoint{};
    mfxFrameSurface1 *Surface{};
  };

  mfxStatus CreateSession(enum codec_enum Codec, [[maybe_unused]] void **Data,
                          int GPUNum);

  mfxStatus SetProcessingParams(struct encoder_params *InputParams,
                                enum codec_enum Codec);
  mfxStatus SetEncoderParams(struct encoder_params *InputParams,
                             enum codec_enum Codec);

  void ParseCustomCodingOptions(const std::string &Options);
  void ApplyQPLimits(struct encoder_params *InputParams);

  mfxStatus GetVideoParam(enum codec_enum Codec);
  void LogActualParams();

  mfxStatus InitTexturePool();
void InitSystemMemorySurfacePool();
  void ReleaseSystemMemorySurfacePool();
  mfxStatus InitBitstreamBuffer(enum codec_enum Codec);
  void ReleaseBitstream();
  mfxStatus InitTaskPool(enum codec_enum Codec);
  void ReleaseTask(int TaskID);
  void ReleaseTaskPool();
  mfxStatus ChangeBitstreamSize(mfxU32 NewSize);
  mfxStatus GetFreeTaskIndex(int *TaskID);
  int GetFreeTaskIndex(); // obs-qsv11 style: no-arg, no QSVSyncTaskID advance
  mfxStatus EncodeFrameRetryLoop(mfxFrameSurface1 *Surface,
                                  mfxEncodeCtrl *Ctrl, int TaskID,
                                  mfxU32 MaxRetries = 200);

  void LoadFrameData(mfxFrameSurface1 *&Surface, uint8_t **FrameData,
                     uint32_t *FrameLinesize);

  void SetupROIEncodeCtrl();

  mfxStatus Drain();

  void DisableVPP();

  void WarmUpEncoder();

  template <typename T>
  static inline T GetTriState(const std::optional<bool> &Value,
                              const T DefaultValue, const T OnValue,
                              const T OffValue) {
    if (!Value.has_value()) {
      return DefaultValue;
    }
    return Value.value() ? OnValue : OffValue;
  }

  static inline mfxU16 GetCodingOpt(const std::optional<bool> &Value) {
    return static_cast<mfxU16>(GetTriState(Value, MFX_CODINGOPTION_UNKNOWN,
                                           MFX_CODINGOPTION_ON,
                                           MFX_CODINGOPTION_OFF));
  }

  static inline std::string GetCodingOptStatus(const mfxU16 &Value) {
    if (Value == MFX_CODINGOPTION_ON) {
      return "ON";
    } else if (Value == MFX_CODINGOPTION_OFF) {
      return "OFF";
    } else {
      return "AUTO";
    }
  }

  mfxStatus InitEncoderInternal(encoder_params *InputParams,
                                enum codec_enum Codec,
                                const char *log_prefix);

private:
  mfxPlatform QSVPlatform{};
  mfxVersion QSVVersion{};
  mfxLoader QSVLoader{};
  mfxConfig QSVLoaderConfig[8]{};
  mfxVariant QSVLoaderVariant[8]{};
  mfxSession QSVSession{};
  mfxIMPL QSVImpl{};
#if defined(__linux__)
  void *QSVSessionData;
#endif

  mfxFrameSurface1 *QSVEncodeSurface{};

  mfxFrameSurface1 *QSVProcessingSurface{};

  std::unique_ptr<MFXVideoENCODE> QSVEncode{};
  std::unique_ptr<MFXVideoVPP> QSVProcessing{};

  mfxU8 QSVVPSBuffer[1024]{};
  mfxU8 QSVSPSBuffer[1024]{};
  mfxU8 QSVPPSBuffer[1024]{};
  mfxU16 QSVVPSBufferSize{1024};
  mfxU16 QSVSPSBufferSize{1024};
  mfxU16 QSVPPSBufferSize{1024};

  mfxBitstream QSVBitstream{};
  std::vector<struct Task> QSVTaskPool;
  int QSVSyncTaskID{};
  mutable std::mutex QSVTaskPoolMutex{};

  mfxVideoParam QSVResetParams{};
  bool QSVResetParamsChanged{false};

  MFXVideoParam QSVEncodeParams{};
  MFXVideoParam QSVProcessingParams{};
  MFXEncodeCtrl QSVEncodeCtrlParams{};

  mfxExtVppAuxData* QSVProcessingAuxData{};
  
  mfxFrameAllocRequest QSVAllocateRequest{};

  bool QSVUseSystemMemoryPath{};

  struct SystemMemSurface {
    mfxFrameSurface1 Surface{};
    mfxU8 *Buffer{};
  };
  std::vector<SystemMemSurface> QSVSystemMemPool;
  mfxU16 QSVSystemMemPoolSize{};

  bool QSVIsTextureEncoder{};
  // Tracks whether a drain marker has been submitted for offline re-encoder.
  bool m_DrainSubmitted{false};
  mfxMemoryInterface *QSVMemoryInterface{};

  std::unique_ptr<class HWManager> HWManager{};

  // Pre-registered VPL surfaces — one per texture pool entry.
  // Imported once at init and reused every frame, avoiding per-frame
  // ImportFrameSurface / Release overhead.
  std::vector<mfxFrameSurface1 *> QSVPreRegisteredSurfaces;

  bool QSVProcessingEnable{};

  mfxU32 QSVEncodeRefCount{};
  mfxU32 QSVProcessingRefCount{};

  mfxSyncPoint QSVProcessingSyncPoint{};
  
  enum class AdditionalFourCC {
    MFX_FOURCC_IMC3 = MFX_MAKEFOURCC('I', 'M', 'C', '3'),
    MFX_FOURCC_YUV400 = MFX_MAKEFOURCC('4', '0', '0', 'P'),
    MFX_FOURCC_YUV411 = MFX_MAKEFOURCC('4', '1', '1', 'P'),
    MFX_FOURCC_YUV422H = MFX_MAKEFOURCC('4', '2', '2', 'H'),
    MFX_FOURCC_YUV422V = MFX_MAKEFOURCC('4', '2', '2', 'V'),
    MFX_FOURCC_YUV444 = MFX_MAKEFOURCC('4', '4', '4', 'P'),
    MFX_FOURCC_RGBP24 = MFX_MAKEFOURCC('R', 'G', 'B', 'P'),
  };

  // ROI (Region of Interest) data for per-frame encoding control
  std::vector<encoder_params::roi_region> CachedROIRegions;
  mfxU16 CachedROIMode{};
  // ROI log per encoder instance (not shared across instances)
  bool ROIFirstLogDone = false;

  // AV1 segmentation map. Must stay alive — mfxExtAV1Segmentation::SegmentIds points into it.
  std::vector<mfxU8> AV1SegmentationMap;

  static constexpr size_t QP_HISTOGRAM_SIZE = 101;

  struct QPFrameTypeStats {
    uint64_t count = 0;
    uint64_t sumQP = 0;
    mfxU16 minQP = UINT16_MAX;
    mfxU16 maxQP = 0;
    std::array<uint64_t, QP_HISTOGRAM_SIZE> histogram{};
  };

  struct QPFrameStats {
    QPFrameTypeStats i, p, b;
    uint64_t totalFrames = 0;
  };

  QPFrameStats FrameQPStats;
  bool QPStatsEnabled = true; // cached from InputParams for Drain path

  // One mfxExtEncodedFrameInfo per task, attached to each task's
  // bitstream so the encoder fills in frame-level QP after encode.
  std::vector<mfxExtEncodedFrameInfo> QSVTaskEncodedInfo;
  // Each task's bitstream.ExtParam points into the array below.
  std::vector<mfxExtBuffer *> QSVTaskEncodedExtPtr;

  // ─ Per-frame QP tracking ─
  static constexpr size_t QSV_SEI_EXTRA = 1024; // extra bytes per task for SEI injection

  // ─ Custom Coding Options deferred logging ─
  struct CustomCodingOptionEntry {
    int LineNo;
    std::string Scope;
    std::string Field;
    std::string RawVal;
  };
  std::vector<CustomCodingOptionEntry> m_CustomCodingOptions;

  void UpdateFrameQPStats(mfxU16 frameType, mfxU16 qp);
  void RecordQPFromBitstream(const mfxBitstream &bs);
  void LogQPStats();
  void LogVideoHeaderHexDump();

  // QP stats SEI injection — appends user_data_unregistered SEI per frame
  static constexpr uint8_t QP_SEI_UUID[16] = {
      0xe7, 0xa5, 0xa8, 0xd0, 0x6b, 0x3c, 0x4c, 0x2e,
      0x9f, 0x1d, 0x8a, 0x5b, 0x7c, 0x9d, 0x3f, 0x1a};

  void AppendQpSeiToBitstream(mfxBitstream &bs);

  void GetQpStatsSei(uint8_t **data, size_t *size);
  std::vector<uint8_t> QpStatsSeiBuffer; // last appended SEI NAL
  std::string QpSeiPayload;              // reused buffer for SEI payload
};