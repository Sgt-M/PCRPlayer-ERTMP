// PcrFilter.cpp : per-user (HKCU) registration of bundled DShow filters.
// Layout mirrors baseclasses AMovieDllRegisterServer2 but rooted at HKCU:
//   HKCU\Software\Classes\CLSID\{clsid}                 = friendly name
//   ...\InprocServer32                                  = dll path, ThreadingModel=Both
//   ...\Implemented Categories\{LegacyAmFilterCategory} = (empty)
// Filters already registered machine-wide are left untouched.
#include "stdafx.h"
#include "PcrFilter.h"
#include "PcrWeb.h"
#include <shlwapi.h>

namespace pcrfilters
{

static const wchar_t* FILTER_STAMP = L"LAV0.83-MPCVR0.11.1.2636+p1";
// A 32-bit in-proc server must live in the 32-bit HKCU\Software\Classes view
// (WOW6432Node); a 64-bit one in the 64-bit view. Registering a bundled x86
// filter into the 64-bit view makes CoCreateInstance from THIS 32-bit process
// miss it. Pick the view matching the current process bitness.
#ifdef _WIN64
static const DWORD REG_VIEW = KEY_WOW64_64KEY;
#else
static const DWORD REG_VIEW = KEY_WOW64_32KEY;
#endif
// CLSID_LegacyAmFilterCategory
static const wchar_t* LEGACY_CAT = L"{083863F1-70DE-11d0-BD40-00A0C911CE86}";

struct Entry { LPCWSTR clsid; LPCWSTR name; LPCWSTR file; int cat; };

//[pcr-prop] ISpecifyPropertyPages of these filters return SEPARATE page-class
// CLSIDs that OleCreatePropertyFrame CoCreates by CLSID. A full regsvr32
// registers them too; without them the property dialogs silently fail on a
// clean machine. cat=1 adds the legacy-filter category (must NOT be done for
// page classes or they would show up as (broken) filters in enumerations).
static const Entry g_tab[] =
{
	{ L"{EE30215D-164F-4A92-A4EB-9D4C13390F9F}", L"LAV Video Decoder",  L"LAVVideo.ax",    1 },
	{ L"{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}", L"LAV Audio Decoder",  L"LAVAudio.ax",    1 },
	{ L"{B98D13E7-55DB-4385-A33D-09FD1BA26338}", L"LAV Splitter",       L"LAVSplitter.ax", 1 },
	// LAV property pages (page CLSIDs are identical in both bitness views;
	// the host .ax differs -> the usual path check picks the right module)
	{ L"{278407C2-558C-4BED-83A0-B6FA454200BD}", L"LAV Video Properties",      L"LAVVideo.ax",    0 },
	{ L"{2D4D6F88-8B41-40A2-B297-3D722816648B}", L"LAV Video Format Settings", L"LAVVideo.ax",    0 },
	{ L"{2D8F1801-A70D-48F4-B76B-7F5AE022AB54}", L"LAV Audio Properties",      L"LAVAudio.ax",    0 },
	{ L"{C89FC33C-E60A-4C97-BEF4-ACC5762B6404}", L"LAV Audio Mixer",           L"LAVAudio.ax",    0 },
	{ L"{BD72668E-6BFF-4CD1-8480-D465708B336B}", L"LAV Audio Format Settings", L"LAVAudio.ax",    0 },
	{ L"{A19DE2F2-2F74-4927-8436-61129D26C141}", L"LAV Splitter Properties",   L"LAVSplitter.ax", 0 },
	{ L"{56904B22-091C-4459-A2E6-B1F4F946B55F}", L"LAV Splitter Input Formats",L"LAVSplitter.ax", 0 },
#ifdef _WIN64
	{ L"{71F080AA-8661-4093-B15E-4F6903E77D0A}", L"MPC Video Renderer", L"MpcVideoRenderer64.ax", 1 },
	{ L"{DA46D181-07D6-441D-B314-019AEB10148A}", L"MainProp",           L"MpcVideoRenderer64.ax", 0 },
#else
	{ L"{71F080AA-8661-4093-B15E-4F6903E77D0A}", L"MPC Video Renderer", L"MpcVideoRenderer.ax", 1 },
	{ L"{DA46D181-07D6-441D-B314-019AEB10148A}", L"MainProp",           L"MpcVideoRenderer.ax", 0 },
#endif
};

static BOOL MachineRegistered(LPCWSTR clsid)
{
	wchar_t sub[160];
	wsprintfW(sub, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid);
	HKEY hk = NULL;
	if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS)
	{ ::RegCloseKey(hk); return TRUE; }
	if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ | KEY_WOW64_32KEY, &hk) == ERROR_SUCCESS)
	{ ::RegCloseKey(hk); return TRUE; }
	return FALSE;
}

static HRESULT SetStr(HKEY root, LPCWSTR sub, LPCWSTR val, LPCWSTR data)
{
	HKEY hk = NULL;
	LONG lr = ::RegCreateKeyExW(root, sub, 0, NULL, REG_OPTION_NON_VOLATILE,
		KEY_SET_VALUE | REG_VIEW, NULL, &hk, NULL);
	if (lr != ERROR_SUCCESS) { return HRESULT_FROM_WIN32(lr); }
	lr = ::RegSetValueExW(hk, val, 0, REG_SZ, (const BYTE*)data,
		(DWORD)((lstrlenW(data) + 1) * sizeof(wchar_t)));
	::RegCloseKey(hk);
	return HRESULT_FROM_WIN32(lr);
}

static HRESULT EnsureEmptyKey(HKEY root, LPCWSTR sub)
{
	HKEY hk = NULL;
	LONG lr = ::RegCreateKeyExW(root, sub, 0, NULL, REG_OPTION_NON_VOLATILE,
		KEY_READ | REG_VIEW, NULL, &hk, NULL);
	if (hk) { ::RegCloseKey(hk); }
	return HRESULT_FROM_WIN32(lr);
}

static void WriteStamp()
{
	HKEY hk = NULL;
	if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\PCRPlayer", 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS)
	{
		::RegSetValueExW(hk, L"FiltersStamp", 0, REG_SZ, (const BYTE*)FILTER_STAMP,
			(DWORD)((lstrlenW(FILTER_STAMP) + 1) * sizeof(wchar_t)));
		::RegCloseKey(hk);
	}
}

void EnsureRegistered()
{
	//[pcr-video] run every launch (4 cheap reg checks): the stale-HKCU cleanup
	// below must stay effective even when the stamp already matches.

	wchar_t dir[MAX_PATH]; ::GetModuleFileNameW(NULL, dir, MAX_PATH);
	wchar_t* slash = wcsrchr(dir, L'\\'); if (slash) { *slash = 0; }
#ifdef _WIN64
	lstrcatW(dir, L"\\filters\\x64\\");
#else
	lstrcatW(dir, L"\\filters\\x86\\");
#endif
	if (!::PathFileExistsW(dir))
	{
		pcrweb::Log(L"pcrfilter: no bundled dir %s (skip)", dir);
		return;
	}

	int done = 0, skip = 0;
	for (size_t i = 0; i < _countof(g_tab); ++i)
	{
		const Entry& e = g_tab[i];
		wchar_t path[MAX_PATH]; lstrcpyW(path, dir); lstrcatW(path, e.file);
		if (!::PathFileExistsW(path)) { ++skip; continue; }
		if (MachineRegistered(e.clsid))
		{
			++skip;
			//[pcr-video] a stale HKCU copy from an earlier run would SHADOW the
			// machine-wide registration (HKCU wins in the HKCR merge) -> drop it,
			// but only when it points into our own filters dir.
			wchar_t sub[256];
			wsprintfW(sub, L"Software\\Classes\\CLSID\\%s\\InprocServer32", e.clsid);
			HKEY hk = NULL;
			if (::RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &hk) == ERROR_SUCCESS)
			{
				wchar_t v[MAX_PATH]; DWORD cb = sizeof(v), t = 0;
				if (::RegQueryValueExW(hk, NULL, NULL, &t, (BYTE*)v, &cb) == ERROR_SUCCESS)
				{
					lstrcpynW(v, v, MAX_PATH);
					if (wcsstr(v, L"\\filters\\x64\\") || wcsstr(v, L"\\filters\\x86\\"))
					{
						::RegCloseKey(hk);
						wsprintfW(sub, L"Software\\Classes\\CLSID\\%s", e.clsid);
						::RegDeleteTreeW(HKEY_CURRENT_USER, sub);
						pcrweb::Log(L"pcrfilter: dropped stale HKCU copy of %s", e.name);
						hk = NULL;
					}
				}
				if (hk) { ::RegCloseKey(hk); }
			}
			continue;
		}

		wchar_t sub[256];
		HRESULT hr = SetStr(HKEY_CURRENT_USER,
			(wsprintfW(sub, L"Software\\Classes\\CLSID\\%s", e.clsid), sub), NULL, e.name);
		if (SUCCEEDED(hr))
		{
			hr = SetStr(HKEY_CURRENT_USER,
				(wsprintfW(sub, L"Software\\Classes\\CLSID\\%s\\InprocServer32", e.clsid), sub),
				NULL, path);
		}
		if (SUCCEEDED(hr))
		{
			hr = SetStr(HKEY_CURRENT_USER,
				(wsprintfW(sub, L"Software\\Classes\\CLSID\\%s\\InprocServer32", e.clsid), sub),
				L"ThreadingModel", L"Both");
		}
		if (SUCCEEDED(hr) && e.cat)
		{
			hr = EnsureEmptyKey(HKEY_CURRENT_USER,
				(wsprintfW(sub, L"Software\\Classes\\CLSID\\%s\\Implemented Categories\\%s", e.clsid, LEGACY_CAT), sub));
		}
		if (SUCCEEDED(hr)) { ++done; }
		else { pcrweb::Log(L"pcrfilter: register %s failed hr=0x%08x", e.name, hr); }
	}
	WriteStamp();
	pcrweb::Log(L"pcrfilter: registered=%d skipped=%d stamp=%s", done, skip, FILTER_STAMP);
}

} // namespace pcrfilters
