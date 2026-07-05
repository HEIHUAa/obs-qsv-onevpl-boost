#include "obs-qsv-onevpl-video-reencoder.hpp"
#include "helpers/common_utils.hpp"
#include "obs-qsv-onevpl-encoder.hpp"
#include "obs-qsv-onevpl-plugin-init.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>
#include <fstream>
#include <system_error>

#include <windows.h>

// FFmpeg enum constants (dynamic loading, no headers available)
static constexpr int AVMEDIA_TYPE_VIDEO = 0;
static constexpr int AV_PIX_FMT_NV12 = 23;
static constexpr int SWS_BILINEAR = 2;

// ============================================================================
// FFmpeg dynamic loading — resolves function pointers from OBS's bundled DLLs
// ============================================================================

// Try to load a DLL by trying multiple versioned names
static HMODULE TryLoadDLL(const wchar_t *BaseName, int MinVer, int MaxVer) {
  HMODULE hMod = nullptr;
  for (int v = MaxVer; v >= MinVer; --v) {
    wchar_t Name[64];
    swprintf_s(Name, L"%s-%d.dll", BaseName, v);
    hMod = GetModuleHandleW(Name);
    if (hMod)
      return hMod;
    hMod = LoadLibraryW(Name);
    if (hMod)
      return hMod;
  }
  return nullptr;
}

static bool ResolveFuncs(HMODULE Mod, const char *Names[], void **Funcs,
                         size_t Count) {
  for (size_t i = 0; i < Count; i++) {
    *Funcs = reinterpret_cast<void *>(GetProcAddress(Mod, Names[i]));
    if (!*Funcs) {
      blog(LOG_WARNING, "[QSV VPL ReEncoder] Failed to resolve %s from DLL",
           Names[i]);
      return false;
    }
    Funcs++;
  }
  return true;
}

bool LoadFFmpegDyn(FFmpegFuncs &ff) {
  ZeroMemory(&ff, sizeof(ff));

  // avformat
  HMODULE avformat = TryLoadDLL(L"avformat", 59, 62);
  if (!avformat) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load avformat DLL");
    return false;
  }
  const char *avformat_names[] = {
      "avformat_open_input", "avformat_close_input",
      "avformat_find_stream_info", "av_read_frame"};
  void *avformat_ptrs[] = {
      reinterpret_cast<void **>(&ff.avformat_open_input),
      reinterpret_cast<void **>(&ff.avformat_close_input),
      reinterpret_cast<void **>(&ff.avformat_find_stream_info),
      nullptr /* av_read_frame — resolved separately */};
  if (!ResolveFuncs(avformat, avformat_names, avformat_ptrs, 3))
    return false;

  // Resolve av_read_frame separately (not in the FFmpegFuncs struct, used directly)
  // We'll resolve it via GetProcAddress in the encode thread.

  // avcodec
  HMODULE avcodec = TryLoadDLL(L"avcodec", 59, 62);
  if (!avcodec) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load avcodec DLL");
    return false;
  }
  const char *avcodec_names[] = {
      "avcodec_find_decoder",      "avcodec_alloc_context3",
      "avcodec_parameters_to_context", "avcodec_open2",
      "avcodec_send_packet",       "avcodec_receive_frame",
      "avcodec_free_context"};
  void *avcodec_ptrs[] = {
      reinterpret_cast<void **>(&ff.avcodec_find_decoder),
      reinterpret_cast<void **>(&ff.avcodec_alloc_context3),
      reinterpret_cast<void **>(&ff.avcodec_parameters_to_context),
      reinterpret_cast<void **>(&ff.avcodec_open2),
      reinterpret_cast<void **>(&ff.avcodec_send_packet),
      reinterpret_cast<void **>(&ff.avcodec_receive_frame),
      reinterpret_cast<void **>(&ff.avcodec_free_context)};
  if (!ResolveFuncs(avcodec, avcodec_names, avcodec_ptrs, 7))
    return false;

  // avutil
  HMODULE avutil = TryLoadDLL(L"avutil", 57, 60);
  if (!avutil) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load avutil DLL");
    return false;
  }
  const char *avutil_names[] = {
      "av_frame_alloc", "av_frame_free", "av_packet_alloc",
      "av_packet_free"};
  void *avutil_ptrs[] = {
      reinterpret_cast<void **>(&ff.av_frame_alloc),
      reinterpret_cast<void **>(&ff.av_frame_free),
      reinterpret_cast<void **>(&ff.av_packet_alloc),
      reinterpret_cast<void **>(&ff.av_packet_free)};
  if (!ResolveFuncs(avutil, avutil_names, avutil_ptrs, 4))
    return false;

  // swscale
  HMODULE swscale = TryLoadDLL(L"swscale", 6, 9);
  if (!swscale) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load swscale DLL");
    return false;
  }
  const char *swscale_names[] = {"sws_getContext", "sws_scale",
                                 "sws_freeContext"};
  void *swscale_ptrs[] = {
      reinterpret_cast<void **>(&ff.sws_getContext),
      reinterpret_cast<void **>(&ff.sws_scale),
      reinterpret_cast<void **>(&ff.sws_freeContext)};
  if (!ResolveFuncs(swscale, swscale_names, swscale_ptrs, 3))
    return false;

  return true;
}

// ============================================================================
// Helper: get a string describing the codec
// ============================================================================
static const char *CodecToStr(enum codec_enum Codec) {
  switch (Codec) {
  case QSV_CODEC_AVC:
    return "H.264";
  case QSV_CODEC_HEVC:
    return "HEVC";
  case QSV_CODEC_AV1:
    return "AV1";
  case QSV_CODEC_VP9:
    return "VP9";
  }
  return "Unknown";
}

// ============================================================================
// ReEncodeDialog implementation
// ============================================================================

ReEncodeDialog::ReEncodeDialog(QWidget *Parent)
    : QDialog(Parent) {
  setWindowTitle(obs_module_text("ReEncoder"));
  setMinimumSize(640, 520);
  resize(720, 600);

  auto *MainLayout = new QVBoxLayout(this);

  // Input file
  auto *InputGroup = new QGroupBox(obs_module_text("ReEncoderInput"), this);
  auto *InputLayout = new QHBoxLayout(InputGroup);
  InputPathEdit = new QLineEdit(this);
  InputPathEdit->setReadOnly(true);
  InputPathEdit->setPlaceholderText(obs_module_text("ReEncoderInputPlaceholder"));
  BrowseInputBtn = new QPushButton(obs_module_text("ReEncoderBrowse"), this);
  InputLayout->addWidget(InputPathEdit, 1);
  InputLayout->addWidget(BrowseInputBtn);
  MainLayout->addWidget(InputGroup);

  // Output file
  auto *OutputGroup = new QGroupBox(obs_module_text("ReEncoderOutput"), this);
  auto *OutputLayout = new QHBoxLayout(OutputGroup);
  OutputPathEdit = new QLineEdit(this);
  OutputPathEdit->setReadOnly(true);
  OutputPathEdit->setPlaceholderText(obs_module_text("ReEncoderOutputPlaceholder"));
  BrowseOutputBtn = new QPushButton(obs_module_text("ReEncoderBrowse"), this);
  OutputLayout->addWidget(OutputPathEdit, 1);
  OutputLayout->addWidget(BrowseOutputBtn);
  MainLayout->addWidget(OutputGroup);

  // Encoder config summary
  ConfigGroup = new QGroupBox(obs_module_text("ReEncoderConfig"), this);
  auto *ConfigLayout = new QVBoxLayout(ConfigGroup);
  ConfigLabel = new QLabel(obs_module_text("ReEncoderConfigPending"), this);
  ConfigLabel->setWordWrap(true);
  ConfigLayout->addWidget(ConfigLabel);
  MainLayout->addWidget(ConfigGroup);

  // Progress
  auto *ProgressGroup = new QGroupBox(obs_module_text("ReEncoderProgress"), this);
  auto *ProgressLayout = new QVBoxLayout(ProgressGroup);
  StatusLabel = new QLabel(obs_module_text("ReEncoderReady"), this);
  ProgressLayout->addWidget(StatusLabel);
  ProgressBar = new QProgressBar(this);
  ProgressBar->setRange(0, 100);
  ProgressBar->setValue(0);
  ProgressLayout->addWidget(ProgressBar);
  MainLayout->addWidget(ProgressGroup);

  // Log output
  LogOutput = new QTextEdit(this);
  LogOutput->setReadOnly(true);
  LogOutput->setFont(QFont("Consolas", 9));
  LogOutput->setMaximumHeight(150);
  MainLayout->addWidget(LogOutput);

  // Buttons
  auto *ButtonLayout = new QHBoxLayout();
  ButtonLayout->addStretch();
  StartStopBtn = new QPushButton(obs_module_text("ReEncoderStart"), this);
  ButtonLayout->addWidget(StartStopBtn);
  MainLayout->addLayout(ButtonLayout);

  // Connections
  connect(BrowseInputBtn, &QPushButton::clicked, this,
          &ReEncodeDialog::OnBrowseInput);
  connect(BrowseOutputBtn, &QPushButton::clicked, this,
          &ReEncodeDialog::OnBrowseOutput);
  connect(StartStopBtn, &QPushButton::clicked, this,
          &ReEncodeDialog::OnStartStop);

  // Populate encoder config from active encoder
  PopulateEncoderConfig();
}

ReEncodeDialog::~ReEncodeDialog() {
  StopEncoding();
}

void ReEncodeDialog::closeEvent(QCloseEvent *Event) {
  StopEncoding();
  QDialog::closeEvent(Event);
}

// Populate encoder params from the first active QSV encoder
void ReEncodeDialog::PopulateEncoderConfig() {
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);

  for (auto &pair : EncoderDataMap) {
    plugin_context *ctx = pair.second;
    if (!ctx)
      continue;

    m_EncoderParams = ctx->EncoderParams;
    m_Codec = ctx->Codec;
    m_ParamsValid = true;

    // Build summary text
    QString summary = QString("%1 | %2x%3 @ %4/%5 fps | %6 kbps")
                          .arg(CodecToStr(m_Codec))
                          .arg(m_EncoderParams.Width)
                          .arg(m_EncoderParams.Height)
                          .arg(m_EncoderParams.FpsNum)
                          .arg(m_EncoderParams.FpsDen)
                          .arg(m_EncoderParams.TargetBitRate);

    if (m_EncoderParams.RateControl == MFX_RATECONTROL_CBR)
      summary += " | CBR";
    else if (m_EncoderParams.RateControl == MFX_RATECONTROL_VBR)
      summary += " | VBR";
    else if (m_EncoderParams.RateControl == MFX_RATECONTROL_CQP)
      summary += " | CQP";
    else if (m_EncoderParams.RateControl == MFX_RATECONTROL_ICQ)
      summary += " | ICQ";
    else
      summary += " | RC=" + QString::number(m_EncoderParams.RateControl);

    ConfigLabel->setText(summary);
    AppendLog(QString("Loaded encoder config from active encoder: %1")
                  .arg(summary));
    return;
  }

  // No active encoder found
  ConfigLabel->setText(obs_module_text("ReEncoderConfigNoEncoder"));
  m_ParamsValid = false;
  AppendLog(obs_module_text("ReEncoderLogNoEncoder"));
}

// Browse input video file
void ReEncodeDialog::OnBrowseInput() {
  QString path = QFileDialog::getOpenFileName(
      this, obs_module_text("ReEncoderSelectInput"), QString(),
      obs_module_text("ReEncoderVideoFilter"));
  if (path.isEmpty())
    return;
  InputPathEdit->setText(path);
  AppendLog(QString("Input: %1").arg(path));
}

// Browse output file
void ReEncodeDialog::OnBrowseOutput() {
  QString path = QFileDialog::getSaveFileName(
      this, obs_module_text("ReEncoderSelectOutput"), QString(),
      obs_module_text("ReEncoderBitstreamFilter"));
  if (path.isEmpty())
    return;
  OutputPathEdit->setText(path);
  AppendLog(QString("Output: %1").arg(path));
}

// Start / Stop toggle
void ReEncodeDialog::OnStartStop() {
  if (m_Encoding) {
    StopEncoding();
    return;
  }
  StartEncoding();
}

bool ReEncodeDialog::StartEncoding() {
  QString inputPath = InputPathEdit->text();
  if (inputPath.isEmpty()) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         obs_module_text("ReEncoderNoInput"));
    return false;
  }
  if (!QFileInfo::exists(inputPath)) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         obs_module_text("ReEncoderInputNotFound"));
    return false;
  }

  QString outputPath = OutputPathEdit->text();
  if (outputPath.isEmpty()) {
    // Auto-generate output path
    QFileInfo fi(inputPath);
    QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    switch (m_Codec) {
    case QSV_CODEC_AVC:
      outputPath = base + "_reencoded.h264";
      break;
    case QSV_CODEC_HEVC:
      outputPath = base + "_reencoded.hevc";
      break;
    case QSV_CODEC_AV1:
    case QSV_CODEC_VP9:
      outputPath = base + "_reencoded.ivf";
      break;
    }
    OutputPathEdit->setText(outputPath);
  }

  if (!m_ParamsValid) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         obs_module_text("ReEncoderNoConfig"));
    return false;
  }

  m_StopRequested = false;
  m_TotalFrames = 0;
  m_EncodedFrames = 0;
  ProgressBar->setValue(0);
  StatusLabel->setText(obs_module_text("ReEncoderStarting"));
  AppendLog("Starting re-encode...");

  SetUIEnabled(false);
  m_Encoding = true;

  m_EncodeThread = std::thread(&ReEncodeDialog::EncodeThreadMain, this);

  return true;
}

void ReEncodeDialog::StopEncoding() {
  if (!m_Encoding)
    return;

  m_StopRequested = true;
  if (m_EncodeThread.joinable())
    m_EncodeThread.join();

  m_Encoding = false;
  SetUIEnabled(true);
  StatusLabel->setText(obs_module_text("ReEncoderStopped"));
  AppendLog("Re-encode stopped.");
}

void ReEncodeDialog::SetUIEnabled(bool Enabled) {
  BrowseInputBtn->setEnabled(Enabled);
  BrowseOutputBtn->setEnabled(Enabled);
  if (Enabled)
    StartStopBtn->setText(obs_module_text("ReEncoderStart"));
  else
    StartStopBtn->setText(obs_module_text("ReEncoderStop"));
}

void ReEncodeDialog::AppendLog(const QString &Msg) {
  QMetaObject::invokeMethod(this, [this, Msg]() {
    LogOutput->append(Msg);
  }, Qt::QueuedConnection);
}

void ReEncodeDialog::UpdateProgress(int64_t Current, int64_t Total) {
  QMetaObject::invokeMethod(this, [this, Current, Total]() {
    if (Total > 0) {
      int pct = static_cast<int>(Current * 100 / Total);
      ProgressBar->setValue(pct);
      StatusLabel->setText(
          QString("%1 / %2 frames (%3%)")
              .arg(Current).arg(Total).arg(pct));
    } else {
      StatusLabel->setText(QString("%1 frames encoded").arg(Current));
    }
  }, Qt::QueuedConnection);
}

// ============================================================================
// FFmpeg ABI struct views — minimal field definitions for dynamic loading
// These are stable across FFmpeg releases (5.x/6.x/7.x).
// ============================================================================

// AVFrame: format(int) + padding + data[8](ptrs) + linesize[8](ints)
// Standard layout since FFmpeg 3.x:
//   offset 0x00: int format
//   offset 0x04: 4 bytes padding (to align pointers)
//   offset 0x08: uint8_t *data[8]
//   offset 0x48: int linesize[8]
struct AVFrameView {
  int format;
  int _pad;
  uint8_t *data[8];
  int linesize[8];
};

// AVCodecContext: key fields at stable offsets:
//   offset 0x00: AVClass* (pointer)
//   offset 0x08: int bit_rate
//   offset 0x10+: various fields
//   width/height/pix_fmt vary by version.
// Instead of probing, we get width/height from the codec context
// after avcodec_parameters_to_context populates them.
// We'll find them by signature: width > 0 && width < 32768
struct AVCodecContextView {
  // We'll use byte probing for width/height/pix_fmt
};

// ============================================================================
// Main encode thread
// ============================================================================
void ReEncodeDialog::EncodeThreadMain() {
  try {
    // 1. Load FFmpeg
    FFmpegFuncs ff;
    if (!LoadFFmpegDyn(ff)) {
      AppendLog("ERROR: Failed to load FFmpeg DLLs");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("FFmpeg load failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }
    AppendLog("FFmpeg loaded successfully");

    // Resolve av_read_frame and av_find_best_stream
    typedef int (*ReadFrameFn)(void *, void *);
    ReadFrameFn av_read_frame = nullptr;
    typedef int (*FindBestStreamFn)(void *, int, int *, int, void **, int);
    FindBestStreamFn av_find_best_stream = nullptr;

    for (int ver = 62; ver >= 59; --ver) {
      wchar_t name[64];
      swprintf_s(name, L"avformat-%d.dll", ver);
      HMODULE h = GetModuleHandleW(name);
      if (!h) h = LoadLibraryW(name);
      if (h) {
        if (!av_read_frame)
          av_read_frame = (ReadFrameFn)GetProcAddress(h, "av_read_frame");
        if (!av_find_best_stream)
          av_find_best_stream = (FindBestStreamFn)GetProcAddress(h, "av_find_best_stream");
      }
    }

    if (!av_read_frame) {
      AppendLog("ERROR: Cannot resolve av_read_frame");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("av_read_frame not found");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // 2. Open input
    void *fmtCtx = nullptr;
    QByteArray inputPath = InputPathEdit->text().toUtf8();
    if (ff.avformat_open_input(&fmtCtx, inputPath.data(), nullptr, nullptr) < 0) {
      AppendLog("ERROR: Cannot open input file");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Open input failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    if (ff.avformat_find_stream_info(fmtCtx, nullptr) < 0) {
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot find stream info");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Stream info failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // Find video stream using av_find_best_stream
    int videoStreamIdx = -1;
    if (av_find_best_stream) {
      videoStreamIdx = av_find_best_stream(
          fmtCtx, AVMEDIA_TYPE_VIDEO, nullptr, -1, nullptr, 0);
    }

    if (videoStreamIdx < 0) {
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: No video stream found");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("No video stream");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    AppendLog(QString("Video stream index: %1").arg(videoStreamIdx));

    // Get codec parameters from the stream.
    // We need to navigate the AVFormatContext to get the AVStream.
    // AVFormatContext layout (stable since FFmpeg 5.x):
    //   +0x00: const AVClass *av_class
    //   +0x08: struct AVInputFormat *iformat
    //   +0x10: struct AVOutputFormat *oformat
    //   +0x18: void *priv_data
    //   +0x20: struct AVIOContext *pb
    //   +0x28: int ctx_flags
    //   +0x2c: unsigned int nb_streams
    //   +0x30: AVStream **streams
    //   ...
    // This layout is very stable (used since FFmpeg 4.x).
    //
    // But since we can't include headers, we probe known offsets.
    // The safest approach: use av_find_best_stream's return value as index,
    // then find the streams pointer by checking which offset gives us
    // a valid pointer for that index.

    // Read nb_streams and streams from common offsets
    auto *rawFmt = reinterpret_cast<const uint8_t *>(fmtCtx);
    unsigned int nb_streams = 0;
    void *const *streams = nullptr;

    // Try common offsets for AVFormatContext
    // FFmpeg 6.x/7.x: nb_streams at 0x2c, streams at 0x30
    // FFmpeg 5.x: nb_streams at 0x28, streams at 0x30
    // FFmpeg 4.x: nb_streams at 0x40, streams at 0x48
    struct {
      int nbOff;
      int streamsOff;
    } const layout_attempts[] = {
      {0x2c, 0x30},  // FFmpeg 6.x/7.x
      {0x28, 0x30},  // FFmpeg 5.x
      {0x40, 0x48},  // FFmpeg 4.x
      {0x3c, 0x40},  // alternative
      {0x44, 0x48},  // alternative
      {0x30, 0x38},  // alternative
    };

    for (const auto &attempt : layout_attempts) {
      unsigned int n = *reinterpret_cast<const unsigned int *>(
          rawFmt + attempt.nbOff);
      if (n > 0 && n < 100) {
        void *const *s = *reinterpret_cast<void *const *const *>(
            rawFmt + attempt.streamsOff);
        if (s && s[0] != nullptr) {
          nb_streams = n;
          streams = s;
          break;
        }
      }
    }

    if (!streams || videoStreamIdx >= static_cast<int>(nb_streams)) {
      // Last resort: try to find streams by scanning
      for (int off = 0x20; off < 0x80; off += 8) {
        void *const *s = *reinterpret_cast<void *const *const *>(
            rawFmt + off);
        if (s && s[videoStreamIdx]) {
          // Verify this is a valid stream by checking nb_streams nearby
          for (int nbOff = off - 8; nbOff <= off + 8; nbOff += 4) {
            unsigned int n = *reinterpret_cast<const unsigned int *>(
                rawFmt + nbOff);
            if (n > 0 && n < 100 && videoStreamIdx < static_cast<int>(n)) {
              nb_streams = n;
              streams = s;
              break;
            }
          }
          if (streams)
            break;
        }
      }
    }

    if (!streams || !streams[videoStreamIdx]) {
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot access video stream (struct layout mismatch)");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Stream access failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    void *videoStream = streams[videoStreamIdx];

    // Get codecpar from AVStream
    // AVStream layout (stable):
    //   +0x00: int index
    //   +0x04: int id
    //   +0x08: void *priv_data
    //   +0x10: AVCodecParameters *codecpar
    //   ...
    // But offsets vary by version. Let's probe.
    auto *rawSt = reinterpret_cast<const uint8_t *>(videoStream);
    void *codecpar = nullptr;
    for (int off : {0x10, 0x18, 0x20, 0x28, 0x30, 0x38}) {
      void *cp = *reinterpret_cast<void *const *>(rawSt + off);
      if (cp) {
        // Verify: read first 4 bytes as int, should be a media type (0-3)
        int mediaType = *reinterpret_cast<const int *>(cp);
        if (mediaType >= 0 && mediaType <= 3) {
          codecpar = cp;
          break;
        }
      }
    }

    if (!codecpar) {
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot get codec parameters");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Codec params failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // Read codec_id, width, height from AVCodecParameters
    // AVCodecParameters layout:
    //   +0x00: int codec_type  (4 bytes)
    //   +0x04: int codec_id    (4 bytes) — or at +0x08 depending on version
    //   +0x08: int codec_tag   (4 bytes)
    //   ...
    //   width/height vary by version
    auto *cpRaw = reinterpret_cast<const uint8_t *>(codecpar);
    int codec_id = *reinterpret_cast<const int *>(cpRaw + 0x04);
    // Verify codec_id is plausible
    if (codec_id <= 0 || codec_id > 0x200) {
      codec_id = *reinterpret_cast<const int *>(cpRaw + 0x08);
    }

    // Find width and height
    int srcWidth = 0, srcHeight = 0;
    for (int wOff : {0x18, 0x20, 0x28, 0x30, 0x38, 0x40}) {
      int w = *reinterpret_cast<const int *>(cpRaw + wOff);
      int h = *reinterpret_cast<const int *>(cpRaw + wOff + 4);
      if (w > 0 && w <= 7680 && h > 0 && h <= 4320) {
        srcWidth = w;
        srcHeight = h;
        break;
      }
    }

    // Get frame rate: probe r_frame_rate in AVStream
    int fpsNum = 0, fpsDen = 1;
    for (int frOff : {0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78}) {
      int num = *reinterpret_cast<const int *>(rawSt + frOff);
      int den = *reinterpret_cast<const int *>(rawSt + frOff + 4);
      if (num > 0 && den > 0 && num < 1000 && den < 1000) {
        fpsNum = num;
        fpsDen = den;
        break;
      }
    }
    if (fpsNum <= 0) {
      fpsNum = 30;
      fpsDen = 1;
    }

    AppendLog(QString("Input: %1x%2, codec_id=0x%3, %4/%5 fps")
                  .arg(srcWidth).arg(srcHeight)
                  .arg(codec_id, 0, 16)
                  .arg(fpsNum).arg(fpsDen));

    // 3. Find decoder
    void *decoder = ff.avcodec_find_decoder(codec_id);
    if (!decoder) {
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot find decoder");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Decoder not found");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // 4. Open decoder
    void *decoderCtx = ff.avcodec_alloc_context3(decoder);
    if (!decoderCtx) {
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot allocate decoder context");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Alloc decoder failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    if (ff.avcodec_parameters_to_context(decoderCtx, codecpar) < 0) {
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot copy codec params to context");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Copy params failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    if (ff.avcodec_open2(decoderCtx, decoder, nullptr) < 0) {
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot open decoder");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Open decoder failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // 5. Open encoder
    m_EncoderParams.Width = static_cast<mfxU16>(srcWidth);
    m_EncoderParams.Height = static_cast<mfxU16>(srcHeight);
    m_EncoderParams.FourCC = MFX_FOURCC_NV12;
    m_EncoderParams.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    m_EncoderParams.FpsNum = static_cast<mfxU32>(fpsNum);
    m_EncoderParams.FpsDen = static_cast<mfxU32>(fpsDen);

    AppendLog(QString("Encoder params: %1x%2 @ %3/%4 fps")
                  .arg(srcWidth).arg(srcHeight)
                  .arg(fpsNum).arg(fpsDen));

    InitGlobalLoader();

    std::unique_ptr<QSVEncoder> encoder;
    if (!OpenEncoder(encoder, &m_EncoderParams, m_Codec, false)) {
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Failed to open encoder");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Open encoder failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }
    AppendLog("Encoder opened successfully");

    // 6. Open output file
    QByteArray outputPath = OutputPathEdit->text().toUtf8();
    std::ofstream outFile(outputPath.data(), std::ios::binary);
    if (!outFile.is_open()) {
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot open output file");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Output file failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // 7. Allocate NV12 frame buffer
    int nv12Size = srcWidth * srcHeight * 3 / 2;
    std::vector<uint8_t> nv12Frame(nv12Size);
    int nv12Strides[2] = {srcWidth, srcWidth};
    uint8_t *nv12Data[2] = {nv12Frame.data(),
                            nv12Frame.data() + srcWidth * srcHeight};

    // 8. Frame and packet allocation
    void *frame = ff.av_frame_alloc();
    void *packet = ff.av_packet_alloc();
    if (!frame || !packet) {
      ff.av_frame_free(&frame);
      ff.av_packet_free(&packet);
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot allocate frame/packet");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Alloc failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // SwsContext for pixel format conversion
    void *swsCtx = nullptr;

    // 9. Decode-encode loop
    int64_t frameCount = 0;
    int64_t pts = 0;
    const int64_t TS_MULT = 90000;
    bool eof = false;

    while (!m_StopRequested && !eof) {
      // Free and re-allocate packet for each read
      ff.av_packet_free(&packet);
      packet = ff.av_packet_alloc();
      if (!packet) {
        AppendLog("ERROR: Cannot allocate packet");
        break;
      }

      // Read next packet
      int ret = av_read_frame(fmtCtx, packet);
      if (ret < 0) {
        eof = true;
        // Send NULL to flush decoder
        ff.avcodec_send_packet(decoderCtx, nullptr);
      } else {
        // Check if this is a video stream packet
        // In AVPacket, stream_index is at a stable offset:
        // AVPacket layout (stable since FFmpeg 3.x):
        //   +0x00: AVBufferRef *buf
        //   +0x08: int64_t pts
        //   +0x10: int64_t dts
        //   +0x18: uint8_t *data
        //   +0x20: int size
        //   +0x24: int stream_index
        //   +0x28: int flags
        auto *pktRaw = reinterpret_cast<const uint8_t *>(packet);
        int pktStreamIdx = *reinterpret_cast<const int *>(pktRaw + 0x24);
        // Verify: try alternative offsets if 0x24 doesn't give a valid index
        if (pktStreamIdx < 0 || pktStreamIdx >= 100) {
          for (int off : {0x20, 0x28, 0x2c, 0x30, 0x34, 0x3c, 0x44}) {
            int idx = *reinterpret_cast<const int *>(pktRaw + off);
            if (idx >= 0 && idx < 100) {
              pktStreamIdx = idx;
              break;
            }
          }
        }

        if (pktStreamIdx != videoStreamIdx) {
          continue; // Skip non-video packets
        }

        // Send packet to decoder
        ret = ff.avcodec_send_packet(decoderCtx, packet);
        if (ret < 0) {
          AppendLog(QString("avcodec_send_packet error: %1").arg(ret));
          continue;
        }
      }

      // Receive all frames from decoder
      while (true) {
        ret = ff.avcodec_receive_frame(decoderCtx, frame);
        if (ret < 0)
          break;

        // Get pixel format from AVFrame
        auto *fv = reinterpret_cast<const AVFrameView *>(frame);
        int srcFormat = fv->format;

        // Create swscale context on first frame or format change
        if (!swsCtx) {
          swsCtx = ff.sws_getContext(
              srcWidth, srcHeight, srcFormat,
              srcWidth, srcHeight, AV_PIX_FMT_NV12,
              SWS_BILINEAR, nullptr, nullptr, nullptr);
          if (!swsCtx) {
            AppendLog("ERROR: Cannot create swscale context");
            break;
          }
        }

        // Convert to NV12
        ff.sws_scale(swsCtx,
                     (const uint8_t *const *)fv->data,
                     fv->linesize, 0, srcHeight,
                     nv12Data, nv12Strides);

        // Encode frame
        mfxBitstream *encBS = nullptr;
        mfxU64 mfxTS = pts * TS_MULT * fpsDen / fpsNum;
        try {
          mfxStatus sts = encoder->EncodeFrame(
              mfxTS, nv12Data, (uint32_t *)nv12Strides, &encBS);
          if (sts >= MFX_ERR_NONE && encBS && encBS->DataLength > 0) {
            outFile.write(
                reinterpret_cast<char *>(encBS->Data + encBS->DataOffset),
                encBS->DataLength);
            frameCount++;
          } else if (sts == MFX_ERR_MORE_DATA) {
            // VPP needs more data, skip
          }
        } catch (const std::exception &e) {
          AppendLog(QString("Encode error: %1").arg(e.what()));
          break;
        }

        pts++;
        m_EncodedFrames = frameCount;
        if (frameCount % 30 == 0) {
          UpdateProgress(frameCount, m_TotalFrames > 0 ? m_TotalFrames : 0);
        }
      }
    }

    // 10. Drain encoder
    AppendLog("Draining encoder...");
    try {
      while (true) {
        mfxBitstream *drainBS = nullptr;
        mfxStatus sts = encoder->EncodeFrame(0, nullptr, nullptr, &drainBS);
        if (sts == MFX_ERR_MORE_DATA)
          break;
        if (sts >= MFX_ERR_NONE && drainBS && drainBS->DataLength > 0) {
          outFile.write(
              reinterpret_cast<char *>(drainBS->Data + drainBS->DataOffset),
              drainBS->DataLength);
          frameCount++;
        }
      }
    } catch (const std::exception &e) {
      AppendLog(QString("Drain warning: %1").arg(e.what()));
    }

    // 11. Cleanup
    outFile.close();
    if (swsCtx)
      ff.sws_freeContext(swsCtx);
    ff.av_frame_free(&frame);
    ff.av_packet_free(&packet);
    ff.avcodec_free_context(&decoderCtx);
    ff.avformat_close_input(&fmtCtx);

    encoder->ClearData();

    m_EncodedFrames = frameCount;
    AppendLog(QString("Re-encode complete: %1 frames encoded").arg(frameCount));
    AppendLog(QString("Output: %1").arg(outputPath.data()));

    QMetaObject::invokeMethod(this, [this, frameCount]() {
      StatusLabel->setText(QString("Complete: %1 frames").arg(frameCount));
      ProgressBar->setValue(100);
      StartStopBtn->setText(obs_module_text("ReEncoderStart"));
      SetUIEnabled(true);
    }, Qt::QueuedConnection);

    m_Encoding = false;
    SetUIEnabled(true);

  } catch (const std::exception &e) {
    AppendLog(QString("Re-encode error: %1").arg(e.what()));
    QMetaObject::invokeMethod(this, [this, e = std::string(e.what())]() {
      StatusLabel->setText(QString("Error: %1").arg(e.c_str()));
      SetUIEnabled(true);
      m_Encoding = false;
    }, Qt::QueuedConnection);
  } catch (...) {
    AppendLog("Unknown error during re-encode");
    QMetaObject::invokeMethod(this, [this]() {
      StatusLabel->setText("Unknown error");
      SetUIEnabled(true);
      m_Encoding = false;
    }, Qt::QueuedConnection);
  }
}

// ============================================================================
// Frontend toolbar registration
// ============================================================================

static ReEncodeDialog *g_ActiveReEncodeDialog = nullptr;

static void OnReEncoderFrontendEvent(obs_frontend_event Event, void *) {
  if (Event != OBS_FRONTEND_EVENT_PROFILE_CHANGED)
    return;
  if (g_ActiveReEncodeDialog) {
    QMetaObject::invokeMethod(g_ActiveReEncodeDialog, [dialog = g_ActiveReEncodeDialog]() {
      dialog->PopulateEncoderConfig();
    }, Qt::QueuedConnection);
  }
}

static void OpenReEncoder(void * /*data*/) {
  try {
    auto *dialog = new ReEncodeDialog();
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    g_ActiveReEncodeDialog = dialog;
    QObject::connect(dialog, &QObject::destroyed, []() {
      g_ActiveReEncodeDialog = nullptr;
    });

    dialog->show();
    blog(LOG_INFO, "[QSV VPL] Re-encode dialog opened");

  } catch (const std::exception &e) {
    blog(LOG_ERROR,
         "[QSV VPL] OpenReEncoder: std::exception caught: %s", e.what());
  } catch (...) {
    blog(LOG_ERROR,
         "[QSV VPL] OpenReEncoder: unknown exception caught");
  }
}

void RegisterReEncoder() {
  obs_frontend_add_tools_menu_item(obs_module_text("ReEncoder"),
                                    OpenReEncoder, nullptr);
  obs_frontend_add_event_callback(OnReEncoderFrontendEvent, nullptr);
}