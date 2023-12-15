#pragma once

#include <string>
#include <vector>

#include <obs.hpp>
#include <util/util.hpp>
#include <util/platform.h>
#include <util/lexer.h>
#include <util/threading.h>

#include "../src/platform.hpp"

////////////////////////////////////////////////////////////////////////////////

namespace core {

bool get_token(lexer* lex, std::string& str, base_token_type type);
bool expect_token(lexer* lex, const char* str, base_token_type type);
uint64_t convert_log_name(bool has_prefix, const char* name);

bool MakeUserDirs();
bool MakeUserProfileDirs();
std::string GetSceneCollectionFileFromName(const char* name);
std::string GetProfileDirFromName(const char* name);

std::string CurrentTimeString();
std::string CurrentDateTimeString();
std::string GenerateTimeDateFilename(const char* extension, bool noSpace = false);
std::string GenerateSpecifiedFilename(const char* extension, bool noSpace, const char* format);
std::string GetFormatString(const char* format, const char* prefix, const char* suffix);
std::string GetFormatExt(const char* container);
std::string GetOutputFilename(const char* path, const char* container, bool noSpace, bool overwrite,
			      const char* format);

int GetConfigPath(char* path, size_t size, const char* name);
char* GetConfigPathPtr(const char* name);

int GetProgramDataPath(char* path, size_t size, const char* name);
char* GetProgramDataPathPtr(const char* name);

int GetProfilePath(char* path, size_t size, const char* file, const char* profile);

bool GetFileSafeName(const char* name, std::string& file);
bool GetClosestUnusedFileName(std::string& path, const char* extension);
bool GetUnusedSceneCollectionFile(std::string& name, std::string& file);

std::vector<std::pair<std::string, std::string>> GetLocaleNames();

bool EncoderAvailable(const char* encoder);

enum obs_scale_type GetScaleType(ConfigFile& basicConfig);
enum video_format GetVideoFormatFromName(const char* name);
enum video_colorspace GetVideoColorSpaceFromName(const char* name);

void SetSafeModuleNames();
void AddExtraModulePaths();
bool HasAudioDevices(const char* source_id);

void LogFilter(obs_source_t*, obs_source_t* filter, void* v_val);
bool LogSceneItem(obs_scene_t*, obs_sceneitem_t* item, void* v_val);
void LoadAudioDevice(const char* name, int channel, obs_data_t* parent);
void AddMissingFiles(void* data, obs_source_t* source);

const char* InputAudioSource();
const char* OutputAudioSource();

char* get_new_source_name(const char* name, const char* format);
} // namespace core
