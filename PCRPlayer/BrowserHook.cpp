#include "stdafx.h"

#include "BrowserHook.h"
#include "CustomDHtmlDialog.h"   // WM_PCR_BROWSERKEY

namespace utl {

BrowserHook::BrowserHookMap BrowserHook::map_;

bool BrowserHook::Subclass(HWND hwnd, HWND target)
{
	if (!hwnd) { return false; }
	Unsubclass(hwnd);

	WNDPROC wndproc = (WNDPROC)::GetWindowLongPtr(hwnd, GWLP_WNDPROC);
	if (!wndproc) { return false; }

	::SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)BrowserHook::WindowProc);
	map_.insert(std::make_pair(hwnd, Target(target, wndproc)));
	return true;
}

bool BrowserHook::Unsubclass(HWND hwnd)
{
	if (!hwnd) { return false; }
	auto it = map_.find(hwnd);
	if (it == map_.end()) { return false; }
	if (!it->second.wndproc) { return false; }

	::SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)it->second.wndproc);
	map_.erase(it);
	return true;
}

LRESULT CALLBACK BrowserHook::WindowProc(HWND hWnd, UINT uMsg, WPARAM wp, LPARAM lp)
{
	auto it = map_.find(hWnd);
	if (it != map_.end())
	{
		switch (uMsg)
		{
		case WM_MOUSEWHEEL:
			// [pcr-web] WebView2 handles the wheel itself.
			// keep the popup-wheel forwarding and Ctrl-zoom suppression only.
			{
				HWND hwnd = WindowFromPoint(CPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
				if (hwnd != hWnd)
				{
					auto it2 = map_.find(hwnd);
					if (it2 != map_.end())
					{
						return CallWindowProc(it2->second.wndproc, hwnd, uMsg, wp, lp);
					}
				}
				if (GetKeyState(VK_CONTROL) & 0x8000)
				{// zoom suppression: strip Ctrl state
					std::array<BYTE, 256> state;
					if (GetKeyboardState(state.data()))
					{
						state[VK_CONTROL] = 0;
						SetKeyboardState(state.data());
					}
				}
			}
			return CallWindowProc(it->second.wndproc, hWnd, uMsg, wp, lp);
		}

		//[pcr-web] former TranslateAccelerator path:
		// give the dialog a chance to consume key messages synchronously.
		switch (uMsg)
		{
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			MSG m = { hWnd, uMsg, wp, lp, GetTickCount() };
			::GetCursorPos(&m.pt);
			LRESULT handled = ::SendMessage(it->second.hwnd, WM_PCR_BROWSERKEY, 0, (LPARAM)&m);
			if (handled == S_OK) { return 0; }
			break;
		}
		}

		switch (uMsg)
		{
		case WM_MOUSEMOVE:
		case WM_MOUSELEAVE:
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
			SendMessage(it->second.hwnd, uMsg, wp, lp);
		}
		return CallWindowProc(it->second.wndproc, hWnd, uMsg, wp, lp);
	}
	return DefWindowProc(hWnd, uMsg, wp, lp);
}


} // namespace utl
