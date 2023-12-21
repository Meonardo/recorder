#include "app.h"

#include <Windows.h>
#include <filesystem>

#include <time.h>
#include <stdio.h>
#include <wchar.h>

#include <cstddef>
#include <chrono>
#include <ratio>
#include <string>
#include <sstream>
#include <mutex>
#include <numeric>
#include <functional>
#include <fstream>
#include <iostream>
#include <algorithm>

#include <util/threading.h>
#include <util/bmem.h>
#include <util/dstr.hpp>
#include <util/platform.h>
#include <util/profiler.hpp>
#include <util/cf-parser.h>
#include <obs-config.h>

#include "defines.h"
#include "output.h"

static log_handler_t def_log_handler;
static std::string currentLogFile;
static std::string lastLogFile;
static std::string lastCrashLogFile;

static bool log_verbose = false;
static bool unfiltered_log = false;

namespace core {

static void delete_oldest_file(bool has_prefix, const char* location) {
	BPtr<char> logDir(GetConfigPathPtr(location));
	std::string oldestLog;
	uint64_t oldest_ts = (uint64_t)-1;
	struct os_dirent* entry;

	unsigned int maxLogs =
	  (unsigned int)config_get_uint(CoreApp->GetGlobalConfig(), "General", "MaxLogs");

	os_dir_t* dir = os_opendir(logDir);
	if (dir) {
		unsigned int count = 0;

		while ((entry = os_readdir(dir)) != NULL) {
			if (entry->directory || *entry->d_name == '.')
				continue;

			uint64_t ts = convert_log_name(has_prefix, entry->d_name);

			if (ts) {
				if (ts < oldest_ts) {
					oldestLog = entry->d_name;
					oldest_ts = ts;
				}

				count++;
			}
		}

		os_closedir(dir);

		if (count > maxLogs) {
			std::stringstream delPath;

			delPath << logDir << "/" << oldestLog;
			os_unlink(delPath.str().c_str());
		}
	}
}

static void get_last_log(bool has_prefix, const char* subdir_to_use, std::string& last) {
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

static void main_crash_handler(const char* format, va_list args, void* /* param */) {
	char* text = new char[MAX_CRASH_REPORT_SIZE];

	vsnprintf(text, MAX_CRASH_REPORT_SIZE, format, args);
	text[MAX_CRASH_REPORT_SIZE - 1] = 0;

	std::string crashFilePath = "obs-studio/crashes";

	delete_oldest_file(true, crashFilePath.c_str());

	std::string name = crashFilePath + "/";
	name += "Crash " + GenerateTimeDateFilename("txt");

	BPtr<char> path(GetConfigPathPtr(name.c_str()));

	std::fstream file;

#ifdef _WIN32
	BPtr<wchar_t> wpath;
	os_utf8_to_wcs_ptr(path, 0, &wpath);
	file.open(wpath, std::ios_base::in | std::ios_base::out | std::ios_base::trunc |
			   std::ios_base::binary);
#else
	file.open(path, ios_base::in | ios_base::out | ios_base::trunc | ios_base::binary);
#endif
	file << text;
	file.close();

	std::string pathString(path.Get());

#ifdef _WIN32
	std::replace(pathString.begin(), pathString.end(), '/', '\\');
#endif

	std::string absolutePath =
	  std::filesystem::canonical(std::filesystem::path(pathString)).u8string();

	size_t size = snprintf(nullptr, 0, CRASH_MESSAGE, absolutePath.c_str());

	std::unique_ptr<char[]> message_buffer(new char[size + 1]);

	snprintf(message_buffer.get(), size + 1, CRASH_MESSAGE, absolutePath.c_str());

	std::string finalMessage = std::string(message_buffer.get(), message_buffer.get() + size);

	int ret = MessageBoxA(NULL, CRASH_MESSAGE, "Woops...",
			      MB_YESNO | MB_ICONERROR | MB_TASKMODAL);

	if (ret == IDYES) {
		size_t len = strlen(text);

		HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len);
		memcpy(GlobalLock(mem), text, len);
		GlobalUnlock(mem);

		OpenClipboard(0);
		EmptyClipboard();
		SetClipboardData(CF_TEXT, mem);
		CloseClipboard();
	}

	exit(-1);
}

static void load_debug_privilege(void) {
	const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
	TOKEN_PRIVILEGES tp;
	HANDLE token;
	LUID val;

	if (!OpenProcessToken(GetCurrentProcess(), flags, &token)) {
		return;
	}

	if (!!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &val)) {
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = val;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL, NULL);
	}

	if (!!LookupPrivilegeValue(NULL, SE_INC_BASE_PRIORITY_NAME, &val)) {
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = val;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		if (!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL, NULL)) {
			blog(LOG_INFO, "Could not set privilege to "
				       "increase GPU priority");
		}
	}

	CloseHandle(token);
}

static inline bool arg_is(const char* arg, const char* long_form, const char* short_form) {
	return (long_form && strcmp(arg, long_form) == 0) ||
	       (short_form && strcmp(arg, short_form) == 0);
}

static bool update_ffmpeg_output(ConfigFile& config) {
	if (config_has_user_value(config, "AdvOut", "FFOutputToFile"))
		return false;

	const char* url = config_get_string(config, "AdvOut", "FFURL");
	if (!url)
		return false;

	bool isActualURL = strstr(url, "://") != nullptr;
	if (isActualURL)
		return false;

	std::string urlStr = url;
	std::string extension;

	for (size_t i = urlStr.length(); i > 0; i--) {
		size_t idx = i - 1;

		if (urlStr[idx] == '.') {
			extension = &urlStr[i];
		}

		if (urlStr[idx] == '\\' || urlStr[idx] == '/') {
			urlStr[idx] = 0;
			break;
		}
	}

	if (urlStr.empty() || extension.empty())
		return false;

	config_remove_value(config, "AdvOut", "FFURL");
	config_set_string(config, "AdvOut", "FFFilePath", urlStr.c_str());
	config_set_string(config, "AdvOut", "FFExtension", extension.c_str());
	config_set_bool(config, "AdvOut", "FFOutputToFile", true);
	return true;
}

static bool move_reconnect_settings(ConfigFile& config, const char* sec) {
	bool changed = false;

	if (config_has_user_value(config, sec, "Reconnect")) {
		bool reconnect = config_get_bool(config, sec, "Reconnect");
		config_set_bool(config, "Output", "Reconnect", reconnect);
		changed = true;
	}
	if (config_has_user_value(config, sec, "RetryDelay")) {
		int delay = (int)config_get_uint(config, sec, "RetryDelay");
		config_set_uint(config, "Output", "RetryDelay", delay);
		changed = true;
	}
	if (config_has_user_value(config, sec, "MaxRetries")) {
		int retries = (int)config_get_uint(config, sec, "MaxRetries");
		config_set_uint(config, "Output", "MaxRetries", retries);
		changed = true;
	}

	return changed;
}

static bool update_reconnect(ConfigFile& config) {
	if (!config_has_user_value(config, "Output", "Mode"))
		return false;

	const char* mode = config_get_string(config, "Output", "Mode");
	if (!mode)
		return false;

	const char* section = (strcmp(mode, "Advanced") == 0) ? "AdvOut" : "SimpleOutput";

	if (move_reconnect_settings(config, section)) {
		config_remove_value(config, "SimpleOutput", "Reconnect");
		config_remove_value(config, "SimpleOutput", "RetryDelay");
		config_remove_value(config, "SimpleOutput", "MaxRetries");
		config_remove_value(config, "AdvOut", "Reconnect");
		config_remove_value(config, "AdvOut", "RetryDelay");
		config_remove_value(config, "AdvOut", "MaxRetries");
		return true;
	}

	return false;
}

static void convert_nvenc_h264_presets(obs_data_t* data) {
	const char* preset = obs_data_get_string(data, "preset");
	const char* rc = obs_data_get_string(data, "rate_control");

	// If already using SDK10+ preset, return early.
	if (astrcmpi_n(preset, "p", 1) == 0) {
		obs_data_set_string(data, "preset2", preset);
		return;
	}

	if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "mq")) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "hp")) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "mq") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "qres");

	} else if (astrcmpi(preset, "hq") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "default") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "hp") == 0) {
		obs_data_set_string(data, "preset2", "p1");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "ll") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhq") == 0) {
		obs_data_set_string(data, "preset2", "p4");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhp") == 0) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");
	}
}

static void convert_nvenc_hevc_presets(obs_data_t* data) {
	const char* preset = obs_data_get_string(data, "preset");
	const char* rc = obs_data_get_string(data, "rate_control");

	// If already using SDK10+ preset, return early.
	if (astrcmpi_n(preset, "p", 1) == 0) {
		obs_data_set_string(data, "preset2", preset);
		return;
	}

	if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "mq")) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "hp")) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "mq") == 0) {
		obs_data_set_string(data, "preset2", "p6");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "qres");

	} else if (astrcmpi(preset, "hq") == 0) {
		obs_data_set_string(data, "preset2", "p6");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "default") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "hp") == 0) {
		obs_data_set_string(data, "preset2", "p1");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "ll") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhq") == 0) {
		obs_data_set_string(data, "preset2", "p4");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhp") == 0) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");
	}
}

static void convert_28_1_encoder_setting(const char* encoder, const char* file) {
	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(file, "bak");
	bool modified = false;

	if (astrcmpi(encoder, "jim_nvenc") == 0 || astrcmpi(encoder, "ffmpeg_nvenc") == 0) {

		if (obs_data_has_user_value(data, "preset") &&
		    !obs_data_has_user_value(data, "preset2")) {
			convert_nvenc_h264_presets(data);

			modified = true;
		}
	} else if (astrcmpi(encoder, "jim_hevc_nvenc") == 0 ||
		   astrcmpi(encoder, "ffmpeg_hevc_nvenc") == 0) {

		if (obs_data_has_user_value(data, "preset") &&
		    !obs_data_has_user_value(data, "preset2")) {
			convert_nvenc_hevc_presets(data);

			modified = true;
		}
	}

	if (modified)
		obs_data_save_json_safe(data, file, "tmp", "bak");
}

bool update_nvenc_presets(ConfigFile& config) {
	if (config_has_user_value(config, "SimpleOutput", "NVENCPreset2") ||
	    !config_has_user_value(config, "SimpleOutput", "NVENCPreset"))
		return false;

	const char* streamEncoder = config_get_string(config, "SimpleOutput", "StreamEncoder");
	const char* nvencPreset = config_get_string(config, "SimpleOutput", "NVENCPreset");

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_string(data, "preset", nvencPreset);

	if (astrcmpi(streamEncoder, "nvenc_hevc") == 0) {
		convert_nvenc_hevc_presets(data);
	} else {
		convert_nvenc_h264_presets(data);
	}

	config_set_string(config, "SimpleOutput", "NVENCPreset2",
			  obs_data_get_string(data, "preset2"));

	return true;
}

static void upgrade_settings(void) {
	char path[512];
	int pathlen = GetConfigPath(path, 512, "obs-studio/basic/profiles");

	if (pathlen <= 0)
		return;
	if (!os_file_exists(path))
		return;

	os_dir_t* dir = os_opendir(path);
	if (!dir)
		return;

	struct os_dirent* ent = os_readdir(dir);

	while (ent) {
		if (ent->directory && strcmp(ent->d_name, ".") != 0 &&
		    strcmp(ent->d_name, "..") != 0) {
			strcat(path, "/");
			strcat(path, ent->d_name);
			strcat(path, "/basic.ini");

			ConfigFile config;
			int ret;

			ret = config.Open(path, CONFIG_OPEN_EXISTING);
			if (ret == CONFIG_SUCCESS) {
				if (update_ffmpeg_output(config) || update_reconnect(config)) {
					config_save_safe(config, "tmp", nullptr);
				}
			}

			if (config) {
				const char* sEnc = config_get_string(config, "AdvOut", "Encoder");
				const char* rEnc =
				  config_get_string(config, "AdvOut", "RecEncoder");

				/* replace "cbr" option with "rate_control" for
         * each profile's encoder data */
				path[pathlen] = 0;
				strcat(path, "/");
				strcat(path, ent->d_name);
				strcat(path, "/recordEncoder.json");
				convert_28_1_encoder_setting(rEnc, path);

				path[pathlen] = 0;
				strcat(path, "/");
				strcat(path, ent->d_name);
				strcat(path, "/streamEncoder.json");
				convert_28_1_encoder_setting(sEnc, path);
			}

			path[pathlen] = 0;
		}

		ent = os_readdir(dir);
	}

	os_closedir(dir);
}

static inline int sum_chars(const char* str) {
	int val = 0;
	for (; *str != 0; str++) val += *str;

	return val;
}

static bool too_many_repeated_entries(std::fstream& logFile, const char* msg,
				      const char* output_str) {
	static std::mutex log_mutex;
	static const char* last_msg_ptr = nullptr;
	static int last_char_sum = 0;
	static char cmp_str[4096];
	static int rep_count = 0;

	int new_sum = sum_chars(output_str);

	std::lock_guard<std::mutex> guard(log_mutex);

	if (unfiltered_log) {
		return false;
	}

	if (last_msg_ptr == msg) {
		int diff = std::abs(new_sum - last_char_sum);
		if (diff < MAX_CHAR_VARIATION) {
			return (rep_count++ >= MAX_REPEATED_LINES);
		}
	}

	if (rep_count > MAX_REPEATED_LINES) {
		logFile << CurrentTimeString() << ": Last log entry repeated for "
			<< std::to_string(rep_count - MAX_REPEATED_LINES) << " more lines"
			<< std::endl;
	}

	last_msg_ptr = msg;
	strcpy(cmp_str, output_str);
	last_char_sum = new_sum;
	rep_count = 0;

	return false;
}

static void LogString(std::fstream& logFile, const char* timeString, char* str, int log_level) {}

static void LogStringChunk(std::fstream& logFile, char* str, int log_level) {
	char* nextLine = str;
	std::string timeString = CurrentTimeString();
	timeString += ": ";

	while (*nextLine) {
		char* nextLine = strchr(str, '\n');
		if (!nextLine)
			break;

		if (nextLine != str && nextLine[-1] == '\r') {
			nextLine[-1] = 0;
		} else {
			nextLine[0] = 0;
		}

		LogString(logFile, timeString.c_str(), str, log_level);
		nextLine++;
		str = nextLine;
	}

	LogString(logFile, timeString.c_str(), str, log_level);
}

static void do_log(int log_level, const char* msg, va_list args, void* param) {
	std::fstream& logFile = *static_cast<std::fstream*>(param);
	char str[4096];

#ifndef _WIN32
	va_list args2;
	va_copy(args2, args);
#endif

	vsnprintf(str, sizeof(str), msg, args);

#ifdef _WIN32
	if (IsDebuggerPresent()) {
		int wNum = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
		if (wNum > 1) {
			static std::wstring wide_buf;
			static std::mutex wide_mutex;

			std::lock_guard<std::mutex> lock(wide_mutex);
			wide_buf.reserve(wNum + 1);
			wide_buf.resize(wNum - 1);
			MultiByteToWideChar(CP_UTF8, 0, str, -1, &wide_buf[0], wNum);
			wide_buf.push_back('\n');

			OutputDebugStringW(wide_buf.c_str());
		}
	}
#endif

#if !defined(_WIN32) && defined(_DEBUG)
	def_log_handler(log_level, msg, args2, nullptr);
#endif

	if (log_level <= LOG_INFO || log_verbose) {
#if !defined(_WIN32) && !defined(_DEBUG)
		def_log_handler(log_level, msg, args2, nullptr);
#endif
		if (!too_many_repeated_entries(logFile, msg, str))
			LogStringChunk(logFile, str, log_level);
	}

#if defined(_WIN32) && defined(OBS_DEBUGBREAK_ON_ERROR)
	if (log_level <= LOG_ERROR && IsDebuggerPresent())
		__debugbreak();
#endif

#ifndef _WIN32
	va_end(args2);
#endif
}

static void create_log_file(std::fstream& logFile) {
	std::stringstream dst;

	get_last_log(false, "obs-studio/logs", lastLogFile);
#ifdef _WIN32
	get_last_log(true, "obs-studio/crashes", lastCrashLogFile);
#endif

	currentLogFile = GenerateTimeDateFilename("txt");
	dst << "obs-studio/logs/" << currentLogFile.c_str();

	BPtr<char> path(GetConfigPathPtr(dst.str().c_str()));

#ifdef _WIN32
	BPtr<wchar_t> wpath;
	os_utf8_to_wcs_ptr(path, 0, &wpath);
	logFile.open(wpath, std::ios_base::in | std::ios_base::out | std::ios_base::trunc);
#else
	logFile.open(path, std::ios_base::in | std::ios_base::out | std::ios_base::trunc);
#endif

	if (logFile.is_open()) {
		delete_oldest_file(false, "obs-studio/logs");
		base_set_log_handler(do_log, &logFile);
	} else {
		blog(LOG_ERROR, "Failed to open log file");
	}
}

static auto ProfilerNameStoreRelease = [](profiler_name_store_t* store) {
	profiler_name_store_free(store);
};

using ProfilerNameStore =
  std::unique_ptr<profiler_name_store_t, decltype(ProfilerNameStoreRelease)>;

ProfilerNameStore CreateNameStore() {
	return ProfilerNameStore{profiler_name_store_create(), ProfilerNameStoreRelease};
}

static auto SnapshotRelease = [](profiler_snapshot_t* snap) {
	profile_snapshot_free(snap);
};

using ProfilerSnapshot = std::unique_ptr<profiler_snapshot_t, decltype(SnapshotRelease)>;

ProfilerSnapshot GetSnapshot() {
	return ProfilerSnapshot{profile_snapshot_create(), SnapshotRelease};
}

static void SaveProfilerData(const ProfilerSnapshot& snap) {
	if (currentLogFile.empty())
		return;

	auto pos = currentLogFile.rfind('.');
	if (pos == currentLogFile.npos)
		return;

#define LITERAL_SIZE(x) x, (sizeof(x) - 1)
	std::ostringstream dst;
	dst.write(LITERAL_SIZE("obs-studio/profiler_data/"));
	dst.write(currentLogFile.c_str(), pos);
	dst.write(LITERAL_SIZE(".csv.gz"));
#undef LITERAL_SIZE

	BPtr<char> path = GetConfigPathPtr(dst.str().c_str());
	if (!profiler_snapshot_dump_csv_gz(snap.get(), path))
		blog(LOG_WARNING, "Could not save profiler data to '%s'",
		     static_cast<const char*>(path));
}

static auto ProfilerFree = [](void*) {
	profiler_stop();

	auto snap = GetSnapshot();

	profiler_print(snap.get());
	profiler_print_time_between_calls(snap.get());

	SaveProfilerData(snap);

	profiler_free();
};

static void move_basic_to_profiles(void) {
	char path[512];
	char new_path[512];
	os_glob_t* glob;

	/* if not first time use */
	if (GetConfigPath(path, 512, "obs-studio/basic") <= 0)
		return;
	if (!os_file_exists(path))
		return;

	/* if the profiles directory doesn't already exist */
	if (GetConfigPath(new_path, 512, "obs-studio/basic/profiles") <= 0)
		return;
	if (os_file_exists(new_path))
		return;

	if (os_mkdir(new_path) == MKDIR_ERROR)
		return;

	strcat(new_path, "/");
	strcat(new_path, Str("Untitled"));
	if (os_mkdir(new_path) == MKDIR_ERROR)
		return;

	strcat(path, "/*.*");
	if (os_glob(path, 0, &glob) != 0)
		return;

	strcpy(path, new_path);

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		struct os_globent ent = glob->gl_pathv[i];
		char* file;

		if (ent.directory)
			continue;

		file = strrchr(ent.path, '/');
		if (!file++)
			continue;

		if (astrcmpi(file, "scenes.json") == 0)
			continue;

		strcpy(new_path, path);
		strcat(new_path, "/");
		strcat(new_path, file);
		os_rename(ent.path, new_path);
	}

	os_globfree(glob);
}

static void move_basic_to_scene_collections(void) {
	char path[512];
	char new_path[512];

	if (GetConfigPath(path, 512, "obs-studio/basic") <= 0)
		return;
	if (!os_file_exists(path))
		return;

	if (GetConfigPath(new_path, 512, "obs-studio/basic/scenes") <= 0)
		return;
	if (os_file_exists(new_path))
		return;

	if (os_mkdir(new_path) == MKDIR_ERROR)
		return;

	strcat(path, "/scenes.json");
	strcat(new_path, "/");
	strcat(new_path, Str("Untitled"));
	strcat(new_path, ".json");

	os_rename(path, new_path);
}

static bool StartupOBS(const char* locale, profiler_name_store_t* store) {
	char path[512];

	if (GetConfigPath(path, sizeof(path), "obs-studio/plugin_config") <= 0)
		return false;

	return obs_startup(locale, path, store);
}

static void SaveAudioDevice(const char* name, int channel, obs_data_t* parent,
			    std::vector<OBSSource>& audioSources) {
	OBSSourceAutoRelease source = obs_get_output_source(channel);
	if (!source)
		return;

	audioSources.push_back(source.Get());

	OBSDataAutoRelease data = obs_save_source(source);

	obs_data_set_obj(parent, name, data);
}

static obs_data_t* GenerateSaveData(obs_data_array_t* sceneOrder,
				    obs_data_array_t* quickTransitionData, int transitionDuration,
				    obs_data_array_t* transitions, OBSScene& scene,
				    OBSSource& curProgramScene,
				    obs_data_array_t* savedProjectorList) {
	obs_data_t* saveData = obs_data_create();

	std::vector<OBSSource> audioSources;
	audioSources.reserve(6);

	SaveAudioDevice(DESKTOP_AUDIO_1, 1, saveData, audioSources);
	SaveAudioDevice(DESKTOP_AUDIO_2, 2, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_1, 3, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_2, 4, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_3, 5, saveData, audioSources);
	SaveAudioDevice(AUX_AUDIO_4, 6, saveData, audioSources);

	/* -------------------------------- */
	/* save non-group sources           */

	auto FilterAudioSources = [&](obs_source_t* source) {
		if (obs_source_is_group(source))
			return false;

		return find(begin(audioSources), end(audioSources), source) == end(audioSources);
	};
	using FilterAudioSources_t = decltype(FilterAudioSources);

	obs_data_array_t* sourcesArray = obs_save_sources_filtered(
	  [](void* data, obs_source_t* source) {
		  auto& func = *static_cast<FilterAudioSources_t*>(data);
		  return func(source);
	  },
	  static_cast<void*>(&FilterAudioSources));

	/* -------------------------------- */
	/* save group sources separately    */

	/* saving separately ensures they won't be loaded in older versions */
	obs_data_array_t* groupsArray = obs_save_sources_filtered(
	  [](void*, obs_source_t* source) { return obs_source_is_group(source); }, nullptr);

	/* -------------------------------- */

	OBSSourceAutoRelease transition = obs_get_output_source(0);
	obs_source_t* currentScene = obs_scene_get_source(scene);
	const char* sceneName = obs_source_get_name(currentScene);
	const char* programName = obs_source_get_name(curProgramScene);

	const char* sceneCollection =
	  config_get_string(CoreApp->GetGlobalConfig(), "Basic", "SceneCollection");

	obs_data_set_string(saveData, "current_scene", sceneName);
	obs_data_set_string(saveData, "current_program_scene", programName);
	obs_data_set_array(saveData, "scene_order", sceneOrder);
	obs_data_set_string(saveData, "name", sceneCollection);
	obs_data_set_array(saveData, "sources", sourcesArray);
	obs_data_set_array(saveData, "groups", groupsArray);
	obs_data_set_array(saveData, "quick_transitions", quickTransitionData);
	obs_data_set_array(saveData, "transitions", transitions);
	obs_data_set_array(saveData, "saved_projectors", savedProjectorList);
	obs_data_array_release(sourcesArray);
	obs_data_array_release(groupsArray);

	obs_data_set_string(saveData, "current_transition", obs_source_get_name(transition));
	obs_data_set_int(saveData, "transition_duration", transitionDuration);

	return saveData;
}

static const char* run_program_init = "run_program_init";

} // namespace core

namespace core {

App::App() {}

App::~App() {}

int App::Run(int argc, char* argv[], UIApplication* application) {
	this->application = application;
	// Try to keep this as early as possible
	install_dll_blocklist_hook();

	obs_init_win32_crash_handler();
	SetErrorMode(SEM_FAILCRITICALERRORS);
	load_debug_privilege();
	base_set_crash_handler(main_crash_handler, nullptr);

	const HMODULE hRtwq = LoadLibrary(L"RTWorkQ.dll");
	if (hRtwq) {
		typedef HRESULT(STDAPICALLTYPE * PFN_RtwqStartup)();
		PFN_RtwqStartup func = (PFN_RtwqStartup)GetProcAddress(hRtwq, "RtwqStartup");
		func();
	}

	base_get_log_handler(&def_log_handler, nullptr);

	obs_set_cmdline_args(argc, argv);

	for (int i = 1; i < argc; i++) {
		if (arg_is(argv[i], "--multi", "-m")) {
			launchOptions.multi = true;

#if ALLOW_PORTABLE_MODE
		} else if (arg_is(argv[i], "--portable", "-p")) {
			portable_mode = true;

#endif
		} else if (arg_is(argv[i], "--verbose", nullptr)) {
			launchOptions.log_verbose = true;

		} else if (arg_is(argv[i], "--safe-mode", nullptr)) {
			launchOptions.safe_mode = true;

		} else if (arg_is(argv[i], "--only-bundled-plugins", nullptr)) {
			launchOptions.disable_3p_plugins = true;

		} else if (arg_is(argv[i], "--disable-shutdown-check", nullptr)) {
			/* This exists mostly to bypass the dialog during development. */
			launchOptions.disable_shutdown_check = true;

		} else if (arg_is(argv[i], "--always-on-top", nullptr)) {
			launchOptions.opt_always_on_top = true;

		} else if (arg_is(argv[i], "--unfiltered_log", nullptr)) {
			launchOptions.unfiltered_log = true;

		} else if (arg_is(argv[i], "--startstreaming", nullptr)) {
			launchOptions.opt_start_streaming = true;

		} else if (arg_is(argv[i], "--startrecording", nullptr)) {
			launchOptions.opt_start_recording = true;

		} else if (arg_is(argv[i], "--startreplaybuffer", nullptr)) {
			launchOptions.opt_start_replaybuffer = true;

		} else if (arg_is(argv[i], "--startvirtualcam", nullptr)) {
			launchOptions.opt_start_virtualcam = true;

		} else if (arg_is(argv[i], "--collection", nullptr)) {
			if (++i < argc)
				launchOptions.opt_starting_collection = argv[i];

		} else if (arg_is(argv[i], "--profile", nullptr)) {
			if (++i < argc)
				launchOptions.opt_starting_profile = argv[i];

		} else if (arg_is(argv[i], "--scene", nullptr)) {
			if (++i < argc)
				launchOptions.opt_starting_scene = argv[i];

		} else if (arg_is(argv[i], "--minimize-to-tray", nullptr)) {
			launchOptions.opt_minimize_tray = true;

		} else if (arg_is(argv[i], "--studio-mode", nullptr)) {
			launchOptions.opt_studio_mode = true;

		} else if (arg_is(argv[i], "--allow-opengl", nullptr)) {
			launchOptions.opt_allow_opengl = true;

		} else if (arg_is(argv[i], "--disable-updater", nullptr)) {
			launchOptions.opt_disable_updater = true;

		} else if (arg_is(argv[i], "--disable-missing-files-check", nullptr)) {
			launchOptions.opt_disable_missing_files_check = true;

		} else if (arg_is(argv[i], "--steam", nullptr)) {
			launchOptions.steam = true;

		} else if (arg_is(argv[i], "--help", "-h")) {
			std::string help =
			  "--help, -h: Get list of available commands.\n\n"
			  "--startstreaming: Automatically start streaming.\n"
			  "--startrecording: Automatically start recording.\n"
			  "--startreplaybuffer: Start replay buffer.\n"
			  "--startvirtualcam: Start virtual camera (if available).\n\n"
			  "--collection <string>: Use specific scene collection."
			  "\n"
			  "--profile <string>: Use specific profile.\n"
			  "--scene <string>: Start with specific scene.\n\n"
			  "--studio-mode: Enable studio mode.\n"
			  "--minimize-to-tray: Minimize to system tray.\n"
#if ALLOW_PORTABLE_MODE
			  "--portable, -p: Use portable mode.\n"
#endif
			  "--multi, -m: Don't warn when launching multiple instances.\n\n"
			  "--safe-mode: Run in Safe Mode (disables third-party plugins, scripting, and WebSockets).\n"
			  "--only-bundled-plugins: Only load included (first-party) plugins\n"
			  "--disable-shutdown-check: Disable unclean shutdown detection.\n"
			  "--verbose: Make log more verbose.\n"
			  "--always-on-top: Start in 'always on top' mode.\n\n"
			  "--unfiltered_log: Make log unfiltered.\n\n"
			  "--disable-updater: Disable built-in updater (Windows/Mac only)\n\n"
			  "--disable-missing-files-check: Disable the missing files dialog which can appear on startup.\n\n";

#ifdef _WIN32
			MessageBoxA(NULL, help.c_str(), "Help", MB_OK | MB_ICONASTERISK);
#else
			std::cout << help << "--version, -V: Get current version.\n";
#endif
			exit(0);

		} else if (arg_is(argv[i], "--version", "-V")) {
			std::cout << "App - " << VERSION << "\n";
			exit(0);
		}
	}
	log_verbose = launchOptions.log_verbose;
	unfiltered_log = launchOptions.unfiltered_log;

	upgrade_settings();

	std::fstream logFile;

	int ret = RunMain(logFile, argc, argv);

  Quit();

	if (hRtwq) {
		typedef HRESULT(STDAPICALLTYPE * PFN_RtwqShutdown)();
		PFN_RtwqShutdown func = (PFN_RtwqShutdown)GetProcAddress(hRtwq, "RtwqShutdown");
		func();
		FreeLibrary(hRtwq);
	}

	log_blocked_dlls();

	blog(LOG_INFO, "Number of memory leaks: %ld", bnum_allocs());
	base_set_log_handler(nullptr, nullptr);

	return ret;
}

void App::Quit() {
	obs_hotkey_set_callback_routing_func(nullptr, nullptr);

	sceneSourceManager.reset();
	service = nullptr;
	outputManager.reset();

	bool disableAudioDucking = config_get_bool(globalConfig, "Audio", "DisableAudioDucking");
	if (disableAudioDucking)
		DisableAudioDucking(false);

	os_inhibit_sleep_set_active(sleepInhibitor, false);
	os_inhibit_sleep_destroy(sleepInhibitor);

	if (libobs_initialized)
		obs_shutdown();
}

int App::RunMain(std::fstream& logFile, int argc, char* argv[]) {
	int ret = -1;

	auto profilerNameStore = CreateNameStore();

	/*std::unique_ptr<void, decltype(ProfilerFree)> prof_release(
	  static_cast<void*>(&ProfilerFree), ProfilerFree);

	profiler_start();*/
	profile_register_root(run_program_init, 0);

	ScopeProfiler prof{run_program_init};

	this->profilerNameStore = profilerNameStore.get();

	sleepInhibitor = os_inhibit_sleep_create("OBS Video/audio");

	AppInit();

	delete_oldest_file(false, "obs-studio/profiler_data");

	bool created_log = false;
	if (!created_log) {
		create_log_file(logFile);
		created_log = true;
	}

	if (argc > 1) {
		std::stringstream stor;
		stor << argv[1];
		for (int i = 2; i < argc; ++i) { stor << " " << argv[i]; }
		blog(LOG_INFO, "Command Line Arguments: %s", stor.str().c_str());
	}

	ret = OBSInit();

	prof.Stop();

	ret = application->Execute();

	return ret;
}

void App::AppInit() {
	ProfileScope("OBSApp::AppInit");

	if (!MakeUserDirs())
		throw "Failed to create required user directories";
	if (!InitGlobalConfig())
		throw "Failed to initialize global config";
	if (!InitLocale())
		throw "Failed to load locale";

  blog(LOG_INFO, "start set default profiles");

	config_set_default_string(globalConfig, "Basic", "Profile", Str("Untitled"));
	config_set_default_string(globalConfig, "Basic", "ProfileDir", Str("Untitled"));
	config_set_default_string(globalConfig, "Basic", "SceneCollection", Str("Untitled"));
	config_set_default_string(globalConfig, "Basic", "SceneCollectionFile", Str("Untitled"));
	config_set_default_bool(globalConfig, "Basic", "ConfigOnNewProfile", true);

	if (!config_has_user_value(globalConfig, "Basic", "Profile")) {
		config_set_string(globalConfig, "Basic", "Profile", Str("Untitled"));
		config_set_string(globalConfig, "Basic", "ProfileDir", Str("Untitled"));
	}

	if (!config_has_user_value(globalConfig, "Basic", "SceneCollection")) {
		config_set_string(globalConfig, "Basic", "SceneCollection", Str("Untitled"));
		config_set_string(globalConfig, "Basic", "SceneCollectionFile", Str("Untitled"));
	}

#ifdef _WIN32
	bool disableAudioDucking = config_get_bool(globalConfig, "Audio", "DisableAudioDucking");
	if (disableAudioDucking)
		DisableAudioDucking(true);
#endif

	move_basic_to_profiles();
	move_basic_to_scene_collections();

	if (!MakeUserProfileDirs())
		throw "Failed to create profile directories";
}

bool App::InitGlobalConfig() {
	char path[512];
	bool changed = false;

	int len = GetConfigPath(path, sizeof(path), "obs-studio/global.ini");
	if (len <= 0) {
		return false;
	}

	int errorcode = globalConfig.Open(path, CONFIG_OPEN_ALWAYS);
	if (errorcode != CONFIG_SUCCESS) {
		blog(LOG_ERROR, "Failed to open global.ini: %d", errorcode);
		return false;
	}

	if (!launchOptions.opt_starting_collection.empty()) {
		std::string path =
		  GetSceneCollectionFileFromName(launchOptions.opt_starting_collection.c_str());
		if (!path.empty()) {
			config_set_string(globalConfig, "Basic", "SceneCollection",
					  launchOptions.opt_starting_collection.c_str());
			config_set_string(globalConfig, "Basic", "SceneCollectionFile",
					  path.c_str());
			changed = true;
		}
	}

	if (!launchOptions.opt_starting_profile.empty()) {
		std::string path =
		  GetProfileDirFromName(launchOptions.opt_starting_profile.c_str());
		if (!path.empty()) {
			config_set_string(globalConfig, "Basic", "Profile",
					  launchOptions.opt_starting_profile.c_str());
			config_set_string(globalConfig, "Basic", "ProfileDir", path.c_str());
			changed = true;
		}
	}

	auto lastVersion = config_get_int(globalConfig, "General", "LastVersion");

	if (!config_has_user_value(globalConfig, "General", "Pre19Defaults")) {
		bool useOldDefaults = lastVersion && lastVersion < MAKE_SEMANTIC_VERSION(19, 0, 0);

		config_set_bool(globalConfig, "General", "Pre19Defaults", useOldDefaults);
		changed = true;
	}

	if (!config_has_user_value(globalConfig, "General", "Pre21Defaults")) {
		bool useOldDefaults = lastVersion && lastVersion < MAKE_SEMANTIC_VERSION(21, 0, 0);

		config_set_bool(globalConfig, "General", "Pre21Defaults", useOldDefaults);
		changed = true;
	}

	if (!config_has_user_value(globalConfig, "General", "Pre23Defaults")) {
		bool useOldDefaults = lastVersion && lastVersion < MAKE_SEMANTIC_VERSION(23, 0, 0);

		config_set_bool(globalConfig, "General", "Pre23Defaults", useOldDefaults);
		changed = true;
	}

#define PRE_24_1_DEFS "Pre24.1Defaults"
	if (!config_has_user_value(globalConfig, "General", PRE_24_1_DEFS)) {
		bool useOldDefaults = lastVersion && lastVersion < MAKE_SEMANTIC_VERSION(24, 1, 0);

		config_set_bool(globalConfig, "General", PRE_24_1_DEFS, useOldDefaults);
		changed = true;
	}
#undef PRE_24_1_DEFS

	if (config_has_user_value(globalConfig, "BasicWindow", "MultiviewLayout")) {
		const char* layout =
		  config_get_string(globalConfig, "BasicWindow", "MultiviewLayout");
	}

	if (lastVersion && lastVersion < MAKE_SEMANTIC_VERSION(24, 0, 0)) {
		bool disableHotkeysInFocus =
		  config_get_bool(globalConfig, "General", "DisableHotkeysInFocus");
		if (disableHotkeysInFocus)
			config_set_string(globalConfig, "General", "HotkeyFocusType",
					  "DisableHotkeysInFocus");
		changed = true;
	}

	if (changed)
		config_save_safe(globalConfig, "tmp", nullptr);

	return InitGlobalConfigDefaults();
}

bool App::InitGlobalConfigDefaults() {
	config_set_default_uint(globalConfig, "General", "MaxLogs", 10);
	config_set_default_int(globalConfig, "General", "InfoIncrement", -1);
	config_set_default_string(globalConfig, "General", "ProcessPriority", "Normal");
	config_set_default_bool(globalConfig, "General", "EnableAutoUpdates", true);

	config_set_default_bool(globalConfig, "General", "ConfirmOnExit", true);

#if _WIN32
	config_set_default_string(globalConfig, "Video", "Renderer", "Direct3D 11");
#else
	config_set_default_string(globalConfig, "Video", "Renderer", "OpenGL");
#endif

	config_set_default_bool(globalConfig, "BasicWindow", "PreviewEnabled", true);
	config_set_default_bool(globalConfig, "BasicWindow", "PreviewProgramMode", false);
	config_set_default_bool(globalConfig, "BasicWindow", "SceneDuplicationMode", true);
	config_set_default_bool(globalConfig, "BasicWindow", "SwapScenesMode", true);
	config_set_default_bool(globalConfig, "BasicWindow", "SnappingEnabled", true);
	config_set_default_bool(globalConfig, "BasicWindow", "ScreenSnapping", true);
	config_set_default_bool(globalConfig, "BasicWindow", "SourceSnapping", true);
	config_set_default_bool(globalConfig, "BasicWindow", "CenterSnapping", false);
	config_set_default_double(globalConfig, "BasicWindow", "SnapDistance", 10.0);
	config_set_default_bool(globalConfig, "BasicWindow", "SpacingHelpersEnabled", true);
	config_set_default_bool(globalConfig, "BasicWindow", "RecordWhenStreaming", false);
	config_set_default_bool(globalConfig, "BasicWindow", "KeepRecordingWhenStreamStops", false);
	config_set_default_bool(globalConfig, "BasicWindow", "SysTrayEnabled", true);
	config_set_default_bool(globalConfig, "BasicWindow", "SysTrayWhenStarted", false);
	config_set_default_bool(globalConfig, "BasicWindow", "SaveProjectors", false);
	config_set_default_bool(globalConfig, "BasicWindow", "ShowTransitions", true);
	config_set_default_bool(globalConfig, "BasicWindow", "ShowListboxToolbars", true);
	config_set_default_bool(globalConfig, "BasicWindow", "ShowStatusBar", true);
	config_set_default_bool(globalConfig, "BasicWindow", "ShowSourceIcons", true);
	config_set_default_bool(globalConfig, "BasicWindow", "ShowContextToolbars", true);
	config_set_default_bool(globalConfig, "BasicWindow", "StudioModeLabels", true);

	config_set_default_string(globalConfig, "General", "HotkeyFocusType",
				  "NeverDisableHotkeys");

	config_set_default_bool(globalConfig, "BasicWindow", "VerticalVolControl", false);

	config_set_default_bool(globalConfig, "BasicWindow", "MultiviewMouseSwitch", true);

	config_set_default_bool(globalConfig, "BasicWindow", "MultiviewDrawNames", true);

	config_set_default_bool(globalConfig, "BasicWindow", "MultiviewDrawAreas", true);

#ifdef _WIN32
	config_set_default_bool(globalConfig, "Audio", "DisableAudioDucking", true);
	config_set_default_bool(globalConfig, "General", "BrowserHWAccel", true);
#endif

#ifdef __APPLE__
	config_set_default_bool(globalConfig, "General", "BrowserHWAccel", true);
	config_set_default_bool(globalConfig, "Video", "DisableOSXVSync", true);
	config_set_default_bool(globalConfig, "Video", "ResetOSXVSyncOnExit", true);
#endif

	config_set_default_bool(globalConfig, "BasicWindow", "MediaControlsCountdownTimer", true);

	return true;
}

bool App::InitLocale() {
	ProfileScope("OBSApp::InitLocale");

	const char* lang = config_get_string(globalConfig, "General", "Language");
	bool userLocale = config_has_user_value(globalConfig, "General", "Language");
	if (!userLocale || !lang || lang[0] == '\0')
		lang = DEFAULT_LANG;

	locale = lang;

	std::string englishPath;
	if (!GetDataFilePath("locale/" DEFAULT_LANG ".ini", englishPath)) {
		return false;
	}

	textLookup = text_lookup_create(englishPath.c_str());
	if (!textLookup) {
		return false;
	}

	bool defaultLang = astrcmpi(lang, DEFAULT_LANG) == 0;

	if (userLocale && defaultLang)
		return true;

	if (!userLocale && defaultLang) {
		for (auto& locale_ : GetPreferredLocales()) {
			if (locale_ == lang)
				return true;

			std::stringstream file;
			file << "locale/" << locale_ << ".ini";

			std::string path;
			if (!GetDataFilePath(file.str().c_str(), path))
				continue;

			if (!text_lookup_add(textLookup, path.c_str()))
				continue;

			blog(LOG_INFO, "Using preferred locale '%s'", locale_.c_str());
			locale = locale_;

			return true;
		}

		return true;
	}

	std::stringstream file;
	file << "locale/" << lang << ".ini";

	std::string path;
	if (GetDataFilePath(file.str().c_str(), path)) {
		if (!text_lookup_add(textLookup, path.c_str()))
			blog(LOG_ERROR, "Failed to add locale file '%s'", path.c_str());
	} else {
		blog(LOG_ERROR, "Could not find locale file '%s'", file.str().c_str());
	}

	return true;
}

bool App::OBSInit() {
	ProfileScope("OBSApp::OBSInit");

	if (!StartupOBS(locale.c_str(), GetProfilerNameStore()))
		return false;

	libobs_initialized = true;

	// tell application instance ready to load config & profiles
	application->OnConfigureBegin();

	const char* sceneCollection =
	  config_get_string(GetGlobalConfig(), "Basic", "SceneCollectionFile");
	char savePath[1024];
	char fileName[1024];
	int ret;

	if (!sceneCollection)
		throw "Failed to get scene collection name";

	ret =
	  snprintf(fileName, sizeof(fileName), "obs-studio/basic/scenes/%s.json", sceneCollection);
	if (ret <= 0)
		throw "Failed to create scene collection file name";

	ret = GetConfigPath(savePath, sizeof(savePath), fileName);
	if (ret <= 0)
		throw "Failed to get scene collection json file path";

	if (!InitBasicConfig())
		throw "Failed to load basic.ini";
	if (!ResetAudio())
		throw "Failed to initialize audio";

	auto base_width = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCX");
	auto base_height = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCY");
	if (base_width == 0 || base_height == 0) {
		ret = ResetVideo();
	} else {
		ret = ResetVideo(base_width, base_height);
	}

	switch (ret) {
	case OBS_VIDEO_MODULE_NOT_FOUND:
		throw "Failed to initialize video:  Graphics module not found";
	case OBS_VIDEO_NOT_SUPPORTED: throw UNSUPPORTED_ERROR;
	case OBS_VIDEO_INVALID_PARAM: throw "Failed to initialize video:  Invalid parameters";
	default:
		if (ret != OBS_VIDEO_SUCCESS)
			throw UNKNOWN_ERROR;
	}

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

	// register source signal handler
	sceneSourceManager = std::make_unique<SceneSourceManager>();

	struct obs_module_failure_info mfi;

	AddExtraModulePaths();

	blog(LOG_INFO, "---------------------------------");
	obs_load_all_modules2(&mfi);
	blog(LOG_INFO, "---------------------------------");
	obs_log_loaded_modules();
	blog(LOG_INFO, "---------------------------------");
	obs_post_load_modules();

	BPtr<char*> failed_modules = mfi.failed_modules;
	OBSDataAutoRelease obsData = obs_get_private_data();
  vcamEnabled = obs_data_get_bool(obsData, "vcamEnabled");

	InitBasicConfigDefaults2();

	CheckForSimpleModeX264Fallback();

	blog(LOG_INFO, STARTUP_SEPARATOR);

	if (!InitService())
		throw "Failed to initialize service";

	ResetOutputs();

	{
		ProfileScope("OBSBasic::Load");
		disableSaving--;
		Load(savePath);
		disableSaving++;
	}

	loaded = true;

	previewEnabled = config_get_bool(GetGlobalConfig(), "BasicWindow", "PreviewEnabled");

	// notify UI to show preview

	RefreshProfiles();
	disableSaving--;

	// begin to render

	bool first_run = config_get_bool(GetGlobalConfig(), "General", "FirstRun");
	if (!first_run) {
		config_set_bool(GetGlobalConfig(), "General", "FirstRun", true);
		config_save_safe(GetGlobalConfig(), "tmp", nullptr);
	}

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_FINISHED_LOADING);

	// tell application instance ready to update UI
	application->OnConfigureFinished();

	return true;
}

bool App::InitBasicConfig() {
	ProfileScope("MainWindow::InitBasicConfig");

	char configPath[512];

	int ret = GetProfilePath(configPath, sizeof(configPath), "");
	if (ret <= 0) {
		blog(LOG_ERROR, "Failed to get profile path");
		return false;
	}

	if (os_mkdir(configPath) == MKDIR_ERROR) {
		blog(LOG_ERROR, "Failed to create profile path");
		return false;
	}

	ret = GetProfilePath(configPath, sizeof(configPath), "basic.ini");
	if (ret <= 0) {
		blog(LOG_ERROR, "Failed to get basic.ini path");
		return false;
	}

	int code = basicConfig.Open(configPath, CONFIG_OPEN_ALWAYS);
	if (code != CONFIG_SUCCESS) {
		blog(LOG_ERROR, "Failed to open basic.ini: %d", code);
		return false;
	}

	if (config_get_string(basicConfig, "General", "Name") == nullptr) {
		const char* curName = config_get_string(GetGlobalConfig(), "Basic", "Profile");

		config_set_string(basicConfig, "General", "Name", curName);
		basicConfig.SaveSafe("tmp");
	}

	return InitBasicConfigDefaults();
}

bool App::InitBasicConfigDefaults() {
	uint32_t cx = 1920;
	uint32_t cy = 1080;

	bool changed = false;

	/* ----------------------------------------------------- */
	/* move over old FFmpeg track settings                   */
	if (config_has_user_value(basicConfig, "AdvOut", "FFAudioTrack") &&
	    !config_has_user_value(basicConfig, "AdvOut", "Pre22.1Settings")) {

		int track = (int)config_get_int(basicConfig, "AdvOut", "FFAudioTrack");
		config_set_int(basicConfig, "AdvOut", "FFAudioMixes", 1LL << (track - 1));
		config_set_bool(basicConfig, "AdvOut", "Pre22.1Settings", true);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* move over mixer values in advanced if older config */
	if (config_has_user_value(basicConfig, "AdvOut", "RecTrackIndex") &&
	    !config_has_user_value(basicConfig, "AdvOut", "RecTracks")) {

		uint64_t track = config_get_uint(basicConfig, "AdvOut", "RecTrackIndex");
		track = 1ULL << (track - 1);
		config_set_uint(basicConfig, "AdvOut", "RecTracks", track);
		config_remove_value(basicConfig, "AdvOut", "RecTrackIndex");
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* move bitrate enforcement setting to new value         */
	if (config_has_user_value(basicConfig, "SimpleOutput", "EnforceBitrate") &&
	    !config_has_user_value(basicConfig, "Stream1", "IgnoreRecommended") &&
	    !config_has_user_value(basicConfig, "Stream1", "MovedOldEnforce")) {
		bool enforce = config_get_bool(basicConfig, "SimpleOutput", "EnforceBitrate");
		config_set_bool(basicConfig, "Stream1", "IgnoreRecommended", !enforce);
		config_set_bool(basicConfig, "Stream1", "MovedOldEnforce", true);
		changed = true;
	}

	/* ----------------------------------------------------- */
	/* enforce minimum retry delay of 1 second prior to 27.1 */
	if (config_has_user_value(basicConfig, "Output", "RetryDelay")) {
		auto retryDelay = config_get_uint(basicConfig, "Output", "RetryDelay");
		if (retryDelay < 1) {
			config_set_uint(basicConfig, "Output", "RetryDelay", 1);
			changed = true;
		}
	}

	/* ----------------------------------------------------- */
	/* Migrate old container selection (if any) to new key.  */

	auto MigrateFormat = [&](const char* section) {
		bool has_old_key = config_has_user_value(basicConfig, section, "RecFormat");
		bool has_new_key = config_has_user_value(basicConfig, section, "RecFormat2");
		if (!has_new_key && !has_old_key)
			return;

		std::string old_format =
		  config_get_string(basicConfig, section, has_new_key ? "RecFormat2" : "RecFormat");
		std::string new_format = old_format;
		if (old_format == "ts")
			new_format = "mpegts";
		else if (old_format == "m3u8")
			new_format = "hls";
		else if (old_format == "fmp4")
			new_format = "fragmented_mp4";
		else if (old_format == "fmov")
			new_format = "fragmented_mov";

		if (new_format != old_format || !has_new_key) {
			config_set_string(basicConfig, section, "RecFormat2", new_format.c_str());
			changed = true;
		}
	};

	MigrateFormat("AdvOut");
	MigrateFormat("SimpleOutput");

	/* ----------------------------------------------------- */

	if (changed)
		config_save_safe(basicConfig, "tmp", nullptr);

	/* ----------------------------------------------------- */

	config_set_default_string(basicConfig, "Output", "Mode", "Simple");

	config_set_default_bool(basicConfig, "Stream1", "IgnoreRecommended", false);

	config_set_default_string(basicConfig, "SimpleOutput", "FilePath",
				  GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "SimpleOutput", "RecFormat2", DEFAULT_CONTAINER);
	config_set_default_uint(basicConfig, "SimpleOutput", "VBitrate", 2500);
	config_set_default_uint(basicConfig, "SimpleOutput", "ABitrate", 160);
	config_set_default_bool(basicConfig, "SimpleOutput", "UseAdvanced", false);
	config_set_default_string(basicConfig, "SimpleOutput", "Preset", "veryfast");
	config_set_default_string(basicConfig, "SimpleOutput", "NVENCPreset2", "p5");
	config_set_default_string(basicConfig, "SimpleOutput", "RecQuality", "Small");
	config_set_default_bool(basicConfig, "SimpleOutput", "RecRB", false);
	config_set_default_int(basicConfig, "SimpleOutput", "RecRBTime", 20);
	config_set_default_int(basicConfig, "SimpleOutput", "RecRBSize", 512);
	config_set_default_string(basicConfig, "SimpleOutput", "RecRBPrefix", "Replay");
	config_set_default_string(basicConfig, "SimpleOutput", "StreamAudioEncoder", "aac");
	config_set_default_string(basicConfig, "SimpleOutput", "RecAudioEncoder", "aac");
	config_set_default_uint(basicConfig, "SimpleOutput", "RecTracks", (1 << 0));

	config_set_default_bool(basicConfig, "AdvOut", "ApplyServiceSettings", true);
	config_set_default_bool(basicConfig, "AdvOut", "UseRescale", false);
	config_set_default_uint(basicConfig, "AdvOut", "TrackIndex", 1);
	config_set_default_uint(basicConfig, "AdvOut", "VodTrackIndex", 2);
	config_set_default_string(basicConfig, "AdvOut", "Encoder", "obs_x264");

	config_set_default_string(basicConfig, "AdvOut", "RecType", "Standard");

	config_set_default_string(basicConfig, "AdvOut", "RecFilePath",
				  GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "RecFormat2", DEFAULT_CONTAINER);
	config_set_default_bool(basicConfig, "AdvOut", "RecUseRescale", false);
	config_set_default_uint(basicConfig, "AdvOut", "RecTracks", (1 << 0));
	config_set_default_string(basicConfig, "AdvOut", "RecEncoder", "none");
	config_set_default_uint(basicConfig, "AdvOut", "FLVTrack", 1);

	config_set_default_bool(basicConfig, "AdvOut", "FFOutputToFile", true);
	config_set_default_string(basicConfig, "AdvOut", "FFFilePath",
				  GetDefaultVideoSavePath().c_str());
	config_set_default_string(basicConfig, "AdvOut", "FFExtension", "mp4");
	config_set_default_uint(basicConfig, "AdvOut", "FFVBitrate", 2500);
	config_set_default_uint(basicConfig, "AdvOut", "FFVGOPSize", 250);
	config_set_default_bool(basicConfig, "AdvOut", "FFUseRescale", false);
	config_set_default_bool(basicConfig, "AdvOut", "FFIgnoreCompat", false);
	config_set_default_uint(basicConfig, "AdvOut", "FFABitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "FFAudioMixes", 1);

	config_set_default_uint(basicConfig, "AdvOut", "Track1Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track2Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track3Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track4Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track5Bitrate", 160);
	config_set_default_uint(basicConfig, "AdvOut", "Track6Bitrate", 160);

	config_set_default_uint(basicConfig, "AdvOut", "RecSplitFileTime", 15);
	config_set_default_uint(basicConfig, "AdvOut", "RecSplitFileSize", 2048);

	config_set_default_bool(basicConfig, "AdvOut", "RecRB", false);
	config_set_default_uint(basicConfig, "AdvOut", "RecRBTime", 20);
	config_set_default_int(basicConfig, "AdvOut", "RecRBSize", 512);

	config_set_default_uint(basicConfig, "Video", "BaseCX", cx);
	config_set_default_uint(basicConfig, "Video", "BaseCY", cy);

	/* don't allow BaseCX/BaseCY to be susceptible to defaults changing */
	if (!config_has_user_value(basicConfig, "Video", "BaseCX") ||
	    !config_has_user_value(basicConfig, "Video", "BaseCY")) {
		config_set_uint(basicConfig, "Video", "BaseCX", cx);
		config_set_uint(basicConfig, "Video", "BaseCY", cy);
		config_save_safe(basicConfig, "tmp", nullptr);
	}

	config_set_default_string(basicConfig, "Output", "FilenameFormatting",
				  "%CCYY-%MM-%DD %hh-%mm-%ss");

	config_set_default_bool(basicConfig, "Output", "DelayEnable", false);
	config_set_default_uint(basicConfig, "Output", "DelaySec", 20);
	config_set_default_bool(basicConfig, "Output", "DelayPreserve", true);

	config_set_default_bool(basicConfig, "Output", "Reconnect", true);
	config_set_default_uint(basicConfig, "Output", "RetryDelay", 2);
	config_set_default_uint(basicConfig, "Output", "MaxRetries", 25);

	config_set_default_string(basicConfig, "Output", "BindIP", "default");
	config_set_default_string(basicConfig, "Output", "IPFamily", "IPv4+IPv6");
	config_set_default_bool(basicConfig, "Output", "NewSocketLoopEnable", false);
	config_set_default_bool(basicConfig, "Output", "LowLatencyEnable", false);

	uint32_t scale_cx = cx;
	uint32_t scale_cy = cy;

	config_set_default_uint(basicConfig, "Video", "OutputCX", scale_cx);
	config_set_default_uint(basicConfig, "Video", "OutputCY", scale_cy);

	/* don't allow OutputCX/OutputCY to be susceptible to defaults
   * changing */
	if (!config_has_user_value(basicConfig, "Video", "OutputCX") ||
	    !config_has_user_value(basicConfig, "Video", "OutputCY")) {
		config_set_uint(basicConfig, "Video", "OutputCX", scale_cx);
		config_set_uint(basicConfig, "Video", "OutputCY", scale_cy);
		config_save_safe(basicConfig, "tmp", nullptr);
	}

	config_set_default_uint(basicConfig, "Video", "FPSType", 0);
	config_set_default_string(basicConfig, "Video", "FPSCommon", "30");
	config_set_default_uint(basicConfig, "Video", "FPSInt", 30);
	config_set_default_uint(basicConfig, "Video", "FPSNum", 30);
	config_set_default_uint(basicConfig, "Video", "FPSDen", 1);
	config_set_default_string(basicConfig, "Video", "ScaleType", "bicubic");
	config_set_default_string(basicConfig, "Video", "ColorFormat", "NV12");
	config_set_default_string(basicConfig, "Video", "ColorSpace", "709");
	config_set_default_string(basicConfig, "Video", "ColorRange", "Partial");
	config_set_default_uint(basicConfig, "Video", "SdrWhiteLevel", 300);
	config_set_default_uint(basicConfig, "Video", "HdrNominalPeakLevel", 1000);

	config_set_default_string(basicConfig, "Audio", "MonitoringDeviceId", "default");
	config_set_default_string(basicConfig, "Audio", "MonitoringDeviceName",
				  Str("Basic.Settings.Advanced.Audio.MonitoringDevice"
				      ".Default"));
	config_set_default_uint(basicConfig, "Audio", "SampleRate", 48000);
	config_set_default_string(basicConfig, "Audio", "ChannelSetup", "Stereo");
	config_set_default_double(basicConfig, "Audio", "MeterDecayRate", 23.53);
	config_set_default_uint(basicConfig, "Audio", "PeakMeterType", 0);

	config_set_bool(GetGlobalConfig(), "BasicWindow", "HideOBSWindowsFromCapture", true);

	return true;
}

void App::InitBasicConfigDefaults2() {
	bool oldEncDefaults = config_get_bool(GetGlobalConfig(), "General", "Pre23Defaults");
	bool useNV = EncoderAvailable("ffmpeg_nvenc") && !oldEncDefaults;

	config_set_default_string(basicConfig, "SimpleOutput", "StreamEncoder",
				  useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);
	config_set_default_string(basicConfig, "SimpleOutput", "RecEncoder",
				  useNV ? SIMPLE_ENCODER_NVENC : SIMPLE_ENCODER_X264);

	const char* aac_default = "ffmpeg_aac";
	if (EncoderAvailable("CoreAudio_AAC"))
		aac_default = "CoreAudio_AAC";
	else if (EncoderAvailable("libfdk_aac"))
		aac_default = "libfdk_aac";

	config_set_default_string(basicConfig, "AdvOut", "AudioEncoder", aac_default);
	config_set_default_string(basicConfig, "AdvOut", "RecAudioEncoder", aac_default);

	if (update_nvenc_presets(basicConfig))
		config_save_safe(basicConfig, "tmp", nullptr);
}

int App::GetProfilePath(char* path, size_t size, const char* file) const {
	char profiles_path[512];
	const char* profile = config_get_string(GetGlobalConfig(), "Basic", "ProfileDir");
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

void App::ResetOutputs() {
	ProfileScope("MainWindow::ResetOutputs");

	const char* mode = config_get_string(basicConfig, "Output", "Mode");
	bool advOut = astrcmpi(mode, "Advanced") == 0;

	if (outputManager == nullptr || outputManager->Active()) {
    outputManager.reset();
		outputManager = std::make_unique<OutputManager>();
		std::unique_ptr<BasicOutputHandler> handler;
		if (advOut) {
			handler.reset(CreateAdvancedOutputHandler(outputManager.get()));
		} else {
			handler.reset(CreateSimpleOutputHandler(outputManager.get()));
		}
		outputManager->SetOutputHandler(std::move(handler));
	} else {
		outputManager->Update();
	}
}

int App::ResetVideo(int width, int height) {
	if (outputManager != nullptr) {
		if (outputManager->Active()) {
			return OBS_VIDEO_CURRENTLY_ACTIVE;
		}
	}

	ProfileScope("MainWindow::ResetVideo");

	struct obs_video_info ovi;
	int ret;

	GetConfigFPS(ovi.fps_num, ovi.fps_den);

	const char* colorFormat = config_get_string(basicConfig, "Video", "ColorFormat");
	const char* colorSpace = config_get_string(basicConfig, "Video", "ColorSpace");
	const char* colorRange = config_get_string(basicConfig, "Video", "ColorRange");

	ovi.graphics_module = GetRenderModule();
	ovi.base_width = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCX");
	ovi.base_height = (uint32_t)config_get_uint(basicConfig, "Video", "BaseCY");
	ovi.output_width = (uint32_t)config_get_uint(basicConfig, "Video", "OutputCX");
	ovi.output_height = (uint32_t)config_get_uint(basicConfig, "Video", "OutputCY");
	ovi.output_format = GetVideoFormatFromName(colorFormat);
	ovi.colorspace = GetVideoColorSpaceFromName(colorSpace);
	ovi.range = astrcmpi(colorRange, "Full") == 0 ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
	ovi.adapter = config_get_uint(GetGlobalConfig(), "Video", "AdapterIdx");
	ovi.gpu_conversion = true;
	ovi.scale_type = GetScaleType(basicConfig);

	bool need_update = (ovi.base_width != width || ovi.base_height != height);

	if (ovi.base_width < 32 || ovi.base_height < 32 || need_update) {
		ovi.base_width = width;
		ovi.base_height = height;
		config_set_uint(basicConfig, "Video", "BaseCX", width);
		config_set_uint(basicConfig, "Video", "BaseCY", height);
	}

	if (ovi.output_width < 32 || ovi.output_height < 32 || need_update) {
		ovi.output_width = ovi.base_width;
		ovi.output_height = ovi.base_height;
		config_set_uint(basicConfig, "Video", "OutputCX", ovi.base_width);
		config_set_uint(basicConfig, "Video", "OutputCY", ovi.base_height);
	}

	ret = obs_reset_video(&ovi);
	if (ret == OBS_VIDEO_CURRENTLY_ACTIVE) {
		blog(LOG_WARNING, "Tried to reset when already active");
		return ret;
	}

	if (ret == OBS_VIDEO_SUCCESS) {
		const float sdr_white_level =
		  (float)config_get_uint(basicConfig, "Video", "SdrWhiteLevel");
		const float hdr_nominal_peak_level =
		  (float)config_get_uint(basicConfig, "Video", "HdrNominalPeakLevel");
		obs_set_video_levels(sdr_white_level, hdr_nominal_peak_level);
	}

	return ret;
}

bool App::ResetAudio() {
	ProfileScope("MainWindow::ResetAudio");

	struct obs_audio_info2 ai = {};
	ai.samples_per_sec = config_get_uint(basicConfig, "Audio", "SampleRate");

	const char* channelSetupStr = config_get_string(basicConfig, "Audio", "ChannelSetup");

	if (strcmp(channelSetupStr, "Mono") == 0)
		ai.speakers = SPEAKERS_MONO;
	else if (strcmp(channelSetupStr, "2.1") == 0)
		ai.speakers = SPEAKERS_2POINT1;
	else if (strcmp(channelSetupStr, "4.0") == 0)
		ai.speakers = SPEAKERS_4POINT0;
	else if (strcmp(channelSetupStr, "4.1") == 0)
		ai.speakers = SPEAKERS_4POINT1;
	else if (strcmp(channelSetupStr, "5.1") == 0)
		ai.speakers = SPEAKERS_5POINT1;
	else if (strcmp(channelSetupStr, "7.1") == 0)
		ai.speakers = SPEAKERS_7POINT1;
	else
		ai.speakers = SPEAKERS_STEREO;

	bool lowLatencyAudioBuffering =
	  config_get_bool(GetGlobalConfig(), "Audio", "LowLatencyAudioBuffering");
	if (lowLatencyAudioBuffering) {
		ai.max_buffering_ms = 20;
		ai.fixed_buffering = true;
	}

	return obs_reset_audio2(&ai);
}

void App::GetFPSInteger(uint32_t& num, uint32_t& den) const {
	num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSInt");
	den = 1;
}

void App::GetFPSFraction(uint32_t& num, uint32_t& den) const {
	num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNum");
	den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSDen");
}

void App::GetFPSNanoseconds(uint32_t& num, uint32_t& den) const {
	num = 1000000000;
	den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNS");
}

void App::GetConfigFPS(uint32_t& num, uint32_t& den) const {
	auto type = config_get_uint(basicConfig, "Video", "FPSType");

	if (type == 1) //"Integer"
		GetFPSInteger(num, den);
	else if (type == 2) //"Fraction"
		GetFPSFraction(num, den);
	/*
   * 	else if (false) //"Nanoseconds", currently not implemented
   *		GetFPSNanoseconds(num, den);
   */
	else
		GetFPSCommon(num, den);
}

void App::GetFPSCommon(uint32_t& num, uint32_t& den) const {
	const char* val = config_get_string(basicConfig, "Video", "FPSCommon");

	if (strcmp(val, "10") == 0) {
		num = 10;
		den = 1;
	} else if (strcmp(val, "20") == 0) {
		num = 20;
		den = 1;
	} else if (strcmp(val, "24 NTSC") == 0) {
		num = 24000;
		den = 1001;
	} else if (strcmp(val, "25 PAL") == 0) {
		num = 25;
		den = 1;
	} else if (strcmp(val, "29.97") == 0) {
		num = 30000;
		den = 1001;
	} else if (strcmp(val, "48") == 0) {
		num = 48;
		den = 1;
	} else if (strcmp(val, "50 PAL") == 0) {
		num = 50;
		den = 1;
	} else if (strcmp(val, "59.94") == 0) {
		num = 60000;
		den = 1001;
	} else if (strcmp(val, "60") == 0) {
		num = 60;
		den = 1;
	} else {
		num = 30;
		den = 1;
	}
}

const char* App::GetRenderModule() const {
	const char* renderer = config_get_string(globalConfig, "Video", "Renderer");
	return (astrcmpi(renderer, "Direct3D 11") == 0) ? DL_D3D11 : DL_OPENGL;
}

obs_service_t* App::GetService() {
	if (!service) {
		service = obs_service_create("rtmp_common", NULL, NULL, nullptr);
		obs_service_release(service);
	}
	return service;
}

void App::SetService(obs_service_t* newService) {
	if (newService) {
		service = newService;
	}
}

void App::SaveService() {
	if (!service)
		return;

	char serviceJsonPath[512];
	int ret = GetProfilePath(serviceJsonPath, sizeof(serviceJsonPath), SERVICE_PATH);
	if (ret <= 0)
		return;

	OBSDataAutoRelease data = obs_data_create();
	OBSDataAutoRelease settings = obs_service_get_settings(service);

	obs_data_set_string(data, "type", obs_service_get_type(service));
	obs_data_set_obj(data, "settings", settings);

	if (!obs_data_save_json_safe(data, serviceJsonPath, "tmp", "bak"))
		blog(LOG_WARNING, "Failed to save service");
}

bool App::LoadService() {
	const char* type;

	char serviceJsonPath[512];
	int ret = GetProfilePath(serviceJsonPath, sizeof(serviceJsonPath), SERVICE_PATH);
	if (ret <= 0)
		return false;

	OBSDataAutoRelease data = obs_data_create_from_json_file_safe(serviceJsonPath, "bak");

	if (!data)
		return false;

	obs_data_set_default_string(data, "type", "rtmp_common");
	type = obs_data_get_string(data, "type");

	OBSDataAutoRelease settings = obs_data_get_obj(data, "settings");
	OBSDataAutoRelease hotkey_data = obs_data_get_obj(data, "hotkeys");

	service = obs_service_create(type, "default_service", settings, hotkey_data);
	obs_service_release(service);

	if (!service)
		return false;

	/* Enforce Opus on FTL if needed */
	if (strcmp(obs_service_get_protocol(service), "FTL") == 0 ||
	    strcmp(obs_service_get_protocol(service), "WHIP") == 0) {
		const char* option =
		  config_get_string(basicConfig, "SimpleOutput", "StreamAudioEncoder");
		if (strcmp(option, "opus") != 0)
			config_set_string(basicConfig, "SimpleOutput", "StreamAudioEncoder",
					  "opus");

		option = config_get_string(basicConfig, "AdvOut", "AudioEncoder");
		if (strcmp(obs_get_encoder_codec(option), "opus") != 0)
			config_set_string(basicConfig, "AdvOut", "AudioEncoder", "ffmpeg_opus");
	}

	return true;
}

bool App::InitService() {
	ProfileScope("MainWindow::InitService");

	if (LoadService())
		return true;

	service = obs_service_create("rtmp_common", "default_service", nullptr, nullptr);
	if (!service)
		return false;
	obs_service_release(service);

	return true;
}

void App::CheckForSimpleModeX264Fallback() {
	const char* curStreamEncoder =
	  config_get_string(basicConfig, "SimpleOutput", "StreamEncoder");
	const char* curRecEncoder = config_get_string(basicConfig, "SimpleOutput", "RecEncoder");
	bool qsv_supported = false;
	bool qsv_av1_supported = false;
	bool amd_supported = false;
	bool nve_supported = false;
#ifdef ENABLE_HEVC
	bool amd_hevc_supported = false;
	bool nve_hevc_supported = false;
	bool apple_hevc_supported = false;
#endif
	bool amd_av1_supported = false;
	bool apple_supported = false;
	bool changed = false;
	size_t idx = 0;
	const char* id;

	while (obs_enum_encoder_types(idx++, &id)) {
		if (strcmp(id, "h264_texture_amf") == 0)
			amd_supported = true;
		else if (strcmp(id, "obs_qsv11") == 0)
			qsv_supported = true;
		else if (strcmp(id, "obs_qsv11_av1") == 0)
			qsv_av1_supported = true;
		else if (strcmp(id, "ffmpeg_nvenc") == 0)
			nve_supported = true;
#ifdef ENABLE_HEVC
		else if (strcmp(id, "h265_texture_amf") == 0)
			amd_hevc_supported = true;
		else if (strcmp(id, "ffmpeg_hevc_nvenc") == 0)
			nve_hevc_supported = true;
#endif
		else if (strcmp(id, "av1_texture_amf") == 0)
			amd_av1_supported = true;
		else if (strcmp(id, "com.apple.videotoolbox.videoencoder.ave.avc") == 0)
			apple_supported = true;
#ifdef ENABLE_HEVC
		else if (strcmp(id, "com.apple.videotoolbox.videoencoder.ave.hevc") == 0)
			apple_hevc_supported = true;
#endif
	}

	auto CheckEncoder = [&](const char*& name) {
		if (strcmp(name, SIMPLE_ENCODER_QSV) == 0) {
			if (!qsv_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_QSV_AV1) == 0) {
			if (!qsv_av1_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_NVENC) == 0) {
			if (!nve_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_NVENC_AV1) == 0) {
			if (!nve_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
#ifdef ENABLE_HEVC
		} else if (strcmp(name, SIMPLE_ENCODER_AMD_HEVC) == 0) {
			if (!amd_hevc_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_NVENC_HEVC) == 0) {
			if (!nve_hevc_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
#endif
		} else if (strcmp(name, SIMPLE_ENCODER_AMD) == 0) {
			if (!amd_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_AMD_AV1) == 0) {
			if (!amd_av1_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
		} else if (strcmp(name, SIMPLE_ENCODER_APPLE_H264) == 0) {
			if (!apple_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
#ifdef ENABLE_HEVC
		} else if (strcmp(name, SIMPLE_ENCODER_APPLE_HEVC) == 0) {
			if (!apple_hevc_supported) {
				changed = true;
				name = SIMPLE_ENCODER_X264;
				return false;
			}
#endif
		}

		return true;
	};

	if (!CheckEncoder(curStreamEncoder))
		config_set_string(basicConfig, "SimpleOutput", "StreamEncoder", curStreamEncoder);
	if (!CheckEncoder(curRecEncoder))
		config_set_string(basicConfig, "SimpleOutput", "RecEncoder", curRecEncoder);
	if (changed)
		config_save_safe(basicConfig, "tmp", nullptr);
}

void App::Load(const char* file) {
	disableSaving++;

	obs_data_t* data = obs_data_create_from_json_file_safe(file, "bak");
	if (!data) {
		disableSaving--;
		blog(LOG_INFO, "No scene file found, creating default scene");
		CreateDefaultScene(true);
		SaveProject();
		return;
	}

	LoadData(data, file);
}

void App::LoadData(obs_data_t* data, const char* file) {
	ClearSceneData();
	InitDefaultTransitions();

	OBSDataAutoRelease modulesObj = obs_data_get_obj(data, "modules");
	if (api)
		api->on_preload(modulesObj);

	OBSDataArrayAutoRelease sceneOrder = obs_data_get_array(data, "scene_order");
	OBSDataArrayAutoRelease sources = obs_data_get_array(data, "sources");
	OBSDataArrayAutoRelease groups = obs_data_get_array(data, "groups");
	OBSDataArrayAutoRelease transitions = obs_data_get_array(data, "transitions");
	const char* sceneName = obs_data_get_string(data, "current_scene");
	const char* programSceneName = obs_data_get_string(data, "current_program_scene");
	const char* transitionName = obs_data_get_string(data, "current_transition");

	const char* curSceneCollection =
	  config_get_string(GetGlobalConfig(), "Basic", "SceneCollection");

	obs_data_set_default_string(data, "name", curSceneCollection);

	const char* name = obs_data_get_string(data, "name");
	OBSSourceAutoRelease curScene;
	OBSSourceAutoRelease curProgramScene;

	LoadAudioDevice(DESKTOP_AUDIO_1, 1, data);
	LoadAudioDevice(DESKTOP_AUDIO_2, 2, data);
	LoadAudioDevice(AUX_AUDIO_1, 3, data);
	LoadAudioDevice(AUX_AUDIO_2, 4, data);
	LoadAudioDevice(AUX_AUDIO_3, 5, data);
	LoadAudioDevice(AUX_AUDIO_4, 6, data);

	if (!sources) {
		sources = std::move(groups);
	} else {
		obs_data_array_push_back_array(sources, groups);
	}

	obs_missing_files_t* files = obs_missing_files_create();
	obs_load_sources(sources, AddMissingFiles, files);

	curScene = obs_get_source_by_name(sceneName);
	curProgramScene = obs_get_source_by_name(programSceneName);

	SetCurrentScene(curScene.Get(), true);

	if (!curProgramScene)
		curProgramScene = std::move(curScene);

	/* ------------------- */

	std::string file_base = strrchr(file, '/') + 1;
	file_base.erase(file_base.size() - 5, 5);

	config_set_string(GetGlobalConfig(), "Basic", "SceneCollection", name);
	config_set_string(GetGlobalConfig(), "Basic", "SceneCollectionFile", file_base.c_str());

	/* ---------------------- */

	bool fixedScaling = obs_data_get_bool(data, "scaling_enabled");
	int scalingLevel = (int)obs_data_get_int(data, "scaling_level");
	float scrollOffX = (float)obs_data_get_double(data, "scaling_off_x");
	float scrollOffY = (float)obs_data_get_double(data, "scaling_off_y");

	if (fixedScaling) {
		// TODO: check if need scale the preview
	}

	/* ---------------------- */

	if (api)
		api->on_load(modulesObj);

	obs_data_release(data);

	blog(LOG_INFO, "------------------------------------------------");
	blog(LOG_INFO, "Loaded scenes:");

	auto scenes = sceneSourceManager->Scenes();
	for (const auto item : scenes) {
		OBSScene scene = OBSScene(item->Data());

		obs_source_t* source = obs_scene_get_source(scene);
		const char* name = obs_source_get_name(source);

		blog(LOG_INFO, "- scene '%s':", name);
		obs_scene_enum_items(scene, LogSceneItem, (void*)(intptr_t)1);
		obs_source_enum_filters(source, LogFilter, (void*)(intptr_t)1);
	}

	blog(LOG_INFO, "------------------------------------------------");

	disableSaving--;

	if (api) {
		api->on_event(OBS_FRONTEND_EVENT_SCENE_CHANGED);
		api->on_event(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
	}
}

void App::ClearSceneData() {
	disableSaving++;

	for (int i = 0; i < MAX_CHANNELS; i++) obs_set_output_source(i, nullptr);

	lastScene = nullptr;
	swapScene = nullptr;
	programScene = nullptr;
	//prevFTBSource = nullptr;

	auto cb = [](void*, obs_source_t* source) {
		obs_source_remove(source);
		return true;
	};

	obs_enum_scenes(cb, nullptr);
	obs_enum_sources(cb, nullptr);

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP);

	/* If scene data wasn't actually cleared, e.g. faulty plugin holding a
   * reference, they will still be in the hash table, enumerate them and
   * store the names for logging purposes. */
	auto cb2 = [](void* param, obs_source_t* source) {
		auto orphans = static_cast<std::vector<std::string>*>(param);
		orphans->push_back(obs_source_get_name(source));
		return true;
	};

	std::vector<std::string> orphan_sources;
	obs_enum_sources(cb2, &orphan_sources);

	if (!orphan_sources.empty()) {
		/* Avoid logging list twice in case it gets called after
     * setting the flag the first time. */
		if (!clearingFailed) {
			/* This ugly mess exists to join a vector of strings
       * with a user-defined delimiter. */
			std::string orphan_names = std::accumulate(
			  orphan_sources.begin(), orphan_sources.end(), std::string(""),
			  [](std::string a, std::string b) { return std::move(a) + "\n- " + b; });

			blog(LOG_ERROR,
			     "Not all sources were cleared when clearing scene data:\n%s\n",
			     orphan_names.c_str());
		}

		/* We do not decrement disableSaving here to avoid OBS
     * overwriting user data with garbage. */
		clearingFailed = true;
	} else {
		disableSaving--;

		blog(LOG_INFO, "All scene data cleared");
		blog(LOG_INFO, "------------------------------------------------");
	}
}

void App::CreateFirstRunSources() {
	bool hasDesktopAudio = HasAudioDevices(OutputAudioSource());
	bool hasInputAudio = HasAudioDevices(InputAudioSource());

	if (hasDesktopAudio)
		ResetAudioDevice(OutputAudioSource(), "default", Str("Basic.DesktopDevice1"), 1);
	if (hasInputAudio)
		ResetAudioDevice(InputAudioSource(), "default", Str("Basic.AuxDevice1"), 3);
}

void App::CreateDefaultScene(bool firstStart) {
	disableSaving++;

	ClearSceneData();
	InitDefaultTransitions();

	SetTransition(fadeTransition);

	OBSSceneAutoRelease scene = obs_scene_create(Str("Basic.Scene"));

	if (firstStart)
		CreateFirstRunSources();

	SetCurrentScene(scene, true);

	disableSaving--;
}

void App::SaveProjectNow() {
	if (disableSaving)
		return;

	projectChanged = true;
	SaveProjectDeferred();
}

void App::SaveProject() {
	if (disableSaving)
		return;

	projectChanged = true;
	SaveProjectDeferred();
}

void App::SaveProjectDeferred() {
	if (disableSaving)
		return;

	if (!projectChanged)
		return;

	projectChanged = false;

	const char* sceneCollection =
	  config_get_string(GetGlobalConfig(), "Basic", "SceneCollectionFile");

	char savePath[1024];
	char fileName[1024];
	int ret;

	if (!sceneCollection)
		return;

	ret =
	  snprintf(fileName, sizeof(fileName), "obs-studio/basic/scenes/%s.json", sceneCollection);
	if (ret <= 0)
		return;

	ret = GetConfigPath(savePath, sizeof(savePath), fileName);
	if (ret <= 0)
		return;

	Save(savePath);
}

void App::Save(const char* file) {
	OBSScene scene = GetCurrentScene();
	OBSSource curProgramScene = OBSGetStrongRef(programScene);
	if (!curProgramScene)
		curProgramScene = obs_scene_get_source(scene);

	OBSDataArrayAutoRelease sceneOrder = obs_data_array_create();
	OBSDataArrayAutoRelease transitions = nullptr;
	OBSDataArrayAutoRelease quickTrData = nullptr;
	OBSDataArrayAutoRelease savedProjectorList = nullptr;
	OBSDataAutoRelease saveData = GenerateSaveData(sceneOrder, quickTrData, 0, transitions,
						       scene, curProgramScene, savedProjectorList);

	if (api) {
		OBSDataAutoRelease moduleObj = obs_data_create();
		api->on_save(moduleObj);
		obs_data_set_obj(saveData, "modules", moduleObj);
	}

	if (!obs_data_save_json_safe(saveData, file, "tmp", "bak"))
		blog(LOG_ERROR, "Could not save scene data to %s", file);
}

void App::DeferSaveBegin() {
	os_atomic_inc_long(&disableSaving);
}

void App::DeferSaveEnd() {
	long result = os_atomic_dec_long(&disableSaving);
	if (result == 0) {
		SaveProject();
	}
}

void App::InitDefaultTransitions() {
	std::vector<OBSSource> transitions;
	size_t idx = 0;
	const char* id;

	/* automatically add transitions that have no configuration (things
   * such as cut/fade/etc) */
	while (obs_enum_transition_types(idx++, &id)) {
		if (!obs_is_source_configurable(id)) {
			const char* name = obs_source_get_display_name(id);

			OBSSourceAutoRelease tr = obs_source_create_private(id, name, NULL);
			InitTransition(tr);
			transitions.emplace_back(tr);

			if (strcmp(id, "fade_transition") == 0)
				fadeTransition = tr;
			else if (strcmp(id, "cut_transition") == 0)
				cutTransition = tr;
		}
	}

	for (OBSSource& tr : transitions) { SetTransition(tr); }
}

void App::InitTransition(obs_source_t* transition) {
	auto onTransitionStop = [](void* data, calldata_t*) {
		// post the message
	};

	auto onTransitionFullStop = [](void* data, calldata_t*) {
		// post the message
	};

	signal_handler_t* handler = obs_source_get_signal_handler(transition);
	signal_handler_connect(handler, "transition_video_stop", onTransitionStop, this);
	signal_handler_connect(handler, "transition_stop", onTransitionFullStop, this);
}

void App::SetTransition(OBSSource transition) {
	OBSSourceAutoRelease oldTransition = obs_get_output_source(0);

	if (oldTransition && transition) {
		obs_transition_swap_begin(transition, oldTransition);
		obs_set_output_source(0, transition);
		obs_transition_swap_end(transition, oldTransition);
	} else {
		obs_set_output_source(0, transition);
	}

	if (api)
		api->on_event(OBS_FRONTEND_EVENT_TRANSITION_CHANGED);
}

void App::TransitionToScene(OBSSource source) {
	obs_scene_t* scene = obs_scene_from_source(source);
	if (!scene)
		return;

	OBSSourceAutoRelease transition = obs_get_output_source(0);
	if (!transition) {
		return;
	}

	obs_transition_set(transition, source);
	if (api)
		api->on_event(OBS_FRONTEND_EVENT_SCENE_CHANGED);
}

void App::SetCurrentScene(OBSSource scene, bool force) {
	TransitionToScene(scene);

	if (obs_scene_get_source(GetCurrentScene()) != scene) {
		auto scenes = sceneSourceManager->Scenes();
		for (const auto item : scenes) {
			OBSScene itemScene = OBSScene(item->Data());
			obs_source_t* source = obs_scene_get_source(itemScene);

			if (source == scene) {
				currentScene = itemScene.Get();

				if (api)
					api->on_event(OBS_FRONTEND_EVENT_SCENE_CHANGED);
				break;
			}
		}
	}

	if (scene) {
		bool userSwitched = (!force && !disableSaving);
		blog(LOG_INFO, "%s to scene '%s'", userSwitched ? "User switched" : "Switched",
		     obs_source_get_name(scene));
	}
}

OBSSource App::GetProgramSource() {
	return OBSGetStrongRef(programScene);
}

OBSScene App::GetCurrentScene() {
	return currentScene.load();
}

void App::SetCurrentScene(obs_scene_t* scene, bool force) {
	obs_source_t* source = obs_scene_get_source(scene);
	SetCurrentScene(source, force);
}

void App::ResetAudioDevice(const char* sourceId, const char* deviceId, const char* deviceDesc,
			   int channel) {
	bool disable = deviceId && strcmp(deviceId, "disabled") == 0;
	OBSSourceAutoRelease source;
	OBSDataAutoRelease settings;

	source = obs_get_output_source(channel);
	if (source) {
		if (disable) {
			obs_set_output_source(channel, nullptr);
		} else {
			settings = obs_source_get_settings(source);
			const char* oldId = obs_data_get_string(settings, "device_id");
			if (strcmp(oldId, deviceId) != 0) {
				obs_data_set_string(settings, "device_id", deviceId);
				obs_source_update(source, settings);
			}
		}

	} else if (!disable) {
		BPtr<char> name = get_new_source_name(deviceDesc, "%s (%d)");

		settings = obs_data_create();
		obs_data_set_string(settings, "device_id", deviceId);
		source = obs_source_create(sourceId, name, settings, nullptr);

		obs_set_output_source(channel, source);
	}
}

} // namespace core
