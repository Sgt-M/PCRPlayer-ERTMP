
// PCRPlayer.cpp : アプリケーションのクラス動作を定義します。
//

#include "stdafx.h"
#include "PCRPlayer.h"
#include "PcrFilter.h"
#include "PcrWeb.h"




#include "MainDlg.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CMainApp

BEGIN_MESSAGE_MAP(CMainApp, CWinApp)
	//ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// CMainApp コンストラクション

CMainApp::CMainApp()
{
	// TODO: この位置に構築用コードを追加してください。
	// ここに InitInstance 中の重要な初期化処理をすべて記述してください。
}


// 唯一の CMainApp オブジェクトです。

CMainApp theApp;


// CMainApp 初期化


//[pcr-diag] freeze sentry: self minidump when the UI thread stops dispatching.
//Background copies the UI heartbeat; if it goes stale >3s while other threads
//still run (== classic message-loop stall), dump this process once per stall to
//%TEMP%\pcrfreeze-<pid>-<tick>.dmp so the block site is readable offline.
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static volatile LONG s_pcrHeartbeat = 0;	// bumped by UI WM_TIMER
static volatile LONG s_pcrDumpArmed = 1;
static volatile LONG s_pcrStopped = 0;	// shutdown began: never dump again
volatile LONG pcrUiHeartbeat() { return s_pcrHeartbeat; }
void pcrUiBeat() { InterlockedIncrement(&s_pcrHeartbeat); }
static void pcrWriteFreezeDump()
{
	s_pcrDumpArmed = 0;
	wchar_t dir[MAX_PATH] = L"";
	if (!::GetTempPathW(MAX_PATH, dir)) { return; }
	SYSTEMTIME st; ::GetLocalTime(&st);
	wchar_t name[MAX_PATH];
	wsprintfW(name, L"pcrfreeze-%lu-%04d%02d%02d-%02d%02d%02d.dmp",
		::GetCurrentProcessId(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	wchar_t path[MAX_PATH * 2];
	wsprintfW(path, L"%s%s", dir, name);
	HANDLE hFile = ::CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory |
			MiniDumpWithThreadInfo | MiniDumpWithHandleData | MiniDumpWithUnloadedModules);
		::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hFile, type, NULL, NULL, NULL);
		::CloseHandle(hFile);
	}
	//[pcr-diag] silent: no messagebox (also fires during normal shutdown; collect %TEMP%\pcrfreeze-*.dmp)
}
static DWORD WINAPI pcrSentryThread(LPVOID)
{
	LONG last = -1; ULONGLONG lastChange = ::GetTickCount64();
	for (;;)
	{
		LONG now = s_pcrHeartbeat;
		ULONGLONG tick = ::GetTickCount64();
		if (now != last) { last = now; lastChange = tick; if (!s_pcrStopped) s_pcrDumpArmed = 1; }
		else if (tick - lastChange > 3000 && s_pcrDumpArmed) { pcrWriteFreezeDump(); lastChange = tick; }
		::Sleep(500);
	}
	return 0;
}
void pcrStopSentry() { s_pcrStopped = 1; s_pcrDumpArmed = 0; }

//[pcr-fix] graceful-shutdown flag: the close path sets this BEFORE tearing
//down the player; GraphManager::uninit then skips the full graph Release
//(quartz graph_.Release() blocks ~20s here waiting on streaming threads
//after the source died) - audio/stream are already stopped by abort+stop,
//the window is already hidden, settings already saved, so the OS reclaims
//the graph at process exit instead of the UI thread waiting on it.
static volatile LONG s_pcrShutdown = 0;
void pcrSetShutdown() { ::InterlockedExchange(&s_pcrShutdown, 1); }
bool pcrIsShutdown() { return s_pcrShutdown != 0; }

//[pcr-diag] staged close trace: only when env PCR_TRACE_CLOSE is set.
void pcrTrace(const wchar_t* msg)
{
	static const BOOL on = (::GetEnvironmentVariableW(L"PCR_TRACE_CLOSE", NULL, 0) > 0);
	if (!on) { return; }
	wchar_t dir[MAX_PATH];
	if (!::GetTempPathW(MAX_PATH, dir)) { return; }
	std::wstring line = std::to_wstring(::GetTickCount64()) + L" pid" +
		std::to_wstring(::GetCurrentProcessId()) + L" " + msg + L"\r\n";
	std::wstring path = std::wstring(dir) + L"pcrclose.log";
	HANDLE h = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE)
	{
		::SetFilePointer(h, 0, NULL, FILE_END);
		DWORD w = 0;
		::WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &w, NULL);
		::CloseHandle(h);
	}
}
void pcrStartSentry()
{
	HANDLE h = ::CreateThread(NULL, 0, pcrSentryThread, NULL, 0, NULL);
	if (h) ::CloseHandle(h);
}

BOOL CMainApp::InitInstance()
{
	pcrStartSentry();
	// アプリケーション マニフェストが visual スタイルを有効にするために、
	// ComCtl32.dll Version 6 以降の使用を指定する場合は、
	// Windows XP に InitCommonControlsEx() が必要です。さもなければ、ウィンドウ作成はすべて失敗します。
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// アプリケーションで使用するすべてのコモン コントロール クラスを含めるには、
	// これを設定します。
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	//[pcr-web] WebView2 loader requires OLE/COM on the UI thread
	if (!AfxOleInit()) { return FALSE; }
	//[pcr-filter] maintenance CLI: /unregister-filters, /register-filters
	if (pcrfilters::HandleUnregisterRequest()) { return FALSE; }
	//[pcr-video] register bundled filters (LAV / MPC VR) per-user
	pcrfilters::EnsureRegistered();
	//[pcr-probe] PCR_EARLY: pre-spawn environment on web thread at startup
	if (::GetEnvironmentVariableW(L"PCR_EARLY", NULL, 0) > 0) {
		pcrweb::RequestEnvironment([](ICoreWebView2Environment* e) {
			pcrweb::Log(L"EARLY env=%p", (void*)e);
		});
	}


	// ActiveX
	AfxEnableControlContainer();

	// ダイアログにシェル ツリー ビューまたはシェル リスト ビュー コントロールが
	// 含まれている場合にシェル マネージャーを作成します。
	CShellManager *pShellManager = new CShellManager;

	// MFC コントロールでテーマを有効にするために、"Windows ネイティブ" のビジュアル マネージャーをアクティブ化
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	// 標準初期化
	// これらの機能を使わずに最終的な実行可能ファイルの
	// サイズを縮小したい場合は、以下から不要な初期化
	// ルーチンを削除してください。
	// 設定が格納されているレジストリ キーを変更します。
	// TODO: 会社名または組織名などの適切な文字列に
	// この文字列を変更してください。
	//SetRegistryKey(_T("アプリケーション ウィザードで生成されたローカル アプリケーション"));


	// ロケール設定
	::setlocale(LC_ALL, "");

	// =======================================================
	// ダイアログベースにウィンドウクラス名を設定
	WNDCLASS wc;
	// Get the info for this class.
	// #32770 is the default class name for dialogs boxes.
	::GetClassInfo(AfxGetInstanceHandle(), L"#32770", &wc);

	// Change the name of the class.
	wc.lpszClassName = L"PCRPlayer";

	// Register this class so that MFC can use it.
	AfxRegisterClass(&wc);	
	// =======================================================

	// DLL初期化
	utl::DWMLoader::Init();
	utl::CommandLine::init();
	d3dutl::D3DLoader::Init();
	utl::GdiplusInit gdipini;
	Mlang::Init();

	// カレントパス取得
	utl::getModuleFolder(gl_.current);

	CMainDlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: ダイアログが <OK> で消された時のコードを
		//  記述してください。
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: ダイアログが <キャンセル> で消された時のコードを
		//  記述してください。
	}

	// 上で作成されたシェル マネージャーを削除します。
	if (pShellManager != NULL)
	{
		delete pShellManager;
	}

	// ダイアログは閉じられました。アプリケーションのメッセージ ポンプを開始しないで
	//  アプリケーションを終了するために FALSE を返してください。
	return FALSE;
}

