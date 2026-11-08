#pragma once
//[pcr-web] WebView2-based browser dialog.
// Drop-in replacement of the MSHTML/CDHtmlDialog host with the same class name
// and a compatible (modernized) API surface. IHTMLElement* based members were
// replaced by PcrElement (DOM snapshot obtained through the JS bridge).

#include "PcrWeb.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

// context menu kinds (former idocidl CONTEXT_MENU_* values)
enum {
	PCRMENU_DEFAULT    = 0,
	PCRMENU_IMAGE      = 1,
	PCRMENU_CONTROL    = 2,
	PCRMENU_TABLE      = 3,
	PCRMENU_TEXTSELECT = 4,
	PCRMENU_ANCHOR     = 5,
	PCRMENU_UNKNOWN    = 6,
	PCRMENU_VSCROLL    = 9,
	PCRMENU_HSCROLL    = 10
};

// forwarded browser input (see utl::BrowserHook)
#define WM_PCR_BROWSERKEY (WM_APP + 40)   // lParam: const MSG*
#define WM_PCR_DOC        (WM_APP + 41)   // document write finished -> OnDocumentComplete
#define WM_PCR_ENVSTRM    (WM_APP + 42)   // [pcr-marsh] marshaled env stream -> UI thread

#include <docobj.h>   // OLECMDID_*

//[pcr-web] lightweight element snapshot replacing IHTMLElement*
struct PcrElement {
	std::wstring tag;
	std::wstring id;
	std::wstring cls;    // className
	std::wstring href;   // anchor href (raw attribute) or img src
	std::wstring text;
	double x, y, w, h;   // getBoundingClientRect (CSS px, viewport based)
	double dpr;

	PcrElement() : x(0), y(0), w(0), h(0), dpr(1.0) {}
	bool valid() const { return !tag.empty(); }
	operator bool() const { return valid(); }
};

class CCustomDHtmlDialog : public CDialog
{
	DECLARE_DYNCREATE(CCustomDHtmlDialog)

public:
	CCustomDHtmlDialog();
	CCustomDHtmlDialog(UINT nIDTemplate, CWnd* pParent = NULL);
	virtual ~CCustomDHtmlDialog();

	struct Custom {
		Custom()
			: navigate(false)
			, scroll(false)
			, scrollTop(0)
			, scrollHeight(0)
			, clientHeight(0)
			, dpr(1.0)
		{}
		bool navigate;
		bool scroll;
		long scrollTop;
		long scrollHeight;
		long clientHeight;
		double dpr;
	} custom_;

	typedef std::unordered_map<std::wstring, int> BookmarkMap;

	// state
	bool IsWebReady() const { return ready_; }
	HWND GetSafeBrowserHwnd();                       // WebView2 child window
	BOOL GetBrowserSize(CSize& size);

	// synchronous JS evaluation (pumps messages while waiting)
	BOOL Eval(const std::wstring& js, std::wstring* result = NULL, DWORD timeoutMs = 5000);
	std::wstring EvalString(const std::wstring& js, DWORD timeoutMs = 5000);

	// content
	BOOL LoadFromString(const std::wstring& html);   // replaces document (async OnDocumentComplete)
	BOOL AppendDocument(const std::wstring& html);   // appends to body

	// scroll
	BOOL StoreScroll();
	BOOL RestoreScroll();
	BOOL ScrollToTop();
	BOOL ScrollToBottom();
	BOOL ScrollToBottomBy(long y);
	BOOL GetScrollParam(long& scrollTop, long& scrollHeight, long& clientHeight);

	// dom helpers
	BOOL RemoveClassName(const std::wstring& search);
	double GetZoomFactor();                          // physical/logical factor (dpr * zoom)
	BOOL SetZoomFactor(double factor);
	BOOL GetBoundingClientRect(const PcrElement& el, CRect& rc);        // physical px (dpr*zoom)
	BOOL GetBoundingClientZoomRect(const PcrElement& el, CRect& rc);    // same (source compat)
	BOOL GetElementAt(const CPoint& screen, PcrElement& el);            // elementFromPoint
	BOOL GetElementById(const std::wstring& id, PcrElement& el);
	BOOL ScrollIntoViewId(const std::wstring& id);
	BOOL IsTextArea(const CPoint& screen);

	// selection / clipboard
	BOOL GetSelectedText(std::wstring& text);
	BOOL SelectAll();
	BOOL CopySelectedText();
	BOOL CopyText(const std::wstring& text);
	BOOL CopyAnchorHref(const PcrElement& el);
	BOOL GetAnchorHref(const PcrElement& el, std::wstring& href);
	BOOL OpenWebSearch();
	BOOL IsEnableCommand(ULONG id);
	BOOL GetSource(std::wstring& source);
	void ChangeFocus();

	// search (JS Range based; replaces IHTMLTxtRange bookmarks)
	BOOL CreateBookmarkMap(const std::wstring& search, BookmarkMap& map);
	BOOL CreateBookmarks(const std::wstring& search, std::vector<std::wstring>& bookmarks);
	BOOL SearchNext(const std::wstring& search, bool next, size_t& pos, size_t& total);
	int  HighlightMatches(const std::wstring& search, const std::wstring& styleAttr, int start, int count);
	BOOL UnhighlightMatches();
	BOOL SearchParentIds(const std::wstring& search, const std::wstring& prefix, std::vector<std::wstring>& ids);

// hooks (formerly DHTML event map / IDocHostUIHandler / eventsink)
public:
	virtual void OnDocumentComplete(const std::wstring& url) { url; }
	virtual void OnWebReady() {}
	virtual HRESULT OnBrowserKey(MSG& msg) { msg; return S_FALSE; }
	virtual HRESULT OnBrowserContext(DWORD menuID, POINT* pt, const PcrElement& el) { menuID; pt; el; return S_FALSE; }
	virtual HRESULT OnAnchorMouseClick(const PcrElement& el) { el; return S_OK; }
	virtual HRESULT OnAnchorMouseOver(const PcrElement& el) { el; return S_OK; }
	virtual HRESULT OnAnchorMouseOut(const PcrElement& el) { el; return S_OK; }
	virtual void OnBrowserScroll(bool programmatic) { programmatic; }

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnSize(UINT nType, int cx, int cy);
	virtual void OnTimer(UINT_PTR nIDEvent);
	virtual void OnDestroy();

	afx_msg LRESULT OnBrowserKeyMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnDocMsg(WPARAM wParam, LPARAM lParam);
	void SpawnController(ICoreWebView2Environment* env);

	void StartCreate();
	void CreateController();
	void UpdateBounds();
	void DispatchWebMessage(const std::wstring& json);
	void ParseElement(const std::wstring& json, PcrElement& el);
	BOOL EvalBoolOk(const std::wstring& js);

	HRESULT OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller);
	void WireEvents();

	ICoreWebView2Controller* controller_;
	ICoreWebView2* webview_;
	HWND webchild_;
	bool ready_;
	bool bridgeOk_;
	bool creating_;
	int navAllow_;
	double scale_;
	std::wstring pendingUrl_;

	EventRegistrationToken tokNavStart_;
	EventRegistrationToken tokNavDone_;
	EventRegistrationToken tokCtx_;
	EventRegistrationToken tokMsg_;
	EventRegistrationToken tokNewWin_;
	EventRegistrationToken tokFail_;

	DECLARE_MESSAGE_MAP()
};
