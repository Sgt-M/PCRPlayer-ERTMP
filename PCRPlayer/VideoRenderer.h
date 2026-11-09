#pragma once

#pragma comment(lib, "strmiids.lib")

#include <atlbase.h>
#include <dshow.h>

#include <d3d9.h>
#include <vmr9.h>
#include <evr.h>
#include <evr9.h>

#include <initguid.h>
#include <qnetwork.h>
//[port] qedit.h is unused and absent from modern SDKs: #include <qedit.h>

#include <streams.h>

#include <Mfidl.h>

#include <Dvdmedia.h>

//#include <Wmsdk.h>
#include <Wmsdkidl.h>

#include <string>

#include "EVRCustom/EVRPresenter.h"

namespace dsp {

struct ProcAmpRange {
	int min;
	int max;
	int def;
	int step;
};

struct ProcAmpRangeGroup {
	ProcAmpRange brightness;
	ProcAmpRange contrast;
	ProcAmpRange hue;
	ProcAmpRange saturation;
};

struct ProcAmpValue {
	ProcAmpValue()
		: init(false)
		, brightness(0)
		, contrast(0)
		, hue(0)
		, saturation(0)
	{}

	// 初期化フラグ
	// デフォルト値は取得しないとわからないので
	// 初期化されているかどうかフラグが必要
	bool init;

	int brightness;
	int contrast;
	int hue;
	int saturation;
};

enum NOMINAL_RANGE {
	NOMINAL_RANGE_NORMAL = 0,
	NOMINAL_RANGE_WIDE = 1,

	NOMINAL_RANGE_0_255 = 0,
	NOMINAL_RANGE_16_235 = 1,
	NOMINAL_RANGE_48_208 = 2,
	NOMINAL_RANGE_64_127 = 3,
	NOMINAL_RANGE_COUNT = 4,
};

class IBaseVideoRenderer {
protected:
	HWND hwnd_;
	CComPtr<IBaseFilter> render_;
	CComPtr<IQualProp> prop_;

public:
	IBaseVideoRenderer()
		: hwnd_(NULL)
	{}
	virtual ~IBaseVideoRenderer() {}

	HWND getHWND() { return hwnd_; };

	virtual operator IBaseFilter*() = 0;
	virtual operator CComPtr<IBaseFilter>&() = 0;
	virtual void detach() = 0;

	virtual bool addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd) = 0;
	virtual bool repaint(HDC hdc) = 0;
	virtual bool setVideoPosition(RECT& dst) = 0;
	virtual bool displayModeChanged() = 0;
	virtual bool getNativeVideoSize(SIZE& size) = 0;
	virtual bool saveCurrentImage(const std::wstring& path, int format, int jpegQuality, UINT width, UINT height, bool fit) { return false; }
	virtual bool getFramerate(int& rate);
	virtual bool getCurrentFramerate(int& rate);
	virtual bool getFramesDrawn(int& frames);
	virtual bool getFramesDropped(int& frames);
	virtual bool getSampleTime(LONGLONG& time) { return false; }
	virtual bool getProcAmpRange(ProcAmpRangeGroup& group) { return false; }
	virtual bool setProcAmp(const ProcAmpValue& value) { return false; }
	virtual bool getProcAmp(ProcAmpValue& value) { return false; }
	virtual bool setNominalRange(int value) { return false; }
	virtual bool setResizer(int type) { return false; }
	virtual bool isRunning(DWORD timeout);
};

class VMR7 : public IBaseVideoRenderer {
	CComPtr<IVMRWindowlessControl> control_;

public:
	operator IBaseFilter*() { return render_; }
	operator CComPtr<IBaseFilter>&() { return render_; }
	void detach() { render_.Detach(); control_.Detach(); }
	bool addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd);
	bool repaint(HDC hdc);
	bool setVideoPosition(RECT& dst);
	bool displayModeChanged();
	bool getNativeVideoSize(SIZE& size);
};


class EVR : public IBaseVideoRenderer {
	CComPtr<IMFVideoDisplayControl> control_;
	
public:
	EVR() {}
	~EVR() {}

	operator IBaseFilter*() { return render_; }
	operator CComPtr<IBaseFilter>&() { return render_; }
	void detach() { render_.Detach(); control_.Detach(); }
	bool addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd);
	bool repaint(HDC hdc);
	bool setVideoPosition(RECT& dst);
	bool displayModeChanged();
	bool getNativeVideoSize(SIZE& size);

private:
	template<typename T>
	bool getService(const CComPtr<IBaseFilter>& evr, REFGUID guidService, CComPtr<T>& dst);
};


//[pcr-video] MPC Video Renderer (external DShow filter, D3D11). embedded via IVideoWindow.
//[pcr-video] MPC-BE/MPC-HC common filter-config interface (minimal decl; the
//IID is from Aleksoid1978/VideoRenderer Include/FilterInterfaces.h). Used to
//set "lessRedraws": without it MPC VR runs a synchronous Render() on the
//renderer lock for every window position/size change = UI-thread stalls.
interface __declspec(uuid("37CBDF10-D65E-4E5A-8F37-40E0C8EA1695")) IExFilterConfigPcr : public IUnknown
{
	STDMETHOD(Flt_GetBool  )(LPCSTR field, bool    *value) PURE;
	STDMETHOD(Flt_GetInt   )(LPCSTR field, int     *value) PURE;
	STDMETHOD(Flt_GetInt64 )(LPCSTR field, __int64 *value) PURE;
	STDMETHOD(Flt_GetDouble)(LPCSTR field, double  *value) PURE;
	STDMETHOD(Flt_GetString)(LPCSTR field, LPWSTR  *value, unsigned *chars) PURE;
	STDMETHOD(Flt_GetBin   )(LPCSTR field, LPVOID  *value, unsigned *size ) PURE;
	STDMETHOD(Flt_SetBool  )(LPCSTR field, bool    value) PURE;
	STDMETHOD(Flt_SetInt   )(LPCSTR field, int     value) PURE;
	STDMETHOD(Flt_SetInt64 )(LPCSTR field, __int64 value) PURE;
	STDMETHOD(Flt_SetDouble)(LPCSTR field, double  value) PURE;
	STDMETHOD(Flt_SetString)(LPCSTR field, LPWSTR  value, int chars) PURE;
	STDMETHOD(Flt_SetBin   )(LPCSTR field, LPVOID  value, int size ) PURE;
};

//[pcr-video] CLSID of the MPC Video Renderer filter (registered by the app or user)
extern const CLSID CLSID_PcrMpcVideoRenderer;

//[pcr-aspect] defined in VideoRenderer.cpp (subclass hooks)
LRESULT CALLBACK MpcAspectSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK MpcChildPosSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

class MpcVideoRenderer : public IBaseVideoRenderer {
	CComPtr<IVideoWindow> win_;
	CComPtr<IBasicVideo> bvideo_;   //[pcr-video] render rect (MPC VR needs IBasicVideo)
	RECT lastdst_;
	RECT lastfit_;                   //[pcr-aspect] last aspect-fit destination applied
	bool fitPending_;                //[pcr-aspect] native size not known yet -> retry

	HWND embedHwnd_;                 //[pcr-video] deferred embed (UI thread only)
	HWND childHwnd_;                 //[pcr-aspect] MPC VR's own video child window
	int childFitX_, childFitY_;        //[pcr-aspect] forced position for the child
	bool childForced() const { return childHwnd_ != NULL && (childFitX_ != 0 || childFitY_ != 0); }
	friend LRESULT CALLBACK MpcChildPosSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
	int childX() const { return childFitX_; }
	int childY() const { return childFitY_; }
	bool embedded_;

	//[pcr-aspect] MPC VR renders exactly the IBasicVideo destination rect, so the
	//player must pass a contain-fit (letterboxed/pillarboxed) rect itself.
	void applyDest(int w, int h);
	HWND FindMpcChildWindow();           //[pcr-aspect] locate MPC VR's child window

public:
	MpcVideoRenderer() : embedHwnd_(NULL), childHwnd_(NULL), childFitX_(0), childFitY_(0), embedded_(false), fitPending_(false) { ::SetRectEmpty(&lastdst_); ::SetRectEmpty(&lastfit_); }
	~MpcVideoRenderer()
	{
		//[pcr-aspect] drop the retry hook before 'this' goes away
		if (embedHwnd_) {
			if (fitPending_) { ::KillTimer(embedHwnd_, 0x5EC1); }
			::RemoveWindowSubclass(embedHwnd_, MpcAspectSubclass, 0);
		}
		if (childHwnd_) { ::RemoveWindowSubclass(childHwnd_, MpcChildPosSubclass, 1); }
	}

	//[pcr-aspect] internal: WM_TIMER retry hook plumbing (see VideoRenderer.cpp)
	bool retryFit();
	void invalidateTimer();

	operator IBaseFilter*() { return render_; }
	operator CComPtr<IBaseFilter>&() { return render_; }
	void detach() { render_.Detach(); win_.Detach(); bvideo_.Detach(); }
	bool addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd);
	bool repaint(HDC hdc);
	bool setVideoPosition(RECT& dst);
	bool displayModeChanged();
	bool getNativeVideoSize(SIZE& size);
};


//[pcr-video] renderer factory: 1=MPC Video Renderer (falls back to EVRCustom when
//the filter is not installed), 0=EVRCustom as before.
IBaseVideoRenderer* createVideoRenderer(bool dxva2, int renderer);


class EVRCustom : public IBaseVideoRenderer {
	CComPtr<IMFVideoDisplayControl> control_;
	CComPtr<ICustomRenderProp> custom_;
	CComPtr<IMFVideoProcessor> proc_;
	
	bool dxva2_;

public:
	EVRCustom(bool dxva2) : dxva2_(dxva2) {}
	~EVRCustom() {}

	operator IBaseFilter*() { return render_; }
	operator CComPtr<IBaseFilter>&() { return render_; }
	void detach() { render_.Detach(); control_.Detach(); custom_.Detach(); proc_.Detach(); }
	bool addToGraph(CComPtr<IGraphBuilder>& graph, HWND hwnd);
	bool repaint(HDC hdc);
	bool setVideoPosition(RECT& dst);
	bool displayModeChanged();
	bool getNativeVideoSize(SIZE& size);
	bool saveCurrentImage(const std::wstring& path, int format, int jpegQuality, UINT width, UINT height, bool fit);
	bool getSampleTime(LONGLONG& time);
	bool getProcAmpRange(ProcAmpRangeGroup& group);
	bool setProcAmp(const ProcAmpValue& value);
	bool getProcAmp(ProcAmpValue& value);
	bool setNominalRange(int value);
	bool setResizer(int type);

private:
	bool getProcAmpRange(DWORD prop, ProcAmpRange& range);

	template<typename T>
	bool getService(const CComPtr<IBaseFilter>& evr, REFGUID guidService, CComPtr<T>& dst);
};



}
