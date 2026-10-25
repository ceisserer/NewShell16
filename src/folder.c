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
 * folder.c - folder windows and the Computer window
 *
 * A folder window is a frame holding a folder view (fview.c), a menu bar
 * and a status bar. The Computer window is a folder window for path "".
 */
#include <string.h>
#include "newshell.h"

#define MAX_FOLDERS     48

static HWND s_hwndFolders[MAX_FOLDERS];
static int  s_nFolders;
static int  s_cyStatus;

static HWND ViewOf(HWND hwnd)
{
    return GetDlgItem(hwnd, 1);
}

static void SetTitle(HWND hwnd)
{
    LPCSTR lpPath = FViewGetPath(ViewOf(hwnd));
    SetWindowText(hwnd, *lpPath ? lpPath : "Computer");
}

/* Paints a one-field status bar at the bottom of a frame window */
void PaintStatusBar(HWND hwnd, HDC hdc, int cyStatus, LPCSTR lpText)
{
    RECT rc;
    HFONT hfOld;

    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - cyStatus;
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    InflateRect(&rc, -2, -2);
    rc.bottom++;
    Draw3DEdge(hdc, &rc, TRUE, FALSE);
    InflateRect(&rc, -4, 0);
    hfOld = SelectObject(hdc, g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    DrawText(hdc, lpText, -1, &rc,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hfOld);
}

int StatusBarHeight(void)
{
    if (!s_cyStatus) {
        TEXTMETRIC tm;
        HDC hdc = GetDC(NULL);
        HFONT hfOld = SelectObject(hdc, g_hfontUI);

        GetTextMetrics(hdc, &tm);
        SelectObject(hdc, hfOld);
        ReleaseDC(NULL, hdc);
        s_cyStatus = tm.tmHeight + 8;
    }
    return s_cyStatus;
}

static void InvalidateStatus(HWND hwnd)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - StatusBarHeight();
    InvalidateRect(hwnd, &rc, FALSE);
}

static void OpenSubfolder(HWND hwnd, LPCSTR lpPath)
{
    if (g_cfg.bFolderNewWindow)
        FolderOpen(lpPath);
    else
        FViewSetPath(ViewOf(hwnd), lpPath);
}

static void GoUp(HWND hwnd)
{
    char szPath[MAXPATHLEN];

    lstrcpyn(szPath, FViewGetPath(ViewOf(hwnd)), sizeof(szPath));
    if (szPath[0] == '\0')
        return;
    if (!PathParent(szPath))
        szPath[0] = '\0';               /* root: go to Computer */
    OpenSubfolder(hwnd, szPath);
}

static void RemoveFolder(HWND hwnd)
{
    int i;

    for (i = 0; i < s_nFolders; i++)
        if (s_hwndFolders[i] == hwnd) {
            s_hwndFolders[i] = s_hwndFolders[--s_nFolders];
            return;
        }
}

LRESULT CALLBACK NSEXPORT FolderWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam)
{
    PAINTSTRUCT ps;
    char szStatus[96];
    HWND hwndView;

    switch (msg) {
    case WM_CREATE:
        {
            CREATESTRUCT FAR *lpcs = (CREATESTRUCT FAR *)lParam;

            if (!FViewCreate(hwnd, (LPCSTR)lpcs->lpCreateParams))
                return -1;
            SetTitle(hwnd);
            if (s_nFolders < MAX_FOLDERS)
                s_hwndFolders[s_nFolders++] = hwnd;
        }
        return 0;

    case WM_SIZE:
        MovePane(ViewOf(hwnd), 0, 0, LOWORD(lParam),
                 HIWORD(lParam) - StatusBarHeight());
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            DrawPaneEdge(ps.hdc, 0, 0, rc.right, rc.bottom - StatusBarHeight());
        }
        FViewGetStatus(ViewOf(hwnd), szStatus, sizeof(szStatus));
        PaintStatusBar(hwnd, ps.hdc, StatusBarHeight(), szStatus);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SETFOCUS:
        SetFocus(ViewOf(hwnd));
        return 0;

    case WM_INITMENUPOPUP:
        FViewInitMenu(ViewOf(hwnd), (HMENU)wParam);
        return 0;

    case WM_NS_FVNOTIFY:
        switch (wParam) {
        case FVN_SELCHANGE:
            InvalidateStatus(hwnd);
            break;
        case FVN_OPENFOLDER:
            OpenSubfolder(hwnd, (LPCSTR)lParam);
            break;
        case FVN_GOUP:
            GoUp(hwnd);
            break;
        case FVN_PATHCHANGED:
            SetTitle(hwnd);
            InvalidateStatus(hwnd);
            break;
        case FVN_GONE:
            PostMessage(hwnd, WM_CLOSE, 0, 0L);
            break;
        }
        return 0;

    case WM_COMMAND:
        hwndView = ViewOf(hwnd);
        switch (wParam) {
        case CMD_F_COMPUTER:    FolderOpen(""); return 0;
        case CMD_F_CLOSE:       DestroyWindow(hwnd); return 0;
        case CMD_F_EXPLORE:
            if (FViewSelCount(hwndView) == 0) {
                ExplorerOpen(FViewGetPath(hwndView));
                return 0;
            }
            break;
        case CMD_ABOUT:         AboutDialog(hwnd); return 0;
        case CMD_FIND:          FindOpen(FViewGetPath(hwndView)); return 0;
        case CMD_RUN:           RunDialog(hwnd); return 0;
        }
        if (FViewCommand(hwndView, wParam) || StartMenuCommand(hwnd, wParam))
            return 0;
        break;

    case WM_DESTROY:
        RemoveFolder(hwnd);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL FolderRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = FolderWndProc;
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_FOLDER));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_FOLDER);
    wc.lpszClassName = CLS_FOLDER;
    return RegisterClass(&wc);
}

HWND FolderOpen(LPCSTR lpPath)
{
    char szPath[MAXPATHLEN];
    HWND hwnd;
    int i;

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    if (szPath[0])
        PathNormalize(szPath);

    /* activate an existing window for the same folder */
    for (i = 0; i < s_nFolders; i++) {
        if (lstrcmpi(FViewGetPath(ViewOf(s_hwndFolders[i])), szPath) == 0) {
            hwnd = s_hwndFolders[i];
            if (IsIconic(hwnd))
                ShowWindow(hwnd, SW_RESTORE);
            BringWindowToTop(hwnd);
            SetActiveWindow(hwnd);
            return hwnd;
        }
    }

    hwnd = CreateWindow(CLS_FOLDER, "", WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, 0, 440, 300, NULL, NULL, g_hInst,
                        (LPVOID)szPath);
    if (!hwnd)
        return NULL;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    TaskbarRefresh();
    return hwnd;
}

void FolderCloseAll(void)
{
    while (s_nFolders > 0)
        DestroyWindow(s_hwndFolders[s_nFolders - 1]);
}
