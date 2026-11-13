// PcrDiag.cpp : bundled-filter registration diagnostics for PCRPlayer.
// Drop next to PCRPlayer(.64).exe, run, send the generated PcrDiag.txt.
// "PcrDiag batch" suppresses the message box.
#include <windows.h>
#include <shlwapi.h>
#include <strmif.h>
#include <initguid.h>
#include <stdio.h>
#include <stdarg.h>
#include <servprov.h>

#pragma comment(lib,"shlwapi")
#pragma comment(lib,"strmiids")
#pragma comment(lib,"ole32")
#pragma comment(lib,"advapi32")
#pragma comment(lib,"oleaut32")

static FILE* g_out = NULL;
static void P(const wchar_t* fmt, ...)
{
	wchar_t buf[1024]; va_list ap; va_start(ap, fmt);
	_vsnwprintf(buf, 1024, fmt, ap); va_end(ap);
	fputws(buf, g_out);
}

static const CLSID CLSID_SDE = {0x62BE5D10,0x60EB,0x11d0,{0xB3,0xE1,0x00,0xA0,0xC9,0x03,0xB8,0x9E}};
static const CLSID CATID_Legacy = {0x083863F1,0x70DE,0x11d0,{0xBD,0x40,0x00,0xA0,0xC9,0x11,0xCE,0x86}};

struct Entry { LPCWSTR clsid; LPCWSTR file; LPCWSTR label; };
#ifdef _WIN64
static Entry g_tab[] = {
	{ L"{EE30215D-164F-4A92-A4EB-9D4C13390F9F}", L"LAVVideo.ax",    L"LAV Video" },
	{ L"{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}", L"LAVAudio.ax",    L"LAV Audio" },
	{ L"{B98D13E7-55DB-4385-A33D-09FD1BA26338}", L"LAVSplitter.ax", L"LAV Splitter" },
	{ L"{71F080AA-8661-4093-B15E-4F6903E77D0A}", L"MpcVideoRenderer64.ax", L"MPC VR" },
};
#define ARCH L"x64"
#define VIEW KEY_WOW64_64KEY
#else
static Entry g_tab[] = {
	{ L"{EE30215D-164F-4A92-A4EB-9D4C13390F9F}", L"LAVVideo.ax",    L"LAV Video" },
	{ L"{E8E73B6B-4CB3-44A4-BE99-4F7BCB96E491}", L"LAVAudio.ax",    L"LAV Audio" },
	{ L"{B98D13E7-55DB-4385-A33D-09FD1BA26338}", L"LAVSplitter.ax", L"LAV Splitter" },
	{ L"{71F080AA-8661-4093-B15E-4F6903E77D0A}", L"MpcVideoRenderer.ax", L"MPC VR" },
};
#define ARCH L"x86"
#define VIEW KEY_WOW64_32KEY
#endif

static BOOL ListedInDShow(LPCWSTR clsidStr)
{
	BOOL found = FALSE;
	ICreateDevEnum* de = NULL;
	if (SUCCEEDED(CoCreateInstance(CLSID_SDE, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&de)) && de)
	{
		IEnumMoniker* em = NULL;
		if (SUCCEEDED(de->CreateClassEnumerator(CATID_Legacy, &em, 0)) && em)
		{
			IMoniker* mk[1]; ULONG got = 0;
			while (em->Next(1, mk, &got) == S_OK && got == 1)
			{
				IBindCtx* bc = NULL; BOOL stop = FALSE;
				if (SUCCEEDED(CreateBindCtx(0, &bc)))
				{
					IPropertyBag* pb = NULL;
					ULONG ptr = 0;
					if (SUCCEEDED(mk[0]->BindToStorage(bc, NULL, __uuidof(IPropertyBag), (void**)&pb)) && pb)
					{
						VARIANT v; VariantInit(&v);
						if (SUCCEEDED(pb->Read(L"CLSID", &v, NULL)) && v.vt == VT_BSTR && v.bstrVal)
						{
							if (lstrcmpiW(v.bstrVal, clsidStr) == 0) stop = TRUE;
							SysFreeString(v.bstrVal);
						}
						pb->Release();
					}
					(void)ptr;
					bc->Release();
				}
				mk[0]->Release();
				if (stop) { found = TRUE; break; }
			}
			em->Release();
		}
		de->Release();
	}
	return found;
}

int wmain(int argc, wchar_t** argv)
{
	wchar_t dir[MAX_PATH]; GetModuleFileNameW(NULL, dir, MAX_PATH);
	wchar_t* s = wcsrchr(dir, L'\\'); if (s) *s = 0;
	wchar_t outp[MAX_PATH]; wsprintfW(outp, L"%s\\PcrDiag.txt", dir);
	g_out = _wfopen(outp, L"wt, ccs=UTF-8");
	if (!g_out) g_out = stderr;

	P(L"=== PCRPlayer filter diagnostics (%s build) ===\r\n\r\n", ARCH);
	P(L"exe dir: %s\r\n\r\n", dir);

	wchar_t fdir[MAX_PATH]; wsprintfW(fdir, L"%s\\filters\\%s\\", dir, ARCH);
	P(L"[1] filter dir %s -> %s\r\n", fdir, PathFileExistsW(fdir) ? L"EXISTS" : L"MISSING");
	wchar_t mani[MAX_PATH]; wsprintfW(mani, L"%sLAVFilters.Dependencies.manifest", fdir);
	P(L"    LAVFilters.Dependencies.manifest -> %s  <<< REQUIRED for LAV\r\n\r\n",
		PathFileExistsW(mani) ? L"present" : L"MISSING!!!");

	// [2] registration state
	HKEY hk; P(L"[2] HKCU stamp: ");
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PCRPlayer", 0, KEY_READ, &hk) == ERROR_SUCCESS)
	{
		wchar_t v[128]; DWORD cb = sizeof(v), t = 0;
		if (RegQueryValueExW(hk, L"FiltersStamp", NULL, &t, (BYTE*)v, &cb) == ERROR_SUCCESS) P(L"%s", v);
		else P(L"(no stamp - not registered yet)");
		DWORD d = 0; cb = sizeof(d);
		if (RegQueryValueExW(hk, L"FiltersDisabled", NULL, &t, (BYTE*)&d, &cb) == ERROR_SUCCESS && d == 1)
			P(L"  [FiltersDisabled=1 : auto-registration is OFF (/register-filters to re-enable)]");
		RegCloseKey(hk);
	}
	else P(L"(no Software\\PCRPlayer key)");
	P(L"\r\n\r\n");

	CoInitialize(NULL);
	for (int i = 0; i < (int)(sizeof(g_tab)/sizeof(g_tab[0])); ++i)
	{
		const Entry& e = g_tab[i];
		wchar_t path[MAX_PATH]; wsprintfW(path, L"%s%s", fdir, e.file);
		WIN32_FIND_DATAW fd; HANDLE hF = FindFirstFileW(path, &fd);
		P(L"[%d] %s (%s)\r\n", 3 + i, e.label, e.file);
		if (hF == INVALID_HANDLE_VALUE) { P(L"    file: MISSING\r\n\r\n"); continue; }
		ULONGLONG sz = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
		P(L"    file: OK size=%llu\r\n", sz);
		FindClose(hF);

		wchar_t sub[256]; wsprintfW(sub, L"Software\\Classes\\CLSID\\%s\\InprocServer32", e.clsid);
		HKEY hk2; wchar_t reg[MAX_PATH] = L"(none)";
		if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ | VIEW, &hk2) == ERROR_SUCCESS)
		{
			DWORD cb2 = sizeof(reg), t2 = 0;
			if (RegQueryValueExW(hk2, NULL, NULL, &t2, (BYTE*)reg, &cb2) != ERROR_SUCCESS) lstrcpyW(reg, L"(value missing)");
			RegCloseKey(hk2);
		}
		P(L"    HKCU reg: %s\r\n", reg);

		HMODULE hm = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		P(L"    LoadLibraryEx: %s gle=%u\r\n", hm ? L"OK" : L"FAIL", GetLastError());

		P(L"    DShow enumeration: %s\r\n", ListedInDShow(e.clsid) ? L"LISTED" : L"NOT LISTED (normal for HKCU-only)");
		if (hm) FreeLibrary(hm);

		CLSID clsid;
		if (SUCCEEDED(CLSIDFromString(e.clsid, &clsid)))
		{
			IUnknown* u = NULL;
			HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&u);
			P(L"    CoCreateInstance: hr=0x%08x\r\n", hr);
			if (u) u->Release();
		}
		P(L"\r\n");
	}
	CoUninitialize();
	P(L"=== done ===\r\n");
	if (g_out != stderr) fclose(g_out);
	if (!(argc > 1 && lstrcmpW(argv[1], L"batch") == 0))
		MessageBoxW(NULL, L"Diagnostics written to PcrDiag.txt next to this exe.\r\nPlease send that file.", L"PcrDiag", MB_OK | MB_ICONINFORMATION);
	return 0;
}
