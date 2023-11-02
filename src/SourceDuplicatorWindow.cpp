#include "SourceDuplicatorWindow.h"

#include "obs-app.hpp"
#include "qt-wrappers.hpp"
#include "display-helpers.hpp"

#include <QCloseEvent>
#include <obs-data.h>
#include <obs.h>
#include <qpointer.h>
#include <util/c99defs.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#endif

static obs_source_t* CreateLabel(const char* name, size_t h) {
	OBSDataAutoRelease settings = obs_data_create();
	OBSDataAutoRelease font = obs_data_create();

	std::string text;
	text += " ";
	text += name;
	text += " ";

#if defined(_WIN32)
	obs_data_set_string(font, "face", "Arial");
#elif defined(__APPLE__)
	obs_data_set_string(font, "face", "Helvetica");
#else
	obs_data_set_string(font, "face", "Monospace");
#endif
	obs_data_set_int(font, "flags", 1); // Bold text
	obs_data_set_int(font, "size", min(int(h), 300));

	obs_data_set_obj(settings, "font", font);
	obs_data_set_string(settings, "text", text.c_str());
	obs_data_set_bool(settings, "outline", false);

#ifdef _WIN32
	const char* text_source_id = "text_gdiplus";
#else
	const char* text_source_id = "text_ft2_source";
#endif

	obs_source_t* txtSource = obs_source_create_private(text_source_id, name, settings);

	return txtSource;
}

static void CreateTransitionScene(OBSSource scene, const char* text, uint32_t color) {
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "width", obs_source_get_width(scene));
	obs_data_set_int(settings, "height", obs_source_get_height(scene));
	obs_data_set_int(settings, "color", color);

	OBSSourceAutoRelease colorBG =
	  obs_source_create_private("color_source", "background", settings);

	obs_scene_add(obs_scene_from_source(scene), colorBG);

	OBSSourceAutoRelease label = CreateLabel(text, obs_source_get_height(scene));
	obs_sceneitem_t* item = obs_scene_add(obs_scene_from_source(scene), label);

	vec2 size;
	vec2_set(&size, obs_source_get_width(scene),
#ifdef _WIN32
		 obs_source_get_height(scene));
#else
		 obs_source_get_height(scene) * 0.8);
#endif

	obs_sceneitem_set_bounds(item, &size);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
}

SourceDuplicatorWindow::SourceDuplicatorWindow(OBSSource source, QWidget* parent)
  : QWidget(parent),
    ui(new Ui::SourceDuplicatorWindowClass()),
    main(reinterpret_cast<MainWindow*>(App()->GetMainWindow())),
    acceptClicked(false),
    source(source),
    removedSignal(obs_source_get_signal_handler(source), "remove",
		  SourceDuplicatorWindow::SourceRemoved, this),
    renamedSignal(obs_source_get_signal_handler(source), "rename",
		  SourceDuplicatorWindow::SourceRenamed, this) {

	ui->setupUi(this);

	enum obs_source_type type = obs_source_get_type(source);
	const char* name = obs_source_get_name(source);
	setWindowTitle(name);

	obs_source_inc_showing(source);

	updatePropertiesSignal.Connect(obs_source_get_signal_handler(source), "update_properties",
				       SourceDuplicatorWindow::UpdateProperties, this);

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(ui->preview->GetDisplay(),
					      SourceDuplicatorWindow::DrawPreview, this);
	};
	auto addTransitionDrawCallback = [this]() {
		obs_display_add_draw_callback(ui->preview->GetDisplay(),
					      SourceDuplicatorWindow::DrawTransitionPreview, this);
	};
	uint32_t caps = obs_source_get_output_flags(source);
	bool drawable_type = type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE;
	bool drawable_preview = (caps & OBS_SOURCE_VIDEO) != 0;

	if (drawable_preview && drawable_type) {
		ui->preview->show();
		connect(ui->preview, &OBSQTDisplay::DisplayCreated, addDrawCallback);

	} else if (type == OBS_SOURCE_TYPE_TRANSITION) {
		sourceA = obs_source_create_private("scene", "sourceA", nullptr);
		sourceB = obs_source_create_private("scene", "sourceB", nullptr);

		uint32_t colorA = 0xFFB26F52;
		uint32_t colorB = 0xFF6FB252;

		CreateTransitionScene(sourceA.Get(), "A", colorA);
		CreateTransitionScene(sourceB.Get(), "B", colorB);

		/**
     * The cloned source is made from scratch, rather than using
     * obs_source_duplicate, as the stinger transition would not
     * play correctly otherwise.
     */

		OBSDataAutoRelease settings = obs_source_get_settings(source);

		sourceClone =
		  obs_source_create_private(obs_source_get_id(source), "clone", settings);

		obs_source_inc_active(sourceClone);
		obs_transition_set(sourceClone, sourceA);

		auto updateCallback = [=]() {
			OBSDataAutoRelease settings = obs_source_get_settings(source);
			obs_source_update(sourceClone, settings);

			obs_transition_clear(sourceClone);
			obs_transition_set(sourceClone, sourceA);
			obs_transition_force_stop(sourceClone);
		};

		ui->preview->show();
		connect(ui->preview, &OBSQTDisplay::DisplayCreated, addTransitionDrawCallback);

	} else {
		ui->preview->hide();
	}
}

SourceDuplicatorWindow::~SourceDuplicatorWindow() {
	if (sourceClone) {
		obs_source_dec_active(sourceClone);
	}
	obs_source_dec_showing(source);

	delete ui;
}

const char* SourceDuplicatorWindow::SourceName() {
	return obs_source_get_name(source);
}

void SourceDuplicatorWindow::SourceRemoved(void* data, calldata_t*) {
	QMetaObject::invokeMethod(static_cast<SourceDuplicatorWindow*>(data), "close");
}

void SourceDuplicatorWindow::SourceRenamed(void* data, calldata_t* params) {
	const char* name = calldata_string(params, "new_name");
	QString title = QTStr("Basic.PropertiesWindow").arg(QT_UTF8(name));

	QMetaObject::invokeMethod(static_cast<SourceDuplicatorWindow*>(data), "setWindowTitle",
				  Q_ARG(QString, title));
}

void SourceDuplicatorWindow::UpdateProperties(void* data, calldata_t*) {}

void SourceDuplicatorWindow::DrawPreview(void* data, uint32_t cx, uint32_t cy) {
	SourceDuplicatorWindow* window = static_cast<SourceDuplicatorWindow*>(data);

	if (!window->source)
		return;

	uint32_t sourceCX = max(obs_source_get_width(window->source), 1u);
	uint32_t sourceCY = max(obs_source_get_height(window->source), 1u);

	int x, y;
	int newCX, newCY;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(window->source);

	gs_set_linear_srgb(previous);
	gs_projection_pop();
	gs_viewport_pop();
}

void SourceDuplicatorWindow::DrawTransitionPreview(void* data, uint32_t cx, uint32_t cy) {
	SourceDuplicatorWindow* window = static_cast<SourceDuplicatorWindow*>(data);

	if (!window->sourceClone)
		return;

	uint32_t sourceCX = max(obs_source_get_width(window->sourceClone), 1u);
	uint32_t sourceCY = max(obs_source_get_height(window->sourceClone), 1u);

	int x, y;
	int newCX, newCY;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);

	obs_source_video_render(window->sourceClone);

	gs_projection_pop();
	gs_viewport_pop();
}

void SourceDuplicatorWindow::Cleanup() {
	config_set_int(App()->GlobalConfig(), "PropertiesWindow", "cx", width());
	config_set_int(App()->GlobalConfig(), "PropertiesWindow", "cy", height());

	obs_display_remove_draw_callback(ui->preview->GetDisplay(),
					 SourceDuplicatorWindow::DrawPreview, this);
	obs_display_remove_draw_callback(ui->preview->GetDisplay(),
					 SourceDuplicatorWindow::DrawTransitionPreview, this);
}

void SourceDuplicatorWindow::closeEvent(QCloseEvent* event) {
	QWidget::closeEvent(event);
	if (!event->isAccepted())
		return;

	Cleanup();
}

bool SourceDuplicatorWindow::nativeEvent(const QByteArray&, void* message, qintptr*) {
#ifdef _WIN32
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
#else
	UNUSED_PARAMETER(message);
#endif

	return false;
}

void SourceDuplicatorWindow::Init() {
	show();
}
