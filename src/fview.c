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
 * fview.c - the folder view control
 *
 * A child window showing the contents of a directory (or the drives of
 * the computer when the path is ""), in large icon, small icon, list or
 * details view; the details view is a table with resizable columns and
 * the descriptions of DESCRIPT.ION. It handles selection, keyboard navigation, renaming, the item
 * context menu, clipboard commands and drag and drop. Folder windows and
 * the File System window embed it; they get notified via WM_NS_FVNOTIFY.
 */
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "newshell.h"

#define MAX_VIEWS       64
#define MARGIN          4
#define LARGE_CX        80
#define LARGE_CY        58
#define SMALL_CY        18
#define DRAG_THRESHOLD  4
#define ID_RENAME       100

#define LINE_CX         16          /* horizontal scroll step (pixels) */
#define SIZE_ZONE       3           /* header: distance to a column edge */
#define COL_MIN_CX      16
#define DESC_MAX        60          /* stored length of a description */

/* details view columns (one set of widths for all views, NEWSHELL.INI
   [Folders] Columns=) */
#define DC_NAME         0
#define DC_SIZE         1
#define DC_TYPE         2
#define DC_DATE         3
#define DC_TIME         4
#define DC_ATTR         5
#define DC_DESC         6
#define NCOLS           7

static const char *s_szColName[NCOLS] = {
    "Name", "Size", "Type", "Date", "Time", "Attributes", "Description"
};
static int s_cxCol[NCOLS] = { 140, 70, 80, 72, 64, 64, 180 };
static UINT s_uHiddenCols;      /* bit n: column n is switched off */
static BOOL s_bColsLoaded;

/* The width of a details column, 0 when it is switched off */
static int ColW(int i)
{
    return (s_uHiddenCols & (1 << i)) ? 0 : s_cxCol[i];
}

typedef struct tagFITEM {
    char  szName[16];       /* file name, or "C:" for drives */
    BYTE  bKind;            /* FK_* */
    BYTE  bAttr;
    BYTE  bSel;
    BYTE  bDriveType;
    DWORD dwSize;
    WORD  wDate, wTime;
    WORD  wDesc;            /* offset + 1 in the description pool, 0 = none */
    BYTE  bAlias;           /* an .ALS file: drawn with the shortcut arrow */
    HICON hIcon;
} FITEM, FAR *LPFITEM;

typedef struct tagFVIEW {
    char    szPath[MAXPATHLEN];     /* "" = Computer */
    LPFITEM lpItems;
    int     nItems;
    int     nCap;
    int     nFocus;
    int     nAnchor;
    int     nTop;                   /* first visible row */
    int     nMode;                  /* FV_* */
    int     nSort;                  /* FS_* */
    int     nCols;
    int     nVisRows;
    int     cxCell, cyCell;
    int     cyHeader;               /* details view column header */
    int     cyText;
    int     xScroll;                /* details: horizontal offset (pixels) */
    int     nLeft;                  /* list: first visible column */
    int     nVisCols;               /* list: visible columns */
    LPSTR   lpDesc;                 /* descriptions (DESCRIPT.ION) */
    WORD    cbDesc, cbDescCap;
    int     nSizing;                /* column being resized, -1 = none */
    int     xSizeDown, cxSizeDown;
    DWORD   dwTotal;
    char    szFilter[64];           /* the files shown ("*.*") */
    BOOL    bHidden;                /* hidden and system files too */
    /* mouse */
    int     nButton;                /* the button held: 1 left, 2 right */
    BOOL    bRubber;                /* rubber-band selection */
    POINT   ptRubber;
    RECT    rcRubber;
    int     nHint;                  /* item with a hint (or waiting), -1 */
    BOOL    bPressed;
    BOOL    bDragging;
    BOOL    bDeferSelect;
    POINT   ptDown;
    int     nDownItem;
    /* inline rename */
    HWND    hwndEdit;
    int     nRenaming;
} FVIEW, FAR *LPFVIEW;

static HWND    s_hwndViews[MAX_VIEWS];
static BOOL    s_bContextMenu;      /* our context menu is being tracked */
static int     s_nViews;
static FARPROC s_lpfnEditProc;
static WNDPROC s_lpfnOldEdit;

/* internal clipboard for Cut/Copy/Paste */
static HGLOBAL s_hClip;
static BOOL    s_bClipCut;

static LPFVIEW GetView(HWND hwnd)
{
    return (LPFVIEW)GetWindowLong(hwnd, 0);
}

/* The name as shown: "Calc.exe" when names are capitalised */
static LPCSTR ShownName(LPFITEM p)
{
    static char szName[16];

    if (!g_cfg.bCapitalize || p->bKind == FK_DRIVE)
        return p->szName;
    lstrcpy(szName, p->szName);
    AnsiLower(szName + 1);
    return szName;
}

static void Notify(HWND hwnd, UINT uCode, LPARAM lParam)
{
    SendMessage(GetParent(hwnd), WM_NS_FVNOTIFY, uCode, lParam);
}

/* -------------------------------------------------------------------- */
/* Reading                                                              */
/* -------------------------------------------------------------------- */

static void LoadColumns(void)
{
    char szValue[64];
    LPSTR p;
    int i, n;

    s_bColsLoaded = TRUE;
    s_uHiddenCols = (UINT)CfgGetInt("Folders", "HiddenColumns", 0) & ~1;
    CfgGetStr("Folders", "Columns", "", szValue, sizeof(szValue));
    for (p = szValue, i = 0; *p && i < NCOLS; i++) {
        n = atoi(p);
        if (n >= COL_MIN_CX && n < 1000)
            s_cxCol[i] = n;
        while (*p && *p != ',')
            p++;
        if (*p == ',')
            p++;
    }
}

static void SaveColumns(void)
{
    char szValue[64];
    int i, n = 0;

    for (i = 0; i < NCOLS; i++)
        n += wsprintf(szValue + n, i ? ",%d" : "%d", s_cxCol[i]);
    CfgSetStr("Folders", "Columns", szValue);
}

static int TableWidth(void)
{
    int i, cx = 0;

    for (i = 0; i < NCOLS; i++)
        cx += ColW(i);
    return cx;
}

static LPFITEM AddItem(LPFVIEW v)
{
    if (v->nItems >= v->nCap) {
        int nCap = v->nCap ? v->nCap * 2 : 64;
        LPFITEM lpNew;

        if ((long)nCap * sizeof(FITEM) > 65000L)
            return NULL;
        lpNew = (LPFITEM)realloc(v->lpItems, nCap * sizeof(FITEM));
        if (!lpNew)
            return NULL;
        v->lpItems = lpNew;
        v->nCap = nCap;
    }
    _fmemset(&v->lpItems[v->nItems], 0, sizeof(FITEM));
    return &v->lpItems[v->nItems++];
}

static int s_nSortMode;

static int CompareItems(const void *a, const void *b)
{
    LPFITEM p = (LPFITEM)a, q = (LPFITEM)b;
    int n;

    if (p->bKind != q->bKind)
        return (int)q->bKind - (int)p->bKind;   /* drives, dirs, files */
    switch (s_nSortMode) {
    case FS_TYPE:
        n = lstrcmpi(PathExtension(p->szName), PathExtension(q->szName));
        if (n)
            return n;
        break;
    case FS_SIZE:
        if (p->dwSize != q->dwSize)
            return p->dwSize < q->dwSize ? 1 : -1;
        break;
    case FS_DATE:
        if (p->wDate != q->wDate)
            return p->wDate < q->wDate ? 1 : -1;
        if (p->wTime != q->wTime)
            return p->wTime < q->wTime ? 1 : -1;
        break;
    }
    return lstrcmpi(p->szName, q->szName);
}

static void SortItems(LPFVIEW v)
{
    s_nSortMode = v->nSort;
    if (v->nItems > 1)
        qsort(v->lpItems, v->nItems, sizeof(FITEM), CompareItems);
}

static void ReadDrives(LPFVIEW v)
{
    int d, nType, idi;
    LPFITEM p;

    for (d = 0; d < 26; d++) {
        nType = GetDriveType(d);
        if (nType == 0)
            continue;
        p = AddItem(v);
        if (!p)
            break;
        idi = DriveIcon(d, &p->bDriveType);
        wsprintf(p->szName, "%c:", 'A' + d);
        p->bKind = FK_DRIVE;
        p->hIcon = IconStock(idi);
    }
}

static void ReadDirectory(LPFVIEW v)
{
    struct find_t ft;
    char szPattern[MAXPATHLEN + 4];
    char szFull[MAXPATHLEN + 14];
    unsigned uAttr = _A_NORMAL | _A_RDONLY | _A_SUBDIR | _A_ARCH;
    UINT uOldMode;
    LPFITEM p;

    BOOL bAll = lstrcmp(v->szFilter, "*.*") == 0 || !v->szFilter[0];

    if (v->bHidden)
        uAttr |= _A_HIDDEN | _A_SYSTEM;

    lstrcpy(szPattern, v->szPath);
    PathAppend(szPattern, "*.*");
    uOldMode = SetErrorMode(1);
    if (_dos_findfirst(szPattern, uAttr, &ft) == 0) {
        do {
            if (ft.name[0] == '.')
                continue;
            if (!v->bHidden && (ft.attrib & (_A_HIDDEN | _A_SYSTEM)))
                continue;
            if (!bAll && !(ft.attrib & _A_SUBDIR) && !MatchSpecList(ft.name, v->szFilter))
                continue;
            p = AddItem(v);
            if (!p)
                break;
            lstrcpyn(p->szName, ft.name, sizeof(p->szName));
            p->bAttr = ft.attrib;
            p->dwSize = ft.size;
            p->wDate = ft.wr_date;
            p->wTime = ft.wr_time;
            p->bKind = (ft.attrib & _A_SUBDIR) ? FK_DIR : FK_FILE;
            if (p->bKind == FK_FILE)
                v->dwTotal += ft.size;
        } while (_dos_findnext(&ft) == 0);
    }
    SetErrorMode(uOldMode);

    /* icons are looked up after reading so FindExecutable cannot upset the
       DOS find state */
    for (p = v->lpItems; p < v->lpItems + v->nItems; p++) {
        lstrcpy(szFull, v->szPath);
        PathAppend(szFull, p->szName);
        if (p->bKind == FK_FILE && IsAliasFile(p->szName)) {
            p->bAlias = TRUE;
            p->hIcon = AliasIcon(szFull);
        } else
            p->hIcon = IconForFile(szFull, p->bKind == FK_DIR);
    }
}

/* Adds a description to the pool; returns its offset + 1, 0 if full */
static WORD AddDesc(LPFVIEW v, LPCSTR lpText, int cb)
{
    WORD wOff;

    if (cb > DESC_MAX)
        cb = DESC_MAX;
    if (v->cbDesc + cb + 1 > v->cbDescCap) {
        WORD cbCap = v->cbDescCap ? v->cbDescCap * 2 : 512;
        LPSTR lpNew;

        if (cbCap > 32000 || v->cbDesc + cb + 1 > cbCap)
            return 0;
        lpNew = (LPSTR)realloc(v->lpDesc, cbCap);
        if (!lpNew)
            return 0;
        v->lpDesc = lpNew;
        v->cbDescCap = cbCap;
    }
    wOff = v->cbDesc;
    _fmemcpy(v->lpDesc + wOff, lpText, cb);
    v->lpDesc[wOff + cb] = '\0';
    v->cbDesc += cb + 1;
    return wOff + 1;
}

/*
 * DESCRIPT.ION (4DOS, Calmira): one line per file, its name, blanks and
 * the description. Only the first 16 KB are read.
 */
static void ReadDescriptions(LPFVIEW v)
{
    char szFile[MAXPATHLEN + 14], szName[16];
    HFILE hf;
    LPSTR lpBuf, p, q;
    LPFITEM it;
    UINT uOldMode;
    int cb, i, n;

    lstrcpy(szFile, v->szPath);
    PathAppend(szFile, "DESCRIPT.ION");
    uOldMode = SetErrorMode(1);
    hf = _lopen(szFile, OF_READ | OF_SHARE_DENY_NONE);
    SetErrorMode(uOldMode);
    if (hf == HFILE_ERROR)
        return;
    lpBuf = (LPSTR)malloc(16384 + 1);
    cb = lpBuf ? _lread(hf, lpBuf, 16384) : 0;
    _lclose(hf);
    if (!lpBuf)
        return;
    if (cb < 0)
        cb = 0;
    lpBuf[cb] = '\0';
    for (p = lpBuf; *p; p = q) {
        for (q = p; *q && *q != '\n' && *q != '\r'; q++)
            ;
        /* p..q is one line */
        for (n = 0; p < q && *p != ' ' && *p != '\t' && n < 15; p++)
            szName[n++] = *p;
        szName[n] = '\0';
        while (p < q && (*p == ' ' || *p == '\t'))
            p++;
        if (n && p < q)
            for (i = 0, it = v->lpItems; i < v->nItems; i++, it++)
                if (!it->wDesc && lstrcmpi(it->szName, szName) == 0) {
                    it->wDesc = AddDesc(v, p, (int)(q - p));
                    break;
                }
        while (*q == '\n' || *q == '\r')
            q++;
    }
    free(lpBuf);
}

static void ReadFolder(LPFVIEW v)
{
    HCURSOR hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

    v->nItems = 0;
    v->dwTotal = 0;
    v->cbDesc = 0;
    if (v->szPath[0] == '\0') {
        ReadDrives(v);
    } else {
        ReadDirectory(v);
        ReadDescriptions(v);
    }
    SortItems(v);
    v->nFocus = v->nItems > 0 ? 0 : -1;
    v->nAnchor = v->nFocus;
    SetCursor(hcurOld);
}

/* -------------------------------------------------------------------- */
/* Layout                                                               */
/* -------------------------------------------------------------------- */

/* Width of a small icon cell: the longest name, within limits */
static int SmallCellWidth(HWND hwnd, LPFVIEW v)
{
    HDC hdc = GetDC(hwnd);
    HFONT hfOld = SelectObject(hdc, g_hfontUI);
    int i, cx, cxCell = 80;

    for (i = 0; i < v->nItems; i++) {
        cx = TextWidth(hdc, ShownName(&v->lpItems[i])) + 30;
        if (cx > cxCell)
            cxCell = cx;
    }
    SelectObject(hdc, hfOld);
    ReleaseDC(hwnd, hdc);
    return cxCell > 160 ? 160 : cxCell;
}

static void UpdateLayout(HWND hwnd, LPFVIEW v)
{
    RECT rc;
    int nRows, nMax, nMaxX = 0, cxTable;

    if (!s_bColsLoaded)
        LoadColumns();
    GetClientRect(hwnd, &rc);
    v->cyHeader = 0;
    switch (v->nMode) {
    case FV_LARGE:
        v->cxCell = LARGE_CX;
        v->cyCell = LARGE_CY;
        break;
    case FV_SMALL:
    case FV_LIST:
        v->cxCell = SmallCellWidth(hwnd, v);
        v->cyCell = SMALL_CY;
        break;
    default:
        cxTable = TableWidth();
        v->cxCell = rc.right - 2 * MARGIN;
        if (v->cxCell < cxTable)
            v->cxCell = cxTable;
        v->cyCell = SMALL_CY;
        v->cyHeader = v->cyText + 6;
        nMaxX = cxTable + 2 * MARGIN - rc.right;
        break;
    }
    if (nMaxX < 0)
        nMaxX = 0;
    if (v->xScroll > nMaxX)
        v->xScroll = nMaxX;
    if (v->nMode != FV_DETAILS)
        v->xScroll = 0;

    v->nVisRows = (rc.bottom - v->cyHeader - MARGIN) / v->cyCell;
    if (v->nVisRows < 1)
        v->nVisRows = 1;

    if (v->nMode == FV_LIST) {
        /* columns of nVisRows items, scrolled sideways */
        v->nCols = 1;
        v->nVisCols = (rc.right - 2 * MARGIN) / v->cxCell;
        if (v->nVisCols < 1)
            v->nVisCols = 1;
        nMax = (v->nItems + v->nVisRows - 1) / v->nVisRows - v->nVisCols;
        if (nMax < 0)
            nMax = 0;
        if (v->nLeft > nMax)
            v->nLeft = nMax;
        v->nTop = 0;
        SetScrollRange(hwnd, SB_VERT, 0, 0, FALSE);
        SetScrollRange(hwnd, SB_HORZ, 0, nMax, FALSE);
        SetScrollPos(hwnd, SB_HORZ, v->nLeft, TRUE);
        return;
    }
    v->nLeft = 0;
    v->nCols = (v->nMode == FV_DETAILS) ? 1 : (rc.right - 2 * MARGIN) / v->cxCell;
    if (v->nCols < 1)
        v->nCols = 1;
    nRows = (v->nItems + v->nCols - 1) / v->nCols;
    nMax = nRows - v->nVisRows;
    if (nMax < 0)
        nMax = 0;
    if (v->nTop > nMax)
        v->nTop = nMax;
    SetScrollRange(hwnd, SB_HORZ, 0, nMaxX, FALSE);
    SetScrollPos(hwnd, SB_HORZ, v->xScroll, TRUE);
    SetScrollRange(hwnd, SB_VERT, 0, nMax, FALSE);
    SetScrollPos(hwnd, SB_VERT, v->nTop, TRUE);
}

static void CellRect(LPFVIEW v, int i, RECT FAR *lprc)
{
    int x, y;

    if (v->nMode == FV_LIST) {
        x = MARGIN + (i / v->nVisRows - v->nLeft) * v->cxCell;
        y = MARGIN + (i % v->nVisRows) * v->cyCell;
    } else {
        x = MARGIN + (i % v->nCols) * v->cxCell - v->xScroll;
        y = MARGIN + v->cyHeader + (i / v->nCols - v->nTop) * v->cyCell;
    }
    SetRect(lprc, x, y, x + v->cxCell, y + v->cyCell);
}

/* The part of an item that reacts to the mouse */
static void HotRect(HWND hwnd, LPFVIEW v, int i, RECT FAR *lprc)
{
    CellRect(v, i, lprc);
    if (v->nMode == FV_LARGE) {
        InflateRect(lprc, -6, 0);
    } else {
        HDC hdc = GetDC(hwnd);
        HFONT hfOld = SelectObject(hdc, g_hfontUI);
        int cx = 22 + TextWidth(hdc, ShownName(&v->lpItems[i])) + 4;

        SelectObject(hdc, hfOld);
        ReleaseDC(hwnd, hdc);
        if (v->nMode == FV_DETAILS && cx > s_cxCol[DC_NAME])
            cx = s_cxCol[DC_NAME];
        if (lprc->left + cx < lprc->right)
            lprc->right = lprc->left + cx;
    }
}

static int HitTest(HWND hwnd, LPFVIEW v, int x, int y)
{
    int col, row, i;
    RECT rc;
    POINT pt;

    if (x < MARGIN || y < MARGIN + v->cyHeader)
        return -1;
    if (v->nMode == FV_LIST) {
        col = (x - MARGIN) / v->cxCell + v->nLeft;
        row = (y - MARGIN) / v->cyCell;
        if (row >= v->nVisRows)
            return -1;
        i = col * v->nVisRows + row;
    } else {
        col = (x - MARGIN + v->xScroll) / v->cxCell;
        row = (y - MARGIN - v->cyHeader) / v->cyCell + v->nTop;
        if (col >= v->nCols)
            return -1;
        i = row * v->nCols + col;
    }
    if (i >= v->nItems)
        return -1;
    HotRect(hwnd, v, i, &rc);
    pt.x = x;
    pt.y = y;
    return PtInRect(&rc, pt) ? i : -1;
}

/* -------------------------------------------------------------------- */
/* Painting                                                             */
/* -------------------------------------------------------------------- */

/* Date and time as set in Control Panel (WIN.INI [intl]) */
static void FormatDate(WORD wDate, LPSTR lpBuf)
{
    char szSep[4];
    UINT d = wDate & 31, m = (wDate >> 5) & 15, y = (wDate >> 9) + 1980;

    GetProfileString("intl", "sDate", "/", szSep, sizeof(szSep));
    switch (GetProfileInt("intl", "iDate", 0)) {
    case 1:  wsprintf(lpBuf, "%02u%s%02u%s%04u", d, (LPSTR)szSep, m, (LPSTR)szSep, y); break;
    case 2:  wsprintf(lpBuf, "%04u%s%02u%s%02u", y, (LPSTR)szSep, m, (LPSTR)szSep, d); break;
    default: wsprintf(lpBuf, "%02u%s%02u%s%04u", m, (LPSTR)szSep, d, (LPSTR)szSep, y); break;
    }
}

static void FormatTime(WORD wTime, LPSTR lpBuf)
{
    char szSep[4], szAmPm[8];
    UINT h = wTime >> 11, mi = (wTime >> 5) & 63;

    GetProfileString("intl", "sTime", ":", szSep, sizeof(szSep));
    if (GetProfileInt("intl", "iTime", 0)) {
        wsprintf(lpBuf, "%02u%s%02u", h, (LPSTR)szSep, mi);
        return;
    }
    GetProfileString("intl", h < 12 ? "s1159" : "s2359", h < 12 ? "AM" : "PM",
                     szAmPm, sizeof(szAmPm));
    wsprintf(lpBuf, "%u%s%02u %s", h % 12 ? h % 12 : 12, (LPSTR)szSep, mi, (LPSTR)szAmPm);
}

static void TypeName(LPFITEM p, LPSTR lpBuf)
{
    static const char *s_szDrive[] = { "Drive", "Floppy disk", "Hard disk",
                                       "Network drive", "CD-ROM" };
    char szName[16];
    LPSTR lpExt;

    if (p->bKind == FK_DRIVE) {
        lstrcpy(lpBuf, s_szDrive[p->bDriveType < 5 ? p->bDriveType : 0]);
        return;
    }
    if (p->bKind == FK_DIR) {
        lstrcpy(lpBuf, "Folder");
        return;
    }
    lstrcpy(szName, p->szName);
    lpExt = PathExtension(szName);
    if (*lpExt)
        wsprintf(lpBuf, "%s File", lpExt);
    else
        lstrcpy(lpBuf, "File");
}

static void DrawLabel(HWND hwnd, HDC hdc, LPFVIEW v, int i, RECT FAR *lprc,
                      LPCSTR lpText, UINT uFormat)
{
    LPFITEM p = &v->lpItems[i];

    if (p->bSel) {
        FillSolid(hdc, lprc, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    } else
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    DrawText(hdc, lpText, -1, lprc, uFormat | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
    if (i == v->nFocus && GetFocus() == hwnd)
        DrawItemFocus(hdc, lprc, p->bSel);
}

static void PaintItem(HWND hwnd, HDC hdc, LPFVIEW v, int i)
{
    RECT rc, rcText;
    LPFITEM p = &v->lpItems[i];
    char szLabel[48];
    int cx, x;

    CellRect(v, i, &rc);

    if (v->nMode == FV_LARGE) {
        x = rc.left + (v->cxCell - 32) / 2;
        DrawIcon(hdc, x, rc.top + 2, p->hIcon);
        if (p->bAlias && g_cfg.bShortcutArrows)
            DrawShortcutArrow(hdc, x, rc.top + 2 + 21);
        if (p->bSel && g_cfg.bShadeSelected)
            ShadeArea(hdc, x, rc.top + 2, 32, 32, GetSysColor(COLOR_WINDOW));
        FitText(hdc, ShownName(p), szLabel, sizeof(szLabel), v->cxCell - 6);
        cx = TextWidth(hdc, szLabel);
        rcText.left = rc.left + (v->cxCell - cx) / 2 - 2;
        rcText.right = rcText.left + cx + 4;
        rcText.top = rc.top + 37;
        rcText.bottom = rcText.top + v->cyText + 2;
        if (i == v->nRenaming)
            return;
        DrawLabel(hwnd, hdc, v, i, &rcText, szLabel, DT_CENTER);
        return;
    }

    DrawIconSmall(hdc, rc.left + 2, rc.top + 1, p->hIcon,
                  GetSysColor(COLOR_WINDOW));
    if (p->bAlias && g_cfg.bShortcutArrows)
        DrawShortcutArrowSmall(hdc, rc.left + 2, rc.top + 1 + 9);
    if (p->bSel && g_cfg.bShadeSelected)
        ShadeArea(hdc, rc.left + 2, rc.top + 1, 16, 16, GetSysColor(COLOR_WINDOW));
    x = rc.left + 22;
    cx = (v->nMode == FV_DETAILS ? s_cxCol[DC_NAME] : v->cxCell) - 26;
    FitText(hdc, ShownName(p), szLabel, sizeof(szLabel), cx);
    SetRect(&rcText, x, rc.top + 1, x + TextWidth(hdc, szLabel) + 4,
            rc.bottom - 1);
    if (i != v->nRenaming) {
        rcText.left -= 2;
        DrawLabel(hwnd, hdc, v, i, &rcText, szLabel, DT_CENTER);
    }

    if (v->nMode == FV_DETAILS) {
        int c;

        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        x = rc.left + s_cxCol[DC_NAME];
        for (c = DC_SIZE; c < NCOLS; x += ColW(c), c++) {
            UINT uAlign = DT_LEFT;

            szLabel[0] = '\0';
            switch (c) {
            case DC_SIZE:
                if (p->bKind == FK_FILE)
                    FormatBytes(p->dwSize, szLabel);
                uAlign = DT_RIGHT;
                break;
            case DC_TYPE:
                TypeName(p, szLabel);
                break;
            case DC_DATE:
                if (p->bKind != FK_DRIVE)
                    FormatDate(p->wDate, szLabel);
                break;
            case DC_TIME:
                if (p->bKind != FK_DRIVE)
                    FormatTime(p->wTime, szLabel);
                break;
            case DC_ATTR:
                if (p->bKind != FK_DRIVE)
                    wsprintf(szLabel, "%s%s%s%s",
                             (LPSTR)((p->bAttr & _A_RDONLY) ? "R" : ""),
                             (LPSTR)((p->bAttr & _A_HIDDEN) ? "H" : ""),
                             (LPSTR)((p->bAttr & _A_SYSTEM) ? "S" : ""),
                             (LPSTR)((p->bAttr & _A_ARCH) ? "A" : ""));
                break;
            }
            SetRect(&rcText, x + 2, rc.top, x + ColW(c) - 6, rc.bottom);
            if (!ColW(c) || rcText.right <= rcText.left)
                continue;
            if (c == DC_DESC) {
                if (p->wDesc)
                    DrawText(hdc, v->lpDesc + p->wDesc - 1, -1, &rcText,
                             DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            } else if (szLabel[0]) {
                DrawText(hdc, szLabel, -1, &rcText,
                         uAlign | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            }
        }
    }
}

static void PaintHeader(HWND hwnd, HDC hdc, LPFVIEW v)
{
    RECT rcClient, rc;
    int i, x = -v->xScroll, cx;

    GetClientRect(hwnd, &rcClient);
    for (i = 0; i <= NCOLS && x < rcClient.right; i++, x += cx) {
        /* the last box fills the rest of the line */
        cx = i < NCOLS ? ColW(i) : rcClient.right - x;
        if (i < NCOLS && cx == 0)
            continue;
        if (i == 0)
            cx += MARGIN;
        SetRect(&rc, x, 0, x + cx, v->cyHeader);
        if (rc.right <= 0)
            continue;
        FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
        Draw3DEdge(hdc, &rc, FALSE, FALSE);
        if (i == NCOLS)
            break;
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        InflateRect(&rc, -4, 0);
        if (i == DC_SIZE)
            rc.right -= 4;
        DrawText(hdc, s_szColName[i], -1, &rc,
                 (i == DC_SIZE ? DT_RIGHT : DT_LEFT) | DT_SINGLELINE | DT_VCENTER);
    }
}

static void PaintView(HWND hwnd, HDC hdc, LPFVIEW v, const RECT FAR *lprcPaint)
{
    HFONT hfOld = SelectObject(hdc, g_hfontUI);
    RECT rc, rcClip;
    int i, iFirst, iLast;

    SetBkMode(hdc, TRANSPARENT);
    if (v->nMode == FV_DETAILS) {
        PaintHeader(hwnd, hdc, v);
        GetClientRect(hwnd, &rc);
        IntersectClipRect(hdc, 0, v->cyHeader, rc.right, rc.bottom);
    }
    if (v->nMode == FV_LIST) {
        iFirst = v->nLeft * v->nVisRows;
        iLast = iFirst + (v->nVisCols + 1) * v->nVisRows;
    } else {
        iFirst = v->nTop * v->nCols;
        iLast = iFirst + (v->nVisRows + 1) * v->nCols;
    }
    if (iLast > v->nItems)
        iLast = v->nItems;
    for (i = iFirst; i < iLast; i++) {
        CellRect(v, i, &rc);
        if (IntersectRect(&rcClip, &rc, lprcPaint))
            PaintItem(hwnd, hdc, v, i);
    }
    if (v->nItems == 0 && v->szPath[0]) {
        GetClientRect(hwnd, &rc);
        rc.top = v->cyHeader + 8;
        SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
        DrawText(hdc, "This folder is empty.", -1, &rc, DT_CENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, hfOld);
}

static void InvalidateItem(HWND hwnd, LPFVIEW v, int i)
{
    RECT rc;

    if (i < 0 || i >= v->nItems)
        return;
    CellRect(v, i, &rc);
    InvalidateRect(hwnd, &rc, TRUE);
}

/* -------------------------------------------------------------------- */
/* Selection and scrolling                                              */
/* -------------------------------------------------------------------- */

static void ScrollTo(HWND hwnd, LPFVIEW v, int nTop)
{
    int nMin, nMax;

    GetScrollRange(hwnd, SB_VERT, &nMin, &nMax);
    if (nTop > nMax)
        nTop = nMax;
    if (nTop < 0)
        nTop = 0;
    if (nTop == v->nTop)
        return;
    v->nTop = nTop;
    SetScrollPos(hwnd, SB_VERT, nTop, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

/* Horizontal scrolling: list view by columns, details view by pixels */
static void ScrollToX(HWND hwnd, LPFVIEW v, int nPos)
{
    int nMin, nMax;

    GetScrollRange(hwnd, SB_HORZ, &nMin, &nMax);
    if (nPos > nMax)
        nPos = nMax;
    if (nPos < 0)
        nPos = 0;
    if (v->nMode == FV_LIST) {
        if (nPos == v->nLeft)
            return;
        v->nLeft = nPos;
    } else {
        if (nPos == v->xScroll)
            return;
        v->xScroll = nPos;
    }
    SetScrollPos(hwnd, SB_HORZ, nPos, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void EnsureVisible(HWND hwnd, LPFVIEW v, int i)
{
    int nRow;

    if (i < 0)
        return;
    if (v->nMode == FV_LIST) {
        int nCol = i / v->nVisRows;

        if (nCol < v->nLeft)
            ScrollToX(hwnd, v, nCol);
        else if (nCol >= v->nLeft + v->nVisCols)
            ScrollToX(hwnd, v, nCol - v->nVisCols + 1);
        return;
    }
    nRow = i / v->nCols;
    if (nRow < v->nTop)
        ScrollTo(hwnd, v, nRow);
    else if (nRow >= v->nTop + v->nVisRows)
        ScrollTo(hwnd, v, nRow - v->nVisRows + 1);
}

static void SetFocusItem(HWND hwnd, LPFVIEW v, int i)
{
    int nOld = v->nFocus;

    v->nFocus = i;
    InvalidateItem(hwnd, v, nOld);
    InvalidateItem(hwnd, v, i);
    EnsureVisible(hwnd, v, i);
}

static void SelectNone(HWND hwnd, LPFVIEW v)
{
    int i;

    for (i = 0; i < v->nItems; i++)
        if (v->lpItems[i].bSel) {
            v->lpItems[i].bSel = FALSE;
            InvalidateItem(hwnd, v, i);
        }
}

static void SelectItem(HWND hwnd, LPFVIEW v, int i, BOOL bSel)
{
    if (i < 0 || i >= v->nItems || v->lpItems[i].bSel == (BYTE)bSel)
        return;
    v->lpItems[i].bSel = (BYTE)bSel;
    InvalidateItem(hwnd, v, i);
}

static void SelectRange(HWND hwnd, LPFVIEW v, int a, int b)
{
    int i;

    if (a > b) {
        i = a;
        a = b;
        b = i;
    }
    for (i = 0; i < v->nItems; i++)
        SelectItem(hwnd, v, i, i >= a && i <= b);
}

/* Moves the focus as a result of a key or click, honouring Shift/Ctrl */
static void MoveFocusTo(HWND hwnd, LPFVIEW v, int i, BOOL bShift, BOOL bCtrl)
{
    if (v->nItems == 0)
        return;
    if (i < 0)
        i = 0;
    if (i >= v->nItems)
        i = v->nItems - 1;
    if (bShift && v->nAnchor >= 0) {
        SelectRange(hwnd, v, v->nAnchor, i);
    } else if (!bCtrl) {
        SelectNone(hwnd, v);
        SelectItem(hwnd, v, i, TRUE);
        v->nAnchor = i;
    }
    SetFocusItem(hwnd, v, i);
    Notify(hwnd, FVN_SELCHANGE, 0L);
}

int FViewSelCount(HWND hwnd)
{
    LPFVIEW v = GetView(hwnd);
    int i, n = 0;

    if (!v)
        return 0;
    for (i = 0; i < v->nItems; i++)
        if (v->lpItems[i].bSel)
            n++;
    return n;
}

static void ItemPath(LPFVIEW v, int i, LPSTR lpBuf)
{
    LPFITEM p = &v->lpItems[i];

    if (p->bKind == FK_DRIVE) {
        wsprintf(lpBuf, "%s\\", (LPSTR)p->szName);
        return;
    }
    lstrcpy(lpBuf, v->szPath);
    PathAppend(lpBuf, p->szName);
}

/* Returns the selected items as a path list (NULL if none) */
HGLOBAL FViewGetSelection(HWND hwnd)
{
    LPFVIEW v = GetView(hwnd);
    char szPath[MAXPATHLEN];
    HGLOBAL hList = NULL;
    int i;

    if (!v)
        return NULL;
    for (i = 0; i < v->nItems; i++)
        if (v->lpItems[i].bSel) {
            ItemPath(v, i, szPath);
            PathListAdd(&hList, szPath);
        }
    return hList;
}

/* -------------------------------------------------------------------- */
/* Status text                                                          */
/* -------------------------------------------------------------------- */

void FViewGetStatus(HWND hwnd, LPSTR lpBuf, int cbBuf)
{
    LPFVIEW v = GetView(hwnd);
    char szText[96], szSize[24];
    DWORD dwSel = 0;
    int i, nSel = 0;

    if (!v) {
        lpBuf[0] = '\0';
        return;
    }
    for (i = 0; i < v->nItems; i++)
        if (v->lpItems[i].bSel) {
            nSel++;
            dwSel += v->lpItems[i].dwSize;
        }
    /* totals, a tab, the selection (two status bar fields) */
    if (v->szPath[0] == '\0') {
        wsprintf(szText, "%d drive(s)\tSelected %d item(s)", v->nItems, nSel);
    } else {
        int n;

        FormatBytes(v->dwTotal, szSize);
        n = wsprintf(szText, "%d object(s)  %s\t", v->nItems, (LPSTR)szSize);
        FormatBytes(dwSel, szSize);
        wsprintf(szText + n, "Selected %d item(s)  %s", nSel, (LPSTR)szSize);
    }
    lstrcpyn(lpBuf, szText, cbBuf);
}

/* -------------------------------------------------------------------- */
/* Opening and commands                                                 */
/* -------------------------------------------------------------------- */

static BOOL DriveReady(int nDrive)
{
    struct find_t ft;
    char szPattern[8];
    UINT uOldMode = SetErrorMode(1);    /* SEM_FAILCRITICALERRORS */
    unsigned rc;

    wsprintf(szPattern, "%c:\\*.*", 'A' + nDrive);
    rc = _dos_findfirst(szPattern, _A_NORMAL | _A_SUBDIR | _A_VOLID, &ft);
    SetErrorMode(uOldMode);
    return rc == 0 || rc == 0x12;       /* 0x12 = no more files (empty) */
}

static void OpenItem(HWND hwnd, LPFVIEW v, int i, BOOL bExplore)
{
    char szPath[MAXPATHLEN];
    char szMsg[80];
    LPFITEM p;

    if (i < 0 || i >= v->nItems)
        return;
    p = &v->lpItems[i];
    ItemPath(v, i, szPath);
    if (p->bKind == FK_DRIVE && !DriveReady(p->szName[0] - 'A')) {
        wsprintf(szMsg, "Drive %s is not ready.", (LPSTR)p->szName);
        MessageBox(hwnd, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (p->bKind == FK_FILE) {
        if (p->bAlias)
            AliasOpen(hwnd, szPath);
        else
            OpenPath(hwnd, szPath);
        return;
    }
    if (bExplore)
        ExplorerOpen(szPath);
    else
        Notify(hwnd, FVN_OPENFOLDER, (LPARAM)(LPSTR)szPath);
}

static void OpenSelection(HWND hwnd, LPFVIEW v)
{
    int i, n = 0;

    for (i = 0; i < v->nItems; i++)
        if (v->lpItems[i].bSel && n++ < 16)
            OpenItem(hwnd, v, i, FALSE);
    if (n == 0 && v->nFocus >= 0)
        OpenItem(hwnd, v, v->nFocus, FALSE);
}

static void DoDelete(HWND hwnd, LPFVIEW v, BOOL bPermanent)
{
    HGLOBAL hList = FViewGetSelection(hwnd);

    if (!hList || v->szPath[0] == '\0') {
        if (hList)
            GlobalFree(hList);
        return;
    }
    FileOpDelete(hwnd, hList, g_cfg.bDeleteToBin && !bPermanent);
    GlobalFree(hList);
}

static void DoCutCopy(HWND hwnd, BOOL bCut)
{
    HGLOBAL hList = FViewGetSelection(hwnd);

    if (!hList)
        return;
    if (s_hClip)
        GlobalFree(s_hClip);
    s_hClip = hList;
    s_bClipCut = bCut;
}

static void DoPaste(HWND hwnd, LPFVIEW v)
{
    if (!s_hClip || v->szPath[0] == '\0')
        return;
    FileOpCopyMove(hwnd, s_hClip, v->szPath, s_bClipCut);
    if (s_bClipCut) {
        GlobalFree(s_hClip);
        s_hClip = NULL;
    }
}

static void DoNewFolder(HWND hwnd, LPFVIEW v)
{
    char szName[16];
    int i;

    if (v->szPath[0] == '\0')
        return;
    if (!FileOpNewFolder(hwnd, v->szPath, szName))
        return;
    FViewRefresh(hwnd);
    for (i = 0; i < v->nItems; i++)
        if (lstrcmpi(v->lpItems[i].szName, szName) == 0) {
            MoveFocusTo(hwnd, v, i, FALSE, FALSE);
            FViewCommand(hwnd, CMD_F_RENAME);
            break;
        }
}

static void DoProperties(HWND hwnd, LPFVIEW v)
{
    HGLOBAL hList = FViewGetSelection(hwnd);

    if (!hList) {
        if (v->szPath[0])
            PathListAdd(&hList, v->szPath);
        else
            return;
    }
    PropertiesDialog(hwnd, hList);
    GlobalFree(hList);
}

/* -------------------------------------------------------------------- */
/* Inline rename                                                        */
/* -------------------------------------------------------------------- */

static void EndRename(HWND hwnd, LPFVIEW v, BOOL bCommit)
{
    char szOld[MAXPATHLEN], szNew[16];
    HWND hwndEdit = v->hwndEdit;
    int i = v->nRenaming;

    if (!hwndEdit)
        return;
    v->hwndEdit = NULL;
    v->nRenaming = -1;
    GetWindowText(hwndEdit, szNew, sizeof(szNew));
    DestroyWindow(hwndEdit);
    InvalidateItem(hwnd, v, i);
    if (!bCommit || i < 0 || i >= v->nItems)
        return;
    if (lstrcmpi(szNew, v->lpItems[i].szName) == 0 || szNew[0] == '\0')
        return;
    ItemPath(v, i, szOld);
    if (FileOpRename(hwnd, szOld, szNew))
        FViewRefreshPath(v->szPath);
}

LRESULT CALLBACK NSEXPORT RenameEditProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam)
{
    HWND hwndView = GetParent(hwnd);

    if (msg == WM_GETDLGCODE)
        return DLGC_WANTALLKEYS;
    if (msg == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        EndRename(hwndView, GetView(hwndView), wParam == VK_RETURN);
        SetFocus(hwndView);
        return 0;
    }
    if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
        return 0;
    if (msg == WM_KILLFOCUS) {
        LRESULT lr = CallWindowProc(s_lpfnOldEdit, hwnd, msg, wParam, lParam);
        PostMessage(hwndView, WM_NS_ENDRENAME, 0, 0L);
        return lr;
    }
    return CallWindowProc(s_lpfnOldEdit, hwnd, msg, wParam, lParam);
}

static void BeginRename(HWND hwnd, LPFVIEW v)
{
    RECT rc;
    int i = v->nFocus;

    if (i < 0 || v->lpItems[i].bKind == FK_DRIVE || v->hwndEdit)
        return;
    EnsureVisible(hwnd, v, i);
    CellRect(v, i, &rc);
    if (v->nMode == FV_LARGE) {
        rc.top += 36;
        rc.bottom = rc.top + v->cyText + 4;
    } else {
        rc.left += 20;
        rc.right = rc.left + 100;
    }
    v->nRenaming = i;
    v->hwndEdit = CreateWindow("EDIT", v->lpItems[i].szName,
                               WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_UPPERCASE,
                               rc.left, rc.top, rc.right - rc.left,
                               rc.bottom - rc.top, hwnd, (HMENU)ID_RENAME,
                               g_hInst, NULL);
    if (!v->hwndEdit) {
        v->nRenaming = -1;
        return;
    }
    if (!s_lpfnEditProc)
        s_lpfnEditProc = MakeProcInstance((FARPROC)RenameEditProc, g_hInst);
    s_lpfnOldEdit = (WNDPROC)SetWindowLong(v->hwndEdit, GWL_WNDPROC,
                                           (LONG)s_lpfnEditProc);
    SendMessage(v->hwndEdit, WM_SETFONT, (WPARAM)g_hfontUI, 0L);
    SendMessage(v->hwndEdit, EM_LIMITTEXT, 12, 0L);
    SendMessage(v->hwndEdit, EM_SETSEL, 0, MAKELONG(0, -1));
    ShowWindow(v->hwndEdit, SW_SHOW);
    SetFocus(v->hwndEdit);
    InvalidateItem(hwnd, v, i);
}

/* -------------------------------------------------------------------- */
/* Drag and drop                                                        */
/* -------------------------------------------------------------------- */

/*
 * Works out what is under a screen point during a drag: a folder (path in
 * lpDir), the Bin, or nothing. Folder views, the File System tree and desktop
 * icons can be targets.
 */
int DropTargetAt(POINT pt, LPSTR lpDir)
{
    HWND hwnd = WindowFromPoint(pt);
    char szClass[32];
    LPFVIEW v;
    int i;

    lpDir[0] = '\0';
    if (!hwnd)
        return DT_NONE;
    GetClassName(hwnd, szClass, sizeof(szClass));
    if (lstrcmpi(szClass, CLS_FVIEW) == 0) {
        v = GetView(hwnd);
        if (!v)
            return DT_NONE;
        ScreenToClient(hwnd, &pt);
        i = HitTest(hwnd, v, pt.x, pt.y);
        if (i >= 0 && v->lpItems[i].bKind != FK_FILE) {
            ItemPath(v, i, lpDir);
            return DT_FOLDER;
        }
        if (i >= 0 && !v->lpItems[i].bSel && IsProgramFile(v->lpItems[i].szName)) {
            ItemPath(v, i, lpDir);
            return DT_PROGRAM;
        }
        if (v->szPath[0] == '\0')
            return DT_NONE;
        lstrcpy(lpDir, v->szPath);
        return DT_FOLDER;
    }
    if (lstrcmpi(szClass, CLS_DESKICON) == 0)
        return DesktopDropTarget(hwnd, lpDir);
    if (hwnd == GetDesktopWindow())
        return g_cfg.bDesktopIcons ? DT_DESKTOP : DT_NONE;
    return ExplorerDropTarget(hwnd, pt, lpDir);
}

static HWND TopLevelOf(HWND hwnd)
{
    HWND hwndParent;

    while ((GetWindowLong(hwnd, GWL_STYLE) & WS_CHILD)
           && (hwndParent = GetParent(hwnd)) != NULL)
        hwnd = hwndParent;
    return hwnd;
}

static HCURSOR DragCursor(int nTarget, BOOL bCopy)
{
    if (nTarget == DT_NONE)
        return LoadCursor(g_hInst, MAKEINTRESOURCE(IDC_NODROP));
    if (nTarget == DT_BIN)
        return LoadCursor(g_hInst, MAKEINTRESOURCE(IDC_DRAGMOVE));
    if (nTarget == DT_DESKTOP || nTarget == DT_PROGRAM)
        return LoadCursor(g_hInst, MAKEINTRESOURCE(IDC_DRAGCOPY));
    return LoadCursor(g_hInst, MAKEINTRESOURCE(bCopy ? IDC_DRAGCOPY : IDC_DRAGMOVE));
}

/* Copy or move? Ctrl forces copy, Shift forces move, else by drive */
static BOOL IsCopyDrop(LPFVIEW v, LPCSTR lpDir)
{
    if (GetKeyState(VK_CONTROL) & 0x8000)
        return TRUE;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        return FALSE;
    if (v->szPath[0] == '\0' || g_cfg.bDragCopy)
        return TRUE;
    return (v->szPath[0] & ~0x20) != (lpDir[0] & ~0x20);
}

/* what a drop does */
#define DA_NONE         0
#define DA_MOVE         0x7400
#define DA_COPY         0x7401
#define DA_ALIAS        0x7402
#define DA_SHORTCUT     0x7403
#define DA_OPEN         0x7404
#define DA_DELETE       0x7405

/*
 * The menu after a drag with the right mouse button (or with Ask on
 * drop): returns the DA_* chosen. Windows 3.1 has no TPM_RETURNCMD, so
 * the WM_COMMAND the menu posts is taken from the queue.
 */
static int DropMenu(HWND hwnd, LPFVIEW v, int nTarget, LPCSTR lpDir, int nDefault,
                    POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    HWND hwndOwner = TopLevelOf(hwnd);
    BOOL bSame = nTarget == DT_FOLDER && lstrcmpi(lpDir, v->szPath) == 0;
    UINT uOff = (bSame || !v->szPath[0]) ? MF_GRAYED : 0;
    MSG msg;
    int nChoice = DA_NONE;

    switch (nTarget) {
    case DT_FOLDER:
        AppendMenu(hMenu, MF_STRING | uOff, DA_MOVE,
                   nDefault == DA_MOVE ? "&Move Here (default)" : "&Move Here");
        AppendMenu(hMenu, MF_STRING | (bSame ? MF_GRAYED : 0), DA_COPY,
                   nDefault == DA_COPY ? "&Copy Here (default)" : "&Copy Here");
        AppendMenu(hMenu, MF_STRING, DA_ALIAS, "Create &Alias Here");
        break;
    case DT_DESKTOP:
        AppendMenu(hMenu, MF_STRING, DA_SHORTCUT, "Create &Shortcut Here");
        break;
    case DT_PROGRAM:
        AppendMenu(hMenu, MF_STRING, DA_OPEN, "&Open with This Program");
        break;
    case DT_BIN:
        AppendMenu(hMenu, MF_STRING | (v->szPath[0] ? 0 : MF_GRAYED), DA_DELETE,
                   "&Delete");
        break;
    }
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, IDCANCEL, "Cancel");
    s_bContextMenu = TRUE;
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwndOwner, NULL);
    s_bContextMenu = FALSE;
    DestroyMenu(hMenu);
    if (PeekMessage(&msg, hwndOwner, WM_COMMAND, WM_COMMAND, PM_REMOVE)
        && msg.wParam >= DA_MOVE && msg.wParam <= DA_DELETE)
        nChoice = msg.wParam;
    return nChoice;
}

static void DoDrop(HWND hwnd, LPFVIEW v, POINT ptScreen, BOOL bMenu)
{
    char szDir[MAXPATHLEN];
    HGLOBAL hList;
    int nTarget = DropTargetAt(ptScreen, szDir);
    int nAction;

    if (nTarget == DT_NONE)
        return;
    if (nTarget == DT_FOLDER && lstrcmpi(szDir, v->szPath) == 0 && !bMenu)
        return;
    switch (nTarget) {
    case DT_BIN:        nAction = DA_DELETE; break;
    case DT_DESKTOP:    nAction = DA_SHORTCUT; break;
    case DT_PROGRAM:    nAction = DA_OPEN; break;
    default:            nAction = IsCopyDrop(v, szDir) ? DA_COPY : DA_MOVE; break;
    }
    if (bMenu || (g_cfg.bAskDrop && nTarget == DT_FOLDER))
        nAction = DropMenu(hwnd, v, nTarget, szDir, nAction, ptScreen);
    if (nAction == DA_NONE)
        return;
    hList = FViewGetSelection(hwnd);
    if (!hList)
        return;
    switch (nAction) {
    case DA_DELETE:
        if (v->szPath[0])
            FileOpDelete(hwnd, hList, TRUE);
        break;
    case DA_SHORTCUT:
        DesktopAddShortcuts(hList, ptScreen);
        break;
    case DA_ALIAS:
        AliasCreateList(hwnd, hList, szDir);
        break;
    case DA_OPEN:
        {
            char szCmd[MAXPATHLEN * 2], szWork[MAXPATHLEN];
            LPCSTR lp;
            int n = 0;

            lstrcpy(szWork, szDir);
            PathParent(szWork);
            for (lp = PathListFirst(hList); lp && n < 8; lp = PathListNext(lp), n++) {
                wsprintf(szCmd, "%s %s", (LPSTR)szDir, lp);
                LaunchCommand(hwnd, szCmd, szWork, SW_SHOWNORMAL);
            }
            PathListUnlock(hList);
        }
        break;
    case DA_COPY:
    case DA_MOVE:
        if (lstrcmpi(szDir, v->szPath) != 0 && (v->szPath[0] || nAction == DA_COPY))
            FileOpCopyMove(hwnd, hList, szDir, nAction == DA_MOVE);
        break;
    }
    GlobalFree(hList);
}

static void OnDropFiles(HWND hwnd, LPFVIEW v, HDROP hDrop)
{
    char szFile[MAXPATHLEN];
    HGLOBAL hList = NULL;
    UINT i, n;

    n = DragQueryFile(hDrop, (UINT)-1, NULL, 0);
    for (i = 0; i < n; i++) {
        DragQueryFile(hDrop, i, szFile, sizeof(szFile));
        PathListAdd(&hList, szFile);
    }
    DragFinish(hDrop);
    if (hList && v->szPath[0])
        FileOpCopyMove(hwnd, hList, v->szPath, FALSE);
    if (hList)
        GlobalFree(hList);
}

/* -------------------------------------------------------------------- */
/* Context menu                                                         */
/* -------------------------------------------------------------------- */

static void AppendViewMenu(HMENU hMenu, LPFVIEW v)
{
    HMENU hView = CreatePopupMenu();
    HMENU hSort = CreatePopupMenu();

    AppendMenu(hView, MF_STRING | (v->nMode == FV_LARGE ? MF_CHECKED : 0),
               CMD_F_LARGEICONS, "Lar&ge Icons");
    AppendMenu(hView, MF_STRING | (v->nMode == FV_SMALL ? MF_CHECKED : 0),
               CMD_F_SMALLICONS, "S&mall Icons");
    AppendMenu(hView, MF_STRING | (v->nMode == FV_LIST ? MF_CHECKED : 0),
               CMD_F_LIST, "&List");
    AppendMenu(hView, MF_STRING | (v->nMode == FV_DETAILS ? MF_CHECKED : 0),
               CMD_F_DETAILS, "&Details");
    AppendMenu(hSort, MF_STRING | (v->nSort == FS_NAME ? MF_CHECKED : 0),
               CMD_F_SORTNAME, "by &Name");
    AppendMenu(hSort, MF_STRING | (v->nSort == FS_TYPE ? MF_CHECKED : 0),
               CMD_F_SORTTYPE, "by &Type");
    AppendMenu(hSort, MF_STRING | (v->nSort == FS_SIZE ? MF_CHECKED : 0),
               CMD_F_SORTSIZE, "by &Size");
    AppendMenu(hSort, MF_STRING | (v->nSort == FS_DATE ? MF_CHECKED : 0),
               CMD_F_SORTDATE, "by &Date");
    AppendMenu(hMenu, MF_POPUP, (UINT)hView, "&View");
    AppendMenu(hMenu, MF_POPUP, (UINT)hSort, "Arrange &Icons");
}

static void ContextMenu(HWND hwnd, LPFVIEW v, POINT pt, BOOL bOnItem)
{
    HMENU hMenu = CreatePopupMenu();
    BOOL bFolder = v->szPath[0] != '\0';
    UINT uPaste = (s_hClip && bFolder) ? MF_ENABLED : MF_GRAYED;

    if (bOnItem) {
        BOOL bDir = v->nFocus >= 0 && v->lpItems[v->nFocus].bKind != FK_FILE;

        AppendMenu(hMenu, MF_STRING, CMD_F_OPEN, "&Open");
        if (bDir)
            AppendMenu(hMenu, MF_STRING, CMD_F_EXPLORE, "&Explore");
        else {
            AppendMenu(hMenu, MF_STRING, CMD_F_OPENWITH, "Open &With...");
            AppendMenu(hMenu, MF_STRING, CMD_F_INSPECT, "&Inspect");
        }
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        if (bFolder) {
            AppendMenu(hMenu, MF_STRING, CMD_F_CUT, "Cu&t");
            AppendMenu(hMenu, MF_STRING, CMD_F_COPY, "&Copy");
            if (bDir)
                AppendMenu(hMenu, MF_STRING | uPaste, CMD_F_PASTE, "&Paste");
            AppendMenu(hMenu, MF_STRING, CMD_F_NEWALIAS, "Create &Alias");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, CMD_F_DELETE, "&Delete");
            AppendMenu(hMenu, MF_STRING, CMD_F_RENAME, "Rena&me");
            AppendMenu(hMenu, MF_STRING, CMD_F_DESCRIBE, "De&scribe...");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        }
        AppendMenu(hMenu, MF_STRING, CMD_F_PROPERTIES, "P&roperties");
    } else {
        AppendViewMenu(hMenu, v);
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        if (bFolder) {
            AppendMenu(hMenu, MF_STRING | uPaste, CMD_F_PASTE, "&Paste");
            AppendMenu(hMenu, MF_STRING, CMD_F_NEWFOLDER, "New &Folder");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, CMD_F_SELECTFILES, "&Select Files...");
            AppendMenu(hMenu, MF_STRING, CMD_F_FILTER, "Set F&ilter...");
        }
        AppendMenu(hMenu, MF_STRING, CMD_F_REFRESH, "&Refresh");
        if (bFolder)
            AppendMenu(hMenu, MF_STRING, CMD_F_PROPERTIES, "P&roperties");
    }
    ClientToScreen(hwnd, &pt);
    /* owned by the frame: theme engines only restyle menus of top-level
       windows, and the frame passes the commands back to us */
    s_bContextMenu = TRUE;
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, TopLevelOf(hwnd), NULL);
    s_bContextMenu = FALSE;
    DestroyMenu(hMenu);
}

/* The focused file, if it is a file */
static BOOL FocusFile(LPFVIEW v, LPSTR lpPath)
{
    if (v->nFocus < 0 || v->nFocus >= v->nItems || !v->lpItems[v->nFocus].bSel
        || v->lpItems[v->nFocus].bKind != FK_FILE)
        return FALSE;
    ItemPath(v, v->nFocus, lpPath);
    return TRUE;
}

static void DoSelectFiles(HWND hwnd, LPFVIEW v)
{
    static char szSpec[64] = "*.*";
    int i, n = SelectFilesDialog(hwnd, szSpec, sizeof(szSpec));

    if (n == IDCANCEL)
        return;
    for (i = 0; i < v->nItems; i++)
        if (v->lpItems[i].bKind != FK_DRIVE && MatchSpecList(v->lpItems[i].szName, szSpec))
            SelectItem(hwnd, v, i, n == IDOK);
    Notify(hwnd, FVN_SELCHANGE, 0L);
}

static void DoFilter(HWND hwnd, LPFVIEW v)
{
    BOOL bHidden = v->bHidden;

    if (!v->szPath[0] || !FilterDialog(hwnd, v->szFilter, sizeof(v->szFilter), &bHidden))
        return;
    v->bHidden = bHidden;
    FViewRefresh(hwnd);
    Notify(hwnd, FVN_PATHCHANGED, 0L);
}

static void DoDescribe(HWND hwnd, LPFVIEW v)
{
    char szDesc[DESC_MAX + 1];
    LPFITEM p;

    if (!v->szPath[0] || v->nFocus < 0 || v->nFocus >= v->nItems)
        return;
    p = &v->lpItems[v->nFocus];
    szDesc[0] = '\0';
    if (p->wDesc)
        lstrcpyn(szDesc, v->lpDesc + p->wDesc - 1, sizeof(szDesc));
    if (!DescribeDialog(hwnd, p->szName, szDesc, sizeof(szDesc)))
        return;
    if (!DescriptionSet(v->szPath, p->szName, szDesc))
        MessageBox(hwnd, "The description could not be saved.", NS_APPNAME,
                   MB_OK | MB_ICONEXCLAMATION);
    FViewRefreshPath(v->szPath);
}

static void DoInspect(HWND hwnd, LPFVIEW v)
{
    char szPath[MAXPATHLEN], szCmd[MAXPATHLEN * 2], szViewer[MAXPATHLEN];

    if (!FocusFile(v, szPath))
        return;
    CfgGetStr("Folders", "Inspect", "NOTEPAD.EXE", szViewer, sizeof(szViewer));
    wsprintf(szCmd, "%s %s", (LPSTR)szViewer, (LPSTR)szPath);
    LaunchCommand(hwnd, szCmd, v->szPath, SW_SHOWNORMAL);
}

static void DoPrint(HWND hwnd, LPFVIEW v)
{
    char szPath[MAXPATHLEN];
    HINSTANCE hInst;
    int i;

    for (i = 0; i < v->nItems; i++) {
        if (!v->lpItems[i].bSel || v->lpItems[i].bKind != FK_FILE)
            continue;
        ItemPath(v, i, szPath);
        hInst = ShellExecute(hwnd, "print", szPath, NULL, v->szPath, SW_SHOWMINNOACTIVE);
        if ((UINT)hInst <= 32) {
            ShowLaunchError(hwnd, (UINT)hInst, szPath);
            break;
        }
    }
}

/* Copies the selected paths to the clipboard as text, one per line */
static void DoCopyNames(HWND hwnd, LPFVIEW v)
{
    HGLOBAL hList = FViewGetSelection(hwnd), hText;
    LPCSTR lp;
    LPSTR lpText;
    UINT cb = 1;

    if (!hList)
        return;
    for (lp = PathListFirst(hList); lp; lp = PathListNext(lp))
        cb += lstrlen(lp) + 2;
    PathListUnlock(hList);
    hText = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, cb);
    if (hText) {
        lpText = (LPSTR)GlobalLock(hText);
        lpText[0] = '\0';
        for (lp = PathListFirst(hList); lp; lp = PathListNext(lp)) {
            lstrcat(lpText, lp);
            lstrcat(lpText, "\r\n");
        }
        PathListUnlock(hList);
        GlobalUnlock(hText);
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            SetClipboardData(CF_TEXT, hText);
            CloseClipboard();
        } else
            GlobalFree(hText);
    }
    GlobalFree(hList);
    (void)v;
}

static void DoColumns(HWND hwnd)
{
    UINT uHidden = s_uHiddenCols;
    int i;

    if (!ColumnsDialog(hwnd, &uHidden))
        return;
    s_uHiddenCols = uHidden;
    CfgSetInt("Folders", "HiddenColumns", (int)uHidden);
    for (i = 0; i < s_nViews; i++) {
        LPFVIEW w = GetView(s_hwndViews[i]);

        if (w) {
            UpdateLayout(s_hwndViews[i], w);
            InvalidateRect(s_hwndViews[i], NULL, TRUE);
        }
    }
}

/* Network drives: Windows' own dialogs, if the network driver has them */
static void NetDialog(HWND hwnd, BOOL bConnect)
{
    typedef WORD (FAR PASCAL *NETDLGPROC)(HWND, WORD);
    NETDLGPROC lpfn = (NETDLGPROC)GetProcAddress(GetModuleHandle("USER"),
                          bConnect ? "WNETCONNECTDIALOG" : "WNETDISCONNECTDIALOG");

    if (!lpfn && bConnect)
        lpfn = (NETDLGPROC)GetProcAddress(GetModuleHandle("USER"), "WNETCONNECTIONDIALOG");
    if (!lpfn || lpfn(hwnd, 1 /* WNTYPE_DRIVE */) == 1 /* WN_NOT_SUPPORTED */) {
        MessageBox(hwnd, "The installed network does not offer this.", NS_APPNAME,
                   MB_OK | MB_ICONINFORMATION);
        return;
    }
    FViewRefreshPath("");
    ExplorerRefreshDrives();
}

/* Handles a CMD_F_* command; returns FALSE if it is not a view command */
BOOL FViewCommand(HWND hwnd, UINT id)
{
    LPFVIEW v = GetView(hwnd);
    int i;

    if (!v)
        return FALSE;
    switch (id) {
    case CMD_F_OPEN:        OpenSelection(hwnd, v); break;
    case CMD_F_EXPLORE:
        if (v->nFocus >= 0)
            OpenItem(hwnd, v, v->nFocus, TRUE);
        else
            ExplorerOpen(v->szPath);
        break;
    case CMD_F_UP:          Notify(hwnd, FVN_GOUP, 0L); break;
    case CMD_F_REFRESH:     FViewRefresh(hwnd); break;
    case CMD_F_SELALL:
        for (i = 0; i < v->nItems; i++)
            SelectItem(hwnd, v, i, TRUE);
        Notify(hwnd, FVN_SELCHANGE, 0L);
        break;
    case CMD_F_INVERTSEL:
        for (i = 0; i < v->nItems; i++)
            SelectItem(hwnd, v, i, !v->lpItems[i].bSel);
        Notify(hwnd, FVN_SELCHANGE, 0L);
        break;
    case CMD_F_LARGEICONS:  FViewSetMode(hwnd, FV_LARGE); break;
    case CMD_F_SMALLICONS:  FViewSetMode(hwnd, FV_SMALL); break;
    case CMD_F_LIST:        FViewSetMode(hwnd, FV_LIST); break;
    case CMD_F_DETAILS:     FViewSetMode(hwnd, FV_DETAILS); break;
    case CMD_F_SORTNAME:    FViewSetSort(hwnd, FS_NAME); break;
    case CMD_F_SORTTYPE:    FViewSetSort(hwnd, FS_TYPE); break;
    case CMD_F_SORTSIZE:    FViewSetSort(hwnd, FS_SIZE); break;
    case CMD_F_SORTDATE:    FViewSetSort(hwnd, FS_DATE); break;
    case CMD_F_CUT:         DoCutCopy(hwnd, TRUE); break;
    case CMD_F_COPY:        DoCutCopy(hwnd, FALSE); break;
    case CMD_F_PASTE:       DoPaste(hwnd, v); break;
    case CMD_F_DELETE:      DoDelete(hwnd, v, IsShiftDown()); break;
    case CMD_F_RENAME:      BeginRename(hwnd, v); break;
    case CMD_F_NEWFOLDER:   DoNewFolder(hwnd, v); break;
    case CMD_F_PROPERTIES:  DoProperties(hwnd, v); break;
    case CMD_F_OPENWITH:
        {
            char szPath[MAXPATHLEN];

            if (FocusFile(v, szPath))
                OpenWithDialog(hwnd, szPath);
        }
        break;
    case CMD_F_SELECTFILES: DoSelectFiles(hwnd, v); break;
    case CMD_F_FILTER:      DoFilter(hwnd, v); break;
    case CMD_F_GOTO:
        {
            char szPath[MAXPATHLEN];

            lstrcpy(szPath, v->szPath[0] ? v->szPath : "C:\\");
            if (GotoDialog(hwnd, szPath, sizeof(szPath)))
                Notify(hwnd, FVN_OPENFOLDER, (LPARAM)(LPSTR)szPath);
        }
        break;
    case CMD_F_DESCRIBE:    DoDescribe(hwnd, v); break;
    case CMD_F_INSPECT:     DoInspect(hwnd, v); break;
    case CMD_F_PRINT:       DoPrint(hwnd, v); break;
    case CMD_F_NEWALIAS:
        {
            HGLOBAL hList = FViewGetSelection(hwnd);

            if (hList && v->szPath[0])
                AliasCreateList(hwnd, hList, v->szPath);
            else if (hList)
                MessageBox(hwnd, "Aliases for drives go into a folder: drag the drive "
                           "there with the right mouse button.", NS_APPNAME,
                           MB_OK | MB_ICONINFORMATION);
            if (hList)
                GlobalFree(hList);
        }
        break;
    case CMD_F_UNDELETE:    BinUndelete(hwnd); break;
    case CMD_F_COLUMNS:     DoColumns(hwnd); break;
    case CMD_F_COPYNAMES:   DoCopyNames(hwnd, v); break;
    case CMD_F_MAPDRIVE:    NetDialog(hwnd, TRUE); break;
    case CMD_F_DISCONNECT:  NetDialog(hwnd, FALSE); break;
    default:
        return FALSE;
    }
    return TRUE;
}

/* Updates check marks in a frame window's menu */
void FViewInitMenu(HWND hwnd, HMENU hMenu)
{
    LPFVIEW v = GetView(hwnd);
    int nSel = FViewSelCount(hwnd);
    BOOL bFolder;

    /* context menus are built with the right states already */
    if (!v || s_bContextMenu)
        return;
    bFolder = v->szPath[0] != '\0';
    CheckMenuItem(hMenu, CMD_F_LARGEICONS, v->nMode == FV_LARGE ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, CMD_F_SMALLICONS, v->nMode == FV_SMALL ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, CMD_F_LIST, v->nMode == FV_LIST ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, CMD_F_DETAILS, v->nMode == FV_DETAILS ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, CMD_F_SORTNAME, v->nSort == FS_NAME ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, CMD_F_SORTTYPE, v->nSort == FS_TYPE ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, CMD_F_SORTSIZE, v->nSort == FS_SIZE ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hMenu, CMD_F_SORTDATE, v->nSort == FS_DATE ? MF_CHECKED : MF_UNCHECKED);
    EnableMenuItem(hMenu, CMD_F_CUT, (bFolder && nSel) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_COPY, (bFolder && nSel) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_PASTE, (bFolder && s_hClip) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_DELETE, (bFolder && nSel) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_RENAME, (bFolder && nSel == 1) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_NEWFOLDER, bFolder ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_UP, bFolder ? MF_ENABLED : MF_GRAYED);
    {
        BOOL bFile = v->nFocus >= 0 && v->nFocus < v->nItems && v->lpItems[v->nFocus].bSel
                     && v->lpItems[v->nFocus].bKind == FK_FILE;

        EnableMenuItem(hMenu, CMD_F_OPENWITH, bFile ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hMenu, CMD_F_INSPECT, bFile ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hMenu, CMD_F_PRINT, bFile ? MF_ENABLED : MF_GRAYED);
    }
    EnableMenuItem(hMenu, CMD_F_NEWALIAS, (bFolder && nSel) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_DESCRIBE, (bFolder && nSel == 1) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_COPYNAMES, nSel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_FILTER, bFolder ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, CMD_F_UNDELETE, BinIsEmpty() ? MF_GRAYED : MF_ENABLED);
    CheckMenuItem(hMenu, CMD_F_FILTER,
                  lstrcmp(v->szFilter, "*.*") != 0 ? MF_CHECKED : MF_UNCHECKED);
}

/* -------------------------------------------------------------------- */
/* Keyboard                                                             */
/* -------------------------------------------------------------------- */

static void OnKeyDown(HWND hwnd, LPFVIEW v, WPARAM vk)
{
    BOOL bShift = IsShiftDown();
    BOOL bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int i = v->nFocus < 0 ? 0 : v->nFocus;
    int nPage = v->nCols * v->nVisRows;
    /* the step of Left/Right and of Up/Down: the list view runs in columns */
    int nSide = v->nMode == FV_LIST ? v->nVisRows : v->nMode == FV_DETAILS ? 0 : 1;
    int nLine = v->nMode == FV_LIST ? 1 : v->nCols;

    if (v->nMode == FV_LIST)
        nPage = v->nVisRows * v->nVisCols;
    switch (vk) {
    case VK_LEFT:
        if (nSide == 0)
            ScrollToX(hwnd, v, v->xScroll - LINE_CX);
        else if (i - nSide >= 0)
            MoveFocusTo(hwnd, v, i - nSide, bShift, bCtrl);
        break;
    case VK_RIGHT:
        if (nSide == 0)
            ScrollToX(hwnd, v, v->xScroll + LINE_CX);
        else if (i + nSide < v->nItems)
            MoveFocusTo(hwnd, v, i + nSide, bShift, bCtrl);
        break;
    case VK_UP:     if (i - nLine >= 0) MoveFocusTo(hwnd, v, i - nLine, bShift, bCtrl); break;
    case VK_DOWN:   if (i + nLine < v->nItems) MoveFocusTo(hwnd, v, i + nLine, bShift, bCtrl); break;
    case VK_HOME:   MoveFocusTo(hwnd, v, 0, bShift, bCtrl); break;
    case VK_END:    MoveFocusTo(hwnd, v, v->nItems - 1, bShift, bCtrl); break;
    case VK_PRIOR:  MoveFocusTo(hwnd, v, i - nPage, bShift, bCtrl); break;
    case VK_NEXT:   MoveFocusTo(hwnd, v, i + nPage, bShift, bCtrl); break;
    case VK_SPACE:
        if (bCtrl && v->nFocus >= 0) {
            SelectItem(hwnd, v, v->nFocus, !v->lpItems[v->nFocus].bSel);
            Notify(hwnd, FVN_SELCHANGE, 0L);
        }
        break;
    case VK_RETURN: FViewCommand(hwnd, CMD_F_OPEN); break;
    case VK_BACK:   FViewCommand(hwnd, CMD_F_UP); break;
    case VK_TAB:    Notify(hwnd, FVN_TAB, 0L); break;
    case VK_F5:     FViewCommand(hwnd, CMD_F_REFRESH); break;
    case VK_F2:     FViewCommand(hwnd, CMD_F_RENAME); break;
    case VK_DELETE: FViewCommand(hwnd, CMD_F_DELETE); break;
    default:
        if (bCtrl) {
            switch (vk) {
            case 'A': FViewCommand(hwnd, CMD_F_SELALL); break;
            case 'C': FViewCommand(hwnd, CMD_F_COPY); break;
            case 'X': FViewCommand(hwnd, CMD_F_CUT); break;
            case 'V': FViewCommand(hwnd, CMD_F_PASTE); break;
            case 'G': FViewCommand(hwnd, CMD_F_GOTO); break;
            case 'U': FViewCommand(hwnd, CMD_F_UNDELETE); break;
            case 'I': FViewCommand(hwnd, CMD_F_INSPECT); break;
            }
        }
    }
}

/* Type-ahead: jump to the next item starting with the typed letter */
static void OnChar(HWND hwnd, LPFVIEW v, WPARAM ch)
{
    int i, n;

    if (ch < '!' || v->nItems == 0)
        return;
    ch = (WPARAM)(DWORD)AnsiUpper((LPSTR)(DWORD)ch);
    for (n = 1; n <= v->nItems; n++) {
        i = (v->nFocus + n) % v->nItems;
        if ((WPARAM)(BYTE)v->lpItems[i].szName[0] == ch) {
            MoveFocusTo(hwnd, v, i, FALSE, FALSE);
            return;
        }
    }
}

/* -------------------------------------------------------------------- */
/* Window procedure                                                     */
/* -------------------------------------------------------------------- */

static void RemoveView(HWND hwnd)
{
    int i;

    for (i = 0; i < s_nViews; i++)
        if (s_hwndViews[i] == hwnd) {
            s_hwndViews[i] = s_hwndViews[--s_nViews];
            return;
        }
}

/* -------------------------------------------------------------------- */
/* Rubber-band selection                                                */
/* -------------------------------------------------------------------- */

static void DrawRubber(HWND hwnd, LPFVIEW v)
{
    HDC hdc;

    if (IsRectEmpty(&v->rcRubber))
        return;
    hdc = GetDC(hwnd);
    DrawFocusRect(hdc, &v->rcRubber);
    ReleaseDC(hwnd, hdc);
}

static void MoveRubber(HWND hwnd, LPFVIEW v, int x, int y)
{
    RECT rc, rcItem, rcTmp;
    BOOL bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int i;

    SetRect(&rc, min(x, v->ptRubber.x), min(y, v->ptRubber.y),
            max(x, v->ptRubber.x) + 1, max(y, v->ptRubber.y) + 1);
    if (EqualRect(&rc, &v->rcRubber))
        return;
    DrawRubber(hwnd, v);
    v->rcRubber = rc;
    for (i = 0; i < v->nItems; i++) {
        HotRect(hwnd, v, i, &rcItem);
        if (v->nMode == FV_LARGE)
            rcItem.bottom = rcItem.top + 37 + v->cyText;
        if (IntersectRect(&rcTmp, &rcItem, &rc))
            SelectItem(hwnd, v, i, TRUE);
        else if (!bCtrl)
            SelectItem(hwnd, v, i, FALSE);
    }
    UpdateWindow(hwnd);
    DrawRubber(hwnd, v);
}

static void EndRubber(HWND hwnd, LPFVIEW v)
{
    int i;

    DrawRubber(hwnd, v);
    v->bRubber = FALSE;
    SetRectEmpty(&v->rcRubber);
    ReleaseCapture();
    for (i = 0; i < v->nItems; i++)
        if (v->lpItems[i].bSel) {
            SetFocusItem(hwnd, v, i);
            v->nAnchor = i;
            break;
        }
    Notify(hwnd, FVN_SELCHANGE, 0L);
}

/* -------------------------------------------------------------------- */
/* File hints                                                           */
/* -------------------------------------------------------------------- */

#define TIMER_HINT      1
#define HINT_DELAY      700
#define HINT_CHECK      200

static void HideHint(HWND hwnd, LPFVIEW v)
{
    KillTimer(hwnd, TIMER_HINT);
    v->nHint = -1;
    HintHide();
}

static void ShowHint(LPFVIEW v, int i)
{
    char szHint[200], szSize[24], szDate[16], szTime[16], szType[32];
    LPFITEM p = &v->lpItems[i];
    POINT pt;
    int n = 0;

    TypeName(p, szType);
    n = wsprintf(szHint, "%s", (LPSTR)ShownName(p));
    if (p->wDesc)
        n += wsprintf(szHint + n, "\n%s", v->lpDesc + p->wDesc - 1);
    n += wsprintf(szHint + n, "\n%s", (LPSTR)szType);
    if (p->bKind == FK_FILE) {
        FormatBytes(p->dwSize, szSize);
        n += wsprintf(szHint + n, ", %s", (LPSTR)szSize);
    }
    if (p->bKind != FK_DRIVE) {
        FormatDate(p->wDate, szDate);
        FormatTime(p->wTime, szTime);
        n += wsprintf(szHint + n, "\n%s  %s", (LPSTR)szDate, (LPSTR)szTime);
        if (p->bAttr & (_A_RDONLY | _A_HIDDEN | _A_SYSTEM))
            wsprintf(szHint + n, "\n%s%s%s",
                     (LPSTR)((p->bAttr & _A_RDONLY) ? "Read-only " : ""),
                     (LPSTR)((p->bAttr & _A_HIDDEN) ? "Hidden " : ""),
                     (LPSTR)((p->bAttr & _A_SYSTEM) ? "System" : ""));
    }
    GetCursorPos(&pt);
    HintShow(szHint, pt.x, pt.y + 20);
}

/* The mouse moved over the view without a button: prepare a hint */
static void TrackHint(HWND hwnd, LPFVIEW v, int x, int y)
{
    int i;

    if (!g_cfg.bFileHints || GetActiveWindow() != TopLevelOf(hwnd))
        return;
    i = HitTest(hwnd, v, x, y);
    if (i == v->nHint)
        return;
    HideHint(hwnd, v);
    if (i < 0)
        return;
    v->nHint = i;
    SetTimer(hwnd, TIMER_HINT, HINT_DELAY, NULL);
}

static void OnHintTimer(HWND hwnd, LPFVIEW v)
{
    POINT pt;

    GetCursorPos(&pt);
    if (WindowFromPoint(pt) != hwnd || v->nHint < 0 || v->nHint >= v->nItems
        || v->bPressed || v->bRubber || GetActiveWindow() != TopLevelOf(hwnd)) {
        HideHint(hwnd, v);
        return;
    }
    ScreenToClient(hwnd, &pt);
    if (HitTest(hwnd, v, pt.x, pt.y) != v->nHint) {
        HideHint(hwnd, v);
        return;
    }
    if (!HintVisible())
        ShowHint(v, v->nHint);
    /* keep checking whether the mouse leaves */
    SetTimer(hwnd, TIMER_HINT, HINT_CHECK, NULL);
}

static void OnLButtonDown(HWND hwnd, LPFVIEW v, int x, int y)
{
    BOOL bShift = IsShiftDown();
    BOOL bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int i = HitTest(hwnd, v, x, y);

    if (v->hwndEdit)
        EndRename(hwnd, v, TRUE);
    SetFocus(hwnd);
    v->nDownItem = i;
    v->bDeferSelect = FALSE;
    if (i < 0) {
        if (!bCtrl && !bShift) {
            SelectNone(hwnd, v);
            Notify(hwnd, FVN_SELCHANGE, 0L);
        }
        /* a rubber band from here selects what it touches */
        UpdateWindow(hwnd);
        v->bRubber = TRUE;
        v->ptRubber.x = x;
        v->ptRubber.y = y;
        SetRect(&v->rcRubber, x, y, x, y);
        SetCapture(hwnd);
        return;
    }
    if (bCtrl) {
        SelectItem(hwnd, v, i, !v->lpItems[i].bSel);
        v->nAnchor = i;
        SetFocusItem(hwnd, v, i);
        Notify(hwnd, FVN_SELCHANGE, 0L);
    } else if (bShift) {
        MoveFocusTo(hwnd, v, i, TRUE, FALSE);
    } else if (v->lpItems[i].bSel) {
        /* may be the start of a drag of the whole selection */
        v->bDeferSelect = TRUE;
        SetFocusItem(hwnd, v, i);
    } else {
        MoveFocusTo(hwnd, v, i, FALSE, FALSE);
    }
    if (v->lpItems[i].bSel) {
        v->bPressed = TRUE;
        v->nButton = 1;
        v->ptDown.x = x;
        v->ptDown.y = y;
        SetCapture(hwnd);
    }
}

/* Details header: the column whose right edge is at x, or -1 */
static int HeaderEdge(LPFVIEW v, int x)
{
    int i, xEdge = MARGIN - v->xScroll;

    for (i = 0; i < NCOLS; i++) {
        xEdge += ColW(i);
        if (ColW(i) && x >= xEdge - SIZE_ZONE && x <= xEdge + SIZE_ZONE)
            return i;
    }
    return -1;
}

/* Details header: the column at x, or -1 */
static int HeaderColumn(LPFVIEW v, int x)
{
    int i, xEdge = MARGIN - v->xScroll;

    for (i = 0; i < NCOLS; i++) {
        xEdge += ColW(i);
        if (ColW(i) && x < xEdge)
            return i;
    }
    return -1;
}

/* Other views pick up changed column widths */
static void ColumnsChanged(HWND hwndFrom)
{
    int i;

    SaveColumns();
    for (i = 0; i < s_nViews; i++) {
        LPFVIEW w = GetView(s_hwndViews[i]);

        if (s_hwndViews[i] != hwndFrom && w && w->nMode == FV_DETAILS) {
            UpdateLayout(s_hwndViews[i], w);
            InvalidateRect(s_hwndViews[i], NULL, TRUE);
        }
    }
}

LRESULT CALLBACK NSEXPORT FViewWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    LPFVIEW v = GetView(hwnd);
    PAINTSTRUCT ps;
    POINT pt;
    int i;

    switch (msg) {
    case WM_CREATE:
        {
            CREATESTRUCT FAR *lpcs = (CREATESTRUCT FAR *)lParam;
            TEXTMETRIC tm;
            HDC hdc;
            HFONT hfOld;

            v = (LPFVIEW)malloc(sizeof(FVIEW));
            if (!v)
                return -1;
            _fmemset(v, 0, sizeof(FVIEW));
            SetWindowLong(hwnd, 0, (LONG)v);
            v->nRenaming = -1;
            v->nSizing = -1;
            v->nHint = -1;
            lstrcpy(v->szFilter, "*.*");
            v->bHidden = g_cfg.bShowHidden;
            v->nMode = CfgGetInt("Folders", "View", FV_LARGE);
            if (v->nMode < FV_LARGE || v->nMode > FV_LIST)
                v->nMode = FV_LARGE;
            v->nSort = CfgGetInt("Folders", "Sort", FS_NAME);

            hdc = GetDC(hwnd);
            hfOld = SelectObject(hdc, g_hfontUI);
            GetTextMetrics(hdc, &tm);
            SelectObject(hdc, hfOld);
            ReleaseDC(hwnd, hdc);
            v->cyText = tm.tmHeight;

            if (lpcs->lpCreateParams)
                lstrcpyn(v->szPath, (LPCSTR)lpcs->lpCreateParams, sizeof(v->szPath));
            ReadFolder(v);
            if (s_nViews < MAX_VIEWS)
                s_hwndViews[s_nViews++] = hwnd;
            DragAcceptFiles(hwnd, TRUE);
        }
        return 0;

    case WM_SIZE:
        if (v) {
            UpdateLayout(hwnd, v);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        if (v)
            PaintView(hwnd, ps.hdc, v, &ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (v)
            InvalidateItem(hwnd, v, v->nFocus);
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_VSCROLL:
        if (!v)
            break;
        switch (wParam) {
        case SB_LINEUP:         ScrollTo(hwnd, v, v->nTop - 1); break;
        case SB_LINEDOWN:       ScrollTo(hwnd, v, v->nTop + 1); break;
        case SB_PAGEUP:         ScrollTo(hwnd, v, v->nTop - v->nVisRows); break;
        case SB_PAGEDOWN:       ScrollTo(hwnd, v, v->nTop + v->nVisRows); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:  ScrollTo(hwnd, v, LOWORD(lParam)); break;
        case SB_TOP:            ScrollTo(hwnd, v, 0); break;
        case SB_BOTTOM:         ScrollTo(hwnd, v, 32767); break;
        }
        return 0;

    case WM_HSCROLL:
        if (!v)
            break;
        {
            int nPos = v->nMode == FV_LIST ? v->nLeft : v->xScroll;
            int nStep = v->nMode == FV_LIST ? 1 : LINE_CX;
            int nPage = v->nMode == FV_LIST ? v->nVisCols : nStep * 8;

            switch (wParam) {
            case SB_LINEUP:         ScrollToX(hwnd, v, nPos - nStep); break;
            case SB_LINEDOWN:       ScrollToX(hwnd, v, nPos + nStep); break;
            case SB_PAGEUP:         ScrollToX(hwnd, v, nPos - nPage); break;
            case SB_PAGEDOWN:       ScrollToX(hwnd, v, nPos + nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:  ScrollToX(hwnd, v, LOWORD(lParam)); break;
            case SB_TOP:            ScrollToX(hwnd, v, 0); break;
            case SB_BOTTOM:         ScrollToX(hwnd, v, 32767); break;
            }
        }
        return 0;

    case WM_SETCURSOR:
        /* the size cursor on the column edges of the details header */
        if (v && (HWND)wParam == hwnd && LOWORD(lParam) == HTCLIENT
            && v->nMode == FV_DETAILS) {
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (pt.y < v->cyHeader && HeaderEdge(v, pt.x) >= 0) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN:
        if (!v)
            break;
        if (v->nMode == FV_DETAILS && (int)HIWORD(lParam) < v->cyHeader) {
            int x = (int)LOWORD(lParam), c = HeaderEdge(v, x);

            if (c >= 0) {
                /* drag the column's right edge */
                v->nSizing = c;
                v->xSizeDown = x;
                v->cxSizeDown = s_cxCol[c];
                SetCapture(hwnd);
                return 0;
            }
            /* header click: sort by that column */
            switch (HeaderColumn(v, x)) {
            case DC_NAME:   FViewSetSort(hwnd, FS_NAME); break;
            case DC_SIZE:   FViewSetSort(hwnd, FS_SIZE); break;
            case DC_TYPE:   FViewSetSort(hwnd, FS_TYPE); break;
            case DC_DATE:
            case DC_TIME:   FViewSetSort(hwnd, FS_DATE); break;
            }
            return 0;
        }
        OnLButtonDown(hwnd, v, (int)LOWORD(lParam), (int)HIWORD(lParam));
        return 0;

    case WM_MOUSEMOVE:
        if (v && v->nSizing >= 0) {
            int cx = v->cxSizeDown + (int)LOWORD(lParam) - v->xSizeDown;

            if (cx < COL_MIN_CX)
                cx = COL_MIN_CX;
            if (cx != s_cxCol[v->nSizing]) {
                s_cxCol[v->nSizing] = cx;
                UpdateLayout(hwnd, v);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        if (v && v->bRubber) {
            MoveRubber(hwnd, v, (int)LOWORD(lParam), (int)HIWORD(lParam));
            return 0;
        }
        if (v && !v->bPressed)
            TrackHint(hwnd, v, (int)LOWORD(lParam), (int)HIWORD(lParam));
        if (!v || !v->bPressed)
            break;
        pt.x = (int)LOWORD(lParam);
        pt.y = (int)HIWORD(lParam);
        if (!v->bDragging
            && (abs(pt.x - v->ptDown.x) > DRAG_THRESHOLD
                || abs(pt.y - v->ptDown.y) > DRAG_THRESHOLD))
            v->bDragging = TRUE;
        if (v->bDragging) {
            char szDir[MAXPATHLEN];
            int nTarget;

            ClientToScreen(hwnd, &pt);
            nTarget = DropTargetAt(pt, szDir);
            if (nTarget == DT_FOLDER && lstrcmpi(szDir, v->szPath) == 0)
                nTarget = DT_NONE;
            SetCursor(v->nButton == 2 && nTarget != DT_NONE
                      ? LoadCursor(g_hInst, MAKEINTRESOURCE(IDC_DRAGMOVE))
                      : DragCursor(nTarget, nTarget == DT_FOLDER && IsCopyDrop(v, szDir)));
        }
        return 0;

    case WM_LBUTTONUP:
        if (v && v->nSizing >= 0) {
            v->nSizing = -1;
            ReleaseCapture();
            ColumnsChanged(hwnd);
            return 0;
        }
        if (v && v->bRubber) {
            EndRubber(hwnd, v);
            return 0;
        }
        if (!v || !v->bPressed || v->nButton != 1)
            break;
        v->bPressed = FALSE;
        ReleaseCapture();
        if (v->bDragging) {
            v->bDragging = FALSE;
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            pt.x = (int)LOWORD(lParam);
            pt.y = (int)HIWORD(lParam);
            ClientToScreen(hwnd, &pt);
            DoDrop(hwnd, v, pt, FALSE);
        } else if (v->bDeferSelect && v->nDownItem >= 0) {
            MoveFocusTo(hwnd, v, v->nDownItem, FALSE, FALSE);
        }
        v->bDeferSelect = FALSE;
        return 0;

    case WM_LBUTTONDBLCLK:
        if (!v)
            break;
        i = HitTest(hwnd, v, (int)LOWORD(lParam), (int)HIWORD(lParam));
        if (i >= 0)
            OpenItem(hwnd, v, i, IsShiftDown());
        return 0;

    case WM_RBUTTONDOWN:
        if (!v || v->bPressed || v->bRubber)
            break;
        HideHint(hwnd, v);
        SetFocus(hwnd);
        i = HitTest(hwnd, v, (int)LOWORD(lParam), (int)HIWORD(lParam));
        if (i >= 0 && !v->lpItems[i].bSel)
            MoveFocusTo(hwnd, v, i, FALSE, FALSE);
        else if (i >= 0)
            SetFocusItem(hwnd, v, i);
        if (i >= 0) {
            /* may be a drag with the right button: a menu at the drop */
            v->bPressed = TRUE;
            v->nButton = 2;
            v->bDragging = FALSE;
            v->ptDown.x = (int)LOWORD(lParam);
            v->ptDown.y = (int)HIWORD(lParam);
            SetCapture(hwnd);
        }
        return 0;

    case WM_RBUTTONUP:
        if (!v)
            break;
        pt.x = (int)LOWORD(lParam);
        pt.y = (int)HIWORD(lParam);
        if (v->bPressed && v->nButton == 2) {
            BOOL bDragged = v->bDragging;

            v->bPressed = FALSE;
            v->bDragging = FALSE;
            ReleaseCapture();
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            if (bDragged) {
                ClientToScreen(hwnd, &pt);
                DoDrop(hwnd, v, pt, TRUE);
                return 0;
            }
        }
        ContextMenu(hwnd, v, pt, HitTest(hwnd, v, pt.x, pt.y) >= 0);
        return 0;

    case WM_TIMER:
        if (v && wParam == TIMER_HINT)
            OnHintTimer(hwnd, v);
        return 0;

    case WM_CANCELMODE:
        if (v && v->bRubber)
            EndRubber(hwnd, v);
        if (v && v->bPressed) {
            v->bPressed = v->bDragging = FALSE;
            ReleaseCapture();
        }
        return 0;

    case WM_KEYDOWN:
        if (v) {
            HideHint(hwnd, v);
            OnKeyDown(hwnd, v, wParam);
        }
        return 0;

    case WM_SYSKEYDOWN:
        if (v && wParam == VK_RETURN) {
            FViewCommand(hwnd, CMD_F_PROPERTIES);
            return 0;
        }
        break;

    case WM_CHAR:
        if (v)
            OnChar(hwnd, v, wParam);
        return 0;

    case WM_COMMAND:
        if (wParam == ID_RENAME)
            return 0;
        if (!FViewCommand(hwnd, wParam))
            SendMessage(GetParent(hwnd), WM_COMMAND, wParam, lParam);
        return 0;

    case WM_NS_ENDRENAME:
        if (v && v->hwndEdit && GetFocus() != v->hwndEdit)
            EndRename(hwnd, v, TRUE);
        return 0;

    case WM_DROPFILES:
        if (v)
            OnDropFiles(hwnd, v, (HDROP)wParam);
        return 0;

    case WM_DESTROY:
        if (v)
            HideHint(hwnd, v);
        RemoveView(hwnd);
        DragAcceptFiles(hwnd, FALSE);
        if (v) {
            if (v->lpItems)
                free(v->lpItems);
            if (v->lpDesc)
                free(v->lpDesc);
            free(v);
            SetWindowLong(hwnd, 0, 0L);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* -------------------------------------------------------------------- */
/* Public interface                                                     */
/* -------------------------------------------------------------------- */

BOOL FViewRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = FViewWndProc;
    wc.cbWndExtra = sizeof(LPFVIEW);
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLS_FVIEW;
    return RegisterClass(&wc);
}

HWND FViewCreate(HWND hwndParent, LPCSTR lpPath)
{
    return CreateWindow(CLS_FVIEW, "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                        0, 0, 0, 0, hwndParent, (HMENU)1, g_hInst,
                        (LPVOID)lpPath);
}

LPCSTR FViewGetPath(HWND hwnd)
{
    LPFVIEW v = GetView(hwnd);
    return v ? v->szPath : "";
}

LPCSTR FViewGetFilter(HWND hwnd)
{
    LPFVIEW v = GetView(hwnd);
    return v ? v->szFilter : "*.*";
}

void FViewSetPath(HWND hwnd, LPCSTR lpPath)
{
    LPFVIEW v = GetView(hwnd);

    if (!v)
        return;
    if (v->hwndEdit)
        EndRename(hwnd, v, FALSE);
    lstrcpyn(v->szPath, lpPath, sizeof(v->szPath));
    v->nTop = 0;
    ReadFolder(v);
    UpdateLayout(hwnd, v);
    InvalidateRect(hwnd, NULL, TRUE);
    Notify(hwnd, FVN_PATHCHANGED, 0L);
}

/* Re-reads the folder, keeping the selection and scroll position */
void FViewRefresh(HWND hwnd)
{
    LPFVIEW v = GetView(hwnd);
    HGLOBAL hSel;
    LPCSTR lp;
    char szFocus[16];
    int i, nTop;

    if (!v)
        return;
    if (v->szPath[0] && !PathIsDirectory(v->szPath)) {
        /* the folder went away */
        Notify(hwnd, FVN_GONE, 0L);
        return;
    }
    hSel = FViewGetSelection(hwnd);
    szFocus[0] = '\0';
    if (v->nFocus >= 0 && v->nFocus < v->nItems)
        lstrcpy(szFocus, v->lpItems[v->nFocus].szName);
    nTop = v->nTop;
    ReadFolder(v);
    if (hSel) {
        for (lp = PathListFirst(hSel); lp; lp = PathListNext(lp)) {
            char szName[MAXPATHLEN];
            lstrcpy(szName, lp);
            for (i = 0; i < v->nItems; i++)
                if (lstrcmpi(v->lpItems[i].szName, PathFileName(szName)) == 0)
                    v->lpItems[i].bSel = TRUE;
        }
        PathListUnlock(hSel);
        GlobalFree(hSel);
    }
    for (i = 0; i < v->nItems; i++)
        if (lstrcmpi(v->lpItems[i].szName, szFocus) == 0)
            v->nFocus = i;
    v->nTop = nTop;
    UpdateLayout(hwnd, v);
    InvalidateRect(hwnd, NULL, TRUE);
    Notify(hwnd, FVN_SELCHANGE, 0L);
}

void FViewSetMode(HWND hwnd, int nMode)
{
    LPFVIEW v = GetView(hwnd);

    if (!v || v->nMode == nMode)
        return;
    v->nMode = nMode;
    CfgSetInt("Folders", "View", nMode);
    UpdateLayout(hwnd, v);
    EnsureVisible(hwnd, v, v->nFocus);
    InvalidateRect(hwnd, NULL, TRUE);
}

void FViewSetSort(HWND hwnd, int nSort)
{
    LPFVIEW v = GetView(hwnd);

    if (!v)
        return;
    v->nSort = nSort;
    CfgSetInt("Folders", "Sort", nSort);
    FViewRefresh(hwnd);
}

/* The options changed: views take the global Show hidden files again */
void FViewApplyOptions(void)
{
    int i;

    for (i = 0; i < s_nViews; i++) {
        LPFVIEW w = GetView(s_hwndViews[i]);

        if (w)
            w->bHidden = g_cfg.bShowHidden;
    }
    FViewRefreshPath(NULL);
}

/* Refreshes every view showing lpDir (or all views if lpDir is NULL) */
void FViewRefreshPath(LPCSTR lpDir)
{
    char szDir[MAXPATHLEN];
    HWND hwndList[MAX_VIEWS];
    int i, n = s_nViews;

    if (lpDir) {
        lstrcpyn(szDir, lpDir, sizeof(szDir));
        PathNormalize(szDir);
    }
    _fmemcpy(hwndList, s_hwndViews, n * sizeof(HWND));
    for (i = 0; i < n; i++)
        if (IsWindow(hwndList[i])
            && (!lpDir || lstrcmpi(FViewGetPath(hwndList[i]), szDir) == 0))
            FViewRefresh(hwndList[i]);
}

/* Drive type and icon for a drive number (0 = A:) */
int DriveIcon(int nDrive, BYTE FAR *lpbType)
{
    union REGS r;

    switch (GetDriveType(nDrive)) {
    case DRIVE_REMOVABLE:
        *lpbType = 1;
        return IDI_FLOPPY;
    case DRIVE_FIXED:
        *lpbType = 2;
        return IDI_HDD;
    }
    /* MSCDEX drives report as remote; ask MSCDEX */
    r.x.ax = 0x150B;
    r.x.bx = 0;
    r.x.cx = nDrive;
    int86(0x2F, &r, &r);
    if (r.x.bx == 0xADAD && r.x.ax != 0) {
        *lpbType = 4;
        return IDI_CDROM;
    }
    *lpbType = 3;
    return IDI_NETDRV;
}
