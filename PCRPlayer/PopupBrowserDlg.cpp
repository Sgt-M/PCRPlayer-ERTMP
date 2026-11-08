// PopupBrowserDlg.cpp : implementation file
//[pcr-web] WebView2 based implementation

#include "stdafx.h"
#include "PCRPlayer.h"
#include "PopupBrowserDlg.h"
#include "ExtractDlg.h"
#include "DHtmlUtl.h"

enum TIMER_ID {
	TIMER_ID_POPUP,
};

#define TARGET_EXPAND 30
#define TARGET_MARGIN 8


// CPopupBrowserDlg dialog

IMPLEMENT_DYNCREATE(CPopupBrowserDlg, CCustomDHtmlDialog)

CPopupBrowserDlg::CPopupBrowserDlg(CCustomDHtmlDialog* pParent /*=NULL*/, CCustomDHtmlDialog* pTarget /*=NULL*/)
	: CCustomDHtmlDialog(IDD_POPUP_BROWSER, /*IDR_HTML_POPUP_BROWSER,*/ pParent)
	, parent_(pParent)
	, target_(pTarget ? pTarget : pParent)
	, popup_(NULL)
	, counter_(0)
{
}

CPopupBrowserDlg::~CPopupBrowserDlg()
{
	gl_.callback.extract.pop(this);
}

//=============================================================================

void CPopupBrowserDlg::onMouseMove()
{
	if (parent_ && !this_.hover)
	{
		CPoint pt;
		if (GetCursorPos(&pt))
		{
			CWnd* wnd = WindowFromPoint(pt);
			if (wnd->GetSafeHwnd() == parent_->GetSafeHwnd() ||
				wnd->GetSafeHwnd() == parent_->GetSafeBrowserHwnd())
			{
				TRACKMOUSEEVENT tme;
				tme.cbSize = sizeof(tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = wnd->GetSafeHwnd();
				tme.dwHoverTime = HOVER_DEFAULT;
				TrackMouseEvent(&tme);
				this_.hover = true;
			}
		}
	}

	if (this_.target.IsRectEmpty()) { return; }

	if (this_.cancel ||
		(popup_ && ::IsWindow(popup_->GetSafeHwnd()) && ::IsWindowVisible(popup_->GetSafeHwnd())))
	{
		return;
	}

	CPoint pt;
	if (GetCursorPos(&pt))
	{
		CRect rc(this_.target);
		if (!this_.readOnly && this_.margin)
		{
			if (this_.horizontal)
			{
				rc.InflateRect(TARGET_EXPAND, TARGET_MARGIN);
			}
			else
			{
				rc.InflateRect(TARGET_MARGIN, TARGET_EXPAND);
			}
		}
		if (rc.PtInRect(pt)) { return; }
	}

	this_.target.SetRectEmpty();
	showWindow(false);
}

bool CPopupBrowserDlg::onMouseLeave()
{
	this_.hover = false;
	CPoint pt;
	if (GetCursorPos(&pt))
	{
		CWnd* wnd = WindowFromPoint(pt);
		if (wnd)
		{
			if (parent_ && wnd->GetSafeHwnd() == parent_->GetSafeHwnd())
			{
				TRACKMOUSEEVENT tme;
				tme.cbSize = sizeof(tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = parent_->GetSafeHwnd();
				tme.dwHoverTime = HOVER_DEFAULT;
				TrackMouseEvent(&tme);
				this_.hover = true;
				return true;
			}
			else if (parent_ && wnd->GetSafeHwnd() == parent_->GetSafeBrowserHwnd())
			{
				// TrackMouseEvent breaks scrolling
				return true;
			}
			else if (wnd->GetSafeHwnd() == GetSafeHwnd() ||
				wnd->GetSafeHwnd() == GetSafeBrowserHwnd())
			{
				return true;
			}
		}
	}

	if (this_.cancel ||
		(popup_ && ::IsWindow(popup_->GetSafeHwnd()) && ::IsWindowVisible(popup_->GetSafeHwnd())))
	{
		return true;
	}

	showWindow(false);
	return false;
}

void CPopupBrowserDlg::hide()
{
	this_.cancel = false;
	showWindow(false);
	if (popup_) { popup_->hide(); }
}

BOOL CPopupBrowserDlg::popup(const CRect& area, const PopupBrowserConfig& cfg, bool margin)
{
	cfg_ = cfg;
	this_.margin = false;
	return preparePopup(area);
}

BOOL CPopupBrowserDlg::popup(const PcrElement& el, const PopupBrowserConfig& cfg)
{
	return popup(NULL, el, cfg);
}

BOOL CPopupBrowserDlg::popup(CPopupBrowserDlg* parent, const PcrElement& el, const PopupBrowserConfig& cfg)
{
	if (!parent_ || !el.valid()) { return FALSE; }

	cfg_ = cfg;
	this_.parent = parent;
	this_.extract = el.cls;
	this_.margin = true;

	if (this_.parent)
	{
		std::wstring& extract = this_.parent->this_.extract;
		this_.readOnly = !extract.empty() && extract == this_.extract;
	}

	bbs::URIConvert cnv;
	bbs::URIConvert::Result res;
	if (cnv(this_.extract, res))
	{
		if (cfg_.anchor.enable && res.anchor.matched)
		{
			if (preparePopup(parent_, el))
			{
				gl_.bbs.extract(res.anchor.value, cfg_.anchor.limit, cfg_.anchor.head);
				return TRUE;
			}
		}
		else if (cfg_.id.enable && res.id.matched)
		{
			if (preparePopup(parent_, el))
			{
				gl_.bbs.extract(res.id.value, cfg_.id.limit, cfg_.id.head);
				return TRUE;
			}
		}
		else if (cfg_.ref.enable && res.ref.matched)
		{
			if (preparePopup(parent_, el))
			{
				gl_.bbs.extract(res.ref.value, cfg_.ref.limit, cfg_.ref.head);
				return TRUE;
			}
		}
		else if (res.http.matched)
		{
		}
	}
	return FALSE;
}

//=============================================================================

void CPopupBrowserDlg::createPopup()
{
	if (!popup_)
	{
		popup_ = new CPopupBrowserDlg(this, target_);
		popup_->counter_ = counter_ + 1;
	}
}

void CPopupBrowserDlg::destroyPopup()
{
	if (popup_)
	{
		if (::IsWindow(popup_->GetSafeHwnd()))
		{
			popup_->DestroyWindow();
		}
		delete popup_;
		popup_ = NULL;
	}
}


BOOL CPopupBrowserDlg::preparePopup(const CRect& area)
{
	gl_.bbs.getThreadURL(this_.url);

	this_.target = area;
	gl_.callback.extract.push(this);
	setShowWindow(true);

	if (!::IsWindow(GetSafeHwnd()))
	{
		Create(IDD_POPUP_BROWSER, GetDesktopWindow());
		SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOZORDER|SWP_NOACTIVATE);
	}
	return TRUE;
}

BOOL CPopupBrowserDlg::preparePopup(CCustomDHtmlDialog* src, const PcrElement& el)
{
	if (!parent_ || !el.valid()) { return FALSE; }

	CRect area;
	if (parent_->GetBoundingClientRect(el, area))
	{
		HWND hwnd = parent_->GetSafeBrowserHwnd();
		::ClientToScreen(hwnd, &area.TopLeft());
		::ClientToScreen(hwnd, &area.BottomRight());
		return preparePopup(area);
	}
	return FALSE;
}

//=============================================================================

//[pcr-web] pump layout after resize so pcr.size() reflects new bounds
static void RefreshLayout(CCustomDHtmlDialog* dlg)
{
	MSG m;
	for (int i = 0; i < 3; ++i) {
		while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&m);
			DispatchMessage(&m);
		}
		Sleep(15);
	}
}

bool CPopupBrowserDlg::calcScrollSize(CSize& size)
{
	// create a minimal 1x1 window to measure scroll size
	// +2 is the border line
	SetWindowPos(NULL, 0, 0, 1 + 2, 1 + 2, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	RefreshLayout(this);

	std::wstring r = EvalString(L"pcr.size()");
	double sw = 0, sh = 0, dpr = 1.0, z = 1.0;
	if (!pcrweb::JsonGetNum(r, L"sw", sw)) { return false; }
	if (!pcrweb::JsonGetNum(r, L"sh", sh)) { return false; }
	pcrweb::JsonGetNum(r, L"dpr", dpr);
	pcrweb::JsonGetNum(r, L"z", z);
	double f = (dpr > 0 ? dpr : 1.0) * (z > 0 ? z : 1.0);
	size.cx = (int)(sw * f) + 2;
	size.cy = (int)(sh * f) + 2;
	return true;
}

bool CPopupBrowserDlg::calcWindowSize(const CSize& scroll, CSize& size)
{
	// apply scroll size to compute scrollbar width
	// +2 is the border line
	SetWindowPos(NULL, 0, 0, scroll.cx + 2, scroll.cy + 2, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	RefreshLayout(this);

	std::wstring r = EvalString(L"pcr.size()");
	double sw = 0, sh = 0, cw = 0, ch = 0, dpr = 1.0, z = 1.0;
	if (!pcrweb::JsonGetNum(r, L"cw", cw)) { return false; }
	if (!pcrweb::JsonGetNum(r, L"ch", ch)) { return false; }
	pcrweb::JsonGetNum(r, L"dpr", dpr);
	pcrweb::JsonGetNum(r, L"z", z);
	double f = (dpr > 0 ? dpr : 1.0) * (z > 0 ? z : 1.0);

	CSize browser;
	if (GetBrowserSize(browser))
	{
		// browser size(width) - visible area(clientW) = scrollbar size
		size.cx = scroll.cx + (std::max)((LONG)(browser.cx - cw * f), (LONG)0) + 2;
		size.cy = scroll.cy + (std::max)((LONG)(browser.cy - ch * f), (LONG)0) + 2;
		return true;
	}
	return false;
}

void CPopupBrowserDlg::setPosition()
{
	if (this_.target.IsRectEmpty()) { return; }
	CRect target(this_.target);

	CSize scroll;
	if (calcScrollSize(scroll))
	{
		// monitor info at cursor
		MONITORINFO mi;
		CPoint pt;
		GetCursorPos(&pt);
		utl::getDesktopRectFromPoint(pt, mi);
		CRect rc(mi.rcWork);

		// fit monitor size
		if (rc.Width() - scroll.cx < 0)
		{
			scroll.cx = rc.Width() - 2;
			if (scroll.cx < 0) { scroll.cx = 0; }
		}
		if (rc.Height() - scroll.cy < 0)
		{
			scroll.cy = rc.Height() - 2;
			if (scroll.cy < 0) { scroll.cy = 0; }
		}

		CSize size;
		if (calcWindowSize(scroll, size))
		{
			// fit scrollbar-added size to monitor
			if (rc.Width() - size.cx < 0)
			{
				size.cx = rc.Width();
			}
			if (rc.Height() - size.cy < 0)
			{
				size.cy = rc.Height();
			}

			this_.horizontal = true;
			int ax = target.left - TARGET_EXPAND;
			int ay = target.top - size.cy;

			if (ax + size.cx > rc.right)
			{
				ax = rc.right - size.cx;
			}
			else if (ax < rc.left)
			{
				ax = rc.left;
			}

			if (ay < rc.top)
			{// overflows top
				if (((rc.bottom - target.bottom) - size.cy) > 0)
				{// fits below
					ay = target.bottom;
				}
				else
				{
					ay = rc.top;
					CRect arc(ax, ay, ax + size.cx, ay + size.cy);
					if (arc.PtInRect(target.TopLeft()) &&
						arc.PtInRect(target.BottomRight()))
					{// popup position hidden
						this_.horizontal = false;
						if (((rc.right - target.right) - size.cx) > 0)
						{// fits right
							ax = target.right;
						}
						else
						{
							ax = rc.right - size.cx;
						}
					}
				}
			}

			SetWindowPos(NULL, ax, ay, size.cx, size.cy, SWP_NOZORDER|SWP_NOACTIVATE);
		}
	}
}

void CPopupBrowserDlg::setShowWindow(bool show)
{
	this_.show = show;
	this_.time = GetTickCount();
}

void CPopupBrowserDlg::showWindow()
{
	showWindow(this_.show);
	this_.show = false;
}

void CPopupBrowserDlg::showWindow(bool show)
{
	if (!::IsWindow(GetSafeHwnd())) { return; }
	KillTimer(TIMER_ID_POPUP);

	if (show)
	{
		if (!IsWindowVisible())
		{
			SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOOWNERZORDER|SWP_NOSENDCHANGING);
			ShowWindow(SW_SHOWNA);
			ChangeFocus();
		}
	}
	else
	{
		if (IsWindowVisible())
		{
			// wndBottom/wndTop cause flicker
			SetWindowPos(&wndNoTopMost, 0, 0, 0, 0, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOOWNERZORDER|SWP_NOSENDCHANGING);
			ShowWindow(SW_HIDE);
			ChangeFocus();
		}
	}
}

void CPopupBrowserDlg::delayShowWindow()
{
	int delay = cfg_.delay;
	DWORD diff = GetTickCount() - this_.time;
	if (delay <= 0 || (int)diff >= delay)
	{
		showWindow();
	}
	else
	{
		showWindow(false);
		SetTimer(TIMER_ID_POPUP, (delay - diff), NULL);
	}
}

void CPopupBrowserDlg::scrollIntoView(int res)
{
	if (target_)
	{
		CString anchor;
		anchor.Format(L"res:%d", res);
		target_->ScrollIntoViewId(anchor.GetString());
	}
}


//=============================================================================

void CPopupBrowserDlg::capture(bool enable)
{
	CPopupBrowserDlg* parent = this;
	while (true)
	{
		if (!parent) { break; }
		parent->this_.capture = enable;
		parent = parent->this_.parent;
	}
}

void CPopupBrowserDlg::cancel(bool enable)
{
	CPopupBrowserDlg* parent = this;
	while (true)
	{
		if (!parent) { break; }

		parent->this_.cancel = enable;
		parent = parent->this_.parent;
	}
}

void CPopupBrowserDlg::beginCapture()
{
	CPoint pt;
	if (GetCursorPos(&pt))
	{
		if (IsTextArea(pt))
		{
			capture(true);
			cancel(true);
		}
	}
}

void CPopupBrowserDlg::endCapture()
{
	capture(false);
	cancel(false);
}

bool CPopupBrowserDlg::checkURL()
{
	std::wstring url;
	if (gl_.bbs.getThreadURL(url) && this_.url == url)
	{
		return true;
	}
	return false;
}

//=============================================================================


void CPopupBrowserDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomDHtmlDialog::DoDataExchange(pDX);
}

BOOL CPopupBrowserDlg::OnInitDialog()
{
	CCustomDHtmlDialog::OnInitDialog();

	return FALSE;
}

BEGIN_MESSAGE_MAP(CPopupBrowserDlg, CCustomDHtmlDialog)
	ON_COMMAND(IDM_UPDATE_EXTRACT_END, &CPopupBrowserDlg::OnCallbackExtract)
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_DESTROY()
	ON_WM_MOUSEWHEEL()
	ON_WM_TIMER()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_COMMAND(IDM_CONTEXT_CLOSE, &CPopupBrowserDlg::OnContextClose)
END_MESSAGE_MAP()


// CPopupBrowserDlg message handlers


void CPopupBrowserDlg::OnCallbackExtract()
{
	if (!this_.complete)
	{
		this_.callback = true;
		return;
	}

	if (this_.cancel || (popup_ && popup_->this_.cancel))
	{
		return;
	}

	std::vector<bbs::ResInfo> dat;
	if (!gl_.bbs.getExtract(dat)) { return; }

	std::wstring noname;
	gl_.bbs.getNoName(noname);
	bbs::BBSSkin skin(gl_.value.detail.skin.popup, noname);

	std::wstring html = skin.header(gl_.html.popupHeader) + L"\r\n";

	std::wstring res;
	for (auto it = dat.begin(); it != dat.end(); ++it)
	{
		res += skin.res(gl_.html.popupRes, *it) + L"\r\n";
	}

	//[pcr-web] full document replace (former writeln + close)
	LoadFromString(html + res);

	// link color update
	utl::refreshColor(this, dat, gl_.value.detail.skin.popup);

	setPosition();
	ScrollToBottom();
	delayShowWindow();
}


void CPopupBrowserDlg::OnDocumentComplete(const std::wstring& url)
{
	CCustomDHtmlDialog::OnDocumentComplete(url);

	if (!this_.header)
	{// load header after resource load
		// browser window hook
		utl::BrowserHook::Subclass(GetSafeBrowserHwnd(), GetSafeHwnd());

		bbs::BBSSkin skin(gl_.value.detail.skin.popup);
		std::wstring html = skin.header(gl_.html.popupHeader);
		LoadFromString(html);
		this_.header = true;
	}
	else
	{// process Dat after new load
		this_.complete = true;
		if (this_.callback)
		{
			this_.callback = false;
			OnCallbackExtract();
		}
	}
}

HRESULT CPopupBrowserDlg::OnBrowserKey(MSG& msg)
{
	if (msg.message == WM_KEYDOWN)
	{
		if (msg.wParam == VK_F5)
		{
			return S_OK;
		}
		else if (GetKeyState(VK_CONTROL) & 0x8000)
		{
			if (msg.wParam != L'A' &&
				msg.wParam != L'Z' &&
				msg.wParam != L'X' &&
				msg.wParam != L'C' &&
				msg.wParam != L'V')
			{
				return S_OK;
			}
		}
	}
	return S_FALSE;
}

HRESULT CPopupBrowserDlg::OnBrowserContext(DWORD menuID, POINT* ppt, const PcrElement& el)
{
	cancel(true);
	HRESULT hr = utl::ContextMenu::show(this, menuID, ppt, el, 2, checkURL(), this_.url);
	cancel(false);
	PostMessage(WM_MOUSELEAVE);
	return hr;
}


void CPopupBrowserDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!popup_ ||
		!::IsWindow(popup_->GetSafeHwnd()) ||
		!::IsWindowVisible(popup_->GetSafeHwnd()))
	{
		if (!this_.capture)
		{
			cancel(false);
		}
	}

	if (this_.readOnly)
	{
		if (parent_)
		{
			parent_->PostMessage(WM_MOUSEMOVE);
		}
		return;
	}

	createPopup();
	if (popup_)
	{
		popup_->onMouseMove();
	}

	CCustomDHtmlDialog::OnMouseMove(nFlags, point);
}


void CPopupBrowserDlg::OnMouseLeave()
{
	if (popup_)
	{
		if (popup_->onMouseLeave())
		{
			return;
		}
	}

	if (this_.cancel ||
		(popup_ && ::IsWindow(popup_->GetSafeHwnd()) && ::IsWindowVisible(popup_->GetSafeHwnd())))
	{
		return;
	}

	showWindow(false);

	destroyPopup();

	if (parent_)
	{
		// we get deleted inside; use PostMessage not SendMessage
		parent_->PostMessage(WM_MOUSELEAVE);
	}

	CCustomDHtmlDialog::OnMouseLeave();
}

void CPopupBrowserDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomDHtmlDialog::OnSize(nType, cx, cy);
}


void CPopupBrowserDlg::OnPaint()
{
	CPaintDC dc(this); // device context for painting
CRect rc;
GetClientRect(rc);

	if (gl_.value.detail.popup.edge)
	{
		dc.DrawEdge(rc, BDR_RAISEDOUTER, BF_RECT);
	}
	else
	{
		CPen pen(PS_SOLID, 1, gl_.value.detail.popup.border);
		CPen* oldPen = dc.SelectObject(&pen);
		CBrush* oldBrush = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
		dc.Rectangle(rc);
		dc.SelectObject(oldPen);
		dc.SelectObject(oldBrush);
	}
}

void CPopupBrowserDlg::OnDestroy()
{
	utl::BrowserHook::Unsubclass(GetSafeBrowserHwnd());

	CCustomDHtmlDialog::OnDestroy();

	destroyPopup();
}


BOOL CPopupBrowserDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	return CCustomDHtmlDialog::OnMouseWheel(nFlags, zDelta, pt);
}

HRESULT CPopupBrowserDlg::OnAnchorMouseClick(const PcrElement& el)
{
	if (!el.valid()) { return S_OK; }

	bbs::URIConvert cnv;
	bbs::URIConvert::Result res;
	if (cnv(el.cls, res))
	{
		if (res.anchor.matched)
		{
			if (checkURL())
			{
				scrollIntoView(res.anchor.value.first);
			}
		}
		else if (res.id.matched)
		{
		}
		else if (res.ref.matched)
		{
			if (checkURL())
			{
				scrollIntoView(res.ref.value);
			}
		}
		else if (res.http.matched)
		{
		}
	}
	else
	{
		if (!el.href.empty())
		{
			utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, el.href);
		}
	}

	return S_OK;
}


HRESULT CPopupBrowserDlg::OnAnchorMouseOver(const PcrElement& el)
{
	if (this_.readOnly)
	{
		return S_OK;
	}

	if (!el.valid()) { return S_OK; }

	if (GetKeyState(VK_LBUTTON) & 0x8000 || GetKeyState(VK_RBUTTON) & 0x8000)
	{
		return S_OK;
	}

	if (popup_)
	{
		if (checkURL())
		{
			popup_->popup(this, el, cfg_);
		}
	}
	return S_OK;
}

void CPopupBrowserDlg::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case TIMER_ID_POPUP:
		{
			showWindow();
			KillTimer(TIMER_ID_POPUP);
			break;
		}
	}
	CCustomDHtmlDialog::OnTimer(nIDEvent);
}


BOOL CPopupBrowserDlg::OnEraseBkgnd(CDC* pDC)
{
	HWND hwnd = GetSafeBrowserHwnd();
	if (hwnd && ::IsWindow(hwnd))
	{
		::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
	}
	return TRUE;
}


void CPopupBrowserDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	ChangeFocus();
}


void CPopupBrowserDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	ChangeFocus();
}


void CPopupBrowserDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	ChangeFocus();
}

void CPopupBrowserDlg::OnContextClose()
{
	hide();
}
