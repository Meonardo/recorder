#include "app.h"

#include <string>
#include <stdint.h>
#include <functional>

namespace core {

void EnumProfiles(std::function<bool(const char*, const char*)>&& cb) {
	char path[512];
	os_glob_t* glob;

	int ret = GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles/*");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return;
	}

	if (os_glob(path, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob profiles");
		return;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char* filePath = glob->gl_pathv[i].path;
		const char* dirName = strrchr(filePath, '/') + 1;

		if (!glob->gl_pathv[i].directory)
			continue;

		if (strcmp(dirName, ".") == 0 || strcmp(dirName, "..") == 0)
			continue;

		std::string file = filePath;
		file += "/basic.ini";

		ConfigFile config;
		int ret = config.Open(file.c_str(), CONFIG_OPEN_EXISTING);
		if (ret != CONFIG_SUCCESS)
			continue;

		const char* name = config_get_string(config, "General", "Name");
		if (!name)
			name = strrchr(filePath, '/') + 1;

		if (!cb(name, filePath))
			break;
	}

	os_globfree(glob);
}

static bool GetProfileDir(const char* findName, const char*& profileDir) {
	bool found = false;
	auto func = [&](const char* name, const char* path) {
		if (strcmp(name, findName) == 0) {
			found = true;
			profileDir = strrchr(path, '/') + 1;
			return false;
		}
		return true;
	};

	EnumProfiles(func);
	return found;
}

static bool ProfileExists(const char* findName) {
	const char* profileDir = nullptr;
	return GetProfileDir(findName, profileDir);
}

static bool FindSafeProfileDirName(const std::string& profileName, std::string& dirName) {
	char path[512];
	int ret;

	if (ProfileExists(profileName.c_str())) {
		blog(LOG_WARNING, "Profile '%s' exists", profileName.c_str());
		return false;
	}

	if (!GetFileSafeName(profileName.c_str(), dirName)) {
		blog(LOG_WARNING, "Failed to create safe file name for '%s'", profileName.c_str());
		return false;
	}

	ret = GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return false;
	}

	dirName.insert(0, path);

	if (!GetClosestUnusedFileName(dirName, nullptr)) {
		blog(LOG_WARNING, "Failed to get closest file name for %s", dirName.c_str());
		return false;
	}

	dirName.erase(0, ret);
	return true;
}

static bool CopyProfile(const char* fromPartial, const char* to) {
	os_glob_t* glob;
	char path[514];
	char dir[512];
	int ret;

	ret = GetConfigPath(dir, sizeof(dir), "obs-studio/basic/profiles/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return false;
	}

	snprintf(path, sizeof(path), "%s%s/*", dir, fromPartial);

	if (os_glob(path, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob profile '%s'", fromPartial);
		return false;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char* filePath = glob->gl_pathv[i].path;
		if (glob->gl_pathv[i].directory)
			continue;

		ret = snprintf(path, sizeof(path), "%s/%s", to, strrchr(filePath, '/') + 1);
		if (ret > 0) {
			if (os_copyfile(filePath, path) != 0) {
				blog(LOG_WARNING,
				     "CopyProfile: Failed to "
				     "copy file %s to %s",
				     filePath, path);
			}
		}
	}

	os_globfree(glob);

	return true;
}

static bool ProfileNeedsRestart(config_t* newConfig, std::string& settings) {
	const char* oldSpeakers =
	  config_get_string(CoreApp->GetBasicConfig(), "Audio", "ChannelSetup");
	uint64_t oldSampleRate = config_get_uint(CoreApp->GetBasicConfig(), "Audio", "SampleRate");

	const char* newSpeakers = config_get_string(newConfig, "Audio", "ChannelSetup");
	uint64_t newSampleRate = config_get_uint(newConfig, "Audio", "SampleRate");

	auto appendSetting = [&settings](const char* name) {
		settings += std::string("\n") + std::string(name);
	};

	bool result = false;
	if (oldSpeakers != NULL && newSpeakers != NULL) {
		result = strcmp(oldSpeakers, newSpeakers) != 0;
		appendSetting("Basic.Settings.Audio.Channels");
	}
	if (oldSampleRate != 0 && newSampleRate != 0) {
		result |= oldSampleRate != newSampleRate;
		appendSetting("Basic.Settings.Audio.SampleRate");
	}

	return result;
}

bool App::AddProfile(bool create_new, const char* title, const char* text, const char* init_text,
		     bool rename) {
	std::string name;
	bool showWizardChecked = config_get_bool(GetGlobalConfig(), "Basic", "ConfigOnNewProfile");
	return CreateProfile(name, create_new, showWizardChecked, rename);
}

bool App::CreateProfile(const std::string& newName, bool createNew, bool showWizardChecked,
			bool rename) {
	std::string newDir;
	std::string newPath;
	ConfigFile config;

	if (!FindSafeProfileDirName(newName, newDir))
		return false;

	if (createNew) {
		config_set_bool(GetGlobalConfig(), "Basic", "ConfigOnNewProfile", showWizardChecked);
	}

	std::string curDir = config_get_string(GetGlobalConfig(), "Basic", "ProfileDir");

	char baseDir[512];
	int ret = GetConfigPath(baseDir, sizeof(baseDir), "obs-studio/basic/profiles/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return false;
	}

	newPath = baseDir;
	newPath += newDir;

	if (os_mkdir(newPath.c_str()) < 0) {
		blog(LOG_WARNING, "Failed to create profile directory '%s'", newDir.c_str());
		return false;
	}

	if (!createNew)
		CopyProfile(curDir.c_str(), newPath.c_str());

	newPath += "/basic.ini";

	if (config.Open(newPath.c_str(), CONFIG_OPEN_ALWAYS) != 0) {
		blog(LOG_ERROR, "Failed to open new config file '%s'", newDir.c_str());
		return false;
	}

	if (api && !rename)
		api->on_event(OBS_FRONTEND_EVENT_PROFILE_CHANGING);

	config_set_string(GetGlobalConfig(), "Basic", "Profile", newName.c_str());
	config_set_string(GetGlobalConfig(), "Basic", "ProfileDir", newDir.c_str());

	config_set_string(config, "General", "Name", newName.c_str());
	basicConfig.SaveSafe("tmp");
	config.SaveSafe("tmp");
	config.Swap(basicConfig);
	InitBasicConfigDefaults();
	InitBasicConfigDefaults2();
	RefreshProfiles();

	if (createNew)
		ResetProfileData();

	blog(LOG_INFO, "Created profile '%s' (%s, %s)", newName.c_str(),
	     createNew ? "clean" : "duplicate", newDir.c_str());
	blog(LOG_INFO, "------------------------------------------------");

	config_save_safe(GetGlobalConfig(), "tmp", nullptr);

	if (api && !rename) {
		api->on_event(OBS_FRONTEND_EVENT_PROFILE_LIST_CHANGED);
		api->on_event(OBS_FRONTEND_EVENT_PROFILE_CHANGED);
	}
	return true;
}

bool App::NewProfile(const std::string& name) {
	return CreateProfile(name, true, false, false);
}

bool App::DuplicateProfile(const std::string& name) {
	return CreateProfile(name, false, false, false);
}

void App::DeleteProfile(const char* profileName, const char* profileDir) {
	char profilePath[512];
	char basePath[512];

	int ret = GetConfigPath(basePath, sizeof(basePath), "obs-studio/basic/profiles");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return;
	}

	ret = snprintf(profilePath, sizeof(profilePath), "%s/%s/*", basePath, profileDir);
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get path for profile dir '%s'", profileDir);
		return;
	}

	os_glob_t* glob;
	if (os_glob(profilePath, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob profile dir '%s'", profileDir);
		return;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char* filePath = glob->gl_pathv[i].path;

		if (glob->gl_pathv[i].directory)
			continue;

		os_unlink(filePath);
	}

	os_globfree(glob);

	ret = snprintf(profilePath, sizeof(profilePath), "%s/%s", basePath, profileDir);
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get path for profile dir '%s'", profileDir);
		return;
	}

	os_rmdir(profilePath);

	blog(LOG_INFO, "------------------------------------------------");
	blog(LOG_INFO, "Removed profile '%s' (%s)", profileName, profileDir);
	blog(LOG_INFO, "------------------------------------------------");
}

void App::DeleteProfile(const std::string& name) {
	const char* curName = config_get_string(GetGlobalConfig(), "Basic", "Profile");

	const char* profileDir = nullptr;
	if (!GetProfileDir(name.c_str(), profileDir)) {
		blog(LOG_WARNING, "Profile '%s' not found", name.c_str());
		return;
	}

	if (!profileDir) {
		blog(LOG_WARNING, "Failed to get profile dir for profile '%s'", name.c_str());
		return;
	}

	DeleteProfile(name.c_str(), profileDir);
	RefreshProfiles();
	config_save_safe(GetGlobalConfig(), "tmp", nullptr);
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_PROFILE_LIST_CHANGED);
}

void App::RefreshProfiles() {
	const char* curName = config_get_string(GetGlobalConfig(), "Basic", "Profile");

	auto addProfile = [&](const char* name, const char* path) {
		std::string file = strrchr(path, '/') + 1;
		blog(LOG_INFO, "profile: %s", file.c_str());

		return true;
	};

	EnumProfiles(addProfile);
}

void App::ResetProfileData() {
	ResetVideo();
	service = nullptr;
	InitService();
	ResetOutputs();

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
}

} // namespace core
