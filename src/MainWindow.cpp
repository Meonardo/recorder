#include "MainWindow.h"

#include <QGuiApplication>
#include <QMessageBox>
#include <QShowEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QScreen>
#include <QColorDialog>
#include <QSizePolicy>
#include <QScrollBar>
#include <QTextStream>
#include <QActionGroup>

#include <iostream>
#include <unordered_set>
#include <string>
#include <cstddef>
#include <ctime>
#include <functional>
#include <sstream>
#include <algorithm>

#include <Windows.h>

#include <util/dstr.h>
#include <util/util.hpp>
#include <util/platform.h>
#include <util/profiler.hpp>
#include <util/dstr.hpp>
#include <libavutil/avutil.h>

#include "display-helpers.hpp"
#include "qt-wrappers.hpp"
#include "platform.hpp"
#include "obs-app.hpp"
#include "window-basic-main-outputs.hpp"
#include "SourceDuplicatorWindow.h"

#define STARTUP_SEPARATOR "==== Startup complete ==============================================="
#define SHUTDOWN_SEPARATOR "==== Shutting down =================================================="

#define UNSUPPORTED_ERROR                                                     \
	"Failed to initialize video:\n\nRequired graphics API functionality " \
	"not found.  Your GPU may not be supported."

#define UNKNOWN_ERROR                                                  \
	"Failed to initialize video.  Your GPU may not be supported, " \
	"or your graphics drivers may need to be updated."

#define RECORDING_START "==== Recording Start ==============================================="
#define RECORDING_STOP "==== Recording Stop ================================================"
#define REPLAY_BUFFER_START "==== Replay Buffer Start ==========================================="
#define REPLAY_BUFFER_STOP "==== Replay Buffer Stop ============================================"
#define STREAMING_START "==== Streaming Start ==============================================="
#define STREAMING_STOP "==== Streaming Stop ================================================"

#if OBS_RELEASE_CANDIDATE == 0 && OBS_BETA == 0
#define DEFAULT_CONTAINER "mkv"
#elif defined(__APPLE__)
#define DEFAULT_CONTAINER "fragmented_mov"
#else
#define DEFAULT_CONTAINER "fragmented_mp4"
#endif

template<typename T> static T GetOBSRef(QListWidgetItem* item) {
	return item->data(static_cast<int>(QtDataRole::OBSRef)).value<T>();
}

template<typename T> static void SetOBSRef(QListWidgetItem* item, T&& val) {
	item->setData(static_cast<int>(QtDataRole::OBSRef), QVariant::fromValue(val));
}

// externs
extern obs_frontend_callbacks* InitializeAPIInterface(MainWindow* main);
extern bool EncoderAvailable(const char* encoder);
extern bool update_nvenc_presets(ConfigFile& config);
// end of externs

// static methods
static char* get_new_source_name(const char* name, const char* format) {
	struct dstr new_name = {0};
	int inc = 0;

	dstr_copy(&new_name, name);

	for (;;) {
		OBSSourceAutoRelease existing_source = obs_get_source_by_name(new_name.array);
		if (!existing_source)
			break;

		dstr_printf(&new_name, format, name, ++inc + 1);
	}

	return new_name.array;
}
static void SaveAudioDevice(const char* name, int channel, obs_data_t* parent,
			    std::vector<OBSSource>& audioSources) {
	OBSSourceAutoRelease source = obs_get_output_source(channel);
	if (!source)
		return;

	audioSources.push_back(source.Get());

	OBSDataAutoRelease data = obs_save_source(source);

	obs_data_set_obj(parent, name, data);
}

static obs_data_t* GenerateSaveData(obs_data_array_t* sceneOrder,
				    obs_data_array_t* quickTransitionData, int transitionDuration,
				    obs_data_array_t* transitions, OBSScene& scene,
				    OBSSource& curProgramScene,
				    obs_data_array_t* savedProjectorList) {
	obs_data_t* saveData = obs_data_create();

	std::vector<OBSSource> audioSources;
	audioSources.reserve(6);

	SaveAudioDevice(DESKTOP_AUDIO_1, 1, saveData, audioSources);
	SaveAudioDevice(DESKTOP_AUDIO_2, 2, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_1, 3, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_2, 4, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_3, 5, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_4, 6, saveData, audioSources);

	/* -------------------------------- */
	/* save non-group sources           */

	auto FilterAudioSources = [&](obs_source_t* source) {
		if (obs_source_is_group(source))
			return false;

		return find(begin(audioSources), end(audioSources), source) == end(audioSources);
	};
	using FilterAudioSources_t = decltype(FilterAudioSources);

	obs_data_array_t* sourcesArray = obs_save_sources_filtered(
	  [](void* data, obs_source_t* source) {
		  auto& func = *static_cast<FilterAudioSources_t*>(data);
		  return func(source);
	  },
	  static_cast<void*>(&FilterAudioSources));

	/* -------------------------------- */
	/* save group sources separately    */

	/* saving separately ensures they won't be loaded in older versions */
	obs_data_array_t* groupsArray = obs_save_sources_filtered(
	  [](void*, obs_source_t* source) { return obs_source_is_group(source); }, nullptr);

	/* -------------------------------- */

	OBSSourceAutoRelease transition = obs_get_output_source(0);
	obs_source_t* currentScene = obs_scene_get_source(scene);
	const char* sceneName = obs_source_get_name(currentScene);
	const char* programName = obs_source_get_name(curProgramScene);

	const char* sceneCollection =
	  config_get_string(App()->GlobalConfig(), "Basic", "SceneCollection");

	obs_data_set_string(saveData, "current_scene", sceneName);
	obs_data_set_string(saveData, "current_program_scene", programName);
	obs_data_set_array(saveData, "scene_order", sceneOrder);
	obs_data_set_string(saveData, "name", sceneCollection);
	obs_data_set_array(saveData, "sources", sourcesArray);
	obs_data_set_array(saveData, "groups", groupsArray);
	obs_data_set_array(saveData, "quick_transitions", quickTransitionData);
	obs_data_set_array(saveData, "transitions", transitions);
	obs_data_set_array(saveData, "saved_projectors", savedProjectorList);
	obs_data_array_release(sourcesArray);
	obs_data_array_release(groupsArray);

	obs_data_set_string(saveData, "current_transition", obs_source_get_name(transition));
	obs_data_set_int(saveData, "transition_duration", transitionDuration);

	return saveData;
}

static inline int AttemptToResetVideo(struct obs_video_info* ovi) {
	return obs_reset_video(ovi);
}

static inline enum obs_scale_type GetScaleType(ConfigFile& basicConfig) {
	const char* scaleTypeStr = config_get_string(basicConfig, "Video", "ScaleType");

	if (astrcmpi(scaleTypeStr, "bilinear") == 0)
		return OBS_SCALE_BILINEAR;
	else if (astrcmpi(scaleTypeStr, "lanczos") == 0)
		return OBS_SCALE_LANCZOS;
	else if (astrcmpi(scaleTypeStr, "area") == 0)
		return OBS_SCALE_AREA;
	else
		return OBS_SCALE_BICUBIC;
}

static inline enum video_format GetVideoFormatFromName(const char* name) {
	if (astrcmpi(name, "I420") == 0)
		return VIDEO_FORMAT_I420;
	else if (astrcmpi(name, "NV12") == 0)
		return VIDEO_FORMAT_NV12;
	else if (astrcmpi(name, "I444") == 0)
		return VIDEO_FORMAT_I444;
	else if (astrcmpi(name, "I010") == 0)
		return VIDEO_FORMAT_I010;
	else if (astrcmpi(name, "P010") == 0)
		return VIDEO_FORMAT_P010;
	else if (astrcmpi(name, "P216") == 0)
		return VIDEO_FORMAT_P216;
	else if (astrcmpi(name, "P416") == 0)
		return VIDEO_FORMAT_P416;
#if 0 //currently unsupported
  else if (astrcmpi(name, "YVYU") == 0)
    return VIDEO_FORMAT_YVYU;
  else if (astrcmpi(name, "YUY2") == 0)
    return VIDEO_FORMAT_YUY2;
  else if (astrcmpi(name, "UYVY") == 0)
    return VIDEO_FORMAT_UYVY;
#endif
	else
		return VIDEO_FORMAT_BGRA;
}

static inline enum video_colorspace GetVideoColorSpaceFromName(const char* name) {
	enum video_colorspace colorspace = VIDEO_CS_SRGB;
	if (strcmp(name, "601") == 0)
		colorspace = VIDEO_CS_601;
	else if (strcmp(name, "709") == 0)
		colorspace = VIDEO_CS_709;
	else if (strcmp(name, "2100PQ") == 0)
		colorspace = VIDEO_CS_2100_PQ;
	else if (strcmp(name, "2100HLG") == 0)
		colorspace = VIDEO_CS_2100_HLG;

	return colorspace;
}

static void LogFilter(obs_source_t*, obs_source_t* filter, void* v_val) {
	const char* name = obs_source_get_name(filter);
	const char* id = obs_source_get_id(filter);
	int val = (int)(intptr_t)v_val;
	std::string indent;

	for (int i = 0; i < val; i++) indent += "    ";

	blog(LOG_INFO, "%s- filter: '%s' (%s)", indent.c_str(), name, id);
}

static bool LogSceneItem(obs_scene_t*, obs_sceneitem_t* item, void* v_val) {
	obs_source_t* source = obs_sceneitem_get_source(item);
	const char* name = obs_source_get_name(source);
	const char* id = obs_source_get_id(source);
	int indent_count = (int)(intptr_t)v_val;
	std::string indent;

	for (int i = 0; i < indent_count; i++) indent += "    ";

	blog(LOG_INFO, "%s- source: '%s' (%s)", indent.c_str(), name, id);

	obs_monitoring_type monitoring_type = obs_source_get_monitoring_type(source);

	if (monitoring_type != OBS_MONITORING_TYPE_NONE) {
		const char* type = (monitoring_type == OBS_MONITORING_TYPE_MONITOR_ONLY)
				     ? "monitor only"
				     : "monitor and output";

		blog(LOG_INFO, "    %s- monitoring: %s", indent.c_str(), type);
	}
	int child_indent = 1 + indent_count;
	obs_source_enum_filters(source, LogFilter, (void*)(intptr_t)child_indent);

	obs_source_t* show_tn = obs_sceneitem_get_transition(item, true);
	obs_source_t* hide_tn = obs_sceneitem_get_transition(item, false);
	if (show_tn)
		blog(LOG_INFO, "    %s- show: '%s' (%s)", indent.c_str(),
		     obs_source_get_name(show_tn), obs_source_get_id(show_tn));
	if (hide_tn)
		blog(LOG_INFO, "    %s- hide: '%s' (%s)", indent.c_str(),
		     obs_source_get_name(hide_tn), obs_source_get_id(hide_tn));

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, LogSceneItem, (void*)(intptr_t)child_indent);
	return true;
}

static void LoadAudioDevice(const char* name, int channel, obs_data_t* parent) {
	OBSDataAutoRelease data = obs_data_get_obj(parent, name);
	if (!data)
		return;

	OBSSourceAutoRelease source = obs_load_source(data);
	if (!source)
		return;

	obs_set_output_source(channel, source);

	const char* source_name = obs_source_get_name(source);
	blog(LOG_INFO, "[Loaded global audio device]: '%s'", source_name);
	obs_source_enum_filters(source, LogFilter, (void*)(intptr_t)1);
	obs_monitoring_type monitoring_type = obs_source_get_monitoring_type(source);
	if (monitoring_type != OBS_MONITORING_TYPE_NONE) {
		const char* type = (monitoring_type == OBS_MONITORING_TYPE_MONITOR_ONLY)
				     ? "monitor only"
				     : "monitor and output";

		blog(LOG_INFO, "    - monitoring: %s", type);
	}
}

static inline void AddMissingFiles(void* data, obs_source_t* source) {
	obs_missing_files_t* f = (obs_missing_files_t*)data;
	obs_missing_files_t* sf = obs_source_get_missing_files(source);

	obs_missing_files_append(f, sf);
	obs_missing_files_destroy(sf);
}

static const double scaled_vals[] = {1.0, 1.25, (1.0 / 0.75), 1.5,  (1.0 / 0.6), 1.75,
				     2.0, 2.25, 2.5,          2.75, 3.0,         0.0};

static inline bool HasAudioDevices(const char* source_id) {
	const char* output_id = source_id;
	obs_properties_t* props = obs_get_source_properties(output_id);
	size_t count = 0;

	if (!props)
		return false;

	obs_property_t* devices = obs_properties_get(props, "device_id");
	if (devices)
		count = obs_property_list_item_count(devices);

	obs_properties_destroy(props);

	return count != 0;
}

static void AddExtraModulePaths() {
	std::string plugins_path, plugins_data_path;
	char* s;

	s = getenv("OBS_PLUGINS_PATH");
	if (s)
		plugins_path = s;

	s = getenv("OBS_PLUGINS_DATA_PATH");
	if (s)
		plugins_data_path = s;

	if (!plugins_path.empty() && !plugins_data_path.empty()) {
		std::string data_path_with_module_suffix;
		data_path_with_module_suffix += plugins_data_path;
		data_path_with_module_suffix += "/%module%";
		obs_add_module_path(plugins_path.c_str(), data_path_with_module_suffix.c_str());
	}

	if (portable_mode)
		return;

	char base_module_dir[512];
#if defined(_WIN32)
	int ret = GetProgramDataPath(base_module_dir, sizeof(base_module_dir),
				     "obs-studio/plugins/%module%");
#elif defined(__APPLE__)
	int ret = GetConfigPath(base_module_dir, sizeof(base_module_dir),
				"obs-studio/plugins/%module%.plugin");
#else
	int ret =
	  GetConfigPath(base_module_dir, sizeof(base_module_dir), "obs-studio/plugins/%module%");
#endif

	if (ret <= 0)
		return;

	std::string path = base_module_dir;
#if defined(__APPLE__)
	/* User Application Support Search Path */
	obs_add_module_path((path + "/Contents/MacOS").c_str(),
			    (path + "/Contents/Resources").c_str());

#ifndef __aarch64__
	/* Legacy System Library Search Path */
	char system_legacy_module_dir[PATH_MAX];
	GetProgramDataPath(system_legacy_module_dir, sizeof(system_legacy_module_dir),
			   "obs-studio/plugins/%module%");
	std::string path_system_legacy = system_legacy_module_dir;
	obs_add_module_path((path_system_legacy + "/bin").c_str(),
			    (path_system_legacy + "/data").c_str());

	/* Legacy User Application Support Search Path */
	char user_legacy_module_dir[PATH_MAX];
	GetConfigPath(user_legacy_module_dir, sizeof(user_legacy_module_dir),
		      "obs-studio/plugins/%module%");
	std::string path_user_legacy = user_legacy_module_dir;
	obs_add_module_path((path_user_legacy + "/bin").c_str(),
			    (path_user_legacy + "/data").c_str());
#endif
#else
#if ARCH_BITS == 64
	obs_add_module_path((path + "/bin/64bit").c_str(), (path + "/data").c_str());
#else
	obs_add_module_path((path + "/bin/32bit").c_str(), (path + "/data").c_str());
#endif
#endif
}

/* First-party modules considered to be potentially unsafe to load in Safe Mode
 * due to them allowing external code (e.g. scripts) to modify OBS's state. */
static const std::unordered_set<std::string> unsafe_modules = {
  "frontend-tools", // Scripting
  "obs-websocket",  // Allows outside modifications
};

static void SetSafeModuleNames() {
#ifndef SAFE_MODULES
	return;
#else
	std::string module;
	std::stringstream modules(SAFE_MODULES);

	while (std::getline(modules, module, '|')) {
		/* When only disallowing third-party plugins, still add
     * "unsafe" bundled modules to the safe list. */
		if (disable_3p_plugins || !unsafe_modules.count(module))
			obs_add_safe_module(module.c_str());
	}
#endif
}
// end of static methods

MainWindow::MainWindow(QWidget* parent) : OBSMainWindow(parent), ui(new Ui::MainWindowClass()) {
	setAttribute(Qt::WA_NativeWindow);

	setAcceptDrops(true);
	setContextMenuPolicy(Qt::CustomContextMenu);

	api = InitializeAPIInterface(this);

	qRegisterMetaType<int64_t>("int64_t");
	qRegisterMetaType<uint32_t>("uint32_t");
	qRegisterMetaType<OBSScene>("OBSScene");
	qRegisterMetaType<OBSSceneItem>("OBSSceneItem");
	qRegisterMetaType<OBSSource>("OBSSource");

	ui->setupUi(this);

	auto displayResize = [this]() {
		struct obs_video_info ovi;

		if (obs_get_video_info(&ovi))
			ResizePreview(ovi.base_width, ovi.base_height);

		dpi = devicePixelRatioF();
	};
	dpi = devicePixelRatioF();

	connect(windowHandle(), &QWindow::screenChanged, displayResize);
	connect(ui->preview, &OBSQTDisplay::DisplayResized, displayResize);

	UpdatePreviewSafeAreas();
	UpdatePreviewSpacingHelpers();
	UpdatePreviewOverflowSettings();
}

MainWindow::~MainWindow() {
	/* clear out UI event queue */
	QApplication::sendPostedEvents(nullptr);
	QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

	service = nullptr;
	outputHandler.reset();

	obs_display_remove_draw_callback(ui->preview->GetDisplay(), MainWindow::RenderMain, this);

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

	QApplication::sendPostedEvents(nullptr);

	config_set_int(App()->GlobalConfig(), "General", "LastVersion", LIBOBS_API_VER);

	config_set_bool(App()->GlobalConfig(), "BasicWindow", "PreviewEnabled", previewEnabled);
	config_save_safe(App()->GlobalConfig(), "tmp", nullptr);

	delete manager;
	delete ui;
}

void MainWindow::OBSInit() {
	ProfileScope("OBSBasic::OBSInit");

	const char* sceneCollection =
	  config_get_string(App()->GlobalConfig(), "Basic", "SceneCollectionFile");
	char savePath[1024];
	char fileName[1024];
	int ret;

	if (!sceneCollection)
		throw "Failed to get scene collection name";

	ret =
	  snprintf(fileName, sizeof(fileName), "obs-studio/basic/scenes/%s.json", sceneCollection);
	if (ret <= 0)
		throw "Failed to create scene collection file name";

	ret = GetConfigPath(savePath, sizeof(savePath), fileName);
	if (ret <= 0)
		throw "Failed to get scene collection json file path";

	if (!InitBasicConfig())
		throw "Failed to load basic.ini";
	if (!ResetAudio())
		throw "Failed to initialize audio";

	ret = ResetVideo();

	switch (ret) {
	case OBS_VIDEO_MODULE_NOT_FOUND:
		throw "Failed to initialize video:  Graphics module not found";
	case OBS_VIDEO_NOT_SUPPORTED: throw UNSUPPORTED_ERROR;
	case OBS_VIDEO_INVALID_PARAM: throw "Failed to initialize video:  Invalid parameters";
	default:
		if (ret != OBS_VIDEO_SUCCESS)
			throw UNKNOWN_ERROR;
	}

	/* load audio monitoring */
	if (obs_audio_monitoring_available()) {
		const char* device_name =
		  config_get_string(basicConfig, "Audio", "MonitoringDeviceName");
		const char* device_id =
		  config_get_string(basicConfig, "Audio", "MonitoringDeviceId");

		obs_set_audio_monitoring_device(device_name, device_id);

		blog(LOG_INFO, "Audio monitoring device:\n\tname: %s\n\tid: %s", device_name,
		     device_id);
	}

	InitOBSCallbacks();

	/* hack to prevent elgato from loading its own QtNetwork that it tries
   * to ship with */
#if defined(_WIN32) && !defined(_DEBUG)
	LoadLibraryW(L"Qt6Network");
#endif
	struct obs_module_failure_info mfi;

	/* Safe Mode disables third-party plugins so we don't need to add earch
   * paths outside the OBS bundle/installation. */
	if (safe_mode || disable_3p_plugins) {
		SetSafeModuleNames();
	} else {
		AddExtraModulePaths();
	}

	blog(LOG_INFO, "---------------------------------");
	obs_load_all_modules2(&mfi);
	blog(LOG_INFO, "---------------------------------");
	obs_log_loaded_modules();
	blog(LOG_INFO, "---------------------------------");
	obs_post_load_modules();

	BPtr<char*> failed_modules = mfi.failed_modules;

#ifdef BROWSER_AVAILABLE
	cef = obs_browser_init_panel();
	cef_js_avail = cef && obs_browser_qcef_version() >= 3;
#endif

	OBSDataAutoRelease obsData = obs_get_private_data();

	InitBasicConfigDefaults2();

	CheckForSimpleModeX264Fallback();

	blog(LOG_INFO, STARTUP_SEPARATOR);

	if (!InitService())
		throw "Failed to initialize service";

	ResetOutputs();

	InitPrimitives();

	{
		ProfileScope("OBSBasic::Load");
		disableSaving--;
		Load(savePath);
		disableSaving++;
	}

	loaded = true;

	previewEnabled = config_get_bool(App()->GlobalConfig(), "BasicWindow", "PreviewEnabled");

	if (!previewEnabled && !IsPreviewProgramMode())
		QMetaObject::invokeMethod(this, "EnablePreviewDisplay", Qt::QueuedConnection,
					  Q_ARG(bool, previewEnabled));
	else if (!previewEnabled && IsPreviewProgramMode())
		QMetaObject::invokeMethod(this, "EnablePreviewDisplay", Qt::QueuedConnection,
					  Q_ARG(bool, true));

	obs_display_set_enabled(ui->preview->GetDisplay(), previewEnabled);
	ui->preview->setVisible(previewEnabled);

	RefreshProfiles();
	disableSaving--;

	auto addDisplay = [this](OBSQTDisplay* window) {
		obs_display_add_draw_callback(window->GetDisplay(), MainWindow::RenderMain, this);

		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi))
			ResizePreview(ovi.base_width, ovi.base_height);
	};

	connect(ui->preview, &OBSQTDisplay::DisplayCreated, addDisplay);

#ifdef _WIN32
	SetWin32DropStyle(this);
	show();
#endif

	bool pre23Defaults = config_get_bool(App()->GlobalConfig(), "General", "Pre23Defaults");
	if (pre23Defaults) {
		bool resetDockLock23 =
		  config_get_bool(App()->GlobalConfig(), "General", "ResetDockLock23");
		if (!resetDockLock23) {
			config_set_bool(App()->GlobalConfig(), "General", "ResetDockLock23", true);
			config_remove_value(App()->GlobalConfig(), "BasicWindow", "DocksLocked");
			config_save_safe(App()->GlobalConfig(), "tmp", nullptr);
		}
	}

	TaskbarOverlayInit();

#ifdef __APPLE__
	disableColorSpaceConversion(this);
#endif

	bool has_last_version =
	  config_has_user_value(App()->GlobalConfig(), "General", "LastVersion");
	bool first_run = config_get_bool(App()->GlobalConfig(), "General", "FirstRun");

	if (!first_run) {
		config_set_bool(App()->GlobalConfig(), "General", "FirstRun", true);
		config_save_safe(App()->GlobalConfig(), "tmp", nullptr);
	}

	if (!first_run && !has_last_version && !Active())
		QMetaObject::invokeMethod(this, "on_autoConfigure_triggered", Qt::QueuedConnection);

#if (defined(_WIN32) || defined(__APPLE__)) && (OBS_RELEASE_CANDIDATE > 0 || OBS_BETA > 0)
	/* Automatically set branch to "beta" the first time a pre-release build is run. */
	if (!config_get_bool(App()->GlobalConfig(), "General", "AutoBetaOptIn")) {
		config_set_string(App()->GlobalConfig(), "General", "UpdateBranch", "beta");
		config_set_bool(App()->GlobalConfig(), "General", "AutoBetaOptIn", true);
		config_save_safe(App()->GlobalConfig(), "tmp", nullptr);
	}
#endif

	OnFirstLoad();

	activateWindow();

	/* ------------------------------------------- */
	/* display warning message for failed modules  */

	if (mfi.count) {
		QString failed_plugins;

		char** plugin = mfi.failed_modules;
		while (*plugin) {
			failed_plugins += *plugin;
			failed_plugins += "\n";
			plugin++;
		}

		QString failed_msg = QTStr("PluginsFailedToLoad.Text").arg(failed_plugins);
		OBSMessageBox::warning(this, QTStr("PluginsFailedToLoad.Title"), failed_msg);
	}

	// init source manager
	manager = new recorder::manager::OBSSourceManager();
	manager->AddEventsSender(api);

	// register ui events
	ConfigureUI();

	// hide from screen capture
	SetDisplayAffinity((HWND)this->winId());
}

void MainWindow::ConfigureUI() {
	{ // load screen list
		auto screenItems =
		  std::vector<std::shared_ptr<recorder::source::ScreenSceneItem>>();
		manager->ListScreenItems(screenItems);
		if (!screenItems.empty()) {
			for (auto& item : screenItems) {
				ui->comboBox->addItem(QString::fromStdString(item->Name()));
			}
		}
	}

	connect(ui->screenAddButton, &QPushButton::clicked, this, [this]() {
		auto screenItems =
		  std::vector<std::shared_ptr<recorder::source::ScreenSceneItem>>();
		manager->ListScreenItems(screenItems);

		auto index = ui->comboBox->currentIndex();
		auto screen = new recorder::source::ScreenSceneItem(*screenItems[index].get());

		if (manager->AttachSceneItem(screen,
					     recorder::source::SceneItem::Category::kMain)) {
			screen->UpdateScale({0.5, 0.5});
			manager->ApplySceneItemSettingsUpdate(screen);
		} else {
			delete screen;
		}
	});

	connect(ui->screenCopyButton, &QPushButton::clicked, this, [this]() {
		std::string name(ui->comboBox->currentText().toStdString());
		OBSSource source = obs_get_source_by_name(name.c_str());
		if (!source)
			return;

		/*for (auto window : duplicators) {
			std::string sourceName = window->SourceName();
			if (sourceName == name) {
				window->show();
				return;
			}
		}*/
		auto window = new SourceDuplicatorWindow(source);
		window->show();
		duplicators.push_back(window);
	});

	connect(ui->screenRemoveButton, &QPushButton::clicked, this, [this]() {
		std::string name(ui->comboBox->currentText().toStdString());
		OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
		if (!source)
			return;

		// remove copy
		auto ret = std::remove_if(duplicators.begin(), duplicators.end(),
					  [name](SourceDuplicatorWindow* d) {
						  return d->SourceName() == name;
					  });
		if (ret != duplicators.end()) {
			auto removed = *ret;
			removed->hide();
			removed->deleteLater();

			duplicators.erase(ret, duplicators.end());
		} else {
			// remove source
      if (manager->RemoveSceneItemByName(name))
        SaveProject();
		}
	});

	connect(ui->rtspAddButton, &QPushButton::clicked, this, [this]() {
		auto url = ui->rtspTextEdit->text();
		std::string url_string(url.toStdString());
		auto camera = new recorder::source::IPCameraSceneItem(url_string, url_string, true);

		if (manager->AttachSceneItem(camera, recorder::source::SceneItem::Category::kPiP)) {
			camera->UpdateScale({0.3f, 0.3f});
			manager->ApplySceneItemSettingsUpdate(camera);
		}
	});

	connect(ui->rtspCopyButton, &QPushButton::clicked, this, [this]() {
		std::string name(ui->rtspTextEdit->text().toStdString());
		OBSSource source = obs_get_source_by_name(name.c_str());
		if (!source)
			return;

		/*for (auto window : duplicators) {
			std::string sourceName = window->SourceName();
			if (sourceName == name) {
				window->show();
				return;
			}
		}*/
		auto window = new SourceDuplicatorWindow(source);
		window->show();
		duplicators.push_back(window);
	});

	connect(ui->rtspRemoveButton, &QPushButton::clicked, this, [this]() {
		std::string name(ui->rtspTextEdit->text().toStdString());
		OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
		if (!source)
			return;

		// remove copy
		auto ret = std::remove_if(duplicators.begin(), duplicators.end(),
					  [name](SourceDuplicatorWindow* d) {
						  return d->SourceName() == name;
					  });
		if (ret != duplicators.end()) {
			auto removed = *ret;
			removed->hide();
			removed->deleteLater();

			duplicators.erase(ret, duplicators.end());
		} else {
			// remove source
      if (manager->RemoveSceneItemByName(name))
        SaveProject();
		}
	});

	connect(ui->startRecordingButton, &QPushButton::clicked, this,
		[this]() { obs_frontend_recording_start(); });
	connect(ui->stopRecordingButton, &QPushButton::clicked, this, [this]() {
		if (!obs_frontend_recording_active())
			return;

		obs_frontend_recording_stop();
	});
	connect(ui->startStreamingButton, &QPushButton::clicked, this, [this]() {
		auto serverAddress = ui->streamAddressTextEdit->text();
		if (serverAddress.isEmpty())
			return;
		std::string address(serverAddress.toStdString());
		std::string username("");
		std::string passwd("");
		manager->SetStreamAddress(address, username, passwd);
		manager->StartStreaming();
	});
	connect(ui->stopStreamingButton, &QPushButton::clicked, this,
		[this]() { manager->StopStreaming(); });
}

void MainWindow::OnFirstLoad() {
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_FINISHED_LOADING);
}

bool MainWindow::Active() const {
	if (!outputHandler)
		return false;
	return outputHandler->Active();
}

config_t* MainWindow::Config() const {
	return basicConfig;
}

int MainWindow::GetProfilePath(char* path, size_t size, const char* file) const {
	char profiles_path[512];
	const char* profile = config_get_string(App()->GlobalConfig(), "Basic", "ProfileDir");
	int ret;

	if (!profile)
		return -1;
	if (!path)
		return -1;
	if (!file)
		file = "";

	ret = GetConfigPath(profiles_path, 512, "obs-studio/basic/profiles");
	if (ret <= 0)
		return ret;

	if (!*file)
		return snprintf(path, size, "%s/%s", profiles_path, profile);

	return snprintf(path, size, "%s/%s/%s", profiles_path, profile, file);
}

OBSSource MainWindow::GetProgramSource() {
	return OBSGetStrongRef(programScene);
}

OBSScene MainWindow::GetCurrentScene() {
	return currentScene.load();
}

MainWindow* MainWindow::Get() {
	return reinterpret_cast<MainWindow*>(App()->GetMainWindow());
}

#define MBYTE (1024ULL * 1024ULL)
#define MBYTES_LEFT_STOP_REC 50ULL
#define MAX_BYTES_LEFT (MBYTES_LEFT_STOP_REC * MBYTE)

const char* MainWindow::GetCurrentOutputPath() {
	const char* path = nullptr;
	const char* mode = config_get_string(Config(), "Output", "Mode");

	if (strcmp(mode, "Advanced") == 0) {
		const char* advanced_mode = config_get_string(Config(), "AdvOut", "RecType");

		if (strcmp(advanced_mode, "FFmpeg") == 0) {
			path = config_get_string(Config(), "AdvOut", "FFFilePath");
		} else {
			path = config_get_string(Config(), "AdvOut", "RecFilePath");
		}
	} else {
		path = config_get_string(Config(), "SimpleOutput", "FilePath");
	}

	return path;
}

void MainWindow::SaveProjectNow() {
	if (disableSaving)
		return;

	projectChanged = true;
	SaveProjectDeferred();
}

void MainWindow::SaveProject() {
	if (disableSaving)
		return;

	projectChanged = true;
	QMetaObject::invokeMethod(this, "SaveProjectDeferred", Qt::QueuedConnection);
}

void MainWindow::SaveProjectDeferred() {
	if (disableSaving)
		return;

	if (!projectChanged)
		return;

	projectChanged = false;

	const char* sceneCollection =
	  config_get_string(App()->GlobalConfig(), "Basic", "SceneCollectionFile");

	char savePath[1024];
	char fileName[1024];
	int ret;

	if (!sceneCollection)
		return;

	ret =
	  snprintf(fileName, sizeof(fileName), "obs-studio/basic/scenes/%s.json", sceneCollection);
	if (ret <= 0)
		return;

	ret = GetConfigPath(savePath, sizeof(savePath), fileName);
	if (ret <= 0)
		return;

	Save(savePath);
}

void MainWindow::Save(const char* file) {
	OBSScene scene = GetCurrentScene();
	OBSSource curProgramScene = OBSGetStrongRef(programScene);
	if (!curProgramScene)
		curProgramScene = obs_scene_get_source(scene);

	OBSDataArrayAutoRelease sceneOrder = SaveSceneListOrder();
	OBSDataArrayAutoRelease transitions = nullptr;
	OBSDataArrayAutoRelease quickTrData = nullptr;
	OBSDataArrayAutoRelease savedProjectorList = nullptr;
	OBSDataAutoRelease saveData = GenerateSaveData(sceneOrder, quickTrData, 0, transitions,
						       scene, curProgramScene, savedProjectorList);

	obs_data_set_bool(saveData, "preview_locked", ui->preview->Locked());
	obs_data_set_bool(saveData, "scaling_enabled", ui->preview->IsFixedScaling());
	obs_data_set_int(saveData, "scaling_level", ui->preview->GetScalingLevel());
	obs_data_set_double(saveData, "scaling_off_x", ui->preview->GetScrollX());
	obs_data_set_double(saveData, "scaling_off_y", ui->preview->GetScrollY());

	if (api) {
		if (safeModeModuleData) {
			/* If we're in Safe Mode and have retained unloaded
       * plugin data, update the existing data object instead
       * of creating a new one. */
			api->on_save(safeModeModuleData);
			obs_data_set_obj(saveData, "modules", safeModeModuleData);
		} else {
			OBSDataAutoRelease moduleObj = obs_data_create();
			api->on_save(moduleObj);
			obs_data_set_obj(saveData, "modules", moduleObj);
		}
	}

	if (!obs_data_save_json_safe(saveData, file, "tmp", "bak"))
		blog(LOG_ERROR, "Could not save scene data to %s", file);
}

void MainWindow::DeferSaveBegin() {
	os_atomic_inc_long(&disableSaving);
}

void MainWindow::DeferSaveEnd() {
	long result = os_atomic_dec_long(&disableSaving);
	if (result == 0) {
		SaveProject();
	}
}

obs_service_t* MainWindow::GetService() {
	if (!service) {
		service = obs_service_create("rtmp_common", NULL, NULL, nullptr);
		obs_service_release(service);
	}
	return service;
}

void MainWindow::SetService(obs_service_t* newService) {
	if (newService) {
		service = newService;
	}
}

#define SERVICE_PATH "service.json"

void MainWindow::SaveService() {
	if (!service)
		return;

	char serviceJsonPath[512];
	int ret = GetProfilePath(serviceJsonPath, sizeof(serviceJsonPath), SERVICE_PATH);
	if (ret <= 0)
		return;

	OBSDataAutoRelease data = obs_data_create();
	OBSDataAutoRelease settings = obs_service_get_settings(service);

	obs_data_set_string(data, "type", obs_service_get_type(service));
	obs_data_set_obj(data, "settings", settings);

	if (!obs_data_save_json_safe(data, serviceJsonPath, "tmp", "bak"))
		blog(LOG_WARNING, "Failed to save service");
}

bool MainWindow::LoadService() {
	const char* type;

	char serviceJsonPath[512];
	int ret = GetProfilePath(serviceJsonPath, sizeof(serviceJsonPath), SERVICE_PATH);
	if (ret <= 0)
		return false;

	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(serviceJsonPath, "bak");

	if (!data)
		return false;

	obs_data_set_default_string(data, "type", "rtmp_common");
	type = obs_data_get_string(data, "type");

	OBSDataAutoRelease settings = obs_data_get_obj(data, "settings");
	OBSDataAutoRelease hotkey_data = obs_data_get_obj(data, "hotkeys");

	service = obs_service_create(type, "default_service", settings, hotkey_data);
	obs_service_release(service);

	if (!service)
		return false;

	/* Enforce Opus on FTL if needed */
	if (strcmp(obs_service_get_protocol(service), "FTL") == 0 ||
	    strcmp(obs_service_get_protocol(service), "WHIP") == 0) {
		const char* option =
		  config_get_string(basicConfig, "SimpleOutput", "StreamAudioEncoder");
		if (strcmp(option, "opus") != 0)
			config_set_string(basicConfig, "SimpleOutput", "StreamAudioEncoder",
					  "opus");

		option = config_get_string(basicConfig, "AdvOut", "AudioEncoder");
		if (strcmp(obs_get_encoder_codec(option), "opus") != 0)
			config_set_string(basicConfig, "AdvOut", "AudioEncoder", "ffmpeg_opus");
	}

	return true;
}

bool MainWindow::InitService() {
	ProfileScope("MainWindow::InitService");

	if (LoadService())
		return true;

	service = obs_service_create("rtmp_common", "default_service", nullptr, nullptr);
	if (!service)
		return false;
	obs_service_release(service);

	return true;
}

int MainWindow::ResetVideo() {
	if (outputHandler && outputHandler->Active())
		return OBS_VIDEO_CURRENTLY_ACTIVE;

	ProfileScope("MainWindow::ResetVideo");

	struct obs_video_info ovi;
	int ret;

	GetConfigFPS(ovi.fps_num, ovi.fps_den);

	const char* colorFormat = config_get_string(basicConfig, "Video", "ColorFormat");
	const char* colorSpace = config_get_string(basicConfig, "Video", "ColorSpace");
	const char* colorRange = config_get_string(basicConfig, "Video", "ColorRange");

	ovi.graphics_module = App()->GetRenderModule();
	ovi.base_width = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCX");
	ovi.base_height = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCY");
	ovi.output_width = (uint32_t)config_get_uint(basicConfig, "Video", "OutputCX");
	ovi.output_height = (uint32_t)config_get_uint(basicConfig, "Video", "OutputCY");
	ovi.output_format = GetVideoFormatFromName(colorFormat);
	ovi.colorspace = GetVideoColorSpaceFromName(colorSpace);
	ovi.range = astrcmpi(colorRange, "Full") == 0 ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
	ovi.adapter = config_get_uint(App()->GlobalConfig(), "Video", "AdapterIdx");
	ovi.gpu_conversion = true;
	ovi.scale_type = GetScaleType(basicConfig);

	if (ovi.base_width < 32 || ovi.base_height < 32) {
		ovi.base_width = 1920;
		ovi.base_height = 1080;
		config_set_uint(basicConfig, "Video", "BaseCX", 1920);
		config_set_uint(basicConfig, "Video", "BaseCY", 1080);
	}

	if (ovi.output_width < 32 || ovi.output_height < 32) {
		ovi.output_width = ovi.base_width;
		ovi.output_height = ovi.base_height;
		config_set_uint(basicConfig, "Video", "OutputCX", ovi.base_width);
		config_set_uint(basicConfig, "Video", "OutputCY", ovi.base_height);
	}

	ret = AttemptToResetVideo(&ovi);
	if (ret == OBS_VIDEO_CURRENTLY_ACTIVE) {
		blog(LOG_WARNING, "Tried to reset when already active");
		return ret;
	}

	if (ret == OBS_VIDEO_SUCCESS) {
		const float sdr_white_level =
		  (float)config_get_uint(basicConfig, "Video", "SdrWhiteLevel");
		const float hdr_nominal_peak_level =
		  (float)config_get_uint(basicConfig, "Video", "HdrNominalPeakLevel");
		obs_set_video_levels(sdr_white_level, hdr_nominal_peak_level);
	}

	return ret;
}

bool MainWindow::ResetAudio() {
	ProfileScope("MainWindow::ResetAudio");

	struct obs_audio_info2 ai = {};
	ai.samples_per_sec = config_get_uint(basicConfig, "Audio", "SampleRate");

	const char* channelSetupStr = config_get_string(basicConfig, "Audio", "ChannelSetup");

	if (strcmp(channelSetupStr, "Mono") == 0)
		ai.speakers = SPEAKERS_MONO;
	else if (strcmp(channelSetupStr, "2.1") == 0)
		ai.speakers = SPEAKERS_2POINT1;
	else if (strcmp(channelSetupStr, "4.0") == 0)
		ai.speakers = SPEAKERS_4POINT0;
	else if (strcmp(channelSetupStr, "4.1") == 0)
		ai.speakers = SPEAKERS_4POINT1;
	else if (strcmp(channelSetupStr, "5.1") == 0)
		ai.speakers = SPEAKERS_5POINT1;
	else if (strcmp(channelSetupStr, "7.1") == 0)
		ai.speakers = SPEAKERS_7POINT1;
	else
		ai.speakers = SPEAKERS_STEREO;

	bool lowLatencyAudioBuffering =
	  config_get_bool(GetGlobalConfig(), "Audio", "LowLatencyAudioBuffering");
	if (lowLatencyAudioBuffering) {
		ai.max_buffering_ms = 20;
		ai.fixed_buffering = true;
	}

	return obs_reset_audio2(&ai);
}

void MainWindow::GetFPSInteger(uint32_t& num, uint32_t& den) const {
	num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSInt");
	den = 1;
}

void MainWindow::GetFPSFraction(uint32_t& num, uint32_t& den) const {
	num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNum");
	den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSDen");
}

void MainWindow::GetFPSNanoseconds(uint32_t& num, uint32_t& den) const {
	num = 1000000000;
	den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNS");
}

void MainWindow::GetConfigFPS(uint32_t& num, uint32_t& den) const {
	uint32_t type = config_get_uint(basicConfig, "Video", "FPSType");

	if (type == 1) //"Integer"
		GetFPSInteger(num, den);
	else if (type == 2) //"Fraction"
		GetFPSFraction(num, den);
	/*
   * 	else if (false) //"Nanoseconds", currently not implemented
   *		GetFPSNanoseconds(num, den);
   */
	else
		GetFPSCommon(num, den);
}

void MainWindow::GetFPSCommon(uint32_t& num, uint32_t& den) const {
	const char* val = config_get_string(basicConfig, "Video", "FPSCommon");

	if (strcmp(val, "10") == 0) {
		num = 10;
		den = 1;
	} else if (strcmp(val, "20") == 0) {
		num = 20;
		den = 1;
	} else if (strcmp(val, "24 NTSC") == 0) {
		num = 24000;
		den = 1001;
	} else if (strcmp(val, "25 PAL") == 0) {
		num = 25;
		den = 1;
	} else if (strcmp(val, "29.97") == 0) {
		num = 30000;
		den = 1001;
	} else if (strcmp(val, "48") == 0) {
		num = 48;
		den = 1;
	} else if (strcmp(val, "50 PAL") == 0) {
		num = 50;
		den = 1;
	} else if (strcmp(val, "59.94") == 0) {
		num = 60000;
		den = 1001;
	} else if (strcmp(val, "60") == 0) {
		num = 60;
		den = 1;
	} else {
		num = 30;
		den = 1;
	}
}

void MainWindow::LogScenes() {
	blog(LOG_INFO, "------------------------------------------------");
	blog(LOG_INFO, "Loaded scenes:");

	for (int i = 0; i < scenes.size(); i++) {
		QListWidgetItem* item = scenes[i];
		OBSScene scene = GetOBSRef<OBSScene>(item);

		obs_source_t* source = obs_scene_get_source(scene);
		const char* name = obs_source_get_name(source);

		blog(LOG_INFO, "- scene '%s':", name);
		obs_scene_enum_items(scene, LogSceneItem, (void*)(intptr_t)1);
		obs_source_enum_filters(source, LogFilter, (void*)(intptr_t)1);
	}

	blog(LOG_INFO, "------------------------------------------------");
}

void MainWindow::Load(const char* file) {
	disableSaving++;

	obs_data_t* data = obs_data_create_from_json_file_safe(file, "bak");
	if (!data) {
		disableSaving--;
		blog(LOG_INFO, "No scene file found, creating default scene");
		CreateDefaultScene(true);
		SaveProject();
		return;
	}

	LoadData(data, file);
}

void MainWindow::LoadData(obs_data_t* data, const char* file) {
	ClearSceneData();
	InitDefaultTransitions();
	/* Exit OBS if clearing scene data failed for some reason. */
	if (clearingFailed) {
		close();
		return;
	}

	if (devicePropertiesThread && devicePropertiesThread->isRunning()) {
		devicePropertiesThread->wait();
		devicePropertiesThread.reset();
	}

	QApplication::sendPostedEvents(nullptr);

	OBSDataAutoRelease modulesObj = obs_data_get_obj(data, "modules");
	if (api)
		api->on_preload(modulesObj);

	if (safe_mode || disable_3p_plugins) {
		/* Keep a reference to "modules" data so plugins that are not
     * loaded do not have their collection specific data lost. */
		safeModeModuleData = obs_data_get_obj(data, "modules");
	}

	OBSDataArrayAutoRelease sceneOrder = obs_data_get_array(data, "scene_order");
	OBSDataArrayAutoRelease sources = obs_data_get_array(data, "sources");
	OBSDataArrayAutoRelease groups = obs_data_get_array(data, "groups");
	OBSDataArrayAutoRelease transitions = obs_data_get_array(data, "transitions");
	const char* sceneName = obs_data_get_string(data, "current_scene");
	const char* programSceneName = obs_data_get_string(data, "current_program_scene");
	const char* transitionName = obs_data_get_string(data, "current_transition");

	if (!opt_starting_scene.empty()) {
		programSceneName = opt_starting_scene.c_str();
		if (!IsPreviewProgramMode())
			sceneName = opt_starting_scene.c_str();
	}

	const char* curSceneCollection =
	  config_get_string(App()->GlobalConfig(), "Basic", "SceneCollection");

	obs_data_set_default_string(data, "name", curSceneCollection);

	const char* name = obs_data_get_string(data, "name");
	OBSSourceAutoRelease curScene;
	OBSSourceAutoRelease curProgramScene;

	/*if (!name || !*name)
		name = curSceneCollection;*/

	LoadAudioDevice(DESKTOP_AUDIO_1, 1, data);
	LoadAudioDevice(DESKTOP_AUDIO_2, 2, data);
	LoadAudioDevice(AUX_AUDIO_1, 3, data);
	LoadAudioDevice(AUX_AUDIO_2, 4, data);
	LoadAudioDevice(AUX_AUDIO_3, 5, data);
	LoadAudioDevice(AUX_AUDIO_4, 6, data);

	if (!sources) {
		sources = std::move(groups);
	} else {
		obs_data_array_push_back_array(sources, groups);
	}

	obs_missing_files_t* files = obs_missing_files_create();
	obs_load_sources(sources, AddMissingFiles, files);

	if (sceneOrder)
		LoadSceneListOrder(sceneOrder);

retryScene:
	curScene = obs_get_source_by_name(sceneName);
	curProgramScene = obs_get_source_by_name(programSceneName);

	/* if the starting scene command line parameter is bad at all,
   * fall back to original settings */
	if (!opt_starting_scene.empty() && (!curScene || !curProgramScene)) {
		sceneName = obs_data_get_string(data, "current_scene");
		programSceneName = obs_data_get_string(data, "current_program_scene");
		opt_starting_scene.clear();
		goto retryScene;
	}

	SetCurrentScene(curScene.Get(), true);

	if (!curProgramScene)
		curProgramScene = std::move(curScene);
	if (IsPreviewProgramMode()) {
		//TransitionToScene(curProgramScene.Get(), true);
	}

	/* ------------------- */

	std::string file_base = strrchr(file, '/') + 1;
	file_base.erase(file_base.size() - 5, 5);

	config_set_string(App()->GlobalConfig(), "Basic", "SceneCollection", name);
	config_set_string(App()->GlobalConfig(), "Basic", "SceneCollectionFile", file_base.c_str());

	bool previewLocked = obs_data_get_bool(data, "preview_locked");
	ui->preview->SetLocked(previewLocked);
	//ui->actionLockPreview->setChecked(previewLocked);

	/* ---------------------- */

	bool fixedScaling = obs_data_get_bool(data, "scaling_enabled");
	int scalingLevel = (int)obs_data_get_int(data, "scaling_level");
	float scrollOffX = (float)obs_data_get_double(data, "scaling_off_x");
	float scrollOffY = (float)obs_data_get_double(data, "scaling_off_y");

	if (fixedScaling) {
		ui->preview->SetScalingLevel(scalingLevel);
		ui->preview->SetScrollingOffset(scrollOffX, scrollOffY);
	}
	ui->preview->SetFixedScaling(fixedScaling);
	emit ui->preview->DisplayResized();

	/* ---------------------- */

	if (api)
		api->on_load(modulesObj);

	obs_data_release(data);

	if (!opt_starting_scene.empty())
		opt_starting_scene.clear();

	if (opt_start_streaming && !safe_mode) {
		blog(LOG_INFO, "Starting stream due to command line parameter");
		QMetaObject::invokeMethod(this, "StartStreaming", Qt::QueuedConnection);
		opt_start_streaming = false;
	}

	if (opt_start_recording && !safe_mode) {
		blog(LOG_INFO, "Starting recording due to command line parameter");
		QMetaObject::invokeMethod(this, "StartRecording", Qt::QueuedConnection);
		opt_start_recording = false;
	}

	if (opt_start_replaybuffer && !safe_mode) {
		QMetaObject::invokeMethod(this, "StartReplayBuffer", Qt::QueuedConnection);
		opt_start_replaybuffer = false;
	}

	if (opt_start_virtualcam && !safe_mode) {
		QMetaObject::invokeMethod(this, "StartVirtualCam", Qt::QueuedConnection);
		opt_start_virtualcam = false;
	}

	LogScenes();

	if (!App()->IsMissingFilesCheckDisabled()) {
		//ShowMissingFilesDialog(files);
	}

	disableSaving--;

	if (api) {
		api->on_event(OBS_FRONTEND_EVENT_SCENE_CHANGED);
		api->on_event(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
	}
}

void MainWindow::ClearSceneData() {
	disableSaving++;

	setCursor(Qt::WaitCursor);

	for (int i = 0; i < MAX_CHANNELS; i++) obs_set_output_source(i, nullptr);

	safeModeModuleData = nullptr;
	lastScene = nullptr;
	swapScene = nullptr;
	programScene = nullptr;
	//prevFTBSource = nullptr;

	auto cb = [](void*, obs_source_t* source) {
		obs_source_remove(source);
		return true;
	};

	obs_enum_scenes(cb, nullptr);
	obs_enum_sources(cb, nullptr);

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP);

	/* using QEvent::DeferredDelete explicitly is the only way to ensure
   * that deleteLater events are processed at this point */
	QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

	do { QApplication::sendPostedEvents(nullptr); } while (obs_wait_for_destroy_queue());

	/* Pump Qt events one final time to give remaining signals time to be
   * processed (since this happens after the destroy thread finishes and
   * the audio/video threads have processed their tasks). */
	QApplication::sendPostedEvents(nullptr);

	unsetCursor();

	/* If scene data wasn't actually cleared, e.g. faulty plugin holding a
   * reference, they will still be in the hash table, enumerate them and
   * store the names for logging purposes. */
	auto cb2 = [](void* param, obs_source_t* source) {
		auto orphans = static_cast<std::vector<std::string>*>(param);
		orphans->push_back(obs_source_get_name(source));
		return true;
	};

	std::vector<std::string> orphan_sources;
	obs_enum_sources(cb2, &orphan_sources);

	if (!orphan_sources.empty()) {
		/* Avoid logging list twice in case it gets called after
     * setting the flag the first time. */
		if (!clearingFailed) {
			/* This ugly mess exists to join a vector of strings
       * with a user-defined delimiter. */
			std::string orphan_names = std::accumulate(
			  orphan_sources.begin(), orphan_sources.end(), std::string(""),
			  [](std::string a, std::string b) { return std::move(a) + "\n- " + b; });

			blog(LOG_ERROR,
			     "Not all sources were cleared when clearing scene data:\n%s\n",
			     orphan_names.c_str());
		}

		/* We do not decrement disableSaving here to avoid OBS
     * overwriting user data with garbage. */
		clearingFailed = true;
	} else {
		disableSaving--;

		blog(LOG_INFO, "All scene data cleared");
		blog(LOG_INFO, "------------------------------------------------");
	}
}

bool MainWindow::nativeEvent(const QByteArray&, void* message, qintptr*) {
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

void MainWindow::changeEvent(QEvent* event) {
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

void MainWindow::EnablePreviewDisplay(bool enable) {
	obs_display_set_enabled(ui->preview->GetDisplay(), enable);
	ui->preview->setVisible(enable);
}

void MainWindow::SetCurrentScene(OBSSource scene, bool force) {
	if (IsPreviewProgramMode()) {
		OBSSource actualLastScene = OBSGetStrongRef(lastScene);
		if (actualLastScene != scene) {
			if (scene)
				obs_source_inc_showing(scene);
			if (actualLastScene)
				obs_source_dec_showing(actualLastScene);
			lastScene = OBSGetWeakRef(scene);
		}
	} else {
		TransitionToScene(scene);
	}

	if (obs_scene_get_source(GetCurrentScene()) != scene) {
		for (int i = 0; i < scenes.size(); i++) {
			QListWidgetItem* item = scenes[i];
			OBSScene itemScene = GetOBSRef<OBSScene>(item);
			obs_source_t* source = obs_scene_get_source(itemScene);

			if (source == scene) {
				currentScene = itemScene.Get();

				if (api)
					api->on_event(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
				break;
			}
		}
	}

	if (scene) {
		bool userSwitched = (!force && !disableSaving);
		blog(LOG_INFO, "%s to scene '%s'", userSwitched ? "User switched" : "Switched",
		     obs_source_get_name(scene));
	}
}

void MainWindow::ResetOutputs() {
	ProfileScope("MainWindow::ResetOutputs");

	const char* mode = config_get_string(basicConfig, "Output", "Mode");
	bool advOut = astrcmpi(mode, "Advanced") == 0;

	if (!outputHandler || !outputHandler->Active()) {
		outputHandler.reset();
		outputHandler.reset(advOut ? CreateAdvancedOutputHandler(this)
					   : CreateSimpleOutputHandler(this));

		if (outputHandler->replayBuffer) {}
	} else {
		outputHandler->Update();
	}
}

bool MainWindow::InitBasicConfigDefaults() {
	QList<QScreen*> screens = QGuiApplication::screens();

	if (!screens.size()) {
		OBSErrorBox(NULL, "There appears to be no monitors.  Er, this "
				  "technically shouldn't be possible.");
		return false;
	}

	QScreen* primaryScreen = QGuiApplication::primaryScreen();

	uint32_t cx = primaryScreen->size().width();
	uint32_t cy = primaryScreen->size().height();

	cx *= devicePixelRatioF();
	cy *= devicePixelRatioF();

	bool oldResolutionDefaults =
	  config_get_bool(App()->GlobalConfig(), "General", "Pre19Defaults");

	/* use 1920x1080 for new default base res if main monitor is above
   * 1920x1080, but don't apply for people from older builds -- only to
   * new users */
	if (!oldResolutionDefaults && (cx * cy) > (1920 * 1080)) {
		cx = 1920;
		cy = 1080;
	}

	bool changed = false;

	/* ----------------------------------------------------- */
	/* move over old FFmpeg track settings                   */
	if (config_has_user_value(basicConfig, "AdvOut", "FFAudioTrack") &&
	    !config_has_user_value(basicConfig, "AdvOut", "Pre22.1Settings")) {

		int track = (int)config_get_int(basicConfig, "AdvOut", "FFAudioTrack");
		config_set_int(basicConfig, "AdvOut", "FFAudioMixes", 1LL << (track - 1));
		config_set_bool(basicConfig, "AdvOut", "Pre22.1Settings", true);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* move over mixer values in advanced if older config */
	if (config_has_user_value(basicConfig, "AdvOut", "RecTrackIndex") &&
	    !config_has_user_value(basicConfig, "AdvOut", "RecTracks")) {

		uint64_t track = config_get_uint(basicConfig, "AdvOut", "RecTrackIndex");
		track = 1ULL << (track - 1);
		config_set_uint(basicConfig, "AdvOut", "RecTracks", track);
		config_remove_value(basicConfig, "AdvOut", "RecTrackIndex");
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* move bitrate enforcement setting to new value         */
	if (config_has_user_value(basicConfig, "SimpleOutput", "EnforceBitrate") &&
	    !config_has_user_value(basicConfig, "Stream1", "IgnoreRecommended") &&
	    !config_has_user_value(basicConfig, "Stream1", "MovedOldEnforce")) {
		bool enforce = config_get_bool(basicConfig, "SimpleOutput", "EnforceBitrate");
		config_set_bool(basicConfig, "Stream1", "IgnoreRecommended", !enforce);
		config_set_bool(basicConfig, "Stream1", "MovedOldEnforce", true);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* enforce minimum retry delay of 1 second prior to 27.1 */
	if (config_has_user_value(basicConfig, "Output", "RetryDelay")) {
		int retryDelay = config_get_uint(basicConfig, "Output", "RetryDelay");
		if (retryDelay < 1) {
			config_set_uint(basicConfig, "Output", "RetryDelay", 1);
			changed = true;
		}
	}

	/* ----------------------------------------------------- */
	/* Migrate old container selection (if any) to new key.  */

	auto MigrateFormat = [&](const char* section) {
		bool has_old_key = config_has_user_value(basicConfig, section, "RecFormat");
		bool has_new_key = config_has_user_value(basicConfig, section, "RecFormat2");
		if (!has_new_key && !has_old_key)
			return;

		std::string old_format =
		  config_get_string(basicConfig, section, has_new_key ? "RecFormat2" : "RecFormat");
		std::string new_format = old_format;
		if (old_format == "ts")
			new_format = "mpegts";
		else if (old_format == "m3u8")
			new_format = "hls";
		else if (old_format == "fmp4")
			new_format = "fragmented_mp4";
		else if (old_format == "fmov")
			new_format = "fragmented_mov";

		if (new_format != old_format || !has_new_key) {
			config_set_string(basicConfig, section, "RecFormat2", new_format.c_str());
			changed = true;
		}
	};

	MigrateFormat("AdvOut");
	MigrateFormat("SimpleOutput");

	/* ----------------------------------------------------- */

	if (changed)
		config_save_safe(basicConfig, "tmp", nullptr);

	/* ----------------------------------------------------- */

	config_set_default_string(basicConfig, "Output", "Mode", "Simple");

	config_set_default_bool(basicConfig, "Stream1", "IgnoreRecommended", false);

	config_set_default_string(basicConfig, "SimpleOutput", "FilePath",
				  GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "SimpleOutput", "RecFormat2", DEFAULT_CONTAINER);
	config_set_default_uint(basicConfig, "SimpleOutput", "VBitrate", 2500);
	config_set_default_uint(basicConfig, "SimpleOutput", "ABitrate", 160);
	config_set_default_bool(basicConfig, "SimpleOutput", "UseAdvanced", false);
	config_set_default_string(basicConfig, "SimpleOutput", "Preset", "veryfast");
	config_set_default_string(basicConfig, "SimpleOutput", "NVENCPreset2", "p5");
	config_set_default_string(basicConfig, "SimpleOutput", "RecQuality", "Stream");
	config_set_default_bool(basicConfig, "SimpleOutput", "RecRB", false);
	config_set_default_int(basicConfig, "SimpleOutput", "RecRBTime", 20);
	config_set_default_int(basicConfig, "SimpleOutput", "RecRBSize", 512);
	config_set_default_string(basicConfig, "SimpleOutput", "RecRBPrefix", "Replay");
	config_set_default_string(basicConfig, "SimpleOutput", "StreamAudioEncoder", "aac");
	config_set_default_string(basicConfig, "SimpleOutput", "RecAudioEncoder", "aac");
	config_set_default_uint(basicConfig, "SimpleOutput", "RecTracks", (1 << 0));

	config_set_default_bool(basicConfig, "AdvOut", "ApplyServiceSettings", true);
	config_set_default_bool(basicConfig, "AdvOut", "UseRescale", false);
	config_set_default_uint(basicConfig, "AdvOut", "TrackIndex", 1);
	config_set_default_uint(basicConfig, "AdvOut", "VodTrackIndex", 2);
	config_set_default_string(basicConfig, "AdvOut", "Encoder", "obs_x264");

	config_set_default_string(basicConfig, "AdvOut", "RecType", "Standard");

	config_set_default_string(basicConfig, "AdvOut", "RecFilePath",
				  GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "RecFormat2", DEFAULT_CONTAINER);
	config_set_default_bool(basicConfig, "AdvOut", "RecUseRescale", false);
	config_set_default_uint(basicConfig, "AdvOut", "RecTracks", (1 << 0));
	config_set_default_string(basicConfig, "AdvOut", "RecEncoder", "none");
	config_set_default_uint(basicConfig, "AdvOut", "FLVTrack", 1);

	config_set_default_bool(basicConfig, "AdvOut", "FFOutputToFile", true);
	config_set_default_string(basicConfig, "AdvOut", "FFFilePath",
				  GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "FFExtension", "mp4");
	config_set_default_uint(basicConfig, "AdvOut", "FFVBitrate", 2500);
	config_set_default_uint(basicConfig, "AdvOut", "FFVGOPSize", 250);
	config_set_default_bool(basicConfig, "AdvOut", "FFUseRescale", false);
	config_set_default_bool(basicConfig, "AdvOut", "FFIgnoreCompat", false);
	config_set_default_uint(basicConfig, "AdvOut", "FFABitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "FFAudioMixes", 1);

	config_set_default_uint(basicConfig, "AdvOut", "Track1Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track2Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track3Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track4Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track5Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track6Bitrate", 160);

	config_set_default_uint(basicConfig, "AdvOut", "RecSplitFileTime", 15);
	config_set_default_uint(basicConfig, "AdvOut", "RecSplitFileSize", 2048);

	config_set_default_bool(basicConfig, "AdvOut", "RecRB", false);
	config_set_default_uint(basicConfig, "AdvOut", "RecRBTime", 20);
	config_set_default_int(basicConfig, "AdvOut", "RecRBSize", 512);

	config_set_default_uint(basicConfig, "Video", "BaseCX", cx);
	config_set_default_uint(basicConfig, "Video", "BaseCY", cy);

	/* don't allow BaseCX/BaseCY to be susceptible to defaults changing */
	if (!config_has_user_value(basicConfig, "Video", "BaseCX") ||
	    !config_has_user_value(basicConfig, "Video", "BaseCY")) {
		config_set_uint(basicConfig, "Video", "BaseCX", cx);
		config_set_uint(basicConfig, "Video", "BaseCY", cy);
		config_save_safe(basicConfig, "tmp", nullptr);
	}

	config_set_default_string(basicConfig, "Output", "FilenameFormatting",
				  "%CCYY-%MM-%DD %hh-%mm-%ss");

	config_set_default_bool(basicConfig, "Output", "DelayEnable", false);
	config_set_default_uint(basicConfig, "Output", "DelaySec", 20);
	config_set_default_bool(basicConfig, "Output", "DelayPreserve", true);

	config_set_default_bool(basicConfig, "Output", "Reconnect", true);
	config_set_default_uint(basicConfig, "Output", "RetryDelay", 2);
	config_set_default_uint(basicConfig, "Output", "MaxRetries", 25);

	config_set_default_string(basicConfig, "Output", "BindIP", "default");
	config_set_default_string(basicConfig, "Output", "IPFamily", "IPv4+IPv6");
	config_set_default_bool(basicConfig, "Output", "NewSocketLoopEnable", false);
	config_set_default_bool(basicConfig, "Output", "LowLatencyEnable", false);

	int i = 0;
	uint32_t scale_cx = cx;
	uint32_t scale_cy = cy;

	/* use a default scaled resolution that has a pixel count no higher
   * than 1280x720 */
	while (((scale_cx * scale_cy) > (1280 * 720)) && scaled_vals[i] > 0.0) {
		double scale = scaled_vals[i++];
		scale_cx = uint32_t(double(cx) / scale);
		scale_cy = uint32_t(double(cy) / scale);
	}

	config_set_default_uint(basicConfig, "Video", "OutputCX", scale_cx);
	config_set_default_uint(basicConfig, "Video", "OutputCY", scale_cy);

	/* don't allow OutputCX/OutputCY to be susceptible to defaults
   * changing */
	if (!config_has_user_value(basicConfig, "Video", "OutputCX") ||
	    !config_has_user_value(basicConfig, "Video", "OutputCY")) {
		config_set_uint(basicConfig, "Video", "OutputCX", scale_cx);
		config_set_uint(basicConfig, "Video", "OutputCY", scale_cy);
		config_save_safe(basicConfig, "tmp", nullptr);
	}

	config_set_default_uint(basicConfig, "Video", "FPSType", 0);
	config_set_default_string(basicConfig, "Video", "FPSCommon", "30");
	config_set_default_uint(basicConfig, "Video", "FPSInt", 30);
	config_set_default_uint(basicConfig, "Video", "FPSNum", 30);
	config_set_default_uint(basicConfig, "Video", "FPSDen", 1);
	config_set_default_string(basicConfig, "Video", "ScaleType", "bicubic");
	config_set_default_string(basicConfig, "Video", "ColorFormat", "NV12");
	config_set_default_string(basicConfig, "Video", "ColorSpace", "709");
	config_set_default_string(basicConfig, "Video", "ColorRange", "Partial");
	config_set_default_uint(basicConfig, "Video", "SdrWhiteLevel", 300);
	config_set_default_uint(basicConfig, "Video", "HdrNominalPeakLevel", 1000);

	config_set_default_string(basicConfig, "Audio", "MonitoringDeviceId", "default");
	config_set_default_string(basicConfig, "Audio", "MonitoringDeviceName",
				  Str("Basic.Settings.Advanced.Audio.MonitoringDevice"
				      ".Default"));
	config_set_default_uint(basicConfig, "Audio", "SampleRate", 48000);
	config_set_default_string(basicConfig, "Audio", "ChannelSetup", "Stereo");
	config_set_default_double(basicConfig, "Audio", "MeterDecayRate", 23.53);
	config_set_default_uint(basicConfig, "Audio", "PeakMeterType", 0);

	config_set_bool(App()->GlobalConfig(), "BasicWindow", "HideOBSWindowsFromCapture", true);

	return true;
}

void MainWindow::InitBasicConfigDefaults2() {
	bool oldEncDefaults = config_get_bool(App()->GlobalConfig(), "General", "Pre23Defaults");
	bool useNV = EncoderAvailable("ffmpeg_nvenc") && !oldEncDefaults;

	config_set_default_string(basicConfig, "SimpleOutput", "StreamEncoder",
				  useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);
	config_set_default_string(basicConfig, "SimpleOutput", "RecEncoder",
				  useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);

	const char* aac_default = "ffmpeg_aac";
	if (EncoderAvailable("CoreAudio_AAC"))
		aac_default = "CoreAudio_AAC";
	else if (EncoderAvailable("libfdk_aac"))
		aac_default = "libfdk_aac";

	config_set_default_string(basicConfig, "AdvOut", "AudioEncoder", aac_default);
	config_set_default_string(basicConfig, "AdvOut", "RecAudioEncoder", aac_default);

	if (update_nvenc_presets(basicConfig))
		config_save_safe(basicConfig, "tmp", nullptr);
}

bool MainWindow::InitBasicConfig() {
	ProfileScope("MainWindow::InitBasicConfig");

	char configPath[512];

	int ret = GetProfilePath(configPath, sizeof(configPath), "");
	if (ret <= 0) {
		OBSErrorBox(nullptr, "Failed to get profile path");
		return false;
	}

	if (os_mkdir(configPath) == MKDIR_ERROR) {
		OBSErrorBox(nullptr, "Failed to create profile path");
		return false;
	}

	ret = GetProfilePath(configPath, sizeof(configPath), "basic.ini");
	if (ret <= 0) {
		OBSErrorBox(nullptr, "Failed to get basic.ini path");
		return false;
	}

	int code = basicConfig.Open(configPath, CONFIG_OPEN_ALWAYS);
	if (code != CONFIG_SUCCESS) {
		OBSErrorBox(NULL, "Failed to open basic.ini: %d", code);
		return false;
	}

	if (config_get_string(basicConfig, "General", "Name") == nullptr) {
		const char* curName = config_get_string(App()->GlobalConfig(), "Basic", "Profile");

		config_set_string(basicConfig, "General", "Name", curName);
		basicConfig.SaveSafe("tmp");
	}

	return InitBasicConfigDefaults();
}

void MainWindow::InitOBSCallbacks() {
	ProfileScope("MainWindow::InitOBSCallbacks");

	signalHandlers.reserve(signalHandlers.size() + 7);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_create",
				    MainWindow::SourceCreated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_remove",
				    MainWindow::SourceRemoved, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_activate",
				    MainWindow::SourceActivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_deactivate",
				    MainWindow::SourceDeactivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_audio_activate",
				    MainWindow::SourceAudioActivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_audio_deactivate",
				    MainWindow::SourceAudioDeactivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_rename",
				    MainWindow::SourceRenamed, this);
}

float MainWindow::GetDevicePixelRatio() {
	return dpi;
}

void MainWindow::InitPrimitives() {
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

/* OBS Callbacks */

void MainWindow::SceneReordered(void* data, calldata_t* params) {
	MainWindow* window = static_cast<MainWindow*>(data);

	obs_scene_t* scene = (obs_scene_t*)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "ReorderSources", Q_ARG(OBSScene, OBSScene(scene)));
}

void MainWindow::SceneRefreshed(void* data, calldata_t* params) {
	MainWindow* window = static_cast<MainWindow*>(data);

	obs_scene_t* scene = (obs_scene_t*)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "RefreshSources", Q_ARG(OBSScene, OBSScene(scene)));
}

void MainWindow::SceneItemAdded(void* data, calldata_t* params) {
	MainWindow* window = static_cast<MainWindow*>(data);

	obs_sceneitem_t* item = (obs_sceneitem_t*)calldata_ptr(params, "item");

	QMetaObject::invokeMethod(window, "AddSceneItem", Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void MainWindow::SourceCreated(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");

	if (obs_scene_from_source(source) != NULL)
		QMetaObject::invokeMethod(static_cast<MainWindow*>(data), "AddScene",
					  WaitConnection(), Q_ARG(OBSSource, OBSSource(source)));
}

void MainWindow::SourceRemoved(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");

	if (obs_scene_from_source(source) != NULL)
		QMetaObject::invokeMethod(static_cast<MainWindow*>(data), "RemoveScene",
					  Q_ARG(OBSSource, OBSSource(source)));
}

void MainWindow::SourceActivated(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_AUDIO)
		QMetaObject::invokeMethod(static_cast<MainWindow*>(data), "ActivateAudioSource",
					  Q_ARG(OBSSource, OBSSource(source)));
}

void MainWindow::SourceDeactivated(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_AUDIO)
		QMetaObject::invokeMethod(static_cast<MainWindow*>(data), "DeactivateAudioSource",
					  Q_ARG(OBSSource, OBSSource(source)));
}

void MainWindow::SourceAudioActivated(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");

	if (obs_source_active(source))
		QMetaObject::invokeMethod(static_cast<MainWindow*>(data), "ActivateAudioSource",
					  Q_ARG(OBSSource, OBSSource(source)));
}

void MainWindow::SourceAudioDeactivated(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");
	QMetaObject::invokeMethod(static_cast<MainWindow*>(data), "DeactivateAudioSource",
				  Q_ARG(OBSSource, OBSSource(source)));
}

void MainWindow::SourceRenamed(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");
	const char* newName = calldata_string(params, "new_name");
	const char* prevName = calldata_string(params, "prev_name");

	QMetaObject::invokeMethod(static_cast<MainWindow*>(data), "RenameSources",
				  Q_ARG(OBSSource, source), Q_ARG(QString, QT_UTF8(newName)),
				  Q_ARG(QString, QT_UTF8(prevName)));

	blog(LOG_INFO, "Source '%s' renamed to '%s'", prevName, newName);
}

void MainWindow::DrawBackdrop(float cx, float cy) {
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

void MainWindow::RenderMain(void* data, uint32_t, uint32_t) {
	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "RenderMain");

	MainWindow* window = static_cast<MainWindow*>(data);
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

	if (window->IsPreviewProgramMode()) {
		window->DrawBackdrop(float(ovi.base_width), float(ovi.base_height));

		OBSScene scene = window->GetCurrentScene();
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

void MainWindow::UpdatePreviewSpacingHelpers() {
	drawSpacingHelpers =
	  config_get_bool(App()->GlobalConfig(), "BasicWindow", "SpacingHelpersEnabled");
}

void MainWindow::UpdatePreviewSafeAreas() {
	drawSafeAreas = config_get_bool(App()->GlobalConfig(), "BasicWindow", "ShowSafeAreas");
}

void MainWindow::LoadSceneListOrder(obs_data_array_t* array) {
	size_t num = obs_data_array_count(array);

	for (size_t i = 0; i < num; i++) {
		/*OBSDataAutoRelease data = obs_data_array_item(array, i);
		const char* name = obs_data_get_string(data, "name");

		ReorderItemByName(ui->scenes, name, (int)i);*/
	}
}

void MainWindow::UpdatePreviewOverflowSettings() {
	bool hidden = config_get_bool(App()->GlobalConfig(), "BasicWindow", "OverflowHidden");
	bool select =
	  config_get_bool(App()->GlobalConfig(), "BasicWindow", "OverflowSelectionHidden");
	bool always =
	  config_get_bool(App()->GlobalConfig(), "BasicWindow", "OverflowAlwaysVisible");

	ui->preview->SetOverflowHidden(hidden);
	ui->preview->SetOverflowSelectionHidden(select);
	ui->preview->SetOverflowAlwaysVisible(always);
}

obs_data_array_t* MainWindow::SaveSceneListOrder() {
	obs_data_array_t* sceneOrder = obs_data_array_create();
	return sceneOrder;
}

void MainWindow::CreateFirstRunSources() {
	bool hasDesktopAudio = HasAudioDevices(App()->OutputAudioSource());
	bool hasInputAudio = HasAudioDevices(App()->InputAudioSource());

	if (hasDesktopAudio)
		ResetAudioDevice(App()->OutputAudioSource(), "default", Str("Basic.DesktopDevice1"),
				 1);
	if (hasInputAudio)
		ResetAudioDevice(App()->InputAudioSource(), "default", Str("Basic.AuxDevice1"), 3);
}

void MainWindow::CreateDefaultScene(bool firstStart) {
	disableSaving++;

	ClearSceneData();
	InitDefaultTransitions();

	SetTransition(fadeTransition);

	OBSSceneAutoRelease scene = obs_scene_create(Str("Basic.Scene"));

	if (firstStart)
		CreateFirstRunSources();

	SetCurrentScene(scene, true);

	disableSaving--;
}

void MainWindow::SetCurrentScene(obs_scene_t* scene, bool force) {
	obs_source_t* source = obs_scene_get_source(scene);
	SetCurrentScene(source, force);
}

void MainWindow::ResetAudioDevice(const char* sourceId, const char* deviceId,
				  const char* deviceDesc, int channel) {
	bool disable = deviceId && strcmp(deviceId, "disabled") == 0;
	OBSSourceAutoRelease source;
	OBSDataAutoRelease settings;

	source = obs_get_output_source(channel);
	if (source) {
		if (disable) {
			obs_set_output_source(channel, nullptr);
		} else {
			settings = obs_source_get_settings(source);
			const char* oldId = obs_data_get_string(settings, "device_id");
			if (strcmp(oldId, deviceId) != 0) {
				obs_data_set_string(settings, "device_id", deviceId);
				obs_source_update(source, settings);
			}
		}

	} else if (!disable) {
		BPtr<char> name = get_new_source_name(deviceDesc, "%s (%d)");

		settings = obs_data_create();
		obs_data_set_string(settings, "device_id", deviceId);
		source = obs_source_create(sourceId, name, settings, nullptr);

		obs_set_output_source(channel, source);
	}
}

void MainWindow::ResizePreview(uint32_t cx, uint32_t cy) {
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
	}

	previewX += float(PREVIEW_EDGE_SIZE);
	previewY += float(PREVIEW_EDGE_SIZE);
}

static inline QColor color_from_int(long long val) {
	return QColor(val & 0xff, (val >> 8) & 0xff, (val >> 16) & 0xff, (val >> 24) & 0xff);
}

QColor MainWindow::GetSelectionColor() const {
	if (config_get_bool(GetGlobalConfig(), "Accessibility", "OverrideColors")) {
		return color_from_int(
		  config_get_int(GetGlobalConfig(), "Accessibility", "SelectRed"));
	} else {
		return QColor::fromRgb(255, 0, 0);
	}
}

QColor MainWindow::GetCropColor() const {
	if (config_get_bool(GetGlobalConfig(), "Accessibility", "OverrideColors")) {
		return color_from_int(
		  config_get_int(GetGlobalConfig(), "Accessibility", "SelectGreen"));
	} else {
		return QColor::fromRgb(0, 255, 0);
	}
}

QColor MainWindow::GetHoverColor() const {
	if (config_get_bool(GetGlobalConfig(), "Accessibility", "OverrideColors")) {
		return color_from_int(
		  config_get_int(GetGlobalConfig(), "Accessibility", "SelectBlue"));
	} else {
		return QColor::fromRgb(0, 127, 255);
	}
}

void MainWindow::SetDisplayAffinity(HWND hwnd) {
	if (!SetDisplayAffinitySupported())
		return;

	bool hideFromCapture =
	  config_get_bool(App()->GlobalConfig(), "BasicWindow", "HideOBSWindowsFromCapture");

#ifdef _WIN32
	DWORD curAffinity;
	if (GetWindowDisplayAffinity(hwnd, &curAffinity)) {
		if (hideFromCapture && curAffinity != WDA_EXCLUDEFROMCAPTURE)
			SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
		else if (!hideFromCapture && curAffinity != WDA_NONE)
			SetWindowDisplayAffinity(hwnd, WDA_NONE);
	}

#else
	// TODO: Implement for other platforms if possible. Don't forget to
	// implement SetDisplayAffinitySupported too!
	UNUSED_PARAMETER(hideFromCapture);
#endif
}

void MainWindow::AddScene(OBSSource source) {
	const char* name = obs_source_get_name(source);
	obs_scene_t* scene = obs_scene_from_source(source);

	QListWidgetItem* item = new QListWidgetItem(QT_UTF8(name));
	SetOBSRef(item, OBSScene(scene));
	scenes.push_back(item);

	signal_handler_t* handler = obs_source_get_signal_handler(source);

	SignalContainer<OBSScene> container;
	container.ref = scene;
	container.handlers.assign({
	  std::make_shared<OBSSignal>(handler, "item_add", MainWindow::SceneItemAdded, this),
	});

	item->setData(static_cast<int>(QtDataRole::OBSSignals), QVariant::fromValue(container));

	/* if the scene already has items (a duplicated scene) add them */
	auto addSceneItem = [this](obs_sceneitem_t* item) {
		AddSceneItem(item);
	};

	using addSceneItem_t = decltype(addSceneItem);

	obs_scene_enum_items(
	  scene,
	  [](obs_scene_t*, obs_sceneitem_t* item, void* param) {
		  addSceneItem_t* func;
		  func = reinterpret_cast<addSceneItem_t*>(param);
		  (*func)(item);
		  return true;
	  },
	  &addSceneItem);

	SaveProject();

	if (!disableSaving) {
		obs_source_t* source = obs_scene_get_source(scene);
		blog(LOG_INFO, "User added scene '%s'", obs_source_get_name(source));
	}

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
}

void MainWindow::RemoveScene(OBSSource source) {
	obs_scene_t* scene = obs_scene_from_source(source);

	QListWidgetItem* sel = nullptr;
	auto count = (int)scenes.size();

	for (int i = 0; i < count; i++) {
		auto item = scenes[i];
		auto cur_scene = GetOBSRef<OBSScene>(item);
		if (cur_scene != scene)
			continue;

		sel = item;
		break;
	}

	if (sel != nullptr) {
		delete sel;
	}

	SaveProject();

	if (!disableSaving) {
		blog(LOG_INFO, "User Removed scene '%s'", obs_source_get_name(source));
	}

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
}

static bool select_one(obs_scene_t* /* scene */, obs_sceneitem_t* item, void* param) {
	obs_sceneitem_t* selectedItem = reinterpret_cast<obs_sceneitem_t*>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, select_one, param);

	obs_sceneitem_select(item, (selectedItem == item));

	return true;
}

void MainWindow::AddSceneItem(OBSSceneItem item) {
	obs_scene_t* scene = obs_sceneitem_get_scene(item);

	if (GetCurrentScene() == scene)
		sources.push_back(item);

	auto source = obs_sceneitem_get_source(item);
	obs_source_active(source);
	obs_source_showing(source);
	auto name = obs_source_get_name(source);
	blog(LOG_INFO, "add scene item: %s", name);

	SaveProject();

	if (!disableSaving) {
		obs_source_t* sceneSource = obs_scene_get_source(scene);
		obs_source_t* itemSource = obs_sceneitem_get_source(item);
		blog(LOG_INFO, "User added source '%s' (%s) to scene '%s'",
		     obs_source_get_name(itemSource), obs_source_get_id(itemSource),
		     obs_source_get_name(sceneSource));

		obs_scene_enum_items(scene, select_one, (obs_sceneitem_t*)item);
	}
}

void MainWindow::DuplicateSelectedScene() {
	OBSScene curScene = GetCurrentScene();

	if (!curScene)
		return;

	OBSSource curSceneSource = obs_scene_get_source(curScene);
	QString format{obs_source_get_name(curSceneSource)};
	format += " %1";

	int i = 2;
	QString placeHolderText = format.arg(i);
	OBSSourceAutoRelease source = nullptr;
	while ((source = obs_get_source_by_name(QT_TO_UTF8(placeHolderText)))) {
		placeHolderText = format.arg(++i);
	}

	for (;;) {
		std::string name(format.toStdString());

		if (name.empty()) {
			OBSMessageBox::warning(this, QTStr("NoNameEntered.Title"),
					       QTStr("NoNameEntered.Text"));
			continue;
		}

		obs_source_t* source = obs_get_source_by_name(name.c_str());
		if (source) {
			OBSMessageBox::warning(this, QTStr("NameExists.Title"),
					       QTStr("NameExists.Text"));

			obs_source_release(source);
			continue;
		}

		break;
	}
}

void MainWindow::on_scenes_currentItemChanged(QListWidgetItem* current, QListWidgetItem*) {
	OBSSource source;

	if (current) {
		OBSScene scene = GetOBSRef<OBSScene>(current);
		source = obs_scene_get_source(scene);

		currentScene = scene;
	} else {
		currentScene = NULL;
	}

	SetCurrentScene(source);

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
}

void MainWindow::InitDefaultTransitions() {
	std::vector<OBSSource> transitions;
	size_t idx = 0;
	const char* id;

	/* automatically add transitions that have no configuration (things
   * such as cut/fade/etc) */
	while (obs_enum_transition_types(idx++, &id)) {
		if (!obs_is_source_configurable(id)) {
			const char* name = obs_source_get_display_name(id);

			OBSSourceAutoRelease tr = obs_source_create_private(id, name, NULL);
			InitTransition(tr);
			transitions.emplace_back(tr);

			if (strcmp(id, "fade_transition") == 0)
				fadeTransition = tr;
			else if (strcmp(id, "cut_transition") == 0)
				cutTransition = tr;
		}
	}

	for (OBSSource& tr : transitions) { SetTransition(tr); }
}

void MainWindow::InitTransition(obs_source_t* transition) {
	auto onTransitionStop = [](void* data, calldata_t*) {
		MainWindow* window = (MainWindow*)data;
		QMetaObject::invokeMethod(window, "TransitionStopped", Qt::QueuedConnection);
	};

	auto onTransitionFullStop = [](void* data, calldata_t*) {
		MainWindow* window = (MainWindow*)data;
		QMetaObject::invokeMethod(window, "TransitionFullyStopped", Qt::QueuedConnection);
	};

	signal_handler_t* handler = obs_source_get_signal_handler(transition);
	signal_handler_connect(handler, "transition_video_stop", onTransitionStop, this);
	signal_handler_connect(handler, "transition_stop", onTransitionFullStop, this);
}

void MainWindow::SetTransition(OBSSource transition) {
	OBSSourceAutoRelease oldTransition = obs_get_output_source(0);

	if (oldTransition && transition) {
		obs_transition_swap_begin(transition, oldTransition);
		obs_set_output_source(0, transition);
		obs_transition_swap_end(transition, oldTransition);
	} else {
		obs_set_output_source(0, transition);
	}

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_TRANSITION_CHANGED);
}

void MainWindow::TransitionToScene(OBSSource source) {
	obs_scene_t* scene = obs_scene_from_source(source);
	bool usingPreviewProgram = IsPreviewProgramMode();
	if (!scene)
		return;

	OBSSourceAutoRelease transition = obs_get_output_source(0);
	if (!transition) {
		return;
	}

	obs_transition_set(transition, source);
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_SCENE_CHANGED);
}

void MainWindow::StartRecording() {
	if (outputHandler->RecordingActive())
		return;
	if (disableOutputsRef)
		return;

	if (!OutputPathValid()) {
		blog(LOG_ERROR, "Recording stopped because of bad output path");

		ui->startRecordingButton->setEnabled(true);
		ui->stopRecordingButton->setEnabled(false);
		return;
	}

	/*if (LowDiskSpace()) {
		DiskSpaceMessage();
		ui->recordButton->setChecked(false);
		return;
	}*/

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_RECORDING_STARTING);

	SaveProject();

	if (!outputHandler->StartRecording()) {
		ui->startRecordingButton->setEnabled(true);
		ui->stopRecordingButton->setEnabled(false);
	}
}

void MainWindow::RecordStopping() {
	recordingStopping = true;
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_RECORDING_STOPPING);
}

void MainWindow::StopRecording() {
	SaveProject();

	if (outputHandler->RecordingActive())
		outputHandler->StopRecording(recordingStopping);
}

void MainWindow::RecordingStart() {
	ui->stopRecordingButton->setEnabled(true);
	ui->startRecordingButton->setEnabled(false);

	recordingStopping = false;
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_RECORDING_STARTED);

	/*if (!diskFullTimer->isActive())
		diskFullTimer->start(1000);*/

	blog(LOG_INFO, RECORDING_START);
}

void MainWindow::RecordingStop(int code, QString last_error) {
	ui->stopRecordingButton->setEnabled(false);
	ui->startRecordingButton->setEnabled(true);

	blog(LOG_INFO, RECORDING_STOP);

	if (code == OBS_OUTPUT_UNSUPPORTED && isVisible()) {
		blog(LOG_ERROR, "unsupported");
	} else if (code == OBS_OUTPUT_ENCODE_ERROR && isVisible()) {
		blog(LOG_ERROR, "encode error");
	} else if (code == OBS_OUTPUT_NO_SPACE && isVisible()) {
		blog(LOG_ERROR, "disk is full");
	} else if (code != OBS_OUTPUT_SUCCESS && isVisible()) {

		const char* errorDescription;
		DStr errorMessage;
		bool use_last_error = true;

		errorDescription = Str("Output.RecordError.Msg");

		if (use_last_error && !last_error.isEmpty())
			dstr_printf(errorMessage, "%s\n\n%s", errorDescription,
				    QT_TO_UTF8(last_error));
		else
			dstr_copy(errorMessage, errorDescription);

		blog(LOG_ERROR, errorMessage);

	} else if (code == OBS_OUTPUT_UNSUPPORTED && !isVisible()) {
		blog(LOG_ERROR, "unsupported");
	} else if (code == OBS_OUTPUT_NO_SPACE && !isVisible()) {
		blog(LOG_ERROR, "disk is full");
	} else if (code != OBS_OUTPUT_SUCCESS && !isVisible()) {

	} else if (code == OBS_OUTPUT_SUCCESS) {
		if (outputHandler) {
			std::string path = outputHandler->lastRecordingPath;
			blog(LOG_INFO, "save to disk at: %s", path.c_str());
		}
	}

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_RECORDING_STOPPED);

	/*if (diskFullTimer->isActive())
		diskFullTimer->stop();*/

	AutoRemux(outputHandler->lastRecordingPath.c_str());

	UpdatePause(false);
}

void MainWindow::RecordingFileChanged(QString lastRecordingPath) {
	QString str = QTStr("Basic.StatusBar.RecordingSavedTo");
	blog(LOG_ERROR, "save file path changed");

	AutoRemux(lastRecordingPath, true);
}

bool MainWindow::OutputPathValid() {
	const char* mode = config_get_string(Config(), "Output", "Mode");
	if (strcmp(mode, "Advanced") == 0) {
		const char* advanced_mode = config_get_string(Config(), "AdvOut", "RecType");
		if (strcmp(advanced_mode, "FFmpeg") == 0) {
			bool is_local = config_get_bool(Config(), "AdvOut", "FFOutputToFile");
			if (!is_local)
				return true;
		}
	}

	const char* path = GetCurrentOutputPath();
	return path && *path && QDir(path).exists();
}

void MainWindow::AutoRemux(QString input, bool no_show) {
	auto config = Config();

	bool autoRemux = config_get_bool(config, "Video", "AutoRemux");

	if (!autoRemux)
		return;

	bool isSimpleMode = false;

	const char* mode = config_get_string(config, "Output", "Mode");
	if (!mode) {
		isSimpleMode = true;
	} else {
		isSimpleMode = strcmp(mode, "Simple") == 0;
	}

	if (!isSimpleMode) {
		const char* recType = config_get_string(config, "AdvOut", "RecType");

		bool ffmpegOutput = astrcmpi(recType, "FFmpeg") == 0;

		if (ffmpegOutput)
			return;
	}

	if (input.isEmpty())
		return;

	QFileInfo fi(input);
	QString suffix = fi.suffix();

	/* do not remux if lossless */
	if (suffix.compare("avi", Qt::CaseInsensitive) == 0) {
		return;
	}

	QString path = fi.path();

	QString output = input;
	output.resize(output.size() - suffix.size());

	const obs_encoder_t* videoEncoder = obs_output_get_video_encoder(outputHandler->fileOutput);
	const obs_encoder_t* audioEncoder =
	  obs_output_get_audio_encoder(outputHandler->fileOutput, 0);
	const char* vCodecName = obs_encoder_get_codec(videoEncoder);
	const char* aCodecName = obs_encoder_get_codec(audioEncoder);
	const char* format =
	  config_get_string(config, isSimpleMode ? "SimpleOutput" : "AdvOut", "RecFormat2");

	bool audio_is_pcm = strncmp(aCodecName, "pcm", 3) == 0;

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(60, 5, 100)
	/* FFmpeg <= 6.0 cannot remux AV1+PCM into any supported format. */
	if (audio_is_pcm && strcmp(vCodecName, "av1") == 0)
		return;
#endif

	/* Retain original container for fMP4/fMOV */
	if (strncmp(format, "fragmented", 10) == 0) {
		output += "remuxed." + suffix;
	} else if (strcmp(vCodecName, "prores") == 0) {
		output += "mov";
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(60, 5, 100)
	} else if (audio_is_pcm) {
		output += "mov";
#endif
	} else {
		output += "mp4";
	}

	/*OBSRemux* remux = new OBSRemux(QT_TO_UTF8(path), this, true);
	if (!no_show)
		remux->show();
	remux->AutoRemux(input, output);*/
}

void MainWindow::UpdatePause(bool activate) {
	if (!activate || !outputHandler || !outputHandler->RecordingActive()) {
		return;
	}

	const char* mode = config_get_string(basicConfig, "Output", "Mode");
	bool adv = astrcmpi(mode, "Advanced") == 0;
	bool shared;

	if (adv) {
		const char* recType = config_get_string(basicConfig, "AdvOut", "RecType");

		if (astrcmpi(recType, "FFmpeg") == 0) {
			shared = config_get_bool(basicConfig, "AdvOut", "FFOutputToFile");
		} else {
			const char* recordEncoder =
			  config_get_string(basicConfig, "AdvOut", "RecEncoder");
			shared = astrcmpi(recordEncoder, "none") == 0;
		}
	} else {
		const char* quality = config_get_string(basicConfig, "SimpleOutput", "RecQuality");
		shared = strcmp(quality, "Stream") == 0;
	}
}

void MainWindow::StartStreaming() {
	if (outputHandler->StreamingActive())
		return;
	if (disableOutputsRef)
		return;

	if (!outputHandler->SetupStreaming(service)) {
		blog(LOG_ERROR, "start streaming failed");
		return;
	}

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_STREAMING_STARTING);

	SaveProject();

	ui->startStreamingButton->setEnabled(false);
	ui->stopStreamingButton->setEnabled(true);

	if (!outputHandler->StartStreaming(service)) {
		blog(LOG_ERROR, "start streaming failed");
		return;
	}

	bool recordWhenStreaming =
	  config_get_bool(GetGlobalConfig(), "BasicWindow", "RecordWhenStreaming");
	if (recordWhenStreaming)
		StartRecording();
}

void MainWindow::StopStreaming() {
	SaveProject();

	if (outputHandler->StreamingActive())
		outputHandler->StopStreaming(streamingStopping);

	bool recordWhenStreaming =
	  config_get_bool(GetGlobalConfig(), "BasicWindow", "RecordWhenStreaming");
	bool keepRecordingWhenStreamStops =
	  config_get_bool(GetGlobalConfig(), "BasicWindow", "KeepRecordingWhenStreamStops");
	if (recordWhenStreaming && !keepRecordingWhenStreamStops)
		StopRecording();
}

void MainWindow::ForceStopStreaming() {
	SaveProject();

	if (outputHandler->StreamingActive())
		outputHandler->StopStreaming(true);

	bool recordWhenStreaming =
	  config_get_bool(GetGlobalConfig(), "BasicWindow", "RecordWhenStreaming");
	bool keepRecordingWhenStreamStops =
	  config_get_bool(GetGlobalConfig(), "BasicWindow", "KeepRecordingWhenStreamStops");
	if (recordWhenStreaming && !keepRecordingWhenStreamStops)
		StopRecording();
}

void MainWindow::StreamingStart() {
	ui->startStreamingButton->setEnabled(false);
	ui->stopStreamingButton->setEnabled(true);

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_STREAMING_STARTED);

	blog(LOG_INFO, STREAMING_START);
}

void MainWindow::StreamStopping() {
	streamingStopping = true;
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_STREAMING_STOPPING);
}

void MainWindow::StreamingStop(int code, QString last_error) {
	const char* errorDescription = "";
	DStr errorMessage;
	bool use_last_error = false;
	bool encode_error = false;

	switch (code) {
	case OBS_OUTPUT_BAD_PATH: errorDescription = Str("Output.ConnectFail.BadPath"); break;

	case OBS_OUTPUT_CONNECT_FAILED:
		use_last_error = true;
		errorDescription = Str("Output.ConnectFail.ConnectFailed");
		break;

	case OBS_OUTPUT_INVALID_STREAM:
		errorDescription = Str("Output.ConnectFail.InvalidStream");
		break;

	case OBS_OUTPUT_ENCODE_ERROR: encode_error = true; break;

	case OBS_OUTPUT_HDR_DISABLED:
		errorDescription = Str("Output.ConnectFail.HdrDisabled");
		break;

	default:
	case OBS_OUTPUT_ERROR:
		use_last_error = true;
		errorDescription = Str("Output.ConnectFail.Error");
		break;

	case OBS_OUTPUT_DISCONNECTED:
		/* doesn't happen if output is set to reconnect.  note that
     * reconnects are handled in the output, not in the UI */
		use_last_error = true;
		errorDescription = Str("Output.ConnectFail.Disconnected");
	}

	if (use_last_error && !last_error.isEmpty())
		dstr_printf(errorMessage, "%s\n\n%s", errorDescription, QT_TO_UTF8(last_error));
	else
		dstr_copy(errorMessage, errorDescription);

	ui->startStreamingButton->setEnabled(true);
	ui->stopStreamingButton->setEnabled(false);

	streamingStopping = false;
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_STREAMING_STOPPED);

	blog(LOG_INFO, STREAMING_STOP);

	if (encode_error) {
		blog(LOG_ERROR, "streaming error, %s", last_error.toStdString().c_str());
	}
}
