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
#include <util/threading.h>
#include <cstdio>
#include <windows.h>

using namespace std::chrono_literals;

// Debug helper: writes to debugger output (cheap — no-op without a debugger).
// The old version opened/flushed/closed a log file on EVERY call (per-frame
// disk I/O on the feed thread).  Define REENCODE_DEBUG_LOG_FILE to re-enable.
static void dbglog(const char *fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  OutputDebugStringA(buf);
  OutputDebugStringA("\n");
#ifdef REENCODE_DEBUG_LOG_FILE
  FILE *f = fopen("h:\\qsv_reencode_debug.log", "a");
  if (f) {
    fprintf(f, "%s\n", buf);
    fflush(f);
    fclose(f);
  }
#endif
}

// ============================================================================
// Output ID for our custom qsv re-encode output
// ============================================================================
static const char *const REENCODE_OUTPUT_ID = "qsv_reencode_output";

// Per-output context — passed to output callbacks via void *data
struct reencode_output_ctx {
  obs_output_t *output;
  ReEncodeDialog *dialog;

  pthread_t stop_thread;
  bool stop_thread_active = false;
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
  ok = ok && ResolveFunc(avutil, "av_mallocz", ff.av_mallocz);
  ok = ok && ResolveFunc(avutil, "av_frame_alloc", ff.av_frame_alloc);
  ok = ok && ResolveFunc(avutil, "av_frame_free", ff.av_frame_free);
  ok = ok && ResolveFunc(avutil, "av_image_get_buffer_size", ff.av_image_get_buffer_size);
  ok = ok && ResolveFunc(avutil, "av_image_fill_arrays", ff.av_image_fill_arrays);
  ok = ok && ResolveFunc(avutil, "av_rescale_q", ff.av_rescale_q);

  // av_packet_alloc/free may be in avutil or avcodec (OBS layout varies)
  ff.av_packet_alloc = reinterpret_cast<decltype(ff.av_packet_alloc)>(
      GetProcAddress(avutil, "av_packet_alloc"));
  ff.av_packet_free = reinterpret_cast<decltype(ff.av_packet_free)>(
      GetProcAddress(avutil, "av_packet_free"));

  // avcodec
  ok = ok && ResolveFunc(avcodec, "avcodec_find_decoder", ff.avcodec_find_decoder);
  ok = ok && ResolveFunc(avcodec, "avcodec_find_decoder_by_name", ff.avcodec_find_decoder_by_name);
  ok = ok && ResolveFunc(avcodec, "avcodec_alloc_context3", ff.avcodec_alloc_context3);
  ok = ok && ResolveFunc(avcodec, "avcodec_parameters_to_context", ff.avcodec_parameters_to_context);
  ok = ok && ResolveFunc(avcodec, "avcodec_open2", ff.avcodec_open2);
  ok = ok && ResolveFunc(avcodec, "avcodec_send_packet", ff.avcodec_send_packet);
  ok = ok && ResolveFunc(avcodec, "avcodec_receive_frame", ff.avcodec_receive_frame);
  ok = ok && ResolveFunc(avcodec, "avcodec_free_context", ff.avcodec_free_context);
  ok = ok && ResolveFunc(avcodec, "av_packet_unref", ff.av_packet_unref);
  ok = ok && ResolveFunc(avcodec, "av_packet_move_ref", ff.av_packet_move_ref);
  ok = ok && ResolveFunc(avcodec, "avcodec_parameters_copy", ff.avcodec_parameters_copy);

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

  // av_packet_move_ref from avformat as fallback (FFmpeg layout varies)
  if (!ff.av_packet_move_ref) {
    ff.av_packet_move_ref = reinterpret_cast<decltype(ff.av_packet_move_ref)>(
        GetProcAddress(avformat, "av_packet_move_ref"));
  }
  if (!ff.av_packet_move_ref) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot resolve av_packet_move_ref");
    return false;
  }

  // av_packet_unref from avformat as fallback (FFmpeg layout varies)
  if (!ff.av_packet_unref) {
    ff.av_packet_unref = reinterpret_cast<decltype(ff.av_packet_unref)>(
        GetProcAddress(avformat, "av_packet_unref"));
  }
  if (!ff.av_packet_unref) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot resolve av_packet_unref");
    return false;
  }

  // avcodec_parameters_copy from avformat as fallback (FFmpeg layout varies)
  if (!ff.avcodec_parameters_copy) {
    ff.avcodec_parameters_copy = reinterpret_cast<decltype(ff.avcodec_parameters_copy)>(
        GetProcAddress(avformat, "avcodec_parameters_copy"));
  }
  if (!ff.avcodec_parameters_copy) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot resolve avcodec_parameters_copy");
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
  ok = ok && ResolveFunc(avformat, "av_find_best_stream", ff.av_find_best_stream);

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

// "stop" signal: the output has fully ended data capture and the video
// encoder has been shut down, so every remaining packet is already in the
// queue.  This is the point where the feed thread may collect the tail
// packets and finalize.
static void reencode_output_stopped(void *data, calldata_t *)
{
  auto *ctx = static_cast<reencode_output_ctx *>(data);
  if (ctx->dialog) {
    auto &rc = ctx->dialog->m_Ctx;
    rc.encoder_flushed = true;
    rc.pkt_cv.notify_all();
  }
}

static void *reencode_output_create(obs_data_t *, obs_output_t *output)
{
  auto *ctx = new reencode_output_ctx;
  ctx->output = output;
  ctx->dialog = g_PendingOutputDialog;
  g_PendingOutputDialog = nullptr;

  // The "stop" signal fires after the encoders have been fully stopped, so
  // it is the safe point to tell the feed thread that the final packets
  // are in its queue.  obs_output_stop() itself is asynchronous — treating
  // it as "flush done" loses the tail packets of the B-frame pipeline.
  signal_handler_t *sh = obs_output_get_signal_handler(output);
  if (sh)
    signal_handler_connect(sh, "stop", reencode_output_stopped, ctx);
  return ctx;
}

static void reencode_output_destroy(void *data)
{
  auto *ctx = static_cast<reencode_output_ctx *>(data);
  signal_handler_t *sh = obs_output_get_signal_handler(ctx->output);
  if (sh)
    signal_handler_disconnect(sh, "stop", reencode_output_stopped, ctx);
  if (ctx->stop_thread_active)
    pthread_join(ctx->stop_thread, nullptr);
  delete ctx;
}

static bool reencode_output_start(void *data)
{
  auto *ctx = static_cast<reencode_output_ctx *>(data);

  dbglog("[QSV VPL ReEncoder] reencode_output_start: checking can_begin_data_capture");
  if (!obs_output_can_begin_data_capture(ctx->output, 0)) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] obs_output_can_begin_data_capture failed");
    return false;
  }

  dbglog("[QSV VPL ReEncoder] reencode_output_start: initializing encoders");
  if (!obs_output_initialize_encoders(ctx->output, 0)) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] obs_output_initialize_encoders failed");
    return false;
  }

  if (ctx->stop_thread_active)
    pthread_join(ctx->stop_thread, nullptr);

  dbglog("[QSV VPL ReEncoder] reencode_output_start: beginning data capture");
  obs_output_begin_data_capture(ctx->output, 0);
  dbglog("[QSV VPL ReEncoder] reencode_output_start: data capture started");
  return true;
}

static void *reencode_stop_thread(void *data)
{
  auto *ctx = static_cast<reencode_output_ctx *>(data);
  // Hold a reference while calling end_data_capture: StopEncoding() may
  // obs_output_release() the last reference and destroy the output before
  // this thread runs, which would be a use-after-free.
  obs_output_t *ref = obs_output_get_ref(ctx->output);
  obs_output_end_data_capture(ctx->output);
  obs_output_release(ref);
  ctx->stop_thread_active = false;
  return nullptr;
}

static void reencode_output_stop(void *data, uint64_t)
{
  auto *ctx = static_cast<reencode_output_ctx *>(data);

  // signal feed thread to stop
  if (ctx->dialog) {
    auto &rc = ctx->dialog->m_Ctx;
    rc.encoder_done = true;
    rc.pkt_cv.notify_all();
  }

  // end data capture in a separate thread (may block waiting for encoder)
  ctx->stop_thread_active = pthread_create(&ctx->stop_thread, nullptr,
                                           reencode_stop_thread, data) == 0;
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
    // obs_encoder_get_settings() already returns an addref'd reference; the
    // extra addref here leaked one reference on every call/refresh.
    if (m_EncoderSettings)
      obs_data_release(m_EncoderSettings);
    m_EncoderSettings = obs_encoder_get_settings(ctx->EncoderData);

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

  blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded config from: %s %dx%d %d/%d fps",
       m_EncoderID.c_str(), m_Width, m_Height, m_FpsNum, m_FpsDen);

  // Load encoder settings from recordEncoder.json in the profile directory.
  // OBS stores encoder settings as separate JSON files, not in the INI config.
  char *profilePath = obs_frontend_get_current_profile_path();
  if (profilePath) {
    char jsonPath[512];
    snprintf(jsonPath, sizeof(jsonPath), "%s/recordEncoder.json", profilePath);
    bfree(profilePath);

    char *jsonData = os_quick_read_utf8_file(jsonPath);
    if (jsonData) {
      obs_data_t *settings = obs_data_create_from_json(jsonData);
      bfree(jsonData);
      if (settings) {
        if (m_EncoderSettings)
          obs_data_release(m_EncoderSettings);
        m_EncoderSettings = settings;
        blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded encoder settings from %s", jsonPath);
      }
    } else {
      blog(LOG_WARNING, "[QSV VPL ReEncoder] Failed to read %s", jsonPath);
    }
  } else {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] Cannot get profile path");
  }

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
  // Throttle GUI updates.  Each call queues a lambda onto the UI thread;
  // doing that per frame at high encode speeds is pure allocation overhead.
  static thread_local int64_t lastReported = -1;
  static thread_local int64_t lastCount = 0;
  static thread_local bool hasLast = false;
  static thread_local std::chrono::steady_clock::time_point lastTime{};
  constexpr int64_t REPORT_STEP = 15; // frames between UI updates
  // New encoding run: reset the throttle state (Current restarts from 0)
  if (Current < lastReported) {
    lastReported = -1;
    hasLast = false;
  }
  if (Total > 0 && Current < Total && (Current - lastReported) < REPORT_STEP)
    return;
  lastReported = Current;

  // Instantaneous encode speed (frames/s) over the reporting window
  double fps = 0.0;
  auto now = std::chrono::steady_clock::now();
  if (hasLast) {
    double dt = std::chrono::duration<double>(now - lastTime).count();
    if (dt > 0.0)
      fps = static_cast<double>(Current - lastCount) / dt;
  }
  lastTime = now;
  lastCount = Current;
  hasLast = true;

  QMetaObject::invokeMethod(this, [this, Current, Total, fps]() {
    if (Total > 0) {
      int pct = static_cast<int>((Current * 100) / Total);
      ProgressBar->setValue(std::min(pct, 100));
      // %p% = percentage placeholder built into QProgressBar
      ProgressBar->setFormat(QString("%p%  %1 fps").arg(fps, 0, 'f', 1));
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
  dbglog("[QSV VPL ReEncoder] DEBUG: LoadFFmpegAPI done, about to reset context");
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
  m_Ctx.header_written = false;
  m_Ctx.audio_packets.clear();
  m_Ctx.duration = 0;
  m_Ctx.total_frames = 0;
  m_Ctx.frames_encoded = 0;
  m_Ctx.frames_fed = 0;
  m_Ctx.stop_requested = false;
  m_Ctx.encoder_flushed = false;
  m_Ctx.feed_error = false;
  m_Ctx.error_msg.clear();

  // 3. Open input file
  dbglog("[QSV VPL ReEncoder] DEBUG: opening input file: %s",
       inputPath.toUtf8().constData());
  int ret = m_FF.avformat_open_input(&m_Ctx.in_fmt_ctx, inputPath.toUtf8().constData(),
                                      nullptr, nullptr);
  if (ret < 0) {
    AppendLog("ERROR: Cannot open input file");
    return false;
  }
  dbglog("[QSV VPL ReEncoder] DEBUG: input file opened OK");

  dbglog("[QSV VPL ReEncoder] DEBUG: finding stream info...");
  ret = m_FF.avformat_find_stream_info(m_Ctx.in_fmt_ctx, nullptr);
  if (ret < 0) {
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: Cannot find stream info");
    return false;
  }
  dbglog("[QSV VPL ReEncoder] DEBUG: stream info found OK");

  // Find video stream
  dbglog("[QSV VPL ReEncoder] DEBUG: finding best video stream...");
  const AVCodec *videoDecoder = nullptr;
  m_Ctx.video_stream_idx = m_FF.av_find_best_stream(m_Ctx.in_fmt_ctx, AVMEDIA_TYPE_VIDEO,
                                                       -1, -1, &videoDecoder, 0);
  if (m_Ctx.video_stream_idx < 0) {
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: No video stream found");
    return false;
  }
  dbglog("[QSV VPL ReEncoder] DEBUG: video stream idx=%d", m_Ctx.video_stream_idx);

  AVStream *inVideoStream = m_Ctx.in_fmt_ctx->streams[m_Ctx.video_stream_idx];

  int srcWidth = inVideoStream->codecpar->width;
  int srcHeight = inVideoStream->codecpar->height;
  AppendLog(QString("Input: %1x%2").arg(srcWidth).arg(srcHeight));

  {
    const int srcBits = inVideoStream->codecpar->bits_per_raw_sample;
    const bool is10bit = (srcBits >= 10 && srcBits <= 14);
    m_TargetPixFmt = is10bit ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
    m_TargetObsFormat = is10bit ? VIDEO_FORMAT_P010 : VIDEO_FORMAT_NV12;
    if (is10bit)
      AppendLog(QString("10-bit source (%1): using P010 pipeline").arg(srcBits));
  }

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

  // 4. Open video decoder — multithreaded software decoding.  FFmpeg API
  //    users default to thread_count=1 (single thread), which starves the
  //    encoder; auto-detect core count and enable frame+slice threading —
  //    decode latency is irrelevant for a file re-encode.
  dbglog("[QSV VPL ReEncoder] DEBUG: opening video decoder...");
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
  m_Ctx.video_decoder->thread_count = 0; // auto = CPU cores
  m_Ctx.video_decoder->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

  dbglog("[QSV VPL ReEncoder] DEBUG: opening codec...");
  ret = m_FF.avcodec_open2(m_Ctx.video_decoder, videoDecoder, nullptr);
  if (ret < 0) {
    m_FF.avcodec_free_context(&m_Ctx.video_decoder);
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
    AppendLog("ERROR: Cannot open decoder");
    return false;
  }
  AppendLog("Decoder: software, multithreaded");
  dbglog("[QSV VPL ReEncoder] DEBUG: decoder opened OK");

  // 5. Find audio stream (optional)
  m_Ctx.audio_stream_idx = m_FF.av_find_best_stream(m_Ctx.in_fmt_ctx, AVMEDIA_TYPE_AUDIO,
                                                       -1, -1, nullptr, 0);
  if (m_Ctx.audio_stream_idx >= 0) {
    AppendLog(QString("Audio stream found at index %1").arg(m_Ctx.audio_stream_idx));
  } else {
    AppendLog("No audio stream (video-only output)");
  }

  // 6. Allocate frames
  dbglog("[QSV VPL ReEncoder] DEBUG: allocating frames...");
  m_Ctx.decoded_frame = m_FF.av_frame_alloc();
  m_Ctx.nv12_frame = m_FF.av_frame_alloc();
  if (!m_Ctx.decoded_frame || !m_Ctx.nv12_frame) {
    AppendLog("ERROR: Cannot allocate frames");
    goto cleanup_failed;
  }
  dbglog("[QSV VPL ReEncoder] DEBUG: frames allocated OK");

  // 7. swscale context is created lazily in FeedThreadMain after first frame
  //    is decoded, using decoded_frame->format (100% accurate). This avoids
  //    the AV_PIX_FMT_NONE issue when avcodec_open2 doesn't set pix_fmt.

  // 8. Create OBS video output
  {
    dbglog("[QSV VPL ReEncoder] DEBUG: creating video output...");
    video_output_info vi = {};
    vi.name = "qsv-reencode-video";
    vi.format = m_TargetObsFormat;
    vi.width = static_cast<uint32_t>(srcWidth);
    vi.height = static_cast<uint32_t>(srcHeight);
    vi.fps_num = static_cast<uint32_t>(m_FpsNum);
    vi.fps_den = static_cast<uint32_t>(m_FpsDen);
    // Deeper cache so the decoder can run ahead of the encoder without
    // the feed thread stalling in video_output_lock_frame.
    vi.cache_size = 32;
    vi.colorspace = VIDEO_CS_709;
    vi.range = VIDEO_RANGE_PARTIAL;

    ret = video_output_open(&m_Video, &vi);
    if (ret != VIDEO_OUTPUT_SUCCESS) {
      AppendLog("ERROR: Cannot create video output");
      goto cleanup_failed;
    }
    dbglog("[QSV VPL ReEncoder] DEBUG: video output created OK");
  }

  // 9. Create OBS encoder
  {
    obs_data_t *encSettings = obs_data_create();
    if (m_EncoderSettings) {
      obs_data_apply(encSettings, m_EncoderSettings);
    }
    // override resolution to match input
    obs_data_set_int(encSettings, "width", srcWidth);
    obs_data_set_int(encSettings, "height", srcHeight);

    blog(LOG_INFO, "[QSV VPL ReEncoder] Creating encoder '%s'...", m_EncoderID.c_str());
    m_Encoder = obs_video_encoder_create(m_EncoderID.c_str(), "qsv-reencode-encoder",
                                         encSettings, nullptr);
    obs_data_release(encSettings);

    if (!m_Encoder) {
      AppendLog(QString("ERROR: Cannot create encoder '%1'").arg(QString::fromStdString(m_EncoderID)));
      goto cleanup_failed;
    }
    blog(LOG_INFO, "[QSV VPL ReEncoder] Encoder created, setting video...");
  }

  obs_encoder_set_video(m_Encoder, m_Video);

  // 10. Create output (dialog ptr is passed via g_PendingOutputDialog to
  // reencode_output_create, since obs_output_t is opaque to plugins)
  g_PendingOutputDialog = this;
  m_Output = obs_output_create(REENCODE_OUTPUT_ID, "qsv-reencode-output", nullptr, nullptr);
  if (!m_Output) {
    g_PendingOutputDialog = nullptr;
    AppendLog("ERROR: Cannot create output");
    goto cleanup_failed;
  }

  obs_output_set_video_encoder(m_Output, m_Encoder);
  blog(LOG_INFO, "[QSV VPL ReEncoder] Output created, encoder attached");

  // 11. Create output file (FFmpeg muxer)
  {
    dbglog("[QSV VPL ReEncoder] DEBUG: creating FFmpeg output context...");
    m_OutputPathBytes = outputPath.toUtf8();
    const char *outPath = m_OutputPathBytes.constData();
    ret = m_FF.avformat_alloc_output_context2(&m_Ctx.out_fmt_ctx, nullptr, nullptr,
                                               outPath);
    if (ret < 0 || !m_Ctx.out_fmt_ctx) {
      AppendLog("ERROR: Cannot create output context");
      goto cleanup_failed;
    }
    dbglog("[QSV VPL ReEncoder] DEBUG: output context created OK");

    // Video stream
    enum AVCodecID outCodecId = EncoderIDToAVCodecID(m_EncoderID);
    m_Ctx.out_video_stream = m_FF.avformat_new_stream(m_Ctx.out_fmt_ctx, nullptr);
    if (!m_Ctx.out_video_stream) {
      AppendLog("ERROR: Cannot create output video stream");
      goto cleanup_failed;
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
    dbglog("[QSV VPL ReEncoder] DEBUG: opening output file...");
    if (!(m_Ctx.out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
      ret = m_FF.avio_open(&m_Ctx.out_fmt_ctx->pb, outPath, AVIO_FLAG_WRITE);
      if (ret < 0) {
        AppendLog("ERROR: Cannot open output file");
        goto cleanup_failed;
      }
    }

    // NOTE: the header is NOT written here.  The MP4 avcC/hvcC box must
    // contain the SPS/PPS, which the QSV encoder only produces together
    // with its first keyframe (well after obs_output_start()).  The feed
    // thread writes the header lazily, right before muxing the first
    // encoded packet, once obs_encoder_get_extra_data() becomes available.
  }

  // 12. Start OBS output (this internally initializes encoder, starts encoding,
  //     and begins data capture)
  dbglog("[QSV VPL ReEncoder] About to start OBS output...");
  if (!obs_output_start(m_Output)) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] obs_output_start failed");
    AppendLog("ERROR: obs_output_start failed");
    goto cleanup_failed;
  }
  dbglog("[QSV VPL ReEncoder] OBS output started successfully");

  AppendLog(QString("Encoding started: %1 -> %2").arg(inputPath, outputPath));

  // 13. Start feed thread
  m_Encoding = true;
  SetUIEnabled(false);
  StartStopBtn->setText(obs_module_text("ReEncoderStop"));
  StatusLabel->setText(obs_module_text("ReEncoderStarting"));

  m_FeedThread = std::thread(&ReEncodeDialog::FeedThreadMain, this);

  return true;

cleanup_failed:
  // Release everything allocated so far — the per-step `return false` paths
  // used to leak whichever resources were created before the failure.
  g_PendingOutputDialog = nullptr;
  if (m_Output) {
    obs_output_release(m_Output);
    m_Output = nullptr;
  }
  if (m_Encoder) {
    obs_encoder_release(m_Encoder);
    m_Encoder = nullptr;
  }
  if (m_Video) {
    video_output_close(m_Video);
    m_Video = nullptr;
  }
  if (m_Ctx.in_fmt_ctx) {
    m_FF.avformat_close_input(&m_Ctx.in_fmt_ctx);
  }
  if (m_Ctx.video_decoder) {
    m_FF.avcodec_free_context(&m_Ctx.video_decoder);
  }
  if (m_Ctx.decoded_frame) {
    m_FF.av_frame_free(&m_Ctx.decoded_frame);
  }
  if (m_Ctx.nv12_frame) {
    m_FF.av_frame_free(&m_Ctx.nv12_frame);
  }
  if (m_Ctx.out_fmt_ctx) {
    if (!(m_Ctx.out_fmt_ctx->oformat->flags & AVFMT_NOFILE) &&
        m_Ctx.out_fmt_ctx->pb) {
      m_FF.avio_closep(&m_Ctx.out_fmt_ctx->pb);
    }
    m_FF.avformat_free_context(m_Ctx.out_fmt_ctx);
    m_Ctx.out_fmt_ctx = nullptr;
  }
  return false;
}

void ReEncodeDialog::StopEncoding()
{
  if (!m_Encoding)
    return;

  // If called from UI (user clicked stop), signal feed thread to stop reading
  bool wasUserStop = !m_Ctx.stop_requested;
  if (wasUserStop) {
    AppendLog("Stopping...");
    m_Ctx.stop_requested = true;
    m_Ctx.pkt_cv.notify_all();
  }

  // Wait for feed thread to finish feeding all frames
  {
    std::unique_lock lock(m_Ctx.pkt_mutex);
    m_Ctx.pkt_cv.wait(lock, [this] { return m_Ctx.encoder_done; });
  }

  // Flush encoder by stopping the output.  obs_output_stop() is async; the
  // output's "stop" signal (see reencode_output_stopped) fires once the
  // encoder has been shut down, and that is what releases the feed thread
  // to write the tail packets and the trailer.
  if (m_Output) {
    obs_output_stop(m_Output);
    obs_output_release(m_Output);
    m_Output = nullptr;
  }

  // Join feed thread (it will now collect remaining packets, write trailer, cleanup)
  if (m_FeedThread.joinable()) {
    m_FeedThread.join();
  }

  // Release remaining resources
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
      // Must signal completion — StopEncoding() waits on encoder_done and
      // would block forever otherwise.
      {
        std::lock_guard lock(ctx.pkt_mutex);
        ctx.encoder_done = true;
      }
      ctx.pkt_cv.notify_all();
      return;
    }

    int ret;

    // Write the output header once, lazily.  The video parameter sets
    // (SPS/PPS for H.264, VPS/SPS/PPS for HEVC) are stored in the MP4
    // avcC/hvcC box, but the QSV encoder strips them from the bitstream
    // and only exposes them via obs_encoder_get_extra_data() after the
    // first keyframe has been encoded.  So the header must be written
    // after the first encoded packet arrives, not in StartEncoding().
    // Thread-safety: the encoder writes ExtraData before pushing packets
    // into pkt_queue (both happen on the encoder thread, in order), so by
    // the time we pop a packet under pkt_mutex the data is stable.
    auto writeHeaderOnce = [&]() -> bool {
      if (ctx.header_written)
        return true;

      uint8_t *extraData = nullptr;
      size_t extraSize = 0;
      if (obs_encoder_get_extra_data(m_Encoder, &extraData, &extraSize) &&
          extraData && extraSize > 0) {
        // av_mallocz: avformat_free_context() releases codecpar->extradata
        // with av_free(), so it must come from the FFmpeg allocator.
        uint8_t *buf = (uint8_t *)ff.av_mallocz(extraSize + AV_INPUT_BUFFER_PADDING_SIZE);
        if (buf) {
          memcpy(buf, extraData, extraSize);
          ctx.out_video_stream->codecpar->extradata = buf;
          ctx.out_video_stream->codecpar->extradata_size = (int)extraSize;
        }
        AppendLog(QString("Got encoder extra data: %1 bytes").arg((int)extraSize));
      } else {
        // VP9 has no parameter sets; anything else cannot be decoded.
        AppendLog("WARNING: encoder provided no extra data (SPS/PPS)");
      }

      int hr = ff.avformat_write_header(ctx.out_fmt_ctx, nullptr);
      if (hr < 0) {
        AppendLog(QString("ERROR: avformat_write_header failed: %1").arg(hr));
        return false;
      }
      ctx.header_written = true;
      dbglog("[QSV VPL ReEncoder] header written lazily, extradata=%d bytes",
             (int)extraSize);
      return true;
    };

    // Mux one encoded video packet.  Encoder PTS/DTS arrive in {1/fps} ticks,
    // but avformat_write_header() lets the MP4 muxer pick a finer stream
    // time_base (typically 1/15360 for 30fps) — so every packet must be
    // rescaled, otherwise all frames land 1 tick apart and the video flashes
    // by in a fraction of a second.  Also flushes buffered audio packets
    // whose PTS is due before this video frame's.
    auto muxVideoPacket = [&](ReEncodeCtx::Packet &encPkt) -> bool {
      if (!writeHeaderOnce())
        return false;

      AVPacket *outPkt = ff.av_packet_alloc();
      if (!outPkt)
        return false;
      outPkt->data = encPkt.data.data();
      outPkt->size = static_cast<int>(encPkt.data.size());
      outPkt->stream_index = ctx.out_video_stream->index;

      AVRational encTb = {m_FpsDen, m_FpsNum};
      AVRational outTb = ctx.out_video_stream->time_base;
      outPkt->pts = ff.av_rescale_q(encPkt.pts, encTb, outTb);
      outPkt->dts = ff.av_rescale_q(encPkt.dts, encTb, outTb);
      outPkt->duration = (int)ff.av_rescale_q(1, encTb, outTb);
      if (encPkt.keyframe)
        outPkt->flags |= AV_PKT_FLAG_KEY;

      int wr = ff.av_interleaved_write_frame(ctx.out_fmt_ctx, outPkt);
      ff.av_packet_free(&outPkt);
      if (wr < 0) {
        AppendLog(QString("ERROR: av_interleaved_write_frame (video) failed: %1").arg(wr));
        return false;
      }

      // Write buffered audio packets with PTS <= this video frame's PTS
      if (ctx.out_audio_stream && !ctx.audio_packets.empty()) {
        int64_t videoPtsNs = ff.av_rescale_q(encPkt.pts, encTb, {1, 1000000000});
        auto it = ctx.audio_packets.begin();
        while (it != ctx.audio_packets.end()) {
          AVPacket *audioPkt = *it;
          int64_t audioPtsNs = ff.av_rescale_q(
              audioPkt->pts, ctx.out_audio_stream->time_base, {1, 1000000000});
          if (audioPtsNs <= videoPtsNs) {
            audioPkt->stream_index = ctx.out_audio_stream->index;
            int awr = ff.av_interleaved_write_frame(ctx.out_fmt_ctx, audioPkt);
            ff.av_packet_free(&audioPkt);
            it = ctx.audio_packets.erase(it);
            if (awr < 0) {
              AppendLog(QString("ERROR: av_interleaved_write_frame (audio) failed: %1").arg(awr));
              return false;
            }
          } else {
            break;
          }
        }
      }
      return true;
    };

    // Helper: process one decoded frame (convert/pass-through
    // → feed OBS → mux).  Defined at loop scope because both the main loop
    // and the EOF decoder-drain loop use it.
    auto processFrame = [&]() -> bool {
          AVFrame *srcF = ctx.decoded_frame;
          AVFrame *workF;
          if ((AVPixelFormat)srcF->format == m_TargetPixFmt) {
            workF = srcF;
          } else {
            if (!ctx.sws_ctx) {
              dbglog("[QSV VPL ReEncoder] Creating sws_context from frame: %dx%d fmt=%d",
                     srcF->width, srcF->height, srcF->format);
              ctx.sws_ctx = ff.sws_getContext(
                  srcF->width, srcF->height, (AVPixelFormat)srcF->format,
                  srcF->width, srcF->height, m_TargetPixFmt,
                  SWS_BILINEAR, nullptr, nullptr, nullptr);
              if (!ctx.sws_ctx) {
                AppendLog("ERROR: Cannot create swscale context");
                return false;
              }
              dbglog("[QSV VPL ReEncoder] sws_context created OK");
            }

            ctx.nv12_frame->format = m_TargetPixFmt;
            ctx.nv12_frame->width = srcF->width;
            ctx.nv12_frame->height = srcF->height;
            int bufSize = ff.av_image_get_buffer_size(m_TargetPixFmt,
                                                       srcF->width,
                                                       srcF->height, 1);
            static thread_local std::vector<uint8_t> cvt_buf;
            cvt_buf.resize(bufSize);
            ff.av_image_fill_arrays(ctx.nv12_frame->data, ctx.nv12_frame->linesize,
                                     cvt_buf.data(), m_TargetPixFmt,
                                     srcF->width, srcF->height, 1);

            ff.sws_scale(ctx.sws_ctx,
                          srcF->data, srcF->linesize,
                          0, srcF->height,
                          ctx.nv12_frame->data, ctx.nv12_frame->linesize);
            workF = ctx.nv12_frame;
          }

          // 3. Feed frame to OBS video pipeline
          int64_t ptsNs = srcF->pts;
          AVRational decTb = ctx.in_fmt_ctx->streams[ctx.video_stream_idx]->time_base;
          if (ptsNs == AV_NOPTS_VALUE)
            // No PTS in source: assume constant frame order, index * frame duration
            ptsNs = ff.av_rescale_q(ctx.frames_encoded.load(),
                                    {m_FpsDen, m_FpsNum}, {1, 1000000000});
          else
            ptsNs = ff.av_rescale_q(ptsNs, decTb, {1, 1000000000});

          video_frame vf = {};
          // Backpressure: keep in-flight frames (= fed - consumed) very low
          // so the video cache always has a free slot.  lock_frame() failure
          // is destructive (the encoder re-encodes the previous frame and
          // the pacing counter is corrupted), so the decoder must be
          // throttled to the encoder's pace — exactly what FFmpeg's bounded
          // encoder queue does.  Speed is then bounded by the encoder, never
          // by frame drops.
          {
            auto stallStart = std::chrono::steady_clock::now();
            bool timedOut = false;
            while (m_Video &&
                   ctx.frames_fed.load() -
                           (int64_t)video_output_get_total_frames(m_Video) >=
                       2) {
              if (ctx.stop_requested)
                return true;
              std::this_thread::sleep_for(1ms);
              if (std::chrono::steady_clock::now() - stallStart > 10s) {
                timedOut = true;
                break;
              }
            }
            if (timedOut)
              dbglog("[QSV VPL ReEncoder] backpressure stall timeout at frame %lld, resuming feed",
                     (long long)ctx.frames_fed.load());
          }
          // Normally succeeds on the first try (see pacing above).  If it
          // still fails, wait for a slot rather than dropping the frame —
          // a dropped frame in an offline re-encode is corruption.
          while (!video_output_lock_frame(m_Video, &vf, 1, ptsNs)) {
            if (ctx.stop_requested)
              return true;
            std::this_thread::sleep_for(2ms);
          }

          for (int i = 0; i < MAX_AV_PLANES; i++) {
            if (vf.data[i] && workF->data[i]) {
              // Copy row-wise but never read past the tightly-packed source:
              // cvt_buf is allocated with align=1 (linesize == width) while
              // OBS's vf.linesize may be larger (aligned) — the old memcpy by
              // vf.linesize could over-read the source buffer.
              const size_t srcLine = (size_t)workF->linesize[i];
              const size_t dstLine = (size_t)vf.linesize[i];
              const size_t rows =
                  (i > 0) ? (size_t)srcF->height / 2
                          : (size_t)srcF->height;
              const size_t copyLine = dstLine < srcLine ? dstLine : srcLine;
              for (size_t row = 0; row < rows; row++)
                memcpy(vf.data[i] + row * dstLine,
                       workF->data[i] + row * srcLine, copyLine);
            }
          }

          video_output_unlock_frame(m_Video);
          ctx.frames_fed++;

          // Process available encoded packets (non-blocking)
          {
            std::unique_lock lock(ctx.pkt_mutex);
            while (!ctx.pkt_queue.empty()) {
              ReEncodeCtx::Packet encPkt = std::move(ctx.pkt_queue.front());
              ctx.pkt_queue.pop_front();
              lock.unlock();

              if (!muxVideoPacket(encPkt))
                return false;

              lock.lock();
            }
          }

          ctx.frames_encoded++;
          UpdateProgress(ctx.frames_encoded, ctx.total_frames);
          return true;
    };

    while (!ctx.stop_requested) {
      // Read next packet from input
      ret = ff.av_read_frame(ctx.in_fmt_ctx, inPkt);
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

        // Send packet to decoder.
        // EAGAIN means decoder output buffer is full — drain it first, then retry.
        ret = ff.avcodec_send_packet(ctx.video_decoder, inPkt);
        if (ret == AVERROR(EAGAIN)) {
          // drain decoder output
          while (true) {
            ret = ff.avcodec_receive_frame(ctx.video_decoder, ctx.decoded_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
              break;
            if (ret < 0) {
              AppendLog(QString("ERROR: avcodec_receive_frame (drain) failed: %1").arg(ret));
              success = false;
              break;
            }
            if (!processFrame()) {
              success = false;
              break;
            }
          }
          if (!success) {
            ff.av_packet_unref(inPkt);
            break;
          }
          // retry sending the same packet
          ret = ff.avcodec_send_packet(ctx.video_decoder, inPkt);
        }
        ff.av_packet_unref(inPkt);

        if (ret < 0 && ret != AVERROR_EOF) {
          AppendLog(QString("ERROR: avcodec_send_packet failed: %1").arg(ret));
          success = false;
          break;
        }

        // Receive all available decoded frames (one packet may produce multiple)
        while (true) {
          ret = ff.avcodec_receive_frame(ctx.video_decoder, ctx.decoded_frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
          if (ret < 0) {
            AppendLog(QString("ERROR: avcodec_receive_frame failed: %1").arg(ret));
            success = false;
            break;
          }
          if (!processFrame()) {
            success = false;
            break;
          }
        }
        if (!success)
          break;

      } else if (inPkt->stream_index == ctx.audio_stream_idx) {
        // --- Audio packet: buffer for later writing ---
        if (ctx.out_audio_stream) {
          AVPacket *audioPkt = ff.av_packet_alloc();
          ff.av_packet_move_ref(audioPkt, inPkt);
          ctx.audio_packets.push_back(audioPkt);
        } else {
          ff.av_packet_unref(inPkt);
        }
      } else {
        ff.av_packet_unref(inPkt);
      }
    }

    // --- Loop ended (EOF or stop_requested) ---

    // Flush the decoder: send null packet, drain remaining frames
    ff.avcodec_send_packet(ctx.video_decoder, nullptr);
    while (true) {
      ret = ff.avcodec_receive_frame(ctx.video_decoder, ctx.decoded_frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0) {
        AppendLog(QString("ERROR: decoder drain failed: %1").arg(ret));
        success = false;
        break;
      }
      if (!processFrame()) {
        success = false;
        break;
      }
    }

    if (m_Encoder) {
      {
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (m_Video &&
               video_output_get_total_frames(m_Video) <
                   (uint64_t)ctx.frames_fed.load() &&
               std::chrono::steady_clock::now() < deadline) {
          if (ctx.stop_requested)
            break;
          std::this_thread::sleep_for(1ms);
        }
      }

      extern std::mutex EncoderDataMapMutex;
      extern std::unordered_map<obs_encoder_t *, plugin_context *> EncoderDataMap;
      plugin_context *pctx = nullptr;
      {
        std::lock_guard lock(EncoderDataMapMutex);
        auto it = EncoderDataMap.find(m_Encoder);
        if (it != EncoderDataMap.end())
          pctx = it->second;
      }
      if (pctx && pctx->EncoderPTR) {
        std::lock_guard lock(pctx->EncoderMutex);
        mfxBitstream *bs = nullptr;
        int drained = 0;
        while (pctx->EncoderPTR->DrainAndRetrieveBitstream(&bs) == MFX_ERR_NONE &&
               bs && bs->DataLength > 0) {
          encoder_packet packet = {};
          bool received = false;
          ParseEncodedPacket(pctx, &packet, bs, &received);
          if (received && packet.size > 0) {
            ReEncodeCtx::Packet p;
            p.data.assign(packet.data, packet.data + packet.size);
            p.pts = packet.pts;
            p.dts = packet.dts;
            p.keyframe = packet.keyframe;
            {
              std::lock_guard lock2(ctx.pkt_mutex);
              ctx.pkt_queue.push_back(std::move(p));
            }
            drained++;
          }
        }
        AppendLog(QString("Encoder drained: recovered %1 trailing frames").arg(drained));
      }
    }

    // Wait for the encoder to actually encode every fed frame before
    // letting StopEncoding() tear the output down: obs_output_stop()
    // disconnects receive_video immediately, and any frames still sitting
    // in the video cache would be silently dropped.
    //
    // Stall detection: frames still buffered inside the driver's lookahead
    // window may never surface as encoded output until shutdown, so waiting
    // for full equality could block for the whole 30s timeout on every run.
    // If encoded_frames stops advancing for 2s, accept the remainder.
    if (!ctx.stop_requested && m_Encoder) {
      auto deadline = std::chrono::steady_clock::now() + 30s;
      auto lastProgress = std::chrono::steady_clock::now();
      uint32_t lastEncoded = obs_encoder_get_encoded_frames(m_Encoder);
      while (obs_encoder_get_encoded_frames(m_Encoder) <
                 (uint32_t)ctx.frames_fed.load() &&
             std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
        uint32_t cur = obs_encoder_get_encoded_frames(m_Encoder);
        if (cur != lastEncoded) {
          lastEncoded = cur;
          lastProgress = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - lastProgress > 2s) {
          break; // stalled — remaining frames live in the lookahead window
        }
      }
      if (obs_encoder_get_encoded_frames(m_Encoder) <
          (uint32_t)ctx.frames_fed.load()) {
        AppendLog(QString("WARNING: encoder did not finish all frames (%1/%2)")
                      .arg((int)obs_encoder_get_encoded_frames(m_Encoder))
                      .arg((int)ctx.frames_fed.load()));
      }
    }

    // Signal that we've finished feeding all frames
    {
      std::lock_guard lock(ctx.pkt_mutex);
      ctx.encoder_done = true;
    }
    ctx.pkt_cv.notify_all();

    // If EOF (not user-requested stop), trigger StopEncoding on main thread
    if (!ctx.stop_requested) {
      QMetaObject::invokeMethod(this, [this]() {
        if (m_Encoding)
          StopEncoding();
      }, Qt::QueuedConnection);
    }

    // Wait for the output's "stopped" signal (encoder fully drained).
    // The timeout is a safety net against a lost signal deadlocking the
    // feed thread (and thus the UI, which joins it).
    {
      std::unique_lock lock(ctx.pkt_mutex);
      if (!ctx.pkt_cv.wait_for(lock, 10s,
                               [&ctx] { return ctx.encoder_flushed.load(); })) {
        AppendLog("WARNING: timed out waiting for encoder flush");
        ctx.encoder_flushed = true;
      }
    }

    // Collect remaining encoded packets flushed by obs_output_stop
    {
      std::unique_lock lock(ctx.pkt_mutex);
      while (!ctx.pkt_queue.empty()) {
        ReEncodeCtx::Packet encPkt = std::move(ctx.pkt_queue.front());
        ctx.pkt_queue.erase(ctx.pkt_queue.begin());
        lock.unlock();

        if (!muxVideoPacket(encPkt))
          success = false;

        lock.lock();
      }
    }

    // Write remaining audio packets
    if (ctx.header_written && ctx.out_audio_stream && !ctx.audio_packets.empty()) {
      AppendLog(QString("Writing %1 remaining audio packets...").arg(ctx.audio_packets.size()));
      for (auto *audioPkt : ctx.audio_packets) {
        audioPkt->stream_index = ctx.out_audio_stream->index;
        ff.av_interleaved_write_frame(ctx.out_fmt_ctx, audioPkt);
        ff.av_packet_free(&audioPkt);
      }
      ctx.audio_packets.clear();
    }

    // Write trailer — only valid if the header was actually written
    if (ctx.out_fmt_ctx && success && ctx.header_written) {
      ff.av_write_trailer(ctx.out_fmt_ctx);
      AppendLog("Trailer written");
    }

    ff.av_packet_free(&inPkt);

  } catch (const std::exception &e) {
    AppendLog(QString("EXCEPTION: %1").arg(e.what()));
    success = false;
    // Signal completion on the exception path too — StopEncoding() waits on
    // encoder_done and would otherwise deadlock (the normal signal below is
    // skipped when an exception jumps straight to cleanup).
    {
      std::lock_guard lock(ctx.pkt_mutex);
      ctx.encoder_done = true;
    }
    ctx.pkt_cv.notify_all();
  } catch (...) {
    AppendLog("UNKNOWN EXCEPTION in feed thread");
    success = false;
    {
      std::lock_guard lock(ctx.pkt_mutex);
      ctx.encoder_done = true;
    }
    ctx.pkt_cv.notify_all();
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

  AppendLog(success ? "Encoding completed" : "Encoding failed");
  UpdateProgress(ctx.total_frames, ctx.total_frames);
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