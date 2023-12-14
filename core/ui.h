#pragma once

#include <string>
#include <vector>

#include <obs.hpp>
#include <obs-frontend-internal.hpp>

namespace core {
class UIApplication {
public:
	virtual ~UIApplication() = default;

	virtual int execute() = 0;
};

class UIWindow {
public:
	UIWindow();
	~UIWindow();

private:
};

class SourceSignalHandler {
public:
	SourceSignalHandler();
	~SourceSignalHandler();

	static void SourceCreated(void* data, calldata_t* params);
	static void SourceRemoved(void* data, calldata_t* params);
	static void SourceActivated(void* data, calldata_t* params);
	static void SourceDeactivated(void* data, calldata_t* params);
	static void SourceAudioDeactivated(void* data, calldata_t* params);
	static void SourceAudioActivated(void* data, calldata_t* params);
	static void SourceRenamed(void* data, calldata_t* params);

private:
	std::vector<OBSSignal> signalHandlers;
};

class Scene {
public:
	Scene(obs_scene_t* data, const std::string& name) : data(data), name(name) {}
	~Scene() {}

	obs_scene_t* Data() const { return data; }

private:
	obs_scene_t* data;
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

} // namespace core
