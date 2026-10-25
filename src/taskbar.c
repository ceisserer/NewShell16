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
 * taskbar.c - the taskbar: Start button, task buttons and clock
 *
 * Windows 3.1 has no shell hooks for window creation, so the list of tasks
 * is refreshed from a timer by enumerating the top-level windows.
 */
#include <string.h>
#include <dos.h>
#include "newshell.h"

#define MAX_TASKS       40
#define TIMER_REFRESH   1
#define REFRESH_MS      500
#define BTN_MAXWIDTH    160
#define BTN_MINWIDTH    40
#define BTN_TOP         4
#define GAP             3

typedef struct tagTASKBTN {
    HWND hwnd;
    char szTitle[80];
} TASKBTN;

static TASKBTN s_tasks[MAX_TASKS];
static int     s_nTasks;
static RECT    s_rcTask[MAX_TASKS];
static HWND    s_hwndActive;
static RECT    s_rcStart;
static RECT    s_rcClock;
static char    s_szClock[16];
static BOOL    s_bStartDown;
static int     s_cyBar;
static HWND    s_hwndSysMenuTarget;
static HBRUSH  s_hbrChecker;

/* collected by EnumTaskProc */
static HWND    s_hwndFound[MAX_TASKS];
static int     s_nFound;

int TaskbarHeight(void)
{
    return s_cyBar;
}

static BOOL IsOwnHelperWindow(HWND hwnd)
{
    char szClass[32];

    GetClassName(hwnd, szClass, sizeof(szClass));
    return lstrcmpi(szClass, CLS_TASKBAR) == 0
        || lstrcmpi(szClass, CLS_DESKICON) == 0
        || lstrcmpi(szClass, CLS_MAIN) == 0;
}

static BOOL IsTaskWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd))
        return FALSE;
    if (GetWindow(hwnd, GW_OWNER) != NULL)
        return FALSE;
    if (GetWindowTextLength(hwnd) == 0)
        return FALSE;
    return !IsOwnHelperWindow(hwnd);
}

BOOL CALLBACK NSEXPORT EnumTaskProc(HWND hwnd, LPARAM lParam)
{
    (void)lParam;
    if (s_nFound < MAX_TASKS && IsTaskWindow(hwnd))
        s_hwndFound[s_nFound++] = hwnd;
    return TRUE;
}

/* The task button for the active window, following owner chains */
static HWND ActiveTaskWindow(void)
{
    HWND hwnd = GetActiveWindow();
    HWND hwndOwner;

    while (hwnd && (hwndOwner = GetWindow(hwnd, GW_OWNER)) != NULL)
        hwnd = hwndOwner;
    return hwnd;
}

/*
 * Moves a minimised window's icon (and its title) below the screen. As in
 * Calmira, the window is hidden first and re-shown minimised through
 * SetWindowPlacement so the desktop underneath is repainted.
 */
static void HideIconicWindow(HWND hwnd)
{
    WINDOWPLACEMENT wp;
    int cyScreen = GetSystemMetrics(SM_CYSCREEN);

    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp))
        return;
    if (wp.ptMinPosition.x == 0 && wp.ptMinPosition.y == cyScreen)
        return;
    wp.ptMinPosition.x = 0;
    wp.ptMinPosition.y = cyScreen;
    wp.flags |= WPF_SETMINPOSITION;
    wp.showCmd = SW_SHOWMINNOACTIVE;
    ShowWindow(hwnd, SW_HIDE);
    SetWindowPlacement(hwnd, &wp);
}

/* Rebuilds the task list; returns TRUE if anything visible changed */
static BOOL UpdateTasks(void)
{
    static FARPROC lpfnEnum;
    TASKBTN newTasks[MAX_TASKS];
    int nNew = 0, i, j;
    BOOL bChanged = FALSE;
    HWND hwndActive;

    if (!lpfnEnum)
        lpfnEnum = MakeProcInstance((FARPROC)EnumTaskProc, g_hInst);
    s_nFound = 0;
    EnumWindows((WNDENUMPROC)lpfnEnum, 0L);

    /* keep the existing order, drop windows that went away */
    for (i = 0; i < s_nTasks; i++)
        for (j = 0; j < s_nFound; j++)
            if (s_hwndFound[j] == s_tasks[i].hwnd) {
                newTasks[nNew++].hwnd = s_tasks[i].hwnd;
                s_hwndFound[j] = NULL;
                break;
            }
    /* append new windows; EnumWindows lists them top of Z-order first */
    for (j = s_nFound - 1; j >= 0; j--)
        if (s_hwndFound[j] && nNew < MAX_TASKS)
            newTasks[nNew++].hwnd = s_hwndFound[j];

    for (i = 0; i < nNew; i++) {
        GetWindowText(newTasks[i].hwnd, newTasks[i].szTitle,
                      sizeof(newTasks[i].szTitle));
        if (g_cfg.bHideMinApps && IsIconic(newTasks[i].hwnd))
            HideIconicWindow(newTasks[i].hwnd);
    }

    if (nNew != s_nTasks)
        bChanged = TRUE;
    else
        for (i = 0; i < nNew; i++)
            if (newTasks[i].hwnd != s_tasks[i].hwnd
                || lstrcmp(newTasks[i].szTitle, s_tasks[i].szTitle) != 0) {
                bChanged = TRUE;
                break;
            }
    if (bChanged) {
        _fmemcpy(s_tasks, newTasks, nNew * sizeof(TASKBTN));
        s_nTasks = nNew;
    }

    hwndActive = ActiveTaskWindow();
    if (hwndActive != s_hwndActive) {
        s_hwndActive = hwndActive;
        bChanged = TRUE;
    }
    return bChanged;
}

static BOOL UpdateClock(void)
{
    struct dostime_t t;
    char szSep[4], szAM[8], szPM[8], szNew[16];
    int nHour;

    if (!g_cfg.bClock)
        return FALSE;
    _dos_gettime(&t);
    GetProfileString("intl", "sTime", ":", szSep, sizeof(szSep));
    if (GetProfileInt("intl", "iTime", 0)) {
        wsprintf(szNew, "%d%s%02d", t.hour, (LPSTR)szSep, t.minute);
    } else {
        GetProfileString("intl", "s1159", "AM", szAM, sizeof(szAM));
        GetProfileString("intl", "s2359", "PM", szPM, sizeof(szPM));
        nHour = t.hour % 12;
        if (nHour == 0)
            nHour = 12;
        wsprintf(szNew, "%d%s%02d %s", nHour, (LPSTR)szSep, t.minute,
                 (LPSTR)(t.hour < 12 ? szAM : szPM));
    }
    if (lstrcmp(szNew, s_szClock) == 0)
        return FALSE;
    lstrcpy(s_szClock, szNew);
    return TRUE;
}

static void Layout(HWND hwnd, HDC hdc)
{
    RECT rc;
    HFONT hfOld;
    int xLeft, xRight, cxBtn, i, cyBtn;

    GetClientRect(hwnd, &rc);
    cyBtn = rc.bottom - BTN_TOP - 2;

    hfOld = SelectObject(hdc, g_hfontBold);
    SetRect(&s_rcStart, 2, BTN_TOP, 2 + 30 + TextWidth(hdc, "Start"),
            BTN_TOP + cyBtn);
    SelectObject(hdc, g_hfontUI);

    if (g_cfg.bClock) {
        int cx = TextWidth(hdc, "88:88 MM") + 12;
        SetRect(&s_rcClock, rc.right - 2 - cx, BTN_TOP, rc.right - 2,
                BTN_TOP + cyBtn);
    } else
        SetRect(&s_rcClock, rc.right, BTN_TOP, rc.right, BTN_TOP + cyBtn);
    SelectObject(hdc, hfOld);

    xLeft = s_rcStart.right + GAP + 2;
    xRight = s_rcClock.left - GAP - 2;
    if (s_nTasks > 0) {
        cxBtn = (xRight - xLeft + GAP) / s_nTasks - GAP;
        if (cxBtn > BTN_MAXWIDTH)
            cxBtn = BTN_MAXWIDTH;
        if (cxBtn < BTN_MINWIDTH)
            cxBtn = BTN_MINWIDTH;
        for (i = 0; i < s_nTasks; i++) {
            SetRect(&s_rcTask[i], xLeft, BTN_TOP, xLeft + cxBtn,
                    BTN_TOP + cyBtn);
            xLeft += cxBtn + GAP;
        }
    }
}

static void DrawButton(HDC hdc, const RECT FAR *lprc, BOOL bDown, BOOL bChecked)
{
    RECT rc = *lprc;

    InflateRect(&rc, -2, -2);
    if (bChecked && s_hbrChecker) {
        COLORREF clrText = SetTextColor(hdc, GetSysColor(COLOR_BTNFACE));
        COLORREF clrBk = SetBkColor(hdc, GetSysColor(COLOR_BTNHIGHLIGHT));
        FillRect(hdc, &rc, s_hbrChecker);
        SetTextColor(hdc, clrText);
        SetBkColor(hdc, clrBk);
    } else
        FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    Draw3DEdge(hdc, lprc, bDown, TRUE);
}

static void PaintTaskbar(HWND hwnd, HDC hdcWin)
{
    HDC hdc;
    HBITMAP hbm, hbmOld;
    HFONT hfOld;
    RECT rc, rcEdge;
    char szText[80];
    int i, off;

    GetClientRect(hwnd, &rc);
    hdc = CreateCompatibleDC(hdcWin);
    hbm = CreateCompatibleBitmap(hdcWin, rc.right, rc.bottom);
    hbmOld = SelectObject(hdc, hbm);
    Layout(hwnd, hdc);

    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    SetRect(&rcEdge, -2, 0, rc.right + 2, rc.bottom + 2);
    Draw3DEdge(hdc, &rcEdge, FALSE, TRUE);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));

    /* Start button */
    DrawButton(hdc, &s_rcStart, s_bStartDown, FALSE);
    off = s_bStartDown ? 1 : 0;
    DrawSmallImage(hdc, s_rcStart.left + 5 + off,
                   s_rcStart.top + (s_rcStart.bottom - s_rcStart.top - 16) / 2 + off,
                   SMI_START);
    hfOld = SelectObject(hdc, g_hfontBold);
    SetRect(&rcEdge, s_rcStart.left + 23 + off, s_rcStart.top + off,
            s_rcStart.right + off, s_rcStart.bottom + off);
    DrawText(hdc, "Start", -1, &rcEdge, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Task buttons */
    for (i = 0; i < s_nTasks; i++) {
        BOOL bActive = (s_tasks[i].hwnd == s_hwndActive);
        HICON hIcon;
        RECT rcB = s_rcTask[i];
        int xText;

        DrawButton(hdc, &rcB, bActive, bActive);
        off = bActive ? 1 : 0;

        hIcon = (HICON)GetClassWord(s_tasks[i].hwnd, GCW_HICON);
        if (!hIcon)
            hIcon = IconStock(IDI_APP);
        xText = rcB.left + 6 + off;
        if (rcB.right - rcB.left >= 48) {
            DrawIconSmall(hdc, rcB.left + 5 + off,
                          rcB.top + (rcB.bottom - rcB.top - 16) / 2 + off,
                          hIcon, GetSysColor(COLOR_BTNFACE));
            xText += 19;
        }
        SelectObject(hdc, bActive ? g_hfontBold : g_hfontUI);
        FitText(hdc, s_tasks[i].szTitle, szText, sizeof(szText),
                rcB.right - xText - 5);
        SetRect(&rcEdge, xText, rcB.top + off, rcB.right - 4, rcB.bottom + off);
        DrawText(hdc, szText, -1, &rcEdge,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    /* Clock */
    if (g_cfg.bClock) {
        SelectObject(hdc, g_hfontUI);
        Draw3DEdge(hdc, &s_rcClock, TRUE, FALSE);
        DrawText(hdc, s_szClock, -1, &s_rcClock,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, hfOld);
    BitBlt(hdcWin, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdc);
}

static void ShowStartMenu(HWND hwnd)
{
    POINT pt;

    s_bStartDown = TRUE;
    InvalidateRect(hwnd, &s_rcStart, FALSE);
    UpdateWindow(hwnd);

    pt.x = s_rcStart.left;
    pt.y = 0;
    ClientToScreen(hwnd, &pt);
    StartMenuShow(hwnd, pt.x, pt.y);

    s_bStartDown = FALSE;
    InvalidateRect(hwnd, &s_rcStart, FALSE);
}

static void ActivateTask(HWND hwnd)
{
    HWND hwndPopup;

    if (!IsWindow(hwnd))
        return;
    if (hwnd == s_hwndActive && !IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_MINIMIZE);
    } else {
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        hwndPopup = GetLastActivePopup(hwnd);
        if (!hwndPopup || !IsWindowVisible(hwndPopup))
            hwndPopup = hwnd;
        BringWindowToTop(hwndPopup);
        SetActiveWindow(hwndPopup);
    }
    if (UpdateTasks())
        InvalidateRect(g_hwndTaskbar, NULL, FALSE);
}

static void TaskSystemMenu(HWND hwnd, HWND hwndTask, POINT pt)
{
    HMENU hMenu = GetSystemMenu(hwndTask, FALSE);
    BOOL bIconic = IsIconic(hwndTask);
    BOOL bZoomed = IsZoomed(hwndTask);

    if (!hMenu)
        return;
    EnableMenuItem(hMenu, SC_RESTORE, (bIconic || bZoomed) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, SC_MOVE, (bIconic || bZoomed) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, SC_SIZE, (bIconic || bZoomed) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, SC_MINIMIZE, bIconic ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, SC_MAXIMIZE, bZoomed ? MF_GRAYED : MF_ENABLED);

    s_hwndSysMenuTarget = hwndTask;
    ClientToScreen(hwnd, &pt);
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
}

static void TaskbarContextMenu(HWND hwnd, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, CMD_CASCADE, "&Cascade Windows");
    AppendMenu(hMenu, MF_STRING, CMD_TILE, "Tile &Horizontally");
    AppendMenu(hMenu, MF_STRING, CMD_TILEVERT, "Tile &Vertically");
    AppendMenu(hMenu, MF_STRING, CMD_MINIMIZEALL, "&Minimize All Windows");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, CMD_TASKMAN, "&Task Manager...");
    AppendMenu(hMenu, MF_STRING, CMD_OPTIONS, "&Options...");
    AppendMenu(hMenu, MF_STRING, CMD_ABOUT, "&About NewShell...");
    if (!g_bIsShell) {
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, CMD_CLOSESHELL, "C&lose NewShell");
    }
    ClientToScreen(hwnd, &pt);
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void TasksMinimizeAll(void)
{
    int i;

    UpdateTasks();
    for (i = 0; i < s_nTasks; i++)
        if (!IsIconic(s_tasks[i].hwnd))
            ShowWindow(s_tasks[i].hwnd, SW_MINIMIZE);
}

/* Copies the windows on the taskbar into lpOut; returns their number */
int TasksGetList(HWND FAR *lpOut, int nMax)
{
    int i;

    UpdateTasks();
    for (i = 0; i < s_nTasks && i < nMax; i++)
        lpOut[i] = s_tasks[i].hwnd;
    return i;
}

static int HitTask(POINT pt)
{
    int i;

    for (i = 0; i < s_nTasks; i++)
        if (PtInRect(&s_rcTask[i], pt))
            return i;
    return -1;
}

LRESULT CALLBACK NSEXPORT TaskbarWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam)
{
    PAINTSTRUCT ps;
    POINT pt;
    int i;

    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, TIMER_REFRESH, REFRESH_MS, NULL);
        UpdateTasks();
        UpdateClock();
        return 0;

    case WM_TIMER:
        i = UpdateTasks();
        i |= UpdateClock();
        if (i)
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        PaintTaskbar(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_CLOSE:
        /* Alt+F4 while the taskbar happens to be active: like Windows 95 */
        PostMessage(g_hwndMain, WM_CLOSE, 0, 0L);
        return 0;

    case WM_LBUTTONDOWN:
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        if (PtInRect(&s_rcStart, pt)) {
            ShowStartMenu(hwnd);
        } else if ((i = HitTask(pt)) >= 0) {
            ActivateTask(s_tasks[i].hwnd);
        }
        return 0;

    case WM_RBUTTONUP:
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        if ((i = HitTask(pt)) >= 0)
            TaskSystemMenu(hwnd, s_tasks[i].hwnd, pt);
        else
            TaskbarContextMenu(hwnd, pt);
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
        if (wParam >= 0xF000) {
            /* chosen from a task's system menu */
            if (s_hwndSysMenuTarget && IsWindow(s_hwndSysMenuTarget))
                PostMessage(s_hwndSysMenuTarget, WM_SYSCOMMAND, wParam, 0L);
            s_hwndSysMenuTarget = NULL;
        } else if (wParam == CMD_STARTMENU) {
            ShowStartMenu(hwnd);
        } else {
            PostMessage(g_hwndMain, WM_COMMAND, wParam, 0L);
        }
        return 0;

    case WM_SYSCOLORCHANGE:
    case WM_WININICHANGE:
        s_szClock[0] = '\0';
        UpdateClock();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_REFRESH);
        if (s_hbrChecker) {
            DeleteObject(s_hbrChecker);
            s_hbrChecker = NULL;
        }
        g_hwndTaskbar = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void TaskbarRefresh(void)
{
    if (g_hwndTaskbar && UpdateTasks())
        InvalidateRect(g_hwndTaskbar, NULL, FALSE);
}

BOOL TaskbarCreate(void)
{
    static WORD wChecker[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
    HBITMAP hbm;
    TEXTMETRIC tm;
    HDC hdc;
    HFONT hfOld;
    int cxScreen = GetSystemMetrics(SM_CXSCREEN);
    int cyScreen = GetSystemMetrics(SM_CYSCREEN);

    hbm = CreateBitmap(8, 8, 1, 1, wChecker);
    s_hbrChecker = CreatePatternBrush(hbm);
    DeleteObject(hbm);

    hdc = GetDC(NULL);
    hfOld = SelectObject(hdc, g_hfontUI);
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);
    s_cyBar = tm.tmHeight + 15;
    if (s_cyBar < 28)
        s_cyBar = 28;

    g_hwndTaskbar = CreateWindowEx(WS_EX_TOPMOST, CLS_TASKBAR, "", WS_POPUP,
                                   0, cyScreen - s_cyBar, cxScreen, s_cyBar,
                                   g_hwndMain, NULL, g_hInst, NULL);
    if (!g_hwndTaskbar)
        return FALSE;
    ShowWindow(g_hwndTaskbar, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwndTaskbar);
    return TRUE;
}
