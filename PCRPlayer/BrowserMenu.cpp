#include "stdafx.h"
#include "PCRPlayer.h"
#include "ExtractDlg.h"

namespace utl {

//[pcr-web] former CONTEXT_MENU_* ids (see PCRMENU_* in CustomDHtmlDialog.h)

std::wstring ContextMenu::text_;

void ContextMenu::clear()
{
	text_.clear();
}

void ContextMenu::put(const std::wstring& text)
{
	text_ = text;
}

std::wstring ContextMenu::pop()
{
	std::wstring text = text_;
	text_.clear();
	return text;
}


HRESULT ContextMenu::show(CCustomDHtmlDialog* dhtml, DWORD menuID, POINT* pt, const PcrElement& el, int submenu, bool check, const std::wstring& url)
{
	//PCRMENU_DEFAULT
	//PCRMENU_IMAGE
	//PCRMENU_CONTROL
	//PCRMENU_TABLE
	//PCRMENU_TEXTSELECT
	//PCRMENU_ANCHOR
	//PCRMENU_UNKNOWN
	//PCRMENU_VSCROLL
	//PCRMENU_HSCROLL

	switch (menuID)
	{
	case PCRMENU_VSCROLL:
	case PCRMENU_HSCROLL:
		return S_FALSE;
	}

	if (GetKeyState(VK_CONTROL) & 0x8000)
	{
		return S_FALSE;
	}

	if (!dhtml) { return S_OK; }

	clear();

	std::wstring extract;
	std::wstring idStr;
	std::wstring anchorStr;
	std::wstring refStr;
	bool id = false;
	bool anchor = false;
	bool ref = false;
	bool href = false;
	if (menuID == PCRMENU_ANCHOR)
	{
		if (el.valid())
		{
			{
				{
					extract = el.cls;
					bbs::URIConvert cnv;
					bbs::URIConvert::Result res;
					if (cnv(extract, res))
					{
						if (res.anchor.matched)
						{
							int first = res.anchor.value.first;
							int last = res.anchor.value.last;
							if (first == last)
							{
								anchorStr = (boost::wformat(L"%d") % first).str();
							}
							else
							{
								if (first > last)
								{
									std::swap(last, first);
								}
								anchorStr = (boost::wformat(L"%d-%d") % first % last).str();
							}
							anchor = true;
						}
						else if (res.id.matched)
						{
							idStr = res.id.value;
							id = true;
						}
						else if (res.ref.matched)
						{
							refStr = (boost::wformat(L"%d") % res.ref.value).str();
							ref = true;
						}
					}
					else
					{
						href = true;
					}
				}
			}
		}
	}

	bool copy = dhtml->IsEnableCommand(OLECMDID_COPY) ? true : false;
	bool selectall = dhtml->IsEnableCommand(OLECMDID_SELECTALL) ? true : false;

	CMenu menu;
	if (!menu.LoadMenu(IDR_CONTEXTMENU)) { return S_OK; }

	auto enableItem = [&](CMenu *sub, int id, bool check){
		if (!check)
		{
			sub->EnableMenuItem(id, MF_DISABLED|MF_BYCOMMAND);
		}
	};

	auto appendMenu = [](CMenu *dst, CMenu *src){
		int count = src->GetMenuItemCount();
		for (int i = count - 1; i >= 0; --i)
		{
			if (src->GetMenuState(i, MF_BYPOSITION) & MF_SEPARATOR)
			{
				dst->InsertMenu(0, MF_BYPOSITION, MF_SEPARATOR);
			}
			else
			{
				UINT id = src->GetMenuItemID(i);
				if (id != -1)
				{
					CString str;
					if (src->GetMenuString(i, str, MF_BYPOSITION) != 0)
					{
						dst->InsertMenu(0, MF_BYPOSITION, id, str);
					}
				}
			}
		}
	};

	auto deleteMenu = [](CMenu *dst){
		int count = dst->GetMenuItemCount();
		for (int i = 0; i < count; ++i)
		{
			dst->DeleteMenu(0, MF_BYPOSITION);
		}
	};

	CMenu *sub = menu.GetSubMenu(submenu);

	if (anchor || id || ref)
	{
		if (copy)
		{
			sub->InsertMenu(0, MF_BYPOSITION, MF_SEPARATOR);
		}
		else
		{
			deleteMenu(sub);
		}

		if (anchor) { appendMenu(sub, menu.GetSubMenu(3)); }
		else if (id) { appendMenu(sub, menu.GetSubMenu(4)); }
		else if (ref) { appendMenu(sub, menu.GetSubMenu(5)); }
	}

	int idm = 0;
	if (sub && pt)
	{
		enableItem(sub, IDM_CONTEXT_COPY, copy);
		enableItem(sub, IDM_CONTEXT_COPY_ALL, selectall);
		enableItem(sub, IDM_CONTEXT_SELECT_ALL, selectall);
		enableItem(sub, IDM_CONTEXT_LINK_COPY, href);
		enableItem(sub, IDM_CONTEXT_LINK_BBS_BROWSER, href);
		enableItem(sub, IDM_CONTEXT_LINK_WEB_BROWSER, href);
		enableItem(sub, IDM_CONTEXT_EXTRACT, copy);
		enableItem(sub, IDM_CONTEXT_WEB_SEARCH, copy);

		enableItem(sub, IDM_CONTEXT_ANCHOR_EXTRACT, check);
		enableItem(sub, IDM_CONTEXT_REF_EXTRACT, check);

		bool exist = !url.empty();
		enableItem(sub, IDM_CONTEXT_ANCHOR_COPY, exist);
		enableItem(sub, IDM_CONTEXT_REF_COPY, exist);

		enableItem(sub, IDM_CONTEXT_ANCHOR_BBS_BROWSER, exist);
		enableItem(sub, IDM_CONTEXT_REF_BBS_BROWSER, exist);

		enableItem(sub, IDM_CONTEXT_ANCHOR_WEB_BROWSER, exist);
		enableItem(sub, IDM_CONTEXT_REF_WEB_BROWSER, exist);

		bool bbs = gl_.bbs;
		enableItem(sub, IDM_CONTEXT_URL_COPY, exist);
		enableItem(sub, IDM_CONTEXT_TITLE_COPY, bbs);
		enableItem(sub, IDM_CONTEXT_TITLE_URL_COPY, bbs && exist);
		enableItem(sub, IDM_CONTEXT_BBS_BROWSER, exist);
		enableItem(sub, IDM_CONTEXT_WEB_BROWSER, exist);

		idm = sub->TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt->x, pt->y, dhtml);
	}
	menu.DestroyMenu();

	switch (idm)
	{
	case IDM_CONTEXT_COPY:
		{
			dhtml->CopySelectedText();
			break;
		}
	case IDM_CONTEXT_COPY_ALL:
		{
			dhtml->SelectAll();
			dhtml->CopySelectedText();
			dhtml->PostMessage(WM_COMMAND, IDM_CONTEXT_CLOSE);
			break;
		}
	case IDM_CONTEXT_LINK_COPY:
		{
			dhtml->CopyAnchorHref(el);
			break;
		}
	case IDM_CONTEXT_LINK_BBS_BROWSER:
		{
			std::wstring href;
			if (dhtml->GetAnchorHref(el, href))
			{
				put(href);
				AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_BBS_BROWSER);
			}
			break;
		}
	case IDM_CONTEXT_LINK_WEB_BROWSER:
		{
			std::wstring href;
			if (dhtml->GetAnchorHref(el, href))
			{
				utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, href);
			}
			break;
		}
	case IDM_CONTEXT_SELECT_ALL:
		{
			dhtml->SelectAll();
			break;
		}
	case IDM_CONTEXT_SEARCH:
		{
			std::wstring str;
			if (dhtml->GetSelectedText(str))
			{
				put(str);
			}

			AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_SEARCH);
			break;
		}
	case IDM_CONTEXT_EXTRACT:
		{
			std::wstring str;
			if (dhtml->GetSelectedText(str))
			{
				put(str);
			}

			AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_EXTRACT);
			break;
		}
	case IDM_CONTEXT_URL_COPY:
		{
			dhtml->CopyText(url);
			break;
		}
	case IDM_CONTEXT_TITLE_COPY:
		{
			std::wstring title;
			bool exist = false;
			if (gl_.bbs.isThread())
			{
				exist = gl_.bbs.getThreadTitle(title);
			}
			else
			{
				exist = gl_.bbs.getBoardTitle(title);
			}

			if (exist)
			{
				dhtml->CopyText(title);
			}
			break;
		}
	case IDM_CONTEXT_TITLE_URL_COPY:
		{
			std::wstring title;
			bool exist = false;
			if (gl_.bbs.isThread())
			{
				exist = gl_.bbs.getThreadTitle(title);
			}
			else
			{
				exist = gl_.bbs.getBoardTitle(title);
			}

			if (exist)
			{
				title += L"\r\n" + url;
				dhtml->CopyText(title);
			}
			break;
		}
	case IDM_CONTEXT_BBS_BROWSER:
		{
			put(url);
			AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_BBS_BROWSER);
			break;
		}
	case IDM_CONTEXT_WEB_BROWSER:
		{
			utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, url);
			break;
		}
	case IDM_CONTEXT_WEB_SEARCH:
		{
			dhtml->OpenWebSearch();
			break;
		}

	case IDM_CONTEXT_REF_POST:
		{
			if (!refStr.empty())
			{
				put(L">>" + refStr + L"\r\n");
				AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_POST);
			}
			break;
		}
	case IDM_CONTEXT_ANCHOR_POST:
		{
			if (!anchorStr.empty())
			{
				put(L">>" + anchorStr + L"\r\n");
				AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_POST);
			}
			break;
		}
	case IDM_CONTEXT_ANCHOR_EXTRACT:
	case IDM_CONTEXT_ID_EXTRACT:
	case IDM_CONTEXT_REF_EXTRACT:
		{
			CExtractDlg* dlg = CExtractDlg::make();
			if (!dlg) { break; }
			dlg->setExtract(extract);
			dlg->setCustomDHtmlDialog(dhtml);
			dlg->Create(IDD_EXTRACT, AfxGetMainWnd());
			dlg->ShowWindow(SW_SHOWNORMAL);
			break;
		}
	case IDM_CONTEXT_ANCHOR_COPY:
		{
			if (!anchorStr.empty())
			{
				dhtml->CopyText(url + anchorStr);
			}
			break;
		}
	case IDM_CONTEXT_ID_COPY:
		{
			if (!idStr.empty())
			{
				dhtml->CopyText(L"ID:" + idStr);
			}
			break;
		}
	case IDM_CONTEXT_REF_COPY:
		{
			if (!refStr.empty())
			{
				dhtml->CopyText(url + refStr);
			}
			break;
		}
	case IDM_CONTEXT_ANCHOR_BBS_BROWSER:
		{
			if (!anchorStr.empty())
			{
				put(url + anchorStr);
				AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_BBS_BROWSER);
			}
			break;
		}
	case IDM_CONTEXT_REF_BBS_BROWSER:
		{
			if (!refStr.empty())
			{
				put(url + refStr);
				AfxGetMainWnd()->PostMessage(WM_COMMAND, IDM_CONTEXT_BBS_BROWSER);
			}
			break;
		}
	case IDM_CONTEXT_ANCHOR_WEB_BROWSER:
		{
			if (!anchorStr.empty())
			{
				utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, url + anchorStr);
			}
			break;
		}
	case IDM_CONTEXT_REF_WEB_BROWSER:
		{
			if (!refStr.empty())
			{
				utl::selectExecute(gl_.value.detail.browser.def, gl_.value.detail.browser.web, url + refStr);
			}
			break;
		}
	case IDM_CONTEXT_CLOSE:
		{
			dhtml->PostMessage(WM_COMMAND, IDM_CONTEXT_CLOSE);
			break;
		}
	}
	return S_OK;
}

}// namespace utl