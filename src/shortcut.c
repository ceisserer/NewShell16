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
 * shortcut.c - the Shortcut Properties and Change Icon dialogs
 *
 * Used for desktop shortcuts and Start menu items: caption, command,
 * working directory, how the program is shown, and an icon picked from
 * an executable, DLL or icon file.
 */
#include <string.h>
#include "newshell.h"
#include <commdlg.h>

#define MAX_PICKICONS   64
#define PICK_CX         40

static LPSHORTCUT s_lpsc;
static HICON      s_hPick[MAX_PICKICONS];
static int        s_nPick;
static char       s_szPickFile[MAXPATHLEN];
static int        s_nPickIndex;

/* Common dialog to pick a file into an edit control */
BOOL BrowseForFile(HWND hDlg, int idEdit, LPCSTR lpFilter, LPCSTR lpTitle)
{
    OPENFILENAME ofn;
    char szFile[MAXPATHLEN];

    GetDlgItemText(hDlg, idEdit, szFile, sizeof(szFile));
    if (_fstrchr(szFile, ' '))
        *_fstrchr(szFile, ' ') = '\0';
    if (szFile[0] == '$' || PathIsDirectory(szFile))
        szFile[0] = '\0';
    _fmemset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg;
    ofn.lpstrFilter = lpFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrTitle = lpTitle;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileName(&ofn))
        return FALSE;
    SetDlgItemText(hDlg, idEdit, szFile);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* Change Icon                                                          */
/* -------------------------------------------------------------------- */

static void FreePickIcons(void)
{
    while (s_nPick > 0)
        DestroyIcon(s_hPick[--s_nPick]);
}

static void LoadPickIcons(HWND hDlg)
{
    char szFile[MAXPATHLEN];
    OFSTRUCT of;
    HWND hwndList = GetDlgItem(hDlg, IDC_CILIST);
    HCURSOR hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    UINT n, i;

    FreePickIcons();
    GetDlgItemText(hDlg, IDC_CIFILE, szFile, sizeof(szFile));
    if (szFile[0] && !_fstrchr(szFile, '\\')
        && OpenFile(szFile, &of, OF_EXIST) != HFILE_ERROR)
        OemToAnsi(of.szPathName, szFile);
    n = szFile[0] ? (UINT)ExtractIcon(g_hInst, szFile, (UINT)-1) : 0;
    for (i = 0; i < n && s_nPick < MAX_PICKICONS; i++) {
        HICON h = ExtractIcon(g_hInst, szFile, i);

        if ((UINT)h > 1)
            s_hPick[s_nPick++] = h;
    }
    SendMessage(hwndList, WM_SETREDRAW, FALSE, 0L);
    SendMessage(hwndList, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < (UINT)s_nPick; i++)
        SendMessage(hwndList, LB_ADDSTRING, 0, (LPARAM)i);
    SendMessage(hwndList, LB_SETCURSEL,
                s_nPickIndex < s_nPick ? s_nPickIndex : 0, 0L);
    SendMessage(hwndList, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(hwndList, NULL, TRUE);
    SetDlgItemText(hDlg, IDC_CISTATUS, s_nPick ? "" : "This file contains no icons.");
    SetCursor(hcurOld);
}

BOOL CALLBACK NSEXPORT ChangeIconDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                         LPARAM lParam)
{
    DRAWITEMSTRUCT FAR *lpdis;
    int i;

    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_CIFILE, s_szPickFile);
        SendDlgItemMessage(hDlg, IDC_CILIST, LB_SETCOLUMNWIDTH, PICK_CX, 0L);
        LoadPickIcons(hDlg);
        return TRUE;

    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lParam)->itemHeight = 40;
        return TRUE;

    case WM_DRAWITEM:
        lpdis = (DRAWITEMSTRUCT FAR *)lParam;
        FillSolid(lpdis->hDC, &lpdis->rcItem,
                  GetSysColor((lpdis->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT
                                                                 : COLOR_WINDOW));
        if ((int)lpdis->itemID >= 0 && (int)lpdis->itemID < s_nPick)
            DrawIcon(lpdis->hDC, lpdis->rcItem.left + (PICK_CX - 32) / 2,
                     lpdis->rcItem.top + 4, s_hPick[lpdis->itemID]);
        if (lpdis->itemState & ODS_FOCUS)
            DrawFocusRect(lpdis->hDC, &lpdis->rcItem);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDC_CIBROWSE:
            if (BrowseForFile(hDlg, IDC_CIFILE,
                              "Icons\0*.ico;*.exe;*.dll\0Programs\0*.exe\0"
                              "Libraries\0*.dll\0Icon files\0*.ico\0All Files (*.*)\0*.*\0",
                              "Change Icon")) {
                s_nPickIndex = 0;
                LoadPickIcons(hDlg);
            }
            return TRUE;
        case IDC_CIFILE:
            if (HIWORD(lParam) == EN_KILLFOCUS
                && SendDlgItemMessage(hDlg, IDC_CIFILE, EM_GETMODIFY, 0, 0L)) {
                SendDlgItemMessage(hDlg, IDC_CIFILE, EM_SETMODIFY, FALSE, 0L);
                s_nPickIndex = 0;
                LoadPickIcons(hDlg);
            }
            return TRUE;
        case IDC_CILIST:
            if (HIWORD(lParam) != LBN_DBLCLK)
                return TRUE;
            /* fall through */
        case IDOK:
            if (SendDlgItemMessage(hDlg, IDC_CIFILE, EM_GETMODIFY, 0, 0L)) {
                /* a file name typed and Enter pressed: show its icons */
                SendDlgItemMessage(hDlg, IDC_CIFILE, EM_SETMODIFY, FALSE, 0L);
                s_nPickIndex = 0;
                LoadPickIcons(hDlg);
                return TRUE;
            }
            GetDlgItemText(hDlg, IDC_CIFILE, s_szPickFile, sizeof(s_szPickFile));
            i = (int)SendDlgItemMessage(hDlg, IDC_CILIST, LB_GETCURSEL, 0, 0L);
            s_nPickIndex = i < 0 ? 0 : i;
            FreePickIcons();
            EndDialog(hDlg, s_szPickFile[0] ? IDOK : IDCANCEL);
            return TRUE;
        case IDCANCEL:
            FreePickIcons();
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Lets the user pick an icon; lpFile and *pnIndex are in and out */
BOOL ChangeIconDialog(HWND hwndOwner, LPSTR lpFile, int cbFile, int FAR *pnIndex)
{
    FARPROC lpfn = MakeProcInstance((FARPROC)ChangeIconDlgProc, g_hInst);
    int n;

    lstrcpyn(s_szPickFile, lpFile, sizeof(s_szPickFile));
    s_nPickIndex = *pnIndex;
    n = DialogBox(g_hInst, MAKEINTRESOURCE(IDD_CHANGEICON), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
    if (n != IDOK)
        return FALSE;
    lstrcpyn(lpFile, s_szPickFile, cbFile);
    *pnIndex = s_nPickIndex;
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* Shortcut Properties                                                  */
/* -------------------------------------------------------------------- */

/* The icon a shortcut shows: its own, or the one of what it runs */
HICON ShortcutIconOf(LPCSHORTCUT lpsc)
{
    char szFile[MAXPATHLEN];
    OFSTRUCT of;
    HICON hIcon;
    int i;

    if (lpsc->szIconFile[0]) {
        hIcon = IconFromFile(lpsc->szIconFile, lpsc->nIconIndex);
        if (hIcon)
            return hIcon;
    }
    if (_fstrnicmp(lpsc->szCmd, "$Folder", 7) == 0)
        return IconStock(IDI_FOLDER);
    if (lpsc->szCmd[0] == '$') {
        if (_fstrnicmp(lpsc->szCmd, "$Explore", 8) == 0)
            return IconStock(IDI_EXPLORER);
        if (_fstrnicmp(lpsc->szCmd, "$System", 7) == 0)
            return IconStock(IDI_COMPUTER);
        if (_fstrnicmp(lpsc->szCmd, "$Find", 5) == 0)
            return IconStock(IDI_FIND);
        if (_fstrnicmp(lpsc->szCmd, "$TaskManager", 12) == 0)
            return IconStock(IDI_TASKMAN);
        return IconStock(IDI_APP);
    }
    for (i = 0; lpsc->szCmd[i] && lpsc->szCmd[i] != ' ' && i < sizeof(szFile) - 1; i++)
        szFile[i] = lpsc->szCmd[i];
    szFile[i] = '\0';
    if (PathIsDirectory(szFile)) {
        if (lstrlen(szFile) <= 3) {
            BYTE bType;
            return IconStock(DriveIcon((szFile[0] & ~0x20) - 'A', &bType));
        }
        return IconStock(IDI_FOLDER);
    }
    if (OpenFile(szFile, &of, OF_EXIST) == HFILE_ERROR)
        return IconStock(IDI_APP);
    OemToAnsi(of.szPathName, szFile);
    return IconForFile(szFile, FALSE);
}

static void ShowIcon(HWND hDlg)
{
    SHORTCUT sc;

    sc = *s_lpsc;
    GetDlgItemText(hDlg, IDC_SCCMD, sc.szCmd, sizeof(sc.szCmd));
    SendDlgItemMessage(hDlg, IDC_SCICON, STM_SETICON, (WPARAM)ShortcutIconOf(&sc), 0L);
}

BOOL CALLBACK NSEXPORT ShortcutDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    char szFile[MAXPATHLEN];
    int i;

    switch (msg) {
    case WM_INITDIALOG:
        if (lParam)
            SetWindowText(hDlg, (LPCSTR)lParam);
        SetDlgItemText(hDlg, IDC_SCCAPTION, s_lpsc->szCaption);
        SetDlgItemText(hDlg, IDC_SCCMD, s_lpsc->szCmd);
        SetDlgItemText(hDlg, IDC_SCDIR, s_lpsc->szDir);
        SendDlgItemMessage(hDlg, IDC_SCCAPTION, EM_LIMITTEXT, sizeof(s_lpsc->szCaption) - 1, 0L);
        SendDlgItemMessage(hDlg, IDC_SCCMD, EM_LIMITTEXT, sizeof(s_lpsc->szCmd) - 1, 0L);
        SendDlgItemMessage(hDlg, IDC_SCDIR, EM_LIMITTEXT, sizeof(s_lpsc->szDir) - 1, 0L);
        SendDlgItemMessage(hDlg, IDC_SCSHOW, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"Normal window");
        SendDlgItemMessage(hDlg, IDC_SCSHOW, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"Minimized");
        SendDlgItemMessage(hDlg, IDC_SCSHOW, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"Maximized");
        SendDlgItemMessage(hDlg, IDC_SCSHOW, CB_SETCURSEL,
                           s_lpsc->nShow >= 0 && s_lpsc->nShow <= 2 ? s_lpsc->nShow : 0, 0L);
        if (s_lpsc->bNoCommand) {
            /* the Computer and the Bin: only the caption can change */
            for (i = IDC_SCCMD; i <= IDC_SCCHANGEICON; i++)
                EnableWindow(GetDlgItem(hDlg, i), FALSE);
        }
        ShowIcon(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDC_SCCMD:
            if (HIWORD(lParam) == EN_KILLFOCUS)
                ShowIcon(hDlg);
            return TRUE;
        case IDC_SCBROWSE:
            if (BrowseForFile(hDlg, IDC_SCCMD, "Programs\0*.exe;*.pif;*.com;*.bat\0"
                              "All Files (*.*)\0*.*\0", "Browse"))
                ShowIcon(hDlg);
            return TRUE;
        case IDC_SCCHANGEICON:
            lstrcpy(szFile, s_lpsc->szIconFile);
            if (!szFile[0]) {
                GetDlgItemText(hDlg, IDC_SCCMD, szFile, sizeof(szFile));
                for (i = 0; szFile[i] && szFile[i] != ' '; i++)
                    ;
                szFile[i] = '\0';
                if (szFile[0] == '$' || PathIsDirectory(szFile)
                    || lstrcmpi(PathExtension(szFile), "exe") != 0)
                    lstrcpy(szFile, "PROGMAN.EXE");
            }
            i = s_lpsc->nIconIndex;
            if (ChangeIconDialog(hDlg, szFile, sizeof(szFile), &i)) {
                lstrcpyn(s_lpsc->szIconFile, szFile, sizeof(s_lpsc->szIconFile));
                s_lpsc->nIconIndex = i;
                ShowIcon(hDlg);
            }
            return TRUE;
        case IDOK:
            GetDlgItemText(hDlg, IDC_SCCAPTION, s_lpsc->szCaption, sizeof(s_lpsc->szCaption));
            if (!s_lpsc->bNoCommand) {
                GetDlgItemText(hDlg, IDC_SCCMD, s_lpsc->szCmd, sizeof(s_lpsc->szCmd));
                GetDlgItemText(hDlg, IDC_SCDIR, s_lpsc->szDir, sizeof(s_lpsc->szDir));
                s_lpsc->nShow = (int)SendDlgItemMessage(hDlg, IDC_SCSHOW, CB_GETCURSEL, 0, 0L);
                if (s_lpsc->nShow < 0)
                    s_lpsc->nShow = 0;
            }
            if (!s_lpsc->szCaption[0]) {
                MessageBox(hDlg, "Please type a caption.", NS_APPNAME,
                           MB_OK | MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(hDlg, IDC_SCCAPTION));
                return TRUE;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Edits *lpsc; returns TRUE when OK was chosen */
BOOL ShortcutDialog(HWND hwndOwner, LPSHORTCUT lpsc, LPCSTR lpTitle)
{
    FARPROC lpfn = MakeProcInstance((FARPROC)ShortcutDlgProc, g_hInst);
    SHORTCUT sc = *lpsc;
    int n;

    s_lpsc = &sc;
    n = DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_SHORTCUT), hwndOwner,
                       (DLGPROC)lpfn, (LPARAM)lpTitle);
    FreeProcInstance(lpfn);
    if (n != IDOK)
        return FALSE;
    *lpsc = sc;
    return TRUE;
}

/* "caption|command|dir|show|iconfile|iconindex" */
void ShortcutParse(LPSTR lpValue, LPSHORTCUT lpsc)
{
    LPSTR f[6];
    int i, n = 0;

    _fmemset(lpsc, 0, sizeof(SHORTCUT));
    f[0] = lpValue;
    for (i = 1; i < 6; i++)
        f[i] = "";
    for (; *lpValue && n < 5; lpValue++)
        if (*lpValue == '|') {
            *lpValue = '\0';
            f[++n] = lpValue + 1;
        }
    lstrcpyn(lpsc->szCaption, f[0], sizeof(lpsc->szCaption));
    lstrcpyn(lpsc->szCmd, f[1], sizeof(lpsc->szCmd));
    lstrcpyn(lpsc->szDir, f[2], sizeof(lpsc->szDir));
    lpsc->nShow = f[3][0] ? f[3][0] - '0' : 0;
    lstrcpyn(lpsc->szIconFile, f[4], sizeof(lpsc->szIconFile));
    for (lpsc->nIconIndex = 0, lpValue = f[5]; *lpValue >= '0' && *lpValue <= '9'; lpValue++)
        lpsc->nIconIndex = lpsc->nIconIndex * 10 + (*lpValue - '0');
}

void ShortcutFormat(LPCSHORTCUT lpsc, LPSTR lpBuf)
{
    if (!lpsc->szDir[0] && !lpsc->nShow && !lpsc->szIconFile[0])
        wsprintf(lpBuf, "%s|%s", (LPSTR)lpsc->szCaption, (LPSTR)lpsc->szCmd);
    else
        wsprintf(lpBuf, "%s|%s|%s|%d|%s|%d", (LPSTR)lpsc->szCaption,
                 (LPSTR)lpsc->szCmd, (LPSTR)lpsc->szDir, lpsc->nShow,
                 (LPSTR)lpsc->szIconFile, lpsc->nIconIndex);
}

/* Runs a shortcut (or Start menu item) */
BOOL ShortcutRun(HWND hwnd, LPCSHORTCUT lpsc)
{
    static const int s_nShow[] = { SW_SHOWNORMAL, SW_SHOWMINNOACTIVE, SW_SHOWMAXIMIZED };

    if (lpsc->szCmd[0] == '$')
        return ExecMacro(hwnd, lpsc->szCmd);
    return LaunchCommand(hwnd, lpsc->szCmd, lpsc->szDir,
                         s_nShow[lpsc->nShow >= 0 && lpsc->nShow <= 2 ? lpsc->nShow : 0]);
}
