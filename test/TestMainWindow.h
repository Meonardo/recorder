#pragma once

#include <QMainWindow>
#include <QBuffer>
#include <QAction>
#include <QThread>

#include "ui_TestMainWindow.h"

#define PREVIEW_EDGE_SIZE 10

class TestMainWindow : public QMainWindow {
	Q_OBJECT

	friend struct OBSStudioAPI;
	friend struct BasicOutputHandler;

public:
	TestMainWindow(QWidget* parent = nullptr);
	~TestMainWindow();

  void Prepare();

protected:
	virtual bool nativeEvent(const QByteArray& eventType, void* message,
				 qintptr* result) override;
	virtual void changeEvent(QEvent* event) override;

private:
	Ui::TestMainWindowClass* ui;
  bool previewEnabled = true;

  // preview
  gs_vertbuffer_t* box = nullptr;
  gs_vertbuffer_t* boxLeft = nullptr;
  gs_vertbuffer_t* boxTop = nullptr;
  gs_vertbuffer_t* boxRight = nullptr;
  gs_vertbuffer_t* boxBottom = nullptr;
  gs_vertbuffer_t* circle = nullptr;

  gs_vertbuffer_t* actionSafeMargin = nullptr;
  gs_vertbuffer_t* graphicsSafeMargin = nullptr;
  gs_vertbuffer_t* fourByThreeSafeMargin = nullptr;
  gs_vertbuffer_t* leftLine = nullptr;
  gs_vertbuffer_t* topLine = nullptr;
  gs_vertbuffer_t* rightLine = nullptr;

  int previewX = 0, previewY = 0;
  int previewCX = 0, previewCY = 0;
  float previewScale = 0.0f;

  bool drawSafeAreas = false;
  bool drawSpacingHelpers = true;

  void EnablePreviewDisplay(bool enable);
  void ResizePreview(uint32_t cx, uint32_t cy);

  void UpdatePreviewSafeAreas();
  void UpdatePreviewSpacingHelpers();
  void UpdatePreviewOverflowSettings();

  void DrawBackdrop(float cx, float cy);
  void InitPrimitives();

  static void RenderMain(void* data, uint32_t cx, uint32_t cy);
};
