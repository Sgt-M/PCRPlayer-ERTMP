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

static BOOL FlagValue(LPCWSTR name, DWORD* out)
{
	HKEY hk = NULL; BOOL ok = FALSE;
	if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PCRPlayer", 0, KEY_READ, &hk) == ERROR_SUCCESS)
	{
		DWORD t = 0, cb = sizeof(DWORD);
		if (::RegQueryValueExW(hk, name, NULL, &t, (BYTE*)out, &cb) == ERROR_SUCCESS && t == REG_DWORD)
			ok = TRUE;
		::RegCloseKey(hk);
	}
	return ok;
}

static void SetFlag(LPCWSTR name, DWORD v)
{
	HKEY hk = NULL;
	if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\PCRPlayer", 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS)
	{
		::RegSetValueExW(hk, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
		::RegCloseKey(hk);
	}
}

//[pcr-filter] user opt-out: after /unregister-filters we must NOT silently
//re-register on the next launch (that would defeat installing official LAV/MPC VR).
BOOL IsDisabled()
{
	DWORD v = 0; FlagValue(L"FiltersDisabled", &v);
	return v == 1;
}

void Enable()
{
	SetFlag(L"FiltersDisabled", 0);
}

//[pcr-filter] remove our HKCU registrations (only entries whose InprocServer32
//points into our own filters\ dir - third-party or machine-wide entries are
//untouched) and keep them off. Returns number of removed CLSID trees.
int UnregisterAll()
{
	int removed = 0;
	for (size_t i = 0; i < _countof(g_tab); ++i)
	{
		const Entry& e = g_tab[i];
		wchar_t sub[256];
		wsprintfW(sub, L"Software\\Classes\\CLSID\\%s\\InprocServer32", e.clsid);
		HKEY hk = NULL;
		if (::RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ | REG_VIEW, &hk) == ERROR_SUCCESS)
		{
			wchar_t v[MAX_PATH]; DWORD cb = sizeof(v), t = 0;
			BOOL mine = FALSE;
			if (::RegQueryValueExW(hk, NULL, NULL, &t, (BYTE*)v, &cb) == ERROR_SUCCESS)
			{
				v[(cb / sizeof(wchar_t) < MAX_PATH) ? cb / sizeof(wchar_t) : MAX_PATH - 1] = 0;
				mine = (wcsstr(v, L"\\filters\\x64\\") || wcsstr(v, L"\\filters\\x86\\")) != NULL;
			}
			::RegCloseKey(hk);
			if (mine)
			{
				wsprintfW(sub, L"Software\\Classes\\CLSID\\%s", e.clsid);
				if (::RegDeleteTreeW(HKEY_CURRENT_USER, sub) == ERROR_SUCCESS) { ++removed; }
			}
		}
	}
	// drop our stamp so a future re-enable re-registers cleanly
	HKEY hk = NULL;
	if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PCRPlayer", 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS)
	{
		::RegDeleteValueW(hk, L"FiltersStamp");
		::RegCloseKey(hk);
	}
	SetFlag(L"FiltersDisabled", 1);
	pcrweb::Log(L"pcrfilter: unregistered %d entries (FiltersDisabled=1)", removed);
	return removed;
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
	if (IsDisabled())
	{
		pcrweb::Log(L"pcrfilter: disabled by user (FiltersDisabled=1), skip");
		return;
	}

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

bool HandleUnregisterRequest()
{
	LPCWSTR cl = ::GetCommandLineW();
	bool unreg = (cl && wcsstr(cl, L"/unregister-filters") != NULL);
	bool reg   = (cl && wcsstr(cl, L"/register-filters")   != NULL);
	if (!unreg && !reg) { return false; }

	wchar_t msg[256];
	if (unreg)
	{
		int n = UnregisterAll();
		// "PCRPlayer that sets aside the bundled LAV Filters / MPC Video Renderer
		//  registrations from HKCU %d items. Automatic registration will not be
		//  performed thereafter. It will use the filters installed by the installer."
		wsprintfW(msg,
			L"\x540c\x6805\x306e LAV Filters / MPC Video Renderer \x306e"
			L"HKCU \x767b\x9332\x3092 %d \x4ef6\x3001\x5916\x3057\x307e\x3057\x305f\x3002\n"
			L"\x4eca\x5f8c\x3001\x81ea\x52d5\x767b\x9332\x306f\x884c\x308f\x308c\x307e\x305b\x3093\x3002"
			L"\x30a4\x30f3\x30b9\x30c8\x30fc\x30e9\x3067\x30a4\x30f3\x30b9\x30c8\x30fc\x30eb\x3055\x308c\x305f"
			L"\x30d5\x30a3\x30eb\x30bf\x304c\x4f7f\x308f\x308c\x307e\x3059\x3002", n);
		::MessageBoxW(NULL, msg, L"PCRPlayer", MB_OK | MB_ICONINFORMATION);
	}
	else
	{
		Enable();
		EnsureRegistered();
		// "Restored automatic registration and re-registered the bundled filters."
		::MessageBoxW(NULL,
			L"\x81ea\x52d5\x767b\x9332\x3092\x6709\x52b9\x306b\x623b\x3057\x3001"
			L"\x540c\x6805\x30d5\x30a3\x30eb\x30bf\x3092\x518d\x767b\x9332\x3057\x307e\x3057\x305f\x3002",
			L"PCRPlayer", MB_OK | MB_ICONINFORMATION);
	}
	return true;
}

} // namespace pcrfilters
