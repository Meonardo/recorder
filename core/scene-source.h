#pragma once

#include <string>
#include <vector>
#include <memory>

#include <obs.hpp>

namespace core {
template<typename OBSRef> struct SignalContainer {
	OBSRef ref;
	std::vector<std::shared_ptr<OBSSignal>> handlers;
};

class SceneSourceManager;
class Scene {
public:
	Scene(obs_scene_t* data, const std::string& name) : data(data), name(name) {}
	~Scene() {}

	obs_scene_t* Data() const { return data; }

	void AddSignalHandler(signal_handler_t* handler);

private:
	obs_scene_t* data;
	SignalContainer<OBSScene> container;
	std::string name;
};

class Source {
public:
	Source(obs_source_t* data, const std::string& name) : data(data), name(name) {}
	~Source() {}

private:
	obs_source_t* data;
	std::string name;
};

class SceneSourceManager {
public:
	SceneSourceManager();
	~SceneSourceManager();

	static void SceneReordered(void* data, calldata_t* params);
	static void SceneRefreshed(void* data, calldata_t* params);
	static void SceneItemAdded(void* data, calldata_t* params);
	static void SourceCreated(void* data, calldata_t* params);
	static void SourceRemoved(void* data, calldata_t* params);
	static void SourceActivated(void* data, calldata_t* params);
	static void SourceDeactivated(void* data, calldata_t* params);
	static void SourceAudioDeactivated(void* data, calldata_t* params);
	static void SourceAudioActivated(void* data, calldata_t* params);
	static void SourceRenamed(void* data, calldata_t* params);

	void AddScene(Scene* scene) { scenes.push_back(scene); }
	void RemoveScene(Scene* scene) {
		scenes.erase(std::remove(scenes.begin(), scenes.end(), scene), scenes.end());
	}
	std::vector<Scene*> Scenes() const { return scenes; }

private:
	std::vector<OBSSignal> signalHandlers;
	std::vector<Scene*> scenes;
	std::vector<OBSSceneItem> sources;

	void AddScene(OBSSource scene);
	void AddSceneItem(OBSSceneItem item);
	void RemoveScene(OBSSource source);
};
} // namespace core
