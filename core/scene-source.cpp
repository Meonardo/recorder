#include "scene-source.h"

#include "app.h"

#include <util/profiler.hpp>

static bool select_one(obs_scene_t* /* scene */, obs_sceneitem_t* item, void* param) {
	obs_sceneitem_t* selectedItem = reinterpret_cast<obs_sceneitem_t*>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, select_one, param);

	obs_sceneitem_select(item, (selectedItem == item));

	return true;
}

static void split_string(const std::string& source, std::string&& token,
			 std::vector<std::string>& result) {
	size_t start = 0;
	size_t end = source.find(token);
	while (end != std::string::npos) {
		result.emplace_back(source.substr(start, end - start));
		start = end + token.length();
		end = source.find(token, start);
	}

	// append the last part
	result.emplace_back(source.substr(start, source.length() - 1));
}

static bool replace(std::string& source, const std::string& from, const std::string& to) {
	size_t start_pos = source.find(from);
	if (start_pos == std::string::npos)
		return false;
	source.replace(start_pos, from.length(), to);
	return true;
}

template<typename T>
static void get_source_setting_value(obs_data_t* d, const char* settingName, T& result) {
	obs_data_item_t* item = nullptr;

	for (item = obs_data_first(d); item; obs_data_item_next(&item)) {
		enum obs_data_type type = obs_data_item_gettype(item);
		const char* name = obs_data_item_get_name(item);
		if (strcmp(name, settingName) == 0) {
			if (!obs_data_item_has_user_value(item))
				continue;
			if (type == OBS_DATA_STRING) {
				result = std::string(obs_data_item_get_string(item));
			} else if (type == OBS_DATA_NUMBER) {
				enum obs_data_number_type type = obs_data_item_numtype(item);
				if (type == OBS_DATA_NUM_INT) {
					result = (int)obs_data_item_get_int(item);
				} else if (type == OBS_DATA_NUM_DOUBLE) {
					result = (int)obs_data_item_get_double(item);
				}
			} else if (type == OBS_DATA_BOOLEAN) {
				result = obs_data_item_get_bool(item);
			}
		}
	}
}

namespace core {
void Scene::AddSignalHandler(signal_handler_t* handler) {
	container.ref = data;
	container.handlers.assign({
	  std::make_shared<OBSSignal>(handler, "item_add", SceneSourceManager::SceneItemAdded,
				      this),
	});
}

SceneSourceManager::SceneSourceManager() {
	ProfileScope("MainWindow::InitOBSCallbacks");

	signalHandlers.reserve(signalHandlers.size() + 7);

	signalHandlers.emplace_back(obs_get_signal_handler(), "source_create",
				    SceneSourceManager::SourceCreated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_remove",
				    SceneSourceManager::SourceRemoved, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_activate",
				    SceneSourceManager::SourceActivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_deactivate",
				    SceneSourceManager::SourceDeactivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_audio_activate",
				    SceneSourceManager::SourceAudioActivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_audio_deactivate",
				    SceneSourceManager::SourceAudioDeactivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_rename",
				    SceneSourceManager::SourceRenamed, this);
}
SceneSourceManager::~SceneSourceManager() {
  for (auto& item : signalHandlers) {
    item.Disconnect();
  }

  for (auto scene : scenes) {
    delete scene;
  }
  scenes.clear();

  sources.clear();
}

void SceneSourceManager::SourceCreated(void* data, calldata_t* params) {
	auto manager = reinterpret_cast<SceneSourceManager*>(data);
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");

	if (obs_scene_from_source(source) != nullptr) {
		// add scene
		manager->AddScene(OBSSource(source));
	}
}

void SceneSourceManager::SceneReordered(void* data, calldata_t* params) {}

void SceneSourceManager::SceneRefreshed(void* data, calldata_t* params) {}

void SceneSourceManager::SceneItemAdded(void* data, calldata_t* params) {
	SceneSourceManager* manager = static_cast<SceneSourceManager*>(data);
	obs_sceneitem_t* item = (obs_sceneitem_t*)calldata_ptr(params, "item");
	manager->AddSceneItem(OBSSceneItem(item));
}

void SceneSourceManager::SourceRemoved(void* data, calldata_t* params) {
	SceneSourceManager* manager = static_cast<SceneSourceManager*>(data);
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");

	manager->RemoveScene(OBSSource(source));
}

void SceneSourceManager::SourceActivated(void* data, calldata_t* params) {}
void SceneSourceManager::SourceDeactivated(void* data, calldata_t* params) {}
void SceneSourceManager::SourceAudioDeactivated(void* data, calldata_t* params) {}
void SceneSourceManager::SourceAudioActivated(void* data, calldata_t* params) {}
void SceneSourceManager::SourceRenamed(void* data, calldata_t* params) {
	obs_source_t* source = (obs_source_t*)calldata_ptr(params, "source");
	const char* newName = calldata_string(params, "new_name");
	const char* prevName = calldata_string(params, "prev_name");

	blog(LOG_INFO, "Source '%s' renamed to '%s'", prevName, newName);
}

void SceneSourceManager::AddScene(OBSSource source) {
	const char* name = obs_source_get_name(source);
	obs_scene_t* scene = obs_scene_from_source(source);

	Scene* scene_ = new Scene(scene, name);
	scenes.push_back(scene_);

	signal_handler_t* handler = obs_source_get_signal_handler(source);
	scene_->AddSignalHandler(handler);

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

	CoreApp->SaveProject();

	if (!CoreApp->disableSaving) {
		obs_source_t* source = obs_scene_get_source(scene);
		blog(LOG_INFO, "User added scene '%s'", obs_source_get_name(source));
	}

	if (CoreApp->api) {
		CoreApp->api->on_event(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
	}
}

void SceneSourceManager::AddSceneItem(OBSSceneItem item) {
	obs_scene_t* scene = obs_sceneitem_get_scene(item);

	/*if (CoreApp->GetCurrentScene() == scene) {
    sources.push_back(item);
  }*/

	auto source = obs_sceneitem_get_source(item);
	obs_source_active(source);
	obs_source_showing(source);
	auto name = obs_source_get_name(source);
	blog(LOG_INFO, "add scene item: %s", name);

	CoreApp->SaveProject();

	if (!CoreApp->disableSaving) {
		obs_source_t* sceneSource = obs_scene_get_source(scene);
		obs_source_t* itemSource = obs_sceneitem_get_source(item);
		blog(LOG_INFO, "User added source '%s' (%s) to scene '%s'",
		     obs_source_get_name(itemSource), obs_source_get_id(itemSource),
		     obs_source_get_name(sceneSource));

		obs_scene_enum_items(scene, select_one, (obs_sceneitem_t*)item);
	}
}

void SceneSourceManager::RemoveScene(OBSSource source) {
	obs_scene_t* scene = obs_scene_from_source(source);

	Scene* sel = nullptr;
	auto count = (int)scenes.size();

	for (int i = 0; i < count; i++) {
		auto item = scenes[i];
		auto cur_scene = item->Data();
		if (cur_scene != scene)
			continue;

		sel = item;
		break;
	}

	if (sel != nullptr) {
    scenes.erase(std::remove(scenes.begin(), scenes.end(), sel), scenes.end());
    delete sel;
	}

	CoreApp->SaveProject();

	if (!CoreApp->disableSaving) {
		blog(LOG_INFO, "User Removed scene '%s'", obs_source_get_name(source));
	}

	if (CoreApp->api)
		CoreApp->api->on_event(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
}

std::vector<AudioSource> AudioSource::GetAudioSources(SourceType type) {
	auto result = std::vector<AudioSource>();

	bool input = (type == kSourceTypeAudioCapture);
	std::string tmpName = input ? "ListAudioCaptureSource(tmp)"
				    : "ListAudioPlaybackSource(tmp)";

	const char* prop_name = "device_id";
	std::string kind = "wasapi_input_capture";
	if (!input) {
		kind = "wasapi_output_capture";
	}

	OBSSourceAutoRelease source = obs_get_source_by_name(tmpName.c_str());
	if (source != nullptr) {
		blog(LOG_ERROR, "can not enum audio item list.");
		return result;
	}

	// create tmp source
	source = obs_source_create(kind.c_str(), tmpName.c_str(), nullptr, nullptr);
	// get properties
	obs_properties_t* props = obs_source_properties(source);
	// get detail
	obs_property_t* p = obs_properties_get(props, prop_name);
	size_t count = obs_property_list_item_count(p);

	for (size_t i = 0; i < count; i++) {
		const char* name = obs_property_list_item_name(p, i);
		const char* id = obs_property_list_item_string(p, i);
		blog(LOG_ERROR, "enum audio device(%s): %s, id=%s", input ? "input" : "output",
		     name, id);

		std::string defaultDeviceName =
		  Str("Basic.Settings.Advanced.Audio.MonitoringDevice.Default");
		std::string name_std_string(name);

		if (defaultDeviceName == name_std_string) { // do not use default device
			continue;
		}

		auto type = input ? kSourceTypeAudioCapture : kSourceTypeAudioPlayback;
		AudioSource item(name, id, type);

		result.push_back(item);
	}

	// release properties for enum device list.
	obs_properties_destroy(props);

	return result;
}

obs_data_t* AudioSource::Properties() {
	obs_data_t* data = obs_data_create();
	obs_data_set_string(data, "device_id", id.c_str());
	return data;
}

std::vector<CameraSource> CameraSource::GetCameraSources() {
	auto result = std::vector<CameraSource>();

	std::string tmpName = "ListCameraSource(tmp)";
	const char* kind = "dshow_input";
	const char* prop_name = "video_device_id";
	const char* resolution_p_name = "resolution";
	const char* fps_p_name = "frame_interval";

	OBSSourceAutoRelease source = obs_get_source_by_name(tmpName.c_str());
	if (source != nullptr) {
		blog(LOG_ERROR, "can not enum camera list.");
		return result;
	}
	// create tmp source
	source = obs_source_create(kind, tmpName.c_str(), nullptr, nullptr);

	// get properties
	obs_properties_t* props = obs_source_properties(source);
	// get detail
	obs_property_t* p = obs_properties_get(props, prop_name);
	size_t count = obs_property_list_item_count(p);

	for (size_t i = 0; i < count; i++) {
		const char* name = obs_property_list_item_name(p, i);
		const char* id = obs_property_list_item_string(p, i);
		blog(LOG_ERROR, "enum device id: %s, id=%s", name, id);

		std::string name_str(name);
		if (name_str.find("screen-capture-recorder") != std::string::npos ||
		    name_str.find("Virtual Camera") != std::string::npos)
			continue;

		CameraSource item(name, id);

		// make a fake selection in order to get resolution properties
		obs_data_t* data = obs_data_create();
		obs_data_set_string(data, prop_name, id);
		obs_source_reset_settings(source, data);
		obs_source_update_properties(source);
		// get properties again
		obs_properties_t* props1 = obs_source_properties(source);
		obs_property_t* p_res = obs_properties_get(props1, resolution_p_name);
		size_t count_res = obs_property_list_item_count(p_res);

		item.resolutions.reserve(count_res);
		size_t max_support_res = 0;
		for (size_t j = 0; j < count_res; j++) {
			const char* res = obs_property_list_item_name(p_res, j);
			blog(LOG_ERROR, "enum device(%s), resolution=%s", name, res);
			std::string res_str(res);
			item.resolutions.emplace_back(res);
			// support max resolution: 1920x1080
			if (res_str.find("1920") != std::string::npos) {
				max_support_res = j;
			}
		}

		if (count_res > 0) {
			// set default resolution value
			item.SelectResolution((uint32_t)max_support_res);

			// make a fake resolution selection
			obs_data_set_string(data, resolution_p_name,
					    item.selected_resolution.c_str());
			obs_data_set_int(data, "res_type", 1);
			obs_source_reset_settings(source, data);
			obs_source_update_properties(source);
			// get properties again
			obs_properties_t* props2 = obs_source_properties(source);
			// get all fps
			obs_property_t* p_fps = obs_properties_get(props2, fps_p_name);
			size_t count_fps = obs_property_list_item_count(p_fps);

			item.fps.reserve(count_fps);
			for (size_t k = 0; k < count_fps; k++) {
				const char* fps_desc = obs_property_list_item_name(p_fps, k);
				int64_t fps = obs_property_list_item_int(p_fps, k);
				blog(LOG_ERROR, "enum device(%s), fps desc=%s, value=%d", name,
				     fps_desc, fps);
				item.fps.emplace_back(std::make_tuple(std::string(fps_desc), fps));
			}

			// set default fps value(match the output of the obs)
			item.SelectFps(0);
		}

		// release anything just created for tmp using.
		obs_properties_destroy(props1);
		obs_data_release(data);

		result.push_back(item);
	}

	// release properties for enum device list.
	obs_properties_destroy(props);

	return result;
}

obs_data_t* CameraSource::Properties() {
	obs_data_t* data = obs_data_create();
	obs_data_set_string(data, "video_device_id", id.c_str());
	//resolution
	obs_data_set_string(data, "resolution", selected_resolution.c_str());
	// res_type
	obs_data_set_int(data, "res_type", 1);
	// must set last-device/id
	obs_data_set_string(data, "last_resolution", selected_resolution.c_str());
	obs_data_set_string(data, "last_video_device_id", id.c_str());
	// frame_interval
	obs_data_set_int(data, "frame_interval", std::get<1>(selected_fps));

	return data;
}

bool CameraSource::SelectResolution(uint32_t idx) {
	if (idx >= resolutions.size()) {
		return false;
	}
	selected_resolution = resolutions[idx];

	// after selecting resolution update its size.
	auto vec = std::vector<std::string>();
	split_string(selected_resolution, "x", vec);
	if (!vec.empty()) {
		size.x = std::stof(vec.front());
		size.y = std::stof(vec.back());
	}

	return true;
}

bool CameraSource::SelectFps(uint32_t idx) {
	if (idx >= fps.size()) {
		return false;
	}
	selected_fps = fps[idx];

	return true;
}

bool CameraSource::ApplyBackgroundRemoval(const std::string& model, bool forceCPU, bool enable) {
	obs_source_t* input = obs_get_source_by_name(name.c_str());
	if (!input) {
		blog(LOG_ERROR, "the source with name(%s) not exist!\n", name.c_str());
		return false;
	}

	if (obs_source_get_type(input) != OBS_SOURCE_TYPE_INPUT) {
		obs_source_release(input);
		blog(LOG_ERROR, "the source with name(%s) is not an input!\n", name.c_str());
		return false;
	}

	const char* filterName = "background removal";
	const char* filterKind = "background_removal";
	OBSSourceAutoRelease existingFilter = obs_source_get_filter_by_name(input, filterName);
	if (existingFilter != nullptr) {
		// remove it first
		blog(LOG_INFO, "filter exists, remove it first!");
		obs_source_filter_remove(input, existingFilter);
		if (!enable) {
			return true;
		}
	}

	std::string modelPath("models/");
	modelPath += model;

	obs_data_t* filterSettings = obs_data_create();
	obs_data_set_string(filterSettings, "model_select", modelPath.c_str());
	if (forceCPU) {
		obs_data_set_string(filterSettings, "useGPU", "cpu");
	}
	obs_data_set_int(filterSettings, "numThreads", 1);

	obs_source_t* filter = obs_source_create_private(filterKind, filterName, filterSettings);

	if (!filter) {
		blog(LOG_ERROR, "can not create the filter!");
		return false;
	}

	obs_source_filter_add(input, filter);

	return true;
}

std::vector<ScreenSource> ScreenSource::GetScreenSources() {
	auto result = std::vector<ScreenSource>();

	std::string tmpName = "ListScreenSource(tmp)";
	const char* prop_name = "monitor_id";
	const char* kind = "monitor_capture";
	OBSSourceAutoRelease source = obs_get_source_by_name(tmpName.c_str());
	if (source != nullptr) {
		blog(LOG_ERROR, "can not enum screen list.");
		return result;
	}

	// create tmp source
	source = obs_source_create(kind, tmpName.c_str(), nullptr, nullptr);

	// get properties
	obs_properties_t* props = obs_source_properties(source);
	// get detail
	obs_property_t* p = obs_properties_get(props, prop_name);
	size_t count = obs_property_list_item_count(p);

	for (size_t i = 0; i < count; i++) {
		const char* name = obs_property_list_item_name(p, i);
		auto id = obs_property_list_item_string(p, i);
		blog(LOG_INFO, "enum monitor: %s, id=%s", name, id);

		ScreenSource item(name, id);

		// eg: 3840x2160 @ 2560,-550
		std::string name_copy = std::string(name);
		auto vec1 = std::vector<std::string>();
		split_string(name_copy, ":", vec1);
		auto vec2 = std::vector<std::string>();
		split_string(vec1.back(), "@", vec2);
		auto size_str = vec2.front();
		replace(size_str, " ", "");
		auto vec3 = std::vector<std::string>();
		split_string(size_str, "x", vec3);
		item.size.x = std::stof(vec3.front());
		item.size.y = std::stof(vec3.back());

		result.push_back(item);
	}

	// release properties for enum device list.
	obs_properties_destroy(props);

	return result;
}

obs_data_t* ScreenSource::Properties() {
	obs_data_t* data = obs_data_create();
	obs_data_set_string(data, "monitor_id", id.c_str());

	if (EncoderAvailable("ffmpeg_nvenc")) {
		obs_data_set_int(data, "method", 2);
	} else {
		obs_data_set_int(data, "method", 0);
	}

	obs_data_set_bool(data, "cursor", true);
	return data;
}

bool ScreenSource::ScaleFitOutputSize() {
	if (size.x <= 0 || size.y <= 0)
		return false;

	float scale_x = OUTPUT_WIDTH / size.x;
	float scale_y = OUTPUT_HEIGHT / size.y;

	vec2 scale = {scale_x, scale_y};

	return Source::Resize(scale);
}

obs_data_t* RTSPSource::Properties() {
	obs_data_t* data = obs_data_create();

	obs_data_set_string(data, "url", id.c_str());
	obs_data_set_int(data, "restart_timeout", 20);
	obs_data_set_bool(data, "sync_appsink_video", false);
	obs_data_set_bool(data, "block_video", false);
	obs_data_set_bool(data, "block_audio", true);
	obs_data_set_bool(data, "hw_decode", false);
	obs_data_set_bool(data, "stop_on_hide", true);

	return data;
}

bool RTSPSource::ApplyBackgroundRemoval(const std::string& model, bool forceCPU, bool enable) {
	obs_source_t* input = obs_get_source_by_name(name.c_str());
	if (!input) {
		blog(LOG_ERROR, "the source with name(%s) not exist!\n", name.c_str());
		return false;
	}

	if (obs_source_get_type(input) != OBS_SOURCE_TYPE_INPUT) {
		obs_source_release(input);
		blog(LOG_ERROR, "the source with name(%s) is not an input!\n", name.c_str());
		return false;
	}

	const char* filterName = "background removal";
	const char* filterKind = "background_removal";
	OBSSourceAutoRelease existingFilter = obs_source_get_filter_by_name(input, filterName);
	if (existingFilter != nullptr) {
		// remove it first
		blog(LOG_INFO, "filter exists, remove it first!");
		obs_source_filter_remove(input, existingFilter);
		if (!enable) {
			return true;
		}
	}

	std::string modelPath("models/");
	modelPath += model;

	obs_data_t* filterSettings = obs_data_create();
	obs_data_set_string(filterSettings, "model_select", modelPath.c_str());
	if (forceCPU) {
		obs_data_set_string(filterSettings, "useGPU", "cpu");
	}
	obs_data_set_int(filterSettings, "numThreads", 1);

	obs_source_t* filter = obs_source_create_private(filterKind, filterName, filterSettings);

	if (!filter) {
		blog(LOG_ERROR, "can not create the filter!");
		return false;
	}

	obs_source_filter_add(input, filter);

	return true;
}

/////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////

struct CreateSceneItemData {
	obs_source_t* source;                             // In
	bool sceneItemEnabled;                            // In
	obs_transform_info* sceneItemTransform = nullptr; // In
	obs_sceneitem_crop* sceneItemCrop = nullptr;      // In
	OBSSceneItem sceneItem;                           // Out
};

static void CreateSceneItemHelper(void* _data, obs_scene_t* scene) {
	auto* data = static_cast<CreateSceneItemData*>(_data);
	data->sceneItem = obs_scene_add(scene, data->source);

	if (data->sceneItemTransform)
		obs_sceneitem_set_info(data->sceneItem, data->sceneItemTransform);

	if (data->sceneItemCrop)
		obs_sceneitem_set_crop(data->sceneItem, data->sceneItemCrop);

	obs_sceneitem_set_visible(data->sceneItem, data->sceneItemEnabled);
}

static obs_sceneitem_t* CreateSceneItem(obs_source_t* source, obs_scene_t* scene,
					bool sceneItemEnabled,
					obs_transform_info* sceneItemTransform,
					obs_sceneitem_crop* sceneItemCrop) {
	if (!(source && scene))
		return nullptr;

	// Create data struct and populate for scene item creation
	CreateSceneItemData data;
	data.source = source;
	data.sceneItemEnabled = sceneItemEnabled;
	data.sceneItemTransform = sceneItemTransform;
	data.sceneItemCrop = sceneItemCrop;

	// Enter graphics context and create the scene item
	obs_enter_graphics();
	obs_scene_atomic_update(scene, CreateSceneItemHelper, &data);
	obs_leave_graphics();

	obs_sceneitem_addref(data.sceneItem);

	return data.sceneItem;
}

static std::string SourceTypeString(SourceType type) {
	switch (type) {
	case kSourceTypeAudioCapture: return "wasapi_input_capture";
	case kSourceTypeAudioPlayback: return "wasapi_output_capture";
	case kSourceTypeScreenCapture: return "monitor_capture";
	case kSourceTypeCamera: return "dshow_input";
	case kSourceTypeRTSP: return "rtsp_source";
	default: return "";
	}
}

bool Source::IsAttached() const {
	OBSSourceAutoRelease ret = obs_get_source_by_name(name.c_str());
	if (ret == nullptr) {
		return false;
	}
	return true;
}

bool Source::Move(vec2 pos) {
	OBSScene scene = CoreApp->GetCurrentScene();
	if (scene == nullptr) {
		blog(LOG_ERROR, "FATAL! get current scene failed!");
		return false;
	}

	OBSSceneItemAutoRelease sceneItem = obs_scene_find_source(scene, name.c_str());
	if (sceneItem == nullptr) {
		blog(LOG_ERROR, "FATAL! find source failed!");
		return false;
	}

	obs_sceneitem_defer_update_begin(sceneItem);

	if (pos.x >= 0 && pos.y >= 0) {
		obs_sceneitem_set_pos(sceneItem, &pos);
		obs_sceneitem_set_alignment(sceneItem, 5);
	}

	obs_sceneitem_defer_update_end(sceneItem);

	return true;
}

bool Source::Resize(vec2 size) {
	OBSScene scene = CoreApp->GetCurrentScene();
	if (scene == nullptr) {
		blog(LOG_ERROR, "FATAL! get current scene failed!");
		return false;
	}

	OBSSceneItemAutoRelease sceneItem = obs_scene_find_source(scene, name.c_str());
	if (sceneItem == nullptr) {
		blog(LOG_ERROR, "FATAL! find source failed!");
		return false;
	}

	obs_sceneitem_defer_update_begin(sceneItem);

	if (size.x >= 0 && size.y >= 0) {
		obs_sceneitem_set_scale(sceneItem, &size);
	}

	obs_sceneitem_defer_update_end(sceneItem);

	return false;
}

bool Source::Attach() {
	OBSSourceAutoRelease ret = obs_get_source_by_name(name.c_str());
	if (ret) {
		blog(LOG_ERROR, "source with name %s already attached!", name.c_str());
		return false;
	}

	OBSDataAutoRelease inputSettings = Properties();
	// create the input source
	OBSSourceAutoRelease input =
	  obs_source_create(SourceTypeString(type).c_str(), name.c_str(), inputSettings, nullptr);
	if (!input) {
		blog(LOG_ERROR, "create scene source failed!");
		return false;
	}

	uint32_t flags = obs_source_get_output_flags(input);
	if ((flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0)
		obs_source_set_monitoring_type(input, OBS_MONITORING_TYPE_MONITOR_ONLY);

	obs_scene_t* scene = CoreApp->GetCurrentScene();
	if (scene == nullptr) {
		blog(LOG_ERROR, "FATAL! get current scene failed!");
		return false;
	}
	obs_sceneitem_t* sceneItem = CreateSceneItem(input, scene, true, nullptr, nullptr);
	if (sceneItem == nullptr) {
		blog(LOG_ERROR, "create scene item failed!");
		return false;
	}

	return true;
}

bool Source::Detach() {
	OBSScene scene = CoreApp->GetCurrentScene();
	if (scene == nullptr) {
		blog(LOG_ERROR, "FATAL! get current scene failed!");
		return false;
	}

	OBSSceneItemAutoRelease source = obs_scene_find_source(scene, name.c_str());
	if (source == nullptr) {
		blog(LOG_ERROR, "FATAL! find source failed!");
		return false;
	}
	obs_sceneitem_remove(source);

	// check input source
	obs_source_t* input = obs_get_source_by_name(name.c_str());
	if (input == nullptr) {
		blog(LOG_ERROR, "the source with name(%s) not exist!\n", name.c_str());
		return false;
	}

	if (obs_source_get_type(input) != OBS_SOURCE_TYPE_INPUT) {
		obs_source_release(input);
		blog(LOG_ERROR, "the source with name(%s) is not an input!\n", name.c_str());
		return false;
	}
	obs_source_remove(input);

	return true;
}

std::vector<Source> Source::GetAttachedSources() {
	auto result = std::vector<Source>();

	auto scene = CoreApp->GetCurrentScene();
	if (scene == nullptr) {
		blog(LOG_ERROR, "FATAL! get current scene failed!");
		return result;
	}

	blog(LOG_INFO, "enum scene item in the scene");

	auto cb = [](obs_scene_t*, obs_sceneitem_t* sceneItem, void* param) {
		auto enumData = static_cast<std::vector<core::Source>*>(param);
		// get scene item
		OBSSource itemSource = obs_sceneitem_get_source(sceneItem);
		OBSDataAutoRelease privateSettings = obs_sceneitem_get_private_settings(sceneItem);
		// get settings
		obs_data_t* settings = obs_source_get_settings(itemSource);
		// get name
		std::string name(obs_source_get_name(itemSource));
		// get id
		uint64_t sceneItemId = obs_sceneitem_get_id(sceneItem);

		if (obs_source_get_type(itemSource) == OBS_SOURCE_TYPE_INPUT) {
			const char* sourceType = obs_source_get_id(itemSource);
			std::string inputType(sourceType);

			if (inputType == "monitor_capture") {
				std::string id;
				get_source_setting_value<std::string>(settings, "monitor_id", id);
				auto item = core::ScreenSource(name, id);
				enumData->push_back(item);

			} else if (inputType == "rtsp_source") {
				std::string url;
				get_source_setting_value<std::string>(settings, "url", url);
				auto item = core::RTSPSource(name, url);
				enumData->push_back(item);
			} else if (inputType == "dshow_input") {
				std::string id;
				get_source_setting_value<std::string>(settings, "video_device_id",
								      id);
				auto item = core::CameraSource(name, id);
				enumData->push_back(item);
			} else if (inputType == "wasapi_output_capture") {
				// speakers
				std::string id;
				auto item =
				  core::AudioSource(name, id, core::kSourceTypeAudioPlayback);
				enumData->push_back(item);
			} else if (inputType == "wasapi_input_capture") {
				// microphones
				std::string id;
				get_source_setting_value<std::string>(settings, "device_id", id);
				auto item =
				  core::AudioSource(name, id, core::kSourceTypeAudioCapture);
				enumData->push_back(item);
			}
		}

		return true;
	};

	// enum scene item and save it to the scene object.
	obs_scene_enum_items(scene, cb, &result);

	return result;
}

std::optional<std::reference_wrapper<Source>> Source::GetAttachedByName(const std::string& name) {
	if (name.empty())
		return std::nullopt;

	auto scene = CoreApp->GetCurrentScene();
	if (scene == nullptr) {
		blog(LOG_ERROR, "FATAL! get current scene failed!");
		return std::nullopt;
	}

	auto sceneItem = obs_scene_find_source(scene, name.c_str());
	if (sceneItem == nullptr) {
		return std::nullopt;
	}

	OBSSource source = obs_sceneitem_get_source(sceneItem);
	if (source == nullptr) {
		return std::nullopt;
	}
	OBSDataAutoRelease privateSettings = obs_sceneitem_get_private_settings(sceneItem);
	// get settings
	obs_data_t* settings = obs_source_get_settings(source);

	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT) {
		const char* sourceType = obs_source_get_id(source);
		std::string inputType(sourceType);

		if (inputType == "monitor_capture") {
			// screen capture
			std::string id;
			get_source_setting_value<std::string>(settings, "monitor_id", id);
			auto item = core::ScreenSource(name, id);
			return std::ref(item);
		} else if (inputType == "rtsp_source") {
			// rtsp source
			std::string url;
			get_source_setting_value<std::string>(settings, "url", url);
			auto item = core::RTSPSource(name, url);
			return std::ref(item);
		} else if (inputType == "dshow_input") {
			// camera
			std::string id;
			get_source_setting_value<std::string>(settings, "video_device_id", id);
			auto item = core::CameraSource(name, id);
			return std::ref(item);
		} else if (inputType == "wasapi_output_capture") {
			// speakers
			std::string id;
			auto item = core::AudioSource(name, id, core::kSourceTypeAudioPlayback);
			return std::ref(item);
		} else if (inputType == "wasapi_input_capture") {
			// microphones
			std::string id;
			get_source_setting_value<std::string>(settings, "device_id", id);
			auto item = core::AudioSource(name, id, core::kSourceTypeAudioCapture);
			return std::ref(item);
		}
	}

	return std::nullopt;
}

bool Source::RemoveAttachedByName(const std::string& name) {
	if (name.empty())
		return false;

	auto scene = CoreApp->GetCurrentScene();
	if (scene == nullptr) {
		blog(LOG_ERROR, "FATAL! get current scene failed!");
		return false;
	}

	OBSSceneItemAutoRelease source = obs_scene_find_source(scene, name.c_str());
	if (source == nullptr) {
		blog(LOG_ERROR, "FATAL! find source failed!");
		return false;
	}
	obs_sceneitem_remove(source);

	// check input source
	obs_source_t* input = obs_get_source_by_name(name.c_str());
	if (input == nullptr) {
		blog(LOG_ERROR, "the source with name(%s) not exist!\n", name.c_str());
		return false;
	}

	if (obs_source_get_type(input) != OBS_SOURCE_TYPE_INPUT) {
		obs_source_release(input);
		blog(LOG_ERROR, "the source with name(%s) is not an input!\n", name.c_str());
		return false;
	}
	obs_source_remove(input);

	return true;
}

OBSSource Source::GetNativeSource() {
	OBSSourceAutoRelease ret = obs_get_source_by_name(name.c_str());
	if (ret) {
		blog(LOG_ERROR, "source with name %s already attached!", name.c_str());
		return nullptr;
	}

	OBSDataAutoRelease inputSettings = Properties();
	// create the input source
	obs_source_t* input =
	  obs_source_create(SourceTypeString(type).c_str(), name.c_str(), inputSettings, nullptr);
	if (!input) {
		blog(LOG_ERROR, "create scene source failed!");
		return nullptr;
	}

	uint32_t flags = obs_source_get_output_flags(input);
	if ((flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0)
		obs_source_set_monitoring_type(input, OBS_MONITORING_TYPE_MONITOR_ONLY);

	return OBSSource(input);
}

} // namespace core
