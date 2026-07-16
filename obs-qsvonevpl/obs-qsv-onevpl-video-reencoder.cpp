// Qt headers must come before OBS headers to avoid macro conflicts
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "obs-qsv-onevpl-video-reencoder.hpp"
#include "obs-qsv-onevpl-plugin-init.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>
#include <cstdio>

using namespace std::chrono_literals;

// ============================================================================
// Output ID for our custom qsv re-encode output
// ============================================================================
static const char *const REENCODE_OUTPUT_ID = "qsv_reencode_output";

// Per-output context — passed to output callbacks via void *data
struct reencode_output_ctx {
  ReEncodeDialog *dialog;
};

// ============================================================================
// FFmpeg API loading — resolve function pointers from OBS's bundled DLLs
// ============================================================================

// get a module handle for an already-loaded DLL by base name + version range
static HMODULE GetLoadedModule(const wchar_t *baseName, int minVer, int maxVer)
{
  wchar_t name[64];
  for (int v = maxVer; v >= minVer; --v) {
    swprintf_s(name, L"%s-%d.dll", baseName, v);
    HMODULE h = GetModuleHandleW(name);
    if (h)
      return h;
  }
  // try unversioned
  swprintf_s(name, L"%s.dll", baseName);
  return GetModuleHandleW(name);
}

// resolve a single function pointer from a module
template <typename T>
static bool ResolveFunc(HMODULE mod, const char *name, T &ptr)
{
  void *addr = reinterpret_cast<void *>(GetProcAddress(mod, name));
  if (!addr) {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] GetProcAddress(%s) failed", name);
    return false;
  }
  std::memcpy(&ptr, &addr, sizeof(void *));
  return true;
}

bool LoadFFmpegAPI(ffmpeg_api &ff)
{
  memset(&ff, 0, sizeof(ff));

  HMODULE avutil = GetLoadedModule(L"avutil", 55, 60);
  HMODULE avcodec = GetLoadedModule(L"avcodec", 57, 62);
  HMODULE avformat = GetLoadedModule(L"avformat", 57, 62);
  HMODULE swscale = GetLoadedModule(L"swscale", 5, 8);

  if (!avutil || !avcodec || !avformat || !swscale) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Failed to find FFmpeg DLLs "
                    "(avutil=%p avcodec=%p avformat=%p swscale=%p)",
         avutil, avcodec, avformat, swscale);
    return false;
  }

  bool ok = true;

  // avutil
  ok = ok && ResolveFunc(avutil, "av_frame_alloc", ff.av_frame_alloc);
  ok = ok && ResolveFunc(avutil, "av_frame_free", ff.av_frame_free);
  ok = ok && ResolveFunc(avutil, "av_image_get_buffer_size", ff.av_image_get_buffer_size);
  ok = ok && ResolveFunc(avutil, "av_image_fill_arrays", ff.av_image_fill_arrays);

  // av_packet_alloc/free may be in avutil or avcodec (OBS layout varies)
  ff.av_packet_alloc = reinterpret_cast<decltype(ff.av_packet_alloc)>(
      GetProcAddress(avutil, "av_packet_alloc"));
  ff.av_packet_free = reinterpret_cast<decltype(ff.av_packet_free)>(
      GetProcAddress(avutil, "av_packet_free"));

  // avcodec
  ok = ok && ResolveFunc(avcodec, "avcodec_find_decoder", ff.avcodec_find_decoder);
  ok = ok && ResolveFunc(avcodec, "avcodec_alloc_context3", ff.avcodec_alloc_context3);
  ok = ok && ResolveFunc(avcodec, "avcodec_parameters_to_context", ff.avcodec_parameters_to_context);
  ok = ok && ResolveFunc(avcodec, "avcodec_open2", ff.avcodec_open2);
  ok = ok && ResolveFunc(avcodec, "avcodec_send_packet", ff.avcodec_send_packet);
  ok = ok && ResolveFunc(avcodec, "avcodec_receive_frame", ff.avcodec_receive_frame);
  ok = ok && ResolveFunc(avcodec, "avcodec_free_context", ff.avcodec_free_context);

  // av_packet_alloc/free from avcodec as fallback
  if (!ff.av_packet_alloc) {
    ff.av_packet_alloc = reinterpret_cast<decltype(ff.av_packet_alloc)>(
        GetProcAddress(avcodec, "av_packet_alloc"));
  }
  if (!ff.av_packet_free) {
    ff.av_packet_free = reinterpret_cast<decltype(ff.av_packet_free)>(
        GetProcAddress(avcodec, "av_packet_free"));
  }
  if (!ff.av_packet_alloc || !ff.av_packet_free) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot resolve av_packet_alloc/free");
    return false;
  }

  // avformat
  ok = ok && ResolveFunc(avformat, "avformat_open_input", ff.avformat_open_input);
  ok = ok && ResolveFunc(avformat, "avformat_close_input", ff.avformat_close_input);
  ok = ok && ResolveFunc(avformat, "avformat_find_stream_info", ff.avformat_find_stream_info);
  ok = ok && ResolveFunc(avformat, "av_read_frame", ff.av_read_frame);
  ok = ok && ResolveFunc(avformat, "avformat_alloc_output_context2", ff.avformat_alloc_output_context2);
  ok = ok && ResolveFunc(avformat, "avformat_new_stream", ff.avformat_new_stream);
  ok = ok && ResolveFunc(avformat, "avformat_free_context", ff.avformat_free_context);
  ok = ok && ResolveFunc(avformat, "avio_open", ff.avio_open);
  ok = ok && ResolveFunc(avformat, "avio_closep", ff.avio_closep);
  ok = ok && ResolveFunc(avformat, "avformat_write_header", ff.avformat_write_header);
  ok = ok && ResolveFunc(avformat, "av_write_trailer", ff.av_write_trailer);
  ok = ok && ResolveFunc(avformat, "av_interleaved_write_frame", ff.av_interleaved_write_frame);
  ok = ok && ResolveFunc(avformat, "av_packet_unref", ff.av_packet_unref);
  ok = ok && ResolveFunc(avformat, "avcodec_parameters_copy", ff.avcodec_parameters_copy);

  // swscale
  ok = ok && ResolveFunc(swscale, "sws_getContext", ff.sws_getContext);
  ok = ok && ResolveFunc(swscale, "sws_scale", ff.sws_scale);
  ok = ok && ResolveFunc(swscale, "sws_freeContext", ff.sws_freeContext);

  if (!ok) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Failed to resolve FFmpeg functions");
    return false;
  }

  blog(LOG_INFO, "[QSV VPL ReEncoder] FFmpeg API loaded successfully");
  return true;
}

// ============================================================================
// Custom output — receives encoded video packets, pushes to queue for feed
// thread to mux.  Audio is handled separately by the feed thread.
// ============================================================================

static const char *reencode_output_getname(void *)
{
  return "QSV Re-Encode Output";
}

// static used to pass the dialog pointer from StartEncoding() into
// reencode_output_create(), since obs_output_t is opaque and we can't
// access context.data from outside.
static ReEncodeDialog *g_PendingOutputDialog = nullptr;

static void *reencode_output_create(obs_data_t *, obs_output_t *output)
{
  auto *ctx = new reencode_output_ctx;
  ctx->dialog = g_PendingOutputDialog;
  g_PendingOutputDialog = nullptr;
  obs_output_set_media(output, obs_get_video(), obs_get_audio());
  return ctx;
}

static void reencode_output_destroy(void *data)
{
  delete static_cast<reencode_output_ctx *>(data);
}

static bool reencode_output_start(void *data)
{
  // nothing — feed thread handles all I/O
  UNUSED_PARAMETER(data);
  return true;
}

static void reencode_output_stop(void *data, uint64_t)
{
  // signal feed thread to drain
  auto *ctx = static_cast<reencode_output_ctx *>(data);
  if (ctx->dialog) {
    auto &rc = ctx->dialog->m_Ctx;
    rc.encoder_done = true;
    rc.pkt_cv.notify_all();
  }
}

static void reencode_output_encoded_packet(void *data, struct encoder_packet *packet)
{
  auto *ctx = static_cast<reencode_output_ctx *>(data);
  if (!ctx->dialog || packet->type != OBS_ENCODER_VIDEO)
    return;

  auto &rc = ctx->dialog->m_Ctx;

  ReEncodeDialog::ReEncodeCtx::Packet pkt;
  pkt.data.assign(packet->data, packet->data + packet->size);
  pkt.pts = packet->pts;
  pkt.dts = packet->dts;
  pkt.keyframe = packet->keyframe;

  {
    std::lock_guard lock(rc.pkt_mutex);
    rc.pkt_queue.push_back(std::move(pkt));
  }
  rc.pkt_cv.notify_one();
}

static obs_output_info reencode_output_info = {
    .id = REENCODE_OUTPUT_ID,
    .flags = OBS_OUTPUT_ENCODED | OBS_OUTPUT_VIDEO,
    .get_name = reencode_output_getname,
    .create = reencode_output_create,
    .destroy = reencode_output_destroy,
    .start = reencode_output_start,
    .stop = reencode_output_stop,
    .encoded_packet = reencode_output_encoded_packet,
};

// ============================================================================
// Encoder config loading
// ============================================================================

// map encoder ID to FFmpeg codec ID
static enum AVCodecID EncoderIDToAVCodecID(const std::string &id)
{
  if (id.find("h264") != std::string::npos)
    return AV_CODEC_ID_H264;
  if (id.find("hevc") != std::string::npos)
    return AV_CODEC_ID_HEVC;
  if (id.find("av1") != std::string::npos)
    return AV_CODEC_ID_AV1;
  if (id.find("vp9") != std::string::npos)
    return AV_CODEC_ID_VP9;
  return AV_CODEC_ID_NONE;
}

// strip "_tex" suffix to get the frame (non-texture) encoder ID
static std::string ToFrameEncoderID(const std::string &id)
{
  if (id.ends_with("_tex"))
    return id.substr(0, id.size() - 4);
  return id;
}

// try to read encoder config from the active recording encoder
bool ReEncodeDialog::LoadEncoderConfigFromActive()
{
  // check the EncoderDataMap for active QSV encoders
  extern std::mutex EncoderDataMapMutex;
  extern std::unordered_map<obs_encoder_t *, plugin_context *> EncoderDataMap;

  std::lock_guard lock(EncoderDataMapMutex);
  for (auto &pair : EncoderDataMap) {
    plugin_context *ctx = pair.second;
    if (!ctx)
      continue;

    m_EncoderID = ToFrameEncoderID(obs_encoder_get_id(ctx->EncoderData));
    m_Width = ctx->EncoderParams.Width;
    m_Height = ctx->EncoderParams.Height;
    m_FpsNum = ctx->EncoderParams.FpsNum;
    m_FpsDen = ctx->EncoderParams.FpsDen;

    if (m_FpsNum <= 0 || m_FpsDen <= 0) {
      m_FpsNum = 30;
      m_FpsDen = 1;
    }

    // copy encoder settings as obs_data
    obs_data_t *settings = obs_encoder_get_settings(ctx->EncoderData);
    obs_data_addref(settings);
    m_EncoderSettings = settings;

    blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded config from active encoder: %s %dx%d %d/%d fps",
         m_EncoderID.c_str(), m_Width, m_Height, m_FpsNum, m_FpsDen);
    return true;
  }

  return false;
}

// fallback: read from OBS profile config files
bool ReEncodeDialog::LoadEncoderConfigFromFile()
{
  config_t *config = obs_frontend_get_profile_config();
  if (!config) {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] No profile config");
    return false;
  }

  // determine output mode and read recording encoder ID
  const char *mode = config_get_string(config, "Output", "Mode");
  const char *encId = nullptr;

  if (mode && strcmp(mode, "Advanced") == 0) {
    encId = config_get_string(config, "AdvOut", "RecEncoder");
  } else {
    // Simple mode — check if using streaming encoder or separate recording
    encId = config_get_string(config, "SimpleOutput", "RecEncoder");
    if (!encId || !*encId || strcmp(encId, "none") == 0) {
      encId = config_get_string(config, "SimpleOutput", "StreamEncoder");
    }
  }

  if (!encId || !*encId) {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] No encoder ID in profile config");
    return false;
  }

  // check if it's a QSV encoder
  std::string encoderId(encId);
  if (encoderId.find("obs_qsv") == std::string::npos) {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] Encoder '%s' is not a QSV encoder", encId);
    return false;
  }

  m_EncoderID = ToFrameEncoderID(encoderId);

  // read basic settings
  m_Width = (int)config_get_uint(config, "Video", "BaseCX");
  m_Height = (int)config_get_uint(config, "Video", "BaseCY");
  m_FpsNum = (int)config_get_uint(config, "Video", "FPSNum");
  m_FpsDen = (int)config_get_uint(config, "Video", "FPSDen");

  if (m_FpsNum <= 0 || m_FpsDen <= 0) {
    m_FpsNum = 30;
    m_FpsDen = 1;
  }

  blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded config from profile: %s %dx%d %d/%d fps",
       m_EncoderID.c_str(), m_Width, m_Height, m_FpsNum, m_FpsDen);
  return true;
}

// ============================================================================
// ReEncodeDialog — UI construction
// ============================================================================

ReEncodeDialog::ReEncodeDialog(QWidget *Parent)
    : QDialog(Parent)
{
  setWindowTitle(obs_module_text("ReEncoder"));
  setMinimumSize(550, 520);
  setAttribute(Qt::WA_DeleteOnClose, false);

  auto *mainLayout = new QVBoxLayout(this);

  // Input file
  auto *inputLayout = new QHBoxLayout;
  InputPath = new QLineEdit(this);
  InputPath->setPlaceholderText(obs_module_text("ReEncoderInputPlaceholder"));
  auto *browseInputBtn = new QPushButton(obs_module_text("ReEncoderBrowse"), this);
  inputLayout->addWidget(InputPath);
  inputLayout->addWidget(browseInputBtn);
  mainLayout->addLayout(inputLayout);

  // Output file
  auto *outputLayout = new QHBoxLayout;
  OutputPath = new QLineEdit(this);
  OutputPath->setPlaceholderText(obs_module_text("ReEncoderOutputPlaceholder"));
  auto *browseOutputBtn = new QPushButton(obs_module_text("ReEncoderBrowse"), this);
  outputLayout->addWidget(OutputPath);
  outputLayout->addWidget(browseOutputBtn);
  mainLayout->addLayout(outputLayout);

  // Encoder config display
  ConfigGroup = new QGroupBox(obs_module_text("ReEncoderConfig"), this);
  auto *configLayout = new QVBoxLayout(ConfigGroup);
  ConfigLabel = new QLabel(this);
  ConfigLabel->setWordWrap(true);
  ConfigLabel->setText(obs_module_text("ReEncoderNoConfig"));
  auto *refreshBtn = new QPushButton(obs_module_text("ReEncoderRefresh"), this);
  auto *configBtnLayout = new QHBoxLayout;
  configBtnLayout->addStretch();
  configBtnLayout->addWidget(refreshBtn);
  configLayout->addWidget(ConfigLabel);
  configLayout->addLayout(configBtnLayout);
  mainLayout->addWidget(ConfigGroup);

  // Start/Stop
  auto *ctrlLayout = new QHBoxLayout;
  StartStopBtn = new QPushButton(obs_module_text("ReEncoderStart"), this);
  StatusLabel = new QLabel(this);
  StatusLabel->setText(obs_module_text("ReEncoderReady"));
  ctrlLayout->addWidget(StartStopBtn);
  ctrlLayout->addWidget(StatusLabel);
  ctrlLayout->addStretch();
  mainLayout->addLayout(ctrlLayout);

  // Progress
  ProgressBar = new QProgressBar(this);
  ProgressBar->setRange(0, 100);
  ProgressBar->setValue(0);
  mainLayout->addWidget(ProgressBar);

  // Log
  LogOutput = new QTextEdit(this);
  LogOutput->setReadOnly(true);
  LogOutput->setMaximumHeight(150);
  mainLayout->addWidget(LogOutput);

  // Connections
  connect(browseInputBtn, &QPushButton::clicked, this, &ReEncodeDialog::OnBrowseInput);
  connect(browseOutputBtn, &QPushButton::clicked, this, &ReEncodeDialog::OnBrowseOutput);
  connect(refreshBtn, &QPushButton::clicked, this, &ReEncodeDialog::OnRefreshConfig);
  connect(StartStopBtn, &QPushButton::clicked, this, &ReEncodeDialog::OnStartStop);

  // Load config
  PopulateEncoderConfig();
}

ReEncodeDialog::~ReEncodeDialog()
{
  StopEncoding();
  obs_data_release(m_EncoderSettings);
}

// ============================================================================
// UI helpers
// ============================================================================

void ReEncodeDialog::SetUIEnabled(bool Enabled)
{
  InputPath->setEnabled(Enabled);
  OutputPath->setEnabled(Enabled);
  ConfigGroup->setEnabled(Enabled);
}

void ReEncodeDialog::AppendLog(const QString &Msg)
{
  QMetaObject::invokeMethod(this, [this, Msg]() {
    LogOutput->append(Msg);
  }, Qt::QueuedConnection);
}

void ReEncodeDialog::UpdateProgress(int64_t Current, int64_t Total)
{
  QMetaObject::invokeMethod(this, [this, Current, Total]() {
    if (Total > 0) {
      int pct = static_cast<int>((Current * 100) / Total);
      ProgressBar->setValue(std::min(pct, 100));
    }
  }, Qt::QueuedConnection);
}

void ReEncodeDialog::closeEvent(QCloseEvent *Event)
{
  if (m_Encoding) {
    Event->ignore();
    return;
  }
  QDialog::closeEvent(Event);
}

// ============================================================================
// Config loading
// ============================================================================

void ReEncodeDialog::PopulateEncoderConfig()
{
  if (LoadEncoderConfigFromActive()) {
    QString summary = QString("%1 | %2x%3 | %4/%5 fps")
                          .arg(QString::fromStdString(m_EncoderID))
                          .arg(m_Width)
                          .arg(m_Height)
                          .arg(m_FpsNum)
                          .arg(m_FpsDen);
    ConfigLabel->setText(summary);
    AppendLog(QString("Encoder config: %1").arg(summary));
  } else if (LoadEncoderConfigFromFile()) {
    QString summary = QString("%1 | %2x%3 | %4/%5 fps")
                          .arg(QString::fromStdString(m_EncoderID))
                          .arg(m_Width)
                          .arg(m_Height)
                          .arg(m_FpsNum)
                          .arg(m_FpsDen);
    ConfigLabel->setText(summary);
    AppendLog(QString("Encoder config (from profile): %1").arg(summary));
  } else {
    ConfigLabel->setText(obs_module_text("ReEncoderNoConfig"));
    AppendLog("WARNING: No QSV encoder config found");
  }
}

void ReEncodeDialog::OnRefreshConfig()
{
  AppendLog("Refreshing encoder config...");
  PopulateEncoderConfig();
}

// ============================================================================
// Browse slots
// ============================================================================

void ReEncodeDialog::OnBrowseInput()
{
  QString path = QFileDialog::getOpenFileName(this, obs_module_text("ReEncoderSelectInput"),
                                               QString(), "Video Files (*.mp4 *.mkv *.webm *.ts *.mov *.avi *.flv);;All Files (*)");
  if (!path.isEmpty())
    InputPath->setText(path);
}

void ReEncodeDialog::OnBrowseOutput()
{
  QString path = QFileDialog::getSaveFileName(this, obs_module_text("ReEncoderSelectOutput"),
                                               QString(), "Video Files (*.mp4 *.mkv *.webm *.ts *.mov);;All Files (*)");
  if (!path.isEmpty())
    OutputPath->setText(path);
}

// ============================================================================
// Start / Stop
// ============================================================================

void ReEncodeDialog::OnStartStop()
{
  if (m_Encoding) {
    StopEncoding();
  } else {
    StartEncoding();
  }
}

bool ReEncodeDialog::StartEncoding()
{
  if (m_Encoding)
    return false;

  QString inputPath = InputPath->text().trimmed();
  QString outputPath = OutputPath->text().trimmed();

  if (inputPath.isEmpty() || outputPath.isEmpty()) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         obs_module_text("ReEncoderNoInput"));
    return false;
  }

  if (!QFileInfo::exists(inputPath)) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         obs_module_text("ReEncoderInputNotFound"));
    return false;
  }

  if (m_EncoderID.empty()) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         obs_module_text("ReEncoderNoConfig"));
    return false;
  }

  // 1. Load FFmpeg
  if (!LoadFFmpegAPI(m_FF)) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         "Failed to load FFmpeg");
    return false;
  }
  AppendLog("FFmpeg loaded");

  // 2. Reset context (individual field reset to avoid std::mutex copy issue)
  m_Ctx.in_fmt_ctx = nullptr;
  m_Ctx.video_stream_idx = -1;
  m_Ctx.audio_stream_idx = -1;
  m_Ctx.video_decoder = nullptr;
  m_Ctx.sws_ctx = nullptr;
  m_Ctx.decoded_frame = nullptr;
  m_Ctx.nv12_frame = nullptr;
  m_Ctx.out_fmt_ctx = nullptr;
  m_Ctx.out_video_stream = nullptr;
  m_Ctx.out_audio_stream = nullptr;
  m_Ctx.pkt_queue.clear();
  m_Ctx.encoder_done = false;
  m_Ctx.audio_packets.clear();
  m_Ctx.duration = 0;
  m_Ctx.total_frames = 0;
  m_Ctx.frames_encoded = 0;
  m_Ctx.stop_requested = false;
  m_Ctx.feed_error = false;
  m_Ctx.error_msg.clear();

  // 3. Open input file
  int ret = m_FF.avformat_open_input(&m_Ctx.in_fmt_ctx, inputPath.toUtf8().constData(),
                                      nullptr, nullptr);
  if (ret < 0) {
    AppendLog("ERROR: Cannot open input file");
    return false;
  }

  ret = m_FF.avformat_find_stream_info(m_Ctx.in_fmt_ctx, nullptr);
  if (ret < 0) {
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: Cannot find stream info");
    return false;
  }

  // Find video stream
  const AVCodec *videoDecoder = nullptr;
  m_Ctx.video_stream_idx = av_find_best_stream(m_Ctx.in_fmt_ctx, AVMEDIA_TYPE_VIDEO,
                                                -1, -1, &videoDecoder, 0);
  if (m_Ctx.video_stream_idx < 0) {
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: No video stream found");
    return false;
  }

  AVStream *inVideoStream = m_Ctx.in_fmt_ctx->streams[m_Ctx.video_stream_idx];
  int srcWidth = inVideoStream->codecpar->width;
  int srcHeight = inVideoStream->codecpar->height;
  AppendLog(QString("Input: %1x%2").arg(srcWidth).arg(srcHeight));

  // Get frame rate from input
  if (inVideoStream->avg_frame_rate.num > 0 && inVideoStream->avg_frame_rate.den > 0) {
    m_FpsNum = inVideoStream->avg_frame_rate.num;
    m_FpsDen = inVideoStream->avg_frame_rate.den;
  }

  m_Ctx.total_frames = static_cast<int64_t>(inVideoStream->nb_frames);
  if (m_Ctx.total_frames <= 0 && m_Ctx.in_fmt_ctx->duration > 0) {
    // estimate from duration
    m_Ctx.duration = m_Ctx.in_fmt_ctx->duration;
    double fps = static_cast<double>(m_FpsNum) / m_FpsDen;
    m_Ctx.total_frames = static_cast<int64_t>(m_Ctx.duration / AV_TIME_BASE * fps);
  }

  // 4. Open video decoder
  m_Ctx.video_decoder = m_FF.avcodec_alloc_context3(videoDecoder);
  if (!m_Ctx.video_decoder) {
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: Cannot allocate decoder");
    return false;
  }

  ret = m_FF.avcodec_parameters_to_context(m_Ctx.video_decoder,
                                            inVideoStream->codecpar);
  if (ret < 0) {
    m_FF.avcodec_free_context(&m_Ctx.video_decoder);
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: Cannot copy codec params");
    return false;
  }

  ret = m_FF.avcodec_open2(m_Ctx.video_decoder, videoDecoder, nullptr);
  if (ret < 0) {
    m_FF.avcodec_free_context(&m_Ctx.video_decoder);
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: Cannot open decoder");
    return false;
  }

  // 5. Find audio stream (optional)
  m_Ctx.audio_stream_idx = av_find_best_stream(m_Ctx.in_fmt_ctx, AVMEDIA_TYPE_AUDIO,
                                                -1, -1, nullptr, 0);
  if (m_Ctx.audio_stream_idx >= 0) {
    AppendLog(QString("Audio stream found at index %1").arg(m_Ctx.audio_stream_idx));
  } else {
    AppendLog("No audio stream (video-only output)");
  }

  // 6. Allocate frames
  m_Ctx.decoded_frame = m_FF.av_frame_alloc();
  m_Ctx.nv12_frame = m_FF.av_frame_alloc();
  if (!m_Ctx.decoded_frame || !m_Ctx.nv12_frame) {
    AppendLog("ERROR: Cannot allocate frames");
    return false;
  }

  // 7. Create swscale context for NV12 conversion
  m_Ctx.sws_ctx = m_FF.sws_getContext(srcWidth, srcHeight, m_Ctx.video_decoder->pix_fmt,
                                       srcWidth, srcHeight, AV_PIX_FMT_NV12,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!m_Ctx.sws_ctx) {
    AppendLog("ERROR: Cannot create swscale context");
    return false;
  }

  // 8. Create OBS video output
  video_output_info vi = {};
  vi.name = "qsv-reencode-video";
  vi.format = VIDEO_FORMAT_NV12;
  vi.width = static_cast<uint32_t>(srcWidth);
  vi.height = static_cast<uint32_t>(srcHeight);
  vi.fps_num = static_cast<uint32_t>(m_FpsNum);
  vi.fps_den = static_cast<uint32_t>(m_FpsDen);
  vi.cache_size = 16;
  vi.colorspace = VIDEO_CS_709;
  vi.range = VIDEO_RANGE_PARTIAL;

  ret = video_output_open(&m_Video, &vi);
  if (ret != VIDEO_OUTPUT_SUCCESS) {
    AppendLog("ERROR: Cannot create video output");
    return false;
  }

  // 9. Create OBS encoder
  obs_data_t *encSettings = obs_data_create();
  if (m_EncoderSettings) {
    obs_data_apply(encSettings, m_EncoderSettings);
  }
  // override resolution to match input
  obs_data_set_int(encSettings, "width", srcWidth);
  obs_data_set_int(encSettings, "height", srcHeight);

  m_Encoder = obs_video_encoder_create(m_EncoderID.c_str(), "qsv-reencode-encoder",
                                       encSettings, nullptr);
  obs_data_release(encSettings);

  if (!m_Encoder) {
    video_output_close(m_Video);
    m_Video = nullptr;
    AppendLog(QString("ERROR: Cannot create encoder '%1'").arg(QString::fromStdString(m_EncoderID)));
    return false;
  }

  obs_encoder_set_video(m_Encoder, m_Video);

  // 10. Create output (dialog ptr is passed via g_PendingOutputDialog to
  // reencode_output_create, since obs_output_t is opaque to plugins)
  g_PendingOutputDialog = this;
  m_Output = obs_output_create(REENCODE_OUTPUT_ID, "qsv-reencode-output", nullptr, nullptr);
  if (!m_Output) {
    g_PendingOutputDialog = nullptr;
    obs_encoder_release(m_Encoder);
    m_Encoder = nullptr;
    video_output_close(m_Video);
    m_Video = nullptr;
    AppendLog("ERROR: Cannot create output");
    return false;
  }

  obs_output_set_video_encoder(m_Output, m_Encoder);

  // 11. Create output file (FFmpeg muxer)
  m_OutputPathBytes = outputPath.toUtf8();
  const char *outPath = m_OutputPathBytes.constData();
  ret = m_FF.avformat_alloc_output_context2(&m_Ctx.out_fmt_ctx, nullptr, nullptr,
                                             outPath);
  if (ret < 0 || !m_Ctx.out_fmt_ctx) {
    AppendLog("ERROR: Cannot create output context");
    return false;
  }

  // Video stream
  enum AVCodecID outCodecId = EncoderIDToAVCodecID(m_EncoderID);
  m_Ctx.out_video_stream = m_FF.avformat_new_stream(m_Ctx.out_fmt_ctx, nullptr);
  if (!m_Ctx.out_video_stream) {
    AppendLog("ERROR: Cannot create output video stream");
    return false;
  }
  m_Ctx.out_video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  m_Ctx.out_video_stream->codecpar->codec_id = outCodecId;
  m_Ctx.out_video_stream->codecpar->width = srcWidth;
  m_Ctx.out_video_stream->codecpar->height = srcHeight;
  // timebase = 1/fps for simplicity; PTS will be rescaled from nanosec
  m_Ctx.out_video_stream->time_base = {m_FpsDen, m_FpsNum};
  // set avg_frame_rate for the stream
  m_Ctx.out_video_stream->avg_frame_rate = {m_FpsNum, m_FpsDen};

  // Audio stream (copy from input)
  if (m_Ctx.audio_stream_idx >= 0) {
    AVStream *inAudioStream = m_Ctx.in_fmt_ctx->streams[m_Ctx.audio_stream_idx];
    m_Ctx.out_audio_stream = m_FF.avformat_new_stream(m_Ctx.out_fmt_ctx, nullptr);
    if (m_Ctx.out_audio_stream) {
      m_FF.avcodec_parameters_copy(m_Ctx.out_audio_stream->codecpar,
                                    inAudioStream->codecpar);
      m_Ctx.out_audio_stream->time_base = inAudioStream->time_base;
    }
  }

  // Open output file
  if (!(m_Ctx.out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = m_FF.avio_open(&m_Ctx.out_fmt_ctx->pb, outPath, AVIO_FLAG_WRITE);
    if (ret < 0) {
      AppendLog("ERROR: Cannot open output file");
      return false;
    }
  }

  // Write header
  ret = m_FF.avformat_write_header(m_Ctx.out_fmt_ctx, nullptr);
  if (ret < 0) {
    AppendLog("ERROR: Cannot write header");
    return false;
  }

  // 12. Start OBS output (this internally initializes encoder, starts encoding,
  //     and begins data capture)
  if (!obs_output_start(m_Output)) {
    AppendLog("ERROR: obs_output_start failed");
    return false;
  }

  AppendLog(QString("Encoding started: %1 -> %2").arg(inputPath, outputPath));

  // 13. Start feed thread
  m_Encoding = true;
  SetUIEnabled(false);
  StartStopBtn->setText(obs_module_text("ReEncoderStop"));
  StatusLabel->setText(obs_module_text("ReEncoderStarting"));

  m_FeedThread = std::thread(&ReEncodeDialog::FeedThreadMain, this);

  return true;
}

void ReEncodeDialog::StopEncoding()
{
  if (!m_Encoding)
    return;

  AppendLog("Stopping...");
  m_Ctx.stop_requested = true;

  // signal feed thread to wake up
  m_Ctx.pkt_cv.notify_all();

  if (m_FeedThread.joinable()) {
    m_FeedThread.join();
  }

  // stop OBS output (stops encoder, disconnects from video output)
  if (m_Output) {
    obs_output_stop(m_Output);
    obs_output_release(m_Output);
    m_Output = nullptr;
  }

  if (m_Encoder) {
    obs_encoder_release(m_Encoder);
    m_Encoder = nullptr;
  }

  if (m_Video) {
    video_output_stop(m_Video);
    video_output_close(m_Video);
    m_Video = nullptr;
  }

  m_Encoding = false;
  SetUIEnabled(true);
  StartStopBtn->setText(obs_module_text("ReEncoderStart"));
  StatusLabel->setText(obs_module_text("ReEncoderReady"));
  ProgressBar->setValue(0);
  AppendLog("Encoding stopped");
}

// ============================================================================
// Feed thread — reads input, feeds video, writes audio, muxes everything
// ============================================================================

void ReEncodeDialog::FeedThreadMain()
{
  auto &ff = m_FF;
  auto &ctx = m_Ctx;
  bool success = true;

  try {
    AVPacket *inPkt = ff.av_packet_alloc();
    if (!inPkt) {
      AppendLog("ERROR: Cannot allocate input packet");
      return;
    }

    while (!ctx.stop_requested) {
      // Read next packet from input
      int ret = ff.av_read_frame(ctx.in_fmt_ctx, inPkt);
      if (ret < 0) {
        if (ret == AVERROR_EOF) {
          break; // done
        }
        AppendLog(QString("ERROR: av_read_frame failed: %1").arg(ret));
        success = false;
        break;
      }

      if (inPkt->stream_index == ctx.video_stream_idx) {
        // --- Video packet ---

        // Send to decoder
        ret = ff.avcodec_send_packet(ctx.video_decoder, inPkt);
        ff.av_packet_unref(inPkt);

        if (ret < 0) {
          AppendLog(QString("ERROR: avcodec_send_packet failed: %1").arg(ret));
          success = false;
          break;
        }

        // Receive decoded frame
        ret = ff.avcodec_receive_frame(ctx.video_decoder, ctx.decoded_frame);
        if (ret == AVERROR(EAGAIN)) {
          continue; // need more packets
        }
        if (ret < 0) {
          AppendLog(QString("ERROR: avcodec_receive_frame failed: %1").arg(ret));
          success = false;
          break;
        }

        // Convert to NV12
        ctx.nv12_frame->format = AV_PIX_FMT_NV12;
        ctx.nv12_frame->width = ctx.decoded_frame->width;
        ctx.nv12_frame->height = ctx.decoded_frame->height;
        ret = ff.av_image_get_buffer_size(AV_PIX_FMT_NV12,
                                           ctx.decoded_frame->width,
                                           ctx.decoded_frame->height, 1);
        // allocate NV12 buffer (reuse if already allocated)
        static thread_local std::vector<uint8_t> nv12_buf;
        nv12_buf.resize(ret);
        ff.av_image_fill_arrays(ctx.nv12_frame->data, ctx.nv12_frame->linesize,
                                 nv12_buf.data(), AV_PIX_FMT_NV12,
                                 ctx.decoded_frame->width,
                                 ctx.decoded_frame->height, 1);

        ff.sws_scale(ctx.sws_ctx,
                      ctx.decoded_frame->data, ctx.decoded_frame->linesize,
                      0, ctx.decoded_frame->height,
                      ctx.nv12_frame->data, ctx.nv12_frame->linesize);

        // Feed NV12 frame to OBS video pipeline
        video_frame vf = {};
        for (int i = 0; i < MAX_AV_PLANES; i++) {
          vf.data[i] = ctx.nv12_frame->data[i];
          vf.linesize[i] = static_cast<uint32_t>(ctx.nv12_frame->linesize[i]);
        }

        // Use decoded frame PTS as timestamp (in decoder's timebase → convert to
        // nanosec for OBS)
        int64_t ptsNs = ctx.decoded_frame->pts;
        if (ptsNs == AV_NOPTS_VALUE)
          ptsNs = ctx.frames_encoded.load();

        // rescale from decoder timebase to nanoseconds
        AVRational decTb = ctx.in_fmt_ctx->streams[ctx.video_stream_idx]->time_base;
        ptsNs = av_rescale_q(ptsNs, decTb, {1, 1000000000});

        video_output_lock_frame(m_Video, &vf, 1, ptsNs);
        video_output_unlock_frame(m_Video);

        // Wait for encoded packet to appear in queue
        ReEncodeCtx::Packet encPkt;
        {
          std::unique_lock lock(ctx.pkt_mutex);
          ctx.pkt_cv.wait(lock, [&ctx] {
            return !ctx.pkt_queue.empty() || ctx.stop_requested || ctx.encoder_done;
          });

          if (ctx.stop_requested)
            break;

          if (ctx.pkt_queue.empty()) {
            // encoder_done without packet — should not happen normally
            continue;
          }

          encPkt = std::move(ctx.pkt_queue.front());
          ctx.pkt_queue.erase(ctx.pkt_queue.begin());
        }

        // Write encoded video packet to muxer
        AVPacket *outPkt = ff.av_packet_alloc();
        outPkt->data = encPkt.data.data();
        outPkt->size = static_cast<int>(encPkt.data.size());
        outPkt->stream_index = ctx.out_video_stream->index;
        // rescale PTS from nanoseconds to output timebase
        outPkt->pts = av_rescale_q(encPkt.pts, {1, 1000000000},
                                    ctx.out_video_stream->time_base);
        outPkt->dts = av_rescale_q(encPkt.dts, {1, 1000000000},
                                    ctx.out_video_stream->time_base);
        // encoder_packet has no duration field; output time_base is
        // {fps_den, fps_num} so 1 = one frame duration
        outPkt->duration = 1;
        if (encPkt.keyframe)
          outPkt->flags |= AV_PKT_FLAG_KEY;

        ret = ff.av_interleaved_write_frame(ctx.out_fmt_ctx, outPkt);
        ff.av_packet_free(&outPkt);

        if (ret < 0) {
          AppendLog(QString("ERROR: av_interleaved_write_frame (video) failed: %1").arg(ret));
          success = false;
          break;
        }

        // Write any buffered audio packets with PTS <= this video frame's PTS
        // (helps with interleaving)
        if (ctx.out_audio_stream && !ctx.audio_packets.empty()) {
          int64_t videoPts = encPkt.pts; // in nanosec
          auto it = ctx.audio_packets.begin();
          while (it != ctx.audio_packets.end()) {
            AVPacket *audioPkt = *it;
            int64_t audioPts = audioPkt->pts;
            // rescale audio PTS to nanoseconds for comparison
            AVRational audioTb = ctx.out_audio_stream->time_base;
            int64_t audioPtsNs = av_rescale_q(audioPts, audioTb, {1, 1000000000});

            if (audioPtsNs <= videoPts) {
              audioPkt->stream_index = ctx.out_audio_stream->index;
              ret = ff.av_interleaved_write_frame(ctx.out_fmt_ctx, audioPkt);
              if (ret < 0) {
                AppendLog(QString("ERROR: av_interleaved_write_frame (audio) failed: %1").arg(ret));
                success = false;
                break;
              }
              ff.av_packet_free(&audioPkt);
              it = ctx.audio_packets.erase(it);
            } else {
              break;
            }
          }
          if (!success)
            break;
        }

        ctx.frames_encoded++;
        UpdateProgress(ctx.frames_encoded, ctx.total_frames);

      } else if (inPkt->stream_index == ctx.audio_stream_idx) {
        // --- Audio packet: buffer for later writing ---
        if (ctx.out_audio_stream) {
          AVPacket *audioPkt = ff.av_packet_alloc();
          av_packet_move_ref(audioPkt, inPkt);
          ctx.audio_packets.push_back(audioPkt);
        } else {
          ff.av_packet_unref(inPkt);
        }
      } else {
        ff.av_packet_unref(inPkt);
      }
    }

    // Write remaining audio packets
    if (ctx.out_audio_stream && !ctx.audio_packets.empty()) {
      AppendLog(QString("Writing %1 remaining audio packets...").arg(ctx.audio_packets.size()));
      for (auto *audioPkt : ctx.audio_packets) {
        audioPkt->stream_index = ctx.out_audio_stream->index;
        ff.av_interleaved_write_frame(ctx.out_fmt_ctx, audioPkt);
        ff.av_packet_free(&audioPkt);
      }
      ctx.audio_packets.clear();
    }

    // Write trailer
    if (ctx.out_fmt_ctx && success) {
      ff.av_write_trailer(ctx.out_fmt_ctx);
      AppendLog("Trailer written");
    }

    ff.av_packet_free(&inPkt);

  } catch (const std::exception &e) {
    AppendLog(QString("EXCEPTION: %1").arg(e.what()));
    success = false;
  } catch (...) {
    AppendLog("UNKNOWN EXCEPTION in feed thread");
    success = false;
  }

  // Cleanup FFmpeg resources
  if (ctx.out_fmt_ctx) {
    if (!(ctx.out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
      ff.avio_closep(&ctx.out_fmt_ctx->pb);
    }
    ff.avformat_free_context(ctx.out_fmt_ctx);
    ctx.out_fmt_ctx = nullptr;
  }

  if (ctx.sws_ctx) {
    ff.sws_freeContext(ctx.sws_ctx);
    ctx.sws_ctx = nullptr;
  }

  if (ctx.video_decoder) {
    ff.avcodec_free_context(&ctx.video_decoder);
  }

  if (ctx.in_fmt_ctx) {
    ff.avformat_close_input(&ctx.in_fmt_ctx);
  }

  if (ctx.decoded_frame) {
    ff.av_frame_free(&ctx.decoded_frame);
  }

  if (ctx.nv12_frame) {
    ff.av_frame_free(&ctx.nv12_frame);
  }

  // cleanup buffered audio packets
  for (auto *p : ctx.audio_packets) {
    ff.av_packet_free(&p);
  }
  ctx.audio_packets.clear();

  // Signal output to stop
  {
    std::lock_guard lock(ctx.pkt_mutex);
    ctx.encoder_done = true;
  }

  AppendLog(success ? "Encoding completed" : "Encoding failed");
  UpdateProgress(ctx.total_frames, ctx.total_frames);

  // Stop encoding on the main thread
  QMetaObject::invokeMethod(this, [this]() {
    if (m_Encoding) {
      StopEncoding();
    }
  }, Qt::QueuedConnection);
}

// ============================================================================
// Toolbar registration
// ============================================================================

static ReEncodeDialog *g_ActiveReEncodeDialog = nullptr;

static void OnReEncoderFrontendEvent(obs_frontend_event Event, void *)
{
  if (Event == OBS_FRONTEND_EVENT_PROFILE_CHANGED ||
      Event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
    if (g_ActiveReEncodeDialog) {
      QMetaObject::invokeMethod(g_ActiveReEncodeDialog, [dialog = g_ActiveReEncodeDialog]() {
        dialog->PopulateEncoderConfig();
      }, Qt::QueuedConnection);
    }
  }
}

static void OpenReEncoder(void *)
{
  try {
    if (g_ActiveReEncodeDialog) {
      g_ActiveReEncodeDialog->raise();
      g_ActiveReEncodeDialog->show();
      return;
    }

    auto *dialog = new ReEncodeDialog(nullptr);
    g_ActiveReEncodeDialog = dialog;
    QObject::connect(dialog, &QDialog::destroyed, []() {
      g_ActiveReEncodeDialog = nullptr;
    });
    dialog->show();
  } catch (const std::exception &e) {
    blog(LOG_ERROR, "[QSV VPL] OpenReEncoder: %s", e.what());
  } catch (...) {
    blog(LOG_ERROR, "[QSV VPL] OpenReEncoder: unknown exception");
  }
}

void RegisterReEncoder()
{
  obs_register_output(&reencode_output_info);
  obs_frontend_add_tools_menu_item(obs_module_text("ReEncoder"),
                                    OpenReEncoder, nullptr);
  obs_frontend_add_event_callback(OnReEncoderFrontendEvent, nullptr);
}