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
 * the pattern has no wildcards, and optionally for files containing a
 * text (ignoring case unless Match case is on). A right click on a
 * result offers Open, Open With, Open Folder, Delete, copying the names
 * or the details, and Properties. Results can be opened or their folder
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
static char    s_szText[64];        /* contents to look for, "" = any */
static BOOL    s_bCase;
static LPSTR   s_lpBuf;             /* file buffer for the contents search */
static FARPROC s_lpfnListProc;
static WNDPROC s_lpfnOldList;

#define FIND_BUF        4096

static BOOL PumpFind(void);

/* Whether the file contains s_szText */
static BOOL ContainsText(LPCSTR lpPath)
{
    HFILE hf;
    int nText = lstrlen(s_szText), nKeep = 0, n, i;
    BOOL bFound = FALSE;
    UINT uOldMode;

    if (!s_lpBuf)
        s_lpBuf = (LPSTR)malloc(FIND_BUF + 1);
    if (!s_lpBuf || nText == 0 || nText >= FIND_BUF / 2)
        return FALSE;
    uOldMode = SetErrorMode(1);
    hf = _lopen(lpPath, OF_READ | OF_SHARE_DENY_NONE);
    SetErrorMode(uOldMode);
    if (hf == HFILE_ERROR)
        return FALSE;
    for (;;) {
        n = _lread(hf, s_lpBuf + nKeep, FIND_BUF - nKeep);
        if (n <= 0 || n == (int)HFILE_ERROR)
            break;
        n += nKeep;
        if (!s_bCase)
            AnsiUpperBuff(s_lpBuf + nKeep, n - nKeep);
        for (i = 0; i + nText <= n; i++)
            if (s_lpBuf[i] == s_szText[0] && _fmemcmp(s_lpBuf + i, s_szText, nText) == 0) {
                bFound = TRUE;
                break;
            }
        if (bFound || !PumpFind())
            break;
        /* keep the end, a match may span two reads */
        nKeep = nText - 1;
        _fmemmove(s_lpBuf, s_lpBuf + n - nKeep, nKeep);
    }
    _lclose(hf);
    return bFound;
}

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
        if (Matches(ft.name)
            && (!s_szText[0] || (!(ft.attrib & _A_SUBDIR) && ContainsText(szPath))))
            AddResult(szPath);
        if (!s_hwndFind)
            return FALSE;
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
    GetDlgItemText(hDlg, IDC_FINDTEXT, s_szText, sizeof(s_szText));
    s_bCase = IsDlgButtonChecked(hDlg, IDC_FINDCASE);
    CfgSetInt("Find", "MatchCase", s_bCase);
    if (!s_bCase)
        AnsiUpper(s_szText);
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

/* -------------------------------------------------------------------- */
/* Result menu                                                          */
/* -------------------------------------------------------------------- */

#define IDM_R_OPEN      901
#define IDM_R_OPENWITH  902
#define IDM_R_FOLDER    903
#define IDM_R_DELETE    904
#define IDM_R_NAMES     905
#define IDM_R_DETAILS   906
#define IDM_R_PROPS     907

/* Right clicks on the results: select, then the menu */
LRESULT CALLBACK NSEXPORT FindListProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    if (msg == WM_RBUTTONDOWN) {
        int i = (int)SendMessage(hwnd, LB_GETTOPINDEX, 0, 0L) + (int)HIWORD(lParam) / ITEM_CY;

        SetFocus(hwnd);
        if (i < s_nResults)
            SendMessage(hwnd, LB_SETCURSEL, i, 0L);
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        POINT pt;
        HMENU hMenu;
        int i = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0L);

        if (i < 0 || i >= s_nResults)
            return 0;
        hMenu = CreatePopupMenu();
        AppendMenu(hMenu, MF_STRING, IDM_R_OPEN, "&Open");
        AppendMenu(hMenu, MF_STRING, IDM_R_OPENWITH, "Open &With...");
        AppendMenu(hMenu, MF_STRING, IDM_R_FOLDER, "Open &Folder");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, IDM_R_DELETE, "&Delete");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, IDM_R_NAMES, "&Copy File Names");
        AppendMenu(hMenu, MF_STRING, IDM_R_DETAILS, "Copy D&etails");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, IDM_R_PROPS, "P&roperties");
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        ClientToScreen(hwnd, &pt);
        TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, GetParent(hwnd), NULL);
        DestroyMenu(hMenu);
        return 0;
    }
    return CallWindowProc(s_lpfnOldList, hwnd, msg, wParam, lParam);
}

/* All results (bDetails: with size, date and folder) to the clipboard */
static void CopyResults(HWND hDlg, BOOL bDetails)
{
    struct find_t ft;
    char szPath[RESULT_LEN], szLine[RESULT_LEN + 64], szSize[24];
    HGLOBAL h;
    LPSTR lp;
    DWORD cb = 1;
    int i;

    cb += (DWORD)s_nResults * (RESULT_LEN + 64);
    if (cb > 60000L)
        cb = 60000L;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, cb);
    if (!h)
        return;
    lp = (LPSTR)GlobalLock(h);
    lp[0] = '\0';
    for (i = 0; i < s_nResults; i++) {
        lstrcpy(szPath, s_lpResults[i]);
        if (bDetails && _dos_findfirst(szPath, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM
                                       | _A_SUBDIR | _A_ARCH, &ft) == 0) {
            char szDir[RESULT_LEN];

            lstrcpy(szDir, szPath);
            PathParent(szDir);
            FormatBytes(ft.size, szSize);
            wsprintf(szLine, "%s\t%s\t%02u.%02u.%04u %02u:%02u\t%s\r\n", (LPSTR)ft.name,
                     (LPSTR)((ft.attrib & _A_SUBDIR) ? "Folder" : szSize),
                     ft.wr_date & 31, (ft.wr_date >> 5) & 15, (ft.wr_date >> 9) + 1980,
                     ft.wr_time >> 11, (ft.wr_time >> 5) & 63, (LPSTR)szDir);
        } else
            wsprintf(szLine, "%s\r\n", (LPSTR)szPath);
        if ((DWORD)lstrlen(lp) + lstrlen(szLine) + 1 >= cb)
            break;
        lstrcat(lp, szLine);
    }
    GlobalUnlock(h);
    if (OpenClipboard(hDlg)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, h);
        CloseClipboard();
    } else
        GlobalFree(h);
}

static void ResultCommand(HWND hDlg, UINT id)
{
    char szPath[RESULT_LEN];
    HGLOBAL hList = NULL;
    int i = (int)SendDlgItemMessage(hDlg, IDC_FINDLIST, LB_GETCURSEL, 0, 0L);

    if (i < 0 || i >= s_nResults)
        return;
    lstrcpy(szPath, s_lpResults[i]);
    switch (id) {
    case IDM_R_OPEN:
        OpenPath(hDlg, szPath);
        break;
    case IDM_R_OPENWITH:
        if (!PathIsDirectory(szPath))
            OpenWithDialog(hDlg, szPath);
        break;
    case IDM_R_FOLDER:
        PathParent(szPath);
        FolderOpen(szPath);
        break;
    case IDM_R_DELETE:
        PathListAdd(&hList, szPath);
        if (hList && FileOpDelete(hDlg, hList, g_cfg.bDeleteToBin)) {
            OFSTRUCT of;

            /* gone: take it out of the results */
            if (!PathIsDirectory(szPath) && OpenFile(szPath, &of, OF_EXIST) == HFILE_ERROR) {
                _fmemmove(&s_lpResults[i], &s_lpResults[i + 1],
                          (s_nResults - i - 1) * sizeof(RESULT));
                s_nResults--;
                SendDlgItemMessage(hDlg, IDC_FINDLIST, LB_DELETESTRING, s_nResults, 0L);
                InvalidateRect(GetDlgItem(hDlg, IDC_FINDLIST), NULL, TRUE);
            }
        }
        break;
    case IDM_R_NAMES:
        CopyResults(hDlg, FALSE);
        break;
    case IDM_R_DETAILS:
        CopyResults(hDlg, TRUE);
        break;
    case IDM_R_PROPS:
        PathListAdd(&hList, szPath);
        if (hList)
            PropertiesDialog(hDlg, hList);
        break;
    }
    if (hList)
        GlobalFree(hList);
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
    if (CfgGetInt("Find", "Icons", 1))
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
        CheckDlgButton(hDlg, IDC_FINDCASE, CfgGetInt("Find", "MatchCase", 0));
        EnableWindow(GetDlgItem(hDlg, IDC_FINDSTOP), FALSE);
        if (!s_lpfnListProc)
            s_lpfnListProc = MakeProcInstance((FARPROC)FindListProc, g_hInst);
        s_lpfnOldList = (WNDPROC)SetWindowLong(GetDlgItem(hDlg, IDC_FINDLIST), GWL_WNDPROC,
                                               (LONG)s_lpfnListProc);
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
        if (wParam >= IDM_R_OPEN && wParam <= IDM_R_PROPS) {
            ResultCommand(hDlg, wParam);
            return TRUE;
        }
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
