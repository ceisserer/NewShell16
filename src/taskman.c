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
 * taskman.c - the Task Manager and window arrangement
 *
 * Lists the running applications with their icons and modules, and can
 * switch to, hide, minimise, close or terminate them, start a new task,
 * and cascade or tile windows within the area not covered by the taskbar.
 * It replaces the Windows Task List (Ctrl+Esc) when NEWSHELL.EXE is set
 * as taskman.exe= in SYSTEM.INI.
 */
#include <string.h>
#include "newshell.h"
#include <toolhelp.h>

#define MAX_TM_TASKS    48
#define MAX_HIDDEN      16
#define ID_LIST         IDC_TMLIST
#define ID_SWITCH       IDC_TMSWITCH
#define ID_ENDTASK      IDC_TMENDTASK
#define ID_NEWTASK      IDC_TMNEWTASK
#define TIMER_REFRESH   1
#define ITEM_CY         20
#define COL_TASK_CX     230

typedef struct tagTMTASK {
    HWND  hwnd;
    char  szTitle[80];
    char  szModule[16];
    BOOL  bHidden;
} TMTASK;

static HWND   s_hwndTM;
static HWND   s_hwndList;
static TMTASK s_tasks[MAX_TM_TASKS];
static int    s_nTasks;
static HWND   s_hwndHidden[MAX_HIDDEN];     /* windows hidden by us */
static int    s_nHidden;
static HWND   s_hwndPrevActive;         /* active before we opened */

/* collected by EnumProc */
static HWND   s_hwndFound[MAX_TM_TASKS];
static int    s_nFound;

/* -------------------------------------------------------------------- */
/* Window arrangement                                                   */
/* -------------------------------------------------------------------- */

void GetWorkArea(RECT FAR *lprc)
{
    SetRect(lprc, 0, 0, GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight());
}

static BOOL IsArrangeable(HWND hwnd)
{
    return IsWindowVisible(hwnd) && IsWindowEnabled(hwnd)
        && (GetWindowLong(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
}

static int Arrangeable(HWND FAR *lpList, int n, HWND FAR *lpOut)
{
    int i, m = 0;

    for (i = 0; i < n; i++)
        if (IsWindow(lpList[i]) && IsArrangeable(lpList[i])) {
            if (IsIconic(lpList[i]) || IsZoomed(lpList[i]))
                ShowWindow(lpList[i], SW_RESTORE);
            lpOut[m++] = lpList[i];
        }
    return m;
}

void CascadeList(HWND FAR *lpList, int n)
{
    HWND hwnds[MAX_TM_TASKS];
    RECT rc;
    int i, m, step, cx, cy;

    m = Arrangeable(lpList, n, hwnds);
    if (m == 0)
        return;
    GetWorkArea(&rc);
    step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
    cx = (rc.right - rc.left) * 3 / 4;
    cy = (rc.bottom - rc.top) * 3 / 4;
    for (i = 0; i < m; i++) {
        int k = i % 8;
        SetWindowPos(hwnds[m - 1 - i], NULL, rc.left + k * step, rc.top + k * step,
                     cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void TileList(HWND FAR *lpList, int n, BOOL bVertical)
{
    HWND hwnds[MAX_TM_TASKS];
    RECT rc;
    int i, m, nCols, nRows, cx, cy, col, row;

    m = Arrangeable(lpList, n, hwnds);
    if (m == 0)
        return;
    GetWorkArea(&rc);
    /* bVertical: windows side by side; otherwise stacked */
    for (nCols = 1; nCols * nCols < m; nCols++)
        ;
    nRows = (m + nCols - 1) / nCols;
    if (!bVertical) {
        i = nCols;
        nCols = nRows;
        nRows = i;
    }
    if (m <= 3) {
        nCols = bVertical ? m : 1;
        nRows = bVertical ? 1 : m;
    }
    cx = (rc.right - rc.left) / nCols;
    cy = (rc.bottom - rc.top) / nRows;
    for (i = 0; i < m; i++) {
        col = bVertical ? i / nRows : i % nCols;
        row = bVertical ? i % nRows : i / nCols;
        SetWindowPos(hwnds[i], NULL, rc.left + col * cx, rc.top + row * cy,
                     cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/* -------------------------------------------------------------------- */
/* Task list                                                            */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT TMEnumProc(HWND hwnd, LPARAM lParam)
{
    (void)lParam;
    if (s_nFound < MAX_TM_TASKS && IsWindowVisible(hwnd)
        && GetWindow(hwnd, GW_OWNER) == NULL && GetWindowTextLength(hwnd) > 0) {
        char szClass[32];
        GetClassName(hwnd, szClass, sizeof(szClass));
        if (lstrcmpi(szClass, CLS_TASKBAR) && lstrcmpi(szClass, CLS_DESKICON)
            && lstrcmpi(szClass, CLS_MAIN))
            s_hwndFound[s_nFound++] = hwnd;
    }
    return TRUE;
}

static void AddTask(HWND hwnd, BOOL bHidden)
{
    char szModule[MAXPATHLEN];
    TMTASK *t;

    if (s_nTasks >= MAX_TM_TASKS)
        return;
    t = &s_tasks[s_nTasks++];
    t->hwnd = hwnd;
    t->bHidden = bHidden;
    GetWindowText(hwnd, t->szTitle, sizeof(t->szTitle) - 10);
    if (bHidden)
        lstrcat(t->szTitle, " (hidden)");
    GetModuleFileName((HINSTANCE)GetWindowWord(hwnd, GWW_HINSTANCE),
                      szModule, sizeof(szModule));
    lstrcpyn(t->szModule, PathFileName(szModule), sizeof(t->szModule));
    AnsiLower(t->szModule);
}

static HWND SelectedWnd(void)
{
    int i = (int)SendMessage(s_hwndList, LB_GETCARETINDEX, 0, 0L);

    if (i < 0 || i >= s_nTasks)
        return NULL;
    if (!SendMessage(s_hwndList, LB_GETSEL, i, 0L))
        return NULL;
    return s_tasks[i].hwnd;
}

static void UpdateStatus(void)
{
    char szText[80];

    if (!s_hwndTM)
        return;
    wsprintf(szText, "%d task(s)    %luKB free memory    %u%% resources",
             s_nTasks, GetFreeSpace(0) / 1024L,
             GetFreeSystemResources(GFSR_SYSTEMRESOURCES));
    SetDlgItemText(s_hwndTM, IDC_TMSTATUS, szText);
}

static void FillTasks(void)
{
    static FARPROC lpfnEnum;
    HWND hwndKeep = s_nTasks ? SelectedWnd() : s_hwndPrevActive;
    int i, nTop;

    if (!lpfnEnum)
        lpfnEnum = MakeProcInstance((FARPROC)TMEnumProc, g_hInst);
    s_nFound = 0;
    EnumWindows((WNDENUMPROC)lpfnEnum, 0L);
    s_nTasks = 0;
    for (i = 0; i < s_nFound; i++)
        if (s_hwndFound[i] != s_hwndTM)
            AddTask(s_hwndFound[i], FALSE);
    for (i = 0; i < s_nHidden; i++)
        if (IsWindow(s_hwndHidden[i]) && !IsWindowVisible(s_hwndHidden[i]))
            AddTask(s_hwndHidden[i], TRUE);

    nTop = (int)SendMessage(s_hwndList, LB_GETTOPINDEX, 0, 0L);
    SendMessage(s_hwndList, WM_SETREDRAW, FALSE, 0L);
    SendMessage(s_hwndList, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < s_nTasks; i++)
        SendMessage(s_hwndList, LB_ADDSTRING, 0, (LPARAM)i);
    for (i = 0; i < s_nTasks; i++)
        if (s_tasks[i].hwnd == hwndKeep) {
            SendMessage(s_hwndList, LB_SETSEL, TRUE, MAKELPARAM(i, 0));
            SendMessage(s_hwndList, LB_SETCARETINDEX, i, 0L);
        }
    SendMessage(s_hwndList, LB_SETTOPINDEX, nTop, 0L);
    SendMessage(s_hwndList, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(s_hwndList, NULL, TRUE);
    UpdateStatus();
}

/* The selected windows, or all of them if none is selected */
static int SelectedList(HWND FAR *lpOut)
{
    int i, n = 0;

    for (i = 0; i < s_nTasks; i++)
        if (SendMessage(s_hwndList, LB_GETSEL, i, 0L))
            lpOut[n++] = s_tasks[i].hwnd;
    if (n == 0)
        for (i = 0; i < s_nTasks; i++)
            if (!s_tasks[i].bHidden)
                lpOut[n++] = s_tasks[i].hwnd;
    return n;
}

static void SwitchTo(void)
{
    HWND hwnd = SelectedWnd();
    HWND hwndPopup;

    if (!hwnd)
        return;
    if (!IsWindowVisible(hwnd))
        ShowWindow(hwnd, SW_SHOWNA);
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    hwndPopup = GetLastActivePopup(hwnd);
    if (!hwndPopup || !IsWindowVisible(hwndPopup))
        hwndPopup = hwnd;
    ShowWindow(s_hwndTM, SW_HIDE);
    BringWindowToTop(hwndPopup);
    SetActiveWindow(hwndPopup);
}

static void HideUnhide(void)
{
    HWND hwnd = SelectedWnd();
    int i;

    if (!hwnd)
        return;
    if (IsWindowVisible(hwnd)) {
        if (s_nHidden < MAX_HIDDEN) {
            ShowWindow(hwnd, SW_HIDE);
            s_hwndHidden[s_nHidden++] = hwnd;
        }
    } else {
        ShowWindow(hwnd, SW_SHOWNA);
        for (i = 0; i < s_nHidden; i++)
            if (s_hwndHidden[i] == hwnd)
                s_hwndHidden[i] = s_hwndHidden[--s_nHidden];
    }
    FillTasks();
    TaskbarRefresh();
}

static void EndTask(void)
{
    HWND hwnds[MAX_TM_TASKS];
    int i, n = 0;

    for (i = 0; i < s_nTasks; i++)
        if (SendMessage(s_hwndList, LB_GETSEL, i, 0L))
            hwnds[n++] = s_tasks[i].hwnd;
    for (i = 0; i < n; i++) {
        if (!IsWindowVisible(hwnds[i]))
            ShowWindow(hwnds[i], SW_SHOWNA);
        PostMessage(hwnds[i], WM_CLOSE, 0, 0L);
    }
    SetTimer(s_hwndTM, TIMER_REFRESH, 700, NULL);
}

static void Terminate(void)
{
    HWND hwnd = SelectedWnd();
    char szMsg[160], szTitle[80];
    HTASK hTask;

    if (!hwnd)
        return;
    hTask = GetWindowTask(hwnd);
    GetWindowText(hwnd, szTitle, sizeof(szTitle));
    wsprintf(szMsg, "Terminating '%s' discards any unsaved data and may leave "
                    "Windows unstable.\n\nTerminate it anyway?", (LPSTR)szTitle);
    if (MessageBox(s_hwndTM, szMsg, "Terminate Task",
                   MB_YESNO | MB_ICONSTOP | MB_DEFBUTTON2) != IDYES)
        return;
    if (hTask == GetCurrentTask()) {
        MessageBox(s_hwndTM, "NewShell16 cannot terminate itself.", "Terminate Task",
                   MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    TerminateApp(hTask, NO_UAE_BOX);
    SetTimer(s_hwndTM, TIMER_REFRESH, 700, NULL);
}

/* -------------------------------------------------------------------- */
/* Window                                                               */
/* -------------------------------------------------------------------- */

static void DrawTask(DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText;
    TMTASK *t;
    HICON hIcon;
    BOOL bSel = (lpdis->itemState & ODS_SELECTED) != 0;
    HFONT hfOld;

    FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
    if ((int)lpdis->itemID < 0 || (int)lpdis->itemID >= s_nTasks)
        return;
    t = &s_tasks[lpdis->itemID];
    hIcon = IsWindow(t->hwnd) ? (HICON)GetClassWord(t->hwnd, GCW_HICON) : (HICON)0;
    if (!hIcon && IsWindow(t->hwnd))
        hIcon = (HICON)LOWORD(SendMessage(t->hwnd, WM_QUERYDRAGICON, 0, 0L));
    if (!hIcon)
        hIcon = IconStock(IDI_APP);
    DrawIconSmall(hdc, rc.left + 2, rc.top + 2, hIcon, GetSysColor(COLOR_WINDOW));

    hfOld = SelectObject(hdc, g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetRect(&rcText, rc.left + 22, rc.top + 1,
            rc.left + 22 + TextWidth(hdc, t->szTitle) + 4, rc.bottom - 1);
    if (rcText.right > rc.left + COL_TASK_CX)
        rcText.right = rc.left + COL_TASK_CX;
    if (bSel)
        FillSolid(hdc, &rcText, GetSysColor(COLOR_HIGHLIGHT));
    SetTextColor(hdc, GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT
                                       : t->bHidden ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
    rcText.left += 2;
    DrawText(hdc, t->szTitle, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    rcText.left -= 2;
    if (lpdis->itemState & ODS_FOCUS)
        DrawFocusRect(hdc, &rcText);
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    SetRect(&rcText, rc.left + COL_TASK_CX + 4, rc.top, rc.right, rc.bottom);
    DrawText(hdc, t->szModule, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hfOld);
}

/* Lays the dialog's controls out for its current size (dialog units are
   only used for the initial size from the template) */
static void LayoutTM(HWND hwnd)
{
    RECT rc, rcBtn;
    int cyText, cyBtn, cxBtn, yBtn, x, i;
    static const int s_ids[] = { ID_SWITCH, ID_ENDTASK, ID_NEWTASK };

    GetClientRect(hwnd, &rc);
    GetWindowRect(GetDlgItem(hwnd, ID_SWITCH), &rcBtn);
    cxBtn = rcBtn.right - rcBtn.left;
    cyBtn = rcBtn.bottom - rcBtn.top;
    GetWindowRect(GetDlgItem(hwnd, IDC_TMSTATUS), &rcBtn);
    cyText = rcBtn.bottom - rcBtn.top;

    MoveWindow(GetDlgItem(hwnd, IDC_TMHDR1), 8, 6, COL_TASK_CX, cyText, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_TMHDR2), 8 + COL_TASK_CX + 4, 6,
               rc.right - COL_TASK_CX - 20, cyText, TRUE);
    yBtn = rc.bottom - 8 - cyText - 6 - cyBtn;
    MoveWindow(s_hwndList, 6, 8 + cyText, rc.right - 12, yBtn - 14 - cyText, TRUE);
    x = rc.right - 6 - 3 * cxBtn - 2 * 6;
    for (i = 0; i < 3; i++, x += cxBtn + 6)
        MoveWindow(GetDlgItem(hwnd, s_ids[i]), x, yBtn, cxBtn, cyBtn, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_TMSTATUS), 8, rc.bottom - 6 - cyText,
               rc.right - 16, cyText, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

BOOL CALLBACK NSEXPORT TaskManDlgProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    HWND hwnds[MAX_TM_TASKS];
    int n;

    switch (msg) {
    case WM_INITDIALOG:
        s_hwndList = GetDlgItem(hwnd, ID_LIST);
        LayoutTM(hwnd);
        return TRUE;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            LayoutTM(hwnd);
        return TRUE;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.x = 320;
        ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.y = 200;
        return TRUE;

    case WM_QUERYDRAGICON:
        return (BOOL)(UINT)IconStock(IDI_TASKMAN);

    case WM_ACTIVATE:
        if (wParam != WA_INACTIVE)
            FillTasks();
        return FALSE;           /* the dialog manager restores the focus */

    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lParam)->itemHeight = ITEM_CY;
        return TRUE;

    case WM_DRAWITEM:
        DrawTask((DRAWITEMSTRUCT FAR *)lParam);
        return TRUE;

    case WM_TIMER:
        KillTimer(hwnd, TIMER_REFRESH);
        FillTasks();
        TaskbarRefresh();
        return TRUE;

    case WM_INITMENUPOPUP:
        {
            HWND hwndSel = SelectedWnd();
            BOOL bSel = hwndSel != NULL;
            UINT uOn = bSel ? MF_ENABLED : MF_GRAYED;

            EnableMenuItem((HMENU)wParam, CMD_TM_SWITCH, uOn);
            EnableMenuItem((HMENU)wParam, CMD_TM_HIDE, uOn);
            EnableMenuItem((HMENU)wParam, CMD_TM_MINIMIZE, uOn);
            EnableMenuItem((HMENU)wParam, CMD_TM_ENDTASK, uOn);
            EnableMenuItem((HMENU)wParam, CMD_TM_TERMINATE, uOn);
            ModifyMenu((HMENU)wParam, CMD_TM_HIDE, MF_BYCOMMAND | MF_STRING | uOn,
                       CMD_TM_HIDE, (bSel && !IsWindowVisible(hwndSel))
                                    ? "Un&hide" : "&Hide");
        }
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case ID_LIST:
            if (HIWORD(lParam) == LBN_DBLCLK)
                SwitchTo();
            return TRUE;
        case ID_SWITCH:
        case CMD_TM_SWITCH:     SwitchTo(); return TRUE;
        case ID_ENDTASK:
        case CMD_TM_ENDTASK:    EndTask(); return TRUE;
        case ID_NEWTASK:
        case CMD_RUN:           RunDialog(hwnd); return TRUE;
        case CMD_TM_HIDE:       HideUnhide(); return TRUE;
        case CMD_TM_TERMINATE:  Terminate(); return TRUE;
        case CMD_TM_MINIMIZE:
            n = SelectedList(hwnds);
            while (n-- > 0)
                ShowWindow(hwnds[n], SW_MINIMIZE);
            return TRUE;
        case CMD_CASCADE:
            n = SelectedList(hwnds);
            CascadeList(hwnds, n);
            return TRUE;
        case CMD_TILE:
        case CMD_TILEVERT:
            n = SelectedList(hwnds);
            TileList(hwnds, n, wParam == CMD_TILEVERT);
            return TRUE;
        case CMD_MINIMIZEALL:
            TasksMinimizeAll();
            return TRUE;
        case CMD_F_REFRESH:     FillTasks(); return TRUE;
        case IDCANCEL:
        case CMD_F_CLOSE:       ShowWindow(hwnd, SW_HIDE); return TRUE;
        }
        break;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return TRUE;

    case WM_DESTROY:
        s_hwndTM = NULL;
        s_hwndList = NULL;
        return TRUE;
    }
    return FALSE;
}

BOOL TaskManRegister(void)
{
    return TRUE;
}

void TaskManOpen(void)
{
    static FARPROC lpfnDlg;
    HWND hwndOwner;
    RECT rc;

    s_hwndPrevActive = GetActiveWindow();
    while (s_hwndPrevActive
           && (hwndOwner = GetWindow(s_hwndPrevActive, GW_OWNER)) != NULL)
        s_hwndPrevActive = hwndOwner;
    s_nTasks = 0;

    if (!s_hwndTM) {
        if (!lpfnDlg)
            lpfnDlg = MakeProcInstance((FARPROC)TaskManDlgProc, g_hInst);
        s_hwndTM = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_TASKMAN), NULL,
                                (DLGPROC)lpfnDlg);
        if (!s_hwndTM)
            return;
        GetWindowRect(s_hwndTM, &rc);
        SetWindowPos(s_hwndTM, NULL,
                     (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
                     (GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight()
                      - (rc.bottom - rc.top)) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (IsIconic(s_hwndTM))
        ShowWindow(s_hwndTM, SW_RESTORE);
    ShowWindow(s_hwndTM, SW_SHOW);
    BringWindowToTop(s_hwndTM);
    SetActiveWindow(s_hwndTM);
    FillTasks();
    SetFocus(s_hwndList);
}

void TaskManClose(void)
{
    if (s_hwndTM)
        DestroyWindow(s_hwndTM);
}

/* Lets the main loop route dialog keys (Tab, Enter, Esc) to the window */
BOOL TaskManIsDialogMessage(MSG FAR *lpMsg)
{
    if (!s_hwndTM || lpMsg->hwnd == NULL)
        return FALSE;
    if (lpMsg->hwnd != s_hwndTM && !IsChild(s_hwndTM, lpMsg->hwnd))
        return FALSE;
    return IsDialogMessage(s_hwndTM, lpMsg);
}
