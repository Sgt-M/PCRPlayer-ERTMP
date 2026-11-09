#include "stdafx.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")
//[pcr-aspect] SetWindowSubclass

namespace dsp {

//******************************************************************************
bool IBaseVideoRenderer::getFramerate(int& rate)
{
	if (render_)
	{
		CComQIPtr<IBasicVideo, &IID_IBasicVideo> video(render_);
		if (video)
		{
			REFTIME rt = 0;
			video->get_AvgTimePerFrame(&rt);
			//DebugText(L"getFramerate:%lld\n", rt);
			return true;
		}
	}
	return false;
}

bool IBaseVideoRenderer::getCurrentFramerate(int& rate)
{
	if (render_)
	{
		if (prop_)
		{
			return SUCCEEDED(prop_->get_AvgFrameRate(&rate));
		}
	}
	return false;
}

bool IBaseVideoRenderer::getFramesDrawn(int& frames)
{
	if (render_)
	{
		if (prop_)
		{
			return SUCCEEDED(prop_->get_FramesDrawn(&frames));
		}
	}
	return false;
}

bool IBaseVideoRenderer::getFramesDropped(int& frames)
{
	if (render_)
	{
		if (prop_)
		{
			return SUCCEEDED(prop_->get_FramesDroppedInRenderer(&frames));
		}
	}
	return false;
}


bool IBaseVideoRenderer::isRunning(DWORD timeout)
{
	if (render_)
	{
		FILTER_STATE state = State_Stopped;
		if (SUCCEEDED(render_->GetState(timeout, &state)))
		{
			if (state == State_Running)
			{
				return true;
			}
		}
	}
	return false;
}



bool VMR7::addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd)
{
	if (graph == NULL || hwnd == NULL) { return false; }

	hwnd_ = hwnd;

	HRESULT hr;

	render_.CoCreateInstance(CLSID_VideoMixingRenderer, 0, CLSCTX_INPROC_SERVER);
	if (!render_) { return false; }

	// ウィンドレスモード設定
	CComQIPtr<IVMRFilterConfig> fc(render_);
	if (!fc) { return false; }

	hr = fc->SetRenderingMode(VMRMode_Windowless);
	if (FAILED(hr)) { return false; }

	render_->QueryInterface<IVMRWindowlessControl>(&control_);
	if (!control_) { return false; }

	hr = control_->SetVideoClippingWindow(hwnd);
	if (FAILED(hr)) { return false; }

	hr = control_->SetAspectRatioMode(VMR_ARMODE_LETTER_BOX);
	if (FAILED(hr)) { return false; }

	render_->QueryInterface(IID_IQualProp, (void**)&prop_);
	if (!prop_) { return false; }

	// フィルタをグラフへ追加
	hr = graph->AddFilter(render_, L"Video Mixing Renderer");
	if (FAILED(hr)) { return false; }

	return true;
}

bool VMR7::repaint(HDC hdc)
{
	if (!control_) { return false; }
	return SUCCEEDED(control_->RepaintVideo(hwnd_, hdc));
}

bool VMR7::setVideoPosition(RECT& dst)
{
	if (!control_) { return false; }
	return SUCCEEDED(control_->SetVideoPosition(NULL, &dst));
}

bool VMR7::displayModeChanged()
{
    if (!control_) { return false; }
	return SUCCEEDED(control_->DisplayModeChanged());
}

bool VMR7::getNativeVideoSize(SIZE& size)
{
    if (!control_) { return false; }
	long ax = 0, ay = 0;
	return SUCCEEDED(control_->GetNativeVideoSize(&size.cx, &size.cy, &ax, &ay));
}


//******************************************************************************


bool EVR::addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd)
{
	if (graph == NULL || hwnd == NULL) { return false; }

	hwnd_ = hwnd;

	render_.CoCreateInstance(CLSID_EnhancedVideoRenderer, 0, CLSCTX_INPROC_SERVER);
	if (!render_) { return false; }

	getService(render_, MR_VIDEO_RENDER_SERVICE, control_);
	if (!control_) { return false; }

	//control_->SetAspectRatioMode(MFVideoARMode_None);

	control_->SetVideoWindow(hwnd);

	render_->QueryInterface(IID_IQualProp, (void**)&prop_);
	if (!prop_) { return false; }

	HRESULT hr = hr = graph->AddFilter(render_, L"Enhanced Video Renderer");
	if (FAILED(hr)) { return false; }

	return true;
}

bool EVR::repaint(HDC /*hdc*/)
{
	if (!control_) { return false; }

	return SUCCEEDED(control_->RepaintVideo());
}

bool EVR::setVideoPosition(RECT& dst)
{
	if (!control_) { return false; }
	MFVideoNormalizedRect nrcSrc;
	nrcSrc.left = 0.0;
	nrcSrc.right = 1.0;
	nrcSrc.top = 0.0;
	nrcSrc.bottom = 1.0;
	return SUCCEEDED(control_->SetVideoPosition(&nrcSrc, &dst));
}

bool EVR::displayModeChanged()
{
	return true;
}


bool EVR::getNativeVideoSize(SIZE& size)
{
    if (!control_) { return false; }
	SIZE asize;
	return SUCCEEDED(control_->GetNativeVideoSize(&size, &asize));
}


template<typename T>
bool EVR::getService(const CComPtr<IBaseFilter>& evr, REFGUID guidService, CComPtr<T>& dst)
{
	if (!evr) { return false; }

	CComPtr<IMFGetService> service;
	evr->QueryInterface<IMFGetService>(&service);

	if (!service) { return false; }


	service->GetService(
		guidService,
		__uuidof(T),
		(void**)&dst);

	return true;
}

//******************************************************************************


bool EVRCustom::addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd)
{
	if (graph == NULL || hwnd == NULL) { return false; }

	hwnd_ = hwnd;

	render_.CoCreateInstance(CLSID_EnhancedVideoRenderer, 0, CLSCTX_INPROC_SERVER);
	if (!render_) { return false; }

	CComPtr<IMFVideoPresenter> vp;
	EVRCustomPresenter::CreateInstance(&vp);
	if (!vp) { return false; }

	//------------------------------------------------------

	CComQIPtr<IMFVideoRenderer, &IID_IMFVideoRenderer> vr(render_);
	if (!vr) { return false; }

	HRESULT hr = vr->InitializeRenderer(NULL, vp);
	if (FAILED(hr)) { return false; }

	//------------------------------------------------------

	vp->QueryInterface(IID_ICustomRenderProp, (void**)&custom_);

	if (!custom_) { return false; }

	custom_->SetDXVA2(dxva2_ ? TRUE : FALSE);

	//------------------------------------------------------

	getService(render_, MR_VIDEO_RENDER_SERVICE, control_);
	if (!control_) { return false; }

	hr = control_->SetAspectRatioMode(MFVideoARMode_None);
	if (FAILED(hr)) { return false; }

	hr = control_->SetVideoWindow(hwnd);
	if (FAILED(hr)) { return false; }

	//------------------------------------------------------

	getService(render_, MR_VIDEO_MIXER_SERVICE, proc_);
	if (!proc_) { return false; }

	//------------------------------------------------------

	vp->QueryInterface(IID_IQualProp, (void**)&prop_);
	if (!prop_) { return false; }

	//------------------------------------------------------

	hr = graph->AddFilter(render_, L"Enhanced Video Renderer Custom");
	if (FAILED(hr)) { return false; }

	//custom_->SetResizer(d3dutl::RESIZER_SHADER_2PASS_LANCZOS3);
	return true;
}




bool EVRCustom::repaint(HDC hdc)
{
	if (!control_) { return false; }

	return SUCCEEDED(control_->RepaintVideo());
}

bool EVRCustom::setVideoPosition(RECT& dst)
{
	if (!control_) { return false; }

	//if (dst.bottom <= dst.top)
	//{
	//	dst.bottom = dst.top + 1;
	//}

	//if (dst.right <= dst.left)
	//{
	//	dst.right = dst.left + 1;
	//}

	MFVideoNormalizedRect nrcSrc = {0.0, 0.0, 1.0, 1.0};
	return SUCCEEDED(control_->SetVideoPosition(&nrcSrc, &dst));
}

bool EVRCustom::displayModeChanged()
{
	return true;
}

bool EVRCustom::getNativeVideoSize(SIZE& size)
{
    if (!control_) { return false; }
	SIZE asize = {};
	bool res =  SUCCEEDED(control_->GetNativeVideoSize(&size, &asize));
	return res;
}

bool EVRCustom::saveCurrentImage(const std::wstring& path, int format, int jpegQuality, UINT width, UINT height, bool fit)
{
	if (control_)
	{
		BITMAPINFO info = {0};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

		LPBYTE dib = NULL;
		control_->GetCurrentImage(&info.bmiHeader, &dib, NULL, NULL);

		if (dib)
		{
			utl::Snapshot snap(&info, dib);
			snap.save(path, format, jpegQuality, width, height, fit);

			CoTaskMemFree(dib);
			return true;
			//HDC dc = GetDC(hwnd_);

			//HBITMAP hbm = CreateDIBitmap(dc,
			//	&info.bmiHeader,
			//	CBM_INIT,
			//	dib,
			//	&info,
			//	DIB_RGB_COLORS);

			//CImage img;
			//img.Attach(hbm);
			//img.Save(path.c_str());
			//img.Detach();

			//DeleteObject(hbm);
			//ReleaseDC(hwnd_, dc);
			//CoTaskMemFree(dib);
			//return true;
		}
	}
	return false;
}

bool EVRCustom::getSampleTime(LONGLONG& time)
{
	if (custom_)
	{
		return SUCCEEDED(custom_->GetSampleTime(&time));
	}
	return false;
}

bool EVRCustom::getProcAmpRange(ProcAmpRangeGroup& group)
{
	if (getProcAmpRange(DXVA2_ProcAmp_Brightness, group.brightness) &&
		getProcAmpRange(DXVA2_ProcAmp_Contrast, group.contrast) &&
		getProcAmpRange(DXVA2_ProcAmp_Hue, group.hue) &&
		getProcAmpRange(DXVA2_ProcAmp_Saturation, group.saturation))
	{
		return true;
	}
	return false;
}

bool EVRCustom::setProcAmp(const ProcAmpValue& value)
{
	if (!value.init) { return false; }
	if (proc_)
	{
		DXVA2_ProcAmpValues v = {};
		v.Brightness =	DXVA2FloatToFixed((float)value.brightness / 100.0f);
		v.Contrast =	DXVA2FloatToFixed((float)value.contrast / 100.0f);
		v.Hue =			DXVA2FloatToFixed((float)value.hue / 100.0f);
		v.Saturation =	DXVA2FloatToFixed((float)value.saturation / 100.0f);

		return SUCCEEDED(proc_->SetProcAmpValues(
			DXVA2_ProcAmp_Brightness|
			DXVA2_ProcAmp_Contrast|
			DXVA2_ProcAmp_Hue|
			DXVA2_ProcAmp_Saturation,
			&v));
	}
	return false;
}

bool EVRCustom::getProcAmp(ProcAmpValue& value)
{
	if (proc_)
	{
		DXVA2_ProcAmpValues v = {};

		if (SUCCEEDED(proc_->GetProcAmpValues(
			DXVA2_ProcAmp_Brightness|
			DXVA2_ProcAmp_Contrast|
			DXVA2_ProcAmp_Hue|
			DXVA2_ProcAmp_Saturation,
			&v)))
		{
			value.brightness =	(int)ceil(DXVA2FixedToFloat(v.Brightness) * 100.0f);
			value.contrast =	(int)ceil(DXVA2FixedToFloat(v.Contrast) * 100.0f);
			value.hue =			(int)ceil(DXVA2FixedToFloat(v.Hue) * 100.0f);
			value.saturation =	(int)ceil(DXVA2FixedToFloat(v.Saturation) * 100.0f);
			value.init = true;
			return true;
		}
	}
	return false;
}

bool EVRCustom::setNominalRange(int value)
{
	if (custom_)
	{
		int v = MFNominalRange_Unknown;
		switch(value)
		{
		case NOMINAL_RANGE_0_255: v = MFNominalRange_0_255; break;
		case NOMINAL_RANGE_16_235: v = MFNominalRange_16_235; break;
		case NOMINAL_RANGE_48_208: v = MFNominalRange_48_208; break;
		case NOMINAL_RANGE_64_127: v = MFNominalRange_64_127; break;

		default: v = MFNominalRange_Unknown; break;
		}

		return SUCCEEDED(custom_->SetNominalRange(v));
	}
	return false;
}

bool EVRCustom::setResizer(int type)
{
	if (custom_)
	{
		return SUCCEEDED(custom_->SetResizer(type));
	}
	return false;
}


//-----------------------------------------------------------------------------
bool EVRCustom::getProcAmpRange(DWORD prop, ProcAmpRange& range)
{
	if (proc_)
	{
		DXVA2_ValueRange vr = {};
		if (SUCCEEDED(proc_->GetProcAmpRange(prop, &vr)))
		{
			range.min =		(int)ceil(DXVA2FixedToFloat(vr.MinValue) * 100.0f);
			range.max =		(int)ceil(DXVA2FixedToFloat(vr.MaxValue) * 100.0f);
			range.def =		(int)ceil(DXVA2FixedToFloat(vr.DefaultValue) * 100.0f);
			range.step =	(int)ceil(DXVA2FixedToFloat(vr.StepSize) * 100.0f);
			return true;
		}
	}
	return false;
}


template<typename T>
bool EVRCustom::getService(const CComPtr<IBaseFilter>& evr, REFGUID guidService, CComPtr<T>& dst)
{
	if (!evr) { return false; }

	CComPtr<IMFGetService> service;
	evr->QueryInterface<IMFGetService>(&service);

	if (!service) { return false; }


	service->GetService(
		guidService,
		__uuidof(T),
		(void**)&dst);

	return true;
}



//******************************************************************************
//[pcr-video] MPC Video Renderer: standard DShow renderer embedded as a child
//window through IVideoWindow (same contract MPC-HC/BE uses). No custom
//presenter; MPC VR does its own D3D11 rendering / aspect handling.

const CLSID CLSID_PcrMpcVideoRenderer =
	{0x71f080aa, 0x8661, 0x4093, {0xb1, 0x5e, 0x4f, 0x69, 0x03, 0xe7, 0x7d, 0x0a}};

bool MpcVideoRenderer::addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd)
{
	if (graph == NULL || hwnd == NULL) { return false; }

	hwnd_ = hwnd;

	HRESULT hr = render_.CoCreateInstance(CLSID_PcrMpcVideoRenderer, NULL, CLSCTX_INPROC_SERVER);
	if (FAILED(hr) || !render_) { return false; }

	graph->AddFilter(render_, L"MPC Video Renderer");

	//[pcr-video] MPC-HC/BE host settings: lessRedraws stops the synchronous
	//forced Redraw() behind the renderer lock on every IVideoWindow call.
	CComQIPtr<IExFilterConfigPcr> cfg = render_;
	if (cfg) {
		cfg->Flt_SetBool("lessRedraws", true);
		cfg->Flt_SetBool("d3dFullscreenControl", true);
	}

	//[pcr-video] IMPORTANT: do NOT call put_Owner/CreateWindow from this thread.
	//addToGraph runs on the player's graph-build thread; MPC VR's Init() would
	//create its child window on that thread under our UI-thread parent, and the
	//resulting cross-thread SendMessage traffic deadlocks the UI thread while
	//it waits for the build to finish (observed as "Not Responding" on stream
	//start). The embed is deferred to setVideoPosition(), which MFC calls on
	//the window's (UI) thread.
	embedHwnd_ = hwnd;
	embedded_ = false;
	return true;
}

bool MpcVideoRenderer::repaint(HDC hdc)
{
	//[pcr-video] child-window renderer paints itself; nothing to do
	return false;
}

//[pcr-aspect] MPC VR stretches exactly the destination rect it is given via
//IBasicVideo (and renders nothing without one), so contain-fit math with the
//video's native size has to happen player-side: letterbox/pillarbox centered.
void MpcVideoRenderer::applyDest(int w, int h)
{
	if (!bvideo_ || w <= 0 || h <= 0) { return; }

	long vw = 0, vh = 0;
	if (FAILED(bvideo_->GetVideoSize(&vw, &vh))) { vw = vh = 0; }

	if (vw <= 0 || vh <= 0)
	{
		// size not negotiated yet: place full rect for now, retry via timer
		bvideo_->SetDestinationPosition(0, 0, w, h);
		::SetRectEmpty(&lastfit_);
		if (!fitPending_ && embedHwnd_) {
			fitPending_ = true;
			::SetTimer(embedHwnd_, 0x5EC1, 500, NULL);
		}
		return;
	}

	int aw, ah;
	if ((double)w * (double)vh > (double)h * (double)vw) {
		// window wider than video -> pillarbox (bars left/right)
		ah = h;
		aw = (int)((double)h * (double)vw / (double)vh + 0.5);
		if (aw > w) { aw = w; }
	} else {
		// window taller -> letterbox (bars top/bottom)
		aw = w;
		ah = (int)((double)w * (double)vh / (double)vw + 0.5);
		if (ah > h) { ah = h; }
	}
	const int px = (w - aw) / 2, py = (h - ah) / 2;

	RECT fit;
	::SetRect(&fit, px, py, px + aw, py + ah);
	if (::EqualRect(&fit, &lastfit_)) { fitPending_ = false; return; }
	lastfit_ = fit;
	fitPending_ = false;

	//[pcr-aspect] MPC VR's render target = videoRect INTERSECT windowRect, and the
	//IBasicVideo destination is clamped to the swapchain; letterboxing is done by
	//sizing the child window itself to the fit rect and keeping the destination
	//full inside it (same mechanism MPC-HC uses for its video pane).
	//[pcr-aspect] keep MPC VR's internal geometry at origin 0,0 with the fit
	//size (its render target = videoRect INTERSECT windowRect, both in its own
	//space), then physically center the child window in the player client.
	if (win_) {
		win_->SetWindowPosition(0, 0, aw, ah);
	}
	HRESULT dh = bvideo_->SetDestinationPosition(0, 0, aw, ah);
	if (!childHwnd_ || !::IsWindow(childHwnd_)) {
		childHwnd_ = FindMpcChildWindow();
		if (childHwnd_) { ::SetWindowSubclass(childHwnd_, MpcChildPosSubclass, 1, (DWORD_PTR)this); }
	}
	if (childHwnd_ && ::IsWindow(childHwnd_)) {
		//[pcr-aspect] MPC VR re-applies its internal windowRect (origin 0,0) on
		//every window-position change, so the centering offset must be forced
		//from a WM_WINDOWPOSCHANGING hook on the child window.
		childFitX_ = fit.left;
		childFitY_ = fit.top;
		::MoveWindow(childHwnd_, fit.left, fit.top, aw, ah, TRUE);
	}
}

bool MpcVideoRenderer::retryFit()
{
	if (!embedded_ || !bvideo_) { return false; }
	if (!embedHwnd_ || !::IsWindow(embedHwnd_)) { return false; }
	RECT rc; ::GetClientRect(embedHwnd_, &rc);
	applyDest(rc.right - rc.left, rc.bottom - rc.top);
	return fitPending_;   // false -> timer can be killed
}

void MpcVideoRenderer::invalidateTimer()
{
	fitPending_ = false;
}

//[pcr-aspect] retry hook: the embed happens before the decoder has set a size
//on the renderer pin, so keep asking until the first fit lands.
//[pcr-aspect] MPC VR's video window class is "VRWindow"; it appears as a
//descendant of the embed host once the graph is running.
struct MpcChildFinder { HWND found; };
static BOOL CALLBACK MpcChildEnumProc(HWND hwnd, LPARAM lp)
{
	wchar_t cls[64];
	if (::GetClassNameW(hwnd, cls, 64) && 0 == lstrcmpW(cls, L"VRWindow")) {
		((MpcChildFinder*)lp)->found = hwnd;
		return FALSE;
	}
	return TRUE;
}
HWND MpcVideoRenderer::FindMpcChildWindow()
{
	if (!embedHwnd_ || !::IsWindow(embedHwnd_)) { return NULL; }
	MpcChildFinder f = { NULL };
	::EnumChildWindows(embedHwnd_, MpcChildEnumProc, (LPARAM)&f);
	return f.found;
}

//[pcr-aspect] force MPC VR's child window to the letterbox position even when
//the filter tries to snap its windowRect origin back to (0,0).
LRESULT CALLBACK MpcChildPosSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
	MpcVideoRenderer* self = (MpcVideoRenderer*)ref;
	if (msg == WM_WINDOWPOSCHANGING && self)
	{
		WINDOWPOS* p = (WINDOWPOS*)lp;
		if (self->childForced())
		{
			p->x = self->childX();
			p->y = self->childY();
		}
	}
	return ::DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MpcAspectSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
	MpcVideoRenderer* self = (MpcVideoRenderer*)ref;
	if (msg == WM_TIMER && wp == 0x5EC1)
	{
		if (!self || !self->retryFit()) { ::KillTimer(hwnd, 0x5EC1); return 0; }
		return 0;
	}
	if (msg == WM_DESTROY && self) { self->invalidateTimer(); }
	return ::DefSubclassProc(hwnd, msg, wp, lp);
}

bool MpcVideoRenderer::setVideoPosition(RECT& dst)
{
	if (!render_) { return false; }

	//[pcr-video] first call performs the embed; MFC routes this through the
	//video window (UI thread) so MPC VR's child window is created on the same
	//thread as its parent -> no cross-thread deadlock.
	if (!embedded_) {
		if (embedHwnd_ == NULL || !::IsWindow(embedHwnd_)) { return false; }
		RECT rc; ::GetClientRect(embedHwnd_, &rc);
		int w0 = rc.right - rc.left, h0 = rc.bottom - rc.top;
		win_ = render_;
		if (!win_) { return false; }
		win_->SetWindowPosition(0, 0, w0, h0);   // BEFORE put_Owner (see notes)
		win_->put_Owner((OAHWND)embedHwnd_);
		win_->put_MessageDrain((OAHWND)embedHwnd_);
		win_->put_WindowStyle(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS);
		win_->put_Visible(1L); // OA_TRUE
		bvideo_ = render_;
		//[pcr-aspect] MPC VR's child window appears only when the graph runs and
		//is not exposed via IOleWindow -> located lazily in applyDest.
		applyDest(w0, h0);
		::SetWindowSubclass(embedHwnd_, MpcAspectSubclass, 0, (DWORD_PTR)this);
		embedded_ = true;
		::SetRect(&lastdst_, 0, 0, w0, h0);
		return true;
	}

	int w = dst.right - dst.left, h = dst.bottom - dst.top;
	HRESULT hr = S_OK;
	if (!::EqualRect(&lastdst_, &dst)) {
		lastdst_ = dst;
		hr = win_->SetWindowPosition(dst.left, dst.top, w, h);
	}
	applyDest(w, h);
	return SUCCEEDED(hr);
}

bool MpcVideoRenderer::displayModeChanged()
{
	//[pcr-video] MPC VR tracks display changes itself (child window + own
	//drain messages); nothing to forward through IVideoWindow.
	return true;
}

bool MpcVideoRenderer::getNativeVideoSize(SIZE& size)
{
	//[pcr-video] needed by ratio-fixed mouse sizing (GlobalValues::getAspect):
	//EVR asks IMFVideoDisplayControl; MPC VR exposes the current media size via
	//IBasicVideo::GetVideoSize.
	CComQIPtr<IBasicVideo> bv = render_;
	if (!bv) { return false; }
	long w = 0, h = 0;
	if (FAILED(bv->GetVideoSize(&w, &h)) || w <= 0 || h <= 0) { return false; }
	size.cx = w;
	size.cy = h;
	return true;
}

IBaseVideoRenderer* createVideoRenderer(bool dxva2, int renderer)
{
	if (renderer == 1)
	{
		//[pcr-video] probe first: MPC VR must be registered (the bundled
		// registration runs at startup, or the user installed it itself).
		CComPtr<IBaseFilter> probe;
		if (SUCCEEDED(probe.CoCreateInstance(CLSID_PcrMpcVideoRenderer, NULL, CLSCTX_INPROC_SERVER)) && probe)
		{
			return new MpcVideoRenderer();
		}
	}
	return new EVRCustom(dxva2);
}

}