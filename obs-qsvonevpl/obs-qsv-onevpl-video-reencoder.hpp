#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QCloseEvent>
#include <QShowEvent>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>

#include "obs-qsv-onevpl-encoder.hpp"
#include "helpers/qsv_params.hpp"

// FFmpeg function pointer typedefs — loaded dynamically from OBS's bundled DLLs
struct FFmpegFuncs {
  // avformat
  int (*avformat_open_input)(void **ps, const char *url, void *fmt, void **opts);
  void (*avformat_close_input)(void **s);
  int (*avformat_find_stream_info)(void *ic, void **options);
  int (*av_read_frame)(void *s, void *pkt);
  int (*av_find_best_stream)(void *ic, int type, int wanted_stream_nb,
                             int related_stream, void **decoder_ret, int flags);

  // avcodec
  void *(*avcodec_find_decoder)(int id);
  void *(*avcodec_alloc_context3)(const void *codec);
  int (*avcodec_parameters_to_context)(void *avctx, const void *par);
  int (*avcodec_open2)(void *avctx, const void *codec, void **opts);
  int (*avcodec_send_packet)(void *avctx, const void *pkt);
  int (*avcodec_receive_frame)(void *avctx, void *frame);
  void (*avcodec_free_context)(void **avctx);

  // avutil
  void *(*av_frame_alloc)(void);
  void (*av_frame_free)(void **frame);
  void *(*av_packet_alloc)(void);
  void (*av_packet_free)(void **pkt);
  int (*av_image_get_buffer_size)(int pix_fmt, int w, int h, int align);
  int (*av_image_fill_arrays)(uint8_t **dst_data, int *dst_linesize,
                              const uint8_t *src, int pix_fmt,
                              int w, int h, int align);

  // swscale
  void *(*sws_getContext)(int srcW, int srcH, int srcFormat,
                          int dstW, int dstH, int dstFormat,
                          int flags, void *srcFilter,
                          void *dstFilter, const double *param);
  int (*sws_scale)(void *c, const uint8_t *const *srcSlice,
                   const int *srcStride, int srcSliceY, int srcSliceH,
                   uint8_t *const *dst, const int *dstStride);
  void (*sws_freeContext)(void *c);
};

// Load FFmpeg DLLs and resolve function pointers. Returns true on success.
bool LoadFFmpegDyn(FFmpegFuncs &ff);

// Video re-encoder dialog — toolbar tool for re-encoding video files using
// the current QSV encoder configuration.
class ReEncodeDialog : public QDialog {
  Q_OBJECT

public:
  explicit ReEncodeDialog(QWidget *Parent = nullptr);
  ~ReEncodeDialog() override;

  // Called externally (frontend event callback) to refresh config
  void PopulateEncoderConfig();

protected:
  void closeEvent(QCloseEvent *Event) override;

private slots:
  void OnBrowseInput();
  void OnBrowseOutput();
  void OnStartStop();
  void OnRefreshConfig();

private:
  bool StartEncoding();
  void StopEncoding();
  void EncodeThreadMain();
  void AppendLog(const QString &Msg);
  void SetUIEnabled(bool Enabled);
  void UpdateProgress(int64_t Current, int64_t Total);

  // Fallback: load encoder config from OBS profile files
  bool LoadEncoderConfigFromFile();

  // UI
  QLineEdit *InputPathEdit;
  QLineEdit *OutputPathEdit;
  QPushButton *BrowseInputBtn;
  QPushButton *BrowseOutputBtn;
  QPushButton *RefreshConfigBtn;
  QPushButton *StartStopBtn;
  QLabel *StatusLabel;
  QProgressBar *ProgressBar;
  QTextEdit *LogOutput;

  // Encoder config display (read-only summary)
  QLabel *ConfigLabel;
  QGroupBox *ConfigGroup;

  // Encoding state
  std::atomic<bool> m_Encoding{false};
  std::atomic<bool> m_StopRequested{false};
  std::thread m_EncodeThread;
  int64_t m_TotalFrames{0};
  int64_t m_EncodedFrames{0};

  // Encoder params — populated from active QSV encoder or recording config
  encoder_params m_EncoderParams{};
  codec_enum m_Codec{QSV_CODEC_AVC};
  bool m_ParamsValid{false};
};