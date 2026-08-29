#pragma once

#include <QDialog>
#include <QByteArray>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QGroupBox>
#include <QCloseEvent>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <memory>

// avoid macro conflicts with Qt
#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif

// OBS types — needed for opaque pointer members (video_t, obs_encoder_t, etc.)
// Included before FFmpeg so the decltype() in ffmpeg_api doesn't see OBS macros.
#include <obs.h>
#include <obs-data.h>
#include <media-io/video-frame.h>

// OBS defines LOG_* as integer constants; Qt/FFmpeg may define them too, so
// undef to avoid conflicts downstream.
#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif

// FFmpeg headers — for type definitions only (we resolve functions at runtime)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

// Dynamically resolved FFmpeg API — all pointers filled from OBS's loaded DLLs
struct ffmpeg_api {
  // avformat
  decltype(&avformat_open_input) avformat_open_input = nullptr;
  decltype(&avformat_close_input) avformat_close_input = nullptr;
  decltype(&avformat_find_stream_info) avformat_find_stream_info = nullptr;
  decltype(&av_read_frame) av_read_frame = nullptr;
  decltype(&avformat_alloc_output_context2) avformat_alloc_output_context2 = nullptr;
  decltype(&avformat_new_stream) avformat_new_stream = nullptr;
  decltype(&avformat_free_context) avformat_free_context = nullptr;
  decltype(&avio_open) avio_open = nullptr;
  decltype(&avio_closep) avio_closep = nullptr;
  decltype(&avformat_write_header) avformat_write_header = nullptr;
  decltype(&av_write_trailer) av_write_trailer = nullptr;
  decltype(&av_interleaved_write_frame) av_interleaved_write_frame = nullptr;
  decltype(&av_packet_unref) av_packet_unref = nullptr;
  decltype(&avcodec_parameters_copy) avcodec_parameters_copy = nullptr;
  decltype(&av_packet_alloc) av_packet_alloc = nullptr;
  decltype(&av_packet_free) av_packet_free = nullptr;
  decltype(&av_find_best_stream) av_find_best_stream = nullptr;
  decltype(&av_packet_move_ref) av_packet_move_ref = nullptr;

  // avcodec
  decltype(&avcodec_find_decoder) avcodec_find_decoder = nullptr;
  decltype(&avcodec_find_decoder_by_name) avcodec_find_decoder_by_name = nullptr;
  decltype(&avcodec_alloc_context3) avcodec_alloc_context3 = nullptr;
  decltype(&avcodec_parameters_to_context) avcodec_parameters_to_context = nullptr;
  decltype(&avcodec_open2) avcodec_open2 = nullptr;
  decltype(&avcodec_send_packet) avcodec_send_packet = nullptr;
  decltype(&avcodec_receive_frame) avcodec_receive_frame = nullptr;
  decltype(&avcodec_free_context) avcodec_free_context = nullptr;

  // avutil
  decltype(&av_mallocz) av_mallocz = nullptr;
  decltype(&av_frame_alloc) av_frame_alloc = nullptr;
  decltype(&av_frame_free) av_frame_free = nullptr;
  decltype(&av_image_get_buffer_size) av_image_get_buffer_size = nullptr;
  decltype(&av_image_fill_arrays) av_image_fill_arrays = nullptr;
  decltype(&av_rescale_q) av_rescale_q = nullptr;
  // used for logging the decoded pixel format
  decltype(&av_get_pix_fmt_name) av_get_pix_fmt_name = nullptr;

  // swscale
  decltype(&sws_getContext) sws_getContext = nullptr;
  decltype(&sws_scale) sws_scale = nullptr;
  decltype(&sws_freeContext) sws_freeContext = nullptr;
};

bool LoadFFmpegAPI(ffmpeg_api &ff);

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

public:
  // Encoding context — exposed to output callbacks (static functions in .cpp)
  struct ReEncodeCtx {
    // FFmpeg input
    AVFormatContext *in_fmt_ctx = nullptr;
    int video_stream_idx = -1;
    int audio_stream_idx = -1;
    AVCodecContext *video_decoder = nullptr;
    SwsContext *sws_ctx = nullptr;
    AVFrame *decoded_frame = nullptr;
    // decoded frame converted to the OBS-facing pixel format (NV12 or P010)
    AVFrame *conv_frame = nullptr;

    // FFmpeg output
    AVFormatContext *out_fmt_ctx = nullptr;
    AVStream *out_video_stream = nullptr;
    AVStream *out_audio_stream = nullptr;

    // Encoded video packet queue (filled by obs output callback, consumed by
    // feed thread)
    struct Packet {
      std::vector<uint8_t> data;
      int64_t pts;
      int64_t dts;
      bool keyframe;
    };
    std::mutex pkt_mutex;
    std::condition_variable pkt_cv;
    // deque: pop_front() is O(1); the old vector + erase(begin()) shifted the
    // whole queue on every encoded frame.
    std::deque<Packet> pkt_queue;
    bool encoder_done = false;
    // MP4 header is written lazily from the feed thread, after the encoder
    // has produced its first keyframe (parameter sets only become available
    // then).  Guarded by happening entirely on the feed thread.
    bool header_written = false;

    // Audio packets from input (stream copy, re-timestamped for output)
    std::vector<AVPacket *> audio_packets;

    // Progress tracking
    int64_t duration = 0;
    int64_t total_frames = 0;
    std::atomic<int64_t> frames_encoded{0};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> encoder_flushed{false};
    std::atomic<bool> feed_error{false};
    std::string error_msg;
    std::mutex err_mutex;
  };

  ReEncodeCtx m_Ctx;

private:
  // OBS pipeline objects
  video_t *m_Video = nullptr;
  obs_encoder_t *m_Encoder = nullptr;
  obs_output_t *m_Output = nullptr;

  // FFmpeg API
  ffmpeg_api m_FF;

  std::thread m_FeedThread;
  std::atomic<bool> m_Encoding{false};

  // Encoder config (from active QSV encoder or profile config)
  std::string m_EncoderID;
  obs_data_t *m_EncoderSettings = nullptr;
  int m_Width = 0;
  int m_Height = 0;
  int m_FpsNum = 30;
  int m_FpsDen = 1;
  // input is 10-bit → run the OBS video pipeline as P010
  bool m_Input10bit = false;

  // keep the QByteArray alive so the const char* from toUtf8() stays valid
  QByteArray m_OutputPathBytes;

  // UI
  QLineEdit *InputPath = nullptr;
  QLineEdit *OutputPath = nullptr;
  QPushButton *StartStopBtn = nullptr;
  QLabel *StatusLabel = nullptr;
  QProgressBar *ProgressBar = nullptr;
  QTextEdit *LogOutput = nullptr;
  QLabel *ConfigLabel = nullptr;
  QGroupBox *ConfigGroup = nullptr;

  void SetUIEnabled(bool Enabled);
  void AppendLog(const QString &Msg);
  void UpdateProgress(int64_t Current, int64_t Total);

  bool LoadEncoderConfigFromActive();
  bool LoadEncoderConfigFromFile();
  bool StartEncoding();
  void StopEncoding();
  void FeedThreadMain();
};

// output is registered here so obs_output_create can find it
void RegisterReEncoder();