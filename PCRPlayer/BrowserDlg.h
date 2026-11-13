#pragma once

//[pcr-web] WebView2 based BBS browser dialog

#include "CustomDHtmlDialog.h"
#include "PopupBrowserDlg.h"
#include "ExtractDlg.h"
#include "ThreadDlg.h"
#include "BrowserMenu.h"

#include "BBSSkin.h"

#include <unordered_set>
#include <unordered_map>
#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/functional.hpp>

#include <strsafe.h>

// CBrowserDlg dialog

class CBrowserDlg : public CCustomDHtmlDialog
{
	DECLARE_DYNCREATE(CBrowserDlg)

public:
	CBrowserDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~CBrowserDlg();

// dialog data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_BROWSER, IDH = IDR_HTML_BROWSER };
#endif

	void setUpdateCallback(const boost::function<void()>& callback) { update_ = callback; }
	bool getStatus(std::wstring& text);
	size_t getPos() { return this_.pos; }
	void startScroll();
	void stopScroll();
	void hidePopup();

protected:
	CPopupBrowserDlg popup_;
	boost::function<void()> update_;

	struct This {
		This()
			: header(false)
			, complete(false)
			, callback(false)
			, init(true)
			, reset(false)
			, scroll(false)
			, newres(false)
			, first(false)
			, pos(0)
			, time(0)
		{}

		bool header;//load new header
		bool complete;//OnDocumentComplete done
		bool callback;//datCallback

		bool init;//new load
		std::wstring url;//thread URL
		bool reset;//reload same URL
		bool scroll;//scrolling
		bool newres;//new posts fetched this callback
		bool first;//load >>1
		size_t pos;//load position
		DWORD time;
		std::vector<bbs::ResInfo> dat;
	} this_;

	void autoScroll(bool latest = true);
	bool createRes(int size = 0);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnCallbackDat();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);

	//[pcr-web] hooks
	virtual void OnDocumentComplete(const std::wstring& url);
	virtual HRESULT OnBrowserKey(MSG& msg);
	virtual HRESULT OnBrowserContext(DWORD menuID, POINT* pt, const PcrElement& el);
	virtual HRESULT OnAnchorMouseClick(const PcrElement& el);
	virtual HRESULT OnAnchorMouseOver(const PcrElement& el);
	virtual void OnBrowserScroll(bool programmatic);
};
