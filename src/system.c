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
 * system.c - sounds, web addresses, splash and shutdown screens, tips,
 * and the folder windows of the last session
 *
 *   [Sounds]           ; a .WAV file for each event
 *   Startup=  Exit=  StartMenu=  OpenFolder=  EmptyBin=
 *
 *   [Internet]
 *   Browser=           ; program for http://, ftp://, www. ... (else
 *                      ; the program associated with .HTM)
 *
 *   [Preferences]
 *   Splash=0           ; NewShell16's name while it starts
 *   ShutdownScreen=1   ; a screen while Windows shuts down
 *   ShowTips=0         ; Tip of the Day at start-up
 *   OpenComputer=0     ; open the Computer window at start-up
 *   ComputerQuits=0    ; closing the Computer window shuts down
 *
 *   [Folders]
 *   SaveWindows=0      ; reopen the folder windows of the last session
 */
#include <string.h>
#include "newshell.h"

#define CLS_SCREEN      "NSScreen"
#define TIMER_SPLASH    1
#define SPLASH_MS       2500
#define MAX_SAVED       16

/* -------------------------------------------------------------------- */
/* Sounds                                                               */
/* -------------------------------------------------------------------- */

typedef BOOL (FAR PASCAL *SNDPLAYPROC)(LPCSTR, UINT);

/* Plays the sound set for an event, if any (MMSYSTEM loaded when needed) */
void PlayEventSound(LPCSTR lpEvent)
{
    static HINSTANCE s_hMM;
    static SNDPLAYPROC s_lpfnPlay;
    char szFile[MAXPATHLEN];
    UINT uOldMode;

    CfgGetStr("Sounds", lpEvent, "", szFile, sizeof(szFile));
    if (!szFile[0])
        return;
    if (!s_lpfnPlay) {
        uOldMode = SetErrorMode(0x8000);
        s_hMM = LoadLibrary("MMSYSTEM.DLL");
        SetErrorMode(uOldMode);
        if ((UINT)s_hMM < 32)
            return;
        s_lpfnPlay = (SNDPLAYPROC)GetProcAddress(s_hMM, "sndPlaySound");
        if (!s_lpfnPlay)
            return;
    }
    /* 1 = SND_ASYNC, 2 = SND_NODEFAULT */
    s_lpfnPlay(szFile, lstrcmpi(lpEvent, "Exit") == 0 ? 2 : 1 | 2);
}

/* -------------------------------------------------------------------- */
/* Web addresses                                                        */
/* -------------------------------------------------------------------- */

BOOL IsUrl(LPCSTR lp)
{
    static const char *s_szPrefix[] = { "http://", "https://", "ftp://", "gopher://",
                                        "mailto:", "news:", "telnet://", "www.", "ftp." };
    int i;

    while (*lp == ' ')
        lp++;
    for (i = 0; i < sizeof(s_szPrefix) / sizeof(s_szPrefix[0]); i++)
        if (_fstrnicmp(lp, s_szPrefix[i], lstrlen(s_szPrefix[i])) == 0)
            return TRUE;
    return FALSE;
}

/* Opens a web address with [Internet] Browser= or the .HTM program */
BOOL OpenUrl(HWND hwnd, LPCSTR lpUrl)
{
    char szBrowser[MAXPATHLEN], szCmd[MAXPATHLEN * 2];
    LPSTR p;

    while (*lpUrl == ' ')
        lpUrl++;
    CfgGetStr("Internet", "Browser", "", szBrowser, sizeof(szBrowser));
    if (!szBrowser[0]) {
        GetProfileString("Extensions", "htm", "", szBrowser, sizeof(szBrowser));
        /* "NETSCAPE.EXE ^.htm": the program only */
        for (p = szBrowser; *p && *p != ' '; p++)
            ;
        *p = '\0';
    }
    if (!szBrowser[0]) {
        MessageBox(hwnd, "No web browser is set. Choose one in the Options "
                   "(System), or associate .HTM files with it.", NS_APPNAME,
                   MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }
    wsprintf(szCmd, "%s %s", (LPSTR)szBrowser, lpUrl);
    if (WinExec(szCmd, SW_SHOWNORMAL) < 32) {
        ShowLaunchError(hwnd, 2, szBrowser);
        return FALSE;
    }
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* Splash and shutdown screens                                          */
/* -------------------------------------------------------------------- */

static HWND s_hwndSplash;

/* A gradient from black to the caption colour, top to bottom */
static void PaintGradient(HDC hdc, RECT FAR *lprc)
{
    COLORREF clr = GetSysColor(COLOR_ACTIVECAPTION);
    RECT rc;
    int y, cy = lprc->bottom - lprc->top;

    for (y = 0; y < cy; y += 4) {
        HBRUSH hbr = CreateSolidBrush(RGB(GetRValue(clr) * (long)y / cy,
                                          GetGValue(clr) * (long)y / cy,
                                          GetBValue(clr) * (long)y / cy));

        SetRect(&rc, lprc->left, lprc->top + y, lprc->right, lprc->top + y + 4);
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);
    }
}

LRESULT CALLBACK NSEXPORT ScreenWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT rc;
    HFONT hf, hfOld;
    LOGFONT lf;
    char szText[80];
    BOOL bSplash = hwnd == s_hwndSplash;

    switch (msg) {
    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        PaintGradient(ps.hdc, &rc);
        _fmemset(&lf, 0, sizeof(lf));
        lf.lfHeight = bSplash ? -36 : -28;
        lf.lfWeight = FW_BOLD;
        lf.lfItalic = TRUE;
        lf.lfOutPrecision = OUT_TT_PRECIS;
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
        lstrcpy(lf.lfFaceName, "Arial");
        hf = CreateFontIndirect(&lf);
        hfOld = SelectObject(ps.hdc, hf);
        SetBkMode(ps.hdc, TRANSPARENT);
        SetTextColor(ps.hdc, RGB(255, 255, 255));
        GetWindowText(hwnd, szText, sizeof(szText));
        DrawText(ps.hdc, szText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(ps.hdc, g_hfontUI);
        if (bSplash) {
            rc.top = rc.bottom - 24;
            wsprintf(szText, "A Calmira-style shell for Windows 3.1 - version %s",
                     (LPSTR)NS_VERSION);
            DrawText(ps.hdc, szText, -1, &rc, DT_CENTER | DT_SINGLELINE);
        }
        SelectObject(ps.hdc, hfOld);
        DeleteObject(hf);
        if (bSplash) {
            GetClientRect(hwnd, &rc);
            Draw3DEdge(ps.hdc, &rc, FALSE, TRUE);
        }
        EndPaint(hwnd, &ps);
        return 0;
    case WM_TIMER:
    case WM_LBUTTONDOWN:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (bSplash)
            s_hwndSplash = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL ScreenRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ScreenWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLS_SCREEN;
    return RegisterClass(&wc);
}

/* NewShell16's name in the middle of the screen while it starts */
void SplashShow(void)
{
    int cx = 360, cy = 140;

    if (!CfgGetInt("Preferences", "Splash", 0))
        return;
    s_hwndSplash = CreateWindowEx(WS_EX_TOPMOST, CLS_SCREEN, NS_APPNAME, WS_POPUP,
                                  (GetSystemMetrics(SM_CXSCREEN) - cx) / 2,
                                  (GetSystemMetrics(SM_CYSCREEN) - cy) / 2, cx, cy,
                                  NULL, NULL, g_hInst, NULL);
    if (!s_hwndSplash)
        return;
    ShowWindow(s_hwndSplash, SW_SHOWNOACTIVATE);
    UpdateWindow(s_hwndSplash);
    SetTimer(s_hwndSplash, TIMER_SPLASH, SPLASH_MS, NULL);
}

/*
 * Covers the screen while Windows shuts down; returns the window to
 * remove if the shutdown is refused (NULL if none was shown).
 */
HWND ShutdownScreenShow(void)
{
    HWND hwnd;

    if (!CfgGetInt("Preferences", "ShutdownScreen", 1))
        return NULL;
    hwnd = CreateWindowEx(WS_EX_TOPMOST, CLS_SCREEN, "Windows is shutting down...",
                          WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                          GetSystemMetrics(SM_CYSCREEN), NULL, NULL, g_hInst, NULL);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

/* -------------------------------------------------------------------- */
/* Tip of the Day                                                       */
/* -------------------------------------------------------------------- */

static const char *s_szTips[] = {
    "Drag files, folders or drives onto the desktop to make shortcuts to them.",
    "Drag with the right mouse button to choose between moving, copying and "
    "making an alias.",
    "Settings, Start Menu shows the Start menu as a tree that you can edit.",
    "Press Ctrl+Esc to open the Start menu, if the Options say so.",
    "Type the first letter of a name in a folder window to jump to it.",
    "Show the Desktop on the taskbar menu minimises every window; use it again "
    "to bring them back.",
    "Drop a document onto a program, on the desktop or in a folder, to open it "
    "with that program.",
    "Hold the mouse over a file for a moment to see its size, date and "
    "description.",
    "Ctrl+U takes the last thing you deleted out of the Bin again.",
    "Setup programs add their groups to the Programs menu, as they would to "
    "Program Manager.",
    "Right-click the folder tree of the File System window to expand whole "
    "branches.",
    "Set Filter in the View menu shows only the files you are interested in.",
    "Programs listed in Tray= in NEWSHELL.INI live beside the clock instead of "
    "having a task button.",
    "Hot keys of your own go into [Hotkeys] in NEWSHELL.INI, for example "
    "1=Ctrl+Alt+N,NOTEPAD.EXE."
};
#define NTIPS (sizeof(s_szTips) / sizeof(s_szTips[0]))

static int s_nTip;

BOOL CALLBACK NSEXPORT TipsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                   LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        s_nTip = CfgGetInt("Preferences", "NextTip", 0) % NTIPS;
        SetDlgItemText(hDlg, IDC_TIPTEXT, s_szTips[s_nTip]);
        CheckDlgButton(hDlg, IDC_TIPSHOW, CfgGetInt("Preferences", "ShowTips", 0));
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDC_TIPNEXT) {
            s_nTip = (s_nTip + 1) % NTIPS;
            SetDlgItemText(hDlg, IDC_TIPTEXT, s_szTips[s_nTip]);
            return TRUE;
        }
        if (wParam == IDOK || wParam == IDCANCEL) {
            CfgSetInt("Preferences", "NextTip", (s_nTip + 1) % NTIPS);
            CfgSetInt("Preferences", "ShowTips", IsDlgButtonChecked(hDlg, IDC_TIPSHOW) != 0);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void TipsDialog(HWND hwndOwner)
{
    FARPROC lpfn = MakeProcInstance((FARPROC)TipsDlgProc, g_hInst);

    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_TIPS), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
}

/* -------------------------------------------------------------------- */
/* Open windows between sessions                                        */
/* -------------------------------------------------------------------- */

/* Writes the open folder and File System windows into [Open Windows] */
void SaveOpenWindows(void)
{
    HWND hwnds[MAX_SAVED];
    char szKey[8], szValue[MAXPATHLEN + 40], szClass[16], szTitle[MAXPATHLEN + 20];
    WINDOWPLACEMENT wp;
    int i, n, nSaved = 0;
    LPSTR lpPath;

    WritePrivateProfileString("Open Windows", NULL, NULL, NS_INIFILE);
    if (!CfgGetInt("Folders", "SaveWindows", 0))
        return;
    n = FolderGetWindows(hwnds, MAX_SAVED);
    for (i = 0; i < n; i++) {
        GetClassName(hwnds[i], szClass, sizeof(szClass));
        GetWindowText(hwnds[i], szTitle, sizeof(szTitle));
        wp.length = sizeof(wp);
        GetWindowPlacement(hwnds[i], &wp);
        if (lstrcmpi(szClass, CLS_EXPLORER) == 0) {
            lpPath = _fstrstr(szTitle, " - ");
            lpPath = lpPath ? lpPath + 3 : szTitle;
            if (lstrcmpi(szTitle, "Browse Folders") == 0)
                continue;
        } else
            lpPath = FolderWindowPath(hwnds[i], szTitle);
        wsprintf(szValue, "%c|%d,%d,%d,%d,%d|%s",
                 lstrcmpi(szClass, CLS_EXPLORER) == 0 ? 'E' : 'F',
                 wp.rcNormalPosition.left, wp.rcNormalPosition.top,
                 wp.rcNormalPosition.right - wp.rcNormalPosition.left,
                 wp.rcNormalPosition.bottom - wp.rcNormalPosition.top,
                 IsIconic(hwnds[i]) ? 1 : IsZoomed(hwnds[i]) ? 2 : 0, lpPath);
        wsprintf(szKey, "%d", ++nSaved);
        CfgSetStr("Open Windows", szKey, szValue);
    }
}

static int NextInt(LPSTR FAR *pp)
{
    LPSTR p = *pp;
    int n = 0, sign = 1;

    if (*p == '-') {
        sign = -1;
        p++;
    }
    while (*p >= '0' && *p <= '9')
        n = n * 10 + (*p++ - '0');
    if (*p == ',' || *p == '|')
        p++;
    *pp = p;
    return n * sign;
}

/* Reopens the windows of the last session */
void RestoreOpenWindows(void)
{
    char szKey[8], szValue[MAXPATHLEN + 40];
    WINDOWPLACEMENT wp;
    HWND hwnd;
    LPSTR p;
    int i, x, y, cx, cy, nShow;

    if (!CfgGetInt("Folders", "SaveWindows", 0))
        return;
    for (i = 1; i <= MAX_SAVED; i++) {
        wsprintf(szKey, "%d", i);
        CfgGetStr("Open Windows", szKey, "", szValue, sizeof(szValue));
        if (!szValue[0] || szValue[1] != '|')
            break;
        p = szValue + 2;
        x = NextInt(&p);
        y = NextInt(&p);
        cx = NextInt(&p);
        cy = NextInt(&p);
        nShow = NextInt(&p);
        if (*p && !PathIsDirectory(p))
            continue;
        hwnd = szValue[0] == 'E' ? ExplorerOpen(p) : FolderOpen(p);
        if (!hwnd || cx < 50 || cy < 30)
            continue;
        wp.length = sizeof(wp);
        GetWindowPlacement(hwnd, &wp);
        SetRect(&wp.rcNormalPosition, x, y, x + cx, y + cy);
        wp.showCmd = nShow == 1 ? SW_SHOWMINNOACTIVE : nShow == 2 ? SW_SHOWMAXIMIZED
                                                                : SW_SHOWNORMAL;
        SetWindowPlacement(hwnd, &wp);
    }
}
