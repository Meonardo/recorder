#include "TestMainWindow.h"

#include <Windows.h>

#include <QScreen>
#include <qwindow.h>
#include <QShowEvent>
#include <QGuiApplication>

#include "util/profiler.hpp"

#include "../core/app.h"
#include "../display-helpers.hpp"
#include "../core/scene-source.h"

TestMainWindow::TestMainWindow(QWidget* parent)
  : QMainWindow(parent),
    ui(new Ui::TestMainWindowClass()) {

	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_DeleteOnClose, true);

	ui->setupUi(this);

	auto displayResize = [this]() {
		struct obs_video_info ovi;

		if (obs_get_video_info(&ovi)) {
			ResizePreview(ovi.base_width, ovi.base_height);
		}

		auto dpi = devicePixelRatioF();
		ui->preview->UpdateDPI(dpi);
	};
	auto dpi = devicePixelRatioF();
	ui->preview->UpdateDPI(dpi);

	// resize preview when the window is resized
	connect(windowHandle(), &QWindow::screenChanged, displayResize);
	connect(ui->preview, &OBSQTDisplay::DisplayResized, displayResize);
}

TestMainWindow::~TestMainWindow() {
	// remove draw callback
	obs_display_remove_draw_callback(ui->preview->GetDisplay(), TestMainWindow::RenderMain,
					 this);

	delete ui;
}

void TestMainWindow::Prepare() {
	ProfileScope("MainWindow::Prepare");

	// check if preview is enabled
	previewEnabled = CoreApp->IsPreviewEnabled();
	QMetaObject::invokeMethod(this, "EnablePreviewDisplay", Qt::QueuedConnection,
				  Q_ARG(bool, true));
	obs_display_set_enabled(ui->preview->GetDisplay(), previewEnabled);
	ui->preview->setVisible(previewEnabled);

	auto addDisplay = [this](OBSQTDisplay* window) {
		obs_display_add_draw_callback(window->GetDisplay(), TestMainWindow::RenderMain,
					      this);

		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi)) {
			ResizePreview(ovi.base_width, ovi.base_height);
		}
	};
	connect(ui->preview, &OBSQTDisplay::DisplayCreated, addDisplay);

	// show this window
	show();
	activateWindow();

	// load all local sources, may be run this in a separate thread ?
	std::thread([this]() { LoadLocalSources(); }).detach();
}

bool TestMainWindow::nativeEvent(const QByteArray&, void* message, qintptr*) {
	const MSG& msg = *static_cast<MSG*>(message);
	switch (msg.message) {
	case WM_MOVE:
		for (OBSQTDisplay* const display : findChildren<OBSQTDisplay*>()) {
			display->OnMove();
		}
		break;
	case WM_DISPLAYCHANGE:
		for (OBSQTDisplay* const display : findChildren<OBSQTDisplay*>()) {
			display->OnDisplayChange();
		}
	}

	return false;
}

void TestMainWindow::changeEvent(QEvent* event) {
	if (event->type() == QEvent::WindowStateChange) {
		QWindowStateChangeEvent* stateEvent = (QWindowStateChangeEvent*)event;

		if (isMinimized()) {
			if (previewEnabled)
				EnablePreviewDisplay(false);
		} else if (stateEvent->oldState() & Qt::WindowMinimized && isVisible()) {
			if (previewEnabled)
				EnablePreviewDisplay(true);
		}
	}
}

void TestMainWindow::EnablePreviewDisplay(bool enable) {
	obs_display_set_enabled(ui->preview->GetDisplay(), enable);
	ui->preview->setVisible(enable);
}

void TestMainWindow::ResizePreview(uint32_t cx, uint32_t cy) {
	auto targetSize = GetPixelSize(ui->preview);

	if (ui->preview->IsFixedScaling()) {
		ui->preview->ClampScrollingOffsets();
		previewScale = ui->preview->GetScalingAmount();
		GetCenterPosFromFixedScale(int(cx), int(cy),
					   targetSize.width() - PREVIEW_EDGE_SIZE * 2,
					   targetSize.height() - PREVIEW_EDGE_SIZE * 2, previewX,
					   previewY, previewScale);
		previewX += ui->preview->GetScrollX();
		previewY += ui->preview->GetScrollY();

	} else {
		GetScaleAndCenterPos(int(cx), int(cy), targetSize.width() - PREVIEW_EDGE_SIZE * 2,
				     targetSize.height() - PREVIEW_EDGE_SIZE * 2, previewX,
				     previewY, previewScale);
		ui->preview->UpdateScale(previewScale);
	}

	previewX += float(PREVIEW_EDGE_SIZE);
	previewY += float(PREVIEW_EDGE_SIZE);

	ui->preview->UpdatePosition(previewX, previewY);
}

void TestMainWindow::RenderMain(void* data, uint32_t, uint32_t) {
	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "RenderMain");

	TestMainWindow* window = static_cast<TestMainWindow*>(data);

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	window->previewCX = int(window->previewScale * float(ovi.base_width));
	window->previewCY = int(window->previewScale * float(ovi.base_height));

	gs_viewport_push();
	gs_projection_push();

	obs_display_t* display = window->ui->preview->GetDisplay();
	uint32_t width, height;
	obs_display_size(display, &width, &height);
	float right = float(width) - window->previewX;
	float bottom = float(height) - window->previewY;

	gs_ortho(-window->previewX, right, -window->previewY, bottom, -100.0f, 100.0f);

	/* --------------------------------------- */

	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
	gs_set_viewport(window->previewX, window->previewY, window->previewCX, window->previewCY);
	obs_render_main_texture_src_color_only();
	gs_load_vertexbuffer(nullptr);

	/* --------------------------------------- */

	gs_ortho(-window->previewX, right, -window->previewY, bottom, -100.0f, 100.0f);
	gs_reset_viewport();

	/* --------------------------------------- */

	gs_projection_pop();
	gs_viewport_pop();

	GS_DEBUG_MARKER_END();
}

void TestMainWindow::LoadLocalSources() {
	{
		auto audioInputSources =
		  core::AudioSource::GetAudioSources(core::kSourceTypeAudioCapture);
		for (const auto& item : audioInputSources) {
			blog(LOG_INFO, "audio capture device: %s, %s", item.Name().c_str(),
			     item.ID().c_str());
		}
	}
	{
		auto audioOutputSources =
		  core::AudioSource::GetAudioSources(core::kSourceTypeAudioPlayback);
		for (const auto& item : audioOutputSources) {
			blog(LOG_INFO, "audio playback device: %s, %s", item.Name().c_str(),
			     item.ID().c_str());
		}
	}
	{
		auto screenSources = core::ScreenSource::GetScreenSources();
		for (const auto& item : screenSources) {
			blog(LOG_INFO, "screen source: %s, %s", item.Name().c_str(),
			     item.ID().c_str());
		}

		/*if (!screenSources.empty()) {
			auto screenSource = screenSources[0];
			screenSource.Attach();
		}*/
	}
	{
		/*	auto cameraSources = core::CameraSource::GetCameraSources();
		for (const auto& item : cameraSources) {
			blog(LOG_INFO, "camera source: %s, %s", item.Name().c_str(),
			     item.ID().c_str());
		}*/
		/*if (!cameraSources.empty()) {
			auto cameraSource = cameraSources[0];
			cameraSource.Attach();
		}*/
	}

	auto sources = core::Source::GetAttachedSources();
	for (const auto& item : sources) {
		blog(LOG_INFO, "attached source: %s, %s", item.Name().c_str(), item.ID().c_str());
	}
	/*if (!sources.empty()) {
		if (sources[0].Detach()) {
			blog(LOG_INFO, "detached source: %s, %s", sources[0].Name().c_str(),
			     sources[0].ID().c_str());
		}
	}*/
}
