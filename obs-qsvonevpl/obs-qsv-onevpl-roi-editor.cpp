#include "obs-qsv-onevpl-roi-editor.hpp"
#include "helpers/common_utils.hpp"
#include <QWindow>
#include <QTimer>
#include <functional>
#include <mutex>
#include <sstream>
#include <cstdio>

// Converts 0-1 normalized coordinates to pixel values per-encoder
static void ApplyROIToAllActiveEncoders(
    const std::vector<encoder_params::normalized_roi_region> &NormRegions,
    mfxU16 Mode, bool Enabled) {
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
  for (auto &pair : EncoderDataMap) {
    ApplyROIConfigToEncoder(pair.second, NormRegions, Mode, Enabled);
  }
}

ROIDialog::ROIDialog(QWidget *Parent)
    : QDialog(Parent),
      PreviewDisplay(nullptr),
      PreviewWidget(nullptr),
      RefreshTimer(new QTimer(this)) {
  blog(LOG_DEBUG, "[QSV VPL] ROIDialog constructor started");

  setWindowTitle(obs_module_text("ROIEditor"));
  setMinimumSize(860, 680);
  resize(960, 720);

  auto *MainLayout = new QVBoxLayout(this);

  // === Info label ===
  InfoLabel = new QLabel(obs_module_text("ROIEditorDesc"), this);
  InfoLabel->setWordWrap(true);
  MainLayout->addWidget(InfoLabel);

  // === Preview canvas (OBS display) ===
  auto *PreviewGroup = new QGroupBox(obs_module_text("ROIPreview"), this);
  auto *PreviewLayout = new QVBoxLayout(PreviewGroup);
  PreviewWidget = new QWidget(this);
  PreviewWidget->setMinimumSize(1, 1);
  PreviewWidget->setStyleSheet("background-color: black;");
  PreviewLayout->addWidget(PreviewWidget);
  MainLayout->addWidget(PreviewGroup, 1); // give preview majority of vertical space

  // === Controls row: enable toggle + always-on-top ===
  auto *ControlsLayout = new QHBoxLayout();
  ROIEnableCheck = new QCheckBox(obs_module_text("ROIEnabled"), this);
  ROIEnableCheck->setChecked(true);
  ControlsLayout->addWidget(ROIEnableCheck);
  ControlsLayout->addStretch();
  AlwaysOnTopCheck = new QCheckBox(obs_module_text("AlwaysOnTop"), this);
  AlwaysOnTopCheck->setChecked(false);
  ControlsLayout->addWidget(AlwaysOnTopCheck);
  MainLayout->addLayout(ControlsLayout);

  // === ROI Mode selection ===
  auto *ModeGroupBox = new QGroupBox(obs_module_text("ROIMode"), this);
  auto *ModeLayout = new QVBoxLayout(ModeGroupBox);
  ModeGroup = new QButtonGroup(this);
  QPDeltaRadio = new QRadioButton(obs_module_text("ROIModeQPDelta"), this);
  PriorityRadio = new QRadioButton(obs_module_text("ROIModePriority"), this);
  ModeGroup->addButton(PriorityRadio, 0);   // id=0 → MFX_ROI_MODE_PRIORITY
  ModeGroup->addButton(QPDeltaRadio, 1);    // id=1 → MFX_ROI_MODE_QP_DELTA
  QPDeltaRadio->setChecked(true);
  ModeLayout->addWidget(QPDeltaRadio);
  ModeLayout->addWidget(PriorityRadio);
  MainLayout->addWidget(ModeGroupBox);

  // === ROI text input ===
  auto *ROIGroup = new QGroupBox(obs_module_text("ROIRegions"), this);
  auto *ROILayout = new QVBoxLayout(ROIGroup);
  FormatLabel = new QLabel(obs_module_text("ROIRegionFormat"), this);
  ROILayout->addWidget(FormatLabel);
  auto *ExampleLabel = new QLabel(obs_module_text("ROIRegionExample"), this);
  ExampleLabel->setStyleSheet("color: gray; font-style: italic;");
  ROILayout->addWidget(ExampleLabel);
  ROITextEdit = new QTextEdit(this);
  ROITextEdit->setPlaceholderText(
      "0 0 0.5 0.5 -10\n0.5 0 1 0.5 5\n0.1 0.1 0.9 0.9 6 0.1 0.1 0.1 0.1 0.1");
  ROITextEdit->setFont(QFont("Consolas", 10));
  ROILayout->addWidget(ROITextEdit);
  MainLayout->addWidget(ROIGroup);

  // === Buttons ===
  auto *ButtonLayout = new QHBoxLayout();
  ButtonLayout->addStretch();
  ApplyButton = new QPushButton(obs_module_text("ROIApply"), this);
  CancelButton = new QPushButton(obs_module_text("ROICancel"), this);
  ButtonLayout->addWidget(ApplyButton);
  ButtonLayout->addWidget(CancelButton);
  MainLayout->addLayout(ButtonLayout);

  // Connections
  connect(ApplyButton, &QPushButton::clicked, this, &ROIDialog::OnApplyClicked);
  connect(CancelButton, &QPushButton::clicked, this,
          &ROIDialog::OnCancelClicked);
  connect(AlwaysOnTopCheck, &QCheckBox::checkStateChanged, this,
          &ROIDialog::OnToggleAlwaysOnTop);
  connect(ROITextEdit, &QTextEdit::textChanged, this, [this]() {
    UpdatePreviewFromText();
  });

  // Install event filter on preview widget for resize handling
  PreviewWidget->installEventFilter(this);

  // Periodic refresh timer: forces obs_display to redraw every ~100ms
  RefreshTimer->setInterval(100);
  connect(RefreshTimer, &QTimer::timeout, this, [this]() {
    ForceRefreshPreview();
  });

  // Load saved ROI data (if any)
  LoadROIData();

  blog(LOG_DEBUG, "[QSV VPL] ROIDialog constructor finished");
}

ROIDialog::~ROIDialog() {
  DestroyPreview();
}

void ROIDialog::showEvent(QShowEvent *Event) {
  QDialog::showEvent(Event);
  // Delay preview creation to next event loop iteration so the widget
  // has a valid native window handle
  QTimer::singleShot(0, this, [this]() {
    if (!PreviewDisplay) {
      if (!CreatePreview()) {
        blog(LOG_WARNING, "[QSV VPL] ROI dialog: CreatePreview failed on show");
      }
    }
  });
  // Start periodic refresh timer
  RefreshTimer->start();
}

void ROIDialog::closeEvent(QCloseEvent *Event) {
  RefreshTimer->stop();
  DestroyPreview();
  QDialog::closeEvent(Event);
}

// Preview (obs_display)
bool ROIDialog::CreatePreview() {
  DestroyPreview();

  if (!PreviewWidget || !PreviewWidget->isVisible())
    return false;

  try {
    // Force creation of native window handle
    PreviewWidget->setAttribute(Qt::WA_NativeWindow);
    PreviewWidget->winId();

    HWND hwnd = (HWND)PreviewWidget->winId();
    if (!hwnd)
      return false;

    // Build gs_init_data for obs_display_create
    struct obs_video_info ovi;
    obs_get_video_info(&ovi);

    gs_init_data init_data = {};
    gs_window window = {};
    window.hwnd = hwnd;
    init_data.window = window;
    init_data.cx = (uint32_t)PreviewWidget->width();
    init_data.cy = (uint32_t)PreviewWidget->height();
    init_data.num_backbuffers = 2;
    init_data.format = GS_BGRA;
    init_data.zsformat = GS_ZS_NONE;
    init_data.adapter = ovi.adapter;

    PreviewDisplay = obs_display_create(&init_data, 0);
    if (!PreviewDisplay) {
      blog(LOG_WARNING, "[QSV VPL] ROI CreatePreview: obs_display_create returned NULL");
      return false;
    }

    // Add draw callback for the display
    obs_display_add_draw_callback(PreviewDisplay, PreviewDraw, this);
    obs_display_set_enabled(PreviewDisplay, true);

    return true;

  } catch (const std::exception &e) {
    blog(LOG_ERROR,
         "[QSV VPL] ROI CreatePreview: std::exception caught: %s", e.what());
    DestroyPreview();
    return false;
  } catch (...) {
    blog(LOG_ERROR,
         "[QSV VPL] ROI CreatePreview: unknown exception caught");
    DestroyPreview();
    return false;
  }
}

void ROIDialog::DestroyPreview() {
  if (PreviewDisplay) {
    obs_display_remove_draw_callback(PreviewDisplay, PreviewDraw, this);
    obs_display_destroy(PreviewDisplay);
    PreviewDisplay = nullptr;
  }
  if (m_CachedVB) {
    gs_vertexbuffer_destroy(m_CachedVB);
    m_CachedVB = nullptr;
    m_CachedVBCapacity = 0;
  }
}

void ROIDialog::ResizePreview() {
  if (PreviewDisplay && PreviewWidget) {
    obs_display_resize(PreviewDisplay,
                       (uint32_t)PreviewWidget->width(),
                       (uint32_t)PreviewWidget->height());
  }
}

void ROIDialog::ForceRefreshPreview() {
  if (!PreviewDisplay)
    return;
  {
    std::lock_guard<std::mutex> lock(m_CacheMutex);
    if (m_GridCacheHash == 0)
      return; // nothing to draw — skip the pointless toggle
  }
  // Toggle enabled state to force obs_display to re-evaluate and redraw
  obs_display_set_enabled(PreviewDisplay, false);
  obs_display_set_enabled(PreviewDisplay, true);
}

bool ROIDialog::eventFilter(QObject *Obj, QEvent *Event) {
  if (Obj == PreviewWidget && Event->type() == QEvent::Resize) {
    ResizePreview();
  }
  return QDialog::eventFilter(Obj, Event);
}

// Preview draw callback (called on OBS render thread)
void ROIDialog::PreviewDraw(void *param, uint32_t cx, uint32_t cy) {
  auto *dialog = static_cast<ROIDialog *>(param);
  if (!dialog)
    return;

  // Get base video info for aspect-ratio-correct scaling
  struct obs_video_info ovi;
  obs_get_video_info(&ovi);
  if (ovi.base_width < 1 || ovi.base_height < 1)
    return;

  // Calculate centered, scaled viewport (maintain aspect ratio)
  float base_w = (float)ovi.base_width;
  float base_h = (float)ovi.base_height;
  float scale = std::min((float)cx / base_w, (float)cy / base_h);
  int vp_w = (int)(base_w * scale);
  int vp_h = (int)(base_h * scale);
  int vp_x = (int)((cx - vp_w) / 2);
  int vp_y = (int)((cy - vp_h) / 2);

  gs_viewport_push();
  gs_projection_push();

  gs_set_viewport(vp_x, vp_y, vp_w, vp_h);
  gs_ortho(0.0f, base_w, 0.0f, base_h, -100.0f, 100.0f);

  obs_source_t *scene = obs_frontend_get_current_scene();
  if (scene) {
    obs_source_video_render(scene);
    obs_source_release(scene);
  }

  gs_projection_pop();
  gs_viewport_pop();

  // Draw ROI overlay in full widget space (after viewport pop)
  dialog->DrawROIOverlay(cx, cy, ovi);
}

// Draw a list of ROI rects (all share the same viewport mapping)
static void DrawROIRects(
    const std::vector<encoder_params::roi_region> &Rects,
    float vp_x, float vp_y, float vp_w, float vp_h,
    float out_w, float out_h, float cx, float cy,
    gs_vertbuffer_t **CachedVB, size_t *CachedVBCapacity,
    mfxU16 mode = 1) {
  gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
  if (!solid)
    return;
  gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
  if (!tech)
    return;
  gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");

  gs_technique_begin(tech);
  gs_technique_begin_pass(tech, 0);

  // Compute dynamic intensity range from actual DeltaQP values
  float dynMaxAbs = 0.0f;
  for (auto &r : Rects)
    dynMaxAbs = std::max(dynMaxAbs, (float)std::abs(r.DeltaQP));
  if (dynMaxAbs < 1.0f)
    dynMaxAbs = (mode == 0) ? 3.0f : 51.0f; // fallback when all QP are 0

  auto clamp = [](float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  };

  // Reuse one cached GS_DYNAMIC vertex buffer instead of creating and
  // destroying a new buffer per rect on the render thread every frame.
  const size_t needed = Rects.size() * 4;
  if (!*CachedVB || *CachedVBCapacity < needed) {
    if (*CachedVB) {
      gs_vertexbuffer_destroy(*CachedVB);
      *CachedVB = nullptr;
    }
    *CachedVBCapacity = 0;
    struct gs_vb_data *vbd = gs_vbdata_create();
    vbd->num = static_cast<uint32_t>(needed);
    vbd->points = (struct vec3 *)bzalloc(sizeof(struct vec3) * needed);
    *CachedVB = gs_vertexbuffer_create(vbd, GS_DYNAMIC);
    if (!*CachedVB)
      return;
    *CachedVBCapacity = needed;
  }

  struct gs_vb_data *vbd = nullptr;
  gs_vertexbuffer_map(*CachedVB, &vbd);
  if (!vbd)
    return;
  vbd->num = static_cast<uint32_t>(needed);

  size_t vertIdx = 0;
  for (auto &r : Rects) {
    // Map from output resolution → preview widget pixel coords
    float x1 = vp_x + (float)r.Left * (vp_w / out_w);
    float y1 = vp_y + (float)r.Top * (vp_h / out_h);
    float x2 = vp_x + (float)r.Right * (vp_w / out_w);
    float y2 = vp_y + (float)r.Bottom * (vp_h / out_h);

    // Clamp
    x1 = clamp(x1, 0.0f, (float)cx);
    y1 = clamp(y1, 0.0f, (float)cy);
    x2 = clamp(x2, 0.0f, (float)cx);
    y2 = clamp(y2, 0.0f, (float)cy);
    if (x2 <= x1 || y2 <= y1)
      continue;

    struct vec3 *pts = vbd->points + vertIdx * 4;
    pts[0].x = x1; pts[0].y = y1; pts[0].z = 0.0f;
    pts[1].x = x2; pts[1].y = y1; pts[1].z = 0.0f;
    pts[2].x = x1; pts[2].y = y2; pts[2].z = 0.0f;
    pts[3].x = x2; pts[3].y = y2; pts[3].z = 0.0f;
    vertIdx++;
  }
  gs_vertexbuffer_unmap(*CachedVB);

  gs_load_vertexbuffer(*CachedVB);
  size_t rectIdx = 0;
  for (auto &r : Rects) {
    // Recompute geometry to mirror the fill pass above (skip same rects)
    float x1 = vp_x + (float)r.Left * (vp_w / out_w);
    float y1 = vp_y + (float)r.Top * (vp_h / out_h);
    float x2 = vp_x + (float)r.Right * (vp_w / out_w);
    float y2 = vp_y + (float)r.Bottom * (vp_h / out_h);
    x1 = clamp(x1, 0.0f, (float)cx);
    y1 = clamp(y1, 0.0f, (float)cy);
    x2 = clamp(x2, 0.0f, (float)cx);
    y2 = clamp(y2, 0.0f, (float)cy);
    if (x2 <= x1 || y2 <= y1)
      continue;

    // Color depends on mode:
    //   Priority mode (0): bigger value = better quality → positive = green
    //   DeltaQP mode  (1): lesser value = better quality → negative = green
    bool isBetter = (mode == 0) ? (r.DeltaQP > 0) : (r.DeltaQP < 0);
    float absVal = (float)std::abs(r.DeltaQP);
    // Use dynamic range so gradient cells get proportional intensity
    float intensity = std::min(absVal / dynMaxAbs, 1.0f);
    intensity = std::max(intensity, 0.3f); // minimum visibility
    vec4 color;
    vec4_zero(&color);
    if (isBetter)
      vec4_set(&color, 0.1f * (1.0f - intensity), intensity, 0.1f, 0.35f); // Green = better
    else
      vec4_set(&color, intensity, 0.1f, 0.1f, 0.35f); // Red = worse

    gs_effect_set_vec4(color_param, &color);
    gs_draw(GS_TRISTRIP, static_cast<uint32_t>(rectIdx * 4), 4);
    rectIdx++;
  }

  gs_technique_end_pass(tech);
  gs_technique_end(tech);
}

void ROIDialog::InvalidateROICache() {
  // Guarded: called from the UI thread while DrawROIOverlay (render thread)
  // may be reading the same members — a concurrent vector clear/read is UB.
  std::lock_guard<std::mutex> lock(m_CacheMutex);
  m_GridCacheHash = 0;
  m_CachedDrawRects.clear();
  m_CachedUseSegmented = false;
}

void ROIDialog::DrawROIOverlay(uint32_t cx, uint32_t cy,
                                 const struct obs_video_info &ovi) {
  // Quick dimension checks
  if (ovi.output_width < 1 || ovi.output_height < 1 ||
      ovi.base_width < 1 || ovi.base_height < 1)
    return;

  // --- 1. Compute cache key from normalized ROI data + output dims ---
  size_t newHash = 0;
  bool enabled = false;
  mfxU16 previewMode = 1; // default DeltaQP

  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    enabled = GlobalROIConfig.Enabled;
    previewMode = GlobalROIConfig.Mode;

    if (enabled && !GlobalROIConfig.NormalizedRegions.empty()) {
      auto hash_combine = [](size_t &seed, auto val) {
        seed ^= std::hash<decltype(val)>{}(val) + 0x9e3779b9 + (seed << 6) +
                (seed >> 2);
      };
      hash_combine(newHash, ovi.output_width);
      hash_combine(newHash, ovi.output_height);
      for (auto &r : GlobalROIConfig.NormalizedRegions) {
        hash_combine(newHash, std::hash<double>{}(r.Left));
        hash_combine(newHash, std::hash<double>{}(r.Top));
        hash_combine(newHash, std::hash<double>{}(r.Right));
        hash_combine(newHash, std::hash<double>{}(r.Bottom));
        hash_combine(newHash, (size_t)r.DeltaQP);
        hash_combine(newHash, (size_t)r.HasGradient);
        if (r.HasGradient) {
          hash_combine(newHash, std::hash<double>{}(r.GradLeft));
          hash_combine(newHash, std::hash<double>{}(r.GradTop));
          hash_combine(newHash, std::hash<double>{}(r.GradRight));
          hash_combine(newHash, std::hash<double>{}(r.GradBottom));
          hash_combine(newHash, (size_t)r.GradientSteps);
        }
      }
    }
  }

  if (!enabled || newHash == 0) {
    InvalidateROICache();
    return;
  }

  // --- 2. Retrieve or compute the segmented grid ---
  // Local copy — the shared cache may be cleared concurrently by the UI thread
  // (InvalidateROICache from textChanged); never iterate the shared vector.
  std::vector<encoder_params::roi_region> drawRects;
  bool useSegmented = false;
  bool cacheHit = false;
  {
    std::lock_guard<std::mutex> lock(m_CacheMutex);
    if (newHash == m_GridCacheHash && !m_CachedDrawRects.empty()) {
      drawRects = m_CachedDrawRects;
      useSegmented = m_CachedUseSegmented;
      cacheHit = true;
    }
  }

  if (!cacheHit) {
    // Cache miss — recompute
    // Convert normalized regions → pixel (inside lock for data integrity)
    mfxU16 outW = (mfxU16)ovi.output_width;
    mfxU16 outH = (mfxU16)ovi.output_height;
    std::vector<encoder_params::roi_region> regions;
    {
      std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
      regions = NormalizeROIToPixel(GlobalROIConfig.NormalizedRegions,
                                     outW, outH);
    }

    // Expand gradient regions
    regions = ExpandGradientRegions(regions, outW, outH);

    // Region segmentation for overlap resolution
    std::vector<mfxU16> xs, ys;
    for (auto &r : regions) {
      xs.push_back(r.Left);
      xs.push_back(r.Right);
      ys.push_back(r.Top);
      ys.push_back(r.Bottom);
    }
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

    for (size_t yi = 0; yi + 1 < ys.size(); yi++) {
      for (size_t xi = 0; xi + 1 < xs.size(); xi++) {
        mfxI16 cellQP = 0;
        bool covered = false;
        for (size_t ri = 0; ri < regions.size(); ri++) {
          auto &r = regions[ri];
          if (xs[xi] >= r.Left && xs[xi + 1] <= r.Right &&
              ys[yi] >= r.Top && ys[yi + 1] <= r.Bottom) {
            cellQP = r.DeltaQP;
            covered = true;
            break; // lowest index wins
          }
        }
        if (!covered || cellQP == 0)
          continue;
        useSegmented = true;
        drawRects.push_back({xs[xi], ys[yi], xs[xi + 1], ys[yi + 1], cellQP});
      }
    }

    if (!useSegmented)
      drawRects = regions;

    // Store in cache (guarded — UI thread may invalidate concurrently)
    {
      std::lock_guard<std::mutex> lock(m_CacheMutex);
      m_GridCacheHash = newHash;
      m_CachedDrawRects = drawRects;
      m_CachedUseSegmented = useSegmented;
    }
  }

  // --- 3. Viewport (same as PreviewDraw) ---
  float out_w = (float)ovi.output_width;
  float out_h = (float)ovi.output_height;
  float base_w = (float)ovi.base_width;
  float base_h = (float)ovi.base_height;

  float scale = std::min((float)cx / base_w, (float)cy / base_h);
  float vp_w = base_w * scale;
  float vp_h = base_h * scale;
  float vp_x = ((float)cx - vp_w) * 0.5f;
  float vp_y = ((float)cy - vp_h) * 0.5f;

  // --- 4. Draw ---
  DrawROIRects(drawRects, vp_x, vp_y, vp_w, vp_h, out_w, out_h, cx, cy,
               &m_CachedVB, &m_CachedVBCapacity, previewMode);
}

// Convert normalized regions to space-separated UI text
// Includes 4 extra gradient values if HasGradient is true.
static std::string RegionsToUIFormat(
    const std::vector<encoder_params::normalized_roi_region> &Regions) {
  std::string text;
  for (auto &r : Regions) {
    if (!text.empty())
      text += "\n";
    text += FormatROIDouble(r.Left) + " " +
            FormatROIDouble(r.Top) + " " +
            FormatROIDouble(r.Right) + " " +
            FormatROIDouble(r.Bottom) + " " +
            std::to_string(r.DeltaQP);
    if (r.HasGradient) {
      text += " " + FormatROIDouble(r.GradLeft) +
              " " + FormatROIDouble(r.GradTop) +
              " " + FormatROIDouble(r.GradRight) +
              " " + FormatROIDouble(r.GradBottom) +
              " " + std::to_string(r.GradientSteps);
    }
  }
  return text;
}

// Populate UI controls from GlobalROIConfig
void ROIDialog::SetUIFromGlobalConfig() {
  std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
  ROIEnableCheck->setChecked(GlobalROIConfig.Enabled);
  if (GlobalROIConfig.Mode == 0) // MFX_ROI_MODE_PRIORITY
    PriorityRadio->setChecked(true);
  else
    QPDeltaRadio->setChecked(true);

  // Guard against re-entrant locking: setPlainText triggers textChanged,
  // which calls UpdatePreviewFromText, which also locks GlobalROIConfigMutex.
  // On MSVC std::mutex (= SRWLOCK), recursive locking causes a deadlock;
  // on other implementations it can throw std::system_error.
  m_IsSettingText = true;
  ROITextEdit->setPlainText(
      QString::fromStdString(RegionsToUIFormat(GlobalROIConfig.NormalizedRegions)));
  m_IsSettingText = false;
}

// Parse text into normalized ROI regions.
// Format: "L T R B DQP" or "L T R B DQP GradL GradT GradR GradB [Steps]"
// Values 0.0~1.0. Gradients are outward only. Default Steps=3 (7x7 grid).
static std::vector<encoder_params::normalized_roi_region> ParseROIText(
    const std::string &Text) {
  std::vector<encoder_params::normalized_roi_region> result;
  std::istringstream stream(Text);
  std::string line;
  while (std::getline(stream, line)) {
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    // Parse all tokens; must have at least 5 (l t r b dqp)
    std::istringstream ls(line);
    std::vector<double> tokens;
    double val;
    while (ls >> val)
      tokens.push_back(val);
    if (tokens.size() < 5)
      continue;

    encoder_params::normalized_roi_region nr = {};
    nr.Left   = tokens[0];
    nr.Top    = tokens[1];
    nr.Right  = tokens[2];
    nr.Bottom = tokens[3];
    nr.DeltaQP = (mfxI16)tokens[4];

    // If a 6th token (GradLeft) is present, enable gradient
    // (tokens 7-9: GradTop, GradRight, GradBottom are optional, default 0)
    // Gradients are forced to positive (absolute value).
    // Token 10 (optional): GradientSteps, number of subdivisions per side.
    if (tokens.size() >= 6) {
      nr.HasGradient = true;
      nr.GradLeft   = std::abs(tokens[5]);
      if (tokens.size() >= 7) nr.GradTop    = std::abs(tokens[6]);
      if (tokens.size() >= 8) nr.GradRight  = std::abs(tokens[7]);
      if (tokens.size() >= 9) nr.GradBottom = std::abs(tokens[8]);
      if (tokens.size() >= 10) nr.GradientSteps = std::max((int)tokens[9], 1);
    }

    result.push_back(nr);
  }
  return result;
}

// ROI data load / save
void ROIDialog::UpdatePreviewFromText() {
  // Suppress re-entrant calls: SetUIFromGlobalConfig → setPlainText → textChanged.
// The mutex is already held, so re-locking would deadlock.
  if (m_IsSettingText)
    return;

  auto normRegions = ParseROIText(ROITextEdit->toPlainText().toStdString());
  mfxU16 mode = (mfxU16)ModeGroup->checkedId(); // 0=Priority, 1=DeltaQP, matches VPL API
  bool enabled = ROIEnableCheck->isChecked();

  // Update GlobalROIConfig for DrawROIOverlay to pick up
  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    GlobalROIConfig.NormalizedRegions = normRegions;
    GlobalROIConfig.Mode = mode;
    GlobalROIConfig.Enabled = enabled;
  }

  InvalidateROICache();
  ForceRefreshPreview();
}

void ROIDialog::LoadROIData() {
  blog(LOG_DEBUG, "[QSV VPL] ROIDialog::LoadROIData: loading from file...");

  // Always load from file to avoid stale GlobalROIConfig cache issues
  if (LoadROIConfigFromFile()) {
    blog(LOG_DEBUG, "[QSV VPL] ROIDialog::LoadROIData: config loaded from file, applying UI");
    SetUIFromGlobalConfig();
    return;
  }

  blog(LOG_DEBUG, "[QSV VPL] ROIDialog::LoadROIData: no saved config, using defaults");
  // No saved config - use defaults
  ROIEnableCheck->setChecked(false);
  QPDeltaRadio->setChecked(true);
  ROITextEdit->clear();
}

void ROIDialog::SaveROIData() {
  auto normRegions = ParseROIText(ROITextEdit->toPlainText().toStdString());
  mfxU16 mode = (mfxU16)ModeGroup->checkedId(); // 0=Priority, 1=DeltaQP, matches VPL API
  bool enabled = ROIEnableCheck->isChecked();

  // Always save normalized regions to global config (resolution-independent)
  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    GlobalROIConfig.NormalizedRegions = normRegions;
    GlobalROIConfig.Mode = mode;
    GlobalROIConfig.Enabled = enabled;
  }

  // Save to each active encoder's persistent settings (per-profile/scene)
  {
    std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
    for (auto &pair : EncoderDataMap) {
      SaveROIToEncoderSettings(pair.second);
    }
  }

  // ALSO save to file for reliable persistence across OBS restarts
  SaveROIConfigToFile();

  // If any encoder instances exist, apply immediately with per-encoder conversion
  ApplyROIToAllActiveEncoders(normRegions, mode, enabled);

  // Force preview refresh to show updated ROI regions
  ForceRefreshPreview();
  blog(LOG_INFO,
       "[QSV VPL] ROI saved: enabled=%d, normalized regions=%zu, mode=%d",
       (int)enabled, normRegions.size(), (int)mode);
  for (size_t i = 0; i < normRegions.size(); i++) {
    blog(LOG_INFO,
         "[QSV VPL]   NormRegion[%zu]: Left=%.4f Top=%.4f Right=%.4f Bottom=%.4f DeltaQP=%d",
         i, normRegions[i].Left, normRegions[i].Top,
         normRegions[i].Right, normRegions[i].Bottom,
         (int)normRegions[i].DeltaQP);
  }
}

void ROIDialog::OnApplyClicked() {
  SaveROIData();
  accept();
}

void ROIDialog::OnCancelClicked() {
  reject();
}

void ROIDialog::OnToggleAlwaysOnTop(Qt::CheckState State) {
  Qt::WindowFlags flags = windowFlags();
  if (State == Qt::Checked) {
    setWindowFlags(flags | Qt::WindowStaysOnTopHint);
  } else {
    setWindowFlags(flags & ~Qt::WindowStaysOnTopHint);
  }
  // Need to show again after changing window flags
  setVisible(true);
}

// Frontend menu callback
// Track the active ROI dialog instance so we can refresh it on profile change
static ROIDialog *ActiveDialog = nullptr;

// Frontend event callback: reload ROI config when OBS profile changes
static void OnFrontendEvent(obs_frontend_event Event, void *) {
  if (Event != OBS_FRONTEND_EVENT_PROFILE_CHANGED)
    return;

  blog(LOG_INFO, "[QSV VPL] Profile changed, reloading ROI config...");

  // 1. Reload GlobalROIConfig from new profile's INI file
  LoadROIConfigFromFile();

  // 2. Re-apply to all active encoders
  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    if (GlobalROIConfig.Enabled &&
        !GlobalROIConfig.NormalizedRegions.empty()) {
      ApplyROIToAllActiveEncoders(GlobalROIConfig.NormalizedRegions,
                                   GlobalROIConfig.Mode,
                                   GlobalROIConfig.Enabled);
    }
  }

  // 3. If ROI editor dialog is open, refresh its display
  if (ActiveDialog) {
    QMetaObject::invokeMethod(ActiveDialog, [dialog = ActiveDialog]() {
      dialog->LoadROIData();
      dialog->ForceRefreshPreview();
    }, Qt::QueuedConnection);
  }
}

static void OpenROIEditor(void * /*data*/) {
  try {
    auto *dialog = new ROIDialog();
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // Track active dialog for profile-change refresh
    ActiveDialog = dialog;
    QObject::connect(dialog, &QObject::destroyed, []() {
      ActiveDialog = nullptr;
    });

    dialog->show();
    blog(LOG_INFO, "[QSV VPL] ROI editor dialog opened successfully");

  } catch (const std::exception &e) {
    blog(LOG_ERROR,
         "[QSV VPL] OpenROIEditor: std::exception caught: %s", e.what());
  } catch (...) {
    blog(LOG_ERROR,
         "[QSV VPL] OpenROIEditor: unknown exception caught");
  }
}

void RegisterROIEditor() {
  obs_frontend_add_tools_menu_item(obs_module_text("ROIEditor"),
                                    OpenROIEditor, nullptr);
  obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}