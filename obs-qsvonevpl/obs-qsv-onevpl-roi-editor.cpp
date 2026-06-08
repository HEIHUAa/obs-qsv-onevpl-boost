#include "obs-qsv-onevpl-roi-editor.hpp"
#include <QWindow>
#include <QTimer>
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
  ModeGroup->addButton(QPDeltaRadio, 0);
  ModeGroup->addButton(PriorityRadio, 1);
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
      "0 0 0.5 0.5 -10\n0.5 0 1 0.5 5\n0 0.5 0.5 1 -3");
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
}

ROIDialog::~ROIDialog() {
  DestroyPreview();
}

void ROIDialog::showEvent(QShowEvent *Event) {
  QDialog::showEvent(Event);
  // Delay preview creation to next event loop iteration so the widget
  // has a valid native window handle
  QTimer::singleShot(0, this, [this]() {
    if (!PreviewDisplay)
      CreatePreview();
  });
  // Start periodic refresh timer
  RefreshTimer->start();
}

void ROIDialog::closeEvent(QCloseEvent *Event) {
  RefreshTimer->stop();
  DestroyPreview();
  QDialog::closeEvent(Event);
}

// ---------------------------------------------------------------------------
// Preview (obs_display)
// ---------------------------------------------------------------------------
bool ROIDialog::CreatePreview() {
  DestroyPreview();

  if (!PreviewWidget || !PreviewWidget->isVisible())
    return false;

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
  if (!PreviewDisplay)
    return false;

  // Add draw callback for the display
  obs_display_add_draw_callback(PreviewDisplay, PreviewDraw, this);
  obs_display_set_enabled(PreviewDisplay, true);

  return true;
}

void ROIDialog::DestroyPreview() {
  if (PreviewDisplay) {
    obs_display_remove_draw_callback(PreviewDisplay, PreviewDraw, this);
    obs_display_destroy(PreviewDisplay);
    PreviewDisplay = nullptr;
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

// ---------------------------------------------------------------------------
// Preview draw callback (called on OBS render thread)
// ---------------------------------------------------------------------------
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
  dialog->DrawROIOverlay(cx, cy);
}

// ── Draw a list of ROI rects (all share the same viewport mapping) ──
static void DrawROIRects(
    const std::vector<encoder_params::roi_region> &Rects,
    float vp_x, float vp_y, float vp_w, float vp_h,
    float out_w, float out_h, float cx, float cy) {
  gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
  if (!solid)
    return;
  gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
  if (!tech)
    return;
  gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");

  gs_technique_begin(tech);
  gs_technique_begin_pass(tech, 0);

  for (auto &r : Rects) {
    // Map from output resolution → preview widget pixel coords
    float x1 = vp_x + (float)r.Left * (vp_w / out_w);
    float y1 = vp_y + (float)r.Top * (vp_h / out_h);
    float x2 = vp_x + (float)r.Right * (vp_w / out_w);
    float y2 = vp_y + (float)r.Bottom * (vp_h / out_h);

    // Clamp
    auto clamp = [](float v, float lo, float hi) {
      return v < lo ? lo : (v > hi ? hi : v);
    };
    x1 = clamp(x1, 0.0f, (float)cx);
    y1 = clamp(y1, 0.0f, (float)cy);
    x2 = clamp(x2, 0.0f, (float)cx);
    y2 = clamp(y2, 0.0f, (float)cy);
    if (x2 <= x1 || y2 <= y1)
      continue;

    // Color: Red = negative DeltaQP (higher quality), Green = positive (lower quality)
    float intensity = std::min((float)std::abs(r.DeltaQP) / 25.0f, 1.0f);
    intensity = std::max(intensity, 0.3f); // minimum visibility
    vec4 color;
    vec4_zero(&color);
    if (r.DeltaQP < 0)
      vec4_set(&color, intensity, 0.1f, 0.1f, 0.35f);
    else
      vec4_set(&color, 0.1f * (1.0f - intensity), intensity, 0.1f, 0.35f);

    gs_effect_set_vec4(color_param, &color);

    struct gs_vb_data *vbd = gs_vbdata_create();
    vbd->num = 4;
    vbd->points = (struct vec3 *)bzalloc(sizeof(struct vec3) * 4);
    vbd->points[0].x = x1; vbd->points[0].y = y1; vbd->points[0].z = 0.0f;
    vbd->points[1].x = x2; vbd->points[1].y = y1; vbd->points[1].z = 0.0f;
    vbd->points[2].x = x1; vbd->points[2].y = y2; vbd->points[2].z = 0.0f;
    vbd->points[3].x = x2; vbd->points[3].y = y2; vbd->points[3].z = 0.0f;

    gs_vertbuffer_t *vb = gs_vertexbuffer_create(vbd, GS_DYNAMIC);
    if (vb) {
      gs_load_vertexbuffer(vb);
      gs_draw(GS_TRISTRIP, 0, 4);
      gs_vertexbuffer_destroy(vb);
    }
  }

  gs_technique_end_pass(tech);
  gs_technique_end(tech);
}

void ROIDialog::DrawROIOverlay(uint32_t cx, uint32_t cy) {
  // --- 1. Read normalized ROI data; convert to pixel for display ---
  bool enabled = false;
  std::vector<encoder_params::roi_region> regions; // pixel values for drawing

  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    enabled = GlobalROIConfig.Enabled;
    if (!GlobalROIConfig.NormalizedRegions.empty()) {
      // Get output resolution for normalized→pixel conversion
      struct obs_video_info ovi;
      obs_get_video_info(&ovi);
      mfxU16 outW = (mfxU16)ovi.output_width;
      mfxU16 outH = (mfxU16)ovi.output_height;
      if (outW > 0 && outH > 0) {
        regions = NormalizeROIToPixel(GlobalROIConfig.NormalizedRegions,
                                       outW, outH);
      }
    }
  }

  if (!enabled || regions.empty())
    return;

  // --- 2. Get output/base dimensions ---
  struct obs_video_info ovi;
  obs_get_video_info(&ovi);
  if (ovi.output_width < 1 || ovi.output_height < 1 ||
      ovi.base_width < 1 || ovi.base_height < 1)
    return;

  float out_w = (float)ovi.output_width;
  float out_h = (float)ovi.output_height;
  float base_w = (float)ovi.base_width;
  float base_h = (float)ovi.base_height;

  // Viewport (same as PreviewDraw)
  float scale = std::min((float)cx / base_w, (float)cy / base_h);
  float vp_w = base_w * scale;
  float vp_h = base_h * scale;
  float vp_x = ((float)cx - vp_w) * 0.5f;
  float vp_y = ((float)cy - vp_h) * 0.5f;

  // --- 3. Region segmentation for overlap resolution ---
  // Collect unique x/y boundaries from all ROI rectangles
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

  // Build a grid of cells; each cell gets the composite DeltaQP of
  // all ROIs covering it.  Cells with sumQP == 0 are skipped.
  // If no cells have net effect, fall back to drawing raw regions.
  std::vector<encoder_params::roi_region> drawRects;
  bool useSegmented = false;

  for (size_t yi = 0; yi + 1 < ys.size(); yi++) {
    for (size_t xi = 0; xi + 1 < xs.size(); xi++) {
      mfxI16 sumQP = 0;
      for (auto &r : regions) {
        if (xs[xi] >= r.Left && xs[xi + 1] <= r.Right &&
            ys[yi] >= r.Top && ys[yi + 1] <= r.Bottom) {
          sumQP += r.DeltaQP;
        }
      }
      if (sumQP == 0)
        continue;
      useSegmented = true;
      drawRects.push_back({xs[xi], ys[yi], xs[xi + 1], ys[yi + 1], sumQP});
    }
  }

  if (!useSegmented)
    drawRects = regions;

  // --- 4. Draw all rects through the shared helper ---
  DrawROIRects(drawRects, vp_x, vp_y, vp_w, vp_h, out_w, out_h, cx, cy);
}

// ── Helper: convert normalized regions → space-separated UI text ─────
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
  }
  return text;
}

// ── Helper: populate UI controls from GlobalROIConfig ────────────────
void ROIDialog::SetUIFromGlobalConfig() {
  std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
  ROIEnableCheck->setChecked(GlobalROIConfig.Enabled);
  if (GlobalROIConfig.Mode == 1)
    PriorityRadio->setChecked(true);
  else
    QPDeltaRadio->setChecked(true);

  ROITextEdit->setPlainText(
      QString::fromStdString(RegionsToUIFormat(GlobalROIConfig.NormalizedRegions)));
}

// ── ROI data load / save
// ----------------------------------------------------------------------
void ROIDialog::UpdatePreviewFromText() {
  // Parse text input into normalized ROI regions (0-1 floats)
  std::vector<encoder_params::normalized_roi_region> normRegions;
  std::string text = ROITextEdit->toPlainText().toStdString();
  std::istringstream stream(text);
  std::string line;

  while (std::getline(stream, line)) {
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    encoder_params::normalized_roi_region nr = {};
    double l = 0, t = 0, r = 0, b = 0;
    int dqp = 0;
    if (std::sscanf(line.c_str(), "%lf %lf %lf %lf %d",
                     &l, &t, &r, &b, &dqp) == 5) {
      nr.Left = l;
      nr.Top = t;
      nr.Right = r;
      nr.Bottom = b;
      nr.DeltaQP = (mfxI16)dqp;
      normRegions.push_back(nr);
    }
  }

  mfxU16 mode = (ModeGroup->checkedId() == 1) ? 1 : 0;
  bool enabled = ROIEnableCheck->isChecked();

  // Update GlobalROIConfig for DrawROIOverlay to pick up
  {
    std::lock_guard<std::mutex> lock(GlobalROIConfigMutex);
    GlobalROIConfig.NormalizedRegions = normRegions;
    GlobalROIConfig.Mode = mode;
    GlobalROIConfig.Enabled = enabled;
  }

  ForceRefreshPreview();
}

void ROIDialog::LoadROIData() {
  // Always load from file to avoid stale GlobalROIConfig cache issues
  if (LoadROIConfigFromFile()) {
    SetUIFromGlobalConfig();
    return;
  }

  // No saved config - use defaults
  ROIEnableCheck->setChecked(false);
  QPDeltaRadio->setChecked(true);
  ROITextEdit->clear();
}

void ROIDialog::SaveROIData() {
  // Parse text input into normalized ROI regions (0-1 floats)
  std::vector<encoder_params::normalized_roi_region> normRegions;
  std::string text = ROITextEdit->toPlainText().toStdString();
  std::istringstream stream(text);
  std::string line;

  while (std::getline(stream, line)) {
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    encoder_params::normalized_roi_region nr = {};
    double l = 0, t = 0, r = 0, b = 0;
    int dqp = 0;
    // Left Top Right Bottom are 0-1 floats; DeltaQP stays as signed int
    int parsed = std::sscanf(line.c_str(), "%lf %lf %lf %lf %d",
                              &l, &t, &r, &b, &dqp);
    if (parsed == 5) {
      nr.Left = l;
      nr.Top = t;
      nr.Right = r;
      nr.Bottom = b;
      nr.DeltaQP = (mfxI16)dqp;
      normRegions.push_back(nr);
    }
  }

  mfxU16 mode = (ModeGroup->checkedId() == 1) ? 1 : 0;
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

// ---------------------------------------------------------------------------
// Frontend menu callback
// ---------------------------------------------------------------------------

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
  auto *dialog = new ROIDialog();
  dialog->setAttribute(Qt::WA_DeleteOnClose);

  // Track active dialog for profile-change refresh
  ActiveDialog = dialog;
  QObject::connect(dialog, &QObject::destroyed, []() {
    ActiveDialog = nullptr;
  });

  dialog->show();
}

void RegisterROIEditor() {
  obs_frontend_add_tools_menu_item(obs_module_text("ROIEditor"),
                                    OpenROIEditor, nullptr);
  obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}