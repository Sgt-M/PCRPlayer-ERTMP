#pragma once
//[pcr-web] DHTML helpers now operate on the WebView2 host (CCustomDHtmlDialog).

#include "CustomDHtmlDialog.h"

namespace utl {

class SafeArray {
	SAFEARRAY *array_;

public:
	SafeArray()
		: array_(NULL)
	{}

	SafeArray(const std::wstring& str)
		: array_(NULL)
	{
		assign(str);
	}

	SafeArray& operator=(const SafeArray& x)
	{
		clear();
		SafeArrayCopy(x.array_, &array_);
		return *this;
	}

	SafeArray& operator=(const std::wstring& str)
	{
		assign(str);
		return *this;
	}

	virtual ~SafeArray()
	{
		clear();
	}

	operator SAFEARRAY *()
	{
		return array_;
	}

	bool assign(const std::wstring& str)
	{
		clear();
		array_ = SafeArrayCreateVector(VT_VARIANT, 0, 1);
		if (array_)
		{
			VARIANT *p = NULL;
			if (SafeArrayAccessData(array_, (LPVOID*)&p) == S_OK)
			{
				BSTR bstr = SysAllocString(str.c_str());
				if (bstr)
				{
					p->vt = VT_BSTR;
					p->bstrVal = bstr;

					if (SafeArrayUnaccessData(array_) == S_OK)
					{
						return true;
					}
				}
			}
		}
		clear();
		return false;
	}

	void clear()
	{
		if (array_) { SafeArrayDestroyData(array_); array_ = NULL; }
	}
};


bool loadHtml(const std::wstring& path, std::wstring& html);


struct Link {
	Link()
		: id(false)
		, total(0)
	{}
	bool id;
	int total;
};
typedef std::unordered_map<std::wstring, Link> LinkMap;

void refreshCounter(
	CCustomDHtmlDialog* dlg, const std::wstring& id, int count, int total);
void refreshColor(
	CCustomDHtmlDialog* dlg, const LinkMap& map, const bbs::SkinConfig& cfg);
void refreshColor(
	CCustomDHtmlDialog* dlg, const std::vector<bbs::ResInfo>& dat, const bbs::SkinConfig& cfg);
std::wstring toStr(COLORREF color);


}; // namespace utl
