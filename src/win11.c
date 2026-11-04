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
 * win11.c - the Windows 11 look of the taskbar
 *
 * Used while the active Retheme16 theme has [Shell] Style=11 (the
 * Redmond-11 theme), or with [Taskbar] Look=3 (luna.c decides). The bar is
 * light, the Start button (the four blue panes), Quick Launch and the task
 * buttons are icons centred on it; the task buttons have a short line under
 * them (a longer blue one for the active task), and the clock shows the
 * time over the date. Drawn procedurally like the XP look: no bitmaps.
 */
#include <dos.h>
#include "newshell.h"

#define BAR         RGB(238, 242, 248)
#define BAR_LINE    RGB(214, 218, 224)
#define HOT         RGB(250, 251, 253)
#define HOT_LINE    RGB(226, 228, 232)
#define DOWN        RGB(232, 235, 240)
#define ACCENT      RGB(0, 95, 184)
#define RUNNING     RGB(138, 138, 138)

COLORREF Win11BarColor(void)
{
    return BAR;
}

static void Fill(HDC hdc, int l, int t, int r, int b, COLORREF clr)
{
    RECT rc;

    SetRect(&rc, l, t, r, b);
    FillSolid(hdc, &rc, clr);
}

/* The bar: light, with a thin line at the top */
void Win11DrawBar(HDC hdc, const RECT FAR *lprc)
{
    FillSolid(hdc, lprc, BAR);
    Fill(hdc, lprc->left, lprc->top, lprc->right, lprc->top + 1, BAR_LINE);
}

/* The rounded light square behind a button under the mouse, pressed, or
   of the active task */
void Win11DrawButton(HDC hdc, const RECT FAR *lprc, BOOL bHot, BOOL bDown)
{
    RECT rc = *lprc;

    if (!bHot && !bDown)
        return;
    LunaRoundRect(hdc, &rc, 4, HOT_LINE);
    InflateRect(&rc, -1, -1);
    LunaRoundRect(hdc, &rc, 3, bDown ? DOWN : HOT);
}

/* The Start button: four blue panes, lighter at the top left */
void Win11DrawStart(HDC hdc, const RECT FAR *lprc, BOOL bHot, BOOL bDown)
{
    int cx = 16, x, y, h = 7;

    Win11DrawButton(hdc, lprc, bHot, bDown);
    x = lprc->left + (lprc->right - lprc->left - cx) / 2;
    y = lprc->top + (lprc->bottom - lprc->top - cx) / 2 + (bDown ? 1 : 0);
    Fill(hdc, x, y, x + h, y + h, RGB(40, 150, 240));
    Fill(hdc, x + h + 2, y, x + cx, y + h, RGB(24, 132, 230));
    Fill(hdc, x, y + h + 2, x + h, y + cx, RGB(16, 118, 220));
    Fill(hdc, x + h + 2, y + h + 2, x + cx, y + cx, RGB(0, 102, 204));
}

/* The line under a task button: short and grey, long and blue if active */
void Win11DrawIndicator(HDC hdc, const RECT FAR *lprc, BOOL bActive)
{
    int cx = bActive ? 14 : 6;
    int x = lprc->left + (lprc->right - lprc->left - cx) / 2;
    RECT rc;

    SetRect(&rc, x, lprc->bottom - 3, x + cx, lprc->bottom - 1);
    LunaRoundRect(hdc, &rc, 1, bActive ? ACCENT : RUNNING);
}

/* The clock: the time over the date, or the time alone in a low bar */
void Win11DrawClock(HDC hdc, const RECT FAR *lprc, LPCSTR lpTime)
{
    char szDate[16], szSep[4];
    struct dosdate_t d;
    RECT rc = *lprc;
    int nOldMode = SetBkMode(hdc, TRANSPARENT), cyLine;
    COLORREF clrOld = SetTextColor(hdc, RGB(0, 0, 0));
    TEXTMETRIC tm;

    GetTextMetrics(hdc, &tm);
    cyLine = tm.tmHeight;
    if (rc.bottom - rc.top < 2 * cyLine + 2) {
        DrawText(hdc, lpTime, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        _dos_getdate(&d);
        GetProfileString("intl", "sDate", "/", szSep, sizeof(szSep));
        switch (GetProfileInt("intl", "iDate", 0)) {
        case 1:
            wsprintf(szDate, "%d%s%d%s%d", d.day, (LPSTR)szSep, d.month, (LPSTR)szSep, d.year);
            break;
        case 2:
            wsprintf(szDate, "%d%s%d%s%d", d.year, (LPSTR)szSep, d.month, (LPSTR)szSep, d.day);
            break;
        default:
            wsprintf(szDate, "%d%s%d%s%d", d.month, (LPSTR)szSep, d.day, (LPSTR)szSep, d.year);
        }
        rc.top += (rc.bottom - rc.top - 2 * cyLine) / 2;
        rc.bottom = rc.top + cyLine;
        DrawText(hdc, lpTime, -1, &rc, DT_RIGHT | DT_SINGLELINE);
        OffsetRect(&rc, 0, cyLine);
        DrawText(hdc, szDate, -1, &rc, DT_RIGHT | DT_SINGLELINE);
    }
    SetTextColor(hdc, clrOld);
    SetBkMode(hdc, nOldMode);
}
