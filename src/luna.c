/*
 * NewShell16 - a Calmira-style shell for Windows 3.1
 * Copyright (C) 2026 NewShell16 contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 * luna.c - the Windows XP ("Luna") look of the taskbar and the Start menu
 *
 * Used while the active Retheme16 theme asks for it ([Shell] Style=XP in
 * its THEME.INI, e.g. the RedmondExperience theme), or always / never with
 *
 *   [Taskbar]
 *   Look=0             ; 0: as the theme says, 1: Windows XP, 2: Windows 95,
 *                      ; 3: Windows 11, 4: Platinum, 5: Aqua
 *
 * This module also decides the look of the taskbar as a whole: Style=11
 * (the Redmond-11 theme) selects the Windows 11 look of win11.c, Style=OS9
 * and Style=OSX (Cupertino Platinum and Aqua) the looks of mac.c.
 *
 * Everything is drawn procedurally (gradients and anti-aliased rounded
 * corners), so the look costs no bitmaps and no memory while it is not
 * used: this module is a discardable code segment of its own (see
 * docs/building.md), which Windows can drop when memory is short.
 * Gradients want a high-colour display; GDI dithers them otherwise.
 */
#include <string.h>
#include "newshell.h"

/* a gradient stop: position 0..255 from the top, colour (newshell.h) */
typedef NSSTOP LSTOP;

static const LSTOP s_stBar[] = {
    {   0,  60, 129, 235 }, {  10, 100, 160, 248 }, {  45,  52, 120, 238 },
    { 120,  36,  94, 222 }, { 215,  32,  88, 216 }, { 255,  24,  70, 190 }
};
static const LSTOP s_stStart[] = {
    {   0, 125, 192, 105 }, {  30,  98, 178,  84 }, { 120,  58, 150,  58 },
    { 215,  50, 136,  50 }, { 255,  32, 104,  32 }
};
static const LSTOP s_stStartDown[] = {
    {   0,  32,  96,  32 }, {  60,  46, 124,  46 }, { 255,  76, 156,  70 }
};
static const LSTOP s_stTask[] = {
    {   0, 112, 170, 252 }, {  40,  66, 136, 242 }, { 255,  52, 112, 232 }
};
static const LSTOP s_stTaskDown[] = {
    {   0,  18,  56, 150 }, {  60,  30,  76, 180 }, { 255,  40,  96, 206 }
};
static const LSTOP s_stTray[] = {
    {   0,  48, 182, 252 }, {  20,  22, 162, 240 }, { 160,  16, 140, 230 },
    { 255,  12, 112, 212 }
};
static const LSTOP s_stBanner[] = {
    {   0,  61, 149, 255 }, {  50,   0,  96, 236 }, { 255,   0,  44, 150 }
};

#define NSTOPS(a)   (sizeof(a) / sizeof((a)[0]))

#define LC_TL   1
#define LC_TR   2
#define LC_BL   4
#define LC_BR   8

static BOOL    s_bKnown, s_bEngineSeen;
static int     s_nLook;        /* LOOK_* */
static HFONT   s_hfStart;

/* -------------------------------------------------------------------- */
/* When                                                                 */
/* -------------------------------------------------------------------- */

/* Re-reads the setting and the active theme; TRUE if the look changed */
BOOL LunaRefresh(void)
{
    int nOld = s_nLook;
    BOOL bWasKnown = s_bKnown;
    int nLook = CfgGetInt("Taskbar", "Look", 0);

    s_bEngineSeen = ThemeEngineActive();
    if (nLook == 1)
        s_nLook = LOOK_XP;
    else if (nLook == 2)
        s_nLook = LOOK_95;
    else if (nLook == 3)
        s_nLook = LOOK_11;
    else if (nLook == 4)
        s_nLook = LOOK_OS9;
    else if (nLook == 5)
        s_nLook = LOOK_OSX;
    else {
        char szTheme[MAXPATHLEN], szValue[16];

        s_nLook = LOOK_95;
        if (s_bEngineSeen && ThemeDirectory(szTheme, sizeof(szTheme))
            && ReadThemeString(szTheme, "Shell", "Style", szValue, sizeof(szValue))) {
            if (lstrcmpi(szValue, "XP") == 0)
                s_nLook = LOOK_XP;
            else if (lstrcmpi(szValue, "11") == 0)
                s_nLook = LOOK_11;
            else if (lstrcmpi(szValue, "OS9") == 0)
                s_nLook = LOOK_OS9;
            else if (lstrcmpi(szValue, "OSX") == 0)
                s_nLook = LOOK_OSX;
        }
    }
    s_bKnown = TRUE;
    return bWasKnown && s_nLook != nOld;
}

/* LOOK_95, LOOK_XP, LOOK_11, LOOK_OS9 or LOOK_OSX */
int ShellLook(void)
{
    if (!s_bKnown)
        LunaRefresh();
    return s_nLook;
}

BOOL LunaLook(void)
{
    return ShellLook() == LOOK_XP;
}

BOOL Win11Look(void)
{
    return ShellLook() == LOOK_11;
}

/* Platinum or Aqua (mac.c) */
BOOL MacLook(void)
{
    return ShellLook() == LOOK_OS9 || ShellLook() == LOOK_OSX;
}

/* Called from a timer: the theme engine was started or stopped */
BOOL LunaCheckEngine(void)
{
    if (s_bKnown && ThemeEngineActive() != s_bEngineSeen)
        return LunaRefresh();
    return FALSE;
}

void LunaFree(void)
{
    if (s_hfStart) {
        DeleteObject(s_hfStart);
        s_hfStart = NULL;
    }
}

/* -------------------------------------------------------------------- */
/* Drawing                                                              */
/* -------------------------------------------------------------------- */

static COLORREF StopColor(const LSTOP *lpst, int n, int y, int cy)
{
    int p = cy > 1 ? (int)((long)y * 255 / (cy - 1)) : 0, i;

    for (i = 0; i < n - 1; i++)
        if (p <= lpst[i + 1].pos)
            break;
    if (i >= n - 1)
        return RGB(lpst[n - 1].r, lpst[n - 1].g, lpst[n - 1].b);
    {
        int d = lpst[i + 1].pos - lpst[i].pos, t = p - lpst[i].pos;

        if (d <= 0)
            return RGB(lpst[i].r, lpst[i].g, lpst[i].b);
        return RGB(lpst[i].r + (lpst[i + 1].r - lpst[i].r) * t / d,
                   lpst[i].g + (lpst[i + 1].g - lpst[i].g) * t / d,
                   lpst[i].b + (lpst[i + 1].b - lpst[i].b) * t / d);
    }
}

static COLORREF Blend(COLORREF a, COLORREF b, int n)    /* n/16 of b */
{
    return RGB((GetRValue(a) * (16 - n) + GetRValue(b) * n) / 16,
               (GetGValue(a) * (16 - n) + GetGValue(b) * n) / 16,
               (GetBValue(a) * (16 - n) + GetBValue(b) * n) / 16);
}

/* Coverage (0..16) of pixel (x, y) of an r x r corner by a quarter
   circle of radius r around (r, r): 4 x 4 samples */
static int Coverage(int x, int y, int r)
{
    int i, j, n = 0;
    long r2 = (long)r * r * 64;

    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++) {
            long dx = (long)r * 8 - (x * 8 + i * 2 + 1);
            long dy = (long)r * 8 - (y * 8 + j * 2 + 1);

            if (dx < 0)
                dx = 0;
            if (dy < 0)
                dy = 0;
            if (dx * dx + dy * dy <= r2)
                n++;
        }
    return n;
}

/*
 * Fills lprc with a vertical gradient, with the corners named in fCorners
 * rounded off (radius r) and blended into what is already there.
 */
static void Shape(HDC hdc, const RECT FAR *lprc, const LSTOP *lpst, int nst,
                  int r, UINT fCorners)
{
    int cx = lprc->right - lprc->left, cy = lprc->bottom - lprc->top, y;

    if (cx <= 0 || cy <= 0)
        return;
    if (r > cx / 2)
        r = cx / 2;
    if (r > cy / 2)
        r = cy / 2;
    for (y = 0; y < cy; y++) {
        COLORREF clr = StopColor(lpst, nst, y, cy);
        int xl = 0, xr = cx, cyRow = -1, x;
        UINT fl = 0, fr = 0;
        RECT rc;

        if (y < r) {
            cyRow = y;
            fl = fCorners & LC_TL;
            fr = fCorners & LC_TR;
        } else if (y >= cy - r) {
            cyRow = cy - 1 - y;
            fl = fCorners & LC_BL;
            fr = fCorners & LC_BR;
        }
        if (fl)
            xl = r;
        if (fr)
            xr = cx - r;
        SetRect(&rc, lprc->left + xl, lprc->top + y, lprc->left + xr, lprc->top + y + 1);
        FillSolid(hdc, &rc, clr);
        for (x = 0; x < r; x++) {
            int n;

            if (fl && (n = Coverage(x, cyRow, r)) > 0) {
                int px = lprc->left + x, py = lprc->top + y;
                SetPixel(hdc, px, py, n >= 16 ? clr : Blend(GetPixel(hdc, px, py), clr, n));
            }
            if (fr && (n = Coverage(x, cyRow, r)) > 0) {
                int px = lprc->right - 1 - x, py = lprc->top + y;
                SetPixel(hdc, px, py, n >= 16 ? clr : Blend(GetPixel(hdc, px, py), clr, n));
            }
        }
    }
}

static void Line(HDC hdc, int x0, int y0, int x1, int y1, COLORREF clr)
{
    RECT rc;

    SetRect(&rc, x0, y0, x1, y1);
    FillSolid(hdc, &rc, clr);
}

/* A vertical gradient in a rectangle with all corners rounded (radius r),
   the corners blended into what is there (for mac.c) */
void LunaGradient(HDC hdc, const RECT FAR *lprc, const NSSTOP FAR *lpst, int nst, int r)
{
    LSTOP st[8];
    int i;

    for (i = 0; i < nst && i < 8; i++)
        st[i] = lpst[i];
    Shape(hdc, lprc, st, i, r, LC_TL | LC_TR | LC_BL | LC_BR);
}

/* The colour of a gradient at y of cy */
COLORREF LunaStopColor(const NSSTOP FAR *lpst, int nst, int y, int cy)
{
    LSTOP st[8];
    int i;

    for (i = 0; i < nst && i < 8; i++)
        st[i] = lpst[i];
    return StopColor(st, i, y, cy);
}

/* The taskbar background */
void LunaDrawBar(HDC hdc, const RECT FAR *lprc)
{
    Shape(hdc, lprc, s_stBar, NSTOPS(s_stBar), 0, 0);
}

/* A task button: rounded, light blue; pressed (active task) dark blue */
void LunaDrawTask(HDC hdc, const RECT FAR *lprc, BOOL bDown)
{
    RECT rc = *lprc;

    Shape(hdc, &rc, bDown ? s_stTaskDown : s_stTask,
          bDown ? NSTOPS(s_stTaskDown) : NSTOPS(s_stTask), 3, LC_TL | LC_TR | LC_BL | LC_BR);
    if (!bDown) {
        /* a lighter rim on the top and a darker one at the bottom */
        Line(hdc, rc.left + 3, rc.top, rc.right - 3, rc.top + 1, RGB(140, 190, 255));
        Line(hdc, rc.left + 3, rc.bottom - 1, rc.right - 3, rc.bottom, RGB(30, 80, 190));
    } else
        Line(hdc, rc.left + 3, rc.top, rc.right - 3, rc.top + 1, RGB(10, 36, 110));
}

/* Width of the Start button for the bar height */
static HFONT StartFont(int cyBar)
{
    if (!s_hfStart) {
        LOGFONT lf;

        _fmemset(&lf, 0, sizeof(lf));
        lf.lfHeight = -(cyBar * 4 / 7);
        lf.lfWeight = FW_BOLD;
        lf.lfItalic = TRUE;
        lf.lfCharSet = ANSI_CHARSET;
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
        lf.lfOutPrecision = OUT_TT_PRECIS;
        lstrcpy(lf.lfFaceName, "Arial");
        s_hfStart = CreateFontIndirect(&lf);
    }
    return s_hfStart ? s_hfStart : g_hfontBold;
}

int LunaStartWidth(HDC hdc, int cyBar)
{
    HFONT hfOld = SelectObject(hdc, StartFont(cyBar));
    int cx = TextWidth(hdc, "start");

    SelectObject(hdc, hfOld);
    return 8 + 16 + 6 + cx + cyBar / 2 + 4;
}

/* The green Start button with the flag and "start" in white italics */
void LunaDrawStart(HDC hdc, const RECT FAR *lprc, BOOL bDown)
{
    RECT rc = *lprc;
    int cy = rc.bottom - rc.top, off = bDown ? 1 : 0, nOldMode;
    HFONT hfOld;
    COLORREF clrOld;

    Shape(hdc, &rc, bDown ? s_stStartDown : s_stStart,
          bDown ? NSTOPS(s_stStartDown) : NSTOPS(s_stStart), cy / 2 - 2, LC_TR | LC_BR);
    if (!bDown)
        Line(hdc, rc.left, rc.top, rc.right - cy / 2, rc.top + 1, RGB(150, 210, 130));

    DrawSmallImage(hdc, rc.left + 8 + off, rc.top + (cy - 16) / 2 + off, SMI_START);
    hfOld = SelectObject(hdc, StartFont(cy));
    nOldMode = SetBkMode(hdc, TRANSPARENT);
    clrOld = SetTextColor(hdc, RGB(24, 72, 24));
    rc.left += 8 + 16 + 6 + off;
    rc.top += off;
    OffsetRect(&rc, 1, 1);
    DrawText(hdc, "start", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    OffsetRect(&rc, -1, -1);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawText(hdc, "start", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(hdc, clrOld);
    SetBkMode(hdc, nOldMode);
    SelectObject(hdc, hfOld);
}

/* The notification area: light blue, from lprc->left to the right end */
void LunaDrawTray(HDC hdc, const RECT FAR *lprc)
{
    RECT rc = *lprc;

    Shape(hdc, &rc, s_stTray, NSTOPS(s_stTray), 0, 0);
    Line(hdc, rc.left, rc.top, rc.left + 1, rc.bottom, RGB(16, 66, 175));
    Line(hdc, rc.left + 1, rc.top, rc.left + 2, rc.bottom, RGB(96, 196, 255));
}

/*
 * A 32x32 icon at 16x16 over a gradient: the background is enlarged into
 * a scratch bitmap, the icon drawn over it and the result shrunk back, so
 * the gradient shows through the icon's transparent parts.
 */
void LunaIconSmall(HDC hdc, int x, int y, HICON hIcon)
{
    HDC hdcMem;
    HBITMAP hbm, hbmOld;
    int nOldMode;

    if (!hIcon)
        return;
    hdcMem = CreateCompatibleDC(hdc);
    hbm = CreateCompatibleBitmap(hdc, 32, 32);
    if (!hdcMem || !hbm) {
        if (hbm)
            DeleteObject(hbm);
        if (hdcMem)
            DeleteDC(hdcMem);
        DrawIconSmall(hdc, x, y, hIcon, GetPixel(hdc, x, y));
        return;
    }
    hbmOld = SelectObject(hdcMem, hbm);
    nOldMode = SetStretchBltMode(hdcMem, COLORONCOLOR);
    StretchBlt(hdcMem, 0, 0, 32, 32, hdc, x, y, 16, 16, SRCCOPY);
    SetStretchBltMode(hdcMem, nOldMode);
    DrawIcon(hdcMem, 0, 0, hIcon);
    nOldMode = SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(hdc, x, y, 16, 16, hdcMem, 0, 0, 32, 32, SRCCOPY);
    SetStretchBltMode(hdc, nOldMode);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
}

/* Colours of the Start menu: white items, a blue banner */
COLORREF LunaMenuFace(void)
{
    return RGB(255, 255, 255);
}

/* The colour of the banner at y of a menu cy high */
COLORREF LunaBannerColor(int y, int cy)
{
    return StopColor(s_stBanner, NSTOPS(s_stBanner), y, cy);
}

/* A Start menu separator: one grey line */
void LunaDrawSeparator(HDC hdc, const RECT FAR *lprc)
{
    int y = lprc->top + (lprc->bottom - lprc->top) / 2;

    Line(hdc, lprc->left + 2, y, lprc->right - 2, y + 1, RGB(172, 168, 153));
}

/* A rounded rectangle in one colour, its corners blended into what is
   there (radius r) */
void LunaRoundRect(HDC hdc, const RECT FAR *lprc, int r, COLORREF clr)
{
    LSTOP st[1];

    st[0].pos = 0;
    st[0].r = GetRValue(clr);
    st[0].g = GetGValue(clr);
    st[0].b = GetBValue(clr);
    Shape(hdc, lprc, st, 1, r, LC_TL | LC_TR | LC_BL | LC_BR);
}

/* The header (bTop: rounded top corners) and the footer of the two-column
   Start menu: Luna blue, lighter at the top */
static const LSTOP s_stPanelHead[] = {
    {   0, 120, 170, 255 }, {  20,  60, 128, 240 }, { 110,  36, 104, 226 },
    { 255,  20,  84, 205 }
};
static const LSTOP s_stPanelFoot[] = {
    {   0,  70, 136, 236 }, {  40,  40, 108, 226 }, { 255,  24,  80, 196 }
};

void LunaDrawPanelBar(HDC hdc, const RECT FAR *lprc, BOOL bTop)
{
    RECT rc = *lprc;

    if (bTop)
        Shape(hdc, &rc, s_stPanelHead, NSTOPS(s_stPanelHead), 6, LC_TL | LC_TR);
    else
        Shape(hdc, &rc, s_stPanelFoot, NSTOPS(s_stPanelFoot), 0, 0);
}

/* A Quick Launch button under the mouse (bDown: pressed) */
void LunaDrawQuickButton(HDC hdc, const RECT FAR *lprc, BOOL bDown)
{
    LunaDrawTask(hdc, lprc, bDown);
}

/* -------------------------------------------------------------------- */
/* The two-column Start menu                                            */
/* -------------------------------------------------------------------- */

static const LSTOP s_stArrow[] = {
    {   0, 132, 214, 112 }, {  90,  78, 182,  66 }, { 255,  38, 132,  38 }
};
static const LSTOP s_stLogOff[] = {
    {   0, 255, 222, 110 }, { 110, 248, 178,  50 }, { 255, 214, 118,  18 }
};
static const LSTOP s_stPower[] = {
    {   0, 255, 150, 110 }, { 110, 236,  86,  46 }, { 255, 186,  44,  18 }
};

/* A white triangle pointing right, cy high (odd), its tip at x + cy / 2 */
static void Triangle(HDC hdc, int x, int y, int cy, COLORREF clr)
{
    int i, h = cy / 2;

    for (i = 0; i <= h; i++)
        Line(hdc, x + i, y + i, x + i + 1, y + cy - i, clr);
}

/* A rounded square button in a gradient with a white rim */
static void Tile(HDC hdc, const RECT FAR *lprc, const LSTOP *lpst, int nst)
{
    RECT rc = *lprc;

    LunaRoundRect(hdc, &rc, 4, RGB(255, 255, 255));
    InflateRect(&rc, -1, -1);
    Shape(hdc, &rc, lpst, nst, 3, LC_TL | LC_TR | LC_BL | LC_BR);
}

/* The green arrow of All Programs: a rounded square, cx wide */
void LunaDrawArrowButton(HDC hdc, int x, int y, int cx)
{
    RECT rc;
    int cyTri = (cx / 2) | 1;

    SetRect(&rc, x, y, x + cx, y + cx);
    Shape(hdc, &rc, s_stArrow, NSTOPS(s_stArrow), 4, LC_TL | LC_TR | LC_BL | LC_BR);
    /* a lighter rim at the top */
    Line(hdc, x + 3, y, x + cx - 3, y + 1, RGB(178, 234, 160));
    Triangle(hdc, x + (cx - cyTri / 2) / 2 + 1, y + (cx - cyTri) / 2, cyTri,
             RGB(255, 255, 255));
}

/* The icons of Log Off (an arrow out of a door) and Shut Down (the power
   symbol) in the footer, cx x cx */
void LunaDrawFootIcon(HDC hdc, int x, int y, int cx, BOOL bPower)
{
    RECT rc;
    COLORREF clrW = RGB(255, 255, 255);
    int m = x + cx / 2, c = y + cx / 2;

    SetRect(&rc, x, y, x + cx, y + cx);
    if (bPower) {
        HPEN hpen = CreatePen(PS_SOLID, 2, clrW), hpOld;
        HBRUSH hbOld;
        int r = cx * 5 / 16;

        Tile(hdc, &rc, s_stPower, NSTOPS(s_stPower));
        hpOld = SelectObject(hdc, hpen);
        hbOld = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, m - r, c - r + 1, m + r + 1, c + r + 2);
        SelectObject(hdc, hbOld);
        SelectObject(hdc, hpOld);
        DeleteObject(hpen);
        /* the gap at the top of the ring, and the bar through it */
        Line(hdc, m - 2, c - r - 1, m + 3, c - 1, StopColor(s_stPower, NSTOPS(s_stPower),
                                                            cx / 4, cx));
        Line(hdc, m, c - r - 2, m + 2, c + 1, clrW);
    } else {
        int cyTri = (cx / 2) | 1;

        Tile(hdc, &rc, s_stLogOff, NSTOPS(s_stLogOff));
        /* the door: an open frame on the left */
        Line(hdc, x + 4, y + 4, x + 6, y + cx - 4, clrW);
        Line(hdc, x + 4, y + 4, x + cx / 2, y + 6, clrW);
        Line(hdc, x + 4, y + cx - 6, x + cx / 2, y + cx - 4, clrW);
        /* the arrow out of it */
        Line(hdc, x + 8, c - 1, x + cx - 4 - cyTri / 2, c + 1, clrW);
        Triangle(hdc, x + cx - 4 - cyTri / 2, c - cyTri / 2, cyTri, clrW);
    }
}

/* A horizontal line that fades into clrFace towards both ends */
void LunaDrawFadeLine(HDC hdc, int x0, int x1, int y, COLORREF clr, COLORREF clrFace)
{
    int cx = x1 - x0, cxFade = cx / 4, x;

    if (cx <= 0)
        return;
    for (x = 0; x < cx; x += 2) {
        int d = x < cx - x ? x : cx - x, n = cxFade > 0 && d < cxFade ? d * 16 / cxFade : 16;

        Line(hdc, x0 + x, y, x0 + x + 2, y + 1, Blend(clrFace, clr, n));
    }
}
