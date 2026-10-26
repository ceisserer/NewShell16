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
 * desktop.c - desktop icons
 *
 * Each desktop icon is a small popup window kept at the bottom of the
 * Z-order, painted with the desktop wallpaper behind it. Icons can be
 * dragged (optionally snapping to a grid); positions are saved in
 * NEWSHELL.INI. Besides the built-in Computer and Bin icons there are
 * shortcuts, made by dragging files, folders, drives or programs from a
 * folder window or File Manager onto the desktop:
 *
 *   [Desktop]
 *   Shortcut1=Notepad|NOTEPAD.EXE
 *   Shortcut2=Docs|$Folder C:\DOCS
 *   Shortcut3=Write|WRITE.EXE|C:\DOCS|2|PROGMAN.EXE|5
 *             caption|command|working folder|show|icon file|icon index
 *
 * Commands may be Start menu macros. Shortcuts carry a small arrow.
 * Dropping files on the Bin deletes them, on a shortcut to a folder
 * copies or moves them there, and on a shortcut to a program opens them
 * with that program.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define MAX_DESKICONS   64
#define DI_CX           76
#define DI_CY           62
#define GRID_CX         (DI_CX + 8)
#define GRID_CY         (DI_CY + 8)
#define GRID_X0         8
#define GRID_Y0         8
#define DRAG_THRESHOLD  4

#define DK_COMPUTER     0
#define DK_BIN          1
#define DK_SHORTCUT     2

#define IDM_OPEN        1
#define IDM_EMPTYBIN    2
#define IDM_EXPLORE     3
#define IDM_DELETE      4
#define IDM_PROPERTIES  5

typedef struct tagDESKICON {
    HWND     hwnd;
    char     szKey[16];     /* INI key: Computer, Bin, ShortcutN */
    BYTE     bKind;         /* DK_* */
    SHORTCUT sc;
    HICON    hIcon;
} DESKICON;

static DESKICON *s_icons;       /* MAX_DESKICONS, on the far heap */
static int      s_nIcons;
static HFONT    s_hfTitle;      /* Windows' icon title font */
static HBITMAP  s_hbmArrow;

/* drag state */
static BOOL  s_bDragging, s_bPressed;
static POINT s_ptDown;
static RECT  s_rcBefore;

typedef void (FAR PASCAL *PAINTDESKTOPPROC)(HDC);
static PAINTDESKTOPPROC s_lpfnPaintDesktop;

static DESKICON *FindIcon(HWND hwnd)
{
    int i;

    for (i = 0; i < s_nIcons; i++)
        if (s_icons[i].hwnd == hwnd)
            return &s_icons[i];
    return NULL;
}

static void PaintBackground(HWND hwnd, HDC hdc)
{
    RECT rc;
    HBRUSH hbr;

    if (s_lpfnPaintDesktop) {
        s_lpfnPaintDesktop(hdc);
        return;
    }
    GetClientRect(hwnd, &rc);
    hbr = CreateSolidBrush(GetSysColor(COLOR_BACKGROUND));
    FillRect(hdc, &rc, hbr);
    DeleteObject(hbr);
}

/*
 * The shortcut arrow of Calmira (11x11): a white box with a black arrow
 * pointing up and right, black edges on the right and at the bottom.
 */
static void CreateArrow(void)
{
    static const char *s_szArrow[11] = {
        "..........#", "..........#", "....####..#", ".....###..#",
        "....####..#", "...###.#..#", "...##.....#", "...#......#",
        "....#.....#", "..........#", "###########"
    };
    HDC hdcScreen = GetDC(NULL);
    HDC hdc = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmOld;
    int x, y;

    s_hbmArrow = CreateCompatibleBitmap(hdcScreen, 11, 11);
    ReleaseDC(NULL, hdcScreen);
    if (!s_hbmArrow) {
        DeleteDC(hdc);
        return;
    }
    hbmOld = SelectObject(hdc, s_hbmArrow);
    for (y = 0; y < 11; y++)
        for (x = 0; x < 11; x++)
            SetPixel(hdc, x, y, s_szArrow[y][x] == '#' ? RGB(0, 0, 0)
                                                       : RGB(255, 255, 255));
    SelectObject(hdc, hbmOld);
    DeleteDC(hdc);
}

void DrawShortcutArrow(HDC hdc, int x, int y)
{
    HDC hdcMem;
    HBITMAP hbmOld;

    if (!s_hbmArrow)
        CreateArrow();
    if (!s_hbmArrow)
        return;
    hdcMem = CreateCompatibleDC(hdc);
    hbmOld = SelectObject(hdcMem, s_hbmArrow);
    BitBlt(hdc, x, y, 11, 11, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
}

/* The arrow for 16x16 icons, shrunk to 7x7 */
void DrawShortcutArrowSmall(HDC hdc, int x, int y)
{
    HDC hdcMem;
    HBITMAP hbmOld;
    int nOldMode;

    if (!s_hbmArrow)
        CreateArrow();
    if (!s_hbmArrow)
        return;
    hdcMem = CreateCompatibleDC(hdc);
    hbmOld = SelectObject(hdcMem, s_hbmArrow);
    nOldMode = SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(hdc, x, y, 7, 7, hdcMem, 0, 0, 11, 11, SRCCOPY);
    SetStretchBltMode(hdc, nOldMode);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
}

static void PaintIcon(HWND hwnd, HDC hdc, DESKICON *p)
{
    RECT rc;
    COLORREF clrDesk = GetSysColor(COLOR_BACKGROUND);
    int nLum = (GetRValue(clrDesk) * 3 + GetGValue(clrDesk) * 6
                + GetBValue(clrDesk)) / 10;
    int xIcon;
    HFONT hfOld;

    GetClientRect(hwnd, &rc);
    xIcon = (rc.right - 32) / 2;
    DrawIcon(hdc, xIcon, 2, p->hIcon);
    if (p->bKind == DK_SHORTCUT && g_cfg.bShortcutArrows)
        DrawShortcutArrow(hdc, xIcon, 2 + 32 - 11);

    hfOld = SelectObject(hdc, s_hfTitle ? s_hfTitle : g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, nLum < 128 ? RGB(255, 255, 255) : RGB(0, 0, 0));
    rc.top = 37;
    DrawText(hdc, p->sc.szCaption, -1, &rc,
             DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, hfOld);
}

static void OpenDeskIcon(DESKICON *p)
{
    ShortcutRun(p->hwnd, &p->sc);
}

/* The folder a shortcut points to, if any */
static BOOL ShortcutFolder(DESKICON *p, LPSTR lpDir)
{
    LPCSTR lp = p->sc.szCmd;

    if (_fstrnicmp(lp, "$Folder ", 8) == 0 || _fstrnicmp(lp, "$Explore ", 9) == 0)
        lp += lp[1] == 'F' || lp[1] == 'f' ? 8 : 9;
    else if (lp[0] == '$')
        return FALSE;
    while (*lp == ' ')
        lp++;
    lstrcpyn(lpDir, lp, MAXPATHLEN);
    return PathIsDirectory(lpDir);
}

/* The program a shortcut runs, if it is one (its file, in lpFile) */
static BOOL ShortcutProgram(DESKICON *p, LPSTR lpFile)
{
    OFSTRUCT of;
    int i;

    if (p->bKind != DK_SHORTCUT || p->sc.szCmd[0] == '$')
        return FALSE;
    for (i = 0; p->sc.szCmd[i] && p->sc.szCmd[i] != ' ' && i < MAXPATHLEN - 1; i++)
        lpFile[i] = p->sc.szCmd[i];
    lpFile[i] = '\0';
    if (!IsProgramFile(lpFile) || OpenFile(lpFile, &of, OF_EXIST) == HFILE_ERROR)
        return FALSE;
    OemToAnsi(of.szPathName, lpFile);
    return TRUE;
}

int DesktopDropTarget(HWND hwnd, LPSTR lpDir)
{
    DESKICON *p = FindIcon(hwnd);

    if (!p)
        return DT_NONE;
    if (p->bKind == DK_BIN)
        return DT_BIN;
    if (p->bKind == DK_COMPUTER)
        return DT_NONE;
    if (ShortcutFolder(p, lpDir))
        return DT_FOLDER;
    if (ShortcutProgram(p, lpDir))
        return DT_PROGRAM;
    return DT_NONE;
}

static void SavePosition(DESKICON *p)
{
    RECT rc;
    char szPos[24];

    GetWindowRect(p->hwnd, &rc);
    wsprintf(szPos, "%d,%d", rc.left, rc.top);
    CfgSetStr("Desktop Positions", p->szKey, szPos);
}

static void SaveShortcut(DESKICON *p)
{
    char szValue[400];

    if (p->bKind == DK_SHORTCUT) {
        ShortcutFormat(&p->sc, szValue);
        CfgSetStr("Desktop", p->szKey, szValue);
    } else
        CfgSetStr("Desktop", p->bKind == DK_BIN ? "BinCaption" : "ComputerCaption",
                  p->sc.szCaption);
}

/* The nearest grid position */
static void Snap(int FAR *px, int FAR *py)
{
    int cyMax = GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight() - DI_CY;
    int cxMax = GetSystemMetrics(SM_CXSCREEN) - DI_CX;

    *px = GRID_X0 + (*px - GRID_X0 + GRID_CX / 2) / GRID_CX * GRID_CX;
    *py = GRID_Y0 + (*py - GRID_Y0 + GRID_CY / 2) / GRID_CY * GRID_CY;
    if (*px < GRID_X0)
        *px = GRID_X0;
    if (*py < GRID_Y0)
        *py = GRID_Y0;
    while (*px > cxMax && *px > GRID_X0)
        *px -= GRID_CX;
    while (*py > cyMax && *py > GRID_Y0)
        *py -= GRID_CY;
}

/* Whether an icon other than pExcept sits at x, y */
static BOOL Occupied(int x, int y, DESKICON *pExcept)
{
    RECT rc;
    int i;

    for (i = 0; i < s_nIcons; i++) {
        if (&s_icons[i] == pExcept)
            continue;
        GetWindowRect(s_icons[i].hwnd, &rc);
        if (abs(rc.left - x) < GRID_CX / 2 && abs(rc.top - y) < GRID_CY / 2)
            return TRUE;
    }
    return FALSE;
}

/* A free grid place at or after x, y, going down the columns */
static void FreePlace(int FAR *px, int FAR *py, DESKICON *pExcept)
{
    int cyMax = GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight() - DI_CY;
    int n;

    Snap(px, py);
    for (n = 0; n < 200 && Occupied(*px, *py, pExcept); n++) {
        *py += GRID_CY;
        if (*py > cyMax) {
            *py = GRID_Y0;
            *px += GRID_CX;
        }
    }
}

static void RemoveShortcut(DESKICON *p)
{
    int i = (int)(p - s_icons);

    CfgSetStr("Desktop", p->szKey, NULL);
    CfgSetStr("Desktop Positions", p->szKey, NULL);
    DestroyWindow(p->hwnd);
    for (; i < s_nIcons - 1; i++)
        s_icons[i] = s_icons[i + 1];
    s_nIcons--;
}

static BOOL ConfirmRemove(HWND hwnd, DESKICON *p)
{
    char szMsg[120];

    wsprintf(szMsg, "Remove the shortcut '%s' from the desktop?\n\n"
                    "What it points to is not deleted.", (LPSTR)p->sc.szCaption);
    return MessageBox(hwnd, szMsg, "Delete Shortcut", MB_YESNO | MB_ICONQUESTION) == IDYES;
}

static void Properties(DESKICON *p)
{
    SHORTCUT sc = p->sc;

    if (p->bKind == DK_BIN && GetKeyState(VK_SHIFT) >= 0) {
        BinPropertiesDialog(p->hwnd);
        return;
    }
    sc.bNoCommand = p->bKind != DK_SHORTCUT;
    if (!ShortcutDialog(p->hwnd, &sc, NULL))
        return;
    sc.bNoCommand = FALSE;
    p->sc = sc;
    SaveShortcut(p);
    if (p->bKind == DK_SHORTCUT)
        p->hIcon = ShortcutIconOf(&p->sc);
    SetWindowText(p->hwnd, p->sc.szCaption);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

/* Files dropped on an icon (from File Manager or a folder window) */
static void DropOnIcon(DESKICON *p, HGLOBAL hList)
{
    char szTarget[MAXPATHLEN], szCmd[MAXPATHLEN * 2];
    LPCSTR lp;
    int n = 0;

    switch (DesktopDropTarget(p->hwnd, szTarget)) {
    case DT_BIN:
        FileOpDelete(p->hwnd, hList, g_cfg.bDeleteToBin);
        break;
    case DT_FOLDER:
        lp = PathListFirst(hList);
        n = lp && (lp[0] & ~0x20) == (szTarget[0] & ~0x20);
        PathListUnlock(hList);
        if (GetKeyState(VK_CONTROL) < 0)
            n = FALSE;
        FileOpCopyMove(p->hwnd, hList, szTarget, n);
        break;
    case DT_PROGRAM:
        for (lp = PathListFirst(hList); lp && n < 8; lp = PathListNext(lp), n++) {
            wsprintf(szCmd, "%s %s", (LPSTR)szTarget, lp);
            LaunchCommand(p->hwnd, szCmd, p->sc.szDir, SW_SHOWNORMAL);
        }
        PathListUnlock(hList);
        break;
    }
}

static HGLOBAL DropList(HDROP hDrop)
{
    char szFile[MAXPATHLEN];
    HGLOBAL hList = NULL;
    UINT i, n;

    n = DragQueryFile(hDrop, (UINT)-1, NULL, 0);
    for (i = 0; i < n; i++) {
        DragQueryFile(hDrop, i, szFile, sizeof(szFile));
        AnsiUpper(szFile);
        PathListAdd(&hList, szFile);
    }
    return hList;
}

static void IconMenu(HWND hwnd, DESKICON *p, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    char szDir[MAXPATHLEN];

    AppendMenu(hMenu, MF_STRING, IDM_OPEN, "&Open");
    if (p->bKind == DK_BIN)
        AppendMenu(hMenu, MF_STRING | (BinIsEmpty() ? MF_GRAYED : 0), IDM_EMPTYBIN,
                   "&Empty Bin");
    else if (p->bKind == DK_COMPUTER || ShortcutFolder(p, szDir))
        AppendMenu(hMenu, MF_STRING, IDM_EXPLORE, "&Explore");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    if (p->bKind == DK_SHORTCUT)
        AppendMenu(hMenu, MF_STRING, IDM_DELETE, "&Delete");
    AppendMenu(hMenu, MF_STRING, IDM_PROPERTIES, "P&roperties");
    ClientToScreen(hwnd, &pt);
    /* the menu's WM_COMMAND comes back here */
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

/* The icon whose window lies under a screen point, other than pExcept */
static DESKICON *IconAt(POINT pt, DESKICON *pExcept)
{
    RECT rc;
    int i;

    for (i = 0; i < s_nIcons; i++) {
        if (&s_icons[i] == pExcept)
            continue;
        GetWindowRect(s_icons[i].hwnd, &rc);
        if (PtInRect(&rc, pt))
            return &s_icons[i];
    }
    return NULL;
}

static void EndDrag(HWND hwnd, DESKICON *p)
{
    DESKICON *pTarget;
    POINT pt;
    RECT rc;
    int x, y;

    GetCursorPos(&pt);
    pTarget = IconAt(pt, p);
    if (pTarget && pTarget->bKind == DK_BIN && p->bKind == DK_SHORTCUT) {
        /* a shortcut dropped on the Bin is removed */
        SetWindowPos(hwnd, NULL, s_rcBefore.left, s_rcBefore.top, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
        if (ConfirmRemove(hwnd, p))
            RemoveShortcut(p);
        return;
    }
    GetWindowRect(hwnd, &rc);
    x = rc.left;
    y = rc.top;
    if (g_cfg.bSnapToGrid)
        FreePlace(&x, &y, p);
    if (x != rc.left || y != rc.top)
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    SavePosition(p);
}

LRESULT CALLBACK NSEXPORT DeskIconWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam)
{
    DESKICON *p = FindIcon(hwnd);
    PAINTSTRUCT ps;
    POINT pt;
    RECT rc;

    switch (msg) {
    case WM_ERASEBKGND:
        PaintBackground(hwnd, (HDC)wParam);
        return 1;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        if (p)
            PaintIcon(hwnd, ps.hdc, p);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_CLOSE:
        PostMessage(g_hwndMain, WM_CLOSE, 0, 0L);
        return 0;

    case WM_WINDOWPOSCHANGING:
        /* stay below every other window */
        {
            WINDOWPOS FAR *lpwp = (WINDOWPOS FAR *)lParam;
            lpwp->hwndInsertAfter = HWND_BOTTOM;
            lpwp->flags &= ~SWP_NOZORDER;
        }
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        s_bPressed = TRUE;
        s_bDragging = FALSE;
        s_ptDown.x = LOWORD(lParam);
        s_ptDown.y = HIWORD(lParam);
        GetWindowRect(hwnd, &s_rcBefore);
        return 0;

    case WM_MOUSEMOVE:
        if (!s_bPressed)
            return 0;
        pt.x = (int)LOWORD(lParam);
        pt.y = (int)HIWORD(lParam);
        if (!s_bDragging
            && (abs(pt.x - s_ptDown.x) > DRAG_THRESHOLD
                || abs(pt.y - s_ptDown.y) > DRAG_THRESHOLD))
            s_bDragging = TRUE;
        if (s_bDragging) {
            GetWindowRect(hwnd, &rc);
            SetWindowPos(hwnd, NULL, rc.left + pt.x - s_ptDown.x,
                         rc.top + pt.y - s_ptDown.y, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (s_bPressed) {
            BOOL bDragged = s_bDragging;

            ReleaseCapture();
            s_bPressed = FALSE;
            s_bDragging = FALSE;
            if (bDragged && p)
                EndDrag(hwnd, p);
            else if (p && g_cfg.bSingleClick)
                OpenDeskIcon(p);
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (p && !g_cfg.bSingleClick)
            OpenDeskIcon(p);
        return 0;

    case WM_RBUTTONUP:
        if (p) {
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            IconMenu(hwnd, p, pt);
        }
        return 0;

    case WM_DROPFILES:
        if (p) {
            HGLOBAL hList = DropList((HDROP)wParam);

            DragFinish((HDROP)wParam);
            if (hList) {
                DropOnIcon(p, hList);
                GlobalFree(hList);
            }
        } else
            DragFinish((HDROP)wParam);
        return 0;

    case WM_COMMAND:
        if (!p)
            return 0;
        switch (wParam) {
        case IDM_OPEN:
            OpenDeskIcon(p);
            break;
        case IDM_EMPTYBIN:
            BinOpen();
            PostMessage(FindWindow(CLS_BIN, NULL), WM_COMMAND, CMD_BIN_EMPTY, 0L);
            break;
        case IDM_EXPLORE:
            {
                char szDir[MAXPATHLEN];

                if (p->bKind == DK_COMPUTER || !ShortcutFolder(p, szDir))
                    ExplorerOpen(NULL);
                else
                    ExplorerOpen(szDir);
            }
            break;
        case IDM_DELETE:
            if (p->bKind == DK_SHORTCUT && ConfirmRemove(hwnd, p))
                RemoveShortcut(p);
            break;
        case IDM_PROPERTIES:
            Properties(p);
            break;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static int ParseInt(LPSTR FAR *pp)
{
    LPSTR q = *pp;
    int n = 0, sign = 1;

    if (*q == '-') {
        sign = -1;
        q++;
    }
    while (*q >= '0' && *q <= '9')
        n = n * 10 + (*q++ - '0');
    *pp = q;
    return n * sign;
}

static DESKICON *AddIcon(LPCSTR lpKey, BYTE bKind, LPCSHORTCUT lpsc, HICON hIcon,
                         int x, int y)
{
    DESKICON *p;
    char szPos[24];
    LPSTR q;

    if (!s_icons)
        s_icons = (DESKICON *)malloc(MAX_DESKICONS * sizeof(DESKICON));
    if (!s_icons || s_nIcons >= MAX_DESKICONS)
        return NULL;
    p = &s_icons[s_nIcons];
    _fmemset(p, 0, sizeof(DESKICON));
    lstrcpyn(p->szKey, lpKey, sizeof(p->szKey));
    p->bKind = bKind;
    p->sc = *lpsc;
    p->hIcon = hIcon;

    if (x == CW_USEDEFAULT) {
        x = GRID_X0;
        y = GRID_Y0 + s_nIcons * GRID_CY;
        CfgGetStr("Desktop Positions", lpKey, "", szPos, sizeof(szPos));
        if (szPos[0]) {
            q = szPos;
            x = ParseInt(&q);
            if (*q == ',') {
                q++;
                y = ParseInt(&q);
            }
        } else
            FreePlace(&x, &y, NULL);
    }

    s_nIcons++;
    p->hwnd = CreateWindow(CLS_DESKICON, p->sc.szCaption, WS_POPUP,
                           x, y, DI_CX, DI_CY, g_hwndMain, NULL, g_hInst, NULL);
    if (!p->hwnd) {
        s_nIcons--;
        return NULL;
    }
    DragAcceptFiles(p->hwnd, TRUE);
    SetWindowPos(p->hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return p;
}

/* A caption for a new shortcut: the file name, capitalised */
static void CaptionFor(LPCSTR lpPath, LPSTR lpBuf, int cbBuf)
{
    char szName[MAXPATHLEN];
    LPSTR lpExt;

    lstrcpyn(szName, lpPath, sizeof(szName));
    if (lstrlen(szName) <= 3) {
        wsprintf(lpBuf, "Drive %c", szName[0] & ~0x20);
        return;
    }
    lpExt = PathExtension(szName);
    if (*lpExt && IsProgramFile(szName))
        lpExt[-1] = '\0';
    lstrcpyn(lpBuf, PathFileName(szName), cbBuf);
    AnsiLower(lpBuf + 1);
}

/* Makes a shortcut to a file, folder or drive at screen position x, y */
static BOOL AddShortcut(LPCSTR lpPath, int x, int y)
{
    SHORTCUT sc;
    DESKICON *p;
    char szKey[16], szValue[8];
    int n;

    _fmemset(&sc, 0, sizeof(sc));
    for (n = 1; n < 1000; n++) {
        wsprintf(szKey, "Shortcut%d", n);
        CfgGetStr("Desktop", szKey, "", szValue, sizeof(szValue));
        if (!szValue[0])
            break;
    }
    CaptionFor(lpPath, sc.szCaption, sizeof(sc.szCaption));
    if (PathIsDirectory(lpPath))
        wsprintf(sc.szCmd, "$Folder %s", lpPath);
    else {
        lstrcpyn(sc.szCmd, lpPath, sizeof(sc.szCmd));
        lstrcpyn(sc.szDir, lpPath, sizeof(sc.szDir));
        PathParent(sc.szDir);
    }
    FreePlace(&x, &y, NULL);
    p = AddIcon(szKey, DK_SHORTCUT, &sc, ShortcutIconOf(&sc), x, y);
    if (!p)
        return FALSE;
    SaveShortcut(p);
    SavePosition(p);
    return TRUE;
}

/* Shortcuts for a list of paths dropped at a screen point */
void DesktopAddShortcuts(HGLOBAL hList, POINT pt)
{
    LPCSTR lp;
    char szPath[MAXPATHLEN];
    int n = 0;

    if (!g_cfg.bDesktopIcons) {
        MessageBox(NULL, "Desktop icons are switched off in the Options.",
                   NS_APPNAME, MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (lp = PathListFirst(hList); lp && n < 16; lp = PathListNext(lp), n++) {
        lstrcpyn(szPath, lp, sizeof(szPath));
        if (!AddShortcut(szPath, pt.x - DI_CX / 2, pt.y - 16 + n * GRID_CY))
            break;
    }
    PathListUnlock(hList);
}

/* Files dropped from File Manager onto the wallpaper */
void DesktopDropFiles(HDROP hDrop)
{
    HGLOBAL hList = DropList(hDrop);
    POINT pt;

    DragQueryPoint(hDrop, &pt);
    DragFinish(hDrop);
    if (hList) {
        DesktopAddShortcuts(hList, pt);
        GlobalFree(hList);
    }
}

/*
 * The desktop colour of Windows 95 (teal) as the default: used while
 * WIN.INI has no [colors] Background= of its own, i.e. none was chosen in
 * Control Panel. Only for the session; nothing is written.
 */
void DesktopDefaultColor(void)
{
    char szValue[16];
    int nIndex = COLOR_BACKGROUND;
    COLORREF clr = RGB(0, 128, 128);

    if (GetProfileString("colors", "Background", "", szValue, sizeof(szValue)) == 0)
        SetSysColors(1, &nIndex, &clr);
}

BOOL DesktopCreate(void)
{
    char szKey[16], szValue[400];
    SHORTCUT sc;
    int i;

    if (!g_cfg.bDesktopIcons)
        return TRUE;

    if (!s_hfTitle)
        s_hfTitle = CreateIconTitleFont();
    s_lpfnPaintDesktop = (PAINTDESKTOPPROC)GetProcAddress(
        GetModuleHandle("USER"), "PaintDesktop");

    _fmemset(&sc, 0, sizeof(sc));
    CfgGetStr("Desktop", "ComputerCaption", "Computer", sc.szCaption, sizeof(sc.szCaption));
    lstrcpy(sc.szCmd, "$System");
    AddIcon("Computer", DK_COMPUTER, &sc, IconStock(IDI_COMPUTER), CW_USEDEFAULT, 0);
    CfgGetStr("Desktop", "BinCaption", "Bin", sc.szCaption, sizeof(sc.szCaption));
    lstrcpy(sc.szCmd, "$Bin");
    AddIcon("Bin", DK_BIN, &sc, IconStock(BinIsEmpty() ? IDI_BIN : IDI_BINFULL),
            CW_USEDEFAULT, 0);

    for (i = 1; i <= 999 && s_nIcons < MAX_DESKICONS; i++) {
        wsprintf(szKey, "Shortcut%d", i);
        CfgGetStr("Desktop", szKey, "", szValue, sizeof(szValue));
        if (szValue[0] == '\0') {
            if (i > 64)
                break;
            continue;
        }
        if (!_fstrchr(szValue, '|'))
            continue;
        ShortcutParse(szValue, &sc);
        AddIcon(szKey, DK_SHORTCUT, &sc, ShortcutIconOf(&sc), CW_USEDEFAULT, 0);
    }
    return TRUE;
}

void DesktopDestroy(void)
{
    int i;

    for (i = 0; i < s_nIcons; i++)
        if (s_icons[i].hwnd)
            DestroyWindow(s_icons[i].hwnd);
    s_nIcons = 0;
    if (s_hfTitle) {
        DeleteObject(s_hfTitle);
        s_hfTitle = NULL;
    }
    if (s_hbmArrow) {
        DeleteObject(s_hbmArrow);
        s_hbmArrow = NULL;
    }
}

/* Lines the icons up in columns on the left of the screen */
void DesktopArrange(void)
{
    int i, x = GRID_X0, y = GRID_Y0;
    int cyMax = GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight() - DI_CY;

    for (i = 0; i < s_nIcons; i++) {
        if (y > cyMax) {
            y = GRID_Y0;
            x += GRID_CX;
        }
        SetWindowPos(s_icons[i].hwnd, NULL, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
        SavePosition(&s_icons[i]);
        y += GRID_CY;
    }
}

/* Moves every icon to the nearest free place on the grid */
void DesktopLineUp(void)
{
    int i, x, y;

    for (i = 0; i < s_nIcons; i++)
        SetWindowPos(s_icons[i].hwnd, NULL, -1000, -1000, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    for (i = 0; i < s_nIcons; i++) {
        char szPos[24];
        LPSTR q = szPos;

        /* from the saved position, since the windows are out of the way */
        CfgGetStr("Desktop Positions", s_icons[i].szKey, "", szPos, sizeof(szPos));
        x = ParseInt(&q);
        y = GRID_Y0;
        if (*q == ',') {
            q++;
            y = ParseInt(&q);
        }
        FreePlace(&x, &y, &s_icons[i]);
        SetWindowPos(s_icons[i].hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        SavePosition(&s_icons[i]);
    }
}

void DesktopBinChanged(void)
{
    HICON hIcon = IconStock(BinIsEmpty() ? IDI_BIN : IDI_BINFULL);
    int i;

    for (i = 0; i < s_nIcons; i++)
        if (s_icons[i].bKind == DK_BIN && s_icons[i].hIcon != hIcon) {
            s_icons[i].hIcon = hIcon;
            InvalidateRect(s_icons[i].hwnd, NULL, TRUE);
        }
}

/* The Computer and Bin captions were changed elsewhere */
void DesktopCaptionsChanged(void)
{
    int i;

    for (i = 0; i < s_nIcons; i++) {
        if (s_icons[i].bKind == DK_SHORTCUT)
            continue;
        CfgGetStr("Desktop", s_icons[i].bKind == DK_BIN ? "BinCaption" : "ComputerCaption",
                  s_icons[i].bKind == DK_BIN ? "Bin" : "Computer",
                  s_icons[i].sc.szCaption, sizeof(s_icons[i].sc.szCaption));
        SetWindowText(s_icons[i].hwnd, s_icons[i].sc.szCaption);
        InvalidateRect(s_icons[i].hwnd, NULL, TRUE);
    }
}

/* Repaints the icons after a change of options (arrows) */
void DesktopRepaint(void)
{
    int i;

    for (i = 0; i < s_nIcons; i++)
        InvalidateRect(s_icons[i].hwnd, NULL, TRUE);
}

/* Context menu for a right click on the desktop background */
void DesktopMenu(int x, int y)
{
    HMENU hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, CMD_ARRANGEICONS, "&Arrange Icons");
    AppendMenu(hMenu, MF_STRING, CMD_LINEUPICONS, "&Line up Icons");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, CMD_CASCADE, "&Cascade Windows");
    AppendMenu(hMenu, MF_STRING, CMD_TILE, "Tile &Horizontally");
    AppendMenu(hMenu, MF_STRING, CMD_TILEVERT, "Tile &Vertically");
    AppendMenu(hMenu, MF_STRING, CMD_MINIMIZEALL, "&Minimize All Windows");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, CMD_TASKMAN, "&Task Manager...");
    AppendMenu(hMenu, MF_STRING, CMD_DESKTOPPROPS, "&Desktop Settings...");
    AppendMenu(hMenu, MF_STRING, CMD_OPTIONS, "NewShell16 &Options...");
    TrackPopupMenu(hMenu, 0, x, y, 0, g_hwndMain, NULL);
    DestroyMenu(hMenu);
}
