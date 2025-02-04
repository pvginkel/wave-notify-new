/*
 * This file is part of Google Wave Notifier.
 *
 * Google Wave Notifier is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Google Wave Notifier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Google Wave Notifier.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _INC_GDI
#define _INC_GDI

#pragma once

class CDC
{
private:
	HDC m_hDC;
	BOOL m_fMustDelete;

public:
	CDC() : m_hDC(NULL), m_fMustDelete(FALSE) { }
	virtual ~CDC() {
		if (m_fMustDelete) {
			ASSERT(m_hDC != NULL);
			DeleteDC(m_hDC);
		}
	}
	
	void CreateCompatibleDC(CDC * lpDC) {
		SetHandle(::CreateCompatibleDC(lpDC == NULL ? GetDC(NULL) : lpDC->GetHandle()));
		m_fMustDelete = TRUE;
	}
	void FrameRect(LPRECT lprc, HBRUSH hBrush) const {
		CHECK_PTR(lprc);
		CHECK_HANDLE(hBrush);

		int nResult = ::FrameRect(m_hDC, lprc, hBrush);

		CHECK_NE_0(nResult);
	}
	void FillRect(LPRECT lprc, HBRUSH hBrush) const {
		CHECK_PTR(lprc);
		CHECK_HANDLE(hBrush);

		int nResult = ::FillRect(m_hDC, lprc, hBrush);

		CHECK_NE_0(nResult);
	}
	void DrawIconEx(int xLeft, int yTop, HICON hIcon, int cxWidth, int cyWidth, UINT istepIfAniCur = 0, HBRUSH hbrFlickerFreeDraw = NULL, UINT diFlags = DI_NORMAL) const {
		BOOL fResult = ::DrawIconEx(m_hDC, xLeft, yTop, hIcon, cxWidth, cyWidth, istepIfAniCur, hbrFlickerFreeDraw, diFlags);

		CHECK(fResult);
	}
	HGDIOBJ SelectObject(HGDIOBJ hGdiObj) const {
		return ::SelectObject(m_hDC, hGdiObj);
	}
	HGDIOBJ SelectFont(HFONT hFont) const {
		CHECK_HANDLE(hFont);

		return ::SelectObject(m_hDC, hFont);
	}
	HGDIOBJ SelectPen(HPEN hPen) const {
		CHECK_HANDLE(hPen);

		return ::SelectObject(m_hDC, hPen);
	}
	void GetTextMetrics(LPTEXTMETRIC lptm) const {
		CHECK_PTR(lptm);

		BOOL fResult = ::GetTextMetrics(m_hDC, lptm);

		CHECK(fResult);
	}
	void DrawText(wstring szText, LPRECT lprc, UINT format) const {
		CHECK_PTR(lprc);

		int nResult = ::DrawText(m_hDC, szText.c_str(), szText.size(), lprc, format);

		CHECK_NE_0(nResult);
	}
	HDC GetHandle() const {
		ASSERT(m_hDC != NULL);

		return m_hDC;
	}
	void SetHandle(HDC hDC) {
		ASSERT(m_hDC == NULL && hDC != NULL);

		m_hDC = hDC;
	}
	COLORREF SetTextColor(COLORREF cr) const { return ::SetTextColor(m_hDC, cr); }
	COLORREF SetBkColor(COLORREF cr) const { return ::SetBkColor(m_hDC, cr); }
	int SetBkMode(int nMode) const { return ::SetBkMode(m_hDC, nMode); }
	void MoveToEx(INT x, INT y, LPPOINT lppt = NULL) const {
		BOOL fResult = ::MoveToEx(m_hDC, x, y, lppt);

		CHECK(fResult);
	}
	void LineTo(INT x, INT y) const {
		int nResult = ::LineTo(m_hDC, x, y);

		CHECK_NE_0(nResult);
	}
	void BitBlt(POINT ptDest, SIZE szDest, CDC * lpSourceDC, POINT ptSource, DWORD dwRop) const {
		ASSERT(lpSourceDC != NULL);

		BOOL fResult = ::BitBlt(m_hDC, ptDest.x, ptDest.y, szDest.cx, szDest.cy, lpSourceDC->GetHandle(), ptSource.x, ptSource.y, dwRop);

		CHECK(fResult);
	}
	INT ExcludeClipRect(LPRECT lprc) {
		return ::ExcludeClipRect(m_hDC, lprc->left, lprc->top, lprc->right, lprc->bottom);
	}
	BOOL GradientFill(PTRIVERTEX pVertex, ULONG dwNumVertex, PVOID pMesh, ULONG dwNumMesh, ULONG dwMode) {
		return ::GradientFill(m_hDC, pVertex, dwNumVertex, pMesh, dwNumMesh, dwMode);
	}
};

class CWindowDC : public CDC
{
private:
	CWindowHandle * m_lpWindow;

public:
	CWindowDC(CWindowHandle * lpWindow) : m_lpWindow(lpWindow) {
		SetHandle(GetWindowDC(lpWindow->GetHandle()));
	}
	~CWindowDC() {
		ReleaseDC(m_lpWindow->GetHandle(), GetHandle());
	}
};

class CClientDC : public CDC
{
private:
	CWindowHandle * m_lpWindow;

public:
	CClientDC(CWindowHandle * lpWindow) : m_lpWindow(lpWindow) {
		SetHandle(GetDC(lpWindow == NULL ? NULL : lpWindow->GetHandle()));
	}
	~CClientDC() {
		ReleaseDC(m_lpWindow == NULL ? NULL : m_lpWindow->GetHandle(), GetHandle());
	}
};

class CPaintDC : public CDC
{
private:
	CWindowHandle * m_lpWindow;
	PAINTSTRUCT m_ps;

public:
	CPaintDC(CWindowHandle * lpWindow) : m_lpWindow(lpWindow) {
		ASSERT(lpWindow != NULL);
		SetHandle(BeginPaint(m_lpWindow->GetHandle(), &m_ps));
	}
	~CPaintDC() {
		EndPaint(m_lpWindow->GetHandle(), &m_ps);
	}
};

class CDoubleBufferedDC : public CDC
{
private:
	HBITMAP m_hMemBitmap;
	CDC * m_lpParent;
	BOOL m_fCompleted;
	CWindowHandle * m_lpWindow;
	HGDIOBJ m_hOriginal;
	SIZE m_szSize;

public:
	CDoubleBufferedDC(CWindowHandle * lpWindow, CDC * lpParent) :
		m_lpParent(lpParent),
		m_fCompleted(FALSE),
		m_lpWindow(lpWindow)
	{
		ASSERT(lpWindow != NULL);

		CreateCompatibleDC(lpParent);

		RECT rc;
		m_lpWindow->GetWindowRect(&rc);
		m_szSize.cx = rc.right - rc.left;
		m_szSize.cy = rc.bottom - rc.top;

		m_hMemBitmap = CreateCompatibleBitmap(m_lpParent->GetHandle(), m_szSize.cx, m_szSize.cy);
		m_hOriginal = SelectObject(m_hMemBitmap);

		ASSERT(m_hMemBitmap != NULL);
	}
	virtual ~CDoubleBufferedDC() { Complete(); }

	void Complete() {
		if (!m_fCompleted) {
			m_fCompleted = TRUE;

			POINT pt = { 0, 0 };
			m_lpParent->BitBlt(pt, m_szSize, this, pt, SRCCOPY);

			SelectObject(m_hOriginal);
			DeleteObject(m_hMemBitmap);
		}
	}
};

#endif // _INC_GDI
