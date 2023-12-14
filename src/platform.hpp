/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include <util/c99defs.h>

#include <string>
#include <vector>

#include <Windows.h>

/* Gets the path of obs-studio specific data files (such as locale) */
bool GetDataFilePath(const char* data, std::string& path);
std::vector<std::string> GetPreferredLocales();

std::string GetDefaultVideoSavePath();

bool SetDisplayAffinitySupported(void);

bool IsAlwaysOnTop(HWND handle);
void SetAlwaysOnTop(HWND handle, bool enable);

enum TaskbarOverlayStatus {
	TaskbarOverlayStatusInactive,
	TaskbarOverlayStatusActive,
	TaskbarOverlayStatusPaused,
};
void TaskbarOverlayInit();
void TaskbarOverlaySetStatus(TaskbarOverlayStatus status);

#ifdef _WIN32
class RunOnceMutex;
RunOnceMutex
#else
void
#endif
CheckIfAlreadyRunning(bool& already_running);

#ifdef _WIN32
uint32_t GetWindowsVersion();
uint32_t GetWindowsBuild();
void SetProcessPriority(const char* priority);
void SetWin32DropStyle(HWND hwnd);
bool DisableAudioDucking(bool disable);

struct RunOnceMutexData;

class RunOnceMutex {
	RunOnceMutexData* data = nullptr;

public:
	RunOnceMutex(RunOnceMutexData* data_) : data(data_) {}
	RunOnceMutex(const RunOnceMutex& rom) = delete;
	RunOnceMutex(RunOnceMutex&& rom);
	~RunOnceMutex();

	RunOnceMutex& operator=(const RunOnceMutex& rom) = delete;
	RunOnceMutex& operator=(RunOnceMutex&& rom);
};

#endif
