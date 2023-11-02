#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <obs.hpp>
#include <obs-frontend-api.h>

#define kMainScene "Scene"
#define kMainGroup "MainGroup"
#define kPiPGroup "PiPGroup"

namespace recorder::utils {
void SplitString(std::string& source, std::string&& token, std::vector<std::string>& result);
bool Replace(std::string& str, const std::string& from, const std::string& to);
std::string GetUUID();
} // namespace recorder::utils

namespace recorder::manager {
class OBSSourceManager;
}

namespace recorder::source {

class Scene;
class SceneItem {
public:
	enum Type { kAudioInput = 0, kAudioOutput, kCamera, kIPCamera, kScreen, kWindow };

	enum Category { kDefault = 0, kMain, kPiP };

	struct Settings {
		// position
		vec2 pos;
		// scale
		vec2 scale;
		// visability
		bool hidden;
		// lock states
		bool lock;
	};

	// the id from the scene
	virtual uint64_t SceneID() const = 0;
	virtual void SetSceneID(uint64_t id) = 0;
	// the scene item name(must be unique)
	virtual std::string Name() const = 0;
	virtual void SetName(std::string& name) = 0;
	// item type string(internal)
	virtual std::string Kind() const = 0;
	// item type enum(external)
	virtual Type type() const = 0;
	// category
	virtual Category category() const = 0;

	// item location & scale in the scene
	virtual Settings GetSettings() const = 0;
	virtual void UpdateSettings(Settings s) = 0;
	virtual void Hide(bool hidden) = 0;
	virtual void Lock(bool lock) = 0;
	virtual void UpdateScale(vec2 sacle) = 0;
	virtual void UpdatePosition(vec2 pos) = 0;

	// the original size of the scene item
	virtual vec2 OrignalSize() const = 0;

	// get item properties
	virtual obs_data_t* Properties() const = 0;

	// should apply any changes(properties or settings)
	virtual bool ShouldApplyAnyUpdates() const = 0;
	// mark the update completed
	virtual void MarkUpdateCompleted() = 0;

	virtual void SetCategory(Category c) = 0;

protected:
	virtual Scene* scene() const = 0;
};

// screen item
class ScreenSceneItem : public SceneItem {
public:
	ScreenSceneItem(std::string& name);
	~ScreenSceneItem();

	virtual uint64_t SceneID() const override;
	virtual void SetSceneID(uint64_t id) override;

	virtual std::string Name() const override;
	virtual void SetName(std::string& name) override;
	virtual std::string Kind() const override;
	virtual Type type() const override;
	virtual Category category() const override;

	virtual Settings GetSettings() const override;
	virtual void UpdateSettings(Settings b) override;
	virtual void Hide(bool hidden) override;
	virtual void Lock(bool lock) override;
	virtual void UpdateScale(vec2 sacle) override;
	virtual void UpdatePosition(vec2 pos) override;

	virtual obs_data_t* Properties() const override;
	virtual bool ShouldApplyAnyUpdates() const override;
	virtual void MarkUpdateCompleted() override;

	virtual vec2 OrignalSize() const override;

	// screen index(all screens are identified by this property)
	int index;
	// capture screen method: Auto, DXGI, or WGC (default is auto)
	int capture_method;
	// show cursor(default is true)
	bool show_cursor;
  // id
  std::string id;

protected:
	virtual Scene* scene() const override;
	virtual void SetCategory(Category c) override;

private:
	std::string name_;
	Type type_;
	Category category_;
	Settings settings_;
	vec2 size_;
	uint64_t scene_id_;
	bool should_apply_changes_;

	friend class manager::OBSSourceManager;
};

class IPCameraSceneItem : public SceneItem {
public:
	IPCameraSceneItem(std::string& name, std::string& url, bool stopOnHide);
	~IPCameraSceneItem();

	virtual uint64_t SceneID() const override;
	virtual void SetSceneID(uint64_t id) override;
	virtual std::string Name() const override;
	virtual void SetName(std::string& name) override;
	virtual std::string Kind() const override;
	virtual Type type() const override;
	virtual Category category() const override;
	virtual Settings GetSettings() const override;
	virtual void UpdateSettings(Settings b) override;
	virtual void Hide(bool hidden) override;
	virtual void Lock(bool lock) override;
	virtual void UpdateScale(vec2 sacle) override;
	virtual void UpdatePosition(vec2 pos) override;
	virtual obs_data_t* Properties() const override;
	virtual bool ShouldApplyAnyUpdates() const override;
	virtual void MarkUpdateCompleted() override;

	virtual vec2 OrignalSize() const override;

	void UpdateURL(std::string& url);
	void UpdateStopOnHide(bool state);

protected:
	virtual Scene* scene() const override;
	virtual void SetCategory(Category c) override;

private:
	std::string name_;
	Type type_;
	Category category_;
	Settings settings_;
	uint64_t scene_id_;
	bool should_apply_changes_;
	std::string url_;
	bool stop_on_hide_;
	vec2 size_;

	friend class manager::OBSSourceManager;
};

class CameraSceneItem : public SceneItem {
public:
	CameraSceneItem(std::string& name);
	~CameraSceneItem();

	virtual uint64_t SceneID() const override;
	virtual void SetSceneID(uint64_t id) override;
	virtual std::string Name() const override;
	virtual void SetName(std::string& name) override;
	virtual std::string Kind() const override;
	virtual Type type() const override;
	virtual Category category() const override;
	virtual Settings GetSettings() const override;
	virtual void UpdateSettings(Settings b) override;
	virtual void Hide(bool hidden) override;
	virtual void Lock(bool lock) override;
	virtual void UpdateScale(vec2 sacle) override;
	virtual void UpdatePosition(vec2 pos) override;
	virtual obs_data_t* Properties() const override;
	virtual bool ShouldApplyAnyUpdates() const override;
	virtual void MarkUpdateCompleted() override;
	virtual vec2 OrignalSize() const override;

	// get a list of available res & fps
	void GetAvailableResolutions(std::vector<std::string>& res) const;
	void GetAvailableFps(std::vector<std::tuple<std::string, int64_t>>& fps) const;
	// select resolution & fps
	bool SelectResolution(uint32_t idx);
	bool SelectFps(uint32_t idx);

protected:
	virtual Scene* scene() const override;
	virtual void SetCategory(Category c) override;

private:
	std::string name_;
	Type type_;
	Category category_;
	Settings settings_;
	uint64_t scene_id_;
	bool should_apply_changes_;
	vec2 size_;

	// device id
	std::string device_id_;
	std::vector<std::string> resolutions_;
	// current selected resolution(default is the first element in the vector(resolutions_))
	std::string selected_res_;
	// all available fps, for example: (Match Output FPS, -1), (Highest FPS, 0), (30, 333333)....
	std::vector<std::tuple<std::string, int64_t>> fps_;
	std::tuple<std::string, int64_t> selected_fps_;

	friend class manager::OBSSourceManager;
};

class AudioSceneItem : public SceneItem {
public:
	AudioSceneItem() = delete;

	virtual uint64_t SceneID() const override;
	virtual void SetSceneID(uint64_t id) override;
	virtual std::string Name() const override;
	virtual void SetName(std::string& name) override;
	virtual Settings GetSettings() const override;
	virtual void UpdateSettings(Settings b) override;
	virtual void Hide(bool hidden) override;
	virtual void Lock(bool lock) override;
	virtual void UpdateScale(vec2 sacle) override{};
	virtual void UpdatePosition(vec2 pos) override{};
	virtual bool ShouldApplyAnyUpdates() const override;
	virtual void MarkUpdateCompleted() override;
	virtual obs_data_t* Properties() const override;
	virtual Category category() const override;
	virtual vec2 OrignalSize() const override { return {0, 0}; };

	virtual std::string Kind() const = 0;
	virtual Type type() const = 0;

protected:
	AudioSceneItem(std::string& name);
	virtual void SetCategory(Category c) override{};
	virtual ~AudioSceneItem();

	virtual Scene* scene() const override;

	std::string name_;
	Type type_;
	Category category_;

	Settings settings_;
	uint64_t scene_id_;
	bool should_apply_changes_;

	// device id
	std::string device_id_;

	friend class manager::OBSSourceManager;
};

class AudioInputItem : public AudioSceneItem {
public:
	AudioInputItem(std::string& name);
	virtual ~AudioInputItem();

	virtual std::string Kind() const override;
	virtual Type type() const override;
};

class AudioOutputItem : public AudioSceneItem {
public:
	AudioOutputItem(std::string& name);
	virtual ~AudioOutputItem();

	virtual std::string Kind() const override;
	virtual Type type() const override;
};

class Scene {
public:
	Scene(std::string& name, obs_scene_t* src);
	Scene(const Scene& copy) = delete;
	~Scene();

	bool Attach(SceneItem* item, SceneItem::Category category);
	bool Detach(SceneItem* item);
	bool ApplySceneItemSettingsUpdate(SceneItem* item);

	int FindFirstPiPSceneItemIndex();

private:
	std::string name_;
	obs_scene_t* scene_;

	std::vector<SceneItem*> items_;

	// obs group manipulates
	void CreateGroups();
	bool CreateGroup(const char* name);

	obs_sceneitem_t* CreateSceneItem(obs_source_t* source, obs_scene_t* scene,
					 bool sceneItemEnabled,
					 obs_transform_info* sceneItemTransform,
					 obs_sceneitem_crop* sceneItemCrop);

	friend manager::OBSSourceManager;
};

} // namespace recorder::source
