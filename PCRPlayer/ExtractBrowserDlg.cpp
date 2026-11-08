// ExtractBrowserDlg.cpp : implementation file
//[pcr-web] WebView2 based implementation

#include "stdafx.h"
#include "PCRPlayer.h"
#include "ExtractDlg.h"
#include "ExtractBrowserDlg.h"

enum TIMER_ID {
	TIMER_ID_DAT,
	TIMER_ID_SEARCH_COLOR,
};


// CExtractBrowserDlg dialog

IMPLEMENT_DYNCREATE(CExtractBrowserDlg, CCustomDHtmlDialog)

CExtractBrowserDlg::CExtractBrowserDlg(CWnd* pParent /*=NULL*/)
	: CCustomDHtmlDialog(IDD_EXTRACT_BROWSER, /*IDR_HTML_EXTRACTBROWSERDLG,*/ pParent)
	, popup_(this)
{

}

CExtractBrowserDlg::~CExtractBrowserDlg()
{
	gl_.callback.extract.pop(this);
}

bool CExtractBrowserDlg::replaceText(std::wstring& text, const std::wstring& header, const std::wstring& replace)
{
	if (text.find(header) == 0)
	{
		text = replace + std::wstring(text.begin() + header.size(), text.end());
		return true;
	}
	return false;
}

void CExtractBrowserDlg::setPaneText()
{
	if (!this_.status || !::IsWindow(this_.status->GetSafeHwnd())) { return; }

	std::wstring word;
	if (!this_.search.empty())
	{
		word = L"keyword:\"" + this_.search + L"\"";
	}
	else
	{
		std::wstring extract = this_.extract;
		if (replaceText(extract, L"anchor:", L">>"))
		{
			word = L"extract anchor:\"" + extract + L"\"";
		}
		else if (replaceText(extract, L"id:", L"ID:"))
		{
			word = L"extract ID:\"" + extract + L"\"";
		}
		else if(replaceText(extract, L"ref:", L""))
		{
			word = L"ref res number:\"" + extract + L"\"";
		}
	}

	CString str;
	if (this_.count > 0)
	{
		str.Format(L"posts:%d extract:%d", this_.dat.size(), this_.count);
	}
	else
	{
		str.Format(L"posts:%d", this_.dat.size());
	}

	if (word.empty())
	{
		this_.status->SetPaneText(0, str);
	}
	else
	{
		this_.status->SetPaneText(0, (str + L" " + word.c_str()));
	}
}

void CExtractBrowserDlg::setPaneText(bool read)
{
	if (!this_.status || !::IsWindow(this_.status->GetSafeHwnd())) { return; }

	std::wstring desc;
	int per = 0;
	if (read)
	{
		desc = L"loading";
		int pos = (int)std::distance(this_.dat.begin(), this_.it);
		per = (int)this_.dat.size() == 0 ? 100 : ((pos * 100) / (int)this_.dat.size());
	}
	else
	{
		desc = L"coloring";
		per = this_.count == 0 ? 100 : ((this_.pos * 100) / this_.count);
	}

	if (per < 100 && per > 0)
	{
		CString str;
		str.Format(L"%s %d%%", desc.c_str(), per);
		this_.status->SetPaneText(0, str);
	}
	else
	{
		setPaneText();
	}
}

void CExtractBrowserDlg::setPaneTextNotify()
{
	if (!this_.status || !::IsWindow(this_.status->GetSafeHwnd())) { return; }
	this_.status->SetPaneText(0, L"source thread not loaded: anchor extract and popups unavailable");
}

bool CExtractBrowserDlg::pageExtract()
{
	if (!this_.dhtml) { return false; }

	std::set<int> set;
	std::vector<std::wstring> ids;
	//[pcr-web] parent res-id lookup through the JS bridge
	if (this_.dhtml->SearchParentIds(this_.search, L"res:", ids))
	{
		std::wstring header = L"res:";
		for (auto& id : ids)
		{
			if (id.size() > header.size())
			{
				++this_.count;
				int res = wcstol(id.c_str() + header.size(), NULL, 10);
				if (set.find(res) == set.end())
				{
					set.insert(res);
				}
			}
		}
	}

	if (!set.empty())
	{
		std::vector<int> extract(set.begin(), set.end());
		gl_.callback.extract.push(this);
		gl_.bbs.extract(extract);
		return true;
	}
	return false;
}


bool CExtractBrowserDlg::checkURL()
{
	std::wstring url;
	if (gl_.bbs.getThreadURL(url) && this_.url == url)
	{
		return true;
	}
	return false;
}

void CExtractBrowserDlg::scrollIntoView(int res)
{
	CString anchor;
	anchor.Format(L"res:%d", res);
	if (this_.dhtml)
	{
		this_.dhtml->ScrollIntoViewId(anchor.GetString());
	}
}


void CExtractBrowserDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomDHtmlDialog::DoDataExchange(pDX);
}

BOOL CExtractBrowserDlg::OnInitDialog()
{
	CCustomDHtmlDialog::OnInitDialog();

	this_.count = 0;
	this_.pos = 0;
	gl_.bbs.getThreadURL(this_.url);

	std::wstring board, thread;
	if (gl_.bbs.isThread() && gl_.bbs.getBoardTitle(board) && gl_.bbs.getThreadTitle(thread))
	{
		GetParent()->SetWindowText((L"extract [" + board + L"] " + thread).c_str());
	}
	setPaneText();

	DWORD time = GetTickCount();
	if (!this_.search.empty())
	{
		pageExtract();
		gl_.addLog(L"Extract time:%d", GetTickCount() - time);
	}
	else if (!this_.extract.empty())
	{
		gl_.callback.extract.push(this);
		gl_.bbs.extract(this_.extract, true);
	}
	else if (this_.dhtml)
	{
		std::wstring str;
		if (this_.dhtml->GetSelectedText(str) && !str.empty())
		{// selection
			this_.search = str;
			pageExtract();
		}
		gl_.addLog(L"Extract time:%d", GetTickCount() - time);
	}

	ShowWindow(SW_SHOWNORMAL);

	return TRUE;
}

BEGIN_MESSAGE_MAP(CExtractBrowserDlg, CCustomDHtmlDialog)
	ON_COMMAND(IDM_UPDATE_EXTRACT_END, &CExtractBrowserDlg::OnCallbackExtract)
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()


// CExtractBrowserDlg message handlers


void CExtractBrowserDlg::OnCallbackExtract()
{
	if (!this_.complete)
	{
		this_.callback = true;
		return;
	}

	KillTimer(TIMER_ID_DAT);
	KillTimer(TIMER_ID_SEARCH_COLOR);

	if (!gl_.bbs.getExtract(this_.dat)) { return; }
	this_.it = this_.dat.begin();

	if (!IsWebReady()) { return; }

	std::wstring noname;
	gl_.bbs.getNoName(noname);
	bbs::BBSSkin skin(gl_.value.detail.skin.thread, noname);

	std::wstring html = skin.header(gl_.html.header) + L"\r\n";

	LoadFromString(html);

	SetTimer(TIMER_ID_DAT, 0, NULL);
}

void CExtractBrowserDlg::OnDocumentComplete(const std::wstring& url)
{
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
			OnCallbackExtract();
		}
	}
}

HRESULT CExtractBrowserDlg::OnBrowserKey(MSG& msg)
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


HRESULT CExtractBrowserDlg::OnBrowserContext(DWORD menuID, POINT* ppt, const PcrElement& el)
{
	return utl::ContextMenu::show(this, menuID, ppt, el, 1, checkURL(), this_.url);
}

void CExtractBrowserDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	popup_.onMouseMove();

	CCustomDHtmlDialog::OnMouseMove(nFlags, point);
}


void CExtractBrowserDlg::OnMouseLeave()
{
	popup_.onMouseLeave();

	CCustomDHtmlDialog::OnMouseLeave();
}


void CExtractBrowserDlg::OnDestroy()
{
	utl::BrowserHook::Unsubclass(GetSafeBrowserHwnd());

	CCustomDHtmlDialog::OnDestroy();

	if (::IsWindow(popup_))
	{
		popup_.DestroyWindow();
	}
}

HRESULT CExtractBrowserDlg::OnAnchorMouseClick(const PcrElement& el)
{
	if (!el.valid()) { return S_OK; }
	if (!this_.dhtml) { return S_OK; }

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


HRESULT CExtractBrowserDlg::OnAnchorMouseOver(const PcrElement& el)
{
	if (!el.valid()) { return S_OK; }

	if (GetKeyState(VK_LBUTTON) & 0x8000 || GetKeyState(VK_RBUTTON) & 0x8000)
	{
		return S_OK;
	}

	if (checkURL())
	{
		popup_.popup(el, gl_.value.detail.popup);
	}
	else
	{
		setPaneTextNotify();
		this_.notify = true;
	}

	return S_OK;
}

HRESULT CExtractBrowserDlg::OnAnchorMouseOut(const PcrElement& el)
{
	if (!el.valid()) { return S_OK; }

	if (this_.notify)
	{
		this_.notify = false;
		setPaneText();
	}
	return S_OK;
}


void CExtractBrowserDlg::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case TIMER_ID_DAT:
		{
			int partial = gl_.value.detail.board.partial;

			setPaneText(true);
			if (!IsWebReady()) { break; }

			std::wstring noname;
			gl_.bbs.getNoName(noname);
			bbs::BBSSkin skin(gl_.value.detail.skin.thread, noname);
			std::wstring res;
			int count = 0;
			for (;this_.it != this_.dat.end(); ++this_.it)
			{
				res += skin.res(gl_.html.res, *this_.it) + L"\r\n";
				++count;
				if (partial > 0 && count >= partial) { break; }
			}
			AppendDocument(res);

			if (this_.it == this_.dat.end())
			{
				KillTimer(TIMER_ID_DAT);
				utl::refreshColor(this, this_.dat, gl_.value.detail.skin.thread);

				this_.pos = 0;
				this_.total = 0;
				SetTimer(TIMER_ID_SEARCH_COLOR, 0, NULL);
				setPaneText();
				break;
			}
			setPaneText(true);
			ChangeFocus();
			break;
		}
	case TIMER_ID_SEARCH_COLOR:
		{
			setPaneText(false);
			std::wstring color = L"color:#0000ff;background-color:#ffff00";
			//[pcr-web] incremental highlight (former IMarkupPointer based HighLight)
			int marked = HighlightMatches(this_.search, color, this_.pos, gl_.value.detail.board.partial);
			if (this_.total == 0)
			{
				double total = 0;
				std::wstring r;
				if (Eval(L"pcr.find(\"" + pcrweb::JsEscape(this_.search) + L"\")", &r)) {
					total = _wtoi(r.c_str());
				}
				this_.total = (int)total;
			}
			this_.pos += marked;

			if (marked == 0 || this_.pos >= this_.total)
			{
				KillTimer(TIMER_ID_SEARCH_COLOR);
				setPaneText();
				break;
			}
			setPaneText(false);
			break;
		}
	}

	CCustomDHtmlDialog::OnTimer(nIDEvent);
}


void CExtractBrowserDlg::OnSize(UINT nType, int cx, int cy)
{
	if (StoreScroll())
	{
		CCustomDHtmlDialog::OnSize(nType, cx, cy);
		RestoreScroll();
		return;
	}
	CCustomDHtmlDialog::OnSize(nType, cx, cy);
}


BOOL CExtractBrowserDlg::OnEraseBkgnd(CDC* pDC)
{
	HWND hwnd = GetSafeBrowserHwnd();
	if (hwnd && ::IsWindow(hwnd))
	{
		::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
	}
	return TRUE;
}
