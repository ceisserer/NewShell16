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
 * cupertino.c - the Platinum and Aqua looks of the taskbar and the Start menu
 *
 * Used while the active Retheme16 theme has [Shell] Style=Platinum
 * (Cupertino Platinum) or Style=Aqua (Cupertino Aqua), or with
 * [Taskbar] Look=4 / 5
 * (luna.c decides). The taskbar keeps its Windows layout (Start button on
 * the left, task buttons, the clock on the right); only the drawing
 * changes:
 *
 *   Platinum   a light grey bar, bevelled grey buttons with a dark outline
 *              (pressed: dark grey with white text), grey menus with a
 *              lavender banner
 *   Aqua       a brushed metal bar, gel buttons (the Start button and the
 *              active task blue, the others clear), pinstriped menus with
 *              a blue gel highlight
 *
 * Drawn procedurally, like the XP look: the brushed metal is made once per
 * bar size into a bitmap the width of the bar. A code segment of its own,
 * loaded only while one of these looks is used.
 */
#include "newshell.h"

#define P_FACE      RGB(221, 221, 221)
#define P_OUTLINE   RGB(34, 34, 34)
#define P_SHADOW    RGB(153, 153, 153)
#define P_HILITE    RGB(51, 51, 153)
#define A_STRIPE_A  RGB(246, 246, 246)
#define A_STRIPE_B  RGB(232, 232, 232)

static const NSSTOP s_stBevel[] = {
    {   0, 255, 255, 255 }, { 110, 236, 236, 236 }, { 255, 190, 190, 190 }
};
static const NSSTOP s_stBevelDown[] = {
    {   0, 102, 102, 102 }, { 255, 150, 150, 150 }
};
/* gel: a light upper half, a sharp edge in the middle, and a body that
   brightens towards the bottom */
static const NSSTOP s_stClear[] = {
    {   0, 255, 255, 255 }, { 112, 236, 236, 236 }, { 128, 214, 214, 214 },
    { 255, 250, 250, 250 }
};
static const NSSTOP s_stBlue[] = {
    {   0, 204, 228, 255 }, { 112,  96, 158, 238 }, { 128,  48, 116, 220 },
    { 255, 156, 214, 255 }
};
static const NSSTOP s_stBlueDown[] = {
    {   0, 150, 180, 230 }, { 112,  52,  98, 190 }, { 128,  24,  70, 170 },
    { 255, 100, 160, 230 }
};
static const NSSTOP s_stMenuSel[] = {
    {   0, 110, 160, 240 }, { 128,  40, 100, 220 }, { 255,  80, 140, 240 }
};
static const NSSTOP s_stBannerP[] = {
    {   0, 170, 170, 221 }, { 255,  51,  51, 153 }
};
static const NSSTOP s_stBannerA[] = {
    {   0, 130, 180, 245 }, { 128,  52, 120, 222 }, { 255,  20,  70, 180 }
};

#define NSTOPS(a)   (sizeof(a) / sizeof((a)[0]))

static HBITMAP s_hbmMetal;
static int     s_cxMetal, s_cyMetal;

static BOOL Aqua(void)
{
    return ShellLook() == LOOK_AQUA;
}

static void Fill(HDC hdc, int l, int t, int r, int b, COLORREF clr)
{
    RECT rc;

    SetRect(&rc, l, t, r, b);
    FillSolid(hdc, &rc, clr);
}

void CupFree(void)
{
    if (s_hbmMetal) {
        DeleteObject(s_hbmMetal);
        s_hbmMetal = NULL;
    }
}

/* -------------------------------------------------------------------- */
/* The bar                                                              */
/* -------------------------------------------------------------------- */

/*
 * Brushed metal: each row is made of streaks of random length, a little
 * lighter or darker than the row's grey, which goes from light at the top
 * to darker at the bottom. A fixed seed, so it looks the same every time.
 */
static void MakeMetal(HDC hdc, int cx, int cy)
{
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmOld;
    unsigned long seed = 12345UL;
    int x, y;

    CupFree();
    s_hbmMetal = CreateCompatibleBitmap(hdc, cx, cy);
    if (!hdcMem || !s_hbmMetal) {
        CupFree();
        if (hdcMem)
            DeleteDC(hdcMem);
        return;
    }
    hbmOld = SelectObject(hdcMem, s_hbmMetal);
    for (y = 0; y < cy; y++) {
        int base = 224 - (cy > 1 ? y * 30 / (cy - 1) : 0);

        seed = seed * 1103515245UL + 12345UL;
        base += (int)((seed >> 16) % 5) - 2;
        for (x = 0; x < cx;) {
            int len, v;

            seed = seed * 1103515245UL + 12345UL;
            len = 6 + (int)((seed >> 16) % 48);
            seed = seed * 1103515245UL + 12345UL;
            v = base + (int)((seed >> 16) % 13) - 6;
            if (v > 255)
                v = 255;
            Fill(hdcMem, x, y, x + len, y + 1, RGB(v, v, v + 2 > 255 ? 255 : v + 2));
            x += len;
        }
    }
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    s_cxMetal = cx;
    s_cyMetal = cy;
}

void CupDrawBar(HDC hdc, const RECT FAR *lprc)
{
    int cx = lprc->right - lprc->left, cy = lprc->bottom - lprc->top;

    if (!Aqua()) {
        /* Platinum: grey, a grey and a white line at the top */
        FillSolid(hdc, lprc, P_FACE);
        Fill(hdc, lprc->left, lprc->top, lprc->right, lprc->top + 1, P_SHADOW);
        Fill(hdc, lprc->left, lprc->top + 1, lprc->right, lprc->top + 2, RGB(255, 255, 255));
        return;
    }
    if (!s_hbmMetal || s_cxMetal < cx || s_cyMetal != cy)
        MakeMetal(hdc, cx, cy);
    if (s_hbmMetal) {
        HDC hdcMem = CreateCompatibleDC(hdc);

        if (hdcMem) {
            HBITMAP hbmOld = SelectObject(hdcMem, s_hbmMetal);

            BitBlt(hdc, lprc->left, lprc->top, cx, cy, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hbmOld);
            DeleteDC(hdcMem);
        }
    } else
        FillSolid(hdc, lprc, RGB(206, 206, 208));
    /* a dark and a light line at the top */
    Fill(hdc, lprc->left, lprc->top, lprc->right, lprc->top + 1, RGB(120, 120, 120));
    Fill(hdc, lprc->left, lprc->top + 1, lprc->right, lprc->top + 2, RGB(244, 244, 244));
}

/* -------------------------------------------------------------------- */
/* Buttons                                                              */
/* -------------------------------------------------------------------- */

/* A gel pill (Aqua): an outline, the gradient inside */
static void Gel(HDC hdc, const RECT FAR *lprc, const NSSTOP *lpst, int nst, COLORREF clrLine)
{
    RECT rc = *lprc;
    int r = (rc.bottom - rc.top) / 2;

    LunaRoundRect(hdc, &rc, r, clrLine);
    InflateRect(&rc, -1, -1);
    LunaGradient(hdc, &rc, lpst, nst, r - 1);
}

/* A Platinum bevel button: a dark outline with rounded corners, a light
   gradient with white at the top (pressed: dark grey) */
static void Bevel(HDC hdc, const RECT FAR *lprc, BOOL bDown)
{
    RECT rc = *lprc;

    LunaRoundRect(hdc, &rc, 3, P_OUTLINE);
    InflateRect(&rc, -1, -1);
    if (bDown)
        LunaGradient(hdc, &rc, s_stBevelDown, NSTOPS(s_stBevelDown), 2);
    else {
        LunaGradient(hdc, &rc, s_stBevel, NSTOPS(s_stBevel), 2);
        Fill(hdc, rc.left + 2, rc.top, rc.right - 2, rc.top + 1, RGB(255, 255, 255));
        Fill(hdc, rc.left + 2, rc.bottom - 1, rc.right - 2, rc.bottom, P_SHADOW);
    }
}

/* A task or Quick Launch button; bDown: the active task, or pressed */
void CupDrawButton(HDC hdc, const RECT FAR *lprc, BOOL bDown)
{
    if (Aqua())
        Gel(hdc, lprc, bDown ? s_stBlue : s_stClear,
            bDown ? NSTOPS(s_stBlue) : NSTOPS(s_stClear),
            bDown ? RGB(40, 72, 150) : RGB(118, 118, 118));
    else
        Bevel(hdc, lprc, bDown);
}

/* The text colour on a button */
COLORREF CupButtonText(BOOL bDown)
{
    return bDown && !Aqua() ? RGB(255, 255, 255) : RGB(0, 0, 0);
}

/* Where a button's content starts: Aqua's round ends need more room */
int CupButtonInset(void)
{
    return Aqua() ? 9 : 5;
}

int CupStartWidth(HDC hdc)
{
    return (Aqua() ? 18 : 10) + 16 + 5 + TextWidth(hdc, "Start") + (Aqua() ? 14 : 8);
}

/* The Start button: Aqua: a blue gel pill; Platinum: a bevel button */
void CupDrawStart(HDC hdc, const RECT FAR *lprc, BOOL bDown)
{
    RECT rc = *lprc;
    int off = bDown ? 1 : 0, x, nOldMode;
    COLORREF clrOld;

    if (Aqua()) {
        Gel(hdc, &rc, bDown ? s_stBlueDown : s_stBlue,
            bDown ? NSTOPS(s_stBlueDown) : NSTOPS(s_stBlue), RGB(36, 64, 140));
        x = rc.left + 12;
    } else {
        Bevel(hdc, &rc, bDown);
        x = rc.left + 6;
    }
    DrawSmallImage(hdc, x + off, rc.top + (rc.bottom - rc.top - 16) / 2 + off, SMI_START);
    nOldMode = SetBkMode(hdc, TRANSPARENT);
    clrOld = SetTextColor(hdc, CupButtonText(bDown));
    rc.left = x + 16 + 5 + off;
    rc.top += off;
    rc.bottom += off;
    DrawText(hdc, "Start", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(hdc, clrOld);
    SetBkMode(hdc, nOldMode);
}

/* The notification area: Platinum: sunken; Aqua: an etched line before
   it, on the metal */
void CupDrawTray(HDC hdc, const RECT FAR *lprc)
{
    if (Aqua()) {
        Fill(hdc, lprc->left, lprc->top + 2, lprc->left + 1, lprc->bottom - 2, RGB(140, 140, 140));
        Fill(hdc, lprc->left + 1, lprc->top + 2, lprc->left + 2, lprc->bottom - 2, RGB(240, 240, 240));
        return;
    }
    FillSolid(hdc, lprc, P_FACE);
    Fill(hdc, lprc->left, lprc->top, lprc->right, lprc->top + 1, P_SHADOW);
    Fill(hdc, lprc->left, lprc->top, lprc->left + 1, lprc->bottom, P_SHADOW);
    Fill(hdc, lprc->left, lprc->bottom - 1, lprc->right, lprc->bottom, RGB(255, 255, 255));
    Fill(hdc, lprc->right - 1, lprc->top, lprc->right, lprc->bottom, RGB(255, 255, 255));
}

/* -------------------------------------------------------------------- */
/* The Start menu                                                       */
/* -------------------------------------------------------------------- */

/* The face of the items, where one colour is needed (icons) */
COLORREF CupMenuFace(void)
{
    return Aqua() ? A_STRIPE_A : P_FACE;
}

/* The background of an item: Aqua: pinstripes, two pixels each, counted
   from the top of the menu (yMenu: lprc->top in the menu) */
void CupFillMenu(HDC hdc, const RECT FAR *lprc, int yMenu)
{
    int y;

    if (!Aqua()) {
        FillSolid(hdc, lprc, P_FACE);
        return;
    }
    for (y = lprc->top; y < lprc->bottom;) {
        int ym = yMenu + (y - lprc->top), n = 2 - (ym & 1);

        if (y + n > lprc->bottom)
            n = lprc->bottom - y;
        Fill(hdc, lprc->left, y, lprc->right, y + n, (ym & 2) ? A_STRIPE_B : A_STRIPE_A);
        y += n;
    }
}

/* The highlight of the selected item: Aqua: blue gel; Platinum: dark blue */
void CupDrawMenuSel(HDC hdc, const RECT FAR *lprc)
{
    if (Aqua())
        LunaGradient(hdc, lprc, s_stMenuSel, NSTOPS(s_stMenuSel), 0);
    else
        FillSolid(hdc, lprc, P_HILITE);
}

/* The banner of the first level: lavender to dark blue, or Aqua blue */
COLORREF CupBannerColor(int y, int cy)
{
    if (Aqua())
        return LunaStopColor(s_stBannerA, NSTOPS(s_stBannerA), y, cy);
    return LunaStopColor(s_stBannerP, NSTOPS(s_stBannerP), y, cy);
}

/* A separator: a grey line (Platinum: with a white one below) */
void CupDrawSeparator(HDC hdc, const RECT FAR *lprc)
{
    int y = lprc->top + (lprc->bottom - lprc->top) / 2;

    if (Aqua())
        Fill(hdc, lprc->left + 1, y, lprc->right - 1, y + 1, RGB(200, 200, 200));
    else {
        Fill(hdc, lprc->left + 1, y - 1, lprc->right - 1, y, P_SHADOW);
        Fill(hdc, lprc->left + 1, y, lprc->right - 1, y + 1, RGB(255, 255, 255));
    }
}
