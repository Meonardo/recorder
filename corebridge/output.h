#pragma once

#include <string>
#include <memory>

#include <obs.hpp>

namespace core {

class OutputCallback {
public:
	virtual ~OutputCallback() {}

	// streaming
	virtual void OnStreamDelayStarting(int seconds) = 0;
	virtual void OnStreamDelayStopping(int seconds) = 0;
	virtual void OnStreamStarted() = 0;
	virtual void OnStreamStopped(std::string error, int code) = 0;

	// recording
	virtual void OnRecordingStarted() = 0;
	virtual void OnRecordingStopping() = 0;
	virtual void OnRecordingStopped(std::string error, int code) = 0;
	virtual void OnRecordingFileChanged(std::string path) = 0;

	// replay
	virtual void OnReplayBufferStarted() = 0;
	virtual void OnReplayBufferStopping() = 0;
	virtual void OnReplayBufferStopped(std::string error, int code) = 0;
	virtual void OnReplayBufferSaved() = 0;

	// virtual camera
	virtual void OnVirtualCamStarted() = 0;
	virtual void OnVirtualCamDeactivated() = 0;
	virtual void OnVirtualCamStopped(std::string error, int code) = 0;
};

struct BasicOutputHandler {
	OBSOutputAutoRelease fileOutput;
	OBSOutputAutoRelease streamOutput;
	OBSOutputAutoRelease replayBuffer;
	OBSOutputAutoRelease virtualCam;
	bool streamingActive = false;
	bool recordingActive = false;
	bool delayActive = false;
	bool replayBufferActive = false;
	bool virtualCamActive = false;
	OutputCallback* callback = nullptr;

	obs_view_t* virtualCamView = nullptr;
	video_t* virtualCamVideo = nullptr;
	obs_scene_t* vCamSourceScene = nullptr;
	obs_sceneitem_t* vCamSourceSceneItem = nullptr;

	std::string outputType;
	std::string lastError;

	std::string lastRecordingPath;

	OBSSignal startRecording;
	OBSSignal stopRecording;
	OBSSignal startReplayBuffer;
	OBSSignal stopReplayBuffer;
	OBSSignal startStreaming;
	OBSSignal stopStreaming;
	OBSSignal startVirtualCam;
	OBSSignal stopVirtualCam;
	OBSSignal deactivateVirtualCam;
	OBSSignal streamDelayStarting;
	OBSSignal streamStopping;
	OBSSignal recordStopping;
	OBSSignal recordFileChanged;
	OBSSignal replayBufferStopping;
	OBSSignal replayBufferSaved;

	inline BasicOutputHandler(OutputCallback* callback);

	virtual ~BasicOutputHandler(){};

	virtual bool SetupStreaming(obs_service_t* service) = 0;
	virtual bool StartStreaming(obs_service_t* service) = 0;
	virtual bool StartRecording() = 0;
	virtual bool StartReplayBuffer() { return false; }
	virtual bool StartVirtualCam();
	virtual void StopStreaming(bool force = false) = 0;
	virtual void StopRecording(bool force = false) = 0;
	virtual void StopReplayBuffer(bool force = false) { (void)force; }
	virtual void StopVirtualCam();
	virtual bool StreamingActive() const = 0;
	virtual bool RecordingActive() const = 0;
	virtual bool ReplayBufferActive() const { return false; }
	virtual bool VirtualCamActive() const;

	virtual void Update() = 0;
	virtual void SetupOutputs() = 0;

	virtual void UpdateVirtualCamOutputSource();
	virtual void DestroyVirtualCamView();
	virtual void DestroyVirtualCameraScene();

	inline bool Active() const {
		return streamingActive || recordingActive || delayActive || replayBufferActive ||
		       virtualCamActive;
	}

protected:
	void SetupAutoRemux(const char*& container);
	std::string GetRecordingFilename(const char* path, const char* container, bool noSpace,
					 bool overwrite, const char* format, bool ffmpeg);
};

BasicOutputHandler* CreateSimpleOutputHandler(OutputCallback* callback);
BasicOutputHandler* CreateAdvancedOutputHandler(OutputCallback* callback);

class OutputManager : public OutputCallback {
public:
	OutputManager();
	~OutputManager();

  void SetOutputHandler(std::unique_ptr<BasicOutputHandler> handler);
  void Update();
  bool Active();

  EXPORT void SetStreamAddress(const std::string& addr, const std::string& username,
			      const std::string& passwd);
  EXPORT void GetSteamAddress(const std::string& address, const std::string& username,
			     const std::string& passwd);

  EXPORT void StartStreaming();
  EXPORT void StopStreaming();

  EXPORT void StartRecording();
  EXPORT void StopRecording();

	void StartReplayBuffer();
	void StopReplayBuffer();

  EXPORT void StartVirtualCam();
  EXPORT void StopVirtualCam();

  EXPORT void SetCurrentRecordingFolder(const char* path);
    
	void OnStreamDelayStarting(int seconds) override;
	void OnStreamDelayStopping(int seconds) override;
	void OnStreamStarted() override;
	void OnStreamStopped(std::string error, int code) override;

	void OnRecordingStarted() override;
	void OnRecordingStopping() override;
	void OnRecordingStopped(std::string error, int code) override;
	void OnRecordingFileChanged(std::string path) override;

	void OnReplayBufferStarted() override;
	void OnReplayBufferStopping() override;
	void OnReplayBufferStopped(std::string error, int code) override;
	void OnReplayBufferSaved() override;

	void OnVirtualCamStarted() override;
	void OnVirtualCamDeactivated() override;
	void OnVirtualCamStopped(std::string error, int code) override;

private:
  std::unique_ptr<BasicOutputHandler> outputHandler;
};

} // namespace core
