#pragma once

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QCloseEvent>
#include <QTimer>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "obs-qsv-onevpl-encoder.hpp"

struct obs_display;

class ROIDialog : public QDialog {
  Q_OBJECT

public:
  explicit ROIDialog(QWidget *Parent = nullptr);
  ~ROIDialog() override;

protected:
  void showEvent(QShowEvent *Event) override;
  void closeEvent(QCloseEvent *Event) override;
  bool eventFilter(QObject *Obj, QEvent *Event) override;

private slots:
  void OnApplyClicked();
  void OnCancelClicked();
  void OnToggleAlwaysOnTop(Qt::CheckState State);

public:
  // Called from static frontend event callback; make public for access
  void LoadROIData();
  void ForceRefreshPreview();

private:
  void SaveROIData();
  void SetUIFromGlobalConfig();
  void UpdatePreviewFromText();

  // Preview
  bool CreatePreview();
  void DestroyPreview();
  void ResizePreview();
  static void PreviewDraw(void *param, uint32_t cx, uint32_t cy);
  void DrawROIOverlay(uint32_t cx, uint32_t cy);

  QLabel *InfoLabel;

  // ROI enable toggle
  QCheckBox *ROIEnableCheck;

  // Always on top toggle
  QCheckBox *AlwaysOnTopCheck;

  // ROI mode
  QButtonGroup *ModeGroup;
  QRadioButton *QPDeltaRadio;
  QRadioButton *PriorityRadio;

  // ROI text input
  QTextEdit *ROITextEdit;
  QLabel *FormatLabel;

  // Buttons
  QPushButton *ApplyButton;
  QPushButton *CancelButton;

  // Preview
  QWidget *PreviewWidget;
  obs_display_t *PreviewDisplay;
  QTimer *RefreshTimer;       // periodic preview refresh
};