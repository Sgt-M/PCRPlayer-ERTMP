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

	auto tryDelta = [&](LONG d) {
		if (abs(d) < f.dist && abs(d) < abs(f.best)) { f.best = d; f.found = true; }
	};

	if (f.xdir)
	{
		if (o.top < f.org.bottom && o.bottom > f.org.top) //[pcr] keep vertical overlap requirement (old probe semantics)
		{
			tryDelta(o.right - f.org.left);  // my left  <- their right
			tryDelta(o.left - f.org.right);  // my right <- their left
		}
	}
	else
	{
		if (o.left < f.org.right && o.right > f.org.left)
		{
			tryDelta(o.bottom - f.org.top);  // my top    <- their bottom
			tryDelta(o.top - f.org.bottom);  // my bottom <- their top
		}
	}
	return TRUE;
}

bool findSnapDelta(HWND hwnd, bool xdir, const CRect& org, LONG dist, LONG& delta)
{
	SnapFind f;
	f.self = hwnd;
	f.xdir = xdir;
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

//[pcr-fix] screen-edge candidates as deltas (same priority order as the old
//screenSnapX/Y) so the caller can compare them against window deltas.
static bool screenDeltaX(const MONITORINFO& mi, const CRect& rc, LONG dist, LONG& d)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);
	if (abs(work.left - rc.left) < dist) { d = work.left - rc.left; return true; }
	if (abs(work.right - rc.right) < dist) { d = work.right - rc.right; return true; }
	if (abs(monitor.left - rc.left) < dist) { d = monitor.left - rc.left; return true; }
	if (abs(monitor.right - rc.right) < dist) { d = monitor.right - rc.right; return true; }
	return false;
}

static bool screenDeltaY(const MONITORINFO& mi, const CRect& rc, LONG dist, LONG& d)
{
	CRect work(mi.rcWork);
	CRect monitor(mi.rcMonitor);
	if (abs(work.top - rc.top) < dist) { d = work.top - rc.top; return true; }
	if (abs(work.bottom - rc.bottom) < dist) { d = work.bottom - rc.bottom; return true; }
	if (abs(monitor.top - rc.top) < dist) { d = monitor.top - rc.top; return true; }
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
		MONITORINFO mi;
		const bool haveMi = screen && utl::getDesktopRectFromRect(rc, mi);
		LONG ds = 0, dw = 0;
		bool hs = false, hw = false;
		if (haveMi) { hs = screenDeltaX(mi, rc, distance_.screen, ds); }
		if (window) { hw = findSnapDelta(hwnd, true, rc, distance_.window, dw); }
		if (hs && (!hw || abs(ds) <= abs(dw))) { rc.OffsetRect(ds, 0); }
		else if (hw) { rc.OffsetRect(dw, 0); }

		hs = false; hw = false; ds = 0; dw = 0;
		if (haveMi) { hs = screenDeltaY(mi, rc, distance_.screen, ds); }
		if (window) { hw = findSnapDelta(hwnd, false, rc, distance_.window, dw); }
		if (hs && (!hw || abs(ds) <= abs(dw))) { rc.OffsetRect(0, ds); }
		else if (hw) { rc.OffsetRect(0, dw); }
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
	if (!findSnapDelta(hwnd, true, org, distance_.window, d)) { return false; }
	rc.OffsetRect(d, 0);
	return true;
}

bool WindowSnap::windowSnapY(HWND hwnd, const CRect& org, CRect& rc)
{
	LONG d = 0;
	if (!findSnapDelta(hwnd, false, org, distance_.window, d)) { return false; }
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

