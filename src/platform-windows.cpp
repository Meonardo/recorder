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

#include <algorithm>
#include <sstream>
#include "obs-config.h"
#include "obs-app.hpp"

#include "platform.hpp"
#include "../core/utils.h"

#include <util/windows/win-version.h>
#include <util/platform.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Dwmapi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <util/windows/WinHandle.hpp>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>

std::string GetDefaultVideoSavePath() {
	wchar_t path_utf16[MAX_PATH];
	char path_utf8[MAX_PATH] = {};

	SHGetFolderPathW(NULL, CSIDL_MYVIDEO, NULL, SHGFP_TYPE_CURRENT, path_utf16);

	os_wcs_to_utf8(path_utf16, wcslen(path_utf16), path_utf8, MAX_PATH);
	return std::string(path_utf8);
}

uint32_t GetWindowsVersion() {
	static uint32_t ver = 0;

	if (ver == 0) {
		struct win_version_info ver_info;

		get_win_ver(&ver_info);
		ver = (ver_info.major << 8) | ver_info.minor;
	}

	return ver;
}

uint32_t GetWindowsBuild() {
	static uint32_t build = 0;

	if (build == 0) {
		struct win_version_info ver_info;

		get_win_ver(&ver_info);
		build = ver_info.build;
	}

	return build;
}

bool IsAlwaysOnTop(HWND handle) {
	DWORD exStyle = GetWindowLong(handle, GWL_EXSTYLE);
	return (exStyle & WS_EX_TOPMOST) != 0;
}

void SetAlwaysOnTop(HWND hwnd, bool enable) {
	SetWindowPos(hwnd, enable ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SetProcessPriority(const char* priority) {
	if (!priority)
		return;

	if (strcmp(priority, "High") == 0)
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	else if (strcmp(priority, "AboveNormal") == 0)
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	else if (strcmp(priority, "Normal") == 0)
		SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
	else if (strcmp(priority, "BelowNormal") == 0)
		SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
	else if (strcmp(priority, "Idle") == 0)
		SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
}

void SetWin32DropStyle(HWND hwnd) {
	LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
	ex_style |= WS_EX_ACCEPTFILES;
	SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);
}

bool SetDisplayAffinitySupported(void) {
	static bool checked = false;
	static bool supported;

	/* this has to be version gated as setting WDA_EXCLUDEFROMCAPTURE on
	   older Windows builds behaves like WDA_MONITOR (black box) */

	if (!checked) {
		if (GetWindowsVersion() > 0x0A00 ||
		    GetWindowsVersion() == 0x0A00 && GetWindowsBuild() >= 19041)
			supported = true;
		else
			supported = false;

		checked = true;
	}

	return supported;
}

bool DisableAudioDucking(bool disable) {
	ComPtr<IMMDeviceEnumerator> devEmum;
	ComPtr<IMMDevice> device;
	ComPtr<IAudioSessionManager2> sessionManager2;
	ComPtr<IAudioSessionControl> sessionControl;
	ComPtr<IAudioSessionControl2> sessionControl2;

	HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
					  CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
					  (void**)&devEmum);
	if (FAILED(result))
		return false;

	result = devEmum->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (FAILED(result))
		return false;

	result = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr,
				  (void**)&sessionManager2);
	if (FAILED(result))
		return false;

	result = sessionManager2->GetAudioSessionControl(nullptr, 0, &sessionControl);
	if (FAILED(result))
		return false;

	result = sessionControl->QueryInterface(&sessionControl2);
	if (FAILED(result))
		return false;

	result = sessionControl2->SetDuckingPreference(disable);
	return SUCCEEDED(result);
}

struct RunOnceMutexData {
	WinHandle handle;

	inline RunOnceMutexData(HANDLE h) : handle(h) {}
};

RunOnceMutex::RunOnceMutex(RunOnceMutex&& rom) {
	delete data;
	data = rom.data;
	rom.data = nullptr;
}

RunOnceMutex::~RunOnceMutex() {
	delete data;
}

RunOnceMutex& RunOnceMutex::operator=(RunOnceMutex&& rom) {
	delete data;
	data = rom.data;
	rom.data = nullptr;
	return *this;
}

RunOnceMutex CheckIfAlreadyRunning(bool& already_running) {
	std::string name = "OBSStudioCore";

	BPtr<wchar_t> wname;
	os_utf8_to_wcs_ptr(name.c_str(), name.size(), &wname);

	if (wname) {
		wchar_t* temp = wname;
		while (*temp) {
			if (!iswalnum(*temp))
				*temp = L'_';
			temp++;
		}
	}

	HANDLE h = OpenMutexW(SYNCHRONIZE, false, wname.Get());
	already_running = !!h;

	if (!already_running)
		h = CreateMutexW(nullptr, false, wname.Get());

	RunOnceMutex rom(h ? new RunOnceMutexData(h) : nullptr);
	return rom;
}

struct MonitorData {
	const wchar_t* id;
	MONITORINFOEX info;
	bool found;
};

static BOOL CALLBACK GetMonitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
	MonitorData* data = (MonitorData*)param;

	if (GetMonitorInfoW(monitor, &data->info)) {
		if (wcscmp(data->info.szDevice, data->id) == 0) {
			data->found = true;
			return false;
		}
	}

	return true;
}

/* Based on https://www.winehq.org/pipermail/wine-devel/2008-September/069387.html */
typedef const char*(CDECL* WINEGETVERSION)(void);
bool IsRunningOnWine() {
	WINEGETVERSION func;
	HMODULE nt;

	nt = GetModuleHandleW(L"ntdll");
	if (!nt)
		return false;

	func = (WINEGETVERSION)GetProcAddress(nt, "wine_get_version");
	if (func) {
		blog(LOG_WARNING, "Running on Wine version \"%s\"", func());
		return true;
	}

	return false;
}

HWND hwnd;
void TaskbarOverlayInit() {
	hwnd = (HWND)App()->GetMainWindow()->winId();
}

void TaskbarOverlaySetStatus(TaskbarOverlayStatus status) {
	ITaskbarList4* taskbarIcon;
	auto hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
				   IID_PPV_ARGS(&taskbarIcon));

	if (FAILED(hr)) {
		taskbarIcon->Release();
		return;
	}

	hr = taskbarIcon->HrInit();

	if (FAILED(hr)) {
		taskbarIcon->Release();
		return;
	}

	QIcon qicon;
	switch (status) {
	case TaskbarOverlayStatusActive:
		qicon = QIcon::fromTheme("obs-active", QIcon(":/res/images/active.png"));
		break;
	case TaskbarOverlayStatusPaused:
		qicon = QIcon::fromTheme("obs-paused", QIcon(":/res/images/paused.png"));
		break;
	case TaskbarOverlayStatusInactive:
		taskbarIcon->SetOverlayIcon(hwnd, nullptr, nullptr);
		taskbarIcon->Release();
		return;
	}

	HICON hicon = nullptr;
	if (!qicon.isNull()) {
		Q_GUI_EXPORT HICON qt_pixmapToWinHICON(const QPixmap& p);
		hicon = qt_pixmapToWinHICON(qicon.pixmap(GetSystemMetrics(SM_CXSMICON)));
		if (!hicon)
			return;
	}

	taskbarIcon->SetOverlayIcon(hwnd, hicon, nullptr);
	DestroyIcon(hicon);
	taskbarIcon->Release();
}
