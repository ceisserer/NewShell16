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
 * A folder window is a frame holding a folder view (fview.c), a menu bar,
 * a toolbar and a status bar (both can be switched off in the View menu,
 * [Folders] Toolbar= and StatusBar=). The Computer window is a folder
 * window for path "".
 */
#include <string.h>
#include "newshell.h"

#define MAX_FOLDERS     48

static HWND s_hwndFolders[MAX_FOLDERS];
static int  s_nFolders;
static int  s_cyStatus;

#define ID_TOOLBAR      2

static HWND ViewOf(HWND hwnd)
{
    return GetDlgItem(hwnd, 1);
}

static HWND ToolbarOf(HWND hwnd)
{
    return GetDlgItem(hwnd, ID_TOOLBAR);
}

static int BarTop(void)
{
    return g_cfg.bFolderToolbar ? ToolbarHeight() : 0;
}

static int BarBottom(void)
{
    return g_cfg.bFolderStatusBar ? StatusBarHeight() : 0;
}

static void Layout(HWND hwnd)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    if (ToolbarOf(hwnd)) {
        ShowWindow(ToolbarOf(hwnd), g_cfg.bFolderToolbar ? SW_SHOWNA : SW_HIDE);
        MoveWindow(ToolbarOf(hwnd), 0, 0, rc.right, ToolbarHeight(), TRUE);
    }
    MovePane(ViewOf(hwnd), 0, BarTop(), rc.right, rc.bottom - BarTop() - BarBottom());
    InvalidateRect(hwnd, NULL, TRUE);
}

static void SetTitle(HWND hwnd)
{
    char szTitle[MAXPATHLEN + 32];
    LPCSTR lpPath = FViewGetPath(ViewOf(hwnd));
    LPCSTR lpFilter = FViewGetFilter(ViewOf(hwnd));

    if (!*lpPath)
        CfgGetStr("Desktop", "ComputerCaption", "Computer", szTitle, 40);
    else if (lstrcmp(lpFilter, "*.*") != 0)
        wsprintf(szTitle, "%s (%s)", lpPath, lpFilter);
    else
        lstrcpy(szTitle, lpPath);
    SetWindowText(hwnd, szTitle);
}

/*
 * Paints the status bar at the bottom of a frame window. A tab in lpText
 * splits it into two fields; the first is xPane wide (0: 40 % of the
 * window).
 */
void PaintStatusBar(HWND hwnd, HDC hdc, int cyStatus, LPCSTR lpText, int xPane)
{
    RECT rc, rcField;
    HFONT hfOld;
    char szText[128];
    LPSTR lpSecond;

    lstrcpyn(szText, lpText, sizeof(szText));
    lpSecond = _fstrchr(szText, '\t');
    if (lpSecond)
        *lpSecond++ = '\0';
    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - cyStatus;
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    InflateRect(&rc, -2, -2);
    rc.bottom++;
    if (xPane <= 0)
        xPane = rc.right * 2 / 5 < 120 ? 120 : rc.right * 2 / 5;
    hfOld = SelectObject(hdc, g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    rcField = rc;
    if (lpSecond && xPane < rc.right - 40)
        rcField.right = xPane;
    Draw3DEdge(hdc, &rcField, TRUE, FALSE);
    InflateRect(&rcField, -4, 0);
    DrawText(hdc, szText, -1, &rcField,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (lpSecond && xPane < rc.right - 40) {
        rcField = rc;
        rcField.left = xPane + 2;
        Draw3DEdge(hdc, &rcField, TRUE, FALSE);
        InflateRect(&rcField, -4, 0);
        DrawText(hdc, lpSecond, -1, &rcField,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
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

    ToolbarSyncView(ToolbarOf(hwnd), ViewOf(hwnd));
    if (!g_cfg.bFolderStatusBar)
        return;
    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - StatusBarHeight();
    InvalidateRect(hwnd, &rc, FALSE);
}

/* View > Toolbar / Status Bar: every folder window follows */
void FolderBarsChanged(void)
{
    int i;

    for (i = 0; i < s_nFolders; i++) {
        Layout(s_hwndFolders[i]);
        ToolbarSyncView(ToolbarOf(s_hwndFolders[i]), ViewOf(s_hwndFolders[i]));
    }
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

            HWND hwndTb;

            if (!FViewCreate(hwnd, (LPCSTR)lpcs->lpCreateParams))
                return -1;
            hwndTb = ToolbarCreate(hwnd, g_aFolderButtons, g_nFolderButtons);
            if (hwndTb)
                SetWindowWord(hwndTb, GWW_ID, ID_TOOLBAR);
            ToolbarSyncView(hwndTb, ViewOf(hwnd));
            SetTitle(hwnd);
            if (s_nFolders < MAX_FOLDERS)
                s_hwndFolders[s_nFolders++] = hwnd;
        }
        return 0;

    case WM_SIZE:
        Layout(hwnd);
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            DrawPaneEdge(ps.hdc, 0, BarTop(), rc.right, rc.bottom - BarTop() - BarBottom());
        }
        if (g_cfg.bFolderStatusBar) {
            FViewGetStatus(ViewOf(hwnd), szStatus, sizeof(szStatus));
            PaintStatusBar(hwnd, ps.hdc, StatusBarHeight(), szStatus, 0);
        }
        EndPaint(hwnd, &ps);
        return 0;

    case WM_ACTIVATE:
        if (wParam != WA_INACTIVE)
            ToolbarSyncView(ToolbarOf(hwnd), ViewOf(hwnd));
        break;

    case WM_SETFOCUS:
        SetFocus(ViewOf(hwnd));
        return 0;

    case WM_INITMENUPOPUP:
        FViewInitMenu(ViewOf(hwnd), (HMENU)wParam);
        CheckMenuItem((HMENU)wParam, CMD_F_TOOLBAR,
                      g_cfg.bFolderToolbar ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem((HMENU)wParam, CMD_F_STATUSBAR,
                      g_cfg.bFolderStatusBar ? MF_CHECKED : MF_UNCHECKED);
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
        case CMD_F_TOOLBAR:
            g_cfg.bFolderToolbar = !g_cfg.bFolderToolbar;
            CfgSave();
            FolderBarsChanged();
            return 0;
        case CMD_F_STATUSBAR:
            g_cfg.bFolderStatusBar = !g_cfg.bFolderStatusBar;
            CfgSave();
            FolderBarsChanged();
            return 0;
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
        if (FViewCommand(hwndView, wParam)) {
            ToolbarSyncView(ToolbarOf(hwnd), hwndView);
            return 0;
        }
        if (StartMenuCommand(hwnd, wParam))
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

/* The folder windows (and File System windows) into lpOut */
int FolderGetWindows(HWND FAR *lpOut, int nMax)
{
    int i, n = 0;

    for (i = 0; i < s_nFolders && n < nMax; i++)
        lpOut[n++] = s_hwndFolders[i];
    return n + ExplorerGetWindows(lpOut + n, nMax - n);
}

void FolderCloseAll(void)
{
    while (s_nFolders > 0)
        DestroyWindow(s_hwndFolders[s_nFolders - 1]);
}
