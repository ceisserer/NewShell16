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
 * taskbar.c - the taskbar: Start button, task buttons, tray and clock
 *
 * Windows 3.1 has no shell hooks for window creation, so the list of tasks
 * is refreshed from a timer by enumerating the top-level windows.
 *
 *   [Taskbar]
 *   Enabled=1          ; 0: no taskbar (Ctrl+Esc still opens the Start menu)
 *   AutoHide=0         ; slide away, back when the mouse reaches the bottom
 *   Exclude=           ; modules without a button, e.g. "CLOCK, WINPOPUP"
 *   Tray=              ; modules shown as small icons beside the clock
 *   DocFirst=0         ; "README.TXT - Notepad" instead of "Notepad - README.TXT"
 *   LowerDoc=0         ; ... with the document name in lower case
 *   MinAbove=0         ; minimised windows: icons above the bar (HideMinApps=0)
 *   Hints=1            ; full captions, date and resources as hints
 */
#include <string.h>
#include <dos.h>
#include "newshell.h"

#define MAX_TASKS       40
#define MAX_TRAY        12
#define TRAY_CX         20
#define TIMER_REFRESH   1
#define TIMER_LIFTMENU  2
#define TIMER_HIDE      3
#define TIMER_HINT      4
#define HIDE_DELAY      600
#define HIDDEN_CY       2           /* what stays visible of a hidden bar */
#define REFRESH_MS      500
#define BTN_MAXWIDTH    160
#define BTN_MINWIDTH    40
#define BTN_TOP         4
#define GAP             3

typedef struct tagTASKBTN {
    HWND  hwnd;
    HICON hIcon;
    char  szTitle[80];
} TASKBTN;

static TASKBTN s_tasks[MAX_TASKS];
static int     s_nTasks;
static RECT    s_rcTask[MAX_TASKS];
static HWND    s_hwndTray[MAX_TRAY];
static HICON   s_hTrayIcon[MAX_TRAY];
static int     s_nTray;
static RECT    s_rcTray;
static BOOL    s_bHidden;          /* auto-hide: slid away */
static int     s_nHintAt = -2;     /* what the hint is for: task, -1 clock */
static char    s_szExclude[128], s_szTrayList[128];
static HWND    s_hwndActive;
static RECT    s_rcStart;
static RECT    s_rcClock;
static char    s_szClock[16];
static BOOL    s_bStartDown;
static BOOL    s_bStartPress;      /* Start pressed, opening on the release */
static int     s_cyBar;
static HWND    s_hwndSysMenuTarget;
static BOOL    s_bLiftMenu;        /* move the next popup above the taskbar */
static HBRUSH  s_hbrChecker;

static void SlideBar(HWND hwnd, BOOL bShow);

/* collected by EnumTaskProc */
static HWND    s_hwndFound[MAX_TASKS];
static int     s_nFound;

/* The height the taskbar keeps free at the bottom of the screen */
int TaskbarHeight(void)
{
    if (!CfgGetInt("Taskbar", "Enabled", 1))
        return 0;
    if (CfgGetInt("Taskbar", "AutoHide", 0))
        return HIDDEN_CY;
    return s_cyBar;
}

/* The module name of a window ("NOTEPAD") */
static void WindowModule(HWND hwnd, LPSTR lpBuf, int cbBuf)
{
    char szPath[MAXPATHLEN];
    LPSTR lpDot;

    szPath[0] = '\0';
    GetModuleFileName((HINSTANCE)GetWindowWord(hwnd, GWW_HINSTANCE), szPath, sizeof(szPath));
    lstrcpyn(lpBuf, PathFileName(szPath), cbBuf);
    lpDot = _fstrchr(lpBuf, '.');
    if (lpDot)
        *lpDot = '\0';
}

/* Whether the window's module is in a list like "CLOCK, WINPOPUP.EXE" */
static BOOL ModuleListed(HWND hwnd, LPCSTR lpList)
{
    char szModule[16], szItem[16];
    LPSTR lpDot;
    int n;

    if (!*lpList)
        return FALSE;
    WindowModule(hwnd, szModule, sizeof(szModule));
    while (*lpList) {
        while (*lpList == ',' || *lpList == ' ' || *lpList == ';')
            lpList++;
        for (n = 0; *lpList && *lpList != ',' && *lpList != ' ' && *lpList != ';'; lpList++)
            if (n < sizeof(szItem) - 1)
                szItem[n++] = *lpList;
        szItem[n] = '\0';
        lpDot = _fstrchr(szItem, '.');
        if (lpDot)
            *lpDot = '\0';
        if (n && lstrcmpi(szItem, szModule) == 0)
            return TRUE;
    }
    return FALSE;
}

/* "Notepad - README.TXT" as "README.TXT - Notepad" (DocFirst=, LowerDoc=) */
static void ButtonTitle(LPSTR lpTitle, int cbTitle)
{
    char szTmp[80];
    LPSTR lpSep;

    if (!CfgGetInt("Taskbar", "DocFirst", 0) || !(lpSep = _fstrstr(lpTitle, " - ")))
        return;
    *lpSep = '\0';
    lstrcpyn(szTmp, lpSep + 3, sizeof(szTmp));
    if (CfgGetInt("Taskbar", "LowerDoc", 0))
        AnsiLower(szTmp);
    if (lstrlen(szTmp) + 3 < sizeof(szTmp)) {
        lstrcat(szTmp, " - ");
        lstrcpyn(szTmp + lstrlen(szTmp), lpTitle, sizeof(szTmp) - lstrlen(szTmp));
    }
    lstrcpyn(lpTitle, szTmp, cbTitle);
}

static BOOL IsOwnHelperWindow(HWND hwnd)
{
    char szClass[32];

    GetClassName(hwnd, szClass, sizeof(szClass));
    return lstrcmpi(szClass, CLS_TASKBAR) == 0
        || lstrcmpi(szClass, CLS_DESKICON) == 0
        || lstrcmpi(szClass, CLS_MAIN) == 0;
}

/*
 * Background programs that keep a minimised window only so that they can be
 * closed (the Retheme16 engine) get no task button and are not left active;
 * Task Manager still lists them.
 */
static BOOL IsBackgroundWindow(HWND hwnd)
{
    char szClass[32];

    GetClassName(hwnd, szClass, sizeof(szClass));
    return lstrcmpi(szClass, "Retheme16") == 0;
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

/*
 * MinAbove=1: the icon of a minimised window goes into slot nSlot of the
 * row above the taskbar, where Windows' own row at the bottom would be
 * covered by it.
 */
static void LiftIconicWindow(HWND hwnd, int nSlot)
{
    WINDOWPLACEMENT wp;
    int cx = GetSystemMetrics(SM_CXICONSPACING);
    int cy = GetSystemMetrics(SM_CYICONSPACING);
    int nPerRow = GetSystemMetrics(SM_CXSCREEN) / cx;
    int x, y;

    if (nPerRow < 1)
        nPerRow = 1;
    x = (nSlot % nPerRow) * cx + (cx - 32) / 2;
    y = GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight() - (nSlot / nPerRow + 1) * cy + 4;
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp))
        return;
    if (wp.ptMinPosition.x == x && wp.ptMinPosition.y == y)
        return;
    wp.ptMinPosition.x = x;
    wp.ptMinPosition.y = y;
    wp.flags |= WPF_SETMINPOSITION;
    wp.showCmd = SW_SHOWMINNOACTIVE;
    ShowWindow(hwnd, SW_HIDE);
    SetWindowPlacement(hwnd, &wp);
}

/*
 * The icon for a task button: the class icon, or for windows without one
 * (dialogs) the icon the window gives for WM_QUERYDRAGICON, like Windows
 * 95 does.
 */
static HICON TaskIcon(HWND hwnd)
{
    HICON hIcon = (HICON)GetClassWord(hwnd, GCW_HICON);

    if (!hIcon)
        hIcon = (HICON)LOWORD(SendMessage(hwnd, WM_QUERYDRAGICON, 0, 0L));
    return hIcon ? hIcon : IconStock(IDI_APP);
}

/* Rebuilds the task list; returns TRUE if anything visible changed */
static BOOL UpdateTasks(void)
{
    static FARPROC lpfnEnum;
    TASKBTN newTasks[MAX_TASKS];
    int nNew = 0, i, j;
    BOOL bChanged = FALSE;
    HWND hwndActive;

    HWND hwndTray[MAX_TRAY];
    int nTray = 0, nIconic = 0;

    if (!lpfnEnum)
        lpfnEnum = MakeProcInstance((FARPROC)EnumTaskProc, g_hInst);
    s_nFound = 0;
    EnumWindows((WNDENUMPROC)lpfnEnum, 0L);

    for (j = 0; j < s_nFound; j++)
        if (IsBackgroundWindow(s_hwndFound[j]) || ModuleListed(s_hwndFound[j], s_szExclude)) {
            if (g_cfg.bHideMinApps && IsIconic(s_hwndFound[j]))
                HideIconicWindow(s_hwndFound[j]);
            s_hwndFound[j] = NULL;
        } else if (ModuleListed(s_hwndFound[j], s_szTrayList)) {
            /* tray programs: an icon beside the clock, hidden when minimised */
            if (nTray < MAX_TRAY)
                hwndTray[nTray++] = s_hwndFound[j];
            if (IsIconic(s_hwndFound[j]))
                HideIconicWindow(s_hwndFound[j]);
            s_hwndFound[j] = NULL;
        }
    /* keep the tray's order */
    for (i = 0; i < nTray; i++)
        if (i >= s_nTray || hwndTray[i] != s_hwndTray[i])
            bChanged = TRUE;
    if (nTray != s_nTray)
        bChanged = TRUE;
    if (bChanged) {
        for (i = 0; i < nTray; i++) {
            s_hwndTray[i] = hwndTray[i];
            s_hTrayIcon[i] = TaskIcon(hwndTray[i]);
        }
        s_nTray = nTray;
    }

    /* keep the existing order, drop windows that went away */
    for (i = 0; i < s_nTasks; i++)
        for (j = 0; j < s_nFound; j++)
            if (s_hwndFound[j] == s_tasks[i].hwnd) {
                newTasks[nNew].hwnd = s_tasks[i].hwnd;
                newTasks[nNew++].hIcon = s_tasks[i].hIcon;
                s_hwndFound[j] = NULL;
                break;
            }
    /* append new windows; EnumWindows lists them top of Z-order first */
    for (j = s_nFound - 1; j >= 0; j--)
        if (s_hwndFound[j] && nNew < MAX_TASKS) {
            newTasks[nNew].hwnd = s_hwndFound[j];
            newTasks[nNew++].hIcon = TaskIcon(s_hwndFound[j]);
        }

    for (i = 0; i < nNew; i++) {
        GetWindowText(newTasks[i].hwnd, newTasks[i].szTitle,
                      sizeof(newTasks[i].szTitle));
        ButtonTitle(newTasks[i].szTitle, sizeof(newTasks[i].szTitle));
        if (IsIconic(newTasks[i].hwnd)) {
            if (g_cfg.bHideMinApps)
                HideIconicWindow(newTasks[i].hwnd);
            else if (CfgGetInt("Taskbar", "MinAbove", 0))
                LiftIconicWindow(newTasks[i].hwnd, nIconic++);
        }
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

    /* Windows activates the next window when a program closes, which can
     * be the background window: Alt+F4 would then close it unseen */
    hwndActive = GetActiveWindow();
    if (hwndActive && IsBackgroundWindow(hwndActive))
        SetActiveWindow(g_hwndMain);

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

    if (g_cfg.bClock || s_nTray) {
        int cx = (g_cfg.bClock ? TextWidth(hdc, "88:88 MM") + 12 : 6) + s_nTray * TRAY_CX;
        SetRect(&s_rcClock, rc.right - 2 - cx, BTN_TOP, rc.right - 2,
                BTN_TOP + cyBtn);
    } else
        SetRect(&s_rcClock, rc.right, BTN_TOP, rc.right, BTN_TOP + cyBtn);
    SetRect(&s_rcTray, s_rcClock.left + 2, s_rcClock.top,
            s_rcClock.left + 2 + s_nTray * TRAY_CX, s_rcClock.bottom);
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

        hIcon = s_tasks[i].hIcon;
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

    /* Tray and clock */
    if (g_cfg.bClock || s_nTray) {
        RECT rcTime = s_rcClock;

        SelectObject(hdc, g_hfontUI);
        Draw3DEdge(hdc, &s_rcClock, TRUE, FALSE);
        for (i = 0; i < s_nTray; i++)
            DrawIconSmall(hdc, s_rcTray.left + i * TRAY_CX + 2,
                          s_rcTray.top + (s_rcTray.bottom - s_rcTray.top - 16) / 2,
                          s_hTrayIcon[i], GetSysColor(COLOR_BTNFACE));
        rcTime.left = s_rcTray.right;
        if (g_cfg.bClock)
            DrawText(hdc, s_szClock, -1, &rcTime,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, hfOld);
    BitBlt(hdcWin, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdc);
}

/*
 * Windows 3.1 closes a popup menu opened on a button press when the button
 * is released outside the menu, i.e. on the Start button. While the menu
 * is open, a message filter hook swallows that first release when it is
 * still over the Start button, so the menu stays open as in Windows 95;
 * dragging onto an item and releasing there still chooses it. A press on
 * the Start button closes the menu and is then passed on to the taskbar,
 * which must not open the menu again for it.
 */
static HHOOK   s_hhkStart;
static RECT    s_rcStartScreen;
static BOOL    s_bStartRelease;    /* the release of the opening press is due */
static DWORD   s_dwCloseTime;      /* time of the press that closed the menu */

LRESULT CALLBACK NSEXPORT StartMenuFilterProc(int code, WPARAM wParam,
                                              LPARAM lParam)
{
    MSG FAR *lpmsg = (MSG FAR *)lParam;

    if (code == MSGF_MENU && s_bStartRelease) {
        if (lpmsg->message == WM_LBUTTONUP) {
            s_bStartRelease = FALSE;
            if (PtInRect(&s_rcStartScreen, lpmsg->pt))
                return 1;
        } else if (lpmsg->message == WM_LBUTTONDOWN) {
            s_bStartRelease = FALSE;
        }
    }
    if (code == MSGF_MENU && lpmsg->message == WM_LBUTTONDOWN
        && PtInRect(&s_rcStartScreen, lpmsg->pt))
        s_dwCloseTime = lpmsg->time;
    return CallNextHookEx(s_hhkStart, code, wParam, lParam);
}

static void ShowStartMenu(HWND hwnd)
{
    static FARPROC lpfnFilter;
    POINT pt;

    HintHide();
    if (s_bHidden)
        SlideBar(hwnd, TRUE);
    s_bStartDown = TRUE;
    InvalidateRect(hwnd, &s_rcStart, FALSE);
    UpdateWindow(hwnd);

    s_rcStartScreen = s_rcStart;
    ClientToScreen(hwnd, (POINT FAR *)&s_rcStartScreen.left);
    ClientToScreen(hwnd, (POINT FAR *)&s_rcStartScreen.right);
    s_bStartRelease = GetKeyState(VK_LBUTTON) < 0;
    if (!lpfnFilter)
        lpfnFilter = MakeProcInstance((FARPROC)StartMenuFilterProc, g_hInst);
    s_hhkStart = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)lpfnFilter, g_hInst,
                                  GetCurrentTask());

    pt.x = s_rcStart.left;
    pt.y = 0;
    ClientToScreen(hwnd, &pt);
    StartMenuShow(hwnd, pt.x, pt.y);

    if (s_hhkStart) {
        UnhookWindowsHookEx(s_hhkStart);
        s_hhkStart = NULL;
    }
    s_bStartRelease = FALSE;
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
    int nItems;

    if (!hMenu)
        return;
    EnableMenuItem(hMenu, SC_RESTORE, (bIconic || bZoomed) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, SC_MOVE, (bIconic || bZoomed) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, SC_SIZE, (bIconic || bZoomed) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, SC_MINIMIZE, bIconic ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu, SC_MAXIMIZE, bZoomed ? MF_GRAYED : MF_ENABLED);

    /* Calmira's additions, removed again afterwards */
    nItems = GetMenuItemCount(hMenu);
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, CMD_TB_WININFO, "Window &Info...");
    AppendMenu(hMenu, MF_STRING, CMD_TB_TERMINATE, "&Terminate...");

    s_hwndSysMenuTarget = hwndTask;
    ClientToScreen(hwnd, &pt);
    s_bLiftMenu = TRUE;
    SetTimer(hwnd, TIMER_LIFTMENU, 20, NULL);
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
    KillTimer(hwnd, TIMER_LIFTMENU);
    s_bLiftMenu = FALSE;
    while (GetMenuItemCount(hMenu) > nItems)
        DeleteMenu(hMenu, nItems, MF_BYPOSITION);
}

/* Window Info: what the window is */
static void WindowInfo(HWND hwndTask)
{
    char szMsg[400], szTitle[80], szClass[40], szModule[MAXPATHLEN];
    RECT rc;

    if (!IsWindow(hwndTask))
        return;
    GetWindowText(hwndTask, szTitle, sizeof(szTitle));
    GetClassName(hwndTask, szClass, sizeof(szClass));
    szModule[0] = '\0';
    GetModuleFileName((HINSTANCE)GetWindowWord(hwndTask, GWW_HINSTANCE), szModule,
                      sizeof(szModule));
    GetWindowRect(hwndTask, &rc);
    wsprintf(szMsg, "Caption:\t%s\nClass:\t%s\nModule:\t%s\nHandle:\t%04X\n"
                    "Position:\t%d, %d\nSize:\t%d x %d%s",
             (LPSTR)szTitle, (LPSTR)szClass, (LPSTR)szModule, (UINT)hwndTask,
             rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             (LPSTR)(IsIconic(hwndTask) ? "\n\t(minimized)" : IsZoomed(hwndTask)
                                         ? "\n\t(maximized)" : ""));
    MessageBox(NULL, szMsg, "Window Info", MB_OK | MB_ICONINFORMATION);
}

static void TaskbarContextMenu(HWND hwnd, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, CMD_CASCADE, "&Cascade Windows");
    AppendMenu(hMenu, MF_STRING, CMD_TILE, "Tile &Horizontally");
    AppendMenu(hMenu, MF_STRING, CMD_TILEVERT, "Tile &Vertically");
    AppendMenu(hMenu, MF_STRING, CMD_MINIMIZEALL, "&Minimize All Windows");
    AppendMenu(hMenu, MF_STRING, CMD_CLEARDESKTOP, "Show the &Desktop");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING | (CfgGetInt("Taskbar", "AutoHide", 0) ? MF_CHECKED : 0),
               CMD_TB_AUTOHIDE, "Auto &Hide");
    AppendMenu(hMenu, MF_STRING, CMD_TOGGLETASKBAR, "Hide the Task&bar");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, CMD_TASKMAN, "&Task Manager...");
    AppendMenu(hMenu, MF_STRING, CMD_STARTPROPS, "&Start Menu...");
    AppendMenu(hMenu, MF_STRING, CMD_OPTIONS, "&Options...");
    AppendMenu(hMenu, MF_STRING, CMD_ABOUT, "&About NewShell16...");
    if (!g_bIsShell) {
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, CMD_CLOSESHELL, "C&lose NewShell16");
    }
    ClientToScreen(hwnd, &pt);
    s_bLiftMenu = TRUE;
    SetTimer(hwnd, TIMER_LIFTMENU, 20, NULL);
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
    KillTimer(hwnd, TIMER_LIFTMENU);
    s_bLiftMenu = FALSE;
    DestroyMenu(hMenu);
}

/*
 * Windows 3.1 has no TPM_BOTTOMALIGN: a menu opened from the taskbar is
 * pushed up only as far as the screen edge and covers the taskbar. Once
 * it is on screen (a timer runs inside the menu loop) the popup window
 * (class "#32768") is moved up above the taskbar.
 */
static void LiftMenu(void)
{
    HWND hwndMenu = FindWindow("#32768", NULL);
    RECT rc, rcBar;

    if (!hwndMenu || !IsWindowVisible(hwndMenu))
        return;
    s_bLiftMenu = FALSE;
    KillTimer(g_hwndTaskbar, TIMER_LIFTMENU);
    GetWindowRect(hwndMenu, &rc);
    GetWindowRect(g_hwndTaskbar, &rcBar);
    if (rc.bottom > rcBar.top)
        SetWindowPos(hwndMenu, NULL, rc.left, rcBar.top - (rc.bottom - rc.top),
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void TasksMinimizeAll(void)
{
    int i;

    UpdateTasks();
    for (i = 0; i < s_nTasks; i++)
        if (!IsIconic(s_tasks[i].hwnd))
            ShowWindow(s_tasks[i].hwnd, SW_MINIMIZE);
}

/*
 * Show the Desktop: minimises every window, and the next time restores
 * those it minimised (if they are all still minimised).
 */
void TasksClearDesktop(void)
{
    static HWND s_hwndCleared[MAX_TASKS];
    static int  s_nCleared;
    int i, n = 0;

    for (i = 0; i < s_nCleared; i++)
        if (IsWindow(s_hwndCleared[i]) && IsIconic(s_hwndCleared[i]))
            n++;
    if (s_nCleared && n == s_nCleared) {
        for (i = s_nCleared - 1; i >= 0; i--)
            ShowWindow(s_hwndCleared[i], SW_RESTORE);
        s_nCleared = 0;
        return;
    }
    UpdateTasks();
    s_nCleared = 0;
    for (i = 0; i < s_nTasks; i++)
        if (!IsIconic(s_tasks[i].hwnd)) {
            s_hwndCleared[s_nCleared++] = s_tasks[i].hwnd;
            ShowWindow(s_tasks[i].hwnd, SW_MINIMIZE);
        }
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

static int HitTray(POINT pt)
{
    if (!PtInRect(&s_rcTray, pt))
        return -1;
    return (pt.x - s_rcTray.left) / TRAY_CX < s_nTray ? (pt.x - s_rcTray.left) / TRAY_CX : -1;
}

/* -------------------------------------------------------------------- */
/* Auto-hide                                                            */
/* -------------------------------------------------------------------- */

/* Slides the bar up (bShow) or down to its hidden position */
static void SlideBar(HWND hwnd, BOOL bShow)
{
    int cyScreen = GetSystemMetrics(SM_CYSCREEN);
    int yShown = cyScreen - s_cyBar, yHidden = cyScreen - HIDDEN_CY;
    int nSteps = CfgGetInt("Taskbar", "AnimSteps", 6), i, y;
    RECT rc;
    DWORD dw;

    if (s_bHidden == !bShow)
        return;
    s_bHidden = !bShow;
    GetWindowRect(hwnd, &rc);
    for (i = 1; i <= nSteps; i++) {
        y = bShow ? yHidden - (yHidden - yShown) * i / nSteps
                  : yShown + (yHidden - yShown) * i / nSteps;
        SetWindowPos(hwnd, NULL, rc.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateWindow(hwnd);
        for (dw = GetTickCount(); GetTickCount() - dw < 12; )
            ;
    }
    SetWindowPos(hwnd, NULL, rc.left, bShow ? yShown : yHidden, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Called from the refresh timer: show the bar when the mouse reaches the
   bottom of the screen, hide it when the mouse has left it */
static void CheckAutoHide(HWND hwnd)
{
    POINT pt;
    RECT rc;

    if (!CfgGetInt("Taskbar", "AutoHide", 0) || !CfgGetInt("Taskbar", "Enabled", 1))
        return;
    GetCursorPos(&pt);
    GetWindowRect(hwnd, &rc);
    if (s_bHidden) {
        if (pt.y >= GetSystemMetrics(SM_CYSCREEN) - HIDDEN_CY)
            SlideBar(hwnd, TRUE);
    } else if (!PtInRect(&rc, pt) && !s_bStartDown && GetCapture() == NULL
               && !(GetSystemDebugState() & SDS_MENU))
        SetTimer(hwnd, TIMER_HIDE, HIDE_DELAY, NULL);
}

/* Applies Enabled=, AutoHide=, Exclude= and Tray= */
void TaskbarApplySettings(void)
{
    int cyScreen = GetSystemMetrics(SM_CYSCREEN);

    if (!g_hwndTaskbar)
        return;
    CfgGetStr("Taskbar", "Exclude", "", s_szExclude, sizeof(s_szExclude));
    CfgGetStr("Taskbar", "Tray", "", s_szTrayList, sizeof(s_szTrayList));
    s_nTray = 0;
    if (!CfgGetInt("Taskbar", "Enabled", 1)) {
        ShowWindow(g_hwndTaskbar, SW_HIDE);
        return;
    }
    s_bHidden = CfgGetInt("Taskbar", "AutoHide", 0);
    SetWindowPos(g_hwndTaskbar, HWND_TOPMOST, 0, s_bHidden ? cyScreen - HIDDEN_CY
                                                           : cyScreen - s_cyBar,
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    if (!IsWindowVisible(g_hwndTaskbar))
        ShowWindow(g_hwndTaskbar, SW_SHOWNOACTIVATE);
    UpdateTasks();
    InvalidateRect(g_hwndTaskbar, NULL, FALSE);
}

/* -------------------------------------------------------------------- */
/* Hints                                                                */
/* -------------------------------------------------------------------- */

/* The hint for the button, tray icon or clock under the mouse */
static void ShowBarHint(HWND hwnd)
{
    char szText[160], szDate[64];
    POINT pt;
    RECT rcBar;
    int i;
    HWND hwndFor = NULL;

    GetCursorPos(&pt);
    GetWindowRect(hwnd, &rcBar);
    if (!PtInRect(&rcBar, pt) || GetCapture() || (GetSystemDebugState() & SDS_MENU)) {
        HintHide();
        s_nHintAt = -2;
        KillTimer(hwnd, TIMER_HINT);
        return;
    }
    ScreenToClient(hwnd, &pt);
    if ((i = HitTask(pt)) >= 0)
        hwndFor = s_tasks[i].hwnd;
    else if ((i = HitTray(pt)) >= 0) {
        hwndFor = s_hwndTray[i];
        i += 100;
    } else if (PtInRect(&s_rcClock, pt))
        i = -1;
    else
        i = -2;
    if (i == s_nHintAt && HintVisible())
        return;
    s_nHintAt = i;
    if (hwndFor) {
        GetWindowText(hwndFor, szText, sizeof(szText));
    } else if (i == -1) {
        struct dosdate_t d;
        static const char *s_szDay[] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                         "Thursday", "Friday", "Saturday" };
        static const char *s_szMonth[] = { "January", "February", "March", "April",
                                           "May", "June", "July", "August", "September",
                                           "October", "November", "December" };

        _dos_getdate(&d);
        wsprintf(szDate, "%s, %d %s %d", (LPSTR)s_szDay[d.dayofweek % 7], d.day,
                 (LPSTR)s_szMonth[(d.month + 11) % 12], d.year);
        wsprintf(szText, "%s\n%lu KB free, %u%% resources", (LPSTR)szDate,
                 GetFreeSpace(0) / 1024L, GetFreeSystemResources(GFSR_SYSTEMRESOURCES));
    } else {
        HintHide();
        return;
    }
    GetCursorPos(&pt);
    HintShow(szText, pt.x, rcBar.top - 2 - (i == -1 ? 34 : 20));
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
        if (wParam == TIMER_LIFTMENU) {
            if (s_bLiftMenu)
                LiftMenu();
            return 0;
        }
        if (wParam == TIMER_HIDE) {
            POINT pt;
            RECT rc;

            KillTimer(hwnd, TIMER_HIDE);
            GetCursorPos(&pt);
            GetWindowRect(hwnd, &rc);
            if (!PtInRect(&rc, pt) && !s_bStartDown && GetCapture() == NULL
                && !(GetSystemDebugState() & SDS_MENU))
                SlideBar(hwnd, FALSE);
            return 0;
        }
        if (wParam == TIMER_HINT) {
            ShowBarHint(hwnd);
            return 0;
        }
        i = UpdateTasks();
        i |= UpdateClock();
        if (i)
            InvalidateRect(hwnd, NULL, FALSE);
        CheckAutoHide(hwnd);
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
            if (GetMessageTime() == (LONG)s_dwCloseTime)
                ;
            else if (CfgGetInt("Start Menu", "MouseUp", 0)) {
                /* opens on the release, like a button */
                s_bStartDown = TRUE;
                s_bStartPress = TRUE;
                SetCapture(hwnd);
                InvalidateRect(hwnd, &s_rcStart, FALSE);
            } else
                ShowStartMenu(hwnd);
        } else if ((i = HitTask(pt)) >= 0) {
            ActivateTask(s_tasks[i].hwnd);
        } else if ((i = HitTray(pt)) >= 0) {
            ActivateTask(s_hwndTray[i]);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (CfgGetInt("Taskbar", "Hints", 1) && !s_bStartPress) {
            if (HintVisible())
                ShowBarHint(hwnd);
            SetTimer(hwnd, TIMER_HINT, 600, NULL);
        }
        if (s_bStartPress) {
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            if (PtInRect(&s_rcStart, pt) != s_bStartDown) {
                s_bStartDown = !s_bStartDown;
                InvalidateRect(hwnd, &s_rcStart, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (s_bStartPress) {
            s_bStartPress = FALSE;
            ReleaseCapture();
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            if (PtInRect(&s_rcStart, pt))
                ShowStartMenu(hwnd);
            else {
                s_bStartDown = FALSE;
                InvalidateRect(hwnd, &s_rcStart, FALSE);
            }
        }
        return 0;

    case WM_RBUTTONUP:
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        HintHide();
        if ((i = HitTask(pt)) >= 0)
            TaskSystemMenu(hwnd, s_tasks[i].hwnd, pt);
        else if ((i = HitTray(pt)) >= 0)
            TaskSystemMenu(hwnd, s_hwndTray[i], pt);
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
        } else if (wParam == CMD_TB_WININFO) {
            WindowInfo(s_hwndSysMenuTarget);
        } else if (wParam == CMD_TB_TERMINATE) {
            TaskManTerminate(s_hwndSysMenuTarget, NULL);
        } else if (wParam == CMD_TB_AUTOHIDE) {
            CfgSetInt("Taskbar", "AutoHide", !CfgGetInt("Taskbar", "AutoHide", 0));
            TaskbarApplySettings();
            HookApplySettings();
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
    TaskbarApplySettings();
    UpdateWindow(g_hwndTaskbar);
    return TRUE;
}
