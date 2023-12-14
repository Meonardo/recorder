#include "utils.h"

#include <sstream>
#include <mutex>
#include <fstream>
#include <chrono>
#include <ratio>
#include <unordered_set>
#include <atomic>
#include <memory>
#include <filesystem>

#include "defines.h"

namespace core {
struct BaseLexer {
	lexer lex;

public:
	inline BaseLexer() { lexer_init(&lex); }
	inline ~BaseLexer() { lexer_free(&lex); }
	operator lexer*() { return &lex; }
};

bool get_token(lexer* lex, std::string& str, base_token_type type) {
	base_token token;
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != type)
		return false;

	str.assign(token.text.array, token.text.len);
	return true;
}

bool expect_token(lexer* lex, const char* str, base_token_type type) {
	base_token token;
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != type)
		return false;

	return strref_cmp(&token.text, str) == 0;
}

uint64_t convert_log_name(bool has_prefix, const char* name) {
	core::BaseLexer lex;
	std::string year, month, day, hour, minute, second;

	lexer_start(lex, name);

	if (has_prefix) {
		std::string temp;
		if (!get_token(lex, temp, BASETOKEN_ALPHA))
			return 0;
	}

	if (!get_token(lex, year, BASETOKEN_DIGIT))
		return 0;
	if (!expect_token(lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!get_token(lex, month, BASETOKEN_DIGIT))
		return 0;
	if (!expect_token(lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!get_token(lex, day, BASETOKEN_DIGIT))
		return 0;
	if (!get_token(lex, hour, BASETOKEN_DIGIT))
		return 0;
	if (!expect_token(lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!get_token(lex, minute, BASETOKEN_DIGIT))
		return 0;
	if (!expect_token(lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!get_token(lex, second, BASETOKEN_DIGIT))
		return 0;

	std::stringstream timestring;
	timestring << year << month << day << hour << minute << second;
	return std::stoull(timestring.str());
}

void get_last_log(bool has_prefix, const char* subdir_to_use, std::string& last) {
	BPtr<char> logDir(GetConfigPathPtr(subdir_to_use));
	struct os_dirent* entry;
	os_dir_t* dir = os_opendir(logDir);
	uint64_t highest_ts = 0;

	if (dir) {
		while ((entry = os_readdir(dir)) != NULL) {
			if (entry->directory || *entry->d_name == '.')
				continue;

			uint64_t ts = convert_log_name(has_prefix, entry->d_name);

			if (ts > highest_ts) {
				last = entry->d_name;
				highest_ts = ts;
			}
		}

		os_closedir(dir);
	}
}

void remove_reserved_file_characters(std::string& s) {
	replace(s.begin(), s.end(), '\\', '/');
	replace(s.begin(), s.end(), '*', '_');
	replace(s.begin(), s.end(), '?', '_');
	replace(s.begin(), s.end(), '"', '_');
	replace(s.begin(), s.end(), '|', '_');
	replace(s.begin(), s.end(), ':', '_');
	replace(s.begin(), s.end(), '>', '_');
	replace(s.begin(), s.end(), '<', '_');
}

void ensure_directory_exists(std::string& path) {
	replace(path.begin(), path.end(), '\\', '/');

	size_t last = path.rfind('/');
	if (last == std::string::npos)
		return;

	std::string directory = path.substr(0, last);
	os_mkdirs(directory.c_str());
}

void FindBestFilename(std::string& strPath, bool noSpace) {
	int num = 2;

	if (!os_file_exists(strPath.c_str()))
		return;

	const char* ext = strrchr(strPath.c_str(), '.');
	if (!ext)
		return;

	int extStart = int(ext - strPath.c_str());
	for (;;) {
		std::string testPath = strPath;
		std::string numStr;

		numStr = noSpace ? "_" : " (";
		numStr += std::to_string(num++);
		if (!noSpace)
			numStr += ")";

		testPath.insert(extStart, numStr);

		if (!os_file_exists(testPath.c_str())) {
			strPath = testPath;
			break;
		}
	}
}

bool do_mkdir(const char* path) {
	if (os_mkdirs(path) == MKDIR_ERROR) {
		blog(LOG_ERROR, "Failed to create directory %s", path);
		return false;
	}

	return true;
}

bool MakeUserDirs() {
	char path[512];

	if (GetConfigPath(path, sizeof(path), "obs-studio/basic") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;

	if (GetConfigPath(path, sizeof(path), "obs-studio/logs") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;

	if (GetConfigPath(path, sizeof(path), "obs-studio/profiler_data") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;

#ifdef _WIN32
	if (GetConfigPath(path, sizeof(path), "obs-studio/crashes") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;
#endif

#ifdef WHATSNEW_ENABLED
	if (GetConfigPath(path, sizeof(path), "obs-studio/updates") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;
#endif

	if (GetConfigPath(path, sizeof(path), "obs-studio/plugin_config") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;

	return true;
}

bool MakeUserProfileDirs() {
	char path[512];

	if (GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;

	if (GetConfigPath(path, sizeof(path), "obs-studio/basic/scenes") <= 0)
		return false;
	if (!do_mkdir(path))
		return false;

	return true;
}

std::string GetProfileDirFromName(const char* name) {
	std::string outputPath;
	os_glob_t* glob;
	char path[512];

	if (GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles") <= 0)
		return outputPath;

	strcat(path, "/*");

	if (os_glob(path, 0, &glob) != 0)
		return outputPath;

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		struct os_globent ent = glob->gl_pathv[i];
		if (!ent.directory)
			continue;

		strcpy(path, ent.path);
		strcat(path, "/basic.ini");

		ConfigFile config;
		if (config.Open(path, CONFIG_OPEN_EXISTING) != 0)
			continue;

		const char* curName = config_get_string(config, "General", "Name");
		if (astrcmpi(curName, name) == 0) {
			outputPath = ent.path;
			break;
		}
	}

	os_globfree(glob);

	if (!outputPath.empty()) {
		replace(outputPath.begin(), outputPath.end(), '\\', '/');
		const char* start = strrchr(outputPath.c_str(), '/');
		if (start)
			outputPath.erase(0, start - outputPath.c_str() + 1);
	}

	return outputPath;
}

std::string GetSceneCollectionFileFromName(const char* name) {
	std::string outputPath;
	os_glob_t* glob;
	char path[512];

	if (GetConfigPath(path, sizeof(path), "obs-studio/basic/scenes") <= 0)
		return outputPath;

	strcat(path, "/*.json");

	if (os_glob(path, 0, &glob) != 0)
		return outputPath;

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		struct os_globent ent = glob->gl_pathv[i];
		if (ent.directory)
			continue;

		OBSDataAutoRelease data = obs_data_create_from_json_file_safe(ent.path, "bak");
		const char* curName = obs_data_get_string(data, "name");

		if (astrcmpi(name, curName) == 0) {
			outputPath = ent.path;
			break;
		}
	}

	os_globfree(glob);

	if (!outputPath.empty()) {
		outputPath.resize(outputPath.size() - 5);
		replace(outputPath.begin(), outputPath.end(), '\\', '/');
		const char* start = strrchr(outputPath.c_str(), '/');
		if (start)
			outputPath.erase(0, start - outputPath.c_str() + 1);
	}

	return outputPath;
}

std::string CurrentTimeString() {
	using namespace std::chrono;

	struct tm tstruct;
	char buf[80];

	auto tp = system_clock::now();
	auto now = system_clock::to_time_t(tp);
	tstruct = *localtime(&now);

	size_t written = strftime(buf, sizeof(buf), "%T", &tstruct);
	if (std::ratio_less<system_clock::period, seconds::period>::value && written &&
	    (sizeof(buf) - written) > 5) {
		auto tp_secs = time_point_cast<seconds>(tp);
		auto millis = duration_cast<milliseconds>(tp - tp_secs).count();

		snprintf(buf + written, sizeof(buf) - written, ".%03u",
			 static_cast<unsigned>(millis));
	}

	return buf;
}

std::string CurrentDateTimeString() {
	time_t now = time(0);
	struct tm tstruct;
	char buf[80];
	tstruct = *localtime(&now);
	strftime(buf, sizeof(buf), "%Y-%m-%d, %X", &tstruct);
	return buf;
}

std::string GenerateTimeDateFilename(const char* extension, bool noSpace) {
	time_t now = time(0);
	char file[256] = {};
	struct tm* cur_time;

	cur_time = localtime(&now);
	snprintf(file, sizeof(file), "%d-%02d-%02d%c%02d-%02d-%02d.%s", cur_time->tm_year + 1900,
		 cur_time->tm_mon + 1, cur_time->tm_mday, noSpace ? '_' : ' ', cur_time->tm_hour,
		 cur_time->tm_min, cur_time->tm_sec, extension);

	return std::string(file);
}

std::string GenerateSpecifiedFilename(const char* extension, bool noSpace, const char* format) {
	BPtr<char> filename = os_generate_formatted_filename(extension, !noSpace, format);
	return std::string(filename);
}

std::string GetFormatString(const char* format, const char* prefix, const char* suffix) {
	std::string f;

	f = format;

	if (prefix && *prefix) {
		std::string str_prefix = prefix;

		if (str_prefix.back() != ' ')
			str_prefix += " ";

		size_t insert_pos = 0;
		size_t tmp;

		tmp = f.find_last_of('/');
		if (tmp != std::string::npos && tmp > insert_pos)
			insert_pos = tmp + 1;

		tmp = f.find_last_of('\\');
		if (tmp != std::string::npos && tmp > insert_pos)
			insert_pos = tmp + 1;

		f.insert(insert_pos, str_prefix);
	}

	if (suffix && *suffix) {
		if (*suffix != ' ')
			f += " ";
		f += suffix;
	}

	remove_reserved_file_characters(f);

	return f;
}

std::string GetFormatExt(const char* container) {
	std::string ext = container;
	if (ext == "fragmented_mp4")
		ext = "mp4";
	else if (ext == "fragmented_mov")
		ext = "mov";
	else if (ext == "hls")
		ext = "m3u8";
	else if (ext == "mpegts")
		ext = "ts";

	return ext;
}

std::string GetOutputFilename(const char* path, const char* container, bool noSpace, bool overwrite,
			      const char* format) {
	os_dir_t* dir = path && path[0] ? os_opendir(path) : nullptr;

	if (!dir) {
		blog(LOG_WARNING, "Could not open output directory '%s'", path);
		return "";
	}

	os_closedir(dir);

	std::string strPath;
	strPath += path;

	char lastChar = strPath.back();
	if (lastChar != '/' && lastChar != '\\')
		strPath += "/";

	std::string ext = GetFormatExt(container);
	strPath += GenerateSpecifiedFilename(ext.c_str(), noSpace, format);
	ensure_directory_exists(strPath);
	if (!overwrite)
		FindBestFilename(strPath, noSpace);

	return strPath;
}

int GetConfigPath(char* path, size_t size, const char* name) {
#if ALLOW_PORTABLE_MODE
	if (portable_mode) {
		if (name && *name) {
			return snprintf(path, size, CONFIG_PATH "/%s", name);
		} else {
			return snprintf(path, size, CONFIG_PATH);
		}
	} else {
		return os_get_config_path(path, size, name);
	}
#else
	return os_get_config_path(path, size, name);
#endif
}

char* GetConfigPathPtr(const char* name) {
#if ALLOW_PORTABLE_MODE
	if (portable_mode) {
		char path[512];

		if (snprintf(path, sizeof(path), CONFIG_PATH "/%s", name) > 0) {
			return bstrdup(path);
		} else {
			return NULL;
		}
	} else {
		return os_get_config_path_ptr(name);
	}
#else
	return os_get_config_path_ptr(name);
#endif
}

int GetProfilePath(char* path, size_t size, const char* file, const char* profile) {
	char profiles_path[512];
	int ret;

	if (!profile)
		return -1;
	if (!path)
		return -1;
	if (!file)
		file = "";

	ret = GetConfigPath(profiles_path, 512, "obs-studio/basic/profiles");
	if (ret <= 0)
		return ret;

	if (!*file)
		return snprintf(path, size, "%s/%s", profiles_path, profile);

	return snprintf(path, size, "%s/%s/%s", profiles_path, profile, file);
}

int GetProgramDataPath(char* path, size_t size, const char* name) {
	return os_get_program_data_path(path, size, name);
}

char* GetProgramDataPathPtr(const char* name) {
	return os_get_program_data_path_ptr(name);
}

bool GetFileSafeName(const char* name, std::string& file) {
	size_t base_len = strlen(name);
	size_t len = os_utf8_to_wcs(name, base_len, nullptr, 0);
	std::wstring wfile;

	if (!len)
		return false;

	wfile.resize(len);
	os_utf8_to_wcs(name, base_len, &wfile[0], len + 1);

	for (size_t i = wfile.size(); i > 0; i--) {
		size_t im1 = i - 1;

		if (iswspace(wfile[im1])) {
			wfile[im1] = '_';
		} else if (wfile[im1] != '_' && !iswalnum(wfile[im1])) {
			wfile.erase(im1, 1);
		}
	}

	if (wfile.size() == 0)
		wfile = L"characters_only";

	len = os_wcs_to_utf8(wfile.c_str(), wfile.size(), nullptr, 0);
	if (!len)
		return false;

	file.resize(len);
	os_wcs_to_utf8(wfile.c_str(), wfile.size(), &file[0], len + 1);
	return true;
}

bool GetClosestUnusedFileName(std::string& path, const char* extension) {
	size_t len = path.size();
	if (extension) {
		path += ".";
		path += extension;
	}

	if (!os_file_exists(path.c_str()))
		return true;

	int index = 1;

	do {
		path.resize(len);
		path += std::to_string(++index);
		if (extension) {
			path += ".";
			path += extension;
		}
	} while (os_file_exists(path.c_str()));

	return true;
}

bool GetUnusedSceneCollectionFile(std::string& name, std::string& file) {
	char path[512];
	int ret;

	if (!GetFileSafeName(name.c_str(), file)) {
		blog(LOG_WARNING, "Failed to create safe file name for '%s'", name.c_str());
		return false;
	}

	ret = GetConfigPath(path, sizeof(path), "obs-studio/basic/scenes/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get scene collection config path");
		return false;
	}

	file.insert(0, path);

	if (!GetClosestUnusedFileName(file, "json")) {
		blog(LOG_WARNING, "Failed to get closest file name for %s", file.c_str());
		return false;
	}

	file.erase(file.size() - 5, 5);
	file.erase(0, strlen(path));
	return true;
}

std::vector<std::pair<std::string, std::string>> GetLocaleNames() {
	std::string path;
	if (!GetDataFilePath("locale.ini", path))
		throw "Could not find locale.ini path";

	ConfigFile ini;
	if (ini.Open(path.c_str(), CONFIG_OPEN_EXISTING) != 0)
		throw "Could not open locale.ini";

	size_t sections = config_num_sections(ini);

	std::vector<std::pair<std::string, std::string>> names;
	names.reserve(sections);
	for (size_t i = 0; i < sections; i++) {
		const char* tag = config_get_section(ini, i);
		const char* name = config_get_string(ini, tag, "Name");
		names.emplace_back(tag, name);
	}

	return names;
}

bool EncoderAvailable(const char* encoder) {
	const char* val;
	int i = 0;

	while (obs_enum_encoder_types(i++, &val))
		if (strcmp(val, encoder) == 0)
			return true;

	return false;
}

enum obs_scale_type GetScaleType(ConfigFile& basicConfig) {
	const char* scaleTypeStr = config_get_string(basicConfig, "Video", "ScaleType");

	if (astrcmpi(scaleTypeStr, "bilinear") == 0)
		return OBS_SCALE_BILINEAR;
	else if (astrcmpi(scaleTypeStr, "lanczos") == 0)
		return OBS_SCALE_LANCZOS;
	else if (astrcmpi(scaleTypeStr, "area") == 0)
		return OBS_SCALE_AREA;
	else
		return OBS_SCALE_BICUBIC;
}

enum video_format GetVideoFormatFromName(const char* name) {
	if (astrcmpi(name, "I420") == 0)
		return VIDEO_FORMAT_I420;
	else if (astrcmpi(name, "NV12") == 0)
		return VIDEO_FORMAT_NV12;
	else if (astrcmpi(name, "I444") == 0)
		return VIDEO_FORMAT_I444;
	else if (astrcmpi(name, "I010") == 0)
		return VIDEO_FORMAT_I010;
	else if (astrcmpi(name, "P010") == 0)
		return VIDEO_FORMAT_P010;
	else if (astrcmpi(name, "P216") == 0)
		return VIDEO_FORMAT_P216;
	else if (astrcmpi(name, "P416") == 0)
		return VIDEO_FORMAT_P416;
#if 0 //currently unsupported
  else if (astrcmpi(name, "YVYU") == 0)
    return VIDEO_FORMAT_YVYU;
  else if (astrcmpi(name, "YUY2") == 0)
    return VIDEO_FORMAT_YUY2;
  else if (astrcmpi(name, "UYVY") == 0)
    return VIDEO_FORMAT_UYVY;
#endif
	else
		return VIDEO_FORMAT_BGRA;
}

enum video_colorspace GetVideoColorSpaceFromName(const char* name) {
	enum video_colorspace colorspace = VIDEO_CS_SRGB;
	if (strcmp(name, "601") == 0)
		colorspace = VIDEO_CS_601;
	else if (strcmp(name, "709") == 0)
		colorspace = VIDEO_CS_709;
	else if (strcmp(name, "2100PQ") == 0)
		colorspace = VIDEO_CS_2100_PQ;
	else if (strcmp(name, "2100HLG") == 0)
		colorspace = VIDEO_CS_2100_HLG;

	return colorspace;
}

bool HasAudioDevices(const char* source_id) {
	const char* output_id = source_id;
	obs_properties_t* props = obs_get_source_properties(output_id);
	size_t count = 0;

	if (!props)
		return false;

	obs_property_t* devices = obs_properties_get(props, "device_id");
	if (devices)
		count = obs_property_list_item_count(devices);

	obs_properties_destroy(props);

	return count != 0;
}

void AddExtraModulePaths() {
	std::string plugins_path, plugins_data_path;
	char* s;

	s = getenv("OBS_PLUGINS_PATH");
	if (s)
		plugins_path = s;

	s = getenv("OBS_PLUGINS_DATA_PATH");
	if (s)
		plugins_data_path = s;

	if (!plugins_path.empty() && !plugins_data_path.empty()) {
		std::string data_path_with_module_suffix;
		data_path_with_module_suffix += plugins_data_path;
		data_path_with_module_suffix += "/%module%";
		obs_add_module_path(plugins_path.c_str(), data_path_with_module_suffix.c_str());
	}

	char base_module_dir[512];
#if defined(_WIN32)
	int ret = GetProgramDataPath(base_module_dir, sizeof(base_module_dir),
				     "obs-studio/plugins/%module%");
#elif defined(__APPLE__)
	int ret = GetConfigPath(base_module_dir, sizeof(base_module_dir),
				"obs-studio/plugins/%module%.plugin");
#else
	int ret =
	  GetConfigPath(base_module_dir, sizeof(base_module_dir), "obs-studio/plugins/%module%");
#endif

	if (ret <= 0)
		return;

	std::string path = base_module_dir;
#if defined(__APPLE__)
	/* User Application Support Search Path */
	obs_add_module_path((path + "/Contents/MacOS").c_str(),
			    (path + "/Contents/Resources").c_str());

#ifndef __aarch64__
	/* Legacy System Library Search Path */
	char system_legacy_module_dir[PATH_MAX];
	GetProgramDataPath(system_legacy_module_dir, sizeof(system_legacy_module_dir),
			   "obs-studio/plugins/%module%");
	std::string path_system_legacy = system_legacy_module_dir;
	obs_add_module_path((path_system_legacy + "/bin").c_str(),
			    (path_system_legacy + "/data").c_str());

	/* Legacy User Application Support Search Path */
	char user_legacy_module_dir[PATH_MAX];
	GetConfigPath(user_legacy_module_dir, sizeof(user_legacy_module_dir),
		      "obs-studio/plugins/%module%");
	std::string path_user_legacy = user_legacy_module_dir;
	obs_add_module_path((path_user_legacy + "/bin").c_str(),
			    (path_user_legacy + "/data").c_str());
#endif
#else
#if ARCH_BITS == 64
	obs_add_module_path((path + "/bin/64bit").c_str(), (path + "/data").c_str());
#else
	obs_add_module_path((path + "/bin/32bit").c_str(), (path + "/data").c_str());
#endif
#endif
}

/* First-party modules considered to be potentially unsafe to load in Safe Mode
 * due to them allowing external code (e.g. scripts) to modify OBS's state. */
const std::unordered_set<std::string> unsafe_modules = {
  "frontend-tools", // Scripting
  "obs-websocket",  // Allows outside modifications
};

void SetSafeModuleNames() {
#ifndef SAFE_MODULES
	return;
#else
	std::string module;
	std::stringstream modules(SAFE_MODULES);

	while (std::getline(modules, module, '|')) {
		/* When only disallowing third-party plugins, still add
     * "unsafe" bundled modules to the safe list. */
		if (!unsafe_modules.count(module))
			obs_add_safe_module(module.c_str());
	}
#endif
}

void LogFilter(obs_source_t*, obs_source_t* filter, void* v_val) {
	const char* name = obs_source_get_name(filter);
	const char* id = obs_source_get_id(filter);
	int val = (int)(intptr_t)v_val;
	std::string indent;

	for (int i = 0; i < val; i++) indent += "    ";

	blog(LOG_INFO, "%s- filter: '%s' (%s)", indent.c_str(), name, id);
}

bool LogSceneItem(obs_scene_t*, obs_sceneitem_t* item, void* v_val) {
	obs_source_t* source = obs_sceneitem_get_source(item);
	const char* name = obs_source_get_name(source);
	const char* id = obs_source_get_id(source);
	int indent_count = (int)(intptr_t)v_val;
	std::string indent;

	for (int i = 0; i < indent_count; i++) indent += "    ";

	blog(LOG_INFO, "%s- source: '%s' (%s)", indent.c_str(), name, id);

	obs_monitoring_type monitoring_type = obs_source_get_monitoring_type(source);

	if (monitoring_type != OBS_MONITORING_TYPE_NONE) {
		const char* type = (monitoring_type == OBS_MONITORING_TYPE_MONITOR_ONLY)
				     ? "monitor only"
				     : "monitor and output";

		blog(LOG_INFO, "    %s- monitoring: %s", indent.c_str(), type);
	}
	int child_indent = 1 + indent_count;
	obs_source_enum_filters(source, LogFilter, (void*)(intptr_t)child_indent);

	obs_source_t* show_tn = obs_sceneitem_get_transition(item, true);
	obs_source_t* hide_tn = obs_sceneitem_get_transition(item, false);
	if (show_tn)
		blog(LOG_INFO, "    %s- show: '%s' (%s)", indent.c_str(),
		     obs_source_get_name(show_tn), obs_source_get_id(show_tn));
	if (hide_tn)
		blog(LOG_INFO, "    %s- hide: '%s' (%s)", indent.c_str(),
		     obs_source_get_name(hide_tn), obs_source_get_id(hide_tn));

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, LogSceneItem, (void*)(intptr_t)child_indent);
	return true;
}

void LoadAudioDevice(const char* name, int channel, obs_data_t* parent) {
	OBSDataAutoRelease data = obs_data_get_obj(parent, name);
	if (!data)
		return;

	OBSSourceAutoRelease source = obs_load_source(data);
	if (!source)
		return;

	obs_set_output_source(channel, source);

	const char* source_name = obs_source_get_name(source);
	blog(LOG_INFO, "[Loaded global audio device]: '%s'", source_name);
	obs_source_enum_filters(source, LogFilter, (void*)(intptr_t)1);
	obs_monitoring_type monitoring_type = obs_source_get_monitoring_type(source);
	if (monitoring_type != OBS_MONITORING_TYPE_NONE) {
		const char* type = (monitoring_type == OBS_MONITORING_TYPE_MONITOR_ONLY)
				     ? "monitor only"
				     : "monitor and output";

		blog(LOG_INFO, "    - monitoring: %s", type);
	}
}

void AddMissingFiles(void* data, obs_source_t* source) {
	obs_missing_files_t* f = (obs_missing_files_t*)data;
	obs_missing_files_t* sf = obs_source_get_missing_files(source);

	obs_missing_files_append(f, sf);
	obs_missing_files_destroy(sf);
}

const char* InputAudioSource() {
	return INPUT_AUDIO_SOURCE;
}

const char* OutputAudioSource() {
	return OUTPUT_AUDIO_SOURCE;
}

char* get_new_source_name(const char* name, const char* format) {
	struct dstr new_name = {0};
	int inc = 0;

	dstr_copy(&new_name, name);

	for (;;) {
		OBSSourceAutoRelease existing_source = obs_get_source_by_name(new_name.array);
		if (!existing_source)
			break;

		dstr_printf(&new_name, format, name, ++inc + 1);
	}

	return new_name.array;
}
} // namespace core
