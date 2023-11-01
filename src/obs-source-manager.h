#pragma once

#include "obs-sources.h"
#include <obs-frontend-internal.hpp>

namespace recorder::manager {
class OBSSourceManager {
public:
	OBSSourceManager();
	~OBSSourceManager();

	void AddEventsSender(obs_frontend_callbacks* api);

	// check if the main is created
	bool IsMainSceneCreated() const;
	// all items already in the scene
	void AvailableSceneItems(std::vector<source::SceneItem*>& items) const;
	// rename the scene item
	bool Rename(source::SceneItem* item, std::string& newName);
	// attach the scene item to the main scene
	bool AttachSceneItem(source::SceneItem* item, source::SceneItem::Category category =
							source::SceneItem::Category::kDefault);
	// apply scene item properties settings(internal settings)
	bool ApplySceneItemPropertiesUpdate(source::SceneItem* item);
	// apply scene item settings update(in the scene)
	bool ApplySceneItemSettingsUpdate(source::SceneItem* item);

	// list all the screens
	void ListScreenItems(std::vector<std::shared_ptr<source::ScreenSceneItem>>& items);

	// create a IPCamera scene item
	source::IPCameraSceneItem* CreateIPCameraItem(std::string& name, std::string& url);

	// list all usb cameras
	void ListCameraItems(std::vector<std::shared_ptr<source::CameraSceneItem>>& items);

	// list all AudioInputDevices & AudioOutputDevices
	void ListAudioItems(std::vector<std::shared_ptr<source::AudioSceneItem>>& items,
			    bool input = true, bool disableFilter = false);

	// find the scene item by the given name
	source::SceneItem* GetSceneItemByName(std::string& name);
	// remove scene item
	bool RemoveSceneItemByName(std::string& name);
	bool Remove(source::SceneItem* item);

	// start virtual camera
	bool StartVirtualCamera();
	// stop virtual camera
	bool StopVirtualCamera();

	// start janus stream
	bool StartJanusStream();
	// stop janus stream
	bool StopJanusStream();

	// set stream address, like: rtmp://192.168.99.135
	bool SetStreamAddress(std::string& addr, std::string& username, std::string& passwd);
	void GetSteamAddress(std::string& address, std::string& username, std::string& passwd);
	// start streaming
	bool StartStreaming();
	// stop streaming
	bool StopStreaming();

	void AddDefaultAudioSource();

private:
	void LoadGroups(std::vector<obs_source_t*>& groups);
	void LoadSceneItemFromScene(std::string& sceneName);
	void RemoveScene(std::string& name);
	obs_scene_t* CreateScene(std::string& name);

	// mix sounds with other input/output item to virtual microphone(VB Cabel)
	bool AddAudioMixFilter(source::AudioSceneItem* item);
	//void RemoveFilterByName(obs_source_t *source, const char* name);

	template<typename T>
	static void GetSettingValueWithName(obs_data_t* d, const char* settingName, T& result);
	template<>
	static void GetSettingValueWithName(obs_data_t* d, const char* settingName,
					    std::string& result);

	static obs_source_t* ValidateScene(const std::string& name);
	static obs_source_t* ValidateInput(const std::string& name);
	static obs_scene_t* ValidateScene2(const std::string& keyName);
	static bool VirtualCamAvailable();

	source::Scene* main_scene_;
	obs_frontend_callbacks* api_;
};

} //namespace recorder::manager
