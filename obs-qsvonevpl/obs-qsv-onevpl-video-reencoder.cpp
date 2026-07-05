// Qt headers must come before OBS headers to avoid macro conflicts (e.g. LOG_ERROR)
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QFileInfo>

#include "obs-qsv-onevpl-video-reencoder.hpp"
#include "helpers/common_utils.hpp"
#include "helpers/encoder_params_parser.hpp"
#include "obs-qsv-onevpl-encoder.hpp"
#include "obs-qsv-onevpl-plugin-init.hpp"
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>
#include <string_view>
#include <vector>
#include <string>

#include <windows.h>

// FFmpeg enum constants (dynamic loading, no headers available)
static constexpr int AVMEDIA_TYPE_VIDEO = 0;
static constexpr int AV_PIX_FMT_NV12 = 23;
static constexpr int SWS_BILINEAR = 2;

// ============================================================================
// FFmpeg dynamic loading — resolves function pointers from OBS's bundled DLLs
// ============================================================================

// Try loading a DLL from a specific directory.
static HMODULE TryLoadDLLFromDir(const wchar_t *Dir, const wchar_t *Name) {
  wchar_t Path[MAX_PATH];
  if (swprintf_s(Path, L"%s\\%s", Dir, Name) <= 0)
    return nullptr;
  HMODULE hMod = LoadLibraryW(Path);
  if (!hMod) {
    DWORD err = GetLastError();
    blog(LOG_DEBUG, "[QSV VPL ReEncoder] LoadLibraryW(%S) failed, gle=%lu", Path, err);
  }
  return hMod;
}

static HMODULE GetCurrentModuleHandle() {
  HMODULE hMod = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(GetCurrentModuleHandle),
                          &hMod)) {
    return nullptr;
  }
  return hMod;
}

// Collect candidate directories that may contain OBS's FFmpeg DLLs.
static std::vector<std::wstring> GetFFmpegSearchDirs() {
  std::vector<std::wstring> dirs;

  // 1. Plugin's own directory (obs-plugins/64bit).
  wchar_t pluginPath[MAX_PATH] = {};
  HMODULE hSelf = GetCurrentModuleHandle();
  if (hSelf && GetModuleFileNameW(hSelf, pluginPath, MAX_PATH) > 0) {
    if (wchar_t *lastSlash = wcsrchr(pluginPath, L'\\'))
      *lastSlash = L'\0';
    dirs.emplace_back(pluginPath);
  }

  // 2. OBS executable directory (bin/64bit).
  wchar_t obsDir[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, obsDir, MAX_PATH) > 0) {
    if (wchar_t *lastSlash = wcsrchr(obsDir, L'\\'))
      *lastSlash = L'\0';
    dirs.emplace_back(obsDir);

    // 3. Parent's bin/64bit (some portable layouts use obs-studio/bin/64bit).
    wchar_t parentBin[MAX_PATH] = {};
    if (swprintf_s(parentBin, L"%s\\..\\bin\\64bit", obsDir) > 0) {
      // Normalize path
      wchar_t absPath[MAX_PATH] = {};
      if (GetFullPathNameW(parentBin, MAX_PATH, absPath, nullptr))
        dirs.emplace_back(absPath);
      else
        dirs.emplace_back(parentBin);
    }
  }

  return dirs;
}

// Try loading a DLL by versioned names, unversioned name, and from candidate dirs.
static HMODULE TryLoadDLLWithFallback(const wchar_t *BaseName, int MinVer,
                                      int MaxVer) {
  HMODULE hMod = nullptr;

  auto tryLoaded = [&](const wchar_t *Name) -> HMODULE {
    HMODULE m = GetModuleHandleW(Name);
    if (m)
      blog(LOG_INFO, "[QSV VPL ReEncoder] Found already-loaded %S", Name);
    return m;
  };

  auto tryLoad = [&](const wchar_t *Name) -> HMODULE {
    HMODULE m = LoadLibraryW(Name);
    if (m)
      blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded %S", Name);
    return m;
  };

  // 1. Already loaded versioned module.
  for (int v = MaxVer; v >= MinVer; --v) {
    wchar_t Name[64];
    swprintf_s(Name, L"%s-%d.dll", BaseName, v);
    hMod = tryLoaded(Name);
    if (hMod)
      return hMod;
  }

  // 2. Load versioned module from default search paths.
  for (int v = MaxVer; v >= MinVer; --v) {
    wchar_t Name[64];
    swprintf_s(Name, L"%s-%d.dll", BaseName, v);
    hMod = tryLoad(Name);
    if (hMod)
      return hMod;
  }

  // 3. Try unversioned name.
  {
    wchar_t Name[64];
    swprintf_s(Name, L"%s.dll", BaseName);
    hMod = tryLoaded(Name);
    if (hMod)
      return hMod;
    hMod = tryLoad(Name);
    if (hMod)
      return hMod;
  }

  // 4. Try candidate directories.
  std::vector<std::wstring> searchDirs = GetFFmpegSearchDirs();
  for (const auto &dir : searchDirs) {
    for (int v = MaxVer; v >= MinVer; --v) {
      wchar_t Name[64];
      swprintf_s(Name, L"%s-%d.dll", BaseName, v);
      hMod = TryLoadDLLFromDir(dir.c_str(), Name);
      if (hMod) {
        blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded %S from %S", Name, dir.c_str());
        return hMod;
      }
    }

    {
      wchar_t Name[64];
      swprintf_s(Name, L"%s.dll", BaseName);
      hMod = TryLoadDLLFromDir(dir.c_str(), Name);
      if (hMod) {
        blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded %S from %S", Name, dir.c_str());
        return hMod;
      }
    }
  }

  blog(LOG_WARNING,
       "[QSV VPL ReEncoder] Could not load %S DLL (tried versions %d-%d + unversioned)",
       BaseName, MinVer, MaxVer);
  return nullptr;
}

static bool ResolveFuncs(HMODULE Mod, const char *Names[], void **Funcs[],
                         size_t Count) {
  for (size_t i = 0; i < Count; i++) {
    void *addr = reinterpret_cast<void *>(GetProcAddress(Mod, Names[i]));
    if (!addr) {
      blog(LOG_WARNING, "[QSV VPL ReEncoder] Failed to resolve %s from DLL",
           Names[i]);
      return false;
    }
    // Use memcpy to avoid strict-aliasing/UB when writing a void* into a
    // function-pointer slot.
    std::memcpy(Funcs[i], &addr, sizeof(void *));
  }
  return true;
}

bool LoadFFmpegDyn(FFmpegFuncs &ff) {
  ZeroMemory(&ff, sizeof(ff));

  // Widen version ranges and fall back to unversioned names / candidate dirs.
  // OBS 28/29/30/31/32 bundle various FFmpeg versions; be permissive.
  // Load order matters: avutil is a dependency of avcodec/avformat, so load it
  // first so that later LoadLibraryW calls can resolve dependencies from the
  // same directory.

  auto searchDirs = GetFFmpegSearchDirs();
  if (searchDirs.empty()) {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] Could not determine any FFmpeg search directory");
  } else {
    for (const auto &dir : searchDirs)
      blog(LOG_INFO, "[QSV VPL ReEncoder] FFmpeg search dir: %S", dir.c_str());
  }

  // avutil
  HMODULE avutil = TryLoadDLLWithFallback(L"avutil", 55, 60);
  if (!avutil) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load avutil DLL");
    return false;
  }
  const char *avutil_names[] = {
      "av_frame_alloc", "av_frame_free",
      "av_image_get_buffer_size", "av_image_fill_arrays"};
  void **avutil_ptrs[] = {
      reinterpret_cast<void **>(&ff.av_frame_alloc),
      reinterpret_cast<void **>(&ff.av_frame_free),
      reinterpret_cast<void **>(&ff.av_image_get_buffer_size),
      reinterpret_cast<void **>(&ff.av_image_fill_arrays)};
  if (!ResolveFuncs(avutil, avutil_names, avutil_ptrs, 4))
    return false;

  // In standard FFmpeg av_packet_alloc/free live in avutil, but OBS's custom
  // builds move them to avcodec. Try avutil first (non-fatal), then require
  // them from avcodec.
  ff.av_packet_alloc = reinterpret_cast<decltype(ff.av_packet_alloc)>(
      GetProcAddress(avutil, "av_packet_alloc"));
  ff.av_packet_free = reinterpret_cast<decltype(ff.av_packet_free)>(
      GetProcAddress(avutil, "av_packet_free"));
  if (ff.av_packet_alloc)
    blog(LOG_INFO, "[QSV VPL ReEncoder] av_packet_alloc resolved from avutil");

  // avcodec
  HMODULE avcodec = TryLoadDLLWithFallback(L"avcodec", 57, 62);
  if (!avcodec) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load avcodec DLL");
    return false;
  }
  const char *avcodec_names[] = {
      "avcodec_find_decoder",
      "avcodec_alloc_context3",
      "avcodec_parameters_to_context",
      "avcodec_open2",
      "avcodec_send_packet",
      "avcodec_receive_frame",
      "avcodec_free_context"};
  void **avcodec_ptrs[] = {
      reinterpret_cast<void **>(&ff.avcodec_find_decoder),
      reinterpret_cast<void **>(&ff.avcodec_alloc_context3),
      reinterpret_cast<void **>(&ff.avcodec_parameters_to_context),
      reinterpret_cast<void **>(&ff.avcodec_open2),
      reinterpret_cast<void **>(&ff.avcodec_send_packet),
      reinterpret_cast<void **>(&ff.avcodec_receive_frame),
      reinterpret_cast<void **>(&ff.avcodec_free_context)};
  if (!ResolveFuncs(avcodec, avcodec_names, avcodec_ptrs, 7))
    return false;

  // av_packet_alloc/free: prefer avcodec if available (OBS layout).
  auto *pktAlloc = reinterpret_cast<decltype(ff.av_packet_alloc)>(
      GetProcAddress(avcodec, "av_packet_alloc"));
  auto *pktFree = reinterpret_cast<decltype(ff.av_packet_free)>(
      GetProcAddress(avcodec, "av_packet_free"));
  if (pktAlloc) {
    ff.av_packet_alloc = pktAlloc;
    blog(LOG_INFO, "[QSV VPL ReEncoder] av_packet_alloc resolved from avcodec");
  }
  if (pktFree) {
    ff.av_packet_free = pktFree;
    blog(LOG_INFO, "[QSV VPL ReEncoder] av_packet_free resolved from avcodec");
  }
  if (!ff.av_packet_alloc || !ff.av_packet_free) {
    blog(LOG_ERROR,
         "[QSV VPL ReEncoder] Cannot resolve av_packet_alloc/free from either "
         "avutil or avcodec");
    return false;
  }

  // avformat (depends on avcodec/avutil)
  HMODULE avformat = TryLoadDLLWithFallback(L"avformat", 57, 62);
  if (!avformat) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load avformat DLL");
    return false;
  }
  const char *avformat_names[] = {
      "avformat_open_input", "avformat_close_input",
      "avformat_find_stream_info", "av_read_frame", "av_find_best_stream",
      "avformat_alloc_output_context2", "avformat_new_stream",
      "avformat_free_context", "avio_open", "avio_closep",
      "avformat_write_header", "av_write_trailer",
      "av_interleaved_write_frame", "av_packet_unref",
      "avcodec_parameters_copy", "avcodec_parameters_alloc",
      "avcodec_parameters_free"};
  void **avformat_ptrs[] = {
      reinterpret_cast<void **>(&ff.avformat_open_input),
      reinterpret_cast<void **>(&ff.avformat_close_input),
      reinterpret_cast<void **>(&ff.avformat_find_stream_info),
      reinterpret_cast<void **>(&ff.av_read_frame),
      reinterpret_cast<void **>(&ff.av_find_best_stream),
      reinterpret_cast<void **>(&ff.avformat_alloc_output_context2),
      reinterpret_cast<void **>(&ff.avformat_new_stream),
      reinterpret_cast<void **>(&ff.avformat_free_context),
      reinterpret_cast<void **>(&ff.avio_open),
      reinterpret_cast<void **>(&ff.avio_closep),
      reinterpret_cast<void **>(&ff.avformat_write_header),
      reinterpret_cast<void **>(&ff.av_write_trailer),
      reinterpret_cast<void **>(&ff.av_interleaved_write_frame),
      reinterpret_cast<void **>(&ff.av_packet_unref),
      reinterpret_cast<void **>(&ff.avcodec_parameters_copy),
      reinterpret_cast<void **>(&ff.avcodec_parameters_alloc),
      reinterpret_cast<void **>(&ff.avcodec_parameters_free)};
  if (!ResolveFuncs(avformat, avformat_names, avformat_ptrs, 17))
    return false;

  // swscale
  HMODULE swscale = TryLoadDLLWithFallback(L"swscale", 4, 11);
  if (!swscale) {
    blog(LOG_ERROR, "[QSV VPL ReEncoder] Cannot load swscale DLL");
    return false;
  }
  const char *swscale_names[] = {"sws_getContext", "sws_scale",
                                 "sws_freeContext"};
  void **swscale_ptrs[] = {
      reinterpret_cast<void **>(&ff.sws_getContext),
      reinterpret_cast<void **>(&ff.sws_scale),
      reinterpret_cast<void **>(&ff.sws_freeContext)};
  if (!ResolveFuncs(swscale, swscale_names, swscale_ptrs, 3))
    return false;

  blog(LOG_INFO, "[QSV VPL ReEncoder] All FFmpeg DLLs loaded and resolved");
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
  RefreshConfigBtn = new QPushButton(obs_module_text("ReEncoderRefresh"), this);
  ButtonLayout->addWidget(RefreshConfigBtn);
  ButtonLayout->addStretch();
  StartStopBtn = new QPushButton(obs_module_text("ReEncoderStart"), this);
  ButtonLayout->addWidget(StartStopBtn);
  MainLayout->addLayout(ButtonLayout);

  // Connections
  connect(BrowseInputBtn, &QPushButton::clicked, this,
          &ReEncodeDialog::OnBrowseInput);
  connect(BrowseOutputBtn, &QPushButton::clicked, this,
          &ReEncodeDialog::OnBrowseOutput);
  connect(RefreshConfigBtn, &QPushButton::clicked, this,
          &ReEncodeDialog::OnRefreshConfig);
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

// ============================================================================
// Fallback: load encoder config from OBS profile config files (basic.ini +
// recordEncoder.json).  Called when no active encoder instance is available.
// ============================================================================
bool ReEncodeDialog::LoadEncoderConfigFromFile() {
  blog(LOG_INFO, "[QSV VPL ReEncoder] LoadEncoderConfigFromFile: attempting to read config from OBS profile");

  config_t *config = obs_frontend_get_profile_config();
  if (!config) {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] obs_frontend_get_profile_config() returned NULL");
    return false;
  }

  // Determine output mode and read the recording encoder ID
  const char *mode = config_get_string(config, "Output", "Mode");
  bool advOut = (mode && strcmp(mode, "Advanced") == 0);
  blog(LOG_INFO, "[QSV VPL ReEncoder] Output mode: %s", advOut ? "Advanced" : (mode ? mode : "NULL"));

  const char *encId = nullptr;
  if (advOut) {
    encId = config_get_string(config, "AdvOut", "RecEncoder");
    blog(LOG_INFO, "[QSV VPL ReEncoder] AdvOut.RecEncoder = %s", encId ? encId : "NULL");
    // "none" means re-use the streaming encoder
    if (!encId || strcmp(encId, "none") == 0 || encId[0] == '\0') {
      encId = config_get_string(config, "AdvOut", "Encoder");
      blog(LOG_INFO, "[QSV VPL ReEncoder] Falling back to AdvOut.Encoder = %s", encId ? encId : "NULL");
    }
  } else {
    encId = config_get_string(config, "SimpleOutput", "RecEncoder");
    blog(LOG_INFO, "[QSV VPL ReEncoder] SimpleOutput.RecEncoder = %s", encId ? encId : "NULL");
  }

  if (!encId || encId[0] == '\0') {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] No encoder ID found in profile config");
    return false;
  }

  // ---- Map encoder ID to codec enum ----------------------------------------
  codec_enum codec;
  if (strcmp(encId, "obs_qsv_vpl_h264") == 0 ||
      strcmp(encId, "obs_qsv_vpl_h264_tex") == 0 ||
      strcmp(encId, "qsv") == 0 ||
      strcmp(encId, "obs_qsv11_v2") == 0)
    codec = QSV_CODEC_AVC;
  else if (strcmp(encId, "obs_qsv_vpl_hevc") == 0 ||
           strcmp(encId, "obs_qsv_vpl_hevc_tex") == 0 ||
           strcmp(encId, "qsv_hevc") == 0 ||
           strcmp(encId, "obs_qsv11_hevc") == 0)
    codec = QSV_CODEC_HEVC;
  else if (strcmp(encId, "obs_qsv_vpl_av1") == 0 ||
           strcmp(encId, "obs_qsv_vpl_av1_tex") == 0 ||
           strcmp(encId, "qsv_av1") == 0 ||
           strcmp(encId, "obs_qsv11_av1") == 0)
    codec = QSV_CODEC_AV1;
  else if (strcmp(encId, "obs_qsv_vpl_vp9") == 0 ||
           strcmp(encId, "obs_qsv_vpl_vp9_tex") == 0 ||
           strcmp(encId, "qsv_vp9") == 0 ||
           strcmp(encId, "obs_qsv11_vp9") == 0)
    codec = QSV_CODEC_VP9;
  else {
    blog(LOG_WARNING, "[QSV VPL ReEncoder] Unrecognized encoder ID '%s' — not a QSV encoder", encId);
    return false; // not a QSV encoder, can't re-encode with our plugin
  }

  m_Codec = codec;
  blog(LOG_INFO, "[QSV VPL ReEncoder] Matched encoder '%s' -> codec %d", encId, (int)codec);

  // Start from a clean default-initialized struct, then overlay whatever we
  // can read from the profile files.
  m_EncoderParams = encoder_params{};

  // Sensible defaults in case the file is missing or incomplete.
  m_EncoderParams.TargetUsage = 4; // balanced
  m_EncoderParams.TargetBitRate = 5000;
  m_EncoderParams.RateControl = MFX_RATECONTROL_VBR;

  // ---- Advanced mode: try reading recordEncoder.json -----------------------
  if (advOut) {
    char *profilePath = obs_frontend_get_current_profile_path();
    if (profilePath) {
      std::string jsonPath = std::string(profilePath) + "/recordEncoder.json";
      blog(LOG_INFO, "[QSV VPL ReEncoder] Trying to load JSON config from: %s", jsonPath.c_str());
      bfree(profilePath);

      obs_data_t *s = obs_data_create_from_json_file(jsonPath.c_str());
      if (s) {
        blog(LOG_INFO, "[QSV VPL ReEncoder] Successfully loaded recordEncoder.json");

        // Use the same parser as the live encoder so every UI option is honored.
        ParseEncoderParamsFromObsData(s, m_Codec, m_EncoderParams);
        obs_data_release(s);

        m_ParamsValid = true;
        blog(LOG_INFO, "[QSV VPL ReEncoder] Config loaded from JSON: rc=%d bitrate=%u keyint=%u tu=%u",
             m_EncoderParams.RateControl, m_EncoderParams.TargetBitRate,
             m_EncoderParams.KeyIntSec, m_EncoderParams.TargetUsage);
        return true;
      }
      blog(LOG_WARNING, "[QSV VPL ReEncoder] Failed to parse recordEncoder.json (file may not exist or be invalid)");
    } else {
      blog(LOG_WARNING, "[QSV VPL ReEncoder] obs_frontend_get_current_profile_path() returned NULL");
    }
    // Fallback for advanced mode w/o JSON: read limited params from basic.ini
    uint64_t vb = config_get_uint(config, "AdvOut", "VBitrate");
    blog(LOG_INFO, "[QSV VPL ReEncoder] Advanced fallback: AdvOut.VBitrate = %llu", (unsigned long long)vb);
    if (vb > 0)
      m_EncoderParams.TargetBitRate = (uint32_t)vb;
  } else {
    // ---- Simple mode: read from [SimpleOutput] -------------------------------
    uint64_t vb = config_get_uint(config, "SimpleOutput", "VBitrate");
    blog(LOG_INFO, "[QSV VPL ReEncoder] Simple mode: SimpleOutput.VBitrate = %llu", (unsigned long long)vb);
    if (vb > 0)
      m_EncoderParams.TargetBitRate = (uint32_t)vb;

    // Simple mode only exposes a small subset of encoder options. Try to read
    // the common ones so the offline encode at least matches the preset/quality
    // level used for recording.
    const char *preset = config_get_string(config, "SimpleOutput", "preset");
    if (!preset || preset[0] == '\0')
      preset = config_get_string(config, "SimpleOutput", "QSVPreset");
    blog(LOG_INFO, "[QSV VPL ReEncoder] Simple mode: preset/QSVPreset = %s", preset ? preset : "NULL");
    if (preset && preset[0] != '\0') {
      std::string_view psv(preset);
      // OBS simple presets: "hq", "mq", "fast", "faster", "slow", "slower",
      // "lossless", "indistinguishable", "superfast", "ultrafast", etc.
      if (psv == "hq" || psv == "slow" || psv == "slower" || psv == "lossless" ||
          psv == "indistinguishable")
        m_EncoderParams.TargetUsage = MFX_TARGETUSAGE_1;
      else if (psv == "mq" || psv == "balanced" || psv == "default")
        m_EncoderParams.TargetUsage = MFX_TARGETUSAGE_4;
      else if (psv == "fast" || psv == "faster" || psv == "superfast" ||
               psv == "ultrafast" || psv == "performance")
        m_EncoderParams.TargetUsage = MFX_TARGETUSAGE_7;
    }

    uint64_t keyint = config_get_uint(config, "SimpleOutput", "keyint_sec");
    blog(LOG_INFO, "[QSV VPL ReEncoder] Simple mode: keyint_sec = %llu", (unsigned long long)keyint);
    if (keyint > 0)
      m_EncoderParams.KeyIntSec = static_cast<mfxU16>(keyint);
  }

  m_ParamsValid = true;
  blog(LOG_INFO, "[QSV VPL ReEncoder] Config loaded from basic.ini: bitrate=%u tu=%u keyint=%u codec=%d",
       m_EncoderParams.TargetBitRate, m_EncoderParams.TargetUsage,
       m_EncoderParams.KeyIntSec, (int)m_Codec);
  return true;
}

// Populate encoder params from the first active QSV encoder
void ReEncodeDialog::PopulateEncoderConfig() {
  {
    std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
    blog(LOG_INFO, "[QSV VPL ReEncoder] PopulateEncoderConfig: EncoderDataMap has %zu entries",
         EncoderDataMap.size());

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
      blog(LOG_INFO, "[QSV VPL ReEncoder] Loaded config from active encoder: codec=%d bitrate=%u",
           (int)m_Codec, m_EncoderParams.TargetBitRate);
      return;
    }
  } // release lock

  blog(LOG_INFO, "[QSV VPL ReEncoder] No active encoder found, trying config file fallback");
  if (LoadEncoderConfigFromFile()) {
    QString summary = QString("%1 | bitrate %2 kbps")
                          .arg(CodecToStr(m_Codec))
                          .arg(m_EncoderParams.TargetBitRate);
    if (m_EncoderParams.RateControl == MFX_RATECONTROL_CBR)
      summary += " | CBR";
    else if (m_EncoderParams.RateControl == MFX_RATECONTROL_VBR)
      summary += " | VBR";
    else if (m_EncoderParams.RateControl == MFX_RATECONTROL_CQP)
      summary += " | CQP";
    else if (m_EncoderParams.RateControl == MFX_RATECONTROL_ICQ)
      summary += " | ICQ";

    ConfigLabel->setText(summary);
    AppendLog(QString("Loaded encoder config from OBS profile: %1").arg(summary));
    blog(LOG_INFO, "[QSV VPL ReEncoder] Config loaded from OBS profile: codec=%d bitrate=%u",
         (int)m_Codec, m_EncoderParams.TargetBitRate);
    return;
  }

  // No active encoder found
  blog(LOG_WARNING, "[QSV VPL ReEncoder] Both active encoder and file config loading failed");
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

// Manually refresh encoder config from active encoder or profile files
void ReEncodeDialog::OnRefreshConfig() {
  if (m_Encoding) {
    AppendLog("Cannot refresh config while encoding is in progress.");
    return;
  }
  AppendLog("Refreshing encoder configuration...");
  PopulateEncoderConfig();
  AppendLog("Configuration refresh complete.");
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
    // Auto-generate output path; default to MP4 container.
    QFileInfo fi(inputPath);
    QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    outputPath = base + "_reencoded.mp4";
    OutputPathEdit->setText(outputPath);
  }

  if (!m_ParamsValid) {
    QMessageBox::warning(this, obs_module_text("ReEncoderError"),
                         obs_module_text("ReEncoderNoConfig"));
    return false;
  }

  // Copy paths to thread-safe members so the worker thread never touches
  // the GUI widgets from a non-GUI thread.
  m_InputPath = inputPath.toUtf8().constData();
  m_OutputPath = outputPath.toUtf8().constData();

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
  // Mirror every UI log message to the OBS log file so it survives a crash.
  QByteArray utf8 = Msg.toUtf8();
  blog(LOG_INFO, "[QSV VPL ReEncoder] %s", utf8.constData());
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

// AVFrame layout (stable since FFmpeg 3.x):
//   offset 0x00: uint8_t *data[8]
//   offset 0x40: int linesize[8]
//   offset 0x60: uint8_t **extended_data
//   offset 0x68: int width
//   offset 0x6c: int height
//   offset 0x70: int nb_samples
//   offset 0x74: int format
struct AVFrameView {
  uint8_t *data[8];
  int linesize[8];
  uint8_t **extended_data;
  int width;
  int height;
  int nb_samples;
  int format;
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
#ifdef _WIN32
  __try {
    EncodeThreadMainImpl();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Keep this handler strictly C-style: no QString/lambda so the compiler
    // does not need object unwinding inside this function.
    DWORD code = GetExceptionCode();
    blog(LOG_ERROR,
         "[QSV VPL ReEncoder] FATAL SEH exception 0x%08X in encode thread", code);
    m_Encoding = false;
    m_StopRequested = true;
  }
#else
  EncodeThreadMainImpl();
#endif
}

void ReEncodeDialog::EncodeThreadMainImpl() {
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

    // Safety net: verify every critical function pointer is non-null before we
    // start using them. This catches any weird loader/alias corruption.
    auto checkPtr = [](auto ptr, const char *name) -> bool {
      if (ptr)
        return true;
      blog(LOG_ERROR,
           "[QSV VPL ReEncoder] Critical function pointer %s is null", name);
      return false;
    };
    bool funcsOk =
        checkPtr(ff.avformat_open_input, "avformat_open_input") &&
        checkPtr(ff.avformat_find_stream_info, "avformat_find_stream_info") &&
        checkPtr(ff.av_read_frame, "av_read_frame") &&
        checkPtr(ff.avcodec_find_decoder, "avcodec_find_decoder") &&
        checkPtr(ff.avcodec_alloc_context3, "avcodec_alloc_context3") &&
        checkPtr(ff.avcodec_open2, "avcodec_open2") &&
        checkPtr(ff.avcodec_send_packet, "avcodec_send_packet") &&
        checkPtr(ff.avcodec_receive_frame, "avcodec_receive_frame") &&
        checkPtr(ff.avformat_alloc_output_context2,
                 "avformat_alloc_output_context2") &&
        checkPtr(ff.avformat_new_stream, "avformat_new_stream") &&
        checkPtr(ff.avio_open, "avio_open") &&
        checkPtr(ff.avformat_write_header, "avformat_write_header") &&
        checkPtr(ff.av_interleaved_write_frame, "av_interleaved_write_frame") &&
        checkPtr(ff.av_write_trailer, "av_write_trailer") &&
        checkPtr(ff.av_packet_unref, "av_packet_unref");
    if (!funcsOk) {
      AppendLog("ERROR: FFmpeg function resolution incomplete");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("FFmpeg funcs incomplete");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // 2. Open input
    void *fmtCtx = nullptr;
    blog(LOG_INFO, "[QSV VPL ReEncoder] Input path member: %s", m_InputPath.c_str());
    if (m_InputPath.empty()) {
      AppendLog("ERROR: Input path is empty");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Empty input path");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }
    int openRet = ff.avformat_open_input(&fmtCtx, m_InputPath.c_str(), nullptr,
                                         nullptr);
    blog(LOG_INFO, "[QSV VPL ReEncoder] avformat_open_input returned %d, fmtCtx=%p",
         openRet, fmtCtx);
    if (openRet < 0) {
      AppendLog("ERROR: Cannot open input file");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Open input failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    blog(LOG_INFO, "[QSV VPL ReEncoder] Calling avformat_find_stream_info...");
    int siRet = ff.avformat_find_stream_info(fmtCtx, nullptr);
    blog(LOG_INFO, "[QSV VPL ReEncoder] avformat_find_stream_info returned %d", siRet);
    if (siRet < 0) {
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
    blog(LOG_INFO, "[QSV VPL ReEncoder] Calling av_find_best_stream...");
    int videoStreamIdx = -1;
    if (ff.av_find_best_stream) {
      videoStreamIdx = ff.av_find_best_stream(
          fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    }
    blog(LOG_INFO, "[QSV VPL ReEncoder] av_find_best_stream returned %d", videoStreamIdx);

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
          blog(LOG_INFO,
               "[QSV VPL ReEncoder] AVFormatContext layout: nb_streams@0x%x=%u, "
               "streams@0x%x",
               attempt.nbOff, n, attempt.streamsOff);
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

    // Find width and height. In modern FFmpeg (6.x/7.x) they sit at 0x48/0x4c
    // inside AVCodecParameters.
    int srcWidth = 0, srcHeight = 0;
    for (int wOff : {0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48}) {
      int w = *reinterpret_cast<const int *>(cpRaw + wOff);
      int h = *reinterpret_cast<const int *>(cpRaw + wOff + 4);
      if (w > 0 && w <= 7680 && h > 0 && h <= 4320) {
        srcWidth = w;
        srcHeight = h;
        blog(LOG_INFO,
             "[QSV VPL ReEncoder] AVCodecParameters width/height offset 0x%x = %dx%d",
             wOff, w, h);
        break;
      }
    }

    // Get frame rate: probe r_frame_rate in AVStream. In FFmpeg 6.x/7.x it
    // sits after the embedded AVPacket (attached_pic), typically around 0xc0.
    int fpsNum = 0, fpsDen = 1;
    for (int frOff = 0x38; frOff < 0x100; frOff += 8) {
      int num = *reinterpret_cast<const int *>(rawSt + frOff);
      int den = *reinterpret_cast<const int *>(rawSt + frOff + 4);
      if (num > 0 && den > 0 && num < 1000 && den < 1000) {
        fpsNum = num;
        fpsDen = den;
        blog(LOG_INFO,
             "[QSV VPL ReEncoder] AVStream r_frame_rate offset 0x%x = %d/%d",
             frOff, num, den);
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

    // 6. Allocate output muxer context (MP4/MKV/etc based on extension)
    blog(LOG_INFO, "[QSV VPL ReEncoder] Output path member: %s", m_OutputPath.c_str());
    void *outFmtCtx = nullptr;
    int allocRet = ff.avformat_alloc_output_context2(
        &outFmtCtx, nullptr, nullptr, m_OutputPath.c_str());
    if (allocRet < 0 || !outFmtCtx) {
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot allocate output format context");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Output context failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    void *outStream = ff.avformat_new_stream(outFmtCtx, nullptr);
    if (!outStream) {
      ff.avformat_free_context(outFmtCtx);
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot create output stream");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Output stream failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // Copy input codec parameters to output stream, then override codec_id
    // to match the encoder (the output is a re-encode, not a remux).
    {
      auto *outStreamRaw = reinterpret_cast<uint8_t *>(outStream);
      void *outCodecpar = *reinterpret_cast<void **>(outStreamRaw + 0x10);
      if (ff.avcodec_parameters_copy(outCodecpar, codecpar) < 0) {
        ff.avformat_free_context(outFmtCtx);
        ff.avcodec_free_context(&decoderCtx);
        ff.avformat_close_input(&fmtCtx);
        AppendLog("ERROR: Cannot copy codec parameters");
        QMetaObject::invokeMethod(this, [this]() {
          StatusLabel->setText("Copy codecpar failed");
          SetUIEnabled(true);
          m_Encoding = false;
        }, Qt::QueuedConnection);
        return;
      }
      int outCodecId = 0;
      switch (m_Codec) {
      case QSV_CODEC_AVC:
        outCodecId = 26; // AV_CODEC_ID_H264
        break;
      case QSV_CODEC_HEVC:
        outCodecId = 173; // AV_CODEC_ID_HEVC
        break;
      case QSV_CODEC_AV1:
        outCodecId = 227; // AV_CODEC_ID_AV1
        break;
      case QSV_CODEC_VP9:
        outCodecId = 167; // AV_CODEC_ID_VP9
        break;
      }
      auto *outCpRaw = reinterpret_cast<uint8_t *>(outCodecpar);
      *reinterpret_cast<int *>(outCpRaw + 0x00) = 0; // AVMEDIA_TYPE_VIDEO
      *reinterpret_cast<int *>(outCpRaw + 0x04) = outCodecId;
      // Use 90kHz time base to match QSV VPL timestamps.
      *reinterpret_cast<int *>(outStreamRaw + 0x20) = 1;
      *reinterpret_cast<int *>(outStreamRaw + 0x24) = 90000;
    }

    // Open output IO. AVIO_FLAG_WRITE = 2.
    auto *outFmtCtxRaw = reinterpret_cast<uint8_t *>(outFmtCtx);
    int avioRet = ff.avio_open(
        reinterpret_cast<void **>(outFmtCtxRaw + 0x20),
        m_OutputPath.c_str(), 2);
    if (avioRet < 0) {
      ff.avformat_free_context(outFmtCtx);
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot open output IO");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Output IO failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    int whRet = ff.avformat_write_header(outFmtCtx, nullptr);
    if (whRet < 0) {
      ff.avio_closep(reinterpret_cast<void **>(outFmtCtxRaw + 0x20));
      ff.avformat_free_context(outFmtCtx);
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      AppendLog("ERROR: Cannot write output header");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Write header failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    AppendLog("Output muxer opened");

    // 7. Allocate NV12 frame buffer
    int nv12Size = srcWidth * srcHeight * 3 / 2;
    std::vector<uint8_t> nv12Frame(nv12Size);
    int nv12Strides[2] = {srcWidth, srcWidth};
    uint8_t *nv12Data[2] = {nv12Frame.data(),
                            nv12Frame.data() + srcWidth * srcHeight};

    // 8. Frame and packet allocation
    void *frame = ff.av_frame_alloc();
    void *packet = ff.av_packet_alloc();
    void *outPacket = ff.av_packet_alloc();
    if (!frame || !packet || !outPacket) {
      ff.av_frame_free(&frame);
      ff.av_packet_free(&packet);
      ff.av_packet_free(&outPacket);
      ff.avcodec_free_context(&decoderCtx);
      ff.avformat_close_input(&fmtCtx);
      ff.avio_closep(reinterpret_cast<void **>(outFmtCtxRaw + 0x20));
      ff.avformat_free_context(outFmtCtx);
      AppendLog("ERROR: Cannot allocate frame/packet");
      QMetaObject::invokeMethod(this, [this]() {
        StatusLabel->setText("Alloc failed");
        SetUIEnabled(true);
        m_Encoding = false;
      }, Qt::QueuedConnection);
      return;
    }

    // Helper to fill an AVPacket for the muxer from a QSV bitstream.
    auto fillOutPacket = [&ff, outPacket](mfxBitstream *bs, int64_t pts90k,
                                          int64_t duration90k, bool key) {
      // Detach any buffer owned by the packet; we only borrow QSV data.
      ff.av_packet_unref(outPacket);
      auto *pktRaw = reinterpret_cast<uint8_t *>(outPacket);
      *reinterpret_cast<void **>(pktRaw + 0x00) = nullptr;         // buf
      *reinterpret_cast<int64_t *>(pktRaw + 0x08) = pts90k;        // pts
      *reinterpret_cast<int64_t *>(pktRaw + 0x10) = pts90k;        // dts
      *reinterpret_cast<uint8_t **>(pktRaw + 0x18) =
          bs->Data + bs->DataOffset;                               // data
      *reinterpret_cast<int *>(pktRaw + 0x20) =
          static_cast<int>(bs->DataLength);                        // size
      *reinterpret_cast<int *>(pktRaw + 0x24) = 0;                 // stream_index
      *reinterpret_cast<int *>(pktRaw + 0x28) = key ? 1 : 0;       // flags
      *reinterpret_cast<int64_t *>(pktRaw + 0x40) = duration90k;   // duration
    };

    // Helper to write one QSV bitstream into the output muxer.
    auto writeBitstream = [&ff, &fillOutPacket, outFmtCtx, outPacket](
                              mfxBitstream *bs, int64_t pts90k,
                              int64_t duration90k) {
      bool key = (bs->FrameType &
                  (MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR)) != 0;
      fillOutPacket(bs, pts90k, duration90k, key);
      int muxRet = ff.av_interleaved_write_frame(outFmtCtx, outPacket);
      if (muxRet < 0) {
        blog(LOG_ERROR,
             "[QSV VPL ReEncoder] av_interleaved_write_frame failed: %d",
             muxRet);
      }
      return muxRet >= 0;
    };

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
      int ret = ff.av_read_frame(fmtCtx, packet);
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
        if (m_StopRequested)
          break;
        ret = ff.avcodec_receive_frame(decoderCtx, frame);
        if (ret < 0)
          break;

        // Get pixel format from AVFrame
        auto *fv = reinterpret_cast<const AVFrameView *>(frame);
        int srcFormat = fv->format;
        blog(LOG_INFO,
             "[QSV VPL ReEncoder] Decoded frame format=%d, data[0]=%p, "
             "linesize[0]=%d",
             srcFormat, static_cast<void *>(fv->data[0]), fv->linesize[0]);

        // Sanity checks before passing possibly bogus pointers to swscale.
        if (srcFormat < 0 || srcFormat > 0xFFFF) {
          AppendLog(QString("ERROR: Invalid pixel format %1, aborting").arg(srcFormat));
          break;
        }
        if (!fv->data[0] || fv->linesize[0] <= 0 || fv->linesize[0] > 32768) {
          AppendLog(QString("ERROR: Invalid frame data=%1 linesize=%2, aborting")
                        .arg(reinterpret_cast<quintptr>(fv->data[0]))
                        .arg(fv->linesize[0]));
          break;
        }

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

        // Encode frame (submitted asynchronously)
        int64_t pts90k = pts * TS_MULT * fpsDen / fpsNum;
        try {
          mfxBitstream *encBS = nullptr;
          mfxStatus sts = encoder->EncodeFrame(
              static_cast<mfxU64>(pts90k), nv12Data, (uint32_t *)nv12Strides,
              &encBS);

          // EncodeFrame may return a completed frame when the task pool is full.
          if (encBS && encBS->DataLength > 0) {
            writeBitstream(encBS, static_cast<int64_t>(encBS->TimeStamp),
                           TS_MULT * fpsDen / fpsNum);
            frameCount++;
          }

          // Poll for additional completed frames from the async pipeline.
          mfxBitstream *syncBS = nullptr;
          while (encoder->SyncAndSwapPendingTask(&syncBS) == MFX_ERR_NONE) {
            if (m_StopRequested)
              break;
            if (syncBS && syncBS->DataLength > 0) {
              writeBitstream(syncBS, static_cast<int64_t>(syncBS->TimeStamp),
                             TS_MULT * fpsDen / fpsNum);
              frameCount++;
            }
          }

          if (sts == MFX_ERR_MORE_DATA) {
            // VPP consumed input but produced no output yet; continue.
          }
        } catch (const std::exception &e) {
          AppendLog(QString("Encode error: %1").arg(e.what()));
          break;
        }

        pts++;
        m_EncodedFrames = frameCount;
        UpdateProgress(frameCount, m_TotalFrames > 0 ? m_TotalFrames : 0);
      }
    }

    // 10. Drain encoder
    AppendLog("Draining encoder...");
    try {
      while (true) {
        if (m_StopRequested) {
          AppendLog("Drain aborted by user.");
          break;
        }
        mfxBitstream *drainBS = nullptr;
        mfxStatus sts = encoder->DrainAndRetrieveBitstream(&drainBS);
        if (sts == MFX_ERR_MORE_DATA)
          break;
        if (sts >= MFX_ERR_NONE && drainBS && drainBS->DataLength > 0) {
          writeBitstream(drainBS, static_cast<int64_t>(drainBS->TimeStamp),
                         TS_MULT * fpsDen / fpsNum);
          frameCount++;
        }
      }
    } catch (const std::exception &e) {
      AppendLog(QString("Drain warning: %1").arg(e.what()));
    }

    // 11. Cleanup
    try {
      if (outFmtCtx) {
        ff.av_write_trailer(outFmtCtx);
        auto *cleanupFmtCtxRaw = reinterpret_cast<uint8_t *>(outFmtCtx);
        ff.avio_closep(reinterpret_cast<void **>(cleanupFmtCtxRaw + 0x20));
        ff.avformat_free_context(outFmtCtx);
      }
    } catch (...) {
      // Cleanup must not throw; swallow any secondary errors.
    }

    if (swsCtx)
      ff.sws_freeContext(swsCtx);
    ff.av_frame_free(&frame);
    ff.av_packet_free(&packet);
    ff.av_packet_free(&outPacket);
    ff.avcodec_free_context(&decoderCtx);
    ff.avformat_close_input(&fmtCtx);

    encoder->ClearData();

    m_EncodedFrames = frameCount;
    AppendLog(QString("Re-encode complete: %1 frames encoded").arg(frameCount));
    AppendLog(QString("Output: %1").arg(QString::fromStdString(m_OutputPath)));

    QMetaObject::invokeMethod(this, [this, frameCount]() {
      StatusLabel->setText(QString("Complete: %1 frames").arg(frameCount));
      ProgressBar->setValue(100);
      StartStopBtn->setText(obs_module_text("ReEncoderStart"));
      SetUIEnabled(true);
      m_Encoding = false;
    }, Qt::QueuedConnection);

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