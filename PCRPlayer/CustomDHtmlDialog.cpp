// CustomDHtmlDialog.cpp : WebView2 host implementation. ASCII-only.
//[pcr-web] replaces the MSHTML/CDHtmlDialog implementation.

#include "stdafx.h"
#include "PCRPlayer.h"
#include "CustomDHtmlDialog.h"
#include "IMEEdit.h"
#include "Util.h"

#include <wrl/client.h>
#include <wrl/implements.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

IMPLEMENT_DYNCREATE(CCustomDHtmlDialog, CDialog)

#define PCR_TIMER_ENV (0x50c1)
#define PCR_TIMER_KICK 0xA7C3

static double ElementScale(const PcrElement& el, double zoom)
{
	double dpr = el.dpr > 0 ? el.dpr : 1.0;
	return dpr * zoom;
}

CCustomDHtmlDialog::CCustomDHtmlDialog()
	: CDialog()
	, controller_(NULL)   //[pcr-web] init COM pointers (see WM_SIZE note)
	, webview_(NULL)
	, webchild_(NULL)
	, ready_(false)
	, bridgeOk_(false)
	, creating_(false)
	, navAllow_(0)
	, scale_(1.0)
{
	::memset(&tokNavStart_, 0, sizeof(tokNavStart_));
	::memset(&tokNavDone_, 0, sizeof(tokNavDone_));
	::memset(&tokMsg_, 0, sizeof(tokMsg_));
	::memset(&tokCtx_, 0, sizeof(tokCtx_));
	::memset(&tokNewWin_, 0, sizeof(tokNewWin_));
	::memset(&tokFail_, 0, sizeof(tokFail_));
}

CCustomDHtmlDialog::CCustomDHtmlDialog(UINT nIDTemplate, CWnd* pParent /*=NULL*/)
	: CDialog(nIDTemplate, pParent)
	, controller_(NULL)   //[pcr-web] must init: WM_SIZE can fire before OnInitDialog
	, webview_(NULL)
	, webchild_(NULL)
	, ready_(false)
	, bridgeOk_(false)
	, creating_(false)
	, navAllow_(0)
	, scale_(1.0)
{
	::memset(&tokNavStart_, 0, sizeof(tokNavStart_));
	::memset(&tokNavDone_, 0, sizeof(tokNavDone_));
	::memset(&tokMsg_, 0, sizeof(tokMsg_));
	::memset(&tokCtx_, 0, sizeof(tokCtx_));
	::memset(&tokNewWin_, 0, sizeof(tokNewWin_));
	::memset(&tokFail_, 0, sizeof(tokFail_));
}

CCustomDHtmlDialog::~CCustomDHtmlDialog()
{
}

// ---------------------------------------------------------------- create

//[pcr-marsh] WebView2 objects belong to the web thread apartment. UI-thread
//code must not call them directly (crash dump: OnSize->UpdateBounds jumped
//into a freed vtable when called cross-apartment). CallWeb runs fn on the
//owning thread, pumping UI messages while waiting; inline on that thread.
static BOOL CallWeb(const std::function<void()>& fn, DWORD timeoutMs = 3000)
{
	if (pcrweb::IsWebThread()) { fn(); return TRUE; }
	struct Ctx { HANDLE ev; volatile BOOL done; } ctx = { NULL, FALSE };
	ctx.ev = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!ctx.ev) { return FALSE; }
	pcrweb::PostWebTask([&ctx, fn]() { fn(); ctx.done = TRUE; SetEvent(ctx.ev); });
	DWORD t0 = GetTickCount();
	BOOL ok = FALSE;
	for (;;) {
		MSG m;
		while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
		if (WaitForSingleObject(ctx.ev, 10) == WAIT_OBJECT_0) { ok = TRUE; break; }
		if (GetTickCount() - t0 > timeoutMs) { break; }
	}
	CloseHandle(ctx.ev);
	return ok;
}

//container window used as the WebView2 parent
static BOOL g_ctrRegistered = FALSE;
static HWND MakeWebContainer(HWND dlg, RECT rc)
{
	if (!g_ctrRegistered) {
		WNDCLASSEXW wc = { sizeof(wc) };
		wc.lpfnWndProc = ::DefWindowProcW;
		wc.hInstance = ::GetModuleHandleW(NULL);
		wc.lpszClassName = L"PCRWEB_CTR";
		g_ctrRegistered = ::RegisterClassExW(&wc) != 0;
	}
	if (!g_ctrRegistered) { return NULL; }
	//plain child container inside the dialog (t53: the earlier 'invisible webview'
	//was a missing controller AddRef, NOT the window shape; WS_CHILD verified ok)
	HWND h = ::CreateWindowExW(0, L"PCRWEB_CTR", L"", WS_CHILD | WS_VISIBLE,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
		dlg, NULL, ::GetModuleHandleW(NULL), NULL);
	return h;
}

void CCustomDHtmlDialog::StartCreate()
{
	pcrweb::Log(L"StartCreate tid=%u", ::GetCurrentThreadId());
	if (controller_ || creating_) { return; }
	creating_ = true;
	//[pcr-web] controller creation MUST run inside the environment completion
	//callback (PcrWeb.h note): register a ready-func and let the loader's
	//callback fire CreateController for us.
	HWND hwnd = GetSafeHwnd();
	CCustomDHtmlDialog* self = this;
	pcrweb::RequestEnvironment([hwnd, self](ICoreWebView2Environment* env) {
		//[pcr-t3] MUST spawn the controller inside the env-completion call
		//stack (t3 works / t13,t14,t16 later-context: S_OK but no document
		//pipeline). Runs on the UI thread: env is UI-owned now.
		if (!env) { pcrweb::Log(L"env ready: null"); return; }
		if (!::IsWindow(hwnd)) { pcrweb::Log(L"env ready: hwnd gone"); return; }
		if (self->controller_) { return; }
		self->SpawnController(env);
	});
	// safety timer: clears creating_ on failure / never-arrived callback
	::SetTimer(GetSafeHwnd(), PCR_TIMER_ENV, 500, NULL);
}

void CCustomDHtmlDialog::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == PCR_TIMER_KICK)
	{
		if (!::IsWindow(GetSafeHwnd())) { return; }
		::KillTimer(GetSafeHwnd(), PCR_TIMER_KICK);
		StartCreate();
		return;
	}

	if (nIDEvent == PCR_TIMER_ENV)
	{
		if (!::IsWindow(GetSafeHwnd())) { return; }
		if (controller_) {
			::KillTimer(GetSafeHwnd(), PCR_TIMER_ENV);
			creating_ = false;
		} else if (pcrweb::GetEnvFailed()) {
			::KillTimer(GetSafeHwnd(), PCR_TIMER_ENV);
			creating_ = false;
			pcrweb::Log(L"env failed -> give up (runtime missing?)");
		}
		return;
	}
	CDialog::OnTimer(nIDEvent);
}

void CCustomDHtmlDialog::CreateController()
{
	pcrweb::Log(L"CreateController enter");
	ICoreWebView2Environment* env = pcrweb::GetEnvironment();
	if (!env || controller_ || !::IsWindow(GetSafeHwnd())) { return; }

	HWND hwnd = GetSafeHwnd();
	HRESULT hr = env->CreateCoreWebView2Controller(hwnd,
		Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
			[this, hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
				if (!::IsWindow(hwnd)) { return S_OK; }
				return OnControllerCreated(result, controller);
			}).Get());

	pcrweb::Log(L"CreateController spawn hr=0x%08x", hr);
	if (FAILED(hr)) {
		pcrweb::Log(L"CreateCoreWebView2Controller failed hr=0x%08x", hr);
	}
}

HRESULT CCustomDHtmlDialog::OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller)
{
	pcrweb::Log(L"ctl created hr=0x%08x tid=%u", result, ::GetCurrentThreadId());
	if (FAILED(result) || !controller) {
		pcrweb::Log(L"controller completed hr=0x%08x", result);
		return S_OK;
	}
	controller_ = controller;
	controller_->AddRef();  //[pcr-fix] completion hands us an UNADDREFED pointer;
	                          // without our own ref the glue releases the controller on
	                          // return and the whole webview is torn down (no nav events).
	HRESULT hrCore = controller_->get_CoreWebView2(&webview_);
	pcrweb::Log(L"get_CoreWebView2 hr=0x%08x webview=%p", hrCore, (void*)webview_);
	if (!webview_) { return S_OK; }

	ComPtr<ICoreWebView2Settings> settings;
	webview_->get_Settings(&settings);
	if (settings) {
		settings->put_AreDefaultContextMenusEnabled(FALSE);
		settings->put_AreDevToolsEnabled(FALSE);
		settings->put_IsStatusBarEnabled(FALSE);
		settings->put_IsZoomControlEnabled(FALSE);
	}
	ComPtr<ICoreWebView2Controller4> controller4;
	controller->QueryInterface(IID_PPV_ARGS(&controller4));
	if (controller4) {
		controller4->put_AllowExternalDrop(FALSE); // former put_RegisterAsDropTarget(FALSE)
	}

	UpdateBounds();
	navAllow_ = 3;   //[pcr-web] debug: let initial navigations through
	WireEvents();

	// register bridge, then load a clean about:blank that has window.pcr
	std::wstring bridge = pcrweb::BridgeScript();
	HRESULT hr = webview_->AddScriptToExecuteOnDocumentCreated(bridge.c_str(),
		Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
			[this](HRESULT error, LPCWSTR /*id*/) -> HRESULT {
				if (!::IsWindow(GetSafeHwnd())) { return S_OK; }
				if (FAILED(error)) {
					pcrweb::Log(L"bridge register failed hr=0x%08x", error);
					return S_OK;
				}
				bridgeOk_ = true;
				pcrweb::Log(L"bridge registered");
				navAllow_ = 1;   // the initial blank navigation
				webview_->Navigate(L"about:blank");
				return S_OK;
			}).Get());
	if (FAILED(hr)) { pcrweb::Log(L"AddScript spawn failed hr=0x%08x", hr); }
	else { pcrweb::Log(L"AddScript spawned ok"); }
	return S_OK;
}

void CCustomDHtmlDialog::WireEvents()
{
	pcrweb::Log(L"WireEvents enter");	// navigation: only the initial about:blank is allowed (former OnBeforeNavigate2)
	HRESULT hrW_NavigationStarting = webview_->add_NavigationStarting(
		Callback<ICoreWebView2NavigationStartingEventHandler>(
			[this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
				BOOL cancel = TRUE;
				if (navAllow_ > 0) { --navAllow_; cancel = FALSE; }
				pcrweb::Log(L"navstart cancel=%d allow=%d", (int)cancel, navAllow_);
				args->put_Cancel(cancel);
				return S_OK;
			}).Get(), &tokNavStart_);

	HRESULT hrW_NavigationCompleted = webview_->add_NavigationCompleted(
		Callback<ICoreWebView2NavigationCompletedEventHandler>(
			[this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
				if (!::IsWindow(GetSafeHwnd())) { return S_OK; }
				if (!ready_ && bridgeOk_) {
					ready_ = true;
					pcrweb::Log(L"navdone tid=%u", ::GetCurrentThreadId());
					// locate child window (subclass target for utl::BrowserHook)
					webchild_ = ::FindWindowEx(GetSafeHwnd(), NULL, L"Chrome_WidgetWin_0", NULL);
					pcrweb::Log(L"webchild=%p", (void*)webchild_);
					UpdateBounds();
					OnWebReady();
					// initial document complete (former about:blank eventsink)
					pendingUrl_ = L"about:blank";
					::PostMessageW(GetSafeHwnd(), WM_PCR_DOC, 0, 0);
				}
				return S_OK;
			}).Get(), &tokNavDone_);

	HRESULT hrW_WebMessageReceived = webview_->add_WebMessageReceived(
		Callback<ICoreWebView2WebMessageReceivedEventHandler>(
			[this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
				if (!::IsWindow(GetSafeHwnd())) { return S_OK; }
				LPWSTR json = NULL;
				if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
					DispatchWebMessage(json);
					CoTaskMemFree(json);
				}
				return S_OK;
			}).Get(), &tokMsg_);

	// context menu: prefer ICoreWebView2_11 event; JS 'context' message is the fallback
	ComPtr<ICoreWebView2_11> webview11;
	webview_->QueryInterface(IID_PPV_ARGS(&webview11));
	if (webview11) {
		webview11->add_ContextMenuRequested(
			Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
				[this](ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs* args) -> HRESULT {
					if (!::IsWindow(GetSafeHwnd())) { return S_OK; }
					args->put_Handled(TRUE);

					POINT pt = {};
					if (FAILED(args->get_Location(&pt))) {
						::GetCursorPos(&pt);
					} else {
						// get_Location is webview-client (device-independent px); to screen via child
						HWND child = GetSafeBrowserHwnd();
						if (child) { ::ClientToScreen(child, &pt); }
						else { ::ClientToScreen(GetSafeHwnd(), &pt); }
					}

					DWORD menuID = PCRMENU_DEFAULT;
					ComPtr<ICoreWebView2ContextMenuTarget> target;
					if (SUCCEEDED(args->get_ContextMenuTarget(&target)) && target) {
						COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND kind = COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_PAGE;
						target->get_Kind(&kind);
						switch (kind) {
						case COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_IMAGE:
							menuID = PCRMENU_IMAGE;
							break;
						case COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_SELECTED_TEXT:
							menuID = PCRMENU_TEXTSELECT;
							break;
						case COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_AUDIO:
						case COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_VIDEO:
							menuID = PCRMENU_CONTROL;
							break;
						default:
							break;
						}
					}

					PcrElement el;
					GetElementAt(CPoint(pt), el);
					if (menuID == PCRMENU_DEFAULT || menuID == PCRMENU_TEXTSELECT) {
						if (el.valid() && !el.href.empty()) {
							menuID = PCRMENU_ANCHOR;
						} else if (menuID == PCRMENU_DEFAULT) {
							std::wstring sel;
							if (GetSelectedText(sel) && !sel.empty()) { menuID = PCRMENU_TEXTSELECT; }
						}
					}

					OnBrowserContext(menuID, &pt, el);
					return S_OK;
				}).Get(), &tokCtx_);
	}

	// target=_blank etc.: open with configured external browser
	HRESULT hrW_NewWindowRequested = webview_->add_NewWindowRequested(
		Callback<ICoreWebView2NewWindowRequestedEventHandler>(
			[this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
				LPWSTR uri = NULL;
				if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
					utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, uri);
					CoTaskMemFree(uri);
				}
				args->put_Handled(TRUE);
				return S_OK;
			}).Get(), &tokNewWin_);

	HRESULT hrW_ProcessFailed = webview_->add_ProcessFailed(
		Callback<ICoreWebView2ProcessFailedEventHandler>(
			[this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
				COREWEBVIEW2_PROCESS_FAILED_KIND kind = (COREWEBVIEW2_PROCESS_FAILED_KIND)0;
				args->get_ProcessFailedKind(&kind);
				pcrweb::Log(L"process failed kind=%d", (int)kind);
				if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
					ready_ = false;
				}
				return S_OK;
			}).Get(), &tokFail_);
	pcrweb::Log(L"WireEvents done");
	pcrweb::Log(L"add_NavigationStarting hr=0x%08x", hrW_NavigationStarting);
	pcrweb::Log(L"add_NavigationCompleted hr=0x%08x", hrW_NavigationCompleted);
	pcrweb::Log(L"add_WebMessageReceived hr=0x%08x", hrW_WebMessageReceived);
	pcrweb::Log(L"add_NewWindowRequested hr=0x%08x", hrW_NewWindowRequested);
	pcrweb::Log(L"add_ProcessFailed hr=0x%08x", hrW_ProcessFailed);
}

// ---------------------------------------------------------------- messages

BEGIN_MESSAGE_MAP(CCustomDHtmlDialog, CDialog)
	ON_MESSAGE(WM_PCR_BROWSERKEY, &CCustomDHtmlDialog::OnBrowserKeyMsg)
	ON_MESSAGE(WM_PCR_DOC, &CCustomDHtmlDialog::OnDocMsg)
	//[pcr-t3] WM_PCR_ENVSTRM hop removed (controller now spawns inside env cb)
END_MESSAGE_MAP()

void CCustomDHtmlDialog::SpawnController(ICoreWebView2Environment* envRaw)
{
	ComPtr<ICoreWebView2Environment> env = envRaw;
	pcrweb::Log(L"SpawnController tid=%u", ::GetCurrentThreadId());
	if (env && !controller_ && ::IsWindow(GetSafeHwnd())) {
		CRect r; GetClientRect(&r);
		HWND container = MakeWebContainer(GetSafeHwnd(), r);
		HWND hwnd = container ? container : GetSafeHwnd();
		pcrweb::Log(L"parent hwnd=%p container=%p", (void*)hwnd, (void*)container);
		HRESULT hrC = env->CreateCoreWebView2Controller(hwnd,
			Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
				[this, hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
					if (!::IsWindow(hwnd)) { return S_OK; }
					return OnControllerCreated(result, controller);
				}).Get());
		pcrweb::Log(L"CreateController(UI) spawn hr=0x%08x", hrC);
	}
}

LRESULT CCustomDHtmlDialog::OnBrowserKeyMsg(WPARAM, LPARAM lParam)
{
	MSG msg = *(MSG*)lParam;
	return OnBrowserKey(msg);
}

LRESULT CCustomDHtmlDialog::OnDocMsg(WPARAM, LPARAM)
{
	std::wstring url = pendingUrl_;
	pcrweb::Log(L"docmsg");
	OnDocumentComplete(url);
	return 0;
}

void CCustomDHtmlDialog::DispatchWebMessage(const std::wstring& json)
{
	std::wstring body = json;
	if (!body.empty() && body[0] == L'"') {
		body = pcrweb::UnwrapJson(body); // older runtimes send the string itself
	}

	std::wstring e;
	if (!pcrweb::JsonGetStr(body, L"e", e)) { return; }

	if (e == L"scroll") {
		double prog = 0;
		pcrweb::JsonGetNum(body, L"prog", prog);
		OnBrowserScroll(prog != 0);
		return;
	}

	if (e == L"context") {
		// fallback when ICoreWebView2_11 is unavailable (runtime too old)
		if (tokCtx_.value != 0) { return; } // native handler already active
		double x = 0, y = 0;
		pcrweb::JsonGetNum(body, L"x", x);
		pcrweb::JsonGetNum(body, L"y", y);
		HWND child = GetSafeBrowserHwnd();
		if (!child) { return; }
		double zoom = 1.0;
		if (controller_) {
		ICoreWebView2Controller* zc = controller_;
		CallWeb([zc, &zoom]() { if (zc) { zc->get_ZoomFactor(&zoom); } });
	}
		double dpr = custom_.dpr > 0 ? custom_.dpr : 1.0;
		POINT pt = { (LONG)llround(x * dpr * zoom), (LONG)llround(y * dpr * zoom) };
		::ClientToScreen(child, &pt);
		CPoint sp(pt);
		PcrElement el;
		GetElementAt(sp, el);
		DWORD menuID = PCRMENU_DEFAULT;
		if (el.valid() && !el.href.empty()) { menuID = PCRMENU_ANCHOR; }
		else {
			std::wstring sel;
			if (GetSelectedText(sel) && !sel.empty()) { menuID = PCRMENU_TEXTSELECT; }
		}
		OnBrowserContext(menuID, &sp, el);
		return;
	}

	PcrElement el;
	ParseElement(body, el);
	if (!el.valid()) { return; }

	if (e == L"click")       { OnAnchorMouseClick(el); }
	else if (e == L"over")   { OnAnchorMouseOver(el); }
	else if (e == L"out")    { OnAnchorMouseOut(el); }
}

void CCustomDHtmlDialog::ParseElement(const std::wstring& json, PcrElement& el)
{
	el = PcrElement();
	pcrweb::JsonGetStr(json, L"tag", el.tag);
	pcrweb::JsonGetStr(json, L"id", el.id);
	pcrweb::JsonGetStr(json, L"cls", el.cls);
	pcrweb::JsonGetStr(json, L"href", el.href);
	pcrweb::JsonGetStr(json, L"text", el.text);
	double v = 0;
	if (pcrweb::JsonGetNum(json, L"x", v)) el.x = v;
	if (pcrweb::JsonGetNum(json, L"y", v)) el.y = v;
	if (pcrweb::JsonGetNum(json, L"w", v)) el.w = v;
	if (pcrweb::JsonGetNum(json, L"h", v)) el.h = v;
	if (pcrweb::JsonGetNum(json, L"dpr", v) && v > 0) el.dpr = v;
}

// ---------------------------------------------------------------- lifecycle

//[pcr-web] deferred start (see PCR_TIMER_KICK): WM_INITDIALOG chain kills WebView2 callbacks
BOOL CCustomDHtmlDialog::OnInitDialog()
{
	pcrweb::Log(L"CDHtml ctor-based OnInit enter");
	CDialog::OnInitDialog();
	pcrweb::Log(L"CDHtml base OnInit done -> deferred kick");
	::SetTimer(GetSafeHwnd(), PCR_TIMER_KICK, 15, NULL);
	return TRUE;
}

void CCustomDHtmlDialog::UpdateBounds()
{
	if (!controller_) { return; }
	CRect rc;
	GetClientRect(&rc);
	ICoreWebView2Controller* ctl = controller_;
	CallWeb([ctl, rc]() {
		if (!ctl) { return; }
		HRESULT hrB = ctl->put_Bounds(rc);
		static int s_bt = 0;
		if (s_bt < 6) { s_bt++; pcrweb::Log(L"bounds %ldx%ld hr=0x%08x", (long)rc.Width(), (long)rc.Height(), hrB); }
	});
}

void CCustomDHtmlDialog::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	UpdateBounds();
}

void CCustomDHtmlDialog::OnDestroy()
{
	::KillTimer(GetSafeHwnd(), PCR_TIMER_ENV);
	//[pcr-marsh] teardown on the owning apartment; members cleared now so no
	//UI path touches them again (the posted task keeps its own refs).
	ICoreWebView2* wv = webview_;
	ICoreWebView2Controller* ctl = controller_;
	const EventRegistrationToken tNs = tokNavStart_, tNd = tokNavDone_, tMsg = tokMsg_,
	                          tCtx = tokCtx_, tNw = tokNewWin_, tPf = tokFail_;
	webview_ = NULL;
	controller_ = NULL;
	webchild_ = NULL;
	ready_ = false;
	if (wv || ctl) {
		pcrweb::PostWebTask([wv, ctl, tNs, tNd, tMsg, tCtx, tNw, tPf]() {
			if (wv) {
				ComPtr<ICoreWebView2_11> w11;
				if (SUCCEEDED(wv->QueryInterface(IID_PPV_ARGS(&w11))) && w11) {
					w11->remove_ContextMenuRequested(tCtx);
				}
				wv->remove_NavigationStarting(tNs);
				wv->remove_NavigationCompleted(tNd);
				wv->remove_WebMessageReceived(tMsg);
				wv->remove_NewWindowRequested(tNw);
				wv->remove_ProcessFailed(tPf);
			}
			if (ctl) {
				ctl->put_IsVisible(FALSE);
				ctl->Close();
			}
			if (wv) { wv->Release(); }
			if (ctl) { ctl->Release(); }
		});
	}
	CDialog::OnDestroy();
}

BOOL CCustomDHtmlDialog::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE)
		{
			return FALSE;
		}
	}
	return CDialog::PreTranslateMessage(pMsg);
}

// ---------------------------------------------------------------- state / js

HWND CCustomDHtmlDialog::GetSafeBrowserHwnd()
{
	return webchild_;
}

BOOL CCustomDHtmlDialog::GetBrowserSize(CSize& size)
{
	HWND hwnd = GetSafeBrowserHwnd();
	if (!hwnd || !::IsWindow(hwnd)) { return FALSE; }
	CRect rc;
	::GetClientRect(hwnd, &rc);
	size = rc.Size();
	return TRUE;
}

BOOL CCustomDHtmlDialog::Eval(const std::wstring& js, std::wstring* result, DWORD timeoutMs)
{
	if (!webview_ || !ready_) { return FALSE; }

	struct Ctx {
		HANDLE ev;
		HRESULT hr;
		std::wstring val;
		volatile bool done;
	} ctx = { NULL, E_FAIL, std::wstring(), false };
	ctx.ev = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!ctx.ev) { return FALSE; }

	ICoreWebView2* wvEval = webview_;
	std::wstring jsCopy = js;
	HRESULT hr = E_FAIL;
	std::function<void()> spawn = [wvEval, jsCopy, &ctx, &hr]() {
		if (!wvEval) { hr = E_POINTER; return; }
		hr = wvEval->ExecuteScript(jsCopy.c_str(),
			Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
				[&ctx](HRESULT error, LPCWSTR result) -> HRESULT {
					ctx.hr = error;
					if (result) { ctx.val = result; }
					ctx.done = true;
					SetEvent(ctx.ev);
					return S_OK;
				}).Get());
	};
	if (!CallWeb(spawn, timeoutMs)) { CloseHandle(ctx.ev); return FALSE; }
	if (FAILED(hr)) { CloseHandle(ctx.ev); return FALSE; }

	DWORD t0 = GetTickCount();
	while (!ctx.done) {
		MSG m;
		while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&m);
			DispatchMessage(&m);
		}
		if (WaitForSingleObject(ctx.ev, 10) == WAIT_OBJECT_0) { break; }
		if (GetTickCount() - t0 > timeoutMs) { break; }
	}
	CloseHandle(ctx.ev);

	if (!ctx.done || FAILED(ctx.hr)) { return FALSE; }
	if (result) {
		// ExecuteScript JSON-encodes the value; strip one string wrapper so
		// bridge JSON objects ("{...}") parse directly.
		*result = pcrweb::UnwrapJson(ctx.val);
	}
	return TRUE;
}

BOOL CCustomDHtmlDialog::EvalBoolOk(const std::wstring& js)
{
	pcrweb::Log(L"evalboolok: %.40s", js.c_str());
	std::wstring r;
	if (!Eval(js, &r)) { return FALSE; }
	return r.find(L"ok") != std::wstring::npos;
}

std::wstring CCustomDHtmlDialog::EvalString(const std::wstring& js, DWORD timeoutMs)
{
	std::wstring r;
	if (!Eval(js, &r, timeoutMs)) { return std::wstring(); }
	return pcrweb::UnwrapJson(r);
}

// ---------------------------------------------------------------- content

BOOL CCustomDHtmlDialog::LoadFromString(const std::wstring& html)
{
	std::wstring js = L"pcr.open(\"" + pcrweb::JsEscape(html) + L"\")";
	pcrweb::Log(L"open eval %d chars", (int)html.size());
	if (!EvalBoolOk(js)) { return FALSE; }
	pcrweb::Log(L"open eval done");
	pendingUrl_ = L"about:blank";
	::PostMessageW(GetSafeHwnd(), WM_PCR_DOC, 0, 0);
	return TRUE;
}

BOOL CCustomDHtmlDialog::AppendDocument(const std::wstring& html)
{
	std::wstring js = L"pcr.append(\"" + pcrweb::JsEscape(html) + L"\")";
	return EvalBoolOk(js);
}

// ---------------------------------------------------------------- scroll

BOOL CCustomDHtmlDialog::GetScrollParam(long& scrollTop, long& scrollHeight, long& clientHeight)
{
	std::wstring r;
	if (!Eval(L"pcr.scroll()", &r)) { return FALSE; }
	double top = 0, sh = 0, ch = 0, dpr = 0;
	if (!pcrweb::JsonGetNum(r, L"top", top)) { return FALSE; }
	if (!pcrweb::JsonGetNum(r, L"sh", sh)) { return FALSE; }
	if (!pcrweb::JsonGetNum(r, L"ch", ch)) { return FALSE; }
	if (pcrweb::JsonGetNum(r, L"dpr", dpr) && dpr > 0) { custom_.dpr = dpr; }
	scrollTop = (long)top;
	scrollHeight = (long)sh;
	clientHeight = (long)ch;
	return TRUE;
}

BOOL CCustomDHtmlDialog::StoreScroll()
{
	return GetScrollParam(custom_.scrollTop, custom_.scrollHeight, custom_.clientHeight);
}

BOOL CCustomDHtmlDialog::RestoreScroll(BOOL smooth)
{
	long top = 0, sh = 0, ch = 0;
	if (!GetScrollParam(top, sh, ch)) { return FALSE; }
	custom_.scroll = true;
	//[pcr-web] smooth = IE-era behavior (IE animated window.scroll; the new-
	//res reveal relied on that). Timer auto-scroll (ScrollToBottomBy) stays
	//instant, its per-tick delta already is the step animation. Resize
	//restores pass smooth=FALSE (animation during window drag looks broken).
	LPCWSTR sw = smooth ? L", 1" : L", 0";
	if (custom_.scrollHeight - custom_.clientHeight - custom_.scrollTop <= 0)
	{
		return EvalBoolOk(L"pcr.scrollTo(" + std::to_wstring((long long)sh) + sw);
	}
	return EvalBoolOk(L"pcr.scrollTo(" + std::to_wstring((long long)custom_.scrollTop) + sw);
}

BOOL CCustomDHtmlDialog::ScrollToTop()
{
	custom_.scroll = true;
	return EvalBoolOk(L"pcr.scrollTo(0, 1)");
}

BOOL CCustomDHtmlDialog::ScrollToBottom()
{
	long top = 0, sh = 0, ch = 0;
	if (!GetScrollParam(top, sh, ch)) { return FALSE; }
	custom_.scroll = true;
	return EvalBoolOk(L"pcr.scrollTo(" + std::to_wstring((long long)sh) + L", 1)");
}

BOOL CCustomDHtmlDialog::ScrollToBottomBy(long y)
{
	long top = 0, sh = 0, ch = 0;
	if (!GetScrollParam(top, sh, ch)) { return FALSE; }
	if (sh - ch - top > 0)
	{
		custom_.scroll = true;
		return EvalBoolOk(L"pcr.scrollBy(" + std::to_wstring((long long)y) + L")");
	}
	return FALSE;
}

// ---------------------------------------------------------------- dom

BOOL CCustomDHtmlDialog::RemoveClassName(const std::wstring& search)
{
	std::wstring r;
	std::wstring js = L"pcr.removeClassName(\"" + pcrweb::JsEscape(search) + L"\")";
	if (!Eval(js, &r)) { return FALSE; }
	return r != L"0" && !r.empty();
}

double CCustomDHtmlDialog::GetZoomFactor()
{
	// former: body rect physical width / offsetWidth logical width (= dpr * zoom)
	std::wstring r = EvalString(L"pcr.scale()");
	if (!r.empty()) {
		double v = wcstod(r.c_str(), NULL);
		if (v > 0) { scale_ = v; }
	}
	return scale_;
}

BOOL CCustomDHtmlDialog::SetZoomFactor(double factor)
{
	if (!controller_) { return FALSE; }
	HRESULT hrZ = E_FAIL;
	ICoreWebView2Controller* zcS = controller_;
	CallWeb([zcS, factor, &hrZ]() { if (zcS) { hrZ = zcS->put_ZoomFactor(factor); } });
	if (SUCCEEDED(hrZ)) {
		scale_ = 0; // refresh on next GetZoomFactor
		return TRUE;
	}
	return FALSE;
}

BOOL CCustomDHtmlDialog::GetBoundingClientRect(const PcrElement& el, CRect& rc)
{
	if (!el.valid()) { return FALSE; }
	// controller zoom is kept at 1.0; el rect is CSS px, multiply dpr for physical px
	double scale = ElementScale(el, 1.0);
	if (controller_) {
		double z = 1.0;
		HRESULT hrG = E_FAIL;
		ICoreWebView2Controller* zcG = controller_;
		CallWeb([zcG, &z, &hrG]() { if (zcG) { hrG = zcG->get_ZoomFactor(&z); } });
		if (SUCCEEDED(hrG) && z > 0) { scale = el.dpr * z; }
	}
	rc.left   = (LONG)llround(el.x * scale);
	rc.top    = (LONG)llround(el.y * scale);
	rc.right  = (LONG)llround((el.x + el.w) * scale);
	rc.bottom = (LONG)llround((el.y + el.h) * scale);
	return TRUE;
}

BOOL CCustomDHtmlDialog::GetBoundingClientZoomRect(const PcrElement& el, CRect& rc)
{
	return GetBoundingClientRect(el, rc);
}

BOOL CCustomDHtmlDialog::GetElementAt(const CPoint& screen, PcrElement& el)
{
	HWND hwnd = GetSafeBrowserHwnd();
	if (!hwnd || !::IsWindow(hwnd)) { return FALSE; }
	CPoint pt(screen);
	::ScreenToClient(hwnd, &pt);
	double scale = custom_.dpr * GetZoomFactor();
	if (scale <= 0) { scale = 1.0; }
	// pt is physical px; CSS px = physical / (dpr*zoom)
	double cssDpr = custom_.dpr > 0 ? custom_.dpr : 1.0;
	double zoom = 1.0;
	if (controller_) {
		ICoreWebView2Controller* zc = controller_;
		CallWeb([zc, &zoom]() { if (zc) { zc->get_ZoomFactor(&zoom); } });
	}
	double divisor = cssDpr * (zoom > 0 ? zoom : 1.0);
	if (divisor <= 0) { divisor = 1.0; }
	double x = pt.x / divisor, y = pt.y / divisor;
	std::wstring r;
	std::wstring js = L"pcr.hit(" + std::to_wstring((long long)llround(x)) + L"," + std::to_wstring((long long)llround(y)) + L")";
	if (!Eval(js, &r)) { return FALSE; }
	if (r == L"null" || r.empty()) { return FALSE; }
	ParseElement(r, el);
	return el.valid();
}

BOOL CCustomDHtmlDialog::GetElementById(const std::wstring& id, PcrElement& el)
{
	std::wstring r;
	std::wstring js = L"pcr.anchor(\"" + pcrweb::JsEscape(id) + L"\")";
	if (!Eval(js, &r)) { return FALSE; }
	if (r == L"null" || r.empty()) { return FALSE; }
	ParseElement(r, el);
	return el.valid();
}

BOOL CCustomDHtmlDialog::ScrollIntoViewId(const std::wstring& id)
{
	//[pcr-web] smooth: IE animated scrollIntoView (res anchor jumps)
	return EvalBoolOk(L"pcr.scrollIntoView(\"" + pcrweb::JsEscape(id) + L"\", 1)");
}

BOOL CCustomDHtmlDialog::IsTextArea(const CPoint& screen)
{
	// former behavior: any element hit inside the browser counts
	HWND hwnd = GetSafeBrowserHwnd();
	if (!hwnd || !::IsWindow(hwnd)) { return FALSE; }
	CPoint pt(screen);
	HWND hit = ::WindowFromPoint(pt);
	if (hit != hwnd) {
		// WebView2 child may be nested; accept any descendant of the dialog
		HWND top = ::GetAncestor(hit, GA_ROOT);
		if (top != GetSafeHwnd()) { return FALSE; }
	}
	PcrElement el;
	return GetElementAt(pt, el) ? TRUE : FALSE;
}

// ---------------------------------------------------------------- selection

BOOL CCustomDHtmlDialog::GetSelectedText(std::wstring& text)
{
	text = EvalString(L"pcr.selectText()");
	return TRUE;
}

BOOL CCustomDHtmlDialog::SelectAll()
{
	return EvalBoolOk(L"pcr.selectAll()");
}

BOOL CCustomDHtmlDialog::CopySelectedText()
{
	std::wstring text;
	if (!GetSelectedText(text) || text.empty()) { return FALSE; }
	return CopyText(text);
}

BOOL CCustomDHtmlDialog::CopyText(const std::wstring& text)
{
	if (OpenClipboard())
	{
		EmptyClipboard();
		size_t size = text.size() + 1;
		HGLOBAL hg = GlobalAlloc(GHND | GMEM_SHARE, sizeof(WCHAR) * size);
		LPWSTR mem = (LPWSTR)GlobalLock(hg);

		wcsncpy_s(mem, size, text.c_str(), _TRUNCATE);
		GlobalUnlock(hg);

		SetClipboardData(CF_UNICODETEXT, hg);

		CloseClipboard();
		return TRUE;
	}
	return FALSE;
}

BOOL CCustomDHtmlDialog::CopyAnchorHref(const PcrElement& el)
{
	if (el.href.empty()) { return FALSE; }
	return CopyText(el.href);
}

BOOL CCustomDHtmlDialog::GetAnchorHref(const PcrElement& el, std::wstring& href)
{
	if (el.href.empty()) { return FALSE; }
	href = el.href;
	return TRUE;
}

BOOL CCustomDHtmlDialog::OpenWebSearch()
{
	std::wstring str;
	if (!GetSelectedText(str) || str.empty()) { return FALSE; }

	std::wstring url = L"http://www.google.co.jp/search?q=" + utl::toUTF16(utl::urlencode(utl::toUTF8(str)));
	utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, url);
	return TRUE;
}

BOOL CCustomDHtmlDialog::IsEnableCommand(ULONG id)
{
	switch (id)
	{
	case OLECMDID_COPY:
	{
		std::wstring sel;
		GetSelectedText(sel);
		return sel.empty() ? FALSE : TRUE;
	}
	case OLECMDID_SELECTALL:
		return ready_ ? TRUE : FALSE;
	}
	return FALSE;
}

BOOL CCustomDHtmlDialog::GetSource(std::wstring& source)
{
	source = EvalString(L"pcr.source()", 10000);
	return !source.empty();
}

void CCustomDHtmlDialog::ChangeFocus()
{
	CWnd* wnd = GetFocus();
	if (wnd)
	{
		CIMEEdit::through() = true;
		wnd->SendMessage(WM_KILLFOCUS, NULL, 0);
		wnd->SendMessage(WM_SETFOCUS, NULL, 0);
		CIMEEdit::through() = false;
	}
}

// ---------------------------------------------------------------- search

BOOL CCustomDHtmlDialog::CreateBookmarks(const std::wstring& search, std::vector<std::wstring>& bookmarks)
{
	bookmarks.clear();
	std::wstring r = EvalString(L"pcr.find(\"" + pcrweb::JsEscape(search) + L"\")");
	int n = _wtoi(r.c_str());
	for (int i = 0; i < n; ++i) { bookmarks.push_back(std::to_wstring(i)); }
	return !bookmarks.empty();
}

BOOL CCustomDHtmlDialog::CreateBookmarkMap(const std::wstring& search, BookmarkMap& map)
{
	map.clear();
	std::vector<std::wstring> v;
	if (!CreateBookmarks(search, v)) { return FALSE; }
	int pos = 0;
	for (auto& k : v) { map.insert(std::make_pair(k, ++pos)); }
	return !map.empty();
}

BOOL CCustomDHtmlDialog::SearchNext(const std::wstring& search, bool next, size_t& pos, size_t& total)
{
	std::wstring r = EvalString(L"pcr.nav(\"" + pcrweb::JsEscape(search) + L"\"," + std::wstring(next ? L"1" : L"-1") + L")");
	double idx = 0, tot = 0;
	if (!pcrweb::JsonGetNum(r, L"idx", idx)) { return FALSE; }
	if (!pcrweb::JsonGetNum(r, L"total", tot)) { return FALSE; }
	if (idx <= 0 || tot <= 0) { return FALSE; }
	pos = (size_t)idx;
	total = (size_t)tot;
	return TRUE;
}

// incremental highlight (former IMarkupPointer based HighLight).
// count<0: all remaining from start. returns marked count.
int CCustomDHtmlDialog::HighlightMatches(const std::wstring& search, const std::wstring& styleAttr, int start, int count)
{
	std::wstring r = EvalString(
		L"pcr.mark(\"" + pcrweb::JsEscape(search) + L"\",\"" + pcrweb::JsEscape(styleAttr) +
		L"\"," + std::to_wstring(start) + L"," + std::to_wstring(count) + L")");
	double marked = 0;
	if (!pcrweb::JsonGetNum(r, L"marked", marked)) { return 0; }
	return (int)marked;
}

BOOL CCustomDHtmlDialog::UnhighlightMatches()
{
	return EvalBoolOk(L"pcr.unmark()");
}

BOOL CCustomDHtmlDialog::SearchParentIds(const std::wstring& search, const std::wstring& prefix, std::vector<std::wstring>& ids)
{
	ids.clear();
	std::wstring arr = EvalString(L"pcr.parentIds(\"" + pcrweb::JsEscape(search) + L"\",\"" + pcrweb::JsEscape(prefix) + L"\")");
	if (arr.empty()) { return FALSE; }
	// parse flat ["a","b"]
	size_t p = 0;
	while ((p = arr.find(L'\"', p)) != std::wstring::npos) {
		size_t q = arr.find(L'\"', p + 1);
		if (q == std::wstring::npos) { break; }
		std::wstring v = arr.substr(p + 1, q - p - 1);
		// unescape minimal
		std::wstring u;
		for (size_t i = 0; i < v.size(); ++i) {
			if (v[i] == L'\\' && i + 1 < v.size()) { ++i; u += v[i]; }
			else { u += v[i]; }
		}
		ids.push_back(u);
		p = q + 1;
	}
	return !ids.empty();
}
