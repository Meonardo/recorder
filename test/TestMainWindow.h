#pragma once

#include <QMainWindow>
#include <QBuffer>
#include <QAction>
#include <QThread>

#include "../core/scene-source.h"

#include "ui_TestMainWindow.h"

#define PREVIEW_EDGE_SIZE 10

struct WebSocketSessionState;
class WebSocketServer;

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
	virtual void closeEvent(QCloseEvent* event) override;

private:
	Ui::TestMainWindowClass* ui;
	bool previewEnabled = true;

	// preview
	int previewX = 0, previewY = 0;
	int previewCX = 0, previewCY = 0;
	float previewScale = 0.0f;

	bool drawSafeAreas = false;
	bool drawSpacingHelpers = true;

	WebSocketServer* websocketServer;

	std::vector<std::unique_ptr<core::Source>> localSources;

	std::vector<core::Source> attachedSources;

	std::vector<WebSocketSessionState> websocketSessions;

	void EnablePreviewDisplay(bool enable);
	void ResizePreview(uint32_t cx, uint32_t cy);

	static void RenderMain(void* data, uint32_t cx, uint32_t cy);

	void ConfigureUI();

	void HideWhileCapturingScreen(QWidget* window);
};
