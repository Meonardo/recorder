#pragma once

#include <QMainWindow>
#include <QBuffer>
#include <QAction>
#include <QThread>

#include "../core/scene-source.h"

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
	int previewX = 0, previewY = 0;
	int previewCX = 0, previewCY = 0;
	float previewScale = 0.0f;

	bool drawSafeAreas = false;
	bool drawSpacingHelpers = true;

	std::vector<std::unique_ptr<core::Source>> localSources;

	std::vector<core::Source> attachedSources;

	void EnablePreviewDisplay(bool enable);
	void ResizePreview(uint32_t cx, uint32_t cy);

	static void RenderMain(void* data, uint32_t cx, uint32_t cy);

	void ConfigureUI();

  void HideWhileCapturingScreen(QWidget* window);
};
