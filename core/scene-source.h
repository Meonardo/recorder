#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

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

/// not for external use
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

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

enum SourceType {
	kSourceTypeUnknow = 1,
	kSourceTypeAudioCapture,
	kSourceTypeAudioPlayback,
	kSourceTypeScreenCapture,
	kSourceTypeCamera,
	kSourceTypeRTSP,
};

class Source {
public:
	Source(const std::string& name, const std::string& id, SourceType type)
	  : name(name),
	    id(id),
	    type(type),
	    size({0, 0}) {}
	virtual ~Source() {}

	virtual const std::string& Name() const { return name; }
	virtual SourceType Type() const { return type; }
	virtual const std::string& ID() const { return id; }
	virtual vec2 Size() const { return size; }

	virtual obs_data_t* Properties() { return nullptr; };

	virtual bool Attach();
	virtual bool Detach();

	static std::vector<Source> GetAttachedSources();

	static std::optional<std::reference_wrapper<Source>>
	GetAttachedByName(const std::string& name);

	static bool RemoveAttachedByName(const std::string& name);

protected:
	std::string name;
	std::string id;
	SourceType type;
	vec2 size;
};

class AudioSource : public Source {
public:
	AudioSource(const std::string& name, const std::string& id, SourceType type)
	  : Source(name, id, type) {}
	virtual ~AudioSource() {}

	static std::vector<AudioSource> GetAudioSources(SourceType type);

	virtual obs_data_t* Properties() override;
};

class RTSPSource : public Source {
public:
	RTSPSource(const std::string& name, const std::string& url)
	  : Source(name, url, kSourceTypeRTSP) {}
	virtual ~RTSPSource() {}

	virtual obs_data_t* Properties() override;
};

class CameraSource : public Source {
public:
	CameraSource(const std::string& name, const std::string& id)
	  : Source(name, id, kSourceTypeCamera) {}
	virtual ~CameraSource() {}

	static std::vector<CameraSource> GetCameraSources();

	virtual obs_data_t* Properties() override;

	bool SelectResolution(uint32_t idx);
	bool SelectFps(uint32_t idx);

	const std::vector<std::string>& GetAvailableResolutions() const { return resolutions; }
	const std::string& GetSelectedResolution() const { return selected_resolution; }
	const std::vector<std::tuple<std::string, int64_t>>& GetAvailableFps() const { return fps; }
	const std::tuple<std::string, int64_t>& GetSelectedFps() const { return selected_fps; }

private:
	std::vector<std::string> resolutions;
	// current selected resolution(default is the first element in the vector(resolutions))
	std::string selected_resolution;
	// all available fps, for example: (Match Output FPS, -1), (Highest FPS, 0), (30, 333333)....
	std::vector<std::tuple<std::string, int64_t>> fps;
	std::tuple<std::string, int64_t> selected_fps;
};

class ScreenSource : public Source {
public:
	ScreenSource(const std::string& name, const std::string& id)
	  : Source(name, id, kSourceTypeScreenCapture) {}
	virtual ~ScreenSource() {}

	static std::vector<ScreenSource> GetScreenSources();

	virtual obs_data_t* Properties() override;
};

} // namespace core
