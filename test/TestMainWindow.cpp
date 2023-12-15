#include "TestMainWindow.h"

#include <Windows.h>

#include <QScreen>
#include <qwindow.h>
#include <QShowEvent>
#include <QGuiApplication>

#include "util/profiler.hpp"

#include "../core/app.h"
#include "../display-helpers.hpp"

TestMainWindow::TestMainWindow(QWidget* parent)
  : QMainWindow(parent),
    ui(new Ui::TestMainWindowClass()) {

	setAttribute(Qt::WA_NativeWindow);

	ui->setupUi(this);

	auto displayResize = [this]() {
		struct obs_video_info ovi;

		if (obs_get_video_info(&ovi))
			ResizePreview(ovi.base_width, ovi.base_height);

		auto dpi = devicePixelRatioF();
		ui->preview->UpdateDPI(dpi);
	};
	auto dpi = devicePixelRatioF();
	ui->preview->UpdateDPI(dpi);

	connect(windowHandle(), &QWindow::screenChanged, displayResize);
	connect(ui->preview, &OBSQTDisplay::DisplayResized, displayResize);
}

TestMainWindow::~TestMainWindow() {
	obs_display_remove_draw_callback(ui->preview->GetDisplay(), TestMainWindow::RenderMain,
					 this);

	obs_enter_graphics();
	gs_vertexbuffer_destroy(box);
	gs_vertexbuffer_destroy(boxLeft);
	gs_vertexbuffer_destroy(boxTop);
	gs_vertexbuffer_destroy(boxRight);
	gs_vertexbuffer_destroy(boxBottom);
	gs_vertexbuffer_destroy(circle);
	gs_vertexbuffer_destroy(actionSafeMargin);
	gs_vertexbuffer_destroy(graphicsSafeMargin);
	gs_vertexbuffer_destroy(fourByThreeSafeMargin);
	gs_vertexbuffer_destroy(leftLine);
	gs_vertexbuffer_destroy(topLine);
	gs_vertexbuffer_destroy(rightLine);
	obs_leave_graphics();
}

void TestMainWindow::Prepare() {
	ProfileScope("MainWindow::Prepare");

  UpdatePreviewSafeAreas();
  UpdatePreviewSpacingHelpers();
  UpdatePreviewOverflowSettings();

	InitPrimitives();

  ui->preview->SetDrawBox(box);

	previewEnabled = CoreApp->IsPreviewEnabled();

	if (!previewEnabled && !CoreApp->IsPreviewProgramMode())
		QMetaObject::invokeMethod(this, "EnablePreviewDisplay", Qt::QueuedConnection,
					  Q_ARG(bool, previewEnabled));
	else if (!previewEnabled && CoreApp->IsPreviewProgramMode())
		QMetaObject::invokeMethod(this, "EnablePreviewDisplay", Qt::QueuedConnection,
					  Q_ARG(bool, true));

	obs_display_set_enabled(ui->preview->GetDisplay(), previewEnabled);
	ui->preview->setVisible(previewEnabled);

	auto addDisplay = [this](OBSQTDisplay* window) {
		obs_display_add_draw_callback(window->GetDisplay(), TestMainWindow::RenderMain,
					      this);

		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi))
			ResizePreview(ovi.base_width, ovi.base_height);
	};

	connect(ui->preview, &OBSQTDisplay::DisplayCreated, addDisplay);

  activateWindow();
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
	QSize targetSize;
	bool isFixedScaling;
	obs_video_info ovi;

	/* resize preview panel to fix to the top section of the window */
	targetSize = GetPixelSize(ui->preview);

	isFixedScaling = ui->preview->IsFixedScaling();
	obs_get_video_info(&ovi);

	if (isFixedScaling) {
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

void TestMainWindow::UpdatePreviewSpacingHelpers() {
	drawSpacingHelpers =
	  config_get_bool(CoreApp->GlobalConfig(), "BasicWindow", "SpacingHelpersEnabled");
}

void TestMainWindow::UpdatePreviewSafeAreas() {
	drawSafeAreas = config_get_bool(CoreApp->GlobalConfig(), "BasicWindow", "ShowSafeAreas");
}

void TestMainWindow::UpdatePreviewOverflowSettings() {
	bool hidden = config_get_bool(CoreApp->GlobalConfig(), "BasicWindow", "OverflowHidden");
	bool select =
	  config_get_bool(CoreApp->GlobalConfig(), "BasicWindow", "OverflowSelectionHidden");
	bool always =
	  config_get_bool(CoreApp->GlobalConfig(), "BasicWindow", "OverflowAlwaysVisible");

	ui->preview->SetOverflowHidden(hidden);
	ui->preview->SetOverflowSelectionHidden(select);
	ui->preview->SetOverflowAlwaysVisible(always);
}

void TestMainWindow::DrawBackdrop(float cx, float cy) {
	if (!box)
		return;

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "DrawBackdrop");

	gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t* color = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t* tech = gs_effect_get_technique(solid, "Solid");

	vec4 colorVal;
	vec4_set(&colorVal, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(color, &colorVal);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);
	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_scale3f(float(cx), float(cy), 1.0f);

	gs_load_vertexbuffer(box);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
	gs_technique_end_pass(tech);
	gs_technique_end(tech);

	gs_load_vertexbuffer(nullptr);

	GS_DEBUG_MARKER_END();
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

	//window->ui->preview->DrawOverflow();

	/* --------------------------------------- */

	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
	gs_set_viewport(window->previewX, window->previewY, window->previewCX, window->previewCY);

	if (CoreApp->IsPreviewProgramMode()) {
		window->DrawBackdrop(float(ovi.base_width), float(ovi.base_height));

		OBSScene scene = CoreApp->GetCurrentScene();
		obs_source_t* source = obs_scene_get_source(scene);
		if (source)
			obs_source_video_render(source);
	} else {
		obs_render_main_texture_src_color_only();
	}
	gs_load_vertexbuffer(nullptr);

	/* --------------------------------------- */

	gs_ortho(-window->previewX, right, -window->previewY, bottom, -100.0f, 100.0f);
	gs_reset_viewport();

	//uint32_t targetCX = window->previewCX;
	//uint32_t targetCY = window->previewCY;

	//if (window->drawSafeAreas) {
	//	RenderSafeAreas(window->actionSafeMargin, targetCX, targetCY);
	//	RenderSafeAreas(window->graphicsSafeMargin, targetCX, targetCY);
	//	RenderSafeAreas(window->fourByThreeSafeMargin, targetCX, targetCY);
	//	RenderSafeAreas(window->leftLine, targetCX, targetCY);
	//	RenderSafeAreas(window->topLine, targetCX, targetCY);
	//	RenderSafeAreas(window->rightLine, targetCX, targetCY);
	//}

	//window->ui->preview->DrawSceneEditing();

	//if (window->drawSpacingHelpers)
	//	window->ui->preview->DrawSpacingHelpers();

	/* --------------------------------------- */

	gs_projection_pop();
	gs_viewport_pop();

	GS_DEBUG_MARKER_END();
}

void TestMainWindow::InitPrimitives() {
	ProfileScope("MainWindow::InitPrimitives");

	obs_enter_graphics();

	gs_render_start(true);
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f, 1.0f);
	gs_vertex2f(1.0f, 0.0f);
	gs_vertex2f(1.0f, 1.0f);
	box = gs_render_save();

	gs_render_start(true);
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f, 1.0f);
	boxLeft = gs_render_save();

	gs_render_start(true);
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(1.0f, 0.0f);
	boxTop = gs_render_save();

	gs_render_start(true);
	gs_vertex2f(1.0f, 0.0f);
	gs_vertex2f(1.0f, 1.0f);
	boxRight = gs_render_save();

	gs_render_start(true);
	gs_vertex2f(0.0f, 1.0f);
	gs_vertex2f(1.0f, 1.0f);
	boxBottom = gs_render_save();

	gs_render_start(true);
	for (int i = 0; i <= 360; i += (360 / 20)) {
		float pos = RAD(float(i));
		gs_vertex2f(cosf(pos), sinf(pos));
	}
	circle = gs_render_save();

	InitSafeAreas(&actionSafeMargin, &graphicsSafeMargin, &fourByThreeSafeMargin, &leftLine,
		      &topLine, &rightLine);
	obs_leave_graphics();
}
