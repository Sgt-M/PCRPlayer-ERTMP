#pragma once
//[pcr-web] WebView2 shared layer (replaces MSHTML/IE hosting).
// Keep this file ASCII-only (project sources are cp932).

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#include <WinSock2.h>
#include <Windows.h>
#include <mutex>
#include <functional>
#include <string>
#include <unordered_map>

#include <WebView2.h>
#include <wrl.h>

namespace pcrweb {

// shared environment (all dialogs share one user-data folder / browser process)
// call EnsureEnvironment() once, then poll HasEnvironment() (never block the UI thread)
HRESULT EnsureEnvironment();
bool  HasEnvironment();

//[pcr-web] ALL WebView2 object creation must run on pcrweb's own dedicated
//STA thread: in the full MFC app the loader glue only initializes healthily on
//that thread (main-thread spawns produced a zombie env: S_OK completion with a
//cached env but no browser process ever started). onReady runs on that thread;
//host HWNDs may belong to the UI thread (t24d verified cross-thread hosting).
typedef std::function<void(ICoreWebView2Environment*)> EnvReadyFn;
void  RequestEnvironment(const EnvReadyFn& onReady);
ICoreWebView2Environment* GetEnvironment();
bool  GetEnvFailed();   // true when environment creation permanently failed

// JS string literal escape (without surrounding quotes)
std::wstring JsEscape(const std::wstring& s);

// tiny JSON readers for bridge messages (flat objects)
bool JsonGetStr(const std::wstring& json, const std::wstring& key, std::wstring& out);
bool JsonGetNum(const std::wstring& json, const std::wstring& key, double& out);

// strip a JSON string wrapper returned by ExecuteScript ("\"abc\"" -> abc)
std::wstring UnwrapJson(const std::wstring& raw);

// document-created bridge script (defines window.pcr)
const wchar_t* BridgeScript();

void Log(const wchar_t* fmt, ...);

//[pcr-marsh] run fn on the thread owning the WebView2 objects
void PostWebTask(const std::function<void()>& fn);
bool IsWebThread();

} // namespace pcrweb
