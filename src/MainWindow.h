#pragma once

#include <QBuffer>
#include <QAction>
#include <QThread>
#include <QListWidgetItem>
#include <QModelRoleData>

#include "ui_MainWindow.h"
#include "window-main.hpp"
#include "obs-source-manager.h"

#include <memory>
#include <vector>

#include <obs.hpp>
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
class SourceDuplicatorWindow;

enum class QtDataRole {
	OBSRef = Qt::UserRole,
	OBSSignals,
};

namespace {

template<typename OBSRef> struct SignalContainer {
	OBSRef ref;
	std::vector<std::shared_ptr<OBSSignal>> handlers;
};
} // namespace

class MainWindow : public OBSMainWindow {
	Q_OBJECT

	friend class OBSBasicPreview;
	friend struct OBSStudioAPI;
	friend struct BasicOutputHandler;

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

  void ResetAudioDevice(const char* sourceId, const char* deviceId, const char* deviceDesc,
    int channel);
	
	float GetDevicePixelRatio();
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

	QColor GetSelectionColor() const;
	QColor GetCropColor() const;
	QColor GetHoverColor() const;

	void SetDisplayAffinity(QWindow* window);

	/// public inlines
	inline bool IsPreviewProgramMode() const {
		return os_atomic_load_bool(&previewProgramMode);
	}
	inline OBSSource GetCurrentSceneSource() {
		OBSScene curScene = GetCurrentScene();
		return OBSSource(obs_scene_get_source(curScene));
	}
	inline bool SavingDisabled() const { return disableSaving; }

protected:
  virtual bool nativeEvent(const QByteArray& eventType, void* message,
    qintptr* result) override;
  virtual void changeEvent(QEvent* event) override;

public slots:
  void SaveProject();
  void SaveProjectDeferred();
  void DeferSaveBegin();
  void DeferSaveEnd();
	void SetCurrentScene(OBSSource scene, bool force = false);

  // recording
  void StartRecording();
  void StopRecording();

  void RecordingStart();
  void RecordStopping();
  void RecordingStop(int code, QString last_error);
  void RecordingFileChanged(QString lastRecordingPath);

private slots:
	void on_actionNewProfile_triggered();
	void on_actionDupProfile_triggered();
	void on_actionRenameProfile_triggered();
	void on_actionRemoveProfile_triggered(bool skipConfirmation = false);
	void on_actionImportProfile_triggered();
	void on_actionExportProfile_triggered();

	void on_scenes_currentItemChanged(QListWidgetItem* current, QListWidgetItem* prev);

	void AddSceneItem(OBSSceneItem item);
	void AddScene(OBSSource source);
	void RemoveScene(OBSSource source);

	void DuplicateSelectedScene();
  void EnablePreviewDisplay(bool enable);

private:
	// UI member
	Ui::MainWindowClass* ui;
	OBSDataAutoRelease safeModeModuleData;
	std::vector<OBSSignal> signalHandlers;
	std::vector<QListWidgetItem*> scenes;
	std::vector<OBSSceneItem> sources;
  std::vector<SourceDuplicatorWindow*> duplicators;

  // source manager
  recorder::manager::OBSSourceManager* manager;
  
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
  int disableOutputsRef = 0;

  bool streamingStopping = false;
  bool recordingStopping = false;

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

  // output & services
	std::unique_ptr<BasicOutputHandler> outputHandler;
	OBSService service;

  // transitions
  obs_source_t* fadeTransition;
  obs_source_t* cutTransition;

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

	void LoadSceneListOrder(obs_data_array_t* array);
	obs_data_array_t* SaveSceneListOrder();
	void LogScenes();

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

  void ConfigureUI();

  // transitions
  void InitDefaultTransitions();
  void InitTransition(obs_source_t* transition);
  void SetTransition(OBSSource transition);
  void TransitionToScene(OBSSource source);

  // recording
  bool OutputPathValid();
  void AutoRemux(QString input, bool no_show = false);

  void UpdatePause(bool activate = true);
};
