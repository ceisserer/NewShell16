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
 * find.c - Find files and folders
 *
 * A modeless dialog that searches a folder (optionally with sub-folders)
 * for names matching a wildcard pattern, or containing the given text if
 * the pattern has no wildcards. Results can be opened or their folder
 * shown.
 */
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "newshell.h"

#define MAX_RESULTS     500
#define RESULT_LEN      80
#define ITEM_CY         18
#define COL_NAME_CX     110

typedef char RESULT[RESULT_LEN];

static HWND    s_hwndFind;
static FARPROC s_lpfnFind;
static RESULT FAR *s_lpResults;
static int     s_nResults;
static BOOL    s_bSearching;
static BOOL    s_bStop;
static char    s_szPattern[64];
static BOOL    s_bWild;

static BOOL Matches(LPCSTR lpName)
{
    char szName[16];
    int n, m, i;

    if (s_bWild) {
        /* "*.*" style patterns should also match names without a dot */
        lstrcpy(szName, lpName);
        if (!_fstrchr(szName, '.'))
            lstrcat(szName, ".");
        return WildMatch(s_szPattern, lpName) || WildMatch(s_szPattern, szName);
    }
    n = lstrlen(lpName);
    m = lstrlen(s_szPattern);
    for (i = 0; i + m <= n; i++)
        if (_fstrnicmp(lpName + i, s_szPattern, m) == 0)
            return TRUE;
    return FALSE;
}

static BOOL PumpFind(void)
{
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (s_hwndFind && IsDialogMessage(s_hwndFind, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return !s_bStop && s_hwndFind != NULL;
}

static void AddResult(LPCSTR lpPath)
{
    if (s_nResults >= MAX_RESULTS)
        return;
    lstrcpyn(s_lpResults[s_nResults], lpPath, RESULT_LEN);
    SendDlgItemMessage(s_hwndFind, IDC_FINDLIST, LB_ADDSTRING, 0, (LPARAM)s_nResults);
    s_nResults++;
}

static void SetStatus(LPCSTR lpText)
{
    if (s_hwndFind)
        SetDlgItemText(s_hwndFind, IDC_FINDSTATUS, lpText);
}

static BOOL Search(LPCSTR lpDir, BOOL bSubdirs)
{
    struct find_t ft;
    char szPath[MAXPATHLEN];

    if (!PumpFind())
        return FALSE;
    SetStatus(lpDir);
    lstrcpy(szPath, lpDir);
    PathAppend(szPath, "*.*");
    if (_dos_findfirst(szPath, _A_NORMAL | _A_RDONLY | _A_SUBDIR | _A_ARCH
                       | (g_cfg.bShowHidden ? _A_HIDDEN | _A_SYSTEM : 0), &ft) != 0)
        return TRUE;
    do {
        if (ft.name[0] == '.')
            continue;
        lstrcpy(szPath, lpDir);
        PathAppend(szPath, ft.name);
        if (Matches(ft.name))
            AddResult(szPath);
        if (s_nResults >= MAX_RESULTS)
            return FALSE;
        if ((ft.attrib & _A_SUBDIR) && bSubdirs) {
            /* _dos_find state lives in ft, so recursing is safe */
            if (!Search(szPath, TRUE))
                return FALSE;
        }
    } while (_dos_findnext(&ft) == 0);
    return TRUE;
}

static void StartSearch(HWND hDlg)
{
    char szDir[MAXPATHLEN], szStatus[48];
    LPSTR p;

    if (s_bSearching)
        return;
    GetDlgItemText(hDlg, IDC_FINDNAME, s_szPattern, sizeof(s_szPattern));
    GetDlgItemText(hDlg, IDC_FINDDIR, szDir, sizeof(szDir));
    if (s_szPattern[0] == '\0')
        lstrcpy(s_szPattern, "*.*");
    s_bWild = FALSE;
    for (p = s_szPattern; *p; p++)
        if (*p == '*' || *p == '?')
            s_bWild = TRUE;
    PathNormalize(szDir);
    if (!PathIsDirectory(szDir)) {
        MessageBox(hDlg, "The folder to look in does not exist.", "Find",
                   MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    CfgSetStr("Find", "Name", s_szPattern);
    CfgSetStr("Find", "Dir", szDir);

    SendDlgItemMessage(hDlg, IDC_FINDLIST, LB_RESETCONTENT, 0, 0L);
    s_nResults = 0;
    s_bSearching = TRUE;
    s_bStop = FALSE;
    EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_FINDSTOP), TRUE);
    SetCursor(LoadCursor(NULL, IDC_WAIT));

    Search(szDir, IsDlgButtonChecked(hDlg, IDC_FINDSUB));

    s_bSearching = FALSE;
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    if (!s_hwndFind)
        return;                             /* closed while searching */
    EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
    EnableWindow(GetDlgItem(hDlg, IDC_FINDSTOP), FALSE);
    wsprintf(szStatus, s_nResults >= MAX_RESULTS ? "%d+ item(s) found"
                                                 : "%d item(s) found", s_nResults);
    SetStatus(szStatus);
}

static int SelectedResult(HWND hDlg)
{
    int i = (int)SendDlgItemMessage(hDlg, IDC_FINDLIST, LB_GETCURSEL, 0, 0L);
    return (i >= 0 && i < s_nResults) ? i : -1;
}

static void DrawResult(DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText;
    char szPath[RESULT_LEN];
    BOOL bSel = (lpdis->itemState & ODS_SELECTED) != 0;
    HFONT hfOld;

    FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
    if ((int)lpdis->itemID < 0 || (int)lpdis->itemID >= s_nResults)
        return;
    lstrcpy(szPath, s_lpResults[lpdis->itemID]);
    DrawIconSmall(hdc, rc.left + 2, rc.top + 1,
                  IconForFile(szPath, PathIsDirectory(szPath)),
                  GetSysColor(COLOR_WINDOW));
    hfOld = SelectObject(hdc, g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetRect(&rcText, rc.left + 20, rc.top + 1,
            rc.left + 24 + TextWidth(hdc, PathFileName(szPath)), rc.bottom - 1);
    if (bSel)
        FillSolid(hdc, &rcText, GetSysColor(COLOR_HIGHLIGHT));
    SetTextColor(hdc, GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    rcText.left += 2;
    DrawText(hdc, PathFileName(szPath), -1, &rcText,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    PathParent(szPath);
    SetRect(&rcText, rc.left + COL_NAME_CX, rc.top, rc.right, rc.bottom);
    DrawText(hdc, szPath, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hfOld);
}

BOOL CALLBACK NSEXPORT FindDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                   LPARAM lParam)
{
    char szPath[RESULT_LEN];
    int i;

    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_FINDNAME, s_szPattern);
        CheckDlgButton(hDlg, IDC_FINDSUB, TRUE);
        EnableWindow(GetDlgItem(hDlg, IDC_FINDSTOP), FALSE);
        SendDlgItemMessage(hDlg, IDC_FINDICON, STM_SETICON,
                           (WPARAM)IconStock(IDI_FIND), 0L);
        return TRUE;

    case WM_QUERYDRAGICON:
        return (BOOL)(UINT)IconStock(IDI_FIND);

    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lParam)->itemHeight = ITEM_CY;
        return TRUE;

    case WM_DRAWITEM:
        DrawResult((DRAWITEMSTRUCT FAR *)lParam);
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            StartSearch(hDlg);
            return TRUE;
        case IDC_FINDSTOP:
            s_bStop = TRUE;
            return TRUE;
        case IDC_FINDLIST:
            if (HIWORD(lParam) != LBN_DBLCLK)
                return TRUE;
            /* fall through */
        case IDC_FINDOPEN:
            if ((i = SelectedResult(hDlg)) >= 0) {
                lstrcpy(szPath, s_lpResults[i]);
                OpenPath(hDlg, szPath);
            }
            return TRUE;
        case IDC_FINDOPENDIR:
            if ((i = SelectedResult(hDlg)) >= 0) {
                lstrcpy(szPath, s_lpResults[i]);
                PathParent(szPath);
                FolderOpen(szPath);
            }
            return TRUE;
        case IDCANCEL:
            s_bStop = TRUE;
            DestroyWindow(hDlg);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        s_hwndFind = NULL;
        return TRUE;
    }
    return FALSE;
}

void FindOpen(LPCSTR lpDir)
{
    char szDir[MAXPATHLEN];

    if (!s_lpResults) {
        s_lpResults = (RESULT FAR *)malloc(MAX_RESULTS * sizeof(RESULT));
        if (!s_lpResults)
            return;
    }
    if (!s_hwndFind) {
        CfgGetStr("Find", "Name", "*.*", s_szPattern, sizeof(s_szPattern));
        if (!s_lpfnFind)
            s_lpfnFind = MakeProcInstance((FARPROC)FindDlgProc, g_hInst);
        s_hwndFind = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_FIND), NULL,
                                  (DLGPROC)s_lpfnFind);
        if (!s_hwndFind)
            return;
        s_nResults = 0;
    }
    if (lpDir && *lpDir)
        lstrcpyn(szDir, lpDir, sizeof(szDir));
    else
        CfgGetStr("Find", "Dir", "C:\\", szDir, sizeof(szDir));
    SetDlgItemText(s_hwndFind, IDC_FINDDIR, szDir);
    ShowWindow(s_hwndFind, SW_SHOW);
    BringWindowToTop(s_hwndFind);
    SetActiveWindow(s_hwndFind);
    TaskbarRefresh();
}

BOOL FindIsDialogMessage(MSG FAR *lpMsg)
{
    return s_hwndFind && IsDialogMessage(s_hwndFind, lpMsg);
}

void FindClose(void)
{
    if (s_hwndFind)
        DestroyWindow(s_hwndFind);
    if (s_lpResults) {
        free(s_lpResults);
        s_lpResults = NULL;
    }
}
