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
 * dragged; positions are saved in NEWSHELL.INI. Besides the built-in
 * Computer and Bin icons, shortcuts can be configured (commands may be
 * Start menu macros such as "$Folder C:\DOCS"):
 *
 *   [Desktop]
 *   Shortcut1=Notepad|NOTEPAD.EXE
 *
 * Dropping files on the Bin deletes them; dropping them on a shortcut to
 * a folder copies or moves them there.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define MAX_DESKICONS   32
#define DI_CX           76
#define DI_CY           62
#define DRAG_THRESHOLD  4

typedef struct tagDESKICON {
    HWND  hwnd;
    char  szKey[16];        /* INI key for the position */
    char  szCaption[40];
    char  szCmd[128];       /* "" = Computer window */
    HICON hIcon;
} DESKICON;

static DESKICON s_icons[MAX_DESKICONS];
static int      s_nIcons;
static HFONT    s_hfTitle;      /* Windows' icon title font */

/* drag state */
static BOOL  s_bDragging, s_bPressed;
static POINT s_ptDown;

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

static void PaintIcon(HWND hwnd, HDC hdc, DESKICON *p)
{
    RECT rc;
    COLORREF clrDesk = GetSysColor(COLOR_BACKGROUND);
    int nLum = (GetRValue(clrDesk) * 3 + GetGValue(clrDesk) * 6
                + GetBValue(clrDesk)) / 10;
    HFONT hfOld;

    GetClientRect(hwnd, &rc);
    DrawIcon(hdc, (rc.right - 32) / 2, 2, p->hIcon);

    hfOld = SelectObject(hdc, s_hfTitle ? s_hfTitle : g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, nLum < 128 ? RGB(255, 255, 255) : RGB(0, 0, 0));
    rc.top = 37;
    DrawText(hdc, p->szCaption, -1, &rc,
             DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, hfOld);
}

static void OpenDeskIcon(DESKICON *p)
{
    if (p->szCmd[0] == '$')
        ExecMacro(p->hwnd, p->szCmd);
    else
        LaunchCommand(p->hwnd, p->szCmd, NULL, SW_SHOWNORMAL);
}

/* The folder a shortcut points to, if any */
static BOOL ShortcutFolder(DESKICON *p, LPSTR lpDir)
{
    LPCSTR lp = p->szCmd;

    if (_fstrnicmp(lp, "$Folder ", 8) == 0)
        lp += 8;
    else if (lp[0] == '$')
        return FALSE;
    lstrcpyn(lpDir, lp, MAXPATHLEN);
    return PathIsDirectory(lpDir);
}

int DesktopDropTarget(HWND hwnd, LPSTR lpDir)
{
    DESKICON *p = FindIcon(hwnd);

    if (!p)
        return DT_NONE;
    if (lstrcmpi(p->szCmd, "$Bin") == 0)
        return DT_BIN;
    if (ShortcutFolder(p, lpDir))
        return DT_FOLDER;
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
            ReleaseCapture();
            s_bPressed = FALSE;
            if (s_bDragging && p)
                SavePosition(p);
            s_bDragging = FALSE;
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (p)
            OpenDeskIcon(p);
        return 0;

    case WM_RBUTTONUP:
        if (p) {
            HMENU hMenu = CreatePopupMenu();

            AppendMenu(hMenu, MF_STRING, 1, "&Open");
            if (lstrcmpi(p->szCmd, "$Bin") == 0)
                AppendMenu(hMenu, MF_STRING | (BinIsEmpty() ? MF_GRAYED : 0), 2,
                           "&Empty Bin");
            else if (lstrcmpi(p->szCmd, "$System") == 0)
                AppendMenu(hMenu, MF_STRING, 3, "&Explore");
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            ClientToScreen(hwnd, &pt);
            /* the menu's WM_COMMAND comes back here */
            TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        if (wParam == 1 && p)
            OpenDeskIcon(p);
        else if (wParam == 2) {
            BinOpen();
            PostMessage(FindWindow(CLS_BIN, NULL), WM_COMMAND, CMD_BIN_EMPTY, 0L);
        } else if (wParam == 3)
            ExplorerOpen(NULL);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void AddIcon(LPCSTR lpKey, LPCSTR lpCaption, LPCSTR lpCmd, HICON hIcon)
{
    DESKICON *p;
    char szPos[24];
    int x, y, n;

    if (s_nIcons >= MAX_DESKICONS)
        return;
    n = s_nIcons;
    p = &s_icons[n];
    lstrcpyn(p->szKey, lpKey, sizeof(p->szKey));
    lstrcpyn(p->szCaption, lpCaption, sizeof(p->szCaption));
    lstrcpyn(p->szCmd, lpCmd, sizeof(p->szCmd));
    p->hIcon = hIcon;

    x = 8;
    y = 8 + n * (DI_CY + 8);
    CfgGetStr("Desktop Positions", lpKey, "", szPos, sizeof(szPos));
    if (szPos[0]) {
        LPSTR q = szPos;
        int sign = 1;

        x = 0;
        if (*q == '-') { sign = -1; q++; }
        while (*q >= '0' && *q <= '9')
            x = x * 10 + (*q++ - '0');
        x *= sign;
        if (*q == ',') {
            q++;
            sign = 1;
            y = 0;
            if (*q == '-') { sign = -1; q++; }
            while (*q >= '0' && *q <= '9')
                y = y * 10 + (*q++ - '0');
            y *= sign;
        }
    }

    s_nIcons++;
    p->hwnd = CreateWindow(CLS_DESKICON, lpCaption, WS_POPUP,
                           x, y, DI_CX, DI_CY, g_hwndMain, NULL, g_hInst, NULL);
    if (!p->hwnd) {
        s_nIcons--;
        return;
    }
    SetWindowPos(p->hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

/* Looks up the icon for a shortcut command (first word of the line) */
static HICON ShortcutIcon(LPCSTR lpCmd)
{
    char szFile[MAXPATHLEN];
    OFSTRUCT of;
    int i;

    if (_fstrnicmp(lpCmd, "$Folder", 7) == 0)
        return IconStock(IDI_FOLDER);
    if (lpCmd[0] == '$')
        return IconStock(IDI_APP);
    for (i = 0; lpCmd[i] && lpCmd[i] != ' ' && i < sizeof(szFile) - 1; i++)
        szFile[i] = lpCmd[i];
    szFile[i] = '\0';
    if (PathIsDirectory(szFile))
        return IconStock(IDI_FOLDER);
    if (OpenFile(szFile, &of, OF_EXIST) == HFILE_ERROR)
        return IconStock(IDI_APP);
    OemToAnsi(of.szPathName, szFile);
    return IconForFile(szFile, FALSE);
}

BOOL DesktopCreate(void)
{
    char szKey[16], szValue[200];
    LPSTR lpCmd;
    int i;

    if (!g_cfg.bDesktopIcons)
        return TRUE;

    if (!s_hfTitle)
        s_hfTitle = CreateIconTitleFont();
    s_lpfnPaintDesktop = (PAINTDESKTOPPROC)GetProcAddress(
        GetModuleHandle("USER"), "PaintDesktop");

    AddIcon("Computer", "Computer", "$System", IconStock(IDI_COMPUTER));
    AddIcon("Bin", "Bin", "$Bin", IconStock(BinIsEmpty() ? IDI_BIN : IDI_BINFULL));

    for (i = 1; i <= MAX_DESKICONS; i++) {
        wsprintf(szKey, "Shortcut%d", i);
        CfgGetStr("Desktop", szKey, "", szValue, sizeof(szValue));
        if (szValue[0] == '\0')
            continue;
        for (lpCmd = szValue; *lpCmd && *lpCmd != '|'; lpCmd++)
            ;
        if (*lpCmd != '|')
            continue;
        *lpCmd++ = '\0';
        AddIcon(szKey, szValue, lpCmd, ShortcutIcon(lpCmd));
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
}

/* Lines the icons up in a column on the left of the screen */
void DesktopArrange(void)
{
    int i, x = 8, y = 8;
    int cyMax = GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight() - DI_CY;

    for (i = 0; i < s_nIcons; i++) {
        if (y > cyMax) {
            y = 8;
            x += DI_CX + 8;
        }
        SetWindowPos(s_icons[i].hwnd, NULL, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
        SavePosition(&s_icons[i]);
        y += DI_CY + 8;
    }
}

void DesktopBinChanged(void)
{
    HICON hIcon = IconStock(BinIsEmpty() ? IDI_BIN : IDI_BINFULL);
    int i;

    for (i = 0; i < s_nIcons; i++)
        if (lstrcmpi(s_icons[i].szCmd, "$Bin") == 0 && s_icons[i].hIcon != hIcon) {
            s_icons[i].hIcon = hIcon;
            InvalidateRect(s_icons[i].hwnd, NULL, TRUE);
        }
}

/* Context menu for a right click on the desktop background */
void DesktopMenu(int x, int y)
{
    HMENU hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, CMD_ARRANGEICONS, "&Arrange Icons");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, CMD_CASCADE, "&Cascade Windows");
    AppendMenu(hMenu, MF_STRING, CMD_TILE, "Tile &Horizontally");
    AppendMenu(hMenu, MF_STRING, CMD_TILEVERT, "Tile &Vertically");
    AppendMenu(hMenu, MF_STRING, CMD_MINIMIZEALL, "&Minimize All Windows");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, CMD_TASKMAN, "&Task Manager...");
    AppendMenu(hMenu, MF_STRING, CMD_DESKTOPPROPS, "&Desktop Settings...");
    AppendMenu(hMenu, MF_STRING, CMD_OPTIONS, "NewShell &Options...");
    TrackPopupMenu(hMenu, 0, x, y, 0, g_hwndMain, NULL);
    DestroyMenu(hMenu);
}
