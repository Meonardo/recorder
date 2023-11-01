#include "obs-sources.h"

#include <chrono>
#include <thread>
#include <random>

extern bool EncoderAvailable(const char* encoder);

namespace recorder::utils {
void SplitString(std::string& source, std::string&& token, std::vector<std::string>& result) {
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

bool Replace(std::string& str, const std::string& from, const std::string& to) {
	size_t start_pos = str.find(from);
	if (start_pos == std::string::npos)
		return false;
	str.replace(start_pos, from.length(), to);
	return true;
}

std::string GetUUID() {
	static std::random_device dev;
	static std::mt19937 rng(dev());

	std::uniform_int_distribution<int> dist(0, 15);

	const char* v = "0123456789abcdef";
	const bool dash[] = {0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0};

	std::string res;
	for (int i = 0; i < 16; i++) {
		if (dash[i])
			res += "-";
		res += v[dist(rng)];
		res += v[dist(rng)];
	}
	return res;
}
} // namespace recorder::utils

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

namespace recorder::source {

#pragma region Screen scene item
ScreenSceneItem::ScreenSceneItem(std::string& name)
  : name_(name),
    index(0),
    capture_method(0),
    show_cursor(true),
    should_apply_changes_(false) {
	settings_.lock = false;
	settings_.pos.x = -1;
	settings_.pos.y = -1;
	settings_.hidden = false;
	settings_.scale.x = 0.f;
	settings_.scale.y = 0.f;
	size_ = {0, 0};
}

ScreenSceneItem::~ScreenSceneItem() {}

uint64_t ScreenSceneItem::SceneID() const {
	return scene_id_;
}

void ScreenSceneItem::SetSceneID(uint64_t id) {
	scene_id_ = id;
}

std::string ScreenSceneItem::Name() const {
	return name_;
}

void ScreenSceneItem::SetName(std::string& name) {
	if (name == name_)
		return;
	name_ = name;
}

std::string ScreenSceneItem::Kind() const {
	return "monitor_capture";
}

SceneItem::Type ScreenSceneItem::type() const {
	return kScreen;
}

SceneItem::Category ScreenSceneItem::category() const {
	return category_;
}

Scene* ScreenSceneItem::scene() const {
	return nullptr;
}

SceneItem::Settings ScreenSceneItem::GetSettings() const {
	return settings_;
}

void ScreenSceneItem::SetCategory(Category c) {
	category_ = c;
}

void ScreenSceneItem::UpdateSettings(SceneItem::Settings settings) {
	vec2_copy(&settings_.pos, &settings.pos);
	vec2_copy(&settings_.scale, &settings.scale);
	settings_.hidden = settings.hidden;
	settings_.lock = settings.lock;

	should_apply_changes_ = true;
}

void ScreenSceneItem::Hide(bool hidden) {
	if (hidden == settings_.hidden)
		return;
	settings_.hidden = hidden;
	should_apply_changes_ = true;
}

void ScreenSceneItem::Lock(bool lock) {
	if (lock == settings_.lock)
		return;
	settings_.lock = lock;
	should_apply_changes_ = true;
}

void ScreenSceneItem::UpdateScale(vec2 scale) {
	if (scale.x == settings_.scale.x && scale.y == settings_.scale.y) {
		return;
	}
	vec2_copy(&settings_.scale, &scale);
	should_apply_changes_ = true;
}

void ScreenSceneItem::UpdatePosition(vec2 pos) {
	if (pos.x == settings_.pos.x && pos.y == settings_.pos.y) {
		return;
	}
	vec2_copy(&settings_.scale, &pos);
	should_apply_changes_ = true;
}

vec2 ScreenSceneItem::OrignalSize() const {
	return size_;
}

bool ScreenSceneItem::ShouldApplyAnyUpdates() const {
	return should_apply_changes_;
}

void ScreenSceneItem::MarkUpdateCompleted() {
	should_apply_changes_ = false;
}

obs_data_t* ScreenSceneItem::Properties() const {
	obs_data_t* data = obs_data_create();
	obs_data_set_int(data, "monitor", index);

	if (EncoderAvailable("ffmpeg_nvenc")) {
		obs_data_set_int(data, "method", 2);
	} else {
		obs_data_set_int(data, "method", 0);
	}

	obs_data_set_bool(data, "cursor", show_cursor);
	return data;
}

#pragma endregion Sceen item

#pragma region IPCamera item
IPCameraSceneItem::IPCameraSceneItem(std::string& name, std::string& url, bool stopOnHide)
  : name_(name),
    url_(url),
    stop_on_hide_(stopOnHide),
    should_apply_changes_(false) {
	settings_.lock = false;
	settings_.pos.x = -1;
	settings_.pos.y = -1;
	settings_.hidden = false;
	settings_.scale.x = 0.f;
	settings_.scale.y = 0.f;
	size_ = {1920, 1080};
}

IPCameraSceneItem::~IPCameraSceneItem() {}

uint64_t IPCameraSceneItem::SceneID() const {
	return scene_id_;
}

void IPCameraSceneItem::SetSceneID(uint64_t id) {
	scene_id_ = id;
}

std::string IPCameraSceneItem::Name() const {
	return name_;
}

void IPCameraSceneItem::SetName(std::string& name) {
	if (name == name_)
		return;
	name_ = name;
}

std::string IPCameraSceneItem::Kind() const {
	return "gstreamer-source";
}

SceneItem::Type IPCameraSceneItem::type() const {
	return kIPCamera;
}

SceneItem::Category IPCameraSceneItem::category() const {
	return category_;
}

void IPCameraSceneItem::SetCategory(Category c) {
	category_ = c;
}

Scene* IPCameraSceneItem::scene() const {
	return nullptr;
}

SceneItem::Settings IPCameraSceneItem::GetSettings() const {
	return settings_;
}

vec2 IPCameraSceneItem::OrignalSize() const {
	return size_;
}

void IPCameraSceneItem::UpdateSettings(SceneItem::Settings settings) {
	vec2_copy(&settings_.pos, &settings.pos);
	vec2_copy(&settings_.scale, &settings.scale);
	settings_.hidden = settings.hidden;
	settings_.lock = settings.lock;

	should_apply_changes_ = true;
}

void IPCameraSceneItem::Hide(bool hidden) {
	if (hidden == settings_.hidden)
		return;
	settings_.hidden = hidden;
	should_apply_changes_ = true;
}

void IPCameraSceneItem::Lock(bool lock) {
	if (lock == settings_.lock)
		return;
	settings_.lock = lock;
	should_apply_changes_ = true;
}

void IPCameraSceneItem::UpdateScale(vec2 scale) {
	if (scale.x == settings_.scale.x && scale.y == settings_.scale.y) {
		return;
	}
	vec2_copy(&settings_.scale, &scale);
	should_apply_changes_ = true;
}

void IPCameraSceneItem::UpdatePosition(vec2 pos) {
	if (pos.x == settings_.pos.x && pos.y == settings_.pos.y) {
		return;
	}
	vec2_copy(&settings_.scale, &pos);
	should_apply_changes_ = true;
}

void IPCameraSceneItem::UpdateURL(std::string& url) {
	if (url.empty() || url == url_) {
		return;
	}
	url_ = url;
	should_apply_changes_ = true;
}

void IPCameraSceneItem::UpdateStopOnHide(bool state) {
	if (stop_on_hide_ == state)
		return;
	stop_on_hide_ = state;
	should_apply_changes_ = true;
}

bool IPCameraSceneItem::ShouldApplyAnyUpdates() const {
	return should_apply_changes_;
}

void IPCameraSceneItem::MarkUpdateCompleted() {
	should_apply_changes_ = false;
}

obs_data_t* IPCameraSceneItem::Properties() const {
	obs_data_t* data = obs_data_create();
	const size_t count = 512;
	char pipeline[count];
	snprintf(pipeline, count, "uridecodebin uri=%s name=bin latency=50 ! queue ! video.",
		 url_.c_str());

	obs_data_set_string(data, "pipeline", pipeline);
	obs_data_set_bool(data, "sync_appsink_audio", false);
	obs_data_set_bool(data, "sync_appsink_video", false);
	obs_data_set_bool(data, "disable_async_appsink_video", false);
	obs_data_set_bool(data, "disable_async_appsink_audio", false);
	obs_data_set_bool(data, "block_video", true);
	obs_data_set_bool(data, "restart_on_error", true);
	obs_data_set_bool(data, "use_timestamps_audio", false);
	obs_data_set_bool(data, "use_timestamps_video", false);
	obs_data_set_bool(data, "stop_on_hide", stop_on_hide_);

	return data;
}
#pragma endregion IPCamera item

#pragma region Camera item
CameraSceneItem::CameraSceneItem(std::string& name) : name_(name), should_apply_changes_(false) {
	settings_.lock = false;
	settings_.pos.x = -1;
	settings_.pos.y = -1;
	settings_.hidden = false;
	settings_.scale.x = 0.f;
	settings_.scale.y = 0.f;
	size_ = {0, 0};
}

CameraSceneItem::~CameraSceneItem() {}

uint64_t CameraSceneItem::SceneID() const {
	return scene_id_;
}

void CameraSceneItem::SetSceneID(uint64_t id) {
	scene_id_ = id;
}

std::string CameraSceneItem::Name() const {
	return name_;
}

void CameraSceneItem::SetName(std::string& name) {
	if (name == name_)
		return;
	name_ = name;
}

std::string CameraSceneItem::Kind() const {
	return "dshow_input";
}

SceneItem::Type CameraSceneItem::type() const {
	return kCamera;
}

SceneItem::Category CameraSceneItem::category() const {
	return category_;
}

void CameraSceneItem::SetCategory(Category c) {
	category_ = c;
}

Scene* CameraSceneItem::scene() const {
	return nullptr;
}

SceneItem::Settings CameraSceneItem::GetSettings() const {
	return settings_;
}

void CameraSceneItem::UpdateSettings(SceneItem::Settings settings) {
	vec2_copy(&settings_.pos, &settings.pos);
	vec2_copy(&settings_.scale, &settings.scale);
	settings_.hidden = settings.hidden;
	settings_.lock = settings.lock;

	should_apply_changes_ = true;
}

void CameraSceneItem::Hide(bool hidden) {
	if (hidden == settings_.hidden)
		return;
	settings_.hidden = hidden;
	should_apply_changes_ = true;
}

void CameraSceneItem::Lock(bool lock) {
	if (lock == settings_.lock)
		return;
	settings_.lock = lock;
	should_apply_changes_ = true;
}

void CameraSceneItem::UpdateScale(vec2 scale) {
	if (scale.x == settings_.scale.x && scale.y == settings_.scale.y) {
		return;
	}
	vec2_copy(&settings_.scale, &scale);
	should_apply_changes_ = true;
}

void CameraSceneItem::UpdatePosition(vec2 pos) {
	if (pos.x == settings_.pos.x && pos.y == settings_.pos.y) {
		return;
	}
	vec2_copy(&settings_.pos, &pos);
	should_apply_changes_ = true;
}

bool CameraSceneItem::ShouldApplyAnyUpdates() const {
	return should_apply_changes_;
}

void CameraSceneItem::MarkUpdateCompleted() {
	should_apply_changes_ = false;
}

vec2 CameraSceneItem::OrignalSize() const {
	return size_;
}

bool CameraSceneItem::SelectResolution(uint32_t idx) {
	if (idx >= resolutions_.size()) {
		return false;
	}
	selected_res_ = resolutions_[idx];

	// after selecting resolution update its size.
	auto vec = std::vector<std::string>();
	utils::SplitString(selected_res_, "x", vec);
	if (!vec.empty()) {
		size_.x = std::stof(vec.front());
		size_.y = std::stof(vec.back());
	}

	return true;
}

bool CameraSceneItem::SelectFps(uint32_t idx) {
	if (idx >= fps_.size()) {
		return false;
	}
	selected_fps_ = fps_[idx];

	return true;
}

void CameraSceneItem::GetAvailableResolutions(std::vector<std::string>& res) const {
	// make copy of the resolutions_
	for (auto r : resolutions_) { res.emplace_back(std::move(r)); }
}

void CameraSceneItem::GetAvailableFps(std::vector<std::tuple<std::string, int64_t>>& fps) const {
	// make copy of the fps_
	for (auto r : fps_) { fps.emplace_back(std::move(r)); }
}

obs_data_t* CameraSceneItem::Properties() const {
	obs_data_t* data = obs_data_create();
	obs_data_set_string(data, "video_device_id", device_id_.c_str());
	//resolution
	obs_data_set_string(data, "resolution", selected_res_.c_str());
	// res_type
	obs_data_set_int(data, "res_type", 1);
	// must set last-device/id
	obs_data_set_string(data, "last_resolution", selected_res_.c_str());
	obs_data_set_string(data, "last_video_device_id", device_id_.c_str());
	// frame_interval
	obs_data_set_int(data, "frame_interval", std::get<1>(selected_fps_));

	return data;
}
#pragma endregion Camera item

#pragma region Audio item
AudioSceneItem::AudioSceneItem(std::string& name) : name_(name), should_apply_changes_(false) {
	settings_.lock = false;
	settings_.pos.x = -1;
	settings_.pos.y = -1;
	settings_.hidden = false;
	settings_.scale.x = 0.f;
	settings_.scale.y = 0.f;
	category_ = kDefault;
}

AudioSceneItem::~AudioSceneItem() {}

uint64_t AudioSceneItem::SceneID() const {
	return scene_id_;
}

void AudioSceneItem::SetSceneID(uint64_t id) {
	scene_id_ = id;
}

std::string AudioSceneItem::Name() const {
	return name_;
}

void AudioSceneItem::SetName(std::string& name) {
	if (name == name_)
		return;
	name_ = name;
}

SceneItem::Category AudioSceneItem::category() const {
	return category_;
}

Scene* AudioSceneItem::scene() const {
	return nullptr;
}

SceneItem::Settings AudioSceneItem::GetSettings() const {
	return settings_;
}

void AudioSceneItem::UpdateSettings(SceneItem::Settings settings) {
	settings_.hidden = settings.hidden;
	settings_.lock = settings.lock;

	should_apply_changes_ = true;
}

void AudioSceneItem::Hide(bool hidden) {
	if (hidden == settings_.hidden)
		return;
	settings_.hidden = hidden;
	should_apply_changes_ = true;
}

void AudioSceneItem::Lock(bool lock) {
	if (lock == settings_.lock)
		return;
	settings_.lock = lock;
	should_apply_changes_ = true;
}

bool AudioSceneItem::ShouldApplyAnyUpdates() const {
	return should_apply_changes_;
}

void AudioSceneItem::MarkUpdateCompleted() {
	should_apply_changes_ = false;
}

obs_data_t* AudioSceneItem::Properties() const {
	obs_data_t* data = obs_data_create();
	obs_data_set_string(data, "device_id", device_id_.c_str());
	return data;
}

////////////////////////////////////////////////////////////////////////////////////
// AudioInputItem subclass of AudioSceneItem
AudioInputItem::AudioInputItem(std::string& name) : AudioSceneItem(name) {}

AudioInputItem::~AudioInputItem() {}

std::string AudioInputItem::Kind() const {
	return "wasapi_input_capture";
}

SceneItem::Type AudioInputItem::type() const {
	return kAudioInput;
}

////////////////////////////////////////////////////////////////////////////////////
// AudioOutputItem subclass of AudioSceneItem
AudioOutputItem::AudioOutputItem(std::string& name) : AudioSceneItem(name) {}

AudioOutputItem::~AudioOutputItem() {}

std::string AudioOutputItem::Kind() const {
	return "wasapi_output_capture";
}

SceneItem::Type AudioOutputItem::type() const {
	return kAudioOutput;
}

#pragma endregion Audio item

////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

Scene::Scene(std::string& name, obs_scene_t* src) : name_(name), scene_(src) {}

Scene::~Scene() {
	obs_scene_release(scene_);

	for (auto& item : items_) {
		delete item;
		item = nullptr;
	}
	items_.clear();
}

bool Scene::Attach(SceneItem* item, SceneItem::Category category) {
	OBSSourceAutoRelease ret = obs_get_source_by_name(item->Name().c_str());
	if (ret) {
		blog(LOG_ERROR, "source with name %s already attached!", item->Name().c_str());
		//obs_source_release(ret);
		return false;
	}

	OBSDataAutoRelease inputSettings = item->Properties();
	// create the input source
	OBSSourceAutoRelease input =
	  obs_source_create(item->Kind().c_str(), item->Name().c_str(), inputSettings, nullptr);
	if (!input) {
		blog(LOG_ERROR, "create scene source failed!");
		return false;
	}

	uint32_t flags = obs_source_get_output_flags(input);
	if ((flags & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0)
		obs_source_set_monitoring_type(input, OBS_MONITORING_TYPE_MONITOR_ONLY);

	// create a scene item for the input(!!!do not use auto release!!!)
	obs_sceneitem_t* sceneItem = CreateSceneItem(input, scene_, true, NULL, NULL);
	if (sceneItem == nullptr) {
		blog(LOG_ERROR, "create scene item failed!");
		return false;
	}

	// save scene id
	item->SetSceneID(obs_sceneitem_get_id(sceneItem));

	// save the category to the private setting of the scene.
	OBSDataAutoRelease privateSettings = obs_sceneitem_get_private_settings(sceneItem);
	OBSDataAutoRelease newPrivateSettings = obs_data_create();
	obs_data_set_int(newPrivateSettings, "category", uint64_t(category));
	obs_data_apply(privateSettings, newPrivateSettings);

	// save item to vector
	items_.push_back(item);

	return true;
}

bool Scene::Detach(SceneItem* item, bool deleteIt) {
	if (item == nullptr)
		return false;

	items_.erase(
	  std::remove_if(items_.begin(), items_.end(),
			 [item](const SceneItem* scene_item) { return scene_item == item; }),
	  items_.end());
	if (deleteIt) {
		delete item;
		item = nullptr;
	}
	return true;
}

bool Scene::CreateGroup(const char* name) {
	obs_source_t* ret = obs_get_source_by_name(name);
	if (ret) {
		blog(LOG_ERROR, "group with name %s already attached!", name);
		return false;
	}

	auto groupItem = obs_scene_add_group(scene_, name);

	return groupItem != nullptr;
}

void Scene::CreateGroups() {
	CreateGroup(kMainGroup);
	CreateGroup(kPiPGroup);
}

bool Scene::ApplySceneItemSettingsUpdate(SceneItem* item) {
	if (!item->ShouldApplyAnyUpdates()) {
		blog(LOG_INFO, "nothing changed, no need to update.");
		return false;
	}

	OBSSceneItem sceneItem = obs_scene_find_sceneitem_by_id(scene_, item->SceneID());
	if (sceneItem == nullptr) {
		blog(LOG_ERROR, "can not find the scene item in the scene!");
		return false;
	}

	obs_sceneitem_defer_update_begin(sceneItem);
	// visibility
	obs_sceneitem_set_visible(sceneItem, !item->GetSettings().hidden);
	// transform
	vec2 newPosition = item->GetSettings().pos;
	if (newPosition.x >= 0 && newPosition.y >= 0) {
		obs_sceneitem_set_pos(sceneItem, &newPosition);
		obs_sceneitem_set_alignment(sceneItem, 5);
	}
	// scale
	vec2 newScale = item->GetSettings().scale;
	if (newScale.x > 0 && newScale.y > 0) {
		obs_sceneitem_set_scale(sceneItem, &newScale);
	}
	// lock
	obs_sceneitem_set_locked(sceneItem, item->GetSettings().lock);
	obs_sceneitem_defer_update_end(sceneItem);

	// mark updates completed
	item->MarkUpdateCompleted();

	return true;
}

int Scene::FindFirstPiPSceneItemIndex() {
	auto vec = std::vector<SceneItem*>();
	for (auto& item : items_) {
		if (item->category() == SceneItem::Category::kPiP)
			vec.emplace_back(item);
	}
	if (vec.empty())
		return -1;

	std::sort(vec.begin(), vec.end(), [](SceneItem* item1, SceneItem* item2) {
		return item1->SceneID() < item2->SceneID();
	});

	auto target = vec.front();
	auto sceneItem = obs_scene_find_sceneitem_by_id(scene_, target->SceneID());
	if (sceneItem == nullptr)
		return -1;

	return obs_sceneitem_get_order_position(sceneItem);
}

obs_sceneitem_t* Scene::CreateSceneItem(obs_source_t* source, obs_scene_t* scene,
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

} // namespace recorder::source
