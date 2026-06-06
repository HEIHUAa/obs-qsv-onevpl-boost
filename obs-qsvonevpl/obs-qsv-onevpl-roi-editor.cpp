#include "obs-qsv-onevpl-roi-editor.hpp"
#include <QMessageBox>
#include <QWindow>
#include <QTimer>
#include <mutex>
#include <sstream>
#include <cstdio>

// Well-known key for the single global ROI config (instead of per-type keys)
static const char *kDefaultROIConfigKey = "";

// Helper: apply ROI config to all active encoder instances
static void ApplyROIToAllActiveEncoders(
    const std::vector<encoder_params::roi_region> &Regions, mfxU16 Mode,
    bool Enabled) {
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
  for (auto &pair : EncoderDataMap) {
    UpdateEncoderROI(pair.second, Regions, Mode, Enabled);
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
  PreviewWidget->setMinimumSize(320, 240);
  PreviewWidget->setStyleSheet("background-color: black;");
  PreviewLayout->addWidget(PreviewWidget);
  MainLayout->addWidget(PreviewGroup);

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
      "0 0 320 240 -10\n320 0 640 480 -5\n0 240 320 480 5");
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

void ROIDialog::DrawROIOverlay(uint32_t cx, uint32_t cy) {
  // --- 1. Read ROI data from global config ---
  bool enabled = false;
  std::vector<encoder_params::roi_region> regions;

  {
    std::lock_guard<std::mutex> lock(PendingROIMutex);
    auto it = PendingROIConfig.find(kDefaultROIConfigKey);
    if (it != PendingROIConfig.end()) {
      enabled = it->second.Enabled;
      regions = it->second.Regions;
      blog(LOG_DEBUG,
           "[ROI Editor] DrawROIOverlay: reading global config, enabled=%d, regions=%zu",
           (int)enabled, regions.size());
    } else {
      blog(LOG_DEBUG,
           "[ROI Editor] DrawROIOverlay: no global ROI config found");
      return;
    }
  }

  if (!enabled) {
    blog(LOG_DEBUG, "[ROI Editor] DrawROIOverlay: ROI not enabled");
    return;
  }
  if (regions.empty()) {
    blog(LOG_DEBUG, "[ROI Editor] DrawROIOverlay: no regions");
    return;
  }

  // --- 2. Get output/base dimensions ---
  struct obs_video_info ovi;
  obs_get_video_info(&ovi);
  if (ovi.output_width < 1 || ovi.output_height < 1) {
    blog(LOG_DEBUG, "[ROI Editor] DrawROIOverlay: invalid output dimensions");
    return;
  }
  if (ovi.base_width < 1 || ovi.base_height < 1) {
    blog(LOG_DEBUG, "[ROI Editor] DrawROIOverlay: invalid base dimensions");
    return;
  }

  float out_w = (float)ovi.output_width;
  float out_h = (float)ovi.output_height;
  float base_w = (float)ovi.base_width;
  float base_h = (float)ovi.base_height;

  blog(LOG_DEBUG,
       "[ROI Editor] DrawROIOverlay: base=%gx%g output=%gx%g widget=%ux%u",
       base_w, base_h, out_w, out_h, cx, cy);

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

  // Build a grid: for each cell (xs[i]..xs[i+1], ys[j]..ys[j+1]),
  // compute composite DeltaQP = sum of all ROIs covering this cell
  struct Cell {
    mfxU16 x1, y1, x2, y2;
    mfxI16 deltaQP; // composite value
  };
  std::vector<Cell> cells;
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
        continue; // skip cells with no net effect
      cells.push_back({xs[xi], ys[yi], xs[xi + 1], ys[yi + 1], sumQP});
    }
  }

  if (cells.empty()) {
    blog(LOG_DEBUG, "[ROI Editor] DrawROIOverlay: cells empty after segmentation, drawing raw regions");

    // --- Fallback: draw raw regions if no overlap ---
    gs_effect_t *solid_raw = obs_get_base_effect(OBS_EFFECT_SOLID);
    if (!solid_raw)
      return;
    gs_technique_t *tech_raw = gs_effect_get_technique(solid_raw, "Solid");
    if (!tech_raw)
      return;
    gs_eparam_t *color_param_raw = gs_effect_get_param_by_name(solid_raw, "color");

    gs_technique_begin(tech_raw);
    gs_technique_begin_pass(tech_raw, 0);

    for (auto &r : regions) {
      float x1 = vp_x + (float)r.Left * (vp_w / out_w);
      float y1 = vp_y + (float)r.Top * (vp_h / out_h);
      float x2 = vp_x + (float)r.Right * (vp_w / out_w);
      float y2 = vp_y + (float)r.Bottom * (vp_h / out_h);

      x1 = (x1 < 0) ? 0 : (x1 > (float)cx ? (float)cx : x1);
      y1 = (y1 < 0) ? 0 : (y1 > (float)cy ? (float)cy : y1);
      x2 = (x2 < 0) ? 0 : (x2 > (float)cx ? (float)cx : x2);
      y2 = (y2 < 0) ? 0 : (y2 > (float)cy ? (float)cy : y2);
      if (x2 <= x1 || y2 <= y1)
        continue;

      vec4 color;
      vec4_zero(&color);
      float intensity = std::min((float)std::abs(r.DeltaQP) / 25.0f, 1.0f);
      intensity = std::max(intensity, 0.3f);
      float alpha = 0.35f;

      if (r.DeltaQP < 0) {
        // Red (减QP = 更高质量)
        vec4_set(&color, intensity, 0.1f, 0.1f, alpha);
      } else {
        // Green (加QP = 更低质量)
        vec4_set(&color, 0.1f * (1.0f - intensity), intensity, 0.1f, alpha);
      }

      gs_effect_set_vec4(color_param_raw, &color);

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

    gs_technique_end_pass(tech_raw);
    gs_technique_end(tech_raw);
    return;
  }

  blog(LOG_DEBUG,
       "[ROI Editor] DrawROIOverlay: drawing %zu segmented cells",
       cells.size());

  // --- 4. Draw with solid effect ---
  gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
  if (!solid)
    return;
  gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
  if (!tech)
    return;
  gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");

  gs_technique_begin(tech);
  gs_technique_begin_pass(tech, 0);

  for (auto &cell : cells) {
    // Map cell from output resolution → preview widget pixel coords
    float x1 = vp_x + (float)cell.x1 * (vp_w / out_w);
    float y1 = vp_y + (float)cell.y1 * (vp_h / out_h);
    float x2 = vp_x + (float)cell.x2 * (vp_w / out_w);
    float y2 = vp_y + (float)cell.y2 * (vp_h / out_h);

    // Clamp
    x1 = (x1 < 0) ? 0 : (x1 > (float)cx ? (float)cx : x1);
    y1 = (y1 < 0) ? 0 : (y1 > (float)cy ? (float)cy : y1);
    x2 = (x2 < 0) ? 0 : (x2 > (float)cx ? (float)cx : x2);
    y2 = (y2 < 0) ? 0 : (y2 > (float)cy ? (float)cy : y2);
    if (x2 <= x1 || y2 <= y1)
      continue;

    // Color: Red = negative DeltaQP (higher quality/bitrate)
    //        Green = positive DeltaQP (lower quality/bitrate)
    //        Intensity scales with |DeltaQP|
    vec4 color;
    vec4_zero(&color);
    float intensity = std::min((float)std::abs(cell.deltaQP) / 25.0f, 1.0f);
    intensity = std::max(intensity, 0.3f); // minimum visibility
    float alpha = 0.35f;

    if (cell.deltaQP < 0) {
      // Red (减QP = 更高质量): intensity controls redness
      vec4_set(&color, intensity, 0.1f, 0.1f, alpha);
    } else {
      // Green (加QP = 更低质量): intensity controls greenness
      vec4_set(&color, 0.1f * (1.0f - intensity), intensity, 0.1f, alpha);
    }

    gs_effect_set_vec4(color_param, &color);

    struct gs_vb_data *vbd = gs_vbdata_create();
    vbd->num = 4;
    vbd->points = (struct vec3 *)bzalloc(sizeof(struct vec3) * 4);
    vbd->points[0].x = x1;
    vbd->points[0].y = y1;
    vbd->points[0].z = 0.0f;
    vbd->points[1].x = x2;
    vbd->points[1].y = y1;
    vbd->points[1].z = 0.0f;
    vbd->points[2].x = x1;
    vbd->points[2].y = y2;
    vbd->points[2].z = 0.0f;
    vbd->points[3].x = x2;
    vbd->points[3].y = y2;
    vbd->points[3].z = 0.0f;

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

// ---------------------------------------------------------------------------
// ROI data load / save
// ---------------------------------------------------------------------------
void ROIDialog::LoadROIData() {
  // Load from global pending config (always the source of truth)
  std::lock_guard<std::mutex> lock(PendingROIMutex);
  auto it = PendingROIConfig.find(kDefaultROIConfigKey);
  if (it != PendingROIConfig.end()) {
    ROIEnableCheck->setChecked(it->second.Enabled);
    if (it->second.Mode == 1)
      PriorityRadio->setChecked(true);
    else
      QPDeltaRadio->setChecked(true);

    std::string text;
    for (auto &r : it->second.Regions) {
      if (!text.empty())
        text += "\n";
      text += std::to_string(r.Left) + " " +
              std::to_string(r.Top) + " " +
              std::to_string(r.Right) + " " +
              std::to_string(r.Bottom) + " " +
              std::to_string(r.DeltaQP);
    }
    ROITextEdit->setPlainText(QString::fromStdString(text));
    blog(LOG_DEBUG,
         "[ROI Editor] LoadROIData: loaded global config, enabled=%d, regions=%zu",
         (int)it->second.Enabled, it->second.Regions.size());
  } else {
    // No saved config - use defaults
    ROIEnableCheck->setChecked(false);
    QPDeltaRadio->setChecked(true);
    ROITextEdit->clear();
    blog(LOG_DEBUG,
         "[ROI Editor] LoadROIData: no global config found, using defaults");
  }
}

void ROIDialog::SaveROIData() {
  // Parse text input into ROI regions
  std::vector<encoder_params::roi_region> regions;
  std::string text = ROITextEdit->toPlainText().toStdString();
  std::istringstream stream(text);
  std::string line;

  while (std::getline(stream, line)) {
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    encoder_params::roi_region region = {};
    int parsed = std::sscanf(line.c_str(), "%hu %hu %hu %hu %hd",
                              &region.Left, &region.Top, &region.Right,
                              &region.Bottom, &region.DeltaQP);
    if (parsed == 5)
      regions.push_back(region);
  }

  mfxU16 mode = (ModeGroup->checkedId() == 1) ? 1 : 0;
  bool enabled = ROIEnableCheck->isChecked();

  // Always save to global pending config (survives encoder restart)
  {
    std::lock_guard<std::mutex> lock(PendingROIMutex);
    auto &cfg = PendingROIConfig[kDefaultROIConfigKey];
    cfg.Regions = regions;
    cfg.Mode = mode;
    cfg.Enabled = enabled;
  }

  // If any encoder instances exist, apply immediately to ALL active encoders
  ApplyROIToAllActiveEncoders(regions, mode, enabled);

  // Force preview refresh to show updated ROI regions
  ForceRefreshPreview();

  blog(LOG_INFO,
       "[QSV VPL] ROI config saved: enabled=%d, regions=%zu, mode=%d",
       (int)enabled, regions.size(), (int)mode);

  QMessageBox::information(this, obs_module_text("ROIEditor"),
                           obs_module_text("ROIApplySuccess"));
}

void ROIDialog::OnApplyClicked() {
  SaveROIData();
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
static void OpenROIEditor(void * /*data*/) {
  auto *dialog = new ROIDialog();
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

void RegisterROIEditor() {
  obs_frontend_add_tools_menu_item(obs_module_text("ROIEditor"),
                                    OpenROIEditor, nullptr);
}