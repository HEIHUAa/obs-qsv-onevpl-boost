#pragma once

#include <QDialog>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <obs-module.h>
#include "obs-qsv-onevpl-encoder.hpp"

class ROIDialog : public QDialog {
  Q_OBJECT

public:
  explicit ROIDialog(QWidget *Parent = nullptr);
  ~ROIDialog() override = default;

private slots:
  void OnEncoderSelected(int Index);
  void OnApplyClicked();
  void OnCancelClicked();

private:
  void PopulateEncoderList();
  void LoadROIData();
  void SaveROIData();

  QComboBox *EncoderCombo;
  QTextEdit *ROITextEdit;
  QRadioButton *QPDeltaRadio;
  QRadioButton *PriorityRadio;
  QButtonGroup *ModeGroup;
  QPushButton *ApplyButton;
  QPushButton *CancelButton;
  QLabel *InfoLabel;
  QLabel *FormatLabel;

  struct EncoderEntry {
    void *Data; // plugin_context pointer
    obs_encoder_t *Encoder;
    std::string Name;
  };
  std::vector<EncoderEntry> EncoderList;
};