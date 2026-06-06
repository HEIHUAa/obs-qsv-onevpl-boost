#include "obs-qsv-onevpl-roi-editor.hpp"
#include <QMessageBox>
#include <QWindow>
#include <QTimer>
#include <mutex>
#include <sstream>
#include <cstdio>

// Helper: get plugin_context from current encoder selection
#define GET_CURRENT_CTX()                                                  \
  ([&]() -> plugin_context * {                                            \
    if (EncoderCombo->currentIndex() < 0 ||                                \
        EncoderCombo->currentIndex() >= (int)EncoderList.size())           \
      return nullptr;                                                      \
    return static_cast<plugin_context *>(                                  \
        EncoderList[EncoderCombo->currentIndex()].Data);                   \
  })()

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
// Encoder list population: try multiple methods to find QSV encoder instances
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

  // Helper lambda: check if encoder is one of ours and add to list
  auto AddIfOurs = [&](obs_encoder_t *enc) {
    if (!enc)
      return;
    const char *id = obs_encoder_get_id(enc);
    if (!id)
      return;

    bool isOurs = false;
    for (const char **pid = PluginEncoderIDs; *pid; pid++) {
      if (strcmp(id, *pid) == 0) {
        isOurs = true;
        break;
      }
    }
    if (!isOurs)
      return;

    // Look up plugin_context from our registry
    void *data = LookupEncoderData(enc);
    if (!data)
      return;

    // Avoid duplicates
    for (auto &existing : EncoderList) {
      if (existing.Encoder == enc)
        return;
    }

    EncoderEntry entry;
    entry.Data = data;
    entry.Encoder = enc;
    entry.Name = obs_encoder_get_name(enc);
    if (entry.Name.empty())
      entry.Name = id;
    EncoderList.push_back(std::move(entry));
  };

  // Check streaming and recording outputs for encoder instances
  obs_output_t *stream_output = obs_frontend_get_streaming_output();
  if (stream_output) {
    AddIfOurs(obs_output_get_video_encoder(stream_output));
  }

  obs_output_t *record_output = obs_frontend_get_recording_output();
  if (record_output) {
    AddIfOurs(obs_output_get_video_encoder(record_output));
  }

  // Fill combo box
  for (auto &entry : EncoderList)
    EncoderCombo->addItem(QString::fromStdString(entry.Name));

  if (EncoderList.empty()) {
    InfoLabel->setText(
        obs_module_text("ROIEditorDesc") +
        QStringLiteral(
            "\n\nNo active QSV encoders found. "
            "Configure a QSV encoder in OBS Settings -> Output "
            "and start streaming/recording."));
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
  init_data.format = ovi.output_format;
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

  // Render the current scene
  obs_source_t *scene = obs_frontend_get_current_scene();
  if (scene) {
    obs_source_video_render(scene);
    obs_source_release(scene);
  }

  // Overlay ROI rectangles on top
  dialog->DrawROIOverlay(cx, cy);
}

void ROIDialog::DrawROIOverlay(uint32_t cx, uint32_t cy) {
  auto *ctx = GET_CURRENT_CTX();
  if (!ctx)
    return;

  std::lock_guard<std::mutex> lock(ctx->EncoderMutex);

  // Check if ROI is enabled and has regions
  if (!ctx->EncoderParams.ROIEnabled)
    return;
  auto &regions = ctx->EncoderParams.ROIRegions;
  if (regions.empty())
    return;

  // Get output (scaled) dimensions from the video output
  video_t *video = obs_encoder_video(ctx->EncoderData);
  if (!video)
    return;
  const struct video_output_info *voi = video_output_get_info(video);
  if (!voi)
    return;

  uint32_t out_w = voi->width;
  uint32_t out_h = voi->height;
  if (out_w == 0 || out_h == 0)
    return;

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
    // Scale ROI coordinates from output resolution to preview widget size
    float x1 = (float)r.Left / (float)out_w * (float)cx;
    float y1 = (float)r.Top / (float)out_h * (float)cy;
    float x2 = (float)r.Right / (float)out_w * (float)cx;
    float y2 = (float)r.Bottom / (float)out_h * (float)cy;

    // Clamp to preview area
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
  auto *ctx = GET_CURRENT_CTX();
  if (!ctx)
    return;

  std::lock_guard<std::mutex> lock(ctx->EncoderMutex);

  // Load ROI enable state
  ROIEnableCheck->setChecked(ctx->EncoderParams.ROIEnabled);

  // Load ROI mode
  if (ctx->EncoderParams.ROIMode == 1)
    PriorityRadio->setChecked(true);
  else
    QPDeltaRadio->setChecked(true);

  // Load ROI regions as text
  std::string text;
  for (auto &region : ctx->EncoderParams.ROIRegions) {
    if (!text.empty())
      text += "\n";
    text += std::to_string(region.Left) + " " +
            std::to_string(region.Top) + " " +
            std::to_string(region.Right) + " " +
            std::to_string(region.Bottom) + " " +
            std::to_string(region.DeltaQP);
  }
  ROITextEdit->setPlainText(QString::fromStdString(text));
}

void ROIDialog::SaveROIData() {
  auto *ctx = GET_CURRENT_CTX();
  if (!ctx)
    return;

  // Parse text input into ROI regions
  std::vector<encoder_params::roi_region> regions;
  std::string text = ROITextEdit->toPlainText().toStdString();
  std::istringstream stream(text);
  std::string line;

  while (std::getline(stream, line)) {
    // Trim whitespace
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

  // Update via the thread-safe function
  UpdateEncoderROI(ctx, regions, mode, enabled);
}

void ROIDialog::OnApplyClicked() {
  SaveROIData();

  QMessageBox::information(this, obs_module_text("ROIEditor"),
                           obs_module_text("ROIApplySuccess"));
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