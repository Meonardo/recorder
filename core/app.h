#pragma once

#include <atomic>

#include "utils.h"
#include "ui.h"
#include "scene-source.h"

#define VERSION "0.0.1"

extern "C" void install_dll_blocklist_hook(void);
extern "C" void log_blocked_dlls(void);

namespace core {
struct LaunchOptions {
	bool portable_mode = false;
	bool steam = false;
	bool safe_mode = false;
	bool disable_3p_plugins = false;
	bool unclean_shutdown = false;
	bool disable_shutdown_check = false;
	bool multi = false;
	bool log_verbose = false;
	bool unfiltered_log = false;
	bool opt_start_streaming = false;
	bool opt_start_recording = false;
	bool opt_studio_mode = false;
	bool opt_start_replaybuffer = false;
	bool opt_start_virtualcam = false;
	bool opt_minimize_tray = false;
	bool opt_allow_opengl = false;
	bool opt_always_on_top = false;
	bool opt_disable_updater = false;
	bool opt_disable_missing_files_check = false;
	std::string opt_starting_collection;
	std::string opt_starting_profile;
	std::string opt_starting_scene;
};

/// forward declares
struct BasicOutputHandler;
class OutputManager;

class App {
	friend class SceneSourceManager;

public:
	static App* Get() {
		static App g_app_;
		return &g_app_;
	}

	int Run(int argc, char* argv[], UIApplication* application);
	void Quit();
	~App();

	inline config_t* GetGlobalConfig() const { return globalConfig; }
	inline const char* GetLocale() const { return locale.c_str(); }
	inline const char* GetString(const char* lookupVal) const {
		return textLookup.GetString(lookupVal);
	}
	inline ConfigFile& GetBasicConfig() { return basicConfig; }
	inline bool IsPreviewProgramMode() const {
		return os_atomic_load_bool(&previewProgramMode);
	}
	inline bool IsPreviewEnabled() const { return previewEnabled; }

	profiler_name_store_t* GetProfilerNameStore() const { return profilerNameStore; }

	OutputManager* GetOutputManager() const { return outputManager.get(); }

	bool IsVcamEnabled() const { return vcamEnabled; }

	obs_service_t* GetService();

	void SaveProject();
	void SaveProjectDeferred();
	void DeferSaveBegin();
	void DeferSaveEnd();
	void SetCurrentScene(OBSSource scene, bool force = false);

	OBSSource GetProgramSource();
	OBSScene GetCurrentScene();

	// audio & video
	int ResetVideo(int width = 1920, int height = 1080);
	bool ResetAudio();
	void ResetOutputs();
	void ClearSceneData();

	void* GetApplication() { return application; }

	App(const App&) = delete;
	App& operator=(const App&) = delete;

protected:
	App();

private:
	profiler_name_store_t* profilerNameStore = nullptr;
	os_inhibit_t* sleepInhibitor = nullptr;
	UIApplication* application = nullptr;

	std::unique_ptr<OutputManager> outputManager = nullptr;
	std::unique_ptr<SceneSourceManager> sceneSourceManager = nullptr;

	volatile bool previewProgramMode = false;
	bool libobs_initialized = false;
	bool loaded = false;
	long disableSaving = 1;
	bool projectChanged = false;
	bool previewEnabled = true;
	bool closing = false;
	bool clearingFailed = false;
	bool vcamEnabled = false;

	// configs & profiles
	std::string locale;
	ConfigFile globalConfig;
	TextLookup textLookup;
	LaunchOptions launchOptions;
	ConfigFile basicConfig;

	// frontend api
	obs_frontend_callbacks* api = nullptr;

	// scenes & sources
	OBSWeakSource lastScene;
	OBSWeakSource swapScene;
	OBSWeakSource programScene;
	OBSWeakSource lastProgramScene;

	std::atomic<obs_scene_t*> currentScene = nullptr;

	// output & services
	OBSService service;

	// transitions
	obs_source_t* fadeTransition;
	obs_source_t* cutTransition;

	// entry functions
	int RunMain(std::fstream& logFile, int argc, char* argv[]);
	void AppInit();
	bool OBSInit();

	// configs & profiles
	bool InitGlobalConfig();
	bool InitGlobalConfigDefaults();
	bool InitLocale();

	bool InitBasicConfig();
	bool InitBasicConfigDefaults();
	void InitBasicConfigDefaults2();
	int GetProfilePath(char* path, size_t size, const char* file) const;
	void ResetProfileData();
	bool AddProfile(bool createNew, const char* title, const char* text,
			const char* initText = nullptr, bool rename = false);
	bool CreateProfile(const std::string& newName, bool createNew, bool showWizardChecked,
			   bool rename = false);
	void DeleteProfile(const char* profileName, const char* profileDir);
	void RefreshProfiles();
	bool NewProfile(const std::string& name);
	bool DuplicateProfile(const std::string& name);
	void DeleteProfile(const std::string& profileName);

	// audio & video
	void GetFPSCommon(uint32_t& num, uint32_t& den) const;
	void GetFPSInteger(uint32_t& num, uint32_t& den) const;
	void GetFPSFraction(uint32_t& num, uint32_t& den) const;
	void GetFPSNanoseconds(uint32_t& num, uint32_t& den) const;
	void GetConfigFPS(uint32_t& num, uint32_t& den) const;

	const char* GetRenderModule() const;

	// service
	void SetService(obs_service_t* service);
	void SaveService();
	bool LoadService();
	bool InitService();

	void CheckForSimpleModeX264Fallback();

	// transitions
	void InitDefaultTransitions();
	void InitTransition(obs_source_t* transition);
	void SetTransition(OBSSource transition);
	void TransitionToScene(OBSSource source);

	void Save(const char* file);
	void LoadData(obs_data_t* data, const char* file);
	void Load(const char* file);

	void CreateFirstRunSources();
	void CreateDefaultScene(bool firstStart);

	void SetCurrentScene(obs_scene_t* scene, bool force = false);
	void ResetAudioDevice(const char* sourceId, const char* deviceId, const char* deviceDesc,
			      int channel);

	void SaveProjectNow();
};
} // namespace core

#define CoreApp core::App::Get()

inline const char* Str(const char* lookup) {
	return CoreApp->GetString(lookup);
}
