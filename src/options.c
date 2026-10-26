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
 * options.c - the Options dialog
 *
 * Windows 3.1 has no tab control, so the dialog has a list of pages on
 * the left; the controls of all pages are made from the table below when
 * it opens and those of the chosen page are shown. Every option is a
 * key in NEWSHELL.INI; OK writes them and applies what changed.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"
#include <commdlg.h>

#define ID_FIRST        2000        /* control ids: ID_FIRST + table index */
#define ID_BROWSE       2200        /* "..." buttons: ID_BROWSE + table index */

#define OT_CHECK        1           /* check box, key = 0 or 1 */
#define OT_CHECKNOT     2           /* check box for key = 0 */
#define OT_EDIT         3           /* text */
#define OT_FILE         4           /* text with a Browse button */
#define OT_COMBO        5           /* drop-down list, key = its index */
#define OT_COLOR        6           /* button choosing a colour */
#define OT_RESETCOLORS  7

typedef struct tagOPTDEF {
    BYTE   bPage;
    BYTE   bType;
    LPCSTR lpText;
    LPCSTR lpSection;
    LPCSTR lpKey;
    int    nDefault;
} OPTDEF;

static const char *s_szPages[] = {
    "Taskbar", "Desktop", "Folders", "Files", "Start-up", "Sounds, Web"
};
#define NPAGES  (sizeof(s_szPages) / sizeof(s_szPages[0]))

static const OPTDEF s_opts[] = {
    { 0, OT_CHECK,    "Show the &clock", "Taskbar", "Clock", 1 },
    { 0, OT_CHECK,    "&Hide minimized windows (reach them on the taskbar)", "Taskbar", "HideMinApps", 1 },
    { 0, OT_CHECK,    "Minimized icons go a&bove the taskbar", "Taskbar", "MinAbove", 0 },
    { 0, OT_CHECK,    "Keep &maximized windows above the taskbar", "Taskbar", "MaxClamp", 1 },
    { 0, OT_CHECK,    "Ctrl+&Esc opens the Start menu (not the Task Manager)", "Taskbar", "CtrlEscStart", 0 },
    { 0, OT_CHECK,    "&Windows key: Start menu, Win+E, Win+R, Win+F...", "Taskbar", "WinKey", 1 },
    { 0, OT_CHECK,    "&Auto hide", "Taskbar", "AutoHide", 0 },
    { 0, OT_CHECK,    "Show the &taskbar", "Taskbar", "Enabled", 1 },
    { 0, OT_CHECK,    "Hi&nts: captions, the date and free resources", "Taskbar", "Hints", 1 },
    { 0, OT_CHECK,    "&Document name first: \"README.TXT - Notepad\"", "Taskbar", "DocFirst", 0 },
    { 0, OT_CHECK,    "... with the document name in &lower case", "Taskbar", "LowerDoc", 0 },
    { 0, OT_EDIT,     "No &button for:", "Taskbar", "Exclude", 0 },
    { 0, OT_EDIT,     "In the t&ray:", "Taskbar", "Tray", 0 },

    { 1, OT_CHECK,    "Show desktop &icons", "Desktop", "Icons", 1 },
    { 1, OT_CHECK,    "Desktop right-click &menu", "Desktop", "Menu", 1 },
    { 1, OT_CHECK,    "Icons s&nap to a grid", "Desktop", "SnapToGrid", 1 },
    { 1, OT_CHECK,    "Open icons with a &single click", "Desktop", "SingleClick", 0 },
    { 1, OT_CHECK,    "&Arrows on shortcuts and aliases", "Desktop", "ShortcutArrows", 1 },
    { 1, OT_CHECK,    "Right click on minimize or maximize box &closes", "Desktop", "RightClose", 0 },
    { 1, OT_COLOR,    "&Taskbar colour...", "Colors", "Taskbar", NSC_TASKBAR },
    { 1, OT_COLOR,    "St&art menu colour...", "Colors", "StartMenu", NSC_STARTMENU },
    { 1, OT_COLOR,    "S&election colour...", "Colors", "Selection", NSC_SELECTION },
    { 1, OT_RESETCOLORS, "&Windows' colours", "Colors", "", 0 },

    { 2, OT_CHECK,    "Open folders in &new windows", "Folders", "NewWindow", 1 },
    { 2, OT_CHECK,    "Show hi&dden files", "Folders", "ShowHidden", 0 },
    { 2, OT_CHECK,    "&Toolbar", "Folders", "Toolbar", 1 },
    { 2, OT_CHECK,    "F&lat toolbar buttons", "Folders", "FlatToolbar", 0 },
    { 2, OT_CHECK,    "&Status bar", "Folders", "StatusBar", 1 },
    { 2, OT_CHECK,    "&Refresh folders on file changes", "Folders", "FileMonitor", 1 },
    { 2, OT_CHECK,    "&Capitalize names (Calc.exe)", "Folders", "Capitalize", 0 },
    { 2, OT_CHECK,    "No .&EXE for programs with their own icon", "Folders", "HideExtensions", 0 },
    { 2, OT_CHECK,    "S&hade selected icons", "Folders", "ShadeSelected", 1 },
    { 2, OT_CHECK,    "&Hints with file details", "Folders", "FileHints", 1 },
    { 2, OT_CHECK,    "Reopen &folder windows of the last session", "Folders", "SaveWindows", 0 },
    { 2, OT_CHECK,    "File S&ystem opens at the last folder", "Explorer", "RememberLast", 1 },
    { 2, OT_COMBO,    "New &windows:", "Folders", "WindowPos", 0 },
    { 2, OT_FILE,     "&Inspect with:", "Folders", "Inspect", 0 },

    { 3, OT_CHECK,    "Delete to the &Bin", "Folders", "DeleteToBin", 1 },
    { 3, OT_CHECK,    "Confirm &deleting", "Folders", "ConfirmDelete", 1 },
    { 3, OT_CHECK,    "Confirm &copying and moving", "Folders", "ConfirmCopy", 0 },
    { 3, OT_CHECK,    "Confirm &replacing files", "Folders", "ConfirmReplace", 1 },
    { 3, OT_CHECK,    "Confirm copying into an existing &folder", "Folders", "ConfirmFolders", 1 },
    { 3, OT_CHECK,    "Confirm for read-only, hidden and &system files", "Folders", "ConfirmProtected", 1 },
    { 3, OT_CHECK,    "&Ask Move, Copy or Alias after every drag", "Folders", "AskOnDrop", 0 },
    { 3, OT_CHECK,    "Drags c&opy (Shift moves)", "Folders", "DragCopy", 0 },
    { 3, OT_CHECK,    "&Icons next to the results of Find", "Find", "Icons", 1 },

    { 4, OT_CHECK,    "Run &StartUp programs (when NewShell16 is the shell)", "Startup", "Run", 1 },
    { 4, OT_CHECK,    "Let setup programs add &groups (Program Manager DDE)", "Startup", "ProgmanDDE", 1 },
    { 4, OT_CHECK,    "S&plash screen", "Preferences", "Splash", 0 },
    { 4, OT_CHECK,    "&Tip of the Day", "Preferences", "ShowTips", 0 },
    { 4, OT_CHECK,    "Open the &Computer window", "Preferences", "OpenComputer", 0 },
    { 4, OT_CHECK,    "Closing the Computer window &shuts down", "Preferences", "ComputerQuits", 0 },
    { 4, OT_CHECK,    "S&hut-down screen", "Preferences", "ShutdownScreen", 1 },
    { 4, OT_CHECK,    "&Users: log on, settings for each user", "Users", "Enabled", 0 },

    { 5, OT_FILE,     "&Start-up:", "Sounds", "Startup", 0 },
    { 5, OT_FILE,     "&Exit:", "Sounds", "Exit", 0 },
    { 5, OT_FILE,     "Start &menu:", "Sounds", "StartMenu", 0 },
    { 5, OT_FILE,     "&Open folder:", "Sounds", "OpenFolder", 0 },
    { 5, OT_FILE,     "Empty &Bin:", "Sounds", "EmptyBin", 0 },
    { 5, OT_FILE,     "&Web browser:", "Internet", "Browser", 0 },
};
#define NOPTS   (sizeof(s_opts) / sizeof(s_opts[0]))

static int      s_nPage;
static COLORREF s_clr[NSC_COUNT];
static BOOL     s_bClrSet[NSC_COUNT];

/* The users list lives in the shared NEWSHELL.INI */
static LPCSTR IniFor(const OPTDEF *o)
{
    return lstrcmpi(o->lpSection, "Users") == 0 ? "NEWSHELL.INI" : NS_INIFILE;
}

static void MakeControls(HWND hDlg)
{
    HFONT hf = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0L);
    RECT rcPage, rc;
    char szValue[MAXPATHLEN];
    int yPage[NPAGES], i, x, cx, y;
    const OPTDEF *o;
    HWND hwnd;

    GetWindowRect(GetDlgItem(hDlg, IDC_OPTFRAME), &rcPage);
    ScreenToClient(hDlg, (POINT FAR *)&rcPage.left);
    ScreenToClient(hDlg, (POINT FAR *)&rcPage.right);
    /* dialog units: 4 x 8 per base unit */
    SetRect(&rc, 0, 0, 4, 8);
    MapDialogRect(hDlg, &rc);
    x = rcPage.left + rc.right * 2;
    cx = rcPage.right - x - rc.right * 2;
    for (i = 0; i < NPAGES; i++)
        yPage[i] = rcPage.top + rc.bottom * 3 / 2;

    for (i = 0; i < NOPTS; i++) {
        o = &s_opts[i];
        y = yPage[o->bPage];
        hwnd = NULL;
        switch (o->bType) {
        case OT_CHECK:
        case OT_CHECKNOT:
            hwnd = CreateWindow("BUTTON", o->lpText, WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                x, y, cx, rc.bottom * 10 / 8, hDlg, (HMENU)(ID_FIRST + i),
                                g_hInst, NULL);
            {
                int n = GetPrivateProfileInt(o->lpSection, o->lpKey, o->nDefault, IniFor(o));

                CheckDlgButton(hDlg, ID_FIRST + i, o->bType == OT_CHECK ? n != 0 : n == 0);
            }
            yPage[o->bPage] += rc.bottom * 12 / 8;
            break;
        case OT_EDIT:
        case OT_FILE:
        case OT_COMBO:
            hwnd = CreateWindow("STATIC", o->lpText, WS_CHILD | SS_LEFT, x, y + 2,
                                rc.right * 17, rc.bottom, hDlg, (HMENU)-1, g_hInst, NULL);
            if (hwnd) {
                SendMessage(hwnd, WM_SETFONT, (WPARAM)hf, 0L);
                SetProp(hwnd, "NSPage", (HANDLE)(o->bPage + 1));
            }
            if (o->bType == OT_COMBO) {
                hwnd = CreateWindow("COMBOBOX", "", WS_CHILD | WS_TABSTOP | WS_VSCROLL
                                    | CBS_DROPDOWNLIST, x + rc.right * 18, y,
                                    cx - rc.right * 18, rc.bottom * 6, hDlg,
                                    (HMENU)(ID_FIRST + i), g_hInst, NULL);
                SendMessage(hwnd, WM_SETFONT, (WPARAM)hf, 0L);
                SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"where Windows puts them");
                SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"cascaded");
                SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"where the last one was");
                SendMessage(hwnd, CB_SETCURSEL, CfgGetInt(o->lpSection, o->lpKey, 0), 0L);
            } else {
                int cxEdit = cx - rc.right * 18 - (o->bType == OT_FILE ? rc.right * 11 : 0);

                hwnd = CreateWindow("EDIT", "", WS_CHILD | WS_TABSTOP | WS_BORDER
                                    | ES_AUTOHSCROLL, x + rc.right * 18, y, cxEdit,
                                    rc.bottom * 12 / 8, hDlg, (HMENU)(ID_FIRST + i), g_hInst,
                                    NULL);
                CfgGetStr(o->lpSection, o->lpKey, "", szValue, sizeof(szValue));
                SetWindowText(hwnd, szValue);
                if (o->bType == OT_FILE) {
                    HWND hwndB = CreateWindow("BUTTON", "...", WS_CHILD | WS_TABSTOP
                                              | BS_PUSHBUTTON,
                                              x + rc.right * 18 + cxEdit + rc.right,
                                              y - 1, rc.right * 10, rc.bottom * 14 / 8,
                                              hDlg, (HMENU)(ID_BROWSE + i), g_hInst, NULL);

                    if (hwndB) {
                        SendMessage(hwndB, WM_SETFONT, (WPARAM)hf, 0L);
                        SetProp(hwndB, "NSPage", (HANDLE)(o->bPage + 1));
                    }
                }
            }
            yPage[o->bPage] += rc.bottom * 16 / 8;
            break;
        case OT_COLOR:
        case OT_RESETCOLORS:
            hwnd = CreateWindow("BUTTON", o->lpText, WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                x, y, rc.right * 30, rc.bottom * 14 / 8, hDlg,
                                (HMENU)(ID_FIRST + i), g_hInst, NULL);
            yPage[o->bPage] += rc.bottom * 18 / 8;
            break;
        }
        if (hwnd) {
            SendMessage(hwnd, WM_SETFONT, (WPARAM)hf, 0L);
            SetProp(hwnd, "NSPage", (HANDLE)(o->bPage + 1));
        }
    }
}

/* Shows the controls of page n (their "NSPage" property is n + 1) */
static void ShowPage(HWND hDlg, int n)
{
    HWND hwnd;

    s_nPage = n;
    SetDlgItemText(hDlg, IDC_OPTFRAME, s_szPages[n]);
    for (hwnd = GetWindow(hDlg, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        int nPage = (int)GetProp(hwnd, "NSPage");

        if (nPage)
            ShowWindow(hwnd, nPage == n + 1 ? SW_SHOWNA : SW_HIDE);
    }
}

static void RemoveProps(HWND hDlg)
{
    HWND hwnd;

    for (hwnd = GetWindow(hDlg, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT))
        RemoveProp(hwnd, "NSPage");
}

static void PickColor(HWND hDlg, int nWhich)
{
    static COLORREF s_clrCustom[16];
    CHOOSECOLOR cc;

    _fmemset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hDlg;
    cc.rgbResult = s_bClrSet[nWhich] ? s_clr[nWhich] : NsColor(nWhich);
    cc.lpCustColors = s_clrCustom;
    cc.Flags = CC_RGBINIT;
    if (ChooseColor(&cc)) {
        s_clr[nWhich] = cc.rgbResult;
        s_bClrSet[nWhich] = TRUE;
    }
}

static void SaveOptions(HWND hDlg)
{
    static const char *s_szColorKey[NSC_COUNT] = { "Taskbar", "StartMenu", "Selection" };
    char szValue[MAXPATHLEN];
    const OPTDEF *o;
    int i, n;

    for (i = 0; i < NOPTS; i++) {
        o = &s_opts[i];
        switch (o->bType) {
        case OT_CHECK:
        case OT_CHECKNOT:
            n = IsDlgButtonChecked(hDlg, ID_FIRST + i) != 0;
            if (o->bType == OT_CHECKNOT)
                n = !n;
            wsprintf(szValue, "%d", n);
            WritePrivateProfileString(o->lpSection, o->lpKey, szValue, IniFor(o));
            break;
        case OT_EDIT:
        case OT_FILE:
            GetDlgItemText(hDlg, ID_FIRST + i, szValue, sizeof(szValue));
            CfgSetStr(o->lpSection, o->lpKey, szValue[0] ? szValue : NULL);
            break;
        case OT_COMBO:
            n = (int)SendDlgItemMessage(hDlg, ID_FIRST + i, CB_GETCURSEL, 0, 0L);
            CfgSetInt(o->lpSection, o->lpKey, n < 0 ? 0 : n);
            break;
        }
    }
    for (i = 0; i < NSC_COUNT; i++)
        if (s_bClrSet[i]) {
            wsprintf(szValue, "%d %d %d", GetRValue(s_clr[i]), GetGValue(s_clr[i]),
                     GetBValue(s_clr[i]));
            CfgSetStr("Colors", s_szColorKey[i], s_clr[i] == (COLORREF)-1 ? NULL : szValue);
        }
}

/* Everything that can change without a restart */
static void ApplyOptions(void)
{
    CfgLoad();
    NsColorsChanged();
    StartMenuOptionsChanged();
    HookApplySettings();
    TaskbarApplySettings();
    DesktopDestroy();
    DesktopCreate();
    FViewApplyOptions();
    FolderBarsChanged();
    if (g_hwndTaskbar)
        InvalidateRect(g_hwndTaskbar, NULL, FALSE);
}

BOOL CALLBACK NSEXPORT OptionsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    int i;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 0; i < NSC_COUNT; i++)
            s_bClrSet[i] = FALSE;
        for (i = 0; i < NPAGES; i++)
            SendDlgItemMessage(hDlg, IDC_OPTPAGES, LB_ADDSTRING, 0, (LPARAM)(LPSTR)s_szPages[i]);
        MakeControls(hDlg);
        SendDlgItemMessage(hDlg, IDC_OPTPAGES, LB_SETCURSEL, s_nPage, 0L);
        ShowPage(hDlg, s_nPage);
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDC_OPTPAGES) {
            if (HIWORD(lParam) == LBN_SELCHANGE) {
                i = (int)SendDlgItemMessage(hDlg, IDC_OPTPAGES, LB_GETCURSEL, 0, 0L);
                if (i >= 0 && i < NPAGES)
                    ShowPage(hDlg, i);
            }
            return TRUE;
        }
        if (wParam >= ID_BROWSE && wParam < ID_BROWSE + NOPTS) {
            i = wParam - ID_BROWSE;
            BrowseForFile(hDlg, ID_FIRST + i,
                          s_opts[i].bPage == 5 && lstrcmpi(s_opts[i].lpSection, "Sounds") == 0
                              ? "Sounds\0*.wav\0All Files (*.*)\0*.*\0"
                              : "Programs\0*.exe;*.pif;*.com;*.bat\0All Files (*.*)\0*.*\0",
                          s_opts[i].lpText[0] == '&' ? s_opts[i].lpText + 1 : s_opts[i].lpText);
            return TRUE;
        }
        if (wParam >= ID_FIRST && wParam < ID_FIRST + NOPTS) {
            i = wParam - ID_FIRST;
            if (s_opts[i].bType == OT_COLOR)
                PickColor(hDlg, s_opts[i].nDefault);
            else if (s_opts[i].bType == OT_RESETCOLORS)
                for (i = 0; i < NSC_COUNT; i++) {
                    s_clr[i] = (COLORREF)-1;
                    s_bClrSet[i] = TRUE;
                }
            return TRUE;
        }
        if (wParam == IDOK) {
            SaveOptions(hDlg);
            RemoveProps(hDlg);
            EndDialog(hDlg, IDOK);
            ApplyOptions();
            return TRUE;
        }
        if (wParam == IDCANCEL) {
            RemoveProps(hDlg);
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* nPage: the page to show first, -1 for the last one shown */
void OptionsDialogPage(HWND hwndOwner, int nPage)
{
    static BOOL s_bOpen;
    FARPROC lpfn;

    if (s_bOpen)
        return;
    if (nPage >= 0 && nPage < NPAGES)
        s_nPage = nPage;
    s_bOpen = TRUE;
    lpfn = MakeProcInstance((FARPROC)OptionsDlgProc, g_hInst);
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_OPTIONS), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
    s_bOpen = FALSE;
}

void OptionsDialog(HWND hwndOwner)
{
    OptionsDialogPage(hwndOwner, -1);
}
