#pragma once

#include <string>
#include <vector>

#include <obs.hpp>
#include <util/util.hpp>
#include <util/platform.h>
#include <util/lexer.h>

#include "../src/platform.hpp"

////////////////////////////////////////////////////////////////////////////////
// static methods
static bool get_token(lexer* lex, std::string& str, base_token_type type);
static bool expect_token(lexer* lex, const char* str, base_token_type type);
static uint64_t convert_log_name(bool has_prefix, const char* name);

static bool MakeUserDirs();
static bool MakeUserProfileDirs();
static std::string GetSceneCollectionFileFromName(const char* name);
static std::string GetProfileDirFromName(const char* name);

static std::string CurrentTimeString();
static std::string CurrentDateTimeString();
static std::string GenerateTimeDateFilename(const char* extension, bool noSpace = false);
static std::string GenerateSpecifiedFilename(const char* extension, bool noSpace,
					     const char* format);
static std::string GetFormatString(const char* format, const char* prefix, const char* suffix);
static std::string GetFormatExt(const char* container);
static std::string GetOutputFilename(const char* path, const char* container, bool noSpace,
				     bool overwrite, const char* format);

static int GetConfigPath(char* path, size_t size, const char* name);
static char* GetConfigPathPtr(const char* name);

static int GetProgramDataPath(char* path, size_t size, const char* name);
static char* GetProgramDataPathPtr(const char* name);

static int GetProfilePath(char* path, size_t size, const char* file, const char* profile);

static bool GetFileSafeName(const char* name, std::string& file);
static bool GetClosestUnusedFileName(std::string& path, const char* extension);
static bool GetUnusedSceneCollectionFile(std::string& name, std::string& file);

static std::vector<std::pair<std::string, std::string>> GetLocaleNames();

static bool EncoderAvailable(const char* encoder);

static enum obs_scale_type GetScaleType(ConfigFile& basicConfig);
static enum video_format GetVideoFormatFromName(const char* name);
static enum video_colorspace GetVideoColorSpaceFromName(const char* name);

static void SetSafeModuleNames();
static void AddExtraModulePaths();
static bool HasAudioDevices(const char* source_id);

static void LogFilter(obs_source_t*, obs_source_t* filter, void* v_val);
static bool LogSceneItem(obs_scene_t*, obs_sceneitem_t* item, void* v_val);
static void LoadAudioDevice(const char* name, int channel, obs_data_t* parent);
static void AddMissingFiles(void* data, obs_source_t* source);

static const char* InputAudioSource();
static const char* OutputAudioSource();

static char* get_new_source_name(const char* name, const char* format);
