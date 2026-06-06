#include "obs-qsv-onevpl-roi-editor.hpp"
#include <QMessageBox>
#include <QWindow>
#include <QTimer>
#include <mutex>
#include <sstream>
#include <cstdio>

// Helper: get type ID from current encoder selection
std::string ROIDialog::GetCurrentTypeID(QComboBox *Combo,
                                         const std::vector<EncoderEntry> &List) {
  int idx = Combo->currentIndex();
  if (idx < 0 || idx >= (int)List.size())
    return {};
  return List[idx].TypeID;
}

// Helper: find plugin_context by encoder type ID (nullptr if not encoding)
static plugin_context *FindCtxByTypeID(const std::string &TypeID) {
  if (TypeID.empty())
    return nullptr;
  std::lock_guard<std::mutex> lock(EncoderDataMapMutex);
  for (auto &pair : EncoderDataMap) {
    const char *id = obs_encoder_get_id(pair.first);
    if (id && TypeID == id)
      return pair.second;
  }
  return nullptr;
}

ROIDialog::ROIDialog(QWidget *Parent)
    : QDialog(Parent),
      PreviewDisplay(nullptr),
      PreviewWidget(nullptr) {
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

  // === Encoder selector + enable toggle ===
  auto *EncoderGroup = new QGroupBox(obs_module_text("ROISelectEncoder"), this);
  auto *EncoderGrid = new QGridLayout(EncoderGroup);

  EncoderCombo = new QComboBox(this);
  EncoderGrid->addWidget(new QLabel(obs_module_text("ROIEncoder"), this), 0, 0);
  EncoderGrid->addWidget(EncoderCombo, 0, 1);

  ROIEnableCheck = new QCheckBox(obs_module_text("ROIEnabled"), this);
  ROIEnableCheck->setChecked(true);
  EncoderGrid->addWidget(ROIEnableCheck, 0, 2);

  AlwaysOnTopCheck = new QCheckBox(obs_module_text("AlwaysOnTop"), this);
  AlwaysOnTopCheck->setChecked(false);
  EncoderGrid->addWidget(AlwaysOnTopCheck, 0, 3);

  MainLayout->addWidget(EncoderGroup);

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
  connect(EncoderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ROIDialog::OnEncoderSelected);
  connect(ApplyButton, &QPushButton::clicked, this, &ROIDialog::OnApplyClicked);
  connect(CancelButton, &QPushButton::clicked, this,
          &ROIDialog::OnCancelClicked);
  connect(AlwaysOnTopCheck, &QCheckBox::checkStateChanged, this,
          &ROIDialog::OnToggleAlwaysOnTop);

  // Install event filter on preview widget for resize handling
  PreviewWidget->installEventFilter(this);

  // Populate encoder list at construction time
  PopulateEncoderList();
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
}

void ROIDialog::closeEvent(QCloseEvent *Event) {
  DestroyPreview();
  QDialog::closeEvent(Event);
}

// ---------------------------------------------------------------------------
// Enumerate QSV encoder types registered by this plugin
// ---------------------------------------------------------------------------
void ROIDialog::PopulateEncoderList() {
  EncoderList.clear();
  EncoderCombo->clear();

  // Known QSV plugin encoder IDs
  static const char *PluginEncoderIDs[] = {
      "obs_qsv_vpl_h264",   "obs_qsv_vpl_h264_tex",
      "obs_qsv_vpl_hevc",   "obs_qsv_vpl_hevc_tex",
      "obs_qsv_vpl_av1",    "obs_qsv_vpl_av1_tex",
      nullptr};

  // Enumerate all registered encoder types and filter by plugin IDs
  for (const char **pid = PluginEncoderIDs; *pid; pid++) {
    bool found = false;
    for (size_t idx = 0;; idx++) {
      const char *type_id = nullptr;
      if (!obs_enum_encoder_types(idx, &type_id))
        break;
      if (type_id && strcmp(type_id, *pid) == 0) {
        found = true;
        break;
      }
    }
    if (!found)
      continue;

    QString displayName;
    if (strstr(*pid, "av1"))
      displayName = "QSV AV1 (VPL)";
    else if (strstr(*pid, "hevc"))
      displayName = "QSV HEVC (VPL)";
    else
      displayName = "QSV H.264 (VPL)";
    if (strstr(*pid, "_tex"))
      displayName += " [tex]";

    EncoderEntry entry;
    entry.TypeID = *pid;
    entry.Name = displayName.toStdString();
    EncoderList.push_back(std::move(entry));
    EncoderCombo->addItem(displayName);
  }

  if (EncoderList.empty()) {
    InfoLabel->setText(
        obs_module_text("ROIEditorDesc") +
        QStringLiteral(
            "\n\nNo QSV encoders available. "
            "The QSV VPL plugin encoders may not be registered correctly."));
    ApplyButton->setEnabled(false);
  } else {
    LoadROIData();
  }
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
  std::string typeId = GetCurrentTypeID(EncoderCombo, EncoderList);
  if (typeId.empty())
    return;

  bool enabled = false;
  mfxU16 mode = 0;
  std::vector<encoder_params::roi_region> regions;

  // Try reading from encoder instance first (already has pending config applied)
  plugin_context *ctx = FindCtxByTypeID(typeId);
  if (ctx) {
    std::lock_guard<std::mutex> lock(ctx->EncoderMutex);
    enabled = ctx->EncoderParams.ROIEnabled;
    mode = ctx->EncoderParams.ROIMode;
    regions = ctx->EncoderParams.ROIRegions;
  } else {
    // Fallback to pending config
    std::lock_guard<std::mutex> lock(PendingROIMutex);
    auto it = PendingROIConfig.find(typeId);
    if (it != PendingROIConfig.end()) {
      enabled = it->second.Enabled;
      mode = it->second.Mode;
      regions = it->second.Regions;
    }
  }

  if (!enabled || regions.empty())
    return;

  // Get output (scaled) and base dimensions from OBS video info
  struct obs_video_info ovi;
  obs_get_video_info(&ovi);
  if (ovi.output_width < 1 || ovi.output_height < 1)
    return;
  if (ovi.base_width < 1 || ovi.base_height < 1)
    return;

  float out_w = (float)ovi.output_width;
  float out_h = (float)ovi.output_height;
  float base_w = (float)ovi.base_width;
  float base_h = (float)ovi.base_height;

  // Same viewport calculation as PreviewDraw
  float scale = std::min((float)cx / base_w, (float)cy / base_h);
  float vp_w = base_w * scale;
  float vp_h = base_h * scale;
  float vp_x = ((float)cx - vp_w) * 0.5f;
  float vp_y = ((float)cy - vp_h) * 0.5f;

  // Set up solid color effect from OBS base effects
  gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
  if (!solid)
    return;

  gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
  if (!tech)
    return;

  gs_technique_begin(tech);
  gs_technique_begin_pass(tech, 0);

  gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");

  for (auto &r : regions) {
    // Map ROI from output resolution → preview widget pixel coordinates
    // Path: output → base → widget (via viewport)
    float x1 = vp_x + (float)r.Left * (vp_w / out_w);
    float y1 = vp_y + (float)r.Top * (vp_h / out_h);
    float x2 = vp_x + (float)r.Right * (vp_w / out_w);
    float y2 = vp_y + (float)r.Bottom * (vp_h / out_h);

    // Clamp to preview widget area
    x1 = (x1 < 0) ? 0 : (x1 > (float)cx ? (float)cx : x1);
    y1 = (y1 < 0) ? 0 : (y1 > (float)cy ? (float)cy : y1);
    x2 = (x2 < 0) ? 0 : (x2 > (float)cx ? (float)cx : x2);
    y2 = (y2 < 0) ? 0 : (y2 > (float)cy ? (float)cy : y2);

    if (x2 <= x1 || y2 <= y1)
      continue;

    // Color based on DeltaQP value
    vec4 color;
    vec4_zero(&color);
    if (r.DeltaQP < 0) {
      // Red (more bitrate) – semi-transparent
      vec4_set(&color, 1.0f, 0.2f, 0.2f, 0.35f);
    } else if (r.DeltaQP > 0) {
      // Blue (less bitrate) – semi-transparent
      vec4_set(&color, 0.2f, 0.4f, 1.0f, 0.35f);
    } else {
      // Green (default) – semi-transparent
      vec4_set(&color, 0.2f, 1.0f, 0.2f, 0.25f);
    }

    gs_effect_set_vec4(color_param, &color);

    // Draw rectangle using immediate vertex buffer
    // Use GS_TRISTRIP with 4 vertices
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
void ROIDialog::OnEncoderSelected(int /*Index*/) {
  LoadROIData();
}

void ROIDialog::LoadROIData() {
  std::string typeId = GetCurrentTypeID(EncoderCombo, EncoderList);
  if (typeId.empty())
    return;

  // Load from pending config (always the source of truth)
  std::lock_guard<std::mutex> lock(PendingROIMutex);
  auto it = PendingROIConfig.find(typeId);
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
  } else {
    // No saved config - use defaults
    ROIEnableCheck->setChecked(false);
    QPDeltaRadio->setChecked(true);
    ROITextEdit->clear();
  }
}

void ROIDialog::SaveROIData() {
  std::string typeId = GetCurrentTypeID(EncoderCombo, EncoderList);
  if (typeId.empty()) {
    QMessageBox::warning(this, obs_module_text("ROIEditor"),
                         "No encoder selected.");
    return;
  }

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

  // Always save to pending config (survives encoder restart)
  {
    std::lock_guard<std::mutex> lock(PendingROIMutex);
    PendingROIConfig[typeId].Regions = regions;
    PendingROIConfig[typeId].Mode = mode;
    PendingROIConfig[typeId].Enabled = enabled;
  }

  // If encoder instance exists, apply immediately
  plugin_context *ctx = FindCtxByTypeID(typeId);
  if (ctx)
    UpdateEncoderROI(ctx, regions, mode, enabled);

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