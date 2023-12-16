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
} // namespace core
