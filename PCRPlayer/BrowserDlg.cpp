// BrowserDlg.cpp : implementation file
//[pcr-web] WebView2 based implementation

#include "stdafx.h"
#include "PCRPlayer.h"
#include "BrowserDlg.h"
#include "DHtmlUtl.h"

enum TIMER_ID {
	TIMER_ID_DAT,
	TIMER_ID_SCROLL,
};


// CBrowserDlg dialog

IMPLEMENT_DYNCREATE(CBrowserDlg, CCustomDHtmlDialog)

CBrowserDlg::CBrowserDlg(CWnd* pParent /*=NULL*/)
	: CCustomDHtmlDialog(IDD_BROWSER, /*IDR_HTML_BROWSER,*/ pParent)
	, popup_(this)
	, update_([](){})
{
	pcrweb::Log(L"CBrowserDlg ctor");
	gl_.callback.dat.push(this);
	pcrweb::Log(L"CBrowserDlg ctor done");
}

CBrowserDlg::~CBrowserDlg()
{
	gl_.callback.dat.pop(this);
}

void CBrowserDlg::startScroll()
{
	SetTimer(TIMER_ID_SCROLL, gl_.value.detail.scroll.interval, NULL);
	this_.scroll = true;
}

void CBrowserDlg::stopScroll()
{
	KillTimer(TIMER_ID_SCROLL);
	this_.scroll = false;
}

void CBrowserDlg::hidePopup()
{
	if (::IsWindow(popup_))
	{
		popup_.hide();
	}
}

void CBrowserDlg::autoScroll(bool latest)
{
	if (this_.init)
	{// new load
		if (this_.reset)
		{// reload
			if (this_.scroll)
			{// restore scroll state
				startScroll();
			}
		}
		else if (gl_.value.detail.scroll.init)
		{// on new load
			startScroll();
		}
	}
	else
	{
		if (this_.scroll || (latest && gl_.value.detail.scroll.latest))
		{// was scrolling / new posts fetched
			startScroll();
		}
	}
}

void CBrowserDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomDHtmlDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CBrowserDlg, CCustomDHtmlDialog)
	ON_WM_SIZE()
	ON_COMMAND(IDM_UPDATE_RES_END, &CBrowserDlg::OnCallbackDat)
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

// CBrowserDlg message handlers


BOOL CBrowserDlg::OnInitDialog()
{
	pcrweb::Log(L"BrowserDlg::OnInitDialog tid=%u hwnd=%p", ::GetCurrentThreadId(), (void*)GetSafeHwnd());
	CCustomDHtmlDialog::OnInitDialog();

	return TRUE;
}


void CBrowserDlg::OnDocumentComplete(const std::wstring& url)
{
	pcrweb::Log(L"BrowserDlg doccomplete header=%d", (int)this_.header);
	CCustomDHtmlDialog::OnDocumentComplete(url);

	if (!this_.header)
	{// load header after resource load
		// browser window hook
		utl::BrowserHook::Subclass(GetSafeBrowserHwnd(), GetSafeHwnd());

		bbs::BBSSkin skin(gl_.value.detail.skin.thread);
		std::wstring html = skin.header(gl_.html.header);
		LoadFromString(html);
		this_.header = true;
	}
	else
	{// process Dat after new load
		this_.complete = true;
		if (this_.callback)
		{
			this_.callback = false;
			OnCallbackDat();
		}
	}
}


HRESULT CBrowserDlg::OnBrowserKey(MSG& msg)
{
	switch (msg.message)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (msg.wParam == VK_F5)
		{// refresh
			if (gl_.value.detail.scroll.refresh)
			{
				startScroll();
			}
			gl_.bbs.refresh();
			return S_OK;
		}
		else if (GetKeyState(VK_CONTROL) & 0x8000)
		{
			switch (msg.wParam)
			{
			case L'F':// search
				AfxGetMainWnd()->SendMessage(WM_COMMAND, IDM_CONTEXT_SEARCH);
				return S_OK;
			case L'A':// select all
				SelectAll();
				return S_OK;
			case L'C':// copy
				CopySelectedText();
				return S_OK;
			}
		}

		{
			CMainDlg *wnd = (CMainDlg *)AfxGetMainWnd();
			if (wnd)
			{
				wnd->PostMessage(msg.message, msg.wParam, msg.lParam);
				return S_OK;
			}
		}
	}
	return S_FALSE;
}

HRESULT CBrowserDlg::OnBrowserContext(DWORD menuID, POINT* ppt, const PcrElement& el)
{
	std::wstring url;
	if (!gl_.bbs.getURL(url))
	{
		gl_.bbs.getSourceURL(url);
	}
	return utl::ContextMenu::show(this, menuID, ppt, el, 0, true, url);
}

void CBrowserDlg::OnSize(UINT nType, int cx, int cy)
{
	if (StoreScroll())
	{
		CCustomDHtmlDialog::OnSize(nType, cx, cy);
		RestoreScroll(FALSE);
		return;
	}

	CCustomDHtmlDialog::OnSize(nType, cx, cy);
}


void CBrowserDlg::OnCallbackDat()
{
	if (!this_.complete)
	{
		this_.callback = true;
		return;
	}

	KillTimer(TIMER_ID_DAT);
	KillTimer(TIMER_ID_SCROLL);

	if (!IsWebReady()) { return; }

	if (!gl_.bbs.isThread())
	{// board URL
		this_.pos = 0;
		bbs::BBSSkin skin(gl_.value.detail.skin.thread);
		std::wstring header = skin.header(gl_.html.header);
		LoadFromString(header);
		return;
	}

	this_.time = GetTickCount();

	std::vector<bbs::ResInfo> dat;
	bool init = true;
	if (!gl_.bbs.getDat(dat, init)) { return; }

	if (init)
	{// new load
		StoreScroll();

		this_.reset = false;
		std::wstring url;
		if (gl_.bbs.getThreadURL(url))
		{
			if (!this_.url.empty() && this_.url == url)
			{
				this_.reset = true;
			}
			else
			{
				this_.url = url;
			}
		}

		this_.pos = 0;
		bbs::BBSSkin skin(gl_.value.detail.skin.thread);
		std::wstring header = skin.header(gl_.html.header);
		LoadFromString(header + L"\r\n");

		int def = gl_.value.detail.board.def;
		if (def == 0 || (int)dat.size() - def <= 0)
		{
			this_.pos = 0;
			this_.first = false;
		}
		else
		{
			this_.pos = dat.size() - def;
			this_.first = true;
		}
	}
	else
	{// load remaining posts
		//[pcr-web] capture the position BEFORE the append. createRes() ends in
		//RestoreScroll(), which decides "follow to bottom" from custom_; with a
		//smooth restore it used to save the mid-animation position afterwards,
		//which permanently pinned RestoreScroll to the "restore top" branch and
		//new posts stopped scrolling. IE was instant, so the order never showed.
		StoreScroll();

		//[pcr-fix] the "new posts fetched" flag has to be captured BEFORE
		//createRes() advances pos; after it, pos==dat.size() and the flag was
		//always false, so autoScroll(latest) never resumed the follow crawl
		//(scroll.latest=true in the config was dead here even in the IE build -
		//IE-era reveals rode the reload cycles instead).
		this_.newres = (this_.pos > 0 && dat.size() > this_.pos);

		createRes();

		StoreScroll();

		// rewrite new posts to normal posts
		RemoveClassName(L"new");
	}

	this_.init = init;
	bool latest = false;
	if (this_.pos > 0 && dat.size() > this_.pos)
	{// update past ID post counts by new post IDs
		latest = true;
		std::unordered_set<std::wstring> set;
		for (auto it = dat.begin() + this_.pos; it != dat.end(); ++it)
		{
			if (it->total > 1 && set.find(it->id) == set.end())
			{
				set.insert(it->id);
				utl::refreshCounter(this, it->id, it->count, it->total);
			}
		}
	}

	this_.dat.swap(dat);

	if (createRes(gl_.value.detail.board.partial))
	{
		SetTimer(TIMER_ID_DAT, 0, NULL);
	}
	else
	{
		autoScroll(this_.newres);
		ChangeFocus();
	}
}

void CBrowserDlg::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case TIMER_ID_DAT:
		{
			if (!createRes(gl_.value.detail.board.partial))
			{
				KillTimer(TIMER_ID_DAT);
				//[pcr-web] keep fetched-new-posts flag across chunked createRes
				autoScroll(this_.newres);
				ChangeFocus();
			}
			break;
		}
	case TIMER_ID_SCROLL:
		{
			if (!ScrollToBottomBy(gl_.value.detail.scroll.delta))
			{
				if (gl_.value.detail.scroll.last)
				{
					stopScroll();
				}
			}
			break;
		}
	}
	CCustomDHtmlDialog::OnTimer(nIDEvent);
}

HRESULT CBrowserDlg::OnAnchorMouseClick(const PcrElement& el)
{
	if (!el.valid()) { return S_OK; }

	bbs::URIConvert cnv;
	bbs::URIConvert::Result res;
	if (cnv(el.cls, res))
	{
		if (res.anchor.matched)
		{
			CString anchor;
			anchor.Format(L"res:%d", res.anchor.value.first);
			ScrollIntoViewId(anchor.GetString());
		}
		else if (res.id.matched)
		{
		}
		else if (res.ref.matched)
		{
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

HRESULT CBrowserDlg::OnAnchorMouseOver(const PcrElement& el)
{
	if (!el.valid()) { return S_OK; }

	if (GetKeyState(VK_LBUTTON) & 0x8000 || GetKeyState(VK_RBUTTON) & 0x8000)
	{
		return S_OK;
	}

	popup_.popup(el, gl_.value.detail.popup);

	return S_OK;
}

void CBrowserDlg::OnBrowserScroll(bool programmatic)
{
	//[pcr-web] former CCustomEventHandler onscroll sink
	if (programmatic)
	{
		if (custom_.scroll)
		{
			custom_.scroll = false;
		}
	}
	else if (this_.scroll && gl_.value.detail.scroll.manual)
	{
		stopScroll();
	}
}

void CBrowserDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	popup_.onMouseMove();

	ClientToScreen(&point);
	(static_cast<CMainDlg*>(AfxGetMainWnd()))->ScreenToClient(&point);
	(static_cast<CMainDlg*>(AfxGetMainWnd()))->OnMouseMove(nFlags, point);
}

void CBrowserDlg::OnMouseLeave()
{
	popup_.onMouseLeave();

	CCustomDHtmlDialog::OnMouseLeave();
}

void CBrowserDlg::OnDestroy()
{
	utl::BrowserHook::Unsubclass(GetSafeBrowserHwnd());

	CCustomDHtmlDialog::OnDestroy();

	if (::IsWindow(popup_))
	{
		popup_.DestroyWindow();
	}
}


BOOL CBrowserDlg::OnEraseBkgnd(CDC* pDC)
{
	HWND hwnd = GetSafeBrowserHwnd();
	if (hwnd && ::IsWindow(hwnd))
	{
		::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
	}
	return TRUE;
}
