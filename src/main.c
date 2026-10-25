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
 * main.c - NewShell16 entry point and hidden main window
 *
 * Install by setting "shell=<path>\NEWSHELL.EXE" in the [boot] section of
 * SYSTEM.INI. It can also be started from Program Manager for testing.
 *
 * Running NEWSHELL.EXE again while it is active forwards the request to
 * the running copy:
 *   NEWSHELL                 Task Manager (or Start menu, see CtrlEscStart),
 *                            so it can be used as "taskman.exe=" for Ctrl+Esc
 *   NEWSHELL /E [path]       File System window
 *   NEWSHELL /S              Start menu
 *   NEWSHELL path            open a folder, document or program
 */
#include <string.h>
#include "newshell.h"
#include "nshook.h"

#define TIMER_FILECHANGE    1
#define FILECHANGE_DELAY    600
#define MAX_CHANGED_DIRS    16

HINSTANCE g_hInst;
HWND      g_hwndMain;
HWND      g_hwndTaskbar;
BOOL      g_bIsShell;
char      g_szWinDir[MAXPATHLEN];
HFONT     g_hfontUI;
HFONT     g_hfontBold;

static HINSTANCE           s_hHookDll;
static NSHOOKSTART         s_lpfnHookStart;
static NSHOOKSTOP          s_lpfnHookStop;
static NSHOOKGETFILECHANGE s_lpfnHookGetChange;
static char s_szChanged[MAX_CHANGED_DIRS][MAXPATHLEN];
static int  s_nChanged;

static BOOL DetectShell(void)
{
    char szShell[MAXPATHLEN], szSelf[MAXPATHLEN];
    LPSTR lpShellName, lpSelfName, p;

    GetPrivateProfileString("boot", "shell", "progman.exe", szShell,
                            sizeof(szShell), "SYSTEM.INI");
    for (p = szShell; *p && *p != ' '; p++)
        ;
    *p = '\0';
    GetModuleFileName(g_hInst, szSelf, sizeof(szSelf));
    lpShellName = PathFileName(szShell);
    lpSelfName = PathFileName(szSelf);
    return lstrcmpi(lpShellName, lpSelfName) == 0;
}

/* -------------------------------------------------------------------- */
/* Hook DLL                                                             */
/* -------------------------------------------------------------------- */

static void LoadHookDll(void)
{
    char szPath[MAXPATHLEN];
    UINT uOldMode;

    GetModuleFileName(g_hInst, szPath, sizeof(szPath));
    *PathFileName(szPath) = '\0';
    lstrcat(szPath, "NSHOOK.DLL");
    uOldMode = SetErrorMode(0x8000);    /* SEM_NOOPENFILEERRORBOX */
    s_hHookDll = LoadLibrary(szPath);
    SetErrorMode(uOldMode);
    if ((UINT)s_hHookDll < 32) {
        s_hHookDll = NULL;
        return;
    }
    s_lpfnHookStart = (NSHOOKSTART)GetProcAddress(s_hHookDll, "NSHOOKSTART");
    s_lpfnHookStop = (NSHOOKSTOP)GetProcAddress(s_hHookDll, "NSHOOKSTOP");
    s_lpfnHookGetChange = (NSHOOKGETFILECHANGE)GetProcAddress(s_hHookDll,
                                                              "NSHOOKGETFILECHANGE");
    if (!s_lpfnHookStart || !s_lpfnHookStop || !s_lpfnHookGetChange) {
        FreeLibrary(s_hHookDll);
        s_hHookDll = NULL;
    }
}

/* (Re)installs the hooks according to the settings */
void HookApplySettings(void)
{
    UINT uFlags = NSH_SHELL;

    if (!s_hHookDll)
        return;
    if (g_cfg.bMaxClamp)
        uFlags |= NSH_MAXIMIZE;
    if (g_cfg.bDesktopMenu)
        uFlags |= NSH_DESKTOP;
    if (g_cfg.bWinKey)
        uFlags |= NSH_WINKEY;
    if (g_cfg.bFileMonitor)
        uFlags |= NSH_FILES;
    s_lpfnHookStart(g_hwndMain,
                    GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight(), uFlags);
}

static void UnloadHookDll(void)
{
    if (!s_hHookDll)
        return;
    s_lpfnHookStop();
    FreeLibrary(s_hHookDll);
    s_hHookDll = NULL;
}

/* Collects changed folders from the hook; refreshed after a short delay */
static void OnFileChange(HWND hwnd)
{
    char szPath[MAXPATHLEN];
    int i;

    while (s_lpfnHookGetChange && s_lpfnHookGetChange(szPath, sizeof(szPath)) >= 0) {
        OemToAnsi(szPath, szPath);
        if (!PathParent(szPath))
            continue;
        for (i = 0; i < s_nChanged; i++)
            if (lstrcmpi(s_szChanged[i], szPath) == 0)
                break;
        if (i == s_nChanged && s_nChanged < MAX_CHANGED_DIRS)
            lstrcpy(s_szChanged[s_nChanged++], szPath);
    }
    if (s_nChanged > 0)
        SetTimer(hwnd, TIMER_FILECHANGE, FILECHANGE_DELAY, NULL);
}

static void RefreshChanged(void)
{
    int i;

    for (i = 0; i < s_nChanged; i++) {
        FViewRefreshPath(s_szChanged[i]);
        ExplorerRefreshPath(s_szChanged[i]);
    }
    s_nChanged = 0;
}

/* -------------------------------------------------------------------- */
/* Commands                                                             */
/* -------------------------------------------------------------------- */

static void OpenFromCommandLine(LPCSTR lpCmdLine)
{
    while (*lpCmdLine == ' ')
        lpCmdLine++;
    if (lpCmdLine[0] == '/' && (lpCmdLine[1] | 0x20) == 'e') {
        lpCmdLine += 2;
        while (*lpCmdLine == ' ')
            lpCmdLine++;
        ExplorerOpen(lpCmdLine);
    } else if (lpCmdLine[0] == '/' && (lpCmdLine[1] | 0x20) == 's') {
        if (g_hwndTaskbar)
            PostMessage(g_hwndTaskbar, WM_COMMAND, CMD_STARTMENU, 0L);
    } else if (*lpCmdLine)
        LaunchCommand(g_hwndMain, lpCmdLine, NULL, SW_SHOWNORMAL);
}

void CloseShell(void)
{
    FindClose();
    TaskManClose();
    BinClose();
    ExplorerCloseAll();
    FolderCloseAll();
    DesktopDestroy();
    if (g_hwndTaskbar)
        DestroyWindow(g_hwndTaskbar);
    if (g_hwndMain)
        DestroyWindow(g_hwndMain);
}

void DoShutdownDialog(void)
{
    int nChoice = ShutdownDialog(g_hwndMain);

    switch (nChoice) {
    case IDC_EXITWIN:
        ExitWindows(0L, 0);
        break;
    case IDC_RESTARTWIN:
        ExitWindows(EW_RESTARTWINDOWS, 0);
        break;
    case IDC_REBOOT:
        ExitWindows(EW_REBOOTSYSTEM, 0);
        break;
    case IDC_CLOSESHELL:
        CloseShell();
        break;
    }
}

static void ArrangeTasks(UINT id)
{
    HWND hwnds[48];
    int n = TasksGetList(hwnds, 48);

    if (id == CMD_CASCADE)
        CascadeList(hwnds, n);
    else
        TileList(hwnds, n, id == CMD_TILEVERT);
}

LRESULT CALLBACK NSEXPORT MainWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    char szPath[MAXPATHLEN];

    switch (msg) {
    case WM_COMMAND:
        switch (wParam) {
        case CMD_STARTMENU:
            if (g_hwndTaskbar)
                PostMessage(g_hwndTaskbar, WM_COMMAND, CMD_STARTMENU, 0L);
            break;
        case CMD_CASCADE:
        case CMD_TILE:
        case CMD_TILEVERT:
            ArrangeTasks(wParam);
            break;
        case CMD_MINIMIZEALL:
            TasksMinimizeAll();
            break;
        case CMD_ARRANGEICONS:
            DesktopArrange();
            break;
        default:
            StartMenuCommand(hwnd, wParam);
        }
        return 0;

    case WM_NS_OPENPATH:
        if (GlobalGetAtomName((ATOM)wParam, szPath, sizeof(szPath)))
            OpenFromCommandLine(szPath);
        GlobalDeleteAtom((ATOM)wParam);
        return 0;

    case WM_NS_SHELLHOOK:
        TaskbarRefresh();
        return 0;

    case WM_NS_DESKMENU:
        DesktopMenu((int)LOWORD(lParam), (int)HIWORD(lParam));
        return 0;

    case WM_NS_WINKEY:
        if (g_hwndTaskbar)
            PostMessage(g_hwndTaskbar, WM_COMMAND, CMD_STARTMENU, 0L);
        return 0;

    case WM_NS_FILECHANGE:
        OnFileChange(hwnd);
        return 0;

    case WM_TIMER:
        KillTimer(hwnd, wParam);
        if (wParam == TIMER_FILECHANGE)
            RefreshChanged();
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam)
            UnloadHookDll();
        return 0;

    case WM_CLOSE:
        DoShutdownDialog();
        return 0;

    case WM_DESTROY:
        UnloadHookDll();
        g_hwndMain = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static BOOL RegisterClasses(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_NEWSHELL));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLS_MAIN;
    if (!RegisterClass(&wc))
        return FALSE;

    wc.lpfnWndProc = TaskbarWndProc;
    wc.lpszClassName = CLS_TASKBAR;
    if (!RegisterClass(&wc))
        return FALSE;

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = DeskIconWndProc;
    wc.lpszClassName = CLS_DESKICON;
    if (!RegisterClass(&wc))
        return FALSE;

    return FolderRegister() && FViewRegister() && ExplorerRegister()
        && TaskManRegister() && BinRegister() && FileOpRegister();
}

/*
 * Windows starts the task manager (taskman.exe=) for Ctrl+Esc and desktop
 * double-clicks with no arguments or with numbers (the click position).
 */
static BOOL IsTaskmanCommandLine(LPCSTR lpCmdLine)
{
    for (; *lpCmdLine; lpCmdLine++)
        if (*lpCmdLine != ' ' && (*lpCmdLine < '0' || *lpCmdLine > '9'))
            return FALSE;
    return TRUE;
}

/* Hands the command line to the copy that is already running */
static void ForwardToRunningCopy(LPSTR lpCmdLine)
{
    HWND hwnd = FindWindow(CLS_MAIN, NULL);
    ATOM atom;

    if (!hwnd)
        return;
    while (*lpCmdLine == ' ')
        lpCmdLine++;
    if (IsTaskmanCommandLine(lpCmdLine)) {
        PostMessage(hwnd, WM_COMMAND,
                    CfgGetInt("Taskbar", "CtrlEscStart", 0) ? CMD_STARTMENU : CMD_TASKMAN,
                    0L);
    } else {
        atom = GlobalAddAtom(lpCmdLine);
        if (atom)
            PostMessage(hwnd, WM_NS_OPENPATH, atom, 0L);
    }
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;

    (void)nCmdShow;
    if (hPrevInstance) {
        ForwardToRunningCopy(lpCmdLine);
        return 0;
    }

    g_hInst = hInstance;
    GetWindowsDirectory(g_szWinDir, sizeof(g_szWinDir));
    CfgLoad();
    g_bIsShell = DetectShell();

    g_hfontUI = CreateUIFont(8, FALSE);
    g_hfontBold = CreateUIFont(8, TRUE);
    IconsInit();

    if (!RegisterClasses())
        return 1;

    g_hwndMain = CreateWindow(CLS_MAIN, NS_APPNAME, WS_OVERLAPPEDWINDOW,
                              0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hwndMain)
        return 1;

    if (!TaskbarCreate()) {
        DestroyWindow(g_hwndMain);
        return 1;
    }
    if (g_bIsShell)
        DesktopDefaultColor();
    DesktopCreate();
    LoadHookDll();
    HookApplySettings();

    if (g_bIsShell && g_cfg.bRunStartup && !IsShiftDown()) {
        RunWinIniLoadRun();
        PmRunStartupGroup();
    }
    OpenFromCommandLine(lpCmdLine);

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (FindIsDialogMessage(&msg) || TaskManIsDialogMessage(&msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    StartMenuFree();
    IconsFree();
    DeleteObject(g_hfontUI);
    DeleteObject(g_hfontBold);
    return msg.wParam;
}
