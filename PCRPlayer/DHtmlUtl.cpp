// DHtmlUtl.cpp : [pcr-web] WebView2 bridge based helpers.
#include "stdafx.h"
#include "DHtmlUtl.h"

namespace utl {

bool loadHtml(const std::wstring& path, std::wstring& html)
{
	std::ifstream ifs(path, std::ios::in|std::ios::binary);
	if (ifs.is_open())
	{
		ifs.seekg (0, ifs.end);
		size_t length = (size_t)ifs.tellg();
		ifs.seekg (0, ifs.beg);

		if (length > 0)
		{
			std::vector<char> dat;
			dat.resize(length);
			ifs.read(dat.data(), dat.size());
			ifs.close();

			html = utl::toUTF16(std::string(dat.data(), dat.size()));
			return true;
		}
	}
	return false;
}

// count from the end, decrementing and rewriting post counts
// id L"id:" + ID
// count post count
// total total posts
void refreshCounter(
	CCustomDHtmlDialog* dlg, const std::wstring& id, int count, int total)
{
	if (!dlg) { return; }
	std::wstring js =
		L"pcr.refreshCounter(\"" + pcrweb::JsEscape(id) + L"\","
		+ std::to_wstring(count) + L"," + std::to_wstring(total) + L")";
	dlg->Eval(js);
}

void refreshColor(
	CCustomDHtmlDialog* dlg, const LinkMap& map, const bbs::SkinConfig& cfg)
{
	if (!dlg) { return; }

	std::wstring m = L"{";
	bool first = true;
	for (auto& kv : map)
	{
		if (!first) { m += L","; }
		first = false;
		m += L"\"" + pcrweb::JsEscape(kv.first) + L"\":{\"id\":"
		   + std::wstring(kv.second.id ? L"true" : L"false")
		   + L",\"total\":" + std::to_wstring(kv.second.total) + L"}";
	}
	m += L"}";

	std::wstring c = std::wstring(L"{\"idT\":")
		+ std::to_wstring(cfg.id.threshold)
		+ L",\"idC1\":\"" + toStr(cfg.id.color.first)      + L"\""
		+ L",\"idC2\":\"" + toStr(cfg.id.color.second)     + L"\""
		+ L",\"idC3\":\"" + toStr(cfg.id.color.third)      + L"\""
		+ L",\"numT\":"  + std::to_wstring(cfg.number.threshold)
		+ L",\"numC1\":\"" + toStr(cfg.number.color.first)  + L"\""
		+ L",\"numC2\":\"" + toStr(cfg.number.color.second) + L"\""
		+ L",\"numC3\":\"" + toStr(cfg.number.color.third)  + L"\""
		+ L"}";

	std::wstring js =
		L"pcr.refreshColor(\"" + pcrweb::JsEscape(m) + L"\",\"" + pcrweb::JsEscape(c) + L"\")";
	dlg->Eval(js);
}

void refreshColor(
	CCustomDHtmlDialog* dlg, const std::vector<bbs::ResInfo>& dat, const bbs::SkinConfig& cfg)
{
	utl::LinkMap map;
	for (auto it = dat.begin(); it != dat.end(); ++it)
	{
		utl::Link link;
		link.id = false;
		link.total = (int)it->ref.size();
		map.insert(std::make_pair(L"ref:" + it->number, link));

		if (it->total > 0 && map.find(L"id:" + it->id) == map.end())
		{
			utl::Link link;
			link.id = true;
			link.total = it->total;
			map.insert(std::make_pair(L"id:" + it->id, link));
		}
	}

	if (!map.empty())
	{
		utl::refreshColor(dlg, map, cfg);
	}
}

std::wstring toStr(COLORREF color)
{
	return (
		boost::wformat(L"#%02x%02x%02x")
		% GetRValue(color)
		% GetGValue(color)
		% GetBValue(color)).str();
}

}; // namespace utl
