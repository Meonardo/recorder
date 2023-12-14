#include "ui.h"

#include <util/profiler.hpp>

namespace core {

UIWindow::UIWindow() {}
UIWindow::~UIWindow() {}

SourceSignalHandler::SourceSignalHandler() {
  ProfileScope("MainWindow::InitOBSCallbacks");

	signalHandlers.reserve(signalHandlers.size() + 7);

	signalHandlers.emplace_back(obs_get_signal_handler(), "source_create",
				    SourceSignalHandler::SourceCreated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_remove",
				    SourceSignalHandler::SourceRemoved, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_activate",
				    SourceSignalHandler::SourceActivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_deactivate",
				    SourceSignalHandler::SourceDeactivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_audio_activate",
				    SourceSignalHandler::SourceAudioActivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_audio_deactivate",
				    SourceSignalHandler::SourceAudioDeactivated, this);
	signalHandlers.emplace_back(obs_get_signal_handler(), "source_rename",
				    SourceSignalHandler::SourceRenamed, this); 
}
SourceSignalHandler::~SourceSignalHandler() {}

void SourceSignalHandler::SourceCreated(void* data, calldata_t* params) {}
void SourceSignalHandler::SourceRemoved(void* data, calldata_t* params) {}
void SourceSignalHandler::SourceActivated(void* data, calldata_t* params) {}
void SourceSignalHandler::SourceDeactivated(void* data, calldata_t* params) {}
void SourceSignalHandler::SourceAudioDeactivated(void* data, calldata_t* params) {}
void SourceSignalHandler::SourceAudioActivated(void* data, calldata_t* params) {}
void SourceSignalHandler::SourceRenamed(void* data, calldata_t* params) {}

} // namespace core
