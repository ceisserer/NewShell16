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
 *   NEWSHELL /D              show the desktop (minimise all, again: restore)
 *   NEWSHELL path            open a folder, document or program
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"
#include <dde.h>
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
static NSHOOKSETHOTKEYS    s_lpfnHookSetHotkeys;

/* hot keys: the key for the hook, and the command it runs */
static WORD s_wHotkey[MAX_HOTKEYS];
static char s_szHotCmd[MAX_HOTKEYS][80];
static int  s_nHotkeys;
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
    s_lpfnHookSetHotkeys = (NSHOOKSETHOTKEYS)GetProcAddress(s_hHookDll, "NSHOOKSETHOTKEYS");
    if (!s_lpfnHookStart || !s_lpfnHookStop || !s_lpfnHookGetChange) {
        FreeLibrary(s_hHookDll);
        s_hHookDll = NULL;
    }
}

/*
 * "Ctrl+Alt+E", "Win+R", "Shift+F5": the key for the hook, 0 if it is not
 * understood
 */
static WORD ParseHotkey(LPCSTR lp)
{
    static const struct { const char *pszName; BYTE bVk; } s_keys[] = {
        { "Space", VK_SPACE }, { "Enter", VK_RETURN }, { "Tab", VK_TAB },
        { "Esc", VK_ESCAPE }, { "Backspace", VK_BACK }, { "Insert", VK_INSERT },
        { "Delete", VK_DELETE }, { "Home", VK_HOME }, { "End", VK_END },
        { "PgUp", VK_PRIOR }, { "PgDn", VK_NEXT }, { "Up", VK_UP }, { "Down", VK_DOWN },
        { "Left", VK_LEFT }, { "Right", VK_RIGHT }, { "Pause", VK_PAUSE }
    };
    char szPart[16];
    WORD wMods = 0, wKey = 0;
    int n, i;

    while (*lp) {
        for (n = 0; *lp && *lp != '+'; lp++)
            if (n < sizeof(szPart) - 1 && *lp != ' ')
                szPart[n++] = *lp;
        szPart[n] = '\0';
        if (*lp == '+')
            lp++;
        if (!lstrcmpi(szPart, "Ctrl"))
            wMods |= HK_CTRL;
        else if (!lstrcmpi(szPart, "Alt"))
            wMods |= HK_ALT;
        else if (!lstrcmpi(szPart, "Shift"))
            wMods |= HK_SHIFT;
        else if (!lstrcmpi(szPart, "Win"))
            wMods |= HK_WIN;
        else if (n == 1)
            wKey = (WORD)(BYTE)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)szPart[0]);
        else if ((szPart[0] | 0x20) == 'f' && szPart[1] >= '1' && szPart[1] <= '9')
            wKey = VK_F1 + atoi(szPart + 1) - 1;
        else
            for (i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); i++)
                if (!lstrcmpi(szPart, s_keys[i].pszName))
                    wKey = s_keys[i].bVk;
    }
    /* a plain letter would take that key from every program */
    if (!wKey || !(wMods & (HK_CTRL | HK_ALT | HK_WIN)) && !(wKey >= VK_F1 && wKey <= VK_F12))
        return 0;
    return wKey | wMods;
}

static void AddHotkey(WORD wKey, LPCSTR lpCmd)
{
    if (!wKey || s_nHotkeys >= MAX_HOTKEYS)
        return;
    s_wHotkey[s_nHotkeys] = wKey;
    lstrcpyn(s_szHotCmd[s_nHotkeys], lpCmd, sizeof(s_szHotCmd[0]));
    s_nHotkeys++;
}

/*
 * The hot keys: those of Windows 95 with the Windows key, then
 * [Hotkeys] in NEWSHELL.INI, "1=Ctrl+Alt+N,NOTEPAD.EXE".
 */
static void LoadHotkeys(void)
{
    char szKey[8], szValue[120];
    LPSTR lpCmd;
    int i;

    s_nHotkeys = 0;
    if (g_cfg.bWinKey) {
        AddHotkey('E' | HK_WIN, "$Explore");
        AddHotkey('R' | HK_WIN, "$Run");
        AddHotkey('F' | HK_WIN, "$Find");
        AddHotkey('M' | HK_WIN, "$MinimizePrograms");
        AddHotkey('D' | HK_WIN, "$ClearDesktop");
        AddHotkey(VK_TAB | HK_WIN, "$TaskManager");
    }
    for (i = 1; i <= MAX_HOTKEYS; i++) {
        wsprintf(szKey, "%d", i);
        CfgGetStr("Hotkeys", szKey, "", szValue, sizeof(szValue));
        lpCmd = _fstrchr(szValue, ',');
        if (!lpCmd)
            continue;
        *lpCmd++ = '\0';
        while (*lpCmd == ' ')
            lpCmd++;
        AddHotkey(ParseHotkey(szValue), lpCmd);
    }
}

static void RunHotkey(int i)
{
    if (i < 0 || i >= s_nHotkeys)
        return;
    if (s_szHotCmd[i][0] == '$')
        ExecMacro(g_hwndMain, s_szHotCmd[i]);
    else
        LaunchCommand(g_hwndMain, s_szHotCmd[i], NULL, SW_SHOWNORMAL);
}

/* (Re)installs the hooks according to the settings */
void HookApplySettings(void)
{
    UINT uFlags = NSH_SHELL;

    if (!s_hHookDll)
        return;
    LoadHotkeys();
    if (s_lpfnHookSetHotkeys && s_nHotkeys) {
        s_lpfnHookSetHotkeys(s_wHotkey, s_nHotkeys);
        uFlags |= NSH_HOTKEYS;
    }
    if (g_cfg.bRightClose)
        uFlags |= NSH_RCLOSE;
    if (g_cfg.bMaxClamp)
        uFlags |= NSH_MAXIMIZE;
    if (g_cfg.bDesktopMenu)
        uFlags |= NSH_DESKTOP;
    if (g_cfg.bWinKey)
        uFlags |= NSH_WINKEY;
    if (g_cfg.bFileMonitor)
        uFlags |= NSH_FILES;
    uFlags = s_lpfnHookStart(g_hwndMain,
                             GetSystemMetrics(SM_CYSCREEN) - TaskbarHeight(), uFlags);
    /* File Manager drops onto the wallpaper reach the desktop subclass */
    DragAcceptFiles(GetDesktopWindow(), (uFlags & NSH_DESKTOP) && g_cfg.bDesktopIcons);
}

static void UnloadHookDll(void)
{
    if (!s_hHookDll)
        return;
    DragAcceptFiles(GetDesktopWindow(), FALSE);
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
    } else if (lpCmdLine[0] == '/' && (lpCmdLine[1] | 0x20) == 'd') {
        TasksClearDesktop();
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

/* Commands of the desktop and taskbar menus and of some macros */
void ShellCommand(UINT id)
{
    HWND hwnds[48];
    char szSaver[MAXPATHLEN + 4];
    int n;

    switch (id) {
    case CMD_TOGGLETASKBAR:
        CfgSetInt("Taskbar", "Enabled", !CfgGetInt("Taskbar", "Enabled", 1));
        TaskbarApplySettings();
        HookApplySettings();
        break;
    case CMD_CLEARDESKTOP:
        TasksClearDesktop();
        break;
    case CMD_CLOSEFOLDERS:
        FolderCloseAll();
        ExplorerCloseAll();
        break;
    case CMD_CASCADEFOLDERS:
        n = FolderGetWindows(hwnds, 48);
        CascadeList(hwnds, n);
        if (n)
            BringWindowToTop(hwnds[0]);
        break;
    case CMD_SCREENSAVER:
        GetPrivateProfileString("boot", "SCRNSAVE.EXE", "", szSaver, MAXPATHLEN,
                                "SYSTEM.INI");
        if (!szSaver[0]) {
            MessageBox(NULL, "No screen saver is chosen (Control Panel, Desktop).",
                       NS_APPNAME, MB_OK | MB_ICONINFORMATION);
            break;
        }
        lstrcat(szSaver, " /s");
        if (WinExec(szSaver, SW_SHOWNORMAL) < 32)
            ShowLaunchError(NULL, 2, szSaver);
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
        case CMD_LINEUPICONS:
            DesktopLineUp();
            break;
        case CMD_TOGGLETASKBAR:
        case CMD_CLEARDESKTOP:
        case CMD_CLOSEFOLDERS:
        case CMD_CASCADEFOLDERS:
        case CMD_SCREENSAVER:
            ShellCommand(wParam);
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

    case WM_DDE_INITIATE:
        DdeInitiate((HWND)wParam, (ATOM)LOWORD(lParam), (ATOM)HIWORD(lParam));
        return 0;

    case WM_NS_SHELLHOOK:
        TaskbarRefresh();
        return 0;

    case WM_NS_DESKMENU:
        DesktopMenu((int)LOWORD(lParam), (int)HIWORD(lParam));
        return 0;

    case WM_NS_HOTKEY:
        RunHotkey((int)wParam);
        return 0;

    case WM_NS_DESKDROP:
        DesktopDropFiles((HDROP)wParam);
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
        if (wParam) {
            if (g_bIsShell)
                BinExit();
            UnloadHookDll();
        }
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

    return FolderRegister() && FViewRegister() && ExplorerRegister() && ToolbarRegister()
        && TaskManRegister() && BinRegister() && FileOpRegister() && DdeRegister() && HintRegister();
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
