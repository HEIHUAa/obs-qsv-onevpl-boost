#include "obs-qsv-onevpl-roi-editor.hpp"
#include <QMessageBox>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <sstream>

ROIDialog::ROIDialog(QWidget *Parent) : QDialog(Parent) {
  setWindowTitle(obs_module_text("ROIEditor"));
  setMinimumSize(600, 500);

  auto *MainLayout = new QVBoxLayout(this);

  // Info label
  InfoLabel = new QLabel(obs_module_text("ROIEditorDesc"), this);
  InfoLabel->setWordWrap(true);
  MainLayout->addWidget(InfoLabel);

  // Encoder selector
  auto *EncoderGroup = new QGroupBox(obs_module_text("ROISelectEncoder"), this);
  auto *EncoderLayout = new QVBoxLayout(EncoderGroup);
  EncoderCombo = new QComboBox(this);
  EncoderLayout->addWidget(EncoderCombo);
  MainLayout->addWidget(EncoderGroup);

  // ROI Mode selection
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

  // ROI text input
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

  // Buttons
  auto *ButtonLayout = new QHBoxLayout();
  ButtonLayout->addStretch();
  ApplyButton = new QPushButton(obs_module_text("ROIApply"), this);
  CancelButton = new QPushButton(obs_module_text("ROICancel"), this);
  ButtonLayout->addWidget(ApplyButton);
  ButtonLayout->addWidget(CancelButton);
  MainLayout->addLayout(ButtonLayout);

  connect(EncoderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ROIDialog::OnEncoderSelected);
  connect(ApplyButton, &QPushButton::clicked, this, &ROIDialog::OnApplyClicked);
  connect(CancelButton, &QPushButton::clicked, this, &ROIDialog::OnCancelClicked);

  PopulateEncoderList();
}

void ROIDialog::PopulateEncoderList() {
  EncoderList.clear();
  EncoderCombo->clear();

  // QSV plugin encoder IDs
  static const char *PluginEncoderIDs[] = {
      "obs_qsv_vpl_h264",   "obs_qsv_vpl_h264_tex",
      "obs_qsv_vpl_hevc",   "obs_qsv_vpl_hevc_tex",
      "obs_qsv_vpl_av1",    "obs_qsv_vpl_av1_tex",
      nullptr};

  auto EnumFunc = [](void *Param, obs_encoder_t *Encoder) -> bool {
    auto *List = static_cast<std::vector<EncoderEntry> *>(Param);
    const char *id = obs_encoder_get_id(Encoder);
    if (!id)
      return true;

    bool isPluginEncoder = false;
    for (const char **pid = PluginEncoderIDs; *pid; pid++) {
      if (strcmp(id, *pid) == 0) {
        isPluginEncoder = true;
        break;
      }
    }
    if (!isPluginEncoder)
      return true;

    void *encoderData = LookupEncoderData(Encoder);
    if (!encoderData)
      return true;

    EncoderEntry entry;
    entry.Data = encoderData;
    entry.Encoder = Encoder;
    entry.Name = obs_encoder_get_name(Encoder);
    if (entry.Name.empty())
      entry.Name = id;
    List->push_back(entry);
    return true;
  };

  obs_enum_encoders(EnumFunc, &EncoderList);

  for (auto &entry : EncoderList) {
    EncoderCombo->addItem(entry.Name.c_str());
  }

  if (EncoderList.empty()) {
    InfoLabel->setText(obs_module_text("ROIEditorDesc") +
                       QString("\n\nNo active QSV encoders found. "
                               "Please start encoding with a QSV encoder first."));
    ApplyButton->setEnabled(false);
  } else {
    LoadROIData();
  }
}

void ROIDialog::OnEncoderSelected(int /*Index*/) {
  LoadROIData();
}

void ROIDialog::LoadROIData() {
  if (EncoderCombo->currentIndex() < 0 ||
      EncoderCombo->currentIndex() >= static_cast<int>(EncoderList.size()))
    return;

  auto &entry = EncoderList[EncoderCombo->currentIndex()];
  auto *ctx = static_cast<plugin_context *>(entry.Data);

  std::lock_guard<std::mutex> lock(ctx->EncoderMutex);

  // Load ROI mode
  if (ctx->EncoderParams.ROIMode == 1) {
    PriorityRadio->setChecked(true);
  } else {
    QPDeltaRadio->setChecked(true);
  }

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
  if (EncoderCombo->currentIndex() < 0 ||
      EncoderCombo->currentIndex() >= static_cast<int>(EncoderList.size()))
    return;

  auto &entry = EncoderList[EncoderCombo->currentIndex()];

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
                              &region.Left, &region.Top,
                              &region.Right, &region.Bottom,
                              &region.DeltaQP);
    if (parsed == 5) {
      regions.push_back(region);
    }
  }

  mfxU16 mode = (ModeGroup->checkedId() == 1) ? 1 : 0;

  // Update via the thread-safe function
  UpdateEncoderROI(entry.Data, regions, mode);
}

void ROIDialog::OnApplyClicked() {
  SaveROIData();

  QMessageBox::information(
      this, obs_module_text("ROIEditor"),
      "ROI settings applied successfully.\n"
      "Changes will take effect on the next encoded frame.");
}

void ROIDialog::OnCancelClicked() {
  reject();
}

// Callback for obs_frontend_add_tools_menu_item
static void OpenROIEditor(void * /*data*/) {
  auto *dialog = new ROIDialog();
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

void RegisterROIEditor() {
  obs_frontend_add_tools_menu_item(obs_module_text("ROIEditor"),
                                    OpenROIEditor, nullptr);
}