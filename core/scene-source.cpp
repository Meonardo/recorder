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
SceneSourceManager::~SceneSourceManager() {}

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

	if (CoreApp->GetCurrentScene() == scene)
		sources.push_back(item);

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

		std::string name_std_string(name);
		auto type = input ? kSourceTypeAudioCapture : kSourceTypeAudioPlayback;
		AudioSource item(name, id, type);
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
		result.push_back(item);

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

} // namespace core
