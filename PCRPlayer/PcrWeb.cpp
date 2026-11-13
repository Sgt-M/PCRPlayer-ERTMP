//[pcr-web] WebView2 shared layer implementation. ASCII-only.
#include "stdafx.h"
#include "PcrWeb.h"
#include <vector>
#include <stdlib.h>
#include <crtdbg.h>

namespace pcrweb {

static CComPtr<ICoreWebView2Environment> s_env;
static HRESULT s_envResult = E_PENDING;      // E_PENDING: not started / in progress
static bool s_creating = false;
//[pcr-web] recursive: the completion handler may run synchronously on the
//calling thread inside CreateCoreWebView2EnvironmentWithOptions
static std::recursive_mutex s_mtx;

void Log(const wchar_t* fmt, ...)
{
	std::wstring buf;
	va_list ap; va_start(ap, fmt);
	int n = _vscwprintf(fmt, ap);
	if (n > 0) {
		buf.resize(n + 1);
		vswprintf_s(&buf[0], n + 1, fmt, ap);
	}
	va_end(ap);
	gl_.addLog(L"webview2:%s", buf.c_str());
	//[pcr-web] debug trace file
	{
		wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
		std::wstring path = std::wstring(tmp) + L"pcrweb.log";
		FILE* fp = _wfopen(path.c_str(), L"ab");
		if (fp) {
			SYSTEMTIME st; GetLocalTime(&st);
			fprintf(fp, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
			int n = WideCharToMultiByte(CP_UTF8, 0, buf.c_str(), -1, NULL, 0, NULL, NULL);
			if (n > 0) { std::vector<char> u(n); WideCharToMultiByte(CP_UTF8, 0, buf.c_str(), -1, u.data(), n, NULL, NULL); fwrite(u.data(), 1, n - 1, fp); }
			fputc('\n', fp);
			fclose(fp);
		}
	}
}

//[pcr-web] diagnostics: log terminate/purecall/assert to pcrweb.log
static void pcrweb_terminate_hook(void) { Log(L"!! terminate() called"); }
static void pcrweb_purecall_hook(void) { Log(L"!! purecall"); }
static int pcrweb_assert_hook(const wchar_t* expr, const wchar_t* file, unsigned line)
{
	Log(L"!! assert %s @%s:%u", expr ? expr : L"?", file ? file : L"?", line);
	return TRUE; // skip dialog
}
static void InstallDiagHooks()
{
	_set_abort_behavior(0, _WRITE_ABORT_MSG);
	std::set_terminate(pcrweb_terminate_hook);
	_set_purecall_handler(pcrweb_purecall_hook);
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, pcrweb_assert_hook);
	Log(L"diag hooks installed");
}

//[pcr-web] controller creation MUST happen inside the environment completion
//callback: every controller created later from a stored env pointer completed
//with hr=S_OK but never attached its document pipeline (verified standalone:
//t3 inside-cb works, t13/t14/t16 later-context fails). So waiters are queued
//and dispatched from inside the completion handler below.
static std::vector<EnvReadyFn> s_readyQueue;

HRESULT EnsureEnvironment()
{
	Log(L"EnsureEnvironment enter");	//[pcr-exp] UI thread may sit in NA apartment (AfxOleInit path); the
	//runtime glue silently fails to bring the browser up from NA. Force STA.
	{
		HRESULT hsi = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		APTTYPE a2; APTTYPEQUALIFIER q2;
		::CoGetApartmentType(&a2, &q2);
		Log(L"force STA hr=0x%08x apt2=%d", hsi, (int)a2);
	}
	{
		APTTYPE apt = (APTTYPE)-1; APTTYPEQUALIFIER qual = (APTTYPEQUALIFIER)-1;
		// APTTYPE: 0=STA 1=MTA 2=NONE 3=MAINSTA
		HRESULT hoc = CoGetApartmentType(&apt, &qual);
		Log(L"com apt hr=0x%08x apt=%d qual=%d tid=%u", hoc, (int)apt, (int)qual, ::GetCurrentThreadId());
	}
	std::lock_guard<std::recursive_mutex> lock(s_mtx);
	static bool s_hooks = (InstallDiagHooks(), true);
	//[pcr-web] NOTE: spawns even when s_env exists, so every caller receives a
	//fresh completion callback (controller must be created inside one). Skipped
	//only while a spawn is already in flight; its callback will flush the queue.
	if (s_creating) { return E_PENDING; }
	//[pcr-fix] NEVER call CreateCoreWebView2EnvironmentWithOptions twice in one
	//process: the runtime glue (EmbeddedBrowserWebView.dll) CHECK-fails
	//(0x80000003, WER APPCRASH P4=EmbeddedBrowserWebView.dll) on a re-spawn while
	//an environment is alive. If the env already exists, deliver it to every
	//queued waiter inline and return S_OK; callers create their controller from
	//the same env (t18/t19 proved that is fine when the env is healthy).
	if (s_env)
	{
		std::vector<EnvReadyFn> q;
		q.swap(s_readyQueue);
		for (size_t i = 0; i < q.size(); ++i)
		{
			try { q[i](s_env); } catch (...) { Log(L"env ready fn exception"); }
		}
		return S_OK;
	}

	// portable user-data folder next to the exe (absolutize! glue refuses to
	// launch a browser for a relative folder: zombie env with sync S_OK cb)
	std::wstring folder;
	{
		WCHAR ebvar[MAX_PATH * 2] = {};
		DWORD en = ::GetEnvironmentVariableW(L"PCR_WEBVIEW2_FOLDER", ebvar, MAX_PATH * 2);
		if (en > 0 && en < MAX_PATH * 2) folder = ebvar;
		else folder = gl_.current + L"webview2";
		if (!folder.empty() && folder[1] != L':')
		{
			WCHAR full[MAX_PATH * 2] = {};
			if (::GetFullPathNameW(folder.c_str(), MAX_PATH * 2, full, NULL) > 0)
				folder = full;
		}
	}
	SHCreateDirectoryExW(NULL, folder.c_str(), NULL);

	s_creating = true;
	s_envResult = E_PENDING;
	Log(L"env folder=%s", folder.c_str());
	HRESULT hr = E_FAIL;
	//[pcr-web] loader may throw (WRL/std) instead of returning a failure HRESULT
	try {
		hr = CreateCoreWebView2EnvironmentWithOptions(
			nullptr, folder.c_str(), nullptr,
			Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
					std::lock_guard<std::recursive_mutex> lock(s_mtx);
					s_env = env;
					s_envResult = result;
					Log(L"env cb: result=0x%08x glue=%p", result,
						(void*)::GetModuleHandleW(L"EmbeddedBrowserWebView.dll"));
					s_creating = false;
					if (FAILED(result)) { Log(L"env create failed hr=0x%08x", result); }
					// dispatch waiters INSIDE this callback (see note above)
					std::vector<EnvReadyFn> q;
					q.swap(s_readyQueue);
					for (size_t i = 0; i < q.size(); ++i)
					{
						try { q[i](s_env); } catch (...) { Log(L"env ready fn exception"); }
					}
					return S_OK;
				}).Get());
	} catch (const std::exception& e) {
		hr = E_FAIL;
		Log(L"env spawn std::exception: %hs", e.what());
	} catch (...) {
		hr = E_FAIL;
		Log(L"env spawn unknown exception");
	}

	Log(L"env spawn hr=0x%08x glue=%p", hr,
		(void*)::GetModuleHandleW(L"EmbeddedBrowserWebView.dll"));
	if (FAILED(hr)) {
		s_creating = false;
		s_envResult = hr;
		std::vector<EnvReadyFn> q2; q2.swap(s_readyQueue);
		for (size_t i = 0; i < q2.size(); ++i) { try { q2[i](NULL); } catch (...) {} }
		Log(L"env spawn failed hr=0x%08x (runtime missing?)", hr);
		return hr;
	}
	return E_PENDING;
}

// ---------------------------------------------------------------- web thread
//[pcr-web] dedicated STA thread for environment/controller creation.
#include <deque>
#include <thread>
namespace {
std::deque<std::function<void()>> s_tasks;
DWORD s_webTid = 0;
HANDLE s_taskEvt = NULL;
bool s_threadStarted = false;

LRESULT CALLBACK WebHostProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return DefWindowProcW(h, m, w, l);
}

void RunWebThread()
{
	s_webTid = ::GetCurrentThreadId();
	::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	//[pcr-web] the loader glue refuses to launch a browser from a thread that
	//owns no window (t24b works / t24c-t24e hang: the difference was exactly
	//this). Create a message-only anchor window before touching the loader.
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WebHostProc;
	wc.hInstance = ::GetModuleHandleW(NULL);
	wc.lpszClassName = L"PCRWEB_HOST";
	::RegisterClassExW(&wc);
	//[pcr-fix] anchor must be a REAL window: the loader glue intermittently
	//refuses to complete environment creation on a message-only-thread
	//t25b(HWND_MESSAGE)=never fires / t25c(offscreen WS_POPUP)=4/4 ok.
	//[pcr-fix] the anchor must be a REAL top-level window: with a
	//message-only thread the loader glue intermittently never fires the
	//environment callback (t25b HWND_MESSAGE=0/4, t25b2 offscreen
	//WS_POPUP=4/4). Keep it far offscreen and never reparent it.
	HWND anchor = ::CreateWindowExW(WS_EX_TOOLWINDOW, L"PCRWEB_HOST", L"pcrweb", WS_POPUP,
		-2000, -2000, 80, 80, NULL, NULL, ::GetModuleHandleW(NULL), NULL);
	if (!anchor) { DWORD e = ::GetLastError(); Log(L"anchor create FAILED err=%u", e); }
	Log(L"web thread started tid=%u anchor=%p", ::GetCurrentThreadId(), (void*)anchor);
	for (;;)
	{
		std::function<void()> task;
		{
			std::lock_guard<std::recursive_mutex> lock(s_mtx);
			if (!s_tasks.empty()) { task = s_tasks.front(); s_tasks.pop_front(); }
		}
		if (task) { try { task(); } catch (...) { Log(L"web task exception"); } continue; }
		MSG m;
		if (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
		{
			if (m.message == WM_QUIT) break;
			TranslateMessage(&m);
			DispatchMessage(&m);
			continue;
		}
		// idle: block until a task event fires or any message arrives
		HANDLE h = s_taskEvt;
		DWORD r = MsgWaitForMultipleObjects(h ? 1 : 0, &h, FALSE, INFINITE, QS_ALLINPUT);
		if (h) ResetEvent(h);
		(void)r;
	}
	Log(L"web thread exit");
	::CoUninitialize();
}
} // namespace


bool HasEnvironment()
{
	std::lock_guard<std::recursive_mutex> lock(s_mtx);
	return s_env != nullptr;
}

ICoreWebView2Environment* GetEnvironment()
{
	std::lock_guard<std::recursive_mutex> lock(s_mtx);
	return s_env;
}

bool GetEnvFailed()
{
	std::lock_guard<std::recursive_mutex> lock(s_mtx);
	return !s_creating && !s_env && s_envResult != E_PENDING;
}

// ---------------------------------------------------------------- JS escape
std::wstring JsEscape(const std::wstring& s)
{
	std::wstring out;
	out.reserve(s.size() + 8);
	for (wchar_t c : s) {
		switch (c) {
		case L'\\': out += L"\\\\"; break;
		case L'"': out += L"\\\""; break;
		case L'\n': out += L"\\n"; break;
		case L'\r': out += L"\\r"; break;
		case L'\t': out += L"\\t"; break;
		case L'<': out += L"\\u003c"; break; // keep </script> harmless if embedded
		default:
			if (c < 0x20) {
				wchar_t buf[8]; swprintf_s(buf, L"\\u%04x", (unsigned)c); out += buf;
			} else {
				out += c;
			}
		}
	}
	return out;
}

// ------------------------------------------------------------ JSON helpers
static bool DecodeJsonString(const std::wstring& s, size_t start, std::wstring& out)
{
	out.clear();
	for (size_t i = start; i < s.size(); ++i) {
		wchar_t c = s[i];
		if (c == L'"') { return true; }
		if (c == L'\\' && i + 1 < s.size()) {
			++i;
			wchar_t e = s[i];
			switch (e) {
			case L'n': out += L'\n'; break;
			case L'r': out += L'\r'; break;
			case L't': out += L'\t'; break;
			case L'b': out += L'\b'; break;
			case L'f': out += L'\f'; break;
			case L'"': out += L'"'; break;
			case L'\\': out += L'\\'; break;
			case L'/': out += L'/'; break;
			case L'u':
				if (i + 4 < s.size()) {
					std::wstring hex = s.substr(i + 1, 4);
					i += 4;
					unsigned v = wcstoul(hex.c_str(), NULL, 16);
					if (v >= 0xD800 && v <= 0xDBFF && i + 6 < s.size() && s[i+1] == L'\\' && s[i+2] == L'u') {
						std::wstring hex2 = s.substr(i + 3, 4);
						unsigned v2 = wcstoul(hex2.c_str(), NULL, 16);
						if (v2 >= 0xDC00 && v2 <= 0xDFFF) {
							i += 6;
							unsigned cp = 0x10000 + ((v - 0xD800) << 10) + (v2 - 0xDC00);
							out += (wchar_t)((cp >> 10) | 0xD800);
							out += (wchar_t)((cp & 0x3FF) | 0xDC00);
							continue;
						}
					}
					out += (wchar_t)v;
				}
				break;
			default: out += e;
			}
			continue;
		}
		out += c;
	}
	return false; // unterminated
}

static size_t FindKey(const std::wstring& json, const std::wstring& key)
{
	std::wstring pat = L"\"" + key + L"\":";
	size_t p = json.find(pat);
	if (p == std::wstring::npos) { return std::wstring::npos; }
	p += pat.size();
	while (p < json.size() && iswspace(json[p])) { ++p; }
	return p;
}

bool JsonGetStr(const std::wstring& json, const std::wstring& key, std::wstring& out)
{
	size_t p = FindKey(json, key);
	if (p == std::wstring::npos || p >= json.size() || json[p] != L'"') { return false; }
	return DecodeJsonString(json, p + 1, out);
}

bool JsonGetNum(const std::wstring& json, const std::wstring& key, double& out)
{
	size_t p = FindKey(json, key);
	if (p == std::wstring::npos || p >= json.size()) { return false; }
	if (json.compare(p, 4, L"true") == 0) { out = 1; return true; }
	if (json.compare(p, 5, L"false") == 0) { out = 0; return true; }
	if (json.compare(p, 4, L"null") == 0) { out = 0; return true; }
	wchar_t* endp = NULL;
	out = wcstod(json.c_str() + p, &endp);
	return endp && endp != json.c_str() + p;
}

std::wstring UnwrapJson(const std::wstring& raw)
{
	if (raw.size() >= 2 && raw[0] == L'"') {
		std::wstring out;
		if (DecodeJsonString(raw, 1, out)) { return out; }
		return raw;
	}
	return raw;
}

// ------------------------------------------------------------- bridge script
const wchar_t* BridgeScript()
{
	static const wchar_t* script =
LR"JS(
(function () {
  if (window.pcr) { return; }
  var p = {
    _prog: false, _ranges: [], _term: '', _last: null,
    _hold: false, _hdt: 0,

    send: function (o) { try { chrome.webview.postMessage(JSON.stringify(o)); } catch (e) { } },

    //[pcr-web] a smooth scroll fires MANY scroll events; hold the programmatic
    //flag until the animation ends (scrollend, or 1.2s safety timeout) so the
    //mid-animation events are not misread as user scrolls (that stopped the
    //follow-new-res auto-scroll timer).
    hold: function () { p._prog = true; p._hold = true; clearTimeout(p._hdt); p._hdt = setTimeout(function () { p._hold = false; p._prog = false; }, 1200); },

    open: function (html) { try { document.open(); document.write(html); document.close(); return 'ok'; } catch (e) { return 'err'; } },
    append: function (html) { var b = document.body; if (!b) { return 'err'; } b.insertAdjacentHTML('beforeend', html); return 'ok'; },

    scroll: function () {
      //[pcr-fix] viewport metrics, NOT element metrics: the BBS skin may stretch
      //html/body to its content height (custom skins do), which inflates
      //documentElement.clientHeight to == scrollHeight, making "sh - ch" (the
      //max scroll offset) always 0 - every bottom-jump then looked like a jump
      //to the top. IE read the window viewport, so it never saw this.
      var de = document.documentElement, b = document.body;
      var ih = window.innerHeight || (de ? de.clientHeight : 0);
      var top = (typeof window.scrollY === 'number') ? window.scrollY
              : (de ? de.scrollTop : 0) || (b ? b.scrollTop : 0);
      var sh = Math.max(de ? de.scrollHeight : 0, b ? b.scrollHeight : 0, ih);
      return JSON.stringify({ top: top, sh: sh, ch: ih, dpr: window.devicePixelRatio || 1 });
    },
    scrollTo: function (y, smooth) { if (smooth) { p.hold(); } else { p._prog = true; } window.scrollTo({ left: 0, top: y, behavior: smooth ? 'smooth' : 'auto' }); return 'ok'; },
    scrollBy: function (y) { p._prog = true; window.scrollBy(0, y); return 'ok'; },

    scale: function () { // physical/logical factor (dpr * zoom)
      var b = document.body;
      if (b && b.offsetWidth > 0) {
        var r = b.getBoundingClientRect();
        return String(r.width / b.offsetWidth);
      }
      return String(window.devicePixelRatio || 1);
    },
    size: function () { // body scroll size + viewport client size (CSS px)
      var b = document.body, de = document.documentElement;
      var sw = b ? b.scrollWidth : 0, sh = b ? b.scrollHeight : 0;
      var cw = de ? de.clientWidth : 0, ch = de ? de.clientHeight : 0;
      if (cw === 0 || ch === 0) { cw = b ? b.clientWidth : 0; ch = b ? b.clientHeight : 0; }
      return JSON.stringify({ sw: sw, sh: sh, cw: cw, ch: ch, dpr: window.devicePixelRatio || 1, z: parseFloat(p.scale()) });
    },

    removeClassName: function (c) {
      var n = 0; document.querySelectorAll('*').forEach(function (e) {
        var cl = e.className; if (typeof cl !== 'string' || !cl) { return; }
        if (cl === c) { e.className = ''; n++; }
        else if (cl.indexOf(c + '-') === 0) { e.className = cl.slice((c + '-').length); n++; }
      });
      return String(n);
    },

    infoOf: function (el) {
      if (!el) { return 'null'; }
      var r = el.getBoundingClientRect();
      var a = (el.closest ? el.closest('a') : null);
      var href = '';
      if (a) { href = a.getAttribute('href') || ''; }
      else if (el.tagName === 'IMG') { href = el.getAttribute('src') || ''; }
      var t = el.textContent || '';
      return JSON.stringify({
        tag: el.tagName || '', id: el.id || '',
        cls: (typeof el.className === 'string' ? el.className : ''),
        href: href, text: t.slice(0, 2000),
        x: r.left, y: r.top, w: r.width, h: r.height,
        dpr: window.devicePixelRatio || 1
      });
    },
    hit: function (x, y) {
      var el = document.elementFromPoint(x, y);
      if (!el) { return 'null'; }
      var a = (el.closest ? el.closest('a') : null);
      return p.infoOf(a || el);
    },
    anchor: function (id) { return p.infoOf(document.getElementById(id)); },
    scrollIntoView: function (id, smooth) { var el = document.getElementById(id); if (!el) { return 'err'; } if (smooth) { p.hold(); } else { p._prog = true; } el.scrollIntoView({ behavior: smooth ? 'smooth' : 'auto', block: 'start' }); return 'ok'; },

    selectText: function () { var s = getSelection(); return s ? s.toString() : ''; },
    selectAll: function () {
      var r = document.createRange(); r.selectNodeContents(document.body || document.documentElement);
      var s = getSelection(); s.removeAllRanges(); s.addRange(r); return 'ok';
    },
    source: function () { try { return document.documentElement.outerHTML; } catch (e) { return ''; } },
    title: function () { return document.title || ''; },
    dpr: function () { return String(window.devicePixelRatio || 1); },

    refreshCounter: function (id, count, total) {
      var cls = 'id:' + id; var spans = document.querySelectorAll('span'); var cx = count;
      for (var i = spans.length - 1; i >= 0; i--) {
        var s = spans[i];
        if (s.className === cls) { cx--; s.innerHTML = '[' + cx + '/' + total + ']'; if (cx <= 0) { break; } }
      }
      return 'ok';
    },
    refreshColor: function (mapJson, cfgJson) {
      var map = JSON.parse(mapJson), cfg = JSON.parse(cfgJson);
      var links = document.links;
      for (var i = 0; i < links.length; i++) {
        var a = links[i]; var cls = a.className; if (!cls) { continue; }
        var e = map[cls]; var color = '';
        if (e) {
          if (e.id) { color = e.total >= cfg.idT ? cfg.idC3 : (e.total >= 2 ? cfg.idC2 : cfg.idC1); }
          else { color = e.total >= cfg.numT ? cfg.numC3 : (e.total >= 1 ? cfg.numC2 : cfg.numC1); }
        } else if (cls.indexOf('id:') === 0) { color = cfg.idC1; }
        if (color) { a.style.color = color; }
      }
      return 'ok';
    },

    find: function (term) {
      p._ranges = []; p._term = term;
      if (!term) { return '0'; }
      var root = document.body || document.documentElement; if (!root) { return '0'; }
      var w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
      var lc = term.toLowerCase();
      var n;
      while ((n = w.nextNode())) {
        var t = n.nodeValue; if (!t) { continue; }
        var lt = t.toLowerCase(); var idx = lt.indexOf(lc);
        while (idx >= 0) {
          try {
            var r = document.createRange(); r.setStart(n, idx); r.setEnd(n, idx + term.length);
            p._ranges.push(r);
          } catch (e) { break; }
          idx = lt.indexOf(lc, idx + term.length);
        }
      }
      return String(p._ranges.length);
    },
    jump: function (i) {
      if (i < 0 || i >= p._ranges.length) { return 'err'; }
      var r = p._ranges[i];
      var s = getSelection(); s.removeAllRanges(); s.addRange(r);
      var rect = r.getBoundingClientRect();
      p.hold();
      window.scrollTo({ left: 0, top: window.scrollY + rect.top - 30, behavior: 'smooth' });
      return String(i);
    },
    nav: function (term, dir) {
      if (term !== p._term) { p.find(term); }
      var n = p._ranges.length; if (n === 0) { return JSON.stringify({ idx: 0, total: 0 }); }
      var cur = -1;
      var s = getSelection();
      if (s && s.rangeCount > 0) {
        var sr = s.getRangeAt(0);
        if (dir > 0) {
          for (var i = 0; i < n; i++) { try { if (p._ranges[i].compareBoundaryPoints(Range.START_TO_START, sr) >= 0) { cur = i; break; } } catch (e) { } }
          if (cur < 0) { cur = 0; }
        } else {
          for (var j = n - 1; j >= 0; j--) { try { if (p._ranges[j].compareBoundaryPoints(Range.START_TO_START, sr) < 0) { cur = j; break; } } catch (e) { } }
          if (cur < 0) { cur = n - 1; }
        }
      } else { cur = dir > 0 ? 0 : n - 1; }
      p.jump(cur);
      return JSON.stringify({ idx: cur + 1, total: n });
    },
    parentIds: function (term, prefix) {
      if (term !== p._term) { p.find(term); }
      var ids = [];
      p._ranges.forEach(function (r) {
        var el = r.startContainer ? r.startContainer.parentElement : null;
        while (el) { if (el.id && el.id.indexOf(prefix) === 0) { ids.push(el.id); break; } el = el.parentElement; }
      });
      return JSON.stringify(ids);
    },
    mark: function (term, attrStyle, from, count) { // partial cursor (former IMarkupPointer HighLight)
      if (term !== p._term) { p.find(term); }
      var total = p._ranges.length;
      var end = (count < 0) ? total : Math.min(total, from + count);
      var c = 0;
      for (var i = end - 1; i >= from; i--) {
        try {
          var sp = document.createElement('span');
          if (attrStyle) { sp.setAttribute('style', attrStyle); }
          sp.className = 'pcr-hl';
          rs = p._ranges[i]; rs.surroundContents(sp); c++;
        } catch (e) { }
      }
      p.find(term);
      return JSON.stringify({ marked: c, total: p._ranges.length });
    },
    unmark: function () {
      document.querySelectorAll('span.pcr-hl').forEach(function (sp) {
        var par = sp.parentNode;
        if (!par) { return; }
        while (sp.firstChild) { par.insertBefore(sp.firstChild, sp); }
        par.removeChild(sp);
        par.normalize();
      });
      p._ranges = []; p._term = '';
      return 'ok';
    }
  };

  function elInfo(target) {
    var a = (target && target.closest) ? target.closest('a') : null;
    return a ? a : null;
  }

  document.addEventListener('click', function (e) {
    var a = elInfo(e.target);
    if (a) {
      e.preventDefault();
      try { var o = JSON.parse(p.infoOf(a)); o.e = 'click'; p.send(o); } catch (x) { }
      return false;
    }
  }, true);

  document.addEventListener('mouseover', function (e) {
    var a = elInfo(e.target);
    var k = a ? (a.id || a.className || '') : '';
    var lk = p._last ? (p._last.id || p._last.cls || '') : '';
    if (a) {
      if (k !== lk) {
        try { var o = JSON.parse(p.infoOf(a)); o.e = 'over'; p._last = o; p.send(o); } catch (x) { }
      }
    } else if (p._last) {
      var out = { e: 'out', tag: p._last.tag, id: p._last.id, cls: p._last.cls, href: p._last.href, text: p._last.text, x: p._last.x, y: p._last.y, w: p._last.w, h: p._last.h, dpr: p._last.dpr };
      p._last = null; p.send(out);
    }
  });

  window.addEventListener('scroll', function () {
    p.send({ e: 'scroll', prog: p._prog ? 1 : 0 });
    if (!p._hold) { p._prog = false; }
  }, { passive: true });
  window.addEventListener('scrollend', function () {
    if (p._hold) { p._hold = false; clearTimeout(p._hdt); p._prog = false; }
  }, { passive: true });

  window.pcr = p;
})();
)JS";
	return script;
}




bool IsWebThread()
{
	//[pcr-revert] web thread is no longer spawned; caller owns WebView2.
	if (s_webTid == 0) { return true; }
	return ::GetCurrentThreadId() == s_webTid;
}

//[pcr-marsh] public: run fn on the web thread; callable from any thread.
void PostWebTask(const std::function<void()>& fn)
{
	//[pcr-revert] no web thread -> inline on caller (UI owns objects).
	if (s_webTid == 0) { fn(); return; }
	{
		std::lock_guard<std::recursive_mutex> lock(s_mtx);
		s_tasks.push_back(fn);
	}
	if (s_taskEvt) { SetEvent(s_taskEvt); }
}


void RequestEnvironment(const EnvReadyFn& onReady)
{
	//[pcr-revert] env + controller must live on ONE UI thread: WebView2 fails
	//CreateCoreWebView2Controller from any other thread (0x802A000C).
	std::lock_guard<std::recursive_mutex> lock(s_mtx);
	if (s_env) { onReady(s_env); return; }
	s_readyQueue.push_back(onReady);
	EnsureEnvironment();
}

} // namespace pcrweb
