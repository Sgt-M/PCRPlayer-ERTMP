#include "stdafx.h"

namespace utl {

WindowSnap::WindowSnap()
	: x_(0)
	, y_(0)
{}

void WindowSnap::Init()
{
	diff_.SetRectEmpty();
}

namespace {
struct SnapFind {
	HWND self;
	bool xdir;
	int side; //[pcr] 0=any, 1=align my left/top, 2=align my right/bottom
	CRect org;
	LONG dist;
	LONG best;
	bool found;
};

BOOL CALLBACK snapFindProc(HWND h, LPARAM lp)
{
	SnapFind& f = *(SnapFind*)lp;
	if (h == f.self || ::GetAncestor(h, GA_ROOT) == f.self) { return TRUE; }
	if (!::IsWindowVisible(h) || ::IsIconic(h) || !::IsWindowEnabled(h)) { return TRUE; }

	CRect o;
	if (!utl::getWindowShapeRect(h, o))
	{
		if (!::GetWindowRect(h, &o)) { return TRUE; }
	}
	if (o.IsRectEmpty()) { return TRUE; }

	auto tryDelta = [&](LONG d, int s) {
		if (f.side != 0 && f.side != s) { return; }
		if (abs(d) < f.dist && abs(d) < abs(f.best)) { f.best = d; f.found = true; }
	};

	if (f.xdir)
	{
		if (o.top < f.org.bottom && o.bottom > f.org.top)
		{
			tryDelta(o.right - f.org.left, 1);  // my left  <- their right
			tryDelta(o.left - f.org.right, 2);  // my right <- their left
		}
	}
	else
	{
		if (o.left < f.org.right && o.right > f.org.left)
		{
			tryDelta(o.bottom - f.org.top, 1);  // my top    <- their bottom
			tryDelta(o.top - f.org.bottom, 2);  // my bottom <- their top
		}
	}
	return TRUE;
}

bool findSnapDelta(HWND hwnd, bool xdir, int side, const CRect& org, LONG dist, LONG& delta)
{
	SnapFind f;
	f.self = hwnd;
	f.xdir = xdir;
	f.side = side;
	f.org = org;
	f.dist = dist;
	f.best = dist;
	f.found = false;
	::EnumWindows(snapFindProc, (LPARAM)&f);
	if (!f.found) { return false; }
	delta = f.best;
	return true;
}
};

//[pcr-fix] drag-direction memory: candidates on the side the user is PULLING
//toward win over a nearer opposite-side magnet (was: nearest overall, so an
//unrelated window on the far side pinned the window and left a few-px
//overlap with the intended target; also seam-vs-edge cases).
static CPoint g_prevPt = {0, 0};
static int g_dirX = 0;
static int g_dirY = 0;

//[pcr-fix] sticky magnet: once a snap engages during THIS drag, keep pinning
//my edge to that same target edge while within its band, even if an opposite
//magnet reports a smaller |delta|. Without this, two opposing bands move in
//lockstep during a pull (measured: wL=-3/wR=-7 -> wL=-1/wR=-5...) and the
//4px between them never closes (user: 'still overlaps by a few px').
static int g_stickX = 0;   // 0 none, 1 my-left pinned, 2 my-right pinned
static LONG g_stickXEdge = 0;
static LONG g_stickXBand = 0;
static int g_stickY = 0;   // 0 none, 1 my-top pinned, 2 my-bottom pinned
static LONG g_stickYEdge = 0;
static LONG g_stickYBand = 0;

//[pcr-fix] screen-edge candidates as deltas (split per side so the caller can prefer the pull direction)
static bool screenDeltaXL(const MONITORINFO& mi, const CRect& rc, LONG dist, LONG& d)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);
	if (abs(work.left - rc.left) < dist) { d = work.left - rc.left; return true; }
	if (abs(monitor.left - rc.left) < dist) { d = monitor.left - rc.left; return true; }
	return false;
}

static bool screenDeltaXR(const MONITORINFO& mi, const CRect& rc, LONG dist, LONG& d)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);
	if (abs(work.right - rc.right) < dist) { d = work.right - rc.right; return true; }
	if (abs(monitor.right - rc.right) < dist) { d = monitor.right - rc.right; return true; }
	return false;
}

static bool screenDeltaYT(const MONITORINFO& mi, const CRect& rc, LONG dist, LONG& d)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);
	if (abs(work.top - rc.top) < dist) { d = work.top - rc.top; return true; }
	if (abs(monitor.top - rc.top) < dist) { d = monitor.top - rc.top; return true; }
	return false;
}

static bool screenDeltaYB(const MONITORINFO& mi, const CRect& rc, LONG dist, LONG& d)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);
	if (abs(work.bottom - rc.bottom) < dist) { d = work.bottom - rc.bottom; return true; }
	if (abs(monitor.bottom - rc.bottom) < dist) { d = monitor.bottom - rc.bottom; return true; }
	return false;
}

void WindowSnap::OnEnterSizeMove(HWND hwnd)
{
	CRect rc, shape;
	GetWindowRect(hwnd, &rc);

	if (utl::getWindowShapeRect(hwnd, shape))
	{
		diff_.TopLeft() = shape.TopLeft() - rc.TopLeft();
		diff_.BottomRight() = rc.BottomRight() - shape.BottomRight();
	}

	rc.DeflateRect(diff_);

	CPoint pt;
	GetCursorPos(&pt);

	x_ = pt.x - rc.left;
	y_ = pt.y - rc.top;
	g_prevPt = pt;
	g_dirX = 0;
	g_dirY = 0;
	g_stickX = 0;
	g_stickY = 0;
}

void WindowSnap::OnMoving(HWND hwnd, LPRECT pRect, bool screen, bool window, const SnapDistance& distance)
{
	if (!screen && !window) { return; }

	distance_ = distance;

	CRect rc(pRect);

	//gl_.addLog(L"diff l:%d t:%d r:%d b:%d", diff_.left, diff_.top, diff_.right, diff_.bottom);

	rc.DeflateRect(diff_);
	//[pcr-fix] window snap measures against re-anchored rc (was org-measured but rc-applied -> anchor delta remained as few-px overlap)

	//-------------------

	CPoint pt;
	GetCursorPos(&pt);
	rc.OffsetRect(pt.x - (rc.left + x_), pt.y - (rc.top  + y_));

	//[pcr-fix] gather BOTH candidate kinds per axis and apply the NEARER one.
	//The old logic gave screen edges unconditional priority whenever one was
	//in range, so next to a monitor seam a window edge a few px closer was
	//ignored (user: 2px gap at the seam / small overlaps elsewhere).
	{
		//[pcr-fix] remember pull direction (last nonzero cursor delta of this drag)
		if (pt.x != g_prevPt.x) { g_dirX = (pt.x > g_prevPt.x) ? 1 : -1; }
		if (pt.y != g_prevPt.y) { g_dirY = (pt.y > g_prevPt.y) ? 1 : -1; }
		g_prevPt = pt;

		MONITORINFO mi;
		const bool haveMi = screen && utl::getDesktopRectFromRect(rc, mi);
		struct C1 { bool ok; LONG d; int side; LONG band; };

		//X axis: honored stick first, else nearest-on-pull-side selection
		if (g_stickX)
		{
			LONG d = (g_stickX == 1) ? (g_stickXEdge - rc.left) : (g_stickXEdge - rc.right);
			if (abs(d) < g_stickXBand) { rc.OffsetRect(d, 0); }
			else { g_stickX = 0; }
		}
		if (!g_stickX)
		{
			C1 sl = {false,0,1,0}, sr = {false,0,2,0}, wl = {false,0,1,0}, wr = {false,0,2,0};
			if (haveMi) { sl.ok = screenDeltaXL(mi, rc, distance_.screen, sl.d); sr.ok = screenDeltaXR(mi, rc, distance_.screen, sr.d); sl.band = sr.band = distance_.screen; }
			if (window) { wl.ok = findSnapDelta(hwnd, true, 1, rc, distance_.window, wl.d); wr.ok = findSnapDelta(hwnd, true, 2, rc, distance_.window, wr.d); wl.band = wr.band = distance_.window; }
			auto bestOf = [](std::initializer_list<C1> v) -> C1 {
				C1 r = {false, 0, 0, 0};
				for (std::initializer_list<C1>::iterator it = v.begin(); it != v.end(); ++it)
				{
					if (it->ok && (!r.ok || abs(it->d) < abs(r.d))) { r = *it; }
				}
				return r;
			};
			C1 pick = {false,0,0,0};
			if (g_dirX < 0) { pick = bestOf({sl, wl}); }
			else if (g_dirX > 0) { pick = bestOf({sr, wr}); }
			if (!pick.ok) { pick = bestOf({sl, wl, sr, wr}); }
			if (pick.ok)
			{
				rc.OffsetRect(pick.d, 0);
				g_stickX = pick.side;
				g_stickXBand = pick.band;
				g_stickXEdge = (pick.side == 1) ? rc.left : rc.right;
			}
			else { (void)0; }
		}

		//Y axis: same
		if (g_stickY)
		{
			LONG d = (g_stickY == 1) ? (g_stickYEdge - rc.top) : (g_stickYEdge - rc.bottom);
			if (abs(d) < g_stickYBand) { rc.OffsetRect(0, d); }
			else { g_stickY = 0; }
		}
		if (!g_stickY)
		{
			C1 st = {false,0,1,0}, sb2 = {false,0,2,0}, wt = {false,0,1,0}, wb = {false,0,2,0};
			if (haveMi) { st.ok = screenDeltaYT(mi, rc, distance_.screen, st.d); sb2.ok = screenDeltaYB(mi, rc, distance_.screen, sb2.d); st.band = sb2.band = distance_.screen; }
			if (window) { wt.ok = findSnapDelta(hwnd, false, 1, rc, distance_.window, wt.d); wb.ok = findSnapDelta(hwnd, false, 2, rc, distance_.window, wb.d); wt.band = wb.band = distance_.window; }
			auto bestOf = [](std::initializer_list<C1> v) -> C1 {
				C1 r = {false, 0, 0, 0};
				for (std::initializer_list<C1>::iterator it = v.begin(); it != v.end(); ++it)
				{
					if (it->ok && (!r.ok || abs(it->d) < abs(r.d))) { r = *it; }
				}
				return r;
			};
			C1 pickY = {false,0,0,0};
			if (g_dirY < 0) { pickY = bestOf({st, wt}); }
			else if (g_dirY > 0) { pickY = bestOf({sb2, wb}); }
			if (!pickY.ok) { pickY = bestOf({st, wt, sb2, wb}); }
			if (pickY.ok)
			{
				rc.OffsetRect(0, pickY.d);
				g_stickY = pickY.side;
				g_stickYBand = pickY.band;
				g_stickYEdge = (pickY.side == 1) ? rc.top : rc.bottom;
			}
		}

		//keep diag block's expected vars alive with stub values when stick held
		C1 sl = {false,0,1,0}, sr = {false,0,2,0}, wl = {false,0,1,0}, wr = {false,0,2,0};
		C1 st = {false,0,1,0}, sb2 = {false,0,2,0}, wt = {false,0,1,0}, wb = {false,0,2,0};
		C1 pick = {g_stickX != 0, 0, g_stickX, g_stickXBand};
		C1 pickY = {g_stickY != 0, 0, g_stickY, g_stickYBand};

		//[pcr-diag] snap decision trace (only on change): 9999 = no candidate in band
		{
			auto v = [](C1 c) -> LONG { return c.ok ? c.d : 9999; };
			static LONG lastSig = 0x7fffffff;
			LONG sig = (g_dirX + 2) * 1000000L + (g_dirY + 2) * 100000L
				+ (abs(v(pick)) % 100) * 100L + (abs(v(pickY)) % 100);
			if (sig != lastSig)
			{
				lastSig = sig;
				pcrweb::Log(L"snap cand X dir=%d [sL=%d sR=%d wL=%d wR=%d]->%d", g_dirX, v(sl), v(sr), v(wl), v(wr), v(pick));
				pcrweb::Log(L"snap cand Y dir=%d [sT=%d sB=%d wT=%d wB=%d]->%d", g_dirY, v(st), v(sb2), v(wt), v(wb), v(pickY));
			}
		}
	}

	//-------------------
	rc.InflateRect(diff_);
	*pRect = rc;
}

bool WindowSnap::snapS(int a, int b)
{
	return (abs(a - b) < distance_.screen);
}

bool WindowSnap::snapW(int a, int b)
{
	return (abs(a - b) < distance_.window);
}


bool WindowSnap::screenSnapX(HWND hwnd, const MONITORINFO& mi, CRect& rc)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);
	if (snapS(work.left, rc.left))
	{
		rc.OffsetRect(work.left - rc.left, 0); return true;
	}
	else if (snapS(work.right, rc.right))
	{
		rc.OffsetRect(work.right - rc.right, 0); return true;
	}
	else if (snapS(monitor.left, rc.left))
	{
		rc.OffsetRect(monitor.left - rc.left, 0); return true;
	}
	else if (snapS(monitor.right, rc.right))
	{
		rc.OffsetRect(monitor.right - rc.right, 0); return true;
	}
	return false;
}

bool WindowSnap::screenSnapY(HWND hwnd, const MONITORINFO& mi, CRect& rc)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);

	if (snapS(work.top, rc.top))
	{
		rc.OffsetRect(0, work.top - rc.top); return true;
	}
	else if (snapS(work.bottom, rc.bottom))
	{
		rc.OffsetRect(0, work.bottom - rc.bottom); return true;
	}
	else if (snapS(monitor.top, rc.top))
	{
		rc.OffsetRect(0, monitor.top - rc.top); return true;
	}
	else if (snapS(monitor.bottom, rc.bottom))
	{
		rc.OffsetRect(0, monitor.bottom - rc.bottom); return true;
	}
	return false;
}

//[pcr-fix] the old detection probed one point per side with WindowFromPoint,
//which only sees what is VISIBLE there: while the snap target was overlapped
//by the dragged window the probe returned self or the desktop and window-snap
//silently failed (the user had to un-overlap first). Scan top-level windows
//instead so covered windows snap too.

bool WindowSnap::windowSnapX(HWND hwnd, const CRect& org, CRect& rc)
{
	LONG d = 0;
	if (!findSnapDelta(hwnd, true, 0, org, distance_.window, d)) { return false; }
	rc.OffsetRect(d, 0);
	return true;
}

bool WindowSnap::windowSnapY(HWND hwnd, const CRect& org, CRect& rc)
{
	LONG d = 0;
	if (!findSnapDelta(hwnd, false, 0, org, distance_.window, d)) { return false; }
	rc.OffsetRect(0, d);
	return true;
}


void SizeMove::setRect(const CRect& rc)
{
	rc_ = rc;
	reset_ = true;
}

bool SizeMove::getRect(CRect& rc)
{
	if (!reset_) { return false; }
	//if (!sizemove_) { return false; }
	rc = rc_;
	reset_ = false;
	return true;
}


};

