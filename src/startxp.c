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
 * startxp.c - the two-column Start menu of Windows XP, and the Start menu
 * of Windows 11
 *
 *   +--------------------------------------+
 *   | [icon] user name                     |  header
 *   +-------------------+------------------+
 *   | pinned programs   | the first level  |
 *   | (large icons)     | of NSSTART.INI   |
 *   | ----------------- | (Documents,      |
 *   | programs started  |  Settings, Find, |
 *   | last              |  Run, Help...)   |
 *   | ----------------- |                  |
 *   |  All Programs  >  |                  |
 *   +-------------------+------------------+
 *   |                [Log Off] [Shut Down] |  footer
 *   +--------------------------------------+
 *
 * The right column holds the first level of the Start menu without its
 * Programs submenu and Shut Down; All Programs opens the Programs submenu.
 * The pinned programs are [Pinned] in NSSTART.INI (Start menu format), the
 * programs started last are [Recent] (startmnu.c keeps it).
 *
 *   [Start Menu]
 *   Style=0        ; 0: two columns with the XP taskbar look, 1: always
 *                  ; the classic menu, 2: always two columns
 *   UserName=      ; the name in the header (default: the user logged on)
 *
 * The panel is a popup window with a modal loop of its own, like a menu:
 * it holds the mouse capture (a click outside closes it) and the keyboard
 * focus. Submenus are ordinary owner-drawn popup menus (startmnu.c). It is
 * drawn in the Windows XP colours with the XP look (luna.c), otherwise in
 * the system colours ("classic" Windows XP).
 *
 * With the Windows 11 look (win11.c) the same panel takes the shape of the
 * Windows 11 Start menu, centred above the taskbar:
 *
 *   +--------------------------------------+
 *   | Pinned                  [All apps >] |
 *   | [ic] [ic] [ic] [ic] [ic] [ic]        |  pinned programs, then the
 *   | name name name name name name        |  first level of the menu
 *   | Recommended                          |
 *   | [i] program      [i] program         |  programs started last
 *   +--------------------------------------+
 *   | (user)                          (o)  |  Log Off, Shut Down
 *   +--------------------------------------+
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define XP_LEFT_W       196
#define XP_RIGHT_W      176
#define XP_HEAD_H       60
#define XP_FOOT_H       40
#define XP_ROW_L        40      /* left column: 32x32 icons */
#define XP_ROW_R        24      /* right column: 16x16 icons */
#define XP_SEP_H        9
#define XP_ALL_H        30
#define XP_ARROW        18      /* the green arrow of All Programs (XP look) */
#define XP_FOOT_ICON    22      /* the icons of Log Off and Shut Down (XP look) */
#define XP_PAD          6
#define XP_MAXE         64
#define TIMER_HOVER     1
#define HOVER_MS        450

/* entries */
#define XE_ITEM         0       /* a Start menu item */
#define XE_SEP          1
#define XE_ALL          2       /* All Programs */
#define XE_LOGOFF       3
#define XE_SHUTDOWN     4

#define COL_LEFT        0
#define COL_RIGHT       1
#define COL_FOOT        2
#define COL_TILE        3       /* Windows 11 */
#define COL_REC         4
#define COL_TOP         5

#define W11_TILE_W      66
#define W11_TILE_H      60
#define W11_COLS        6
#define W11_ROWS        3
#define W11_PAD         16
#define W11_REC_H       32
#define W11_FOOT_H      46
#define W11_FACE        RGB(243, 243, 243)
#define W11_FOOT        RGB(236, 237, 240)
#define W11_LINE        RGB(214, 214, 214)

typedef struct tagXPENTRY {
    BYTE bKind;
    BYTE bCol;
    BYTE bBold;
    int  n;                     /* XE_ITEM: the Start menu item */
    RECT rc;
} XPENTRY;

static XPENTRY *s_e;
static int      s_nE;
static HWND     s_hwnd, s_hwndOwner;
static int      s_nHot = -1;
static int      s_nNoOpen = -1; /* its submenu was closed by this press */
static BOOL     s_bDone, s_bInSub, s_bLuna, s_b11;
static int      s_yRecLabel;    /* Windows 11: where "Recommended" goes */
static BOOL     s_bNoRecent;
static UINT     s_uCmd;
static HMENU    s_hmTop, s_hmPinned, s_hmRecent, s_hmAll;
static HBITMAP  s_hbmUnder;     /* the screen under the panel (round corners) */
static int      s_cx, s_cy;
static RECT     s_rcHead, s_rcLeft, s_rcRight, s_rcFoot;
static HFONT    s_hfName;
static char     s_szName[40];

/* -------------------------------------------------------------------- */
/* Colours                                                              */
/* -------------------------------------------------------------------- */

static COLORREF Mix(COLORREF a, COLORREF b, int n)      /* n/16 of b */
{
    return RGB((GetRValue(a) * (16 - n) + GetRValue(b) * n) / 16,
               (GetGValue(a) * (16 - n) + GetGValue(b) * n) / 16,
               (GetBValue(a) * (16 - n) + GetBValue(b) * n) / 16);
}

static COLORREF LeftFace(void)
{
    return s_bLuna ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOW);
}

static COLORREF RightFace(void)
{
    return s_bLuna ? RGB(211, 229, 250) : NsColor(NSC_STARTMENU);
}

static COLORREF SelFace(void)
{
    return s_bLuna ? RGB(49, 106, 197) : NsColor(NSC_SELECTION);
}

static COLORREF TextColor(int nCol, BOOL bSel)
{
    if (bSel)
        return s_bLuna ? RGB(255, 255, 255) : GetSysColor(COLOR_HIGHLIGHTTEXT);
    if (nCol == COL_FOOT)
        return s_bLuna ? RGB(255, 255, 255) : GetSysColor(COLOR_CAPTIONTEXT);
    if (s_bLuna)
        return nCol == COL_RIGHT ? RGB(0, 33, 99) : RGB(0, 0, 0);
    return GetSysColor(nCol == COL_RIGHT ? COLOR_BTNTEXT : COLOR_WINDOWTEXT);
}

/* -------------------------------------------------------------------- */
/* Layout                                                               */
/* -------------------------------------------------------------------- */

static BOOL Selectable(int i);
static void SubArrow(HDC hdc, int x, int y, COLORREF clr);

static int AddEntry(int nKind, int nCol, int n)
{
    XPENTRY *e;

    if (s_nE >= XP_MAXE)
        return -1;
    e = &s_e[s_nE];
    _fmemset(e, 0, sizeof(XPENTRY));
    e->bKind = (BYTE)nKind;
    e->bCol = (BYTE)nCol;
    e->n = n;
    return s_nE++;
}

/* Adds the items of hMenu to a column; returns the height they take */
static int AddItems(HMENU hMenu, int nCol, BOOL bBold, int y, int x0, int cx, BOOL bRight)
{
    int idx[48], i, n, k, cy, y0 = y;
    BOOL bSepDue = FALSE;

    if (!hMenu)
        return 0;
    n = SmItems(hMenu, idx, 48);
    for (i = 0; i < n; i++) {
        UINT f = SmFlags(idx[i]);

        if (f & SF_EMPTY)
            continue;
        if (bRight && (SmIsPrograms(idx[i]) || SmIsMacro(idx[i], "$ShutDown")))
            continue;
        if (f & SF_SEP) {
            bSepDue = y > y0;   /* no separator first or twice */
            continue;
        }
        if (bSepDue && (k = AddEntry(XE_SEP, nCol, -1)) >= 0) {
            SetRect(&s_e[k].rc, x0, y, x0 + cx, y + XP_SEP_H);
            y += XP_SEP_H;
        }
        bSepDue = FALSE;
        if ((k = AddEntry(XE_ITEM, nCol, idx[i])) < 0)
            break;
        cy = bRight ? XP_ROW_R : XP_ROW_L;
        s_e[k].bBold = (BYTE)bBold;
        SetRect(&s_e[k].rc, x0, y, x0 + cx, y + cy);
        y += cy;
    }
    return y - y0;
}

static int FootIcon(void)
{
    return s_bLuna ? XP_FOOT_ICON : 16;
}

static int FootButtonWidth(HDC hdc, LPCSTR lpText)
{
    return 4 + FootIcon() + 6 + TextWidth(hdc, lpText) + 8;
}

static void Layout(void)
{
    int yBody = XP_HEAD_H, cyLeft, cyRight, cyBody, y, k, i, x, cyRecent;
    HDC hdc;
    HFONT hfOld;

    s_nE = 0;
    /* left: pinned, programs started last; All Programs at the bottom */
    y = yBody + XP_PAD;
    y += AddItems(s_hmPinned, COL_LEFT, TRUE, y, 0, XP_LEFT_W, FALSE);
    k = s_nE;
    cyRecent = AddItems(s_hmRecent, COL_LEFT, FALSE, y + XP_SEP_H, 0, XP_LEFT_W, FALSE);
    if (cyRecent > 0) {
        /* a separator between the pinned and the recent programs */
        if (k > 0 && (i = AddEntry(XE_SEP, COL_LEFT, -1)) >= 0)
            SetRect(&s_e[i].rc, 0, y, XP_LEFT_W, y + XP_SEP_H);
        else
            for (i = k; i < s_nE; i++)
                OffsetRect(&s_e[i].rc, 0, -XP_SEP_H);
        y += (k > 0 ? XP_SEP_H : 0) + cyRecent;
    }
    cyLeft = y - yBody + (s_hmAll ? XP_SEP_H + XP_ALL_H : 0) + XP_PAD;

    /* right: the first level of the Start menu */
    cyRight = AddItems(s_hmTop, COL_RIGHT, FALSE, yBody + XP_PAD, XP_LEFT_W + 1,
                       XP_RIGHT_W - 1, TRUE) + 2 * XP_PAD;

    cyBody = cyLeft > cyRight ? cyLeft : cyRight;
    if (cyBody < 160)
        cyBody = 160;
    if (s_hmAll) {
        y = yBody + cyBody - XP_PAD - XP_ALL_H;
        if ((i = AddEntry(XE_SEP, COL_LEFT, -1)) >= 0)
            SetRect(&s_e[i].rc, 0, y - XP_SEP_H, XP_LEFT_W, y);
        if ((i = AddEntry(XE_ALL, COL_LEFT, -1)) >= 0)
            SetRect(&s_e[i].rc, 0, y, XP_LEFT_W, y + XP_ALL_H);
    }

    s_cx = XP_LEFT_W + XP_RIGHT_W;
    s_cy = XP_HEAD_H + cyBody + XP_FOOT_H;
    SetRect(&s_rcHead, 0, 0, s_cx, XP_HEAD_H);
    SetRect(&s_rcLeft, 0, XP_HEAD_H, XP_LEFT_W, XP_HEAD_H + cyBody);
    SetRect(&s_rcRight, XP_LEFT_W, XP_HEAD_H, s_cx, XP_HEAD_H + cyBody);
    SetRect(&s_rcFoot, 0, XP_HEAD_H + cyBody, s_cx, s_cy);

    /* footer: Shut Down on the right, Log Off left of it */
    hdc = GetDC(NULL);
    hfOld = SelectObject(hdc, SmFont(FALSE));
    x = s_cx - 8;
    if ((i = AddEntry(XE_SHUTDOWN, COL_FOOT, -1)) >= 0) {
        SetRect(&s_e[i].rc, x - FootButtonWidth(hdc, "Sh&ut Down"), s_rcFoot.top + 7, x,
                s_rcFoot.bottom - 7);
        x = s_e[i].rc.left - 6;
    }
    if (UsersEnabled() && (i = AddEntry(XE_LOGOFF, COL_FOOT, -1)) >= 0)
        SetRect(&s_e[i].rc, x - FootButtonWidth(hdc, "&Log Off"), s_rcFoot.top + 7, x,
                s_rcFoot.bottom - 7);
    SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);
}

/* -------------------------------------------------------------------- */
/* Painting                                                             */
/* -------------------------------------------------------------------- */

static void HLine(HDC hdc, int x0, int x1, int y, COLORREF clr)
{
    RECT rc;

    SetRect(&rc, x0, y, x1, y + 1);
    FillSolid(hdc, &rc, clr);
}

/* -------------------------------------------------------------------- */
/* Windows 11                                                           */
/* -------------------------------------------------------------------- */

/* Adds the items of hMenu as tiles from *pn on; FALSE when the grid is full */
static BOOL AddTiles(HMENU hMenu, int *pn, int y0, BOOL bTop)
{
    int idx[48], i, n, k;

    if (!hMenu)
        return TRUE;
    n = SmItems(hMenu, idx, 48);
    for (i = 0; i < n; i++) {
        UINT f = SmFlags(idx[i]);

        if (f & (SF_SEP | SF_EMPTY))
            continue;
        if (bTop && (SmIsPrograms(idx[i]) || SmIsMacro(idx[i], "$ShutDown")))
            continue;
        if (*pn >= W11_COLS * W11_ROWS || (k = AddEntry(XE_ITEM, COL_TILE, idx[i])) < 0)
            return FALSE;
        SetRect(&s_e[k].rc, W11_PAD + (*pn % W11_COLS) * W11_TILE_W,
                y0 + (*pn / W11_COLS) * W11_TILE_H,
                W11_PAD + (*pn % W11_COLS + 1) * W11_TILE_W,
                y0 + (*pn / W11_COLS + 1) * W11_TILE_H);
        (*pn)++;
    }
    return TRUE;
}

static void Layout11(void)
{
    int y = W11_PAD, n = 0, i, k, idx[8], nRec, cxRec, rows;
    HDC hdc = GetDC(NULL);
    HFONT hfOld = SelectObject(hdc, SmFont(FALSE));

    s_nE = 0;
    s_cx = 2 * W11_PAD + W11_COLS * W11_TILE_W;
    if (s_hmAll && (k = AddEntry(XE_ALL, COL_TOP, -1)) >= 0)
        SetRect(&s_e[k].rc, s_cx - W11_PAD - TextWidth(hdc, "All apps") - 28, y - 4,
                s_cx - W11_PAD, y + 18);
    y += 26;
    if (AddTiles(s_hmPinned, &n, y, FALSE))
        AddTiles(s_hmTop, &n, y, TRUE);
    rows = (n + W11_COLS - 1) / W11_COLS;
    if (rows < 2)
        rows = 2;
    y += rows * W11_TILE_H + 10;

    s_yRecLabel = y;
    y += 26;
    nRec = s_hmRecent ? SmItems(s_hmRecent, idx, 8) : 0;
    s_bNoRecent = nRec == 0;
    cxRec = (s_cx - 2 * W11_PAD) / 2;
    for (i = 0; i < nRec && i < 6; i++)
        if ((k = AddEntry(XE_ITEM, COL_REC, idx[i])) >= 0)
            SetRect(&s_e[k].rc, W11_PAD + (i % 2) * cxRec, y + (i / 2) * W11_REC_H,
                    W11_PAD + (i % 2 + 1) * cxRec, y + (i / 2 + 1) * W11_REC_H);
    y += (nRec > 4 ? 3 : 2) * W11_REC_H + W11_PAD;

    SetRect(&s_rcFoot, 0, y, s_cx, y + W11_FOOT_H);
    s_cy = y + W11_FOOT_H;
    /* the user (Log Off when there are users), the power button */
    if ((k = AddEntry(XE_SHUTDOWN, COL_FOOT, -1)) >= 0)
        SetRect(&s_e[k].rc, s_cx - W11_PAD - 32, y + 7, s_cx - W11_PAD, y + W11_FOOT_H - 7);
    if (UsersEnabled() && (k = AddEntry(XE_LOGOFF, COL_FOOT, -1)) >= 0) {
        SelectObject(hdc, s_hfName ? SmFont(TRUE) : SmFont(TRUE));
        SetRect(&s_e[k].rc, W11_PAD + 16, y + 7,
                W11_PAD + 16 + 36 + TextWidth(hdc, s_szName) + 12, y + W11_FOOT_H - 7);
    }
    SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);
}

static void Label11(HDC hdc, int x, int y, LPCSTR lpText, BOOL bBold, COLORREF clr)
{
    RECT rc;
    HFONT hfOld = SelectObject(hdc, SmFont(bBold));
    COLORREF clrOld = SetTextColor(hdc, clr);

    SetRect(&rc, x, y, s_cx - W11_PAD, y + 20);
    DrawText(hdc, lpText, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(hdc, clrOld);
    SelectObject(hdc, hfOld);
}

static void PaintEntry11(HDC hdc, int i)
{
    XPENTRY *e = &s_e[i];
    BOOL bSel = (i == s_nHot);
    RECT rc = e->rc, rcText;
    HFONT hfOld;
    char szText[48], szFit[48];

    if (e->bCol == COL_TILE || e->bCol == COL_REC)
        InflateRect(&rc, -2, -2);
    Win11DrawButton(hdc, &rc, bSel, FALSE);
    switch (e->bCol) {
    case COL_TILE:
        SmDrawIcon(hdc, e->n, rc.left + (rc.right - rc.left - 32) / 2, rc.top + 5, TRUE,
                   bSel ? RGB(250, 251, 253) : W11_FACE);
        hfOld = SelectObject(hdc, SmFont(FALSE));
        {
            LPCSTR lp;
            int n = 0;

            /* the name without '&', shortened to the tile */
            for (lp = SmText(e->n); *lp && n < sizeof(szText) - 1; lp++)
                if (*lp != '&' || lp[1] == '&')
                    szText[n++] = *lp;
            szText[n] = '\0';
        }
        FitText(hdc, szText, szFit, sizeof(szFit), rc.right - rc.left - 4);
        SetRect(&rcText, rc.left, rc.top + 39, rc.right, rc.bottom - 2);
        DrawText(hdc, szFit, -1, &rcText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, hfOld);
        break;
    case COL_REC:
        SmDrawIcon(hdc, e->n, rc.left + 8, rc.top + (rc.bottom - rc.top - 16) / 2, FALSE,
                   bSel ? RGB(250, 251, 253) : W11_FACE);
        hfOld = SelectObject(hdc, SmFont(FALSE));
        SetRect(&rcText, rc.left + 32, rc.top, rc.right - 4, rc.bottom);
        DrawText(hdc, SmText(e->n), -1, &rcText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hfOld);
        break;
    case COL_TOP:       /* All apps > */
        if (!bSel) {
            RECT rcB = rc;

            LunaRoundRect(hdc, &rcB, 4, RGB(226, 228, 232));
            InflateRect(&rcB, -1, -1);
            LunaRoundRect(hdc, &rcB, 3, RGB(251, 251, 251));
        }
        hfOld = SelectObject(hdc, SmFont(FALSE));
        SetRect(&rcText, rc.left + 10, rc.top, rc.right, rc.bottom);
        DrawText(hdc, "&All apps", -1, &rcText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hfOld);
        SubArrow(hdc, rc.right - 12, rc.top + (rc.bottom - rc.top - 7) / 2, RGB(0, 0, 0));
        break;
    case COL_FOOT:
        if (e->bKind == XE_SHUTDOWN)
            DrawSmallImage(hdc, rc.left + 8, rc.top + (rc.bottom - rc.top - 16) / 2,
                           SMI_SHUTDOWN);
        break;
    }
}

static void Paint11(HDC hdc)
{
    RECT rc;
    HICON hIcon;
    int i, nOldMode;

    /* the panel: rounded corners over what was under it, a thin border */
    SetRect(&rc, 0, 0, s_cx, s_cy);
    LunaRoundRect(hdc, &rc, 8, RGB(190, 192, 196));
    InflateRect(&rc, -1, -1);
    LunaRoundRect(hdc, &rc, 7, W11_FACE);
    rc = s_rcFoot;
    InflateRect(&rc, -1, 0);
    rc.bottom--;
    LunaRoundRect(hdc, &rc, 7, W11_FOOT);
    rc.bottom = rc.top + 8;
    FillSolid(hdc, &rc, W11_FOOT);
    HLine(hdc, 1, s_cx - 1, s_rcFoot.top, W11_LINE);

    nOldMode = SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    Label11(hdc, W11_PAD + 8, W11_PAD - 4, "Pinned", TRUE, RGB(0, 0, 0));
    Label11(hdc, W11_PAD + 8, s_yRecLabel, "Recommended", TRUE, RGB(0, 0, 0));
    if (s_bNoRecent)
        Label11(hdc, W11_PAD + 8, s_yRecLabel + 30,
                "The programs you start appear here.", FALSE, RGB(96, 96, 96));
    for (i = 0; i < s_nE; i++)
        PaintEntry11(hdc, i);

    /* the user: a round picture and the name */
    hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_NEWSHELL));
    SetRect(&rc, W11_PAD + 22, s_rcFoot.top + 7, W11_PAD + 22 + 32, s_rcFoot.bottom - 7);
    rc.bottom = rc.top + 32;
    LunaRoundRect(hdc, &rc, 16, RGB(255, 255, 255));
    if (hIcon)
        LunaIconSmall(hdc, rc.left + 8, rc.top + 8, hIcon);
    Label11(hdc, rc.right + 10, rc.top + 6, s_szName, FALSE, RGB(0, 0, 0));
    SetBkMode(hdc, nOldMode);
}

/* Windows 11: the arrows go to the nearest entry in that direction */
static int Spatial(int i, int dx, int dy)
{
    int j, best = -1;
    long lBest = 0, cx, cy;

    if (!Selectable(i)) {
        for (j = 0; j < s_nE; j++)
            if (Selectable(j))
                return j;
        return -1;
    }
    cx = (s_e[i].rc.left + s_e[i].rc.right) / 2;
    cy = (s_e[i].rc.top + s_e[i].rc.bottom) / 2;
    for (j = 0; j < s_nE; j++) {
        long ex, ey, along, across;

        if (j == i || !Selectable(j))
            continue;
        ex = (s_e[j].rc.left + s_e[j].rc.right) / 2 - cx;
        ey = (s_e[j].rc.top + s_e[j].rc.bottom) / 2 - cy;
        along = dx ? ex * dx : ey * dy;
        across = dx ? ey : ex;
        if (across < 0)
            across = -across;
        if (along <= 0)
            continue;
        if (best < 0 || along + 3 * across < lBest) {
            best = j;
            lBest = along + 3 * across;
        }
    }
    return best;
}

/* The header and footer without the XP look: the caption colour, lighter
   to the right (as the gradient captions of Windows 98) */
static void ClassicBar(HDC hdc, const RECT FAR *lprc)
{
    COLORREF clr = GetSysColor(COLOR_ACTIVECAPTION);
    int cx = lprc->right - lprc->left, x;
    RECT rc;

    for (x = 0; x < cx; x += 4) {
        SetRect(&rc, lprc->left + x, lprc->top, lprc->left + x + 4, lprc->bottom);
        FillSolid(hdc, &rc, Mix(clr, RGB(255, 255, 255), (int)((long)x * 9 / cx)));
    }
}

static void DrawText1(HDC hdc, LPCSTR lpText, RECT FAR *lprc, COLORREF clr, UINT uFmt)
{
    COLORREF clrOld = SetTextColor(hdc, clr);

    DrawText(hdc, lpText, -1, lprc, uFmt | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(hdc, clrOld);
}

static void PaintHeader(HDC hdc)
{
    HICON hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_NEWSHELL));
    RECT rc;
    HFONT hfOld;
    int y = (XP_HEAD_H - 2 - 46) / 2;

    if (s_bLuna) {
        LunaDrawPanelBar(hdc, &s_rcHead, TRUE);
        /* the orange line under the header */
        HLine(hdc, 0, s_cx, XP_HEAD_H - 2, RGB(232, 144, 64));
        HLine(hdc, 0, s_cx, XP_HEAD_H - 1, RGB(250, 190, 120));
    } else
        ClassicBar(hdc, &s_rcHead);

    /* the "user picture": the NewShell16 icon on a tile, in Windows XP
       with a white rounded frame and a shadow */
    SetRect(&rc, 7, y, 7 + 46, y + 46);
    if (s_bLuna) {
        OffsetRect(&rc, 1, 1);
        LunaRoundRect(hdc, &rc, 5, RGB(18, 62, 160));
        OffsetRect(&rc, -1, -1);
        LunaRoundRect(hdc, &rc, 5, RGB(255, 255, 255));
        InflateRect(&rc, -3, -3);
        LunaRoundRect(hdc, &rc, 2, RGB(224, 236, 252));
    } else {
        FillSolid(hdc, &rc, GetSysColor(COLOR_BTNHIGHLIGHT));
        InflateRect(&rc, -1, -1);
        FillSolid(hdc, &rc, RGB(255, 255, 255));
    }
    if (hIcon)
        DrawIcon(hdc, 7 + 7, y + 7, hIcon);

    SetBkMode(hdc, TRANSPARENT);
    hfOld = SelectObject(hdc, s_hfName ? s_hfName : SmFont(TRUE));
    SetRect(&rc, 62, 0, s_cx - 6, XP_HEAD_H - 2);
    if (s_bLuna) {
        OffsetRect(&rc, 1, 1);
        DrawText1(hdc, s_szName, &rc, RGB(12, 50, 130), DT_LEFT | DT_NOPREFIX);
        OffsetRect(&rc, -1, -1);
    }
    DrawText1(hdc, s_szName, &rc, s_bLuna ? RGB(255, 255, 255) : GetSysColor(COLOR_CAPTIONTEXT),
              DT_LEFT | DT_NOPREFIX);
    SelectObject(hdc, hfOld);
}

/* The triangle of a submenu in the right column */
static void SubArrow(HDC hdc, int x, int y, COLORREF clr)
{
    int i;
    RECT rc;

    for (i = 0; i < 4; i++) {
        SetRect(&rc, x + i, y + i, x + i + 1, y + 7 - i);
        FillSolid(hdc, &rc, clr);
    }
}

static void PaintEntry(HDC hdc, int i)
{
    XPENTRY *e = &s_e[i];
    RECT rc = e->rc, rcText;
    BOOL bSel = (i == s_nHot) && e->bKind != XE_SEP;
    COLORREF clrFace = e->bCol == COL_RIGHT ? RightFace() : LeftFace();
    HFONT hfOld;
    int nOldMode = SetBkMode(hdc, TRANSPARENT);

    if (e->bKind == XE_SEP) {
        int y = rc.top + XP_SEP_H / 2;

        if (s_bLuna && e->bCol == COL_RIGHT)
            HLine(hdc, rc.left + 8, rc.right - 8, y, RGB(149, 189, 236));
        else if (s_bLuna)
            LunaDrawFadeLine(hdc, rc.left + 4, rc.right - 4, y, RGB(208, 208, 200),
                             LeftFace());
        else {
            HLine(hdc, rc.left + 6, rc.right - 6, y - 1, GetSysColor(COLOR_BTNSHADOW));
            HLine(hdc, rc.left + 6, rc.right - 6, y, GetSysColor(COLOR_BTNHIGHLIGHT));
        }
        SetBkMode(hdc, nOldMode);
        return;
    }

    if (e->bCol == COL_FOOT) {
        LPCSTR lpText = e->bKind == XE_LOGOFF ? "&Log Off" : "Sh&ut Down";

        if (bSel) {
            if (s_bLuna)
                LunaDrawQuickButton(hdc, &rc, FALSE);
            else
                Draw3DEdge(hdc, &rc, FALSE, FALSE);
        }
        if (s_bLuna)
            LunaDrawFootIcon(hdc, rc.left + 4, rc.top + (rc.bottom - rc.top - XP_FOOT_ICON) / 2,
                             XP_FOOT_ICON, e->bKind == XE_SHUTDOWN);
        else
            DrawSmallImage(hdc, rc.left + 4, rc.top + (rc.bottom - rc.top - 16) / 2,
                           e->bKind == XE_LOGOFF ? SMI_LOGOFF : SMI_SHUTDOWN);
        hfOld = SelectObject(hdc, SmFont(FALSE));
        SetRect(&rcText, rc.left + 4 + FootIcon() + 6, rc.top, rc.right, rc.bottom);
        DrawText1(hdc, lpText, &rcText, TextColor(COL_FOOT, FALSE), DT_LEFT);
        SelectObject(hdc, hfOld);
        SetBkMode(hdc, nOldMode);
        return;
    }

    InflateRect(&rc, -2, 0);
    if (bSel) {
        FillSolid(hdc, &rc, SelFace());
        clrFace = SelFace();
    }

    if (e->bKind == XE_ALL) {
        static const char szAll[] = "&All Programs";
        int cxArrow = s_bLuna ? XP_ARROW : 16, cx, x;

        hfOld = SelectObject(hdc, SmFont(TRUE));
        cx = TextWidth(hdc, "All Programs") + 8 + cxArrow;
        x = rc.left + (rc.right - rc.left - cx) / 2;
        SetRect(&rcText, x, rc.top, x + cx, rc.bottom);
        DrawText1(hdc, szAll, &rcText, TextColor(COL_LEFT, bSel), DT_LEFT);
        if (s_bLuna)
            LunaDrawArrowButton(hdc, x + cx - cxArrow, rc.top + (rc.bottom - rc.top - cxArrow) / 2,
                                cxArrow);
        else
            DrawSmallImage(hdc, x + cx - 16, rc.top + (rc.bottom - rc.top - 16) / 2,
                           SMI_ALLPROGRAMS);
        SelectObject(hdc, hfOld);
        SetBkMode(hdc, nOldMode);
        return;
    }

    if (e->bCol == COL_LEFT) {
        SmDrawIcon(hdc, e->n, rc.left + 6, rc.top + (rc.bottom - rc.top - 32) / 2, TRUE, clrFace);
        SetRect(&rcText, rc.left + 46, rc.top, rc.right - 4, rc.bottom);
    } else {
        SmDrawIcon(hdc, e->n, rc.left + 6, rc.top + (rc.bottom - rc.top - 16) / 2, FALSE,
                   clrFace);
        SetRect(&rcText, rc.left + 28, rc.top, rc.right - 12, rc.bottom);
        if (SmFlags(e->n) & SF_SUBMENU)
            SubArrow(hdc, rc.right - 10, rc.top + (rc.bottom - rc.top - 7) / 2,
                     TextColor(COL_RIGHT, bSel));
    }
    hfOld = SelectObject(hdc, SmFont(e->bBold));
    DrawText1(hdc, SmText(e->n), &rcText, TextColor(e->bCol, bSel), DT_LEFT);
    SelectObject(hdc, hfOld);
    SetBkMode(hdc, nOldMode);
}

static void Paint(HDC hdc)
{
    RECT rc;
    int i;

    if (s_hbmUnder) {
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmOld = SelectObject(hdcMem, s_hbmUnder);

        BitBlt(hdc, 0, 0, s_cx, s_cy, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteDC(hdcMem);
    }
    if (s_b11) {
        Paint11(hdc);
        return;
    }
    PaintHeader(hdc);
    FillSolid(hdc, &s_rcLeft, LeftFace());
    FillSolid(hdc, &s_rcRight, RightFace());
    if (s_bLuna) {
        SetRect(&rc, s_rcRight.left, s_rcRight.top, s_rcRight.left + 1, s_rcRight.bottom);
        FillSolid(hdc, &rc, RGB(149, 189, 236));
        LunaDrawPanelBar(hdc, &s_rcFoot, FALSE);
    } else {
        SetRect(&rc, s_rcRight.left, s_rcRight.top, s_rcRight.left + 1, s_rcRight.bottom);
        FillSolid(hdc, &rc, GetSysColor(COLOR_BTNSHADOW));
        ClassicBar(hdc, &s_rcFoot);
    }
    for (i = 0; i < s_nE; i++)
        PaintEntry(hdc, i);

    /* the frame */
    if (s_bLuna) {
        COLORREF clr = RGB(0, 48, 160);

        SetRect(&rc, 0, 6, 1, s_cy);
        FillSolid(hdc, &rc, clr);
        SetRect(&rc, s_cx - 1, 6, s_cx, s_cy);
        FillSolid(hdc, &rc, clr);
        HLine(hdc, 0, s_cx, s_cy - 1, clr);
    } else {
        SetRect(&rc, 0, 0, s_cx, s_cy);
        Draw3DEdge(hdc, &rc, FALSE, TRUE);
    }
}

static void PaintAll(HWND hwnd, HDC hdcWin)
{
    HDC hdc = CreateCompatibleDC(hdcWin);
    HBITMAP hbm = CreateCompatibleBitmap(hdcWin, s_cx, s_cy);

    if (!hdc || !hbm) {
        if (hbm)
            DeleteObject(hbm);
        if (hdc)
            DeleteDC(hdc);
        Paint(hdcWin);
        return;
    }
    {
        HBITMAP hbmOld = SelectObject(hdc, hbm);

        Paint(hdc);
        BitBlt(hdcWin, 0, 0, s_cx, s_cy, hdc, 0, 0, SRCCOPY);
        SelectObject(hdc, hbmOld);
    }
    DeleteObject(hbm);
    DeleteDC(hdc);
    (void)hwnd;
}

/* -------------------------------------------------------------------- */
/* Interaction                                                          */
/* -------------------------------------------------------------------- */

static BOOL Selectable(int i)
{
    return i >= 0 && i < s_nE && s_e[i].bKind != XE_SEP;
}

static int HitTest(int x, int y)
{
    POINT pt;
    int i;

    pt.x = x;
    pt.y = y;
    for (i = 0; i < s_nE; i++)
        if (Selectable(i) && PtInRect(&s_e[i].rc, pt))
            return i;
    return -1;
}

static BOOL OpensMenu(int i)
{
    return Selectable(i) && (s_e[i].bKind == XE_ALL
                             || (s_e[i].bKind == XE_ITEM && (SmFlags(s_e[i].n) & SF_SUBMENU)));
}

static void SetHot(int i)
{
    if (i == s_nHot)
        return;
    s_nHot = i;
    InvalidateRect(s_hwnd, NULL, FALSE);
    UpdateWindow(s_hwnd);
    KillTimer(s_hwnd, TIMER_HOVER);
    if (OpensMenu(i) && !s_b11)     /* Windows 11 opens submenus on a click */
        SetTimer(s_hwnd, TIMER_HOVER, HOVER_MS, NULL);
}

/* The first reason to close counts (destroying the window deactivates it) */
static void Close(UINT uCmd)
{
    if (s_bDone)
        return;
    s_uCmd = uCmd;
    s_bDone = TRUE;
}

/* Opens the submenu of entry i to its right */
static void OpenSubmenu(int i)
{
    HMENU hMenu = s_e[i].bKind == XE_ALL ? s_hmAll : SmSubmenu(s_e[i].n);
    POINT pt;
    MSG msg;
    RECT rcWin;
    BOOL bDown;
    int cy;

    KillTimer(s_hwnd, TIMER_HOVER);
    if (!hMenu)
        return;
    SetHot(i);
    KillTimer(s_hwnd, TIMER_HOVER);
    /* beside the entry, but above the taskbar (the bottom of the panel):
       All Programs goes up from there as in Windows XP */
    pt.x = s_e[i].rc.right - 2;
    pt.y = s_e[i].rc.top;
    cy = SmMenuHeight(hMenu) + 2 * GetSystemMetrics(SM_CYBORDER) + 4;
    if (!s_b11 && pt.y > s_cy - cy)
        pt.y = s_cy - cy;
    ClientToScreen(s_hwnd, &pt);
    if (pt.y < 0)
        pt.y = 0;
    if (GetCapture() == s_hwnd)
        ReleaseCapture();
    s_bInSub = TRUE;
    TrackPopupMenu(hMenu, TPM_LEFTALIGN, pt.x, pt.y, 0, s_hwnd, NULL);
    s_bInSub = FALSE;
    if (PeekMessage(&msg, s_hwnd, WM_COMMAND, WM_COMMAND, PM_REMOVE)) {
        Close(msg.wParam);
        return;
    }
    /* closed by a click outside the menu and the panel: close the panel too */
    GetCursorPos(&pt);
    GetWindowRect(s_hwnd, &rcWin);
    bDown = GetAsyncKeyState(VK_LBUTTON) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0;
    if (!PtInRect(&rcWin, pt) && bDown) {
        Close(0);
        return;
    }
    /* closed by a click on its own entry: the release must not open it
       again (a second click closes the submenu, as in a menu bar) */
    ScreenToClient(s_hwnd, &pt);
    s_nNoOpen = bDown && HitTest(pt.x, pt.y) == i ? i : -1;
    SetCapture(s_hwnd);
}

static void Activate(int i)
{
    if (!Selectable(i))
        return;
    if (OpensMenu(i)) {
        OpenSubmenu(i);
        return;
    }
    switch (s_e[i].bKind) {
    case XE_ITEM:
        Close(CMD_SM_FIRST + s_e[i].n);
        break;
    case XE_LOGOFF:
        Close(CMD_LOGOFF);
        break;
    case XE_SHUTDOWN:
        Close(CMD_SHUTDOWN);
        break;
    }
}

/* The next selectable entry from i in direction d (+1/-1) in column nCol */
static int NextInColumn(int i, int d, int nCol)
{
    int j, best = -1, yBest = 0, yFrom = (i >= 0) ? s_e[i].rc.top : (d > 0 ? -1 : 32000);

    for (j = 0; j < s_nE; j++) {
        int y = s_e[j].rc.top;

        if (!Selectable(j) || s_e[j].bCol != nCol)
            continue;
        if (nCol == COL_FOOT)
            y = s_e[j].rc.left;
        if (i >= 0 && nCol == COL_FOOT)
            yFrom = s_e[i].rc.left;
        if ((d > 0 && y > yFrom && (best < 0 || y < yBest))
            || (d < 0 && y < yFrom && (best < 0 || y > yBest))) {
            best = j;
            yBest = y;
        }
    }
    return best;
}

/* The entry of column nCol nearest to entry i in height */
static int NearestInColumn(int i, int nCol)
{
    int j, best = -1, dBest = 0, y = i >= 0 ? s_e[i].rc.top : 0;

    for (j = 0; j < s_nE; j++) {
        int dy;

        if (!Selectable(j) || s_e[j].bCol != nCol)
            continue;
        dy = s_e[j].rc.top - y;
        if (dy < 0)
            dy = -dy;
        if (best < 0 || dy < dBest) {
            best = j;
            dBest = dy;
        }
    }
    return best;
}

static void KeyDown(UINT vk)
{
    int i = s_nHot, nCol = Selectable(i) ? s_e[i].bCol : COL_LEFT, j;

    if (s_b11 && (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN
                  || vk == VK_TAB)) {
        if (vk == VK_RIGHT && OpensMenu(i)) {
            OpenSubmenu(i);
            return;
        }
        j = vk == VK_TAB ? (i + 1) % s_nE
            : Spatial(i, vk == VK_LEFT ? -1 : vk == VK_RIGHT ? 1 : 0,
                      vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : 0);
        while (vk == VK_TAB && !Selectable(j) && j != i)
            j = (j + 1) % s_nE;
        if (j >= 0)
            SetHot(j);
        return;
    }
    switch (vk) {
    case VK_ESCAPE:
        Close(0);
        break;
    case VK_DOWN:
    case VK_UP:
        if (!Selectable(i)) {
            j = NextInColumn(-1, vk == VK_DOWN ? 1 : -1, COL_LEFT);
        } else if (nCol == COL_FOOT) {
            j = vk == VK_UP ? NextInColumn(-1, -1, COL_LEFT) : -1;
        } else {
            j = NextInColumn(i, vk == VK_DOWN ? 1 : -1, nCol);
            if (j < 0 && vk == VK_DOWN)
                j = NextInColumn(-1, -1, COL_FOOT);     /* Shut Down */
        }
        if (j >= 0)
            SetHot(j);
        break;
    case VK_LEFT:
    case VK_RIGHT:
    case VK_TAB:
        if (nCol == COL_FOOT && vk != VK_TAB) {
            j = NextInColumn(i, vk == VK_RIGHT ? 1 : -1, COL_FOOT);
        } else if (vk == VK_RIGHT && nCol == COL_RIGHT && OpensMenu(i)) {
            OpenSubmenu(i);
            return;
        } else if (vk == VK_RIGHT && nCol == COL_LEFT && OpensMenu(i)) {
            OpenSubmenu(i);
            return;
        } else
            j = NearestInColumn(i, nCol == COL_LEFT ? COL_RIGHT : COL_LEFT);
        if (j >= 0)
            SetHot(j);
        break;
    case VK_RETURN:
    case VK_SPACE:
        Activate(i);
        break;
    }
}

/* A letter: the item with that mnemonic (or first letter) */
static void CharKey(UINT ch)
{
    int i, pass;

    ch = (UINT)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)ch);
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < s_nE; i++) {
            LPCSTR lp, lpText;

            if (!Selectable(i))
                continue;
            lpText = s_e[i].bKind == XE_ITEM ? SmText(s_e[i].n)
                     : s_e[i].bKind == XE_ALL ? "&All Programs"
                     : s_e[i].bKind == XE_LOGOFF ? "&Log Off" : "Sh&ut Down";
            if (pass == 0) {
                for (lp = lpText; *lp; lp++)
                    if (lp[0] == '&' && lp[1] && lp[1] != '&')
                        break;
                if (!*lp)
                    continue;
                lp++;
            } else {
                lp = lpText[0] == '&' ? lpText + 1 : lpText;
            }
            if ((UINT)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)*lp) == ch) {
                SetHot(i);
                Activate(i);
                return;
            }
        }
}

LRESULT CALLBACK NSEXPORT StartXPWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    POINT pt;
    int i;

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        PaintAll(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_MOUSEMOVE:
        i = HitTest((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        if (i >= 0 || s_nHot < 0 || !OpensMenu(s_nHot))
            SetHot(i);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);
        if (pt.x < 0 || pt.y < 0 || pt.x >= s_cx || pt.y >= s_cy) {
            Close(0);       /* a click outside, also on the Start button */
            return 0;
        }
        s_nNoOpen = -1;
        SetHot(HitTest(pt.x, pt.y));
        return 0;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        /* runs the item under the mouse or opens its submenu, on the
           release: a submenu opened while the button is down would close
           when it is released. The release of the press that opened the
           panel (on the Start button) is outside and ignored. */
        i = HitTest((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        if (i >= 0 && i == s_nNoOpen)
            i = -1;
        s_nNoOpen = -1;
        if (OpensMenu(i))
            OpenSubmenu(i);
        else if (i >= 0 && msg == WM_LBUTTONUP)
            Activate(i);
        return 0;

    case WM_TIMER:
        KillTimer(hwnd, TIMER_HOVER);
        if (GetAsyncKeyState(VK_LBUTTON) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0) {
            /* not while a button is down (see WM_LBUTTONUP) */
            if (OpensMenu(s_nHot))
                SetTimer(hwnd, TIMER_HOVER, HOVER_MS, NULL);
            return 0;
        }
        if (OpensMenu(s_nHot)) {
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (HitTest(pt.x, pt.y) == s_nHot)
                OpenSubmenu(s_nHot);
        }
        return 0;

    case WM_KEYDOWN:
        KeyDown(wParam);
        return 0;

    case WM_CHAR:
        if (wParam > ' ')
            CharKey(wParam);
        return 0;

    case WM_SYSKEYDOWN:
        if (wParam == VK_MENU || wParam == VK_F10) {
            Close(0);
            return 0;
        }
        break;

    case WM_CANCELMODE:
        if (!s_bInSub)
            Close(0);
        return 0;

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE && !s_bInSub)
            Close(0);
        return 0;

    case WM_MENUSELECT:
        StartMenuSelect(hwnd, wParam, LOWORD(lParam));
        return 0;

    case WM_MENUCHAR:
        return StartMenuChar(wParam, (HMENU)HIWORD(lParam));

    case WM_MEASUREITEM:
        if (StartMenuMeasureItem((MEASUREITEMSTRUCT FAR *)lParam))
            return TRUE;
        break;

    case WM_DRAWITEM:
        if (StartMenuDrawItem((DRAWITEMSTRUCT FAR *)lParam))
            return TRUE;
        break;

    case WM_COMMAND:
        Close(wParam);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* -------------------------------------------------------------------- */
/* Showing                                                              */
/* -------------------------------------------------------------------- */

static BOOL Register(void)
{
    static BOOL s_bDone;
    WNDCLASS wc;

    if (s_bDone)
        return TRUE;
    _fmemset(&wc, 0, sizeof(wc));
    wc.style = CS_SAVEBITS;
    wc.lpfnWndProc = StartXPWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLS_STARTXP;
    s_bDone = RegisterClass(&wc);
    return s_bDone;
}

static void MakeNameFont(void)
{
    LOGFONT lf;

    if (s_hfName)
        return;
    _fmemset(&lf, 0, sizeof(lf));
    lf.lfHeight = -17;
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, "Arial");
    s_hfName = CreateFontIndirect(&lf);
}

/* Shows the menu with its bottom-left corner at (x, y) and returns when
   it is closed; the command chosen is posted to hwndOwner */
void StartXPShow(HWND hwndOwner, int x, int y, BOOL b11)
{
    HWND hwndPrev = GetActiveWindow();
    int i, idx[32], n;
    MSG msg;
    HDC hdcScreen;

    if (!Register() || !SmBegin())
        return;
    if (!s_e && (s_e = (XPENTRY *)malloc(XP_MAXE * sizeof(XPENTRY))) == NULL)
        return;
    s_bLuna = LunaLook();
    s_b11 = b11;
    s_hwndOwner = hwndOwner;
    MakeNameFont();
    CfgGetStr("Start Menu", "UserName", "", s_szName, sizeof(s_szName));
    if (!s_szName[0])
        lstrcpyn(s_szName, g_szUser[0] ? (LPSTR)g_szUser : (LPSTR)NS_APPNAME, sizeof(s_szName));

    s_hmTop = SmBuild("Start", TRUE);
    s_hmPinned = SmBuildPinned();
    s_hmRecent = SmBuildRecent();
    s_hmAll = NULL;
    n = SmItems(s_hmTop, idx, 32);
    for (i = 0; i < n && !s_hmAll; i++)
        if (SmIsPrograms(idx[i]))
            s_hmAll = SmSubmenu(idx[i]);
    if (s_b11) {
        /* centred above the taskbar, with a gap */
        Layout11();
        x = (GetSystemMetrics(SM_CXSCREEN) - s_cx) / 2;
        y -= 10;
    } else
        Layout();

    y -= s_cy;
    if (x + s_cx > GetSystemMetrics(SM_CXSCREEN))
        x = GetSystemMetrics(SM_CXSCREEN) - s_cx;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;

    /* what is under the rounded corners */
    s_hbmUnder = NULL;
    if ((s_bLuna || s_b11) && (hdcScreen = GetDC(NULL)) != NULL) {
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        s_hbmUnder = CreateCompatibleBitmap(hdcScreen, s_cx, s_cy);
        if (hdcMem && s_hbmUnder) {
            HBITMAP hbmOld = SelectObject(hdcMem, s_hbmUnder);

            BitBlt(hdcMem, 0, 0, s_cx, s_cy, hdcScreen, x, y, SRCCOPY);
            SelectObject(hdcMem, hbmOld);
        }
        if (hdcMem)
            DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
    }

    s_nHot = -1;
    s_nNoOpen = -1;
    s_bDone = FALSE;
    s_bInSub = FALSE;
    s_uCmd = 0;
    s_hwnd = CreateWindowEx(WS_EX_TOPMOST, CLS_STARTXP, "", WS_POPUP, x, y, s_cx, s_cy,
                            hwndOwner, NULL, g_hInst, NULL);
    if (s_hwnd) {
        ShowWindow(s_hwnd, SW_SHOW);
        UpdateWindow(s_hwnd);
        SetFocus(s_hwnd);
        SetCapture(s_hwnd);

        while (!s_bDone && IsWindow(s_hwnd)) {
            if (!GetMessage(&msg, NULL, 0, 0)) {
                PostQuitMessage(msg.wParam);
                break;
            }
            /* the keyboard belongs to the menu */
            if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST)
                msg.hwnd = s_hwnd;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (!s_bDone && !s_bInSub && GetCapture() != s_hwnd)
                SetCapture(s_hwnd);
        }
        if (GetCapture() == s_hwnd)
            ReleaseCapture();
        KillTimer(s_hwnd, TIMER_HOVER);
        DestroyWindow(s_hwnd);
    }
    s_hwnd = NULL;
    if (hwndPrev && IsWindow(hwndPrev) && IsWindowVisible(hwndPrev))
        SetActiveWindow(hwndPrev);
    if (s_hbmUnder) {
        DeleteObject(s_hbmUnder);
        s_hbmUnder = NULL;
    }
    if (s_uCmd)
        PostMessage(hwndOwner, WM_COMMAND, s_uCmd, 0L);
    /* the items stay: the command chosen refers to them */
    DestroyMenu(s_hmTop);
    DestroyMenu(s_hmPinned);
    DestroyMenu(s_hmRecent);
    s_hmTop = s_hmPinned = s_hmRecent = s_hmAll = NULL;
}
