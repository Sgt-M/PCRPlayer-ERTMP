#pragma once

//[pcr-web] WebView2 based popup browser dialog

#include "CustomDHtmlDialog.h"
#include "BrowserHook.h"
#include "BrowserMenu.h"


#include <strsafe.h>

struct PopupBrowserConfig {
	struct Popup {
		Popup()
			: enable(true)
			, limit(1000)
			, head(true)
		{}
		bool enable; // popup display
		int limit; // displayed post count
		bool head; // remove remainder from head, prefer newest posts
	};

	struct PopupAnchor : public Popup {
		PopupAnchor() { head = false; }
	};

	struct PopupID : public Popup {
		PopupID() { limit = 10; }
	};

	PopupBrowserConfig()
		: delay(50)
		, edge(true)
		, border(RGB(0, 0, 0))
	{}

	int delay; // time before popup
	bool edge; // 3D edge
	COLORREF border; // border color
	Popup ref; // post number popup
	PopupAnchor anchor; // anchor popup
	PopupID id; // ID popup
};

// CPopupBrowserDlg dialog

class CPopupBrowserDlg : public CCustomDHtmlDialog
{
	DECLARE_DYNCREATE(CPopupBrowserDlg)

public:
	CPopupBrowserDlg(CCustomDHtmlDialog* pParent = NULL, CCustomDHtmlDialog* pTarget = NULL);
	virtual ~CPopupBrowserDlg();

// dialog data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_POPUP_BROWSER, IDH = IDR_HTML_POPUP_BROWSER };
#endif

public:
	void setTarget(CCustomDHtmlDialog* dhtml) { target_ = dhtml; }

	void onMouseMove();
	bool onMouseLeave();

	void hide();

	BOOL popup(const CRect& area, const PopupBrowserConfig& cfg, bool margin = false);
	BOOL popup(const PcrElement& el, const PopupBrowserConfig& cfg);

protected:
	BOOL popup(CPopupBrowserDlg* parent, const PcrElement& el, const PopupBrowserConfig& cfg);

	void createPopup();
	void destroyPopup();
	BOOL preparePopup(const CRect& area);
	BOOL preparePopup(CCustomDHtmlDialog* src, const PcrElement& el);

protected:
	PopupBrowserConfig cfg_;
	CCustomDHtmlDialog* target_;
	CCustomDHtmlDialog* parent_;
	CPopupBrowserDlg* popup_;
	int counter_;

	struct This {
		This()
			: header(false)
			, complete(false)
			, callback(false)
			, parent(NULL)
			, margin(true)
			, horizontal(true)
			, show(false)
			, readOnly(false)
			, hover(false)
			, capture(false)
			, cancel(false)
		{}

		bool header;//load new header
		bool complete;//OnDocumentComplete done
		bool callback;//extractCallback

		CPopupBrowserDlg* parent;
		std::wstring url;//thread URL
		std::wstring extract;//extract target
		CRect target;//popup position
		bool margin;//popup margin
		bool horizontal;//popup horizontal expand
		bool show;//popup show
		DWORD time;//popup time
		bool readOnly;//display only
		bool hover;//TrackMouseEvent
		bool capture;//mouse capture
		bool cancel;//cancel popup erase
	} this_;

	bool calcScrollSize(CSize& size);
	bool calcWindowSize(const CSize& scroll, CSize& size);
	void setPosition();
	void setShowWindow(bool show);
	void showWindow();
	void showWindow(bool show);
	void delayShowWindow();
	void scrollIntoView(int res);
	void capture(bool enable);
	void cancel(bool enable);
	void beginCapture();
	void endCapture();
	bool checkURL();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnCallbackExtract();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg void OnDestroy();
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextClose();

	//[pcr-web] hooks
	virtual void OnDocumentComplete(const std::wstring& url);
	virtual HRESULT OnBrowserKey(MSG& msg);
	virtual HRESULT OnBrowserContext(DWORD menuID, POINT* ppt, const PcrElement& el);
	virtual HRESULT OnAnchorMouseClick(const PcrElement& el);
	virtual HRESULT OnAnchorMouseOver(const PcrElement& el);
};
