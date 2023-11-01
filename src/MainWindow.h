#pragma once

#include <QMainWindow>
#include <QBuffer>
#include <QAction>
#include <QThread>
#include <QWidgetAction>

#include "ui_MainWindow.h"

#include <memory>
#include <vector>

#include <obs.hpp>
#include <util/config-file.h>
#include <util/platform.h>
#include <util/util.hpp>
#include <util/threading.h>

#include <obs-frontend-internal.hpp>

#define DESKTOP_AUDIO_1 Str("DesktopAudioDevice1")
#define DESKTOP_AUDIO_2 Str("DesktopAudioDevice2")
#define AUX_AUDIO_1 Str("AuxAudioDevice1")
#define AUX_AUDIO_2 Str("AuxAudioDevice2")
#define AUX_AUDIO_3 Str("AuxAudioDevice3")
#define AUX_AUDIO_4 Str("AuxAudioDevice4")

#define SIMPLE_ENCODER_X264 "x264"
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_QSV "qsv"
#define SIMPLE_ENCODER_QSV_AV1 "qsv_av1"
#define SIMPLE_ENCODER_NVENC "nvenc"
#define SIMPLE_ENCODER_NVENC_AV1 "nvenc_av1"
#define SIMPLE_ENCODER_NVENC_HEVC "nvenc_hevc"
#define SIMPLE_ENCODER_AMD "amd"
#define SIMPLE_ENCODER_AMD_HEVC "amd_hevc"
#define SIMPLE_ENCODER_AMD_AV1 "amd_av1"
#define SIMPLE_ENCODER_APPLE_H264 "apple_h264"
#define SIMPLE_ENCODER_APPLE_HEVC "apple_hevc"

#define PREVIEW_EDGE_SIZE 10

struct BasicOutputHandler;
class QListWidgetItem;

class OBSMainWindow : public QMainWindow {
	Q_OBJECT

public:
	inline OBSMainWindow(QWidget* parent) : QMainWindow(parent) {}

	virtual config_t* Config() const = 0;
	virtual void OBSInit() = 0;

	virtual int GetProfilePath(char* path, size_t size, const char* file) const = 0;
};

class MainWindow : public OBSMainWindow {
	Q_OBJECT

	friend class OBSBasicPreview;
	friend struct OBSStudioAPI;

public:
	MainWindow(QWidget* parent = nullptr);
	~MainWindow();

	// Public instance
	static MainWindow* Get();
  bool Active() const;
	virtual config_t* Config() const override;
	virtual void OBSInit() override;
	virtual int GetProfilePath(char* path, size_t size, const char* file) const override;

	/// Public APIs
	OBSSource GetProgramSource();
	OBSScene GetCurrentScene();

  void ResetAudioDevice(const char* sourceId, const char* deviceId,
    const char* deviceDesc, int channel);

	void SaveProjectDeferred();
	void SaveProject();

	const char* GetCurrentOutputPath();

	obs_service_t* GetService();
	void SetService(obs_service_t* service);
	void SaveService();
	bool LoadService();

	int ResetVideo();
	bool ResetAudio();
	void ResetOutputs();

	bool InitService();

	bool InitBasicConfigDefaults();
	void InitBasicConfigDefaults2();
	bool InitBasicConfig();

	void InitOBSCallbacks();

	/// public inlines
	inline bool IsPreviewProgramMode() const {
		return os_atomic_load_bool(&previewProgramMode);
	}
	inline OBSSource GetCurrentSceneSource() {
		OBSScene curScene = GetCurrentScene();
		return OBSSource(obs_scene_get_source(curScene));
	}
	inline bool SavingDisabled() const { return disableSaving; }

public slots:
	void SetCurrentScene(OBSSource scene, bool force = false);

private slots:
	void on_actionNewProfile_triggered();
	void on_actionDupProfile_triggered();
	void on_actionRenameProfile_triggered();
	void on_actionRemoveProfile_triggered(bool skipConfirmation = false);
	void on_actionImportProfile_triggered();
	void on_actionExportProfile_triggered();

private:
	// UI member
	Ui::MainWindowClass* ui;
	OBSDataAutoRelease safeModeModuleData;
	std::vector<OBSSignal> signalHandlers;
	// configures
  float dpi = 1.0;
	ConfigFile basicConfig;
	volatile bool previewProgramMode = false;
	bool loaded = false;
	long disableSaving = 1;
	bool projectChanged = false;
	bool previewEnabled = true;
	bool closing = false;
	bool clearingFailed = false;
	bool drawSafeAreas = false;
  bool drawSpacingHelpers = true;

	// preview
	gs_vertbuffer_t* box = nullptr;
	gs_vertbuffer_t* boxLeft = nullptr;
	gs_vertbuffer_t* boxTop = nullptr;
	gs_vertbuffer_t* boxRight = nullptr;
	gs_vertbuffer_t* boxBottom = nullptr;
	gs_vertbuffer_t* circle = nullptr;

	gs_vertbuffer_t* actionSafeMargin = nullptr;
	gs_vertbuffer_t* graphicsSafeMargin = nullptr;
	gs_vertbuffer_t* fourByThreeSafeMargin = nullptr;
	gs_vertbuffer_t* leftLine = nullptr;
	gs_vertbuffer_t* topLine = nullptr;
	gs_vertbuffer_t* rightLine = nullptr;

	int previewX = 0, previewY = 0;
	int previewCX = 0, previewCY = 0;
	float previewScale = 0.0f;

	// thread
	QScopedPointer<QThread> devicePropertiesThread;

	// frontend api
	obs_frontend_callbacks* api = nullptr;

	// scenes & sources
	OBSWeakSource lastScene;
	OBSWeakSource swapScene;
	OBSWeakSource programScene;
	OBSWeakSource lastProgramScene;

	std::atomic<obs_scene_t*> currentScene = nullptr;
	std::unique_ptr<BasicOutputHandler> outputHandler;
	OBSService service;

  void OnFirstLoad();
	void SaveProjectNow();

	// profiles
	void ResetProfileData();
	bool AddProfile(bool create_new, const char* title, const char* text,
			const char* init_text = nullptr, bool rename = false);
	bool CreateProfile(const std::string& newName, bool create_new, bool showWizardChecked,
			   bool rename = false);
	void DeleteProfile(const char* profile_name, const char* profile_dir);
	void RefreshProfiles();
	void ChangeProfile();

	bool NewProfile(const QString& name);
	bool DuplicateProfile(const QString& name);
	void DeleteProfile(const QString& profileName);

	void CheckForSimpleModeX264Fallback();

	void Save(const char* file);
	void LoadData(obs_data_t* data, const char* file);
	void Load(const char* file);

	void DeferSaveBegin();
	void DeferSaveEnd();

	void LoadSceneListOrder(obs_data_array_t* array);
	obs_data_array_t* SaveSceneListOrder();

	void CreateFirstRunSources();
	void CreateDefaultScene(bool firstStart);
	void ClearSceneData();

	void GetFPSCommon(uint32_t& num, uint32_t& den) const;
	void GetFPSInteger(uint32_t& num, uint32_t& den) const;
	void GetFPSFraction(uint32_t& num, uint32_t& den) const;
	void GetFPSNanoseconds(uint32_t& num, uint32_t& den) const;
	void GetConfigFPS(uint32_t& num, uint32_t& den) const;

	/* OBS Callbacks */
	static void SceneReordered(void* data, calldata_t* params);
	static void SceneRefreshed(void* data, calldata_t* params);
	static void SceneItemAdded(void* data, calldata_t* params);
	static void SourceCreated(void* data, calldata_t* params);
	static void SourceRemoved(void* data, calldata_t* params);
	static void SourceActivated(void* data, calldata_t* params);
	static void SourceDeactivated(void* data, calldata_t* params);
	static void SourceAudioActivated(void* data, calldata_t* params);
	static void SourceAudioDeactivated(void* data, calldata_t* params);
	static void SourceRenamed(void* data, calldata_t* params);
	static void RenderMain(void* data, uint32_t cx, uint32_t cy);

	void DrawBackdrop(float cx, float cy);
	void InitPrimitives();

	void UpdatePreviewSafeAreas();
  void UpdatePreviewSpacingHelpers();
  void UpdatePreviewOverflowSettings();

  void SetCurrentScene(obs_scene_t* scene, bool force = false);
  void ResizePreview(uint32_t cx, uint32_t cy);
};
