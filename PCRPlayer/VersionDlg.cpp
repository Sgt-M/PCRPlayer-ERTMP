// VersionDlg.cpp : 実装ファイル
//

#include "stdafx.h"
#include "PCRPlayer.h"
#include "VersionDlg.h"
//#include "afxdialogex.h"


// CVersionDlg ダイアログ

IMPLEMENT_DYNAMIC(CVersionDlg, CDialog)

CVersionDlg::CVersionDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CVersionDlg::IDD, pParent)
{

}

CVersionDlg::~CVersionDlg()
{
}

void CVersionDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LINK_URL, m_xcLinkUrl);
	DDX_Control(pDX, IDC_LINK_URL2, m_xcLinkUrl2);
}


BEGIN_MESSAGE_MAP(CVersionDlg, CDialog)
	ON_NOTIFY(NM_CLICK, IDC_LINK_URL, &CVersionDlg::OnNMClickLinkUrl)
	ON_NOTIFY(NM_CLICK, IDC_LINK_URL2, &CVersionDlg::OnNMClickLinkUrl2)
END_MESSAGE_MAP()


// CVersionDlg メッセージ ハンドラー


BOOL CVersionDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	LITEM item = {};
	item.mask = LIF_ITEMINDEX | LIF_URL;
	item.iLink = 0;
	wcscpy_s(item.szUrl, L"http://pecatv.s25.xrea.com/");
	m_xcLinkUrl.SetItem(&item);

	//[pcr-about] second link: fork repository
	LITEM item2 = {};
	item2.mask = LIF_ITEMINDEX | LIF_URL;
	item2.iLink = 0;
	wcscpy_s(item2.szUrl, L"https://github.com/Sgt-M/PCRPlayer-ERTMP");
	m_xcLinkUrl2.SetItem(&item2);

	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}


BOOL CVersionDlg::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。

	return CDialog::PreTranslateMessage(pMsg);
}


void CVersionDlg::OnNMClickLinkUrl(NMHDR *pNMHDR, LRESULT *pResult)
{
	PNMLINK pNMLink = (PNMLINK)pNMHDR;

	std::wstring url = L"http://pecatv.s25.xrea.com/";
	utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, url);

	if (pResult) { *pResult = 0; }
}

void CVersionDlg::OnNMClickLinkUrl2(NMHDR *pNMHDR, LRESULT *pResult)
{
	//[pcr-about] open the fork repository in the configured browser
	std::wstring url = L"https://github.com/Sgt-M/PCRPlayer-ERTMP";
	utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, url);

	if (pResult) { *pResult = 0; }
}
