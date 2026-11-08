#pragma once

//[pcr-web] WebView2 based extract browser dialog

#include "CustomDHtmlDialog.h"
#include "BrowserHook.h"
#include "BrowserMenu.h"

// CExtractBrowserDlg dialog

class CExtractBrowserDlg : public CCustomDHtmlDialog
{
	DECLARE_DYNCREATE(CExtractBrowserDlg)

public:
	CExtractBrowserDlg(CWnd* pParent = NULL);
	virtual ~CExtractBrowserDlg();
// overrides

// dialog data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_EXTRACT_BROWSER, IDH = IDR_HTML_EXTRACTBROWSERDLG };
#endif

	void setSearch(const std::wstring& search) { this_.search = search; }
	void setExtract(const std::wstring& extract) { this_.extract = extract; }
	void setCustomDHtmlDialog(CCustomDHtmlDialog* dhtml) { this_.dhtml = dhtml; popup_.setTarget(dhtml); }
	void setStatusBar(CStatusBar* status) { this_.status = status; }

protected:
	CPopupBrowserDlg popup_;
	struct This {
		This()
			: header(false)
			, complete(false)
			, callback(false)
			, dhtml(NULL)
			, status(NULL)
			, count(0)
			, pos(0)
			, notify(false)
		{}

		bool header;//load new header
		bool complete;//OnDocumentComplete done
		bool callback;//extractCallback

		std::wstring search;//search string
		std::wstring extract;//extract URI
		CCustomDHtmlDialog* dhtml;//extract source
		CStatusBar* status;//parent status bar
		std::wstring url;//thread URL
		int count;//extract count
		std::vector<bbs::ResInfo> dat;//extract body
		std::vector<bbs::ResInfo>::iterator it;//extract body load position
		int pos;//coloring position
		int total;//coloring total
		bool notify;//status notify shown
	} this_;

	bool replaceText(std::wstring& text, const std::wstring& header, const std::wstring& replace);
	void setPaneText();
	void setPaneText(bool read);
	void setPaneTextNotify();
	bool pageExtract();
	bool checkURL();
	void scrollIntoView(int res);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnCallbackExtract();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);

	//[pcr-web] hooks
	virtual void OnDocumentComplete(const std::wstring& url);
	virtual HRESULT OnBrowserKey(MSG& msg);
	virtual HRESULT OnBrowserContext(DWORD menuID, POINT* ppt, const PcrElement& el);
	virtual HRESULT OnAnchorMouseClick(const PcrElement& el);
	virtual HRESULT OnAnchorMouseOver(const PcrElement& el);
	virtual HRESULT OnAnchorMouseOut(const PcrElement& el);
};
