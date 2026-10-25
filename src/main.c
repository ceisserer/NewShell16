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
 * the running copy: without arguments the Start menu opens (so it can be
 * used as "taskman.exe=" for Ctrl+Esc), with a path that folder or file is
 * opened.
 */
#include <string.h>
#include "newshell.h"

HINSTANCE g_hInst;
HWND      g_hwndMain;
HWND      g_hwndTaskbar;
BOOL      g_bIsShell;
char      g_szWinDir[MAXPATHLEN];
HFONT     g_hfontUI;
HFONT     g_hfontBold;

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

static void OpenFromCommandLine(LPCSTR lpCmdLine)
{
    while (*lpCmdLine == ' ')
        lpCmdLine++;
    if (*lpCmdLine)
        LaunchCommand(g_hwndMain, lpCmdLine, NULL, SW_SHOWNORMAL);
}

void CloseShell(void)
{
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

LRESULT CALLBACK NSEXPORT MainWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    char szPath[MAXPATHLEN];

    switch (msg) {
    case WM_COMMAND:
        if (wParam == CMD_STARTMENU) {
            if (g_hwndTaskbar)
                PostMessage(g_hwndTaskbar, WM_COMMAND, CMD_STARTMENU, 0L);
        } else
            StartMenuCommand(hwnd, wParam);
        return 0;

    case WM_NS_OPENPATH:
        if (GlobalGetAtomName((ATOM)wParam, szPath, sizeof(szPath)))
            OpenFromCommandLine(szPath);
        GlobalDeleteAtom((ATOM)wParam);
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_CLOSE:
        if (g_bIsShell)
            DoShutdownDialog();
        else
            CloseShell();
        return 0;

    case WM_DESTROY:
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

    return FolderRegister();
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
        PostMessage(hwnd, WM_COMMAND, CMD_STARTMENU, 0L);
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
    DesktopCreate();

    if (g_bIsShell && g_cfg.bRunStartup && !IsShiftDown()) {
        RunWinIniLoadRun();
        PmRunStartupGroup();
    }
    OpenFromCommandLine(lpCmdLine);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    StartMenuFree();
    IconsFree();
    DeleteObject(g_hfontUI);
    DeleteObject(g_hfontBold);
    return msg.wParam;
}
