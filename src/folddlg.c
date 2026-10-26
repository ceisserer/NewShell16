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
 * folddlg.c - dialogs of the folder windows
 *
 * Select Files, Set Filter, Go To, Open With, Describe and Columns, and
 * the file of 4DOS descriptions (DESCRIPT.ION) they write.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "newshell.h"

#define GOTO_HISTORY    12
#define MAX_PROGRAMS    64

static LPSTR s_lpBuf;           /* the text a dialog edits */
static int   s_cbBuf;
static UINT  s_uFlags;          /* dialog specific */
static LPCSTR s_lpFile;         /* Open With: the document */

static int DoDialog(int idd, FARPROC lpfnProc, HWND hwndOwner, LPARAM lParam)
{
    FARPROC lpfn = MakeProcInstance(lpfnProc, g_hInst);
    int n = DialogBoxParam(g_hInst, MAKEINTRESOURCE(idd), hwndOwner, (DLGPROC)lpfn, lParam);

    FreeProcInstance(lpfn);
    return n;
}

/* -------------------------------------------------------------------- */
/* Select Files                                                         */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT SelectDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                     LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_SELSPEC, s_lpBuf);
        SendDlgItemMessage(hDlg, IDC_SELSPEC, EM_SETSEL, 0, MAKELONG(0, -1));
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDOK || wParam == IDC_SELNONE) {
            GetDlgItemText(hDlg, IDC_SELSPEC, s_lpBuf, s_cbBuf);
            if (s_lpBuf[0])
                EndDialog(hDlg, wParam);
            return TRUE;
        }
        if (wParam == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Returns IDOK (select), IDC_SELNONE (deselect) or IDCANCEL */
int SelectFilesDialog(HWND hwndOwner, LPSTR lpSpec, int cbSpec)
{
    s_lpBuf = lpSpec;
    s_cbBuf = cbSpec;
    return DoDialog(IDD_SELECT, (FARPROC)SelectDlgProc, hwndOwner, 0L);
}

/* -------------------------------------------------------------------- */
/* Set Filter                                                           */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT FilterDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                     LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_FILTERSPEC, s_lpBuf);
        CheckDlgButton(hDlg, IDC_FILTERHIDDEN, s_uFlags != 0);
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDOK) {
            GetDlgItemText(hDlg, IDC_FILTERSPEC, s_lpBuf, s_cbBuf);
            if (!s_lpBuf[0])
                lstrcpyn(s_lpBuf, "*.*", s_cbBuf);
            s_uFlags = IsDlgButtonChecked(hDlg, IDC_FILTERHIDDEN);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (wParam == IDC_FILTERALL) {
            SetDlgItemText(hDlg, IDC_FILTERSPEC, "*.*");
            return TRUE;
        }
        if (wParam == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL FilterDialog(HWND hwndOwner, LPSTR lpSpec, int cbSpec, BOOL FAR *pbHidden)
{
    s_lpBuf = lpSpec;
    s_cbBuf = cbSpec;
    s_uFlags = *pbHidden;
    if (DoDialog(IDD_FILTER, (FARPROC)FilterDlgProc, hwndOwner, 0L) != IDOK)
        return FALSE;
    *pbHidden = s_uFlags != 0;
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* Go To                                                                */
/* -------------------------------------------------------------------- */

static void AddHistory(LPCSTR lpPath)
{
    char szKey[8], szOld[MAXPATHLEN], szCarry[MAXPATHLEN];
    int i;

    lstrcpyn(szCarry, lpPath, sizeof(szCarry));
    for (i = 1; i <= GOTO_HISTORY; i++) {
        wsprintf(szKey, "%d", i);
        CfgGetStr("Go To", szKey, "", szOld, sizeof(szOld));
        CfgSetStr("Go To", szKey, szCarry);
        if (!szOld[0] || lstrcmpi(szOld, lpPath) == 0)
            break;
        lstrcpy(szCarry, szOld);
    }
}

BOOL CALLBACK NSEXPORT GotoDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                   LPARAM lParam)
{
    char szKey[8], szPath[MAXPATHLEN];
    int i;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 1; i <= GOTO_HISTORY; i++) {
            wsprintf(szKey, "%d", i);
            CfgGetStr("Go To", szKey, "", szPath, sizeof(szPath));
            if (szPath[0])
                SendDlgItemMessage(hDlg, IDC_GOTOPATH, CB_ADDSTRING, 0, (LPARAM)(LPSTR)szPath);
        }
        SetDlgItemText(hDlg, IDC_GOTOPATH, s_lpBuf);
        SendDlgItemMessage(hDlg, IDC_GOTOPATH, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDOK) {
            GetDlgItemText(hDlg, IDC_GOTOPATH, szPath, sizeof(szPath));
            PathNormalize(szPath);
            if (!PathIsDirectory(szPath)) {
                MessageBox(hDlg, "This folder does not exist.", "Go To",
                           MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            lstrcpyn(s_lpBuf, szPath, s_cbBuf);
            AddHistory(szPath);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (wParam == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Asks for a folder; lpPath is the suggestion and the result */
BOOL GotoDialog(HWND hwndOwner, LPSTR lpPath, int cbPath)
{
    s_lpBuf = lpPath;
    s_cbBuf = cbPath;
    return DoDialog(IDD_GOTO, (FARPROC)GotoDlgProc, hwndOwner, 0L) == IDOK;
}

/* -------------------------------------------------------------------- */
/* Open With                                                            */
/* -------------------------------------------------------------------- */

/* The programs named in WIN.INI [Extensions], each once */
static void FillPrograms(HWND hwndList)
{
    char *pKeys, *p;
    char szValue[MAXPATHLEN], szProg[MAXPATHLEN];
    int i;

    pKeys = (char *)malloc(2048);
    if (!pKeys)
        return;
    GetProfileString("Extensions", NULL, "", pKeys, 2048);
    for (p = pKeys; *p; p += lstrlen(p) + 1) {
        GetProfileString("Extensions", p, "", szValue, sizeof(szValue));
        for (i = 0; szValue[i] && szValue[i] != ' ' && i < sizeof(szProg) - 1; i++)
            szProg[i] = szValue[i];
        szProg[i] = '\0';
        AnsiLower(szProg);
        if (szProg[0] && SendMessage(hwndList, LB_FINDSTRINGEXACT, (WPARAM)-1,
                                     (LPARAM)(LPSTR)szProg) == LB_ERR)
            SendMessage(hwndList, LB_ADDSTRING, 0, (LPARAM)(LPSTR)szProg);
    }
    free(pKeys);
}

BOOL CALLBACK NSEXPORT OpenWithDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    char szProg[MAXPATHLEN], szCmd[MAXPATHLEN * 2], szText[MAXPATHLEN + 40];
    char szName[MAXPATHLEN], szDir[MAXPATHLEN];
    LPSTR lpExt;
    int i;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        lstrcpyn(szName, s_lpFile, sizeof(szName));
        wsprintf(szText, "Choose the program to open %s with:", (LPSTR)PathFileName(szName));
        SetDlgItemText(hDlg, IDC_OWTEXT, szText);
        lpExt = PathExtension(szName);
        wsprintf(szText, "&Always use this program for .%s files", lpExt);
        SetDlgItemText(hDlg, IDC_OWALWAYS, szText);
        if (!*lpExt)
            EnableWindow(GetDlgItem(hDlg, IDC_OWALWAYS), FALSE);
        FillPrograms(GetDlgItem(hDlg, IDC_OWLIST));
        CfgGetStr("Open With", "Last", "notepad.exe", szProg, sizeof(szProg));
        SetDlgItemText(hDlg, IDC_OWPROG, szProg);
        return TRUE;
    case WM_COMMAND:
        switch (wParam) {
        case IDC_OWLIST:
            if (HIWORD(lParam) == LBN_SELCHANGE || HIWORD(lParam) == LBN_DBLCLK) {
                i = (int)SendDlgItemMessage(hDlg, IDC_OWLIST, LB_GETCURSEL, 0, 0L);
                if (i >= 0) {
                    SendDlgItemMessage(hDlg, IDC_OWLIST, LB_GETTEXT, i, (LPARAM)(LPSTR)szProg);
                    SetDlgItemText(hDlg, IDC_OWPROG, szProg);
                }
                if (HIWORD(lParam) == LBN_DBLCLK)
                    PostMessage(hDlg, WM_COMMAND, IDOK, 0L);
            }
            return TRUE;
        case IDC_OWBROWSE:
            BrowseForFile(hDlg, IDC_OWPROG, "Programs\0*.exe;*.pif;*.com;*.bat\0"
                          "All Files (*.*)\0*.*\0", "Open With");
            return TRUE;
        case IDOK:
            GetDlgItemText(hDlg, IDC_OWPROG, szProg, sizeof(szProg));
            if (!szProg[0])
                return TRUE;
            lstrcpyn(szDir, s_lpFile, sizeof(szDir));
            PathParent(szDir);
            wsprintf(szCmd, "%s %s", (LPSTR)szProg, s_lpFile);
            if (!LaunchCommand(hDlg, szCmd, szDir, SW_SHOWNORMAL))
                return TRUE;
            CfgSetStr("Open With", "Last", szProg);
            if (IsDlgButtonChecked(hDlg, IDC_OWALWAYS)) {
                lstrcpyn(szName, s_lpFile, sizeof(szName));
                lpExt = PathExtension(szName);
                AnsiLower(lpExt);
                wsprintf(szCmd, "%s ^.%s", (LPSTR)szProg, lpExt);
                WriteProfileString("Extensions", lpExt, szCmd);
                IconsForget();
                FViewRefreshPath(NULL);
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

/* Opens lpFile with a program the user picks */
BOOL OpenWithDialog(HWND hwndOwner, LPCSTR lpFile)
{
    s_lpFile = lpFile;
    return DoDialog(IDD_OPENWITH, (FARPROC)OpenWithDlgProc, hwndOwner, 0L) == IDOK;
}

/* -------------------------------------------------------------------- */
/* Describe                                                             */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT DescribeDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    char szText[80];

    switch (msg) {
    case WM_INITDIALOG:
        wsprintf(szText, "&Description of %s:", (LPSTR)lParam);
        SetDlgItemText(hDlg, IDC_DESCTEXT, szText);
        SetDlgItemText(hDlg, IDC_DESCEDIT, s_lpBuf);
        SendDlgItemMessage(hDlg, IDC_DESCEDIT, EM_LIMITTEXT, s_cbBuf - 1, 0L);
        SendDlgItemMessage(hDlg, IDC_DESCEDIT, EM_SETSEL, 0, MAKELONG(0, -1));
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDOK) {
            GetDlgItemText(hDlg, IDC_DESCEDIT, s_lpBuf, s_cbBuf);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (wParam == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL DescribeDialog(HWND hwndOwner, LPCSTR lpName, LPSTR lpDesc, int cbDesc)
{
    s_lpBuf = lpDesc;
    s_cbBuf = cbDesc;
    return DoDialog(IDD_DESCRIBE, (FARPROC)DescribeDlgProc, hwndOwner,
                     (LPARAM)lpName) == IDOK;
}

/*
 * Sets (or with "" removes) the description of lpName in lpDir's
 * DESCRIPT.ION, which is kept hidden as 4DOS does.
 */
BOOL DescriptionSet(LPCSTR lpDir, LPCSTR lpName, LPCSTR lpDesc)
{
    char szFile[MAXPATHLEN], szLineName[16];
    HFILE hf;
    LPSTR lpOld, lpNew, p, q;
    int cbOld = 0, cbNew = 0, n;
    BOOL bOK;

    lstrcpy(szFile, lpDir);
    PathAppend(szFile, "DESCRIPT.ION");
    lpOld = (LPSTR)malloc(16384 + 1);
    lpNew = (LPSTR)malloc(16384 + 200);
    if (!lpOld || !lpNew) {
        if (lpOld)
            free(lpOld);
        if (lpNew)
            free(lpNew);
        return FALSE;
    }
    hf = _lopen(szFile, OF_READ | OF_SHARE_DENY_WRITE);
    if (hf != HFILE_ERROR) {
        cbOld = _lread(hf, lpOld, 16384);
        _lclose(hf);
        if (cbOld < 0)
            cbOld = 0;
    }
    lpOld[cbOld] = '\0';

    /* copy every line but the one for lpName */
    for (p = lpOld; *p; p = q) {
        for (q = p; *q && *q != '\n'; q++)
            ;
        if (*q == '\n')
            q++;
        for (n = 0; p + n < q && p[n] != ' ' && p[n] != '\t' && p[n] != '\r'
                    && p[n] != '\n' && n < 15; n++)
            szLineName[n] = p[n];
        szLineName[n] = '\0';
        if (n == 0 || lstrcmpi(szLineName, lpName) == 0)
            continue;
        _fmemcpy(lpNew + cbNew, p, (int)(q - p));
        cbNew += (int)(q - p);
        if (q[-1] != '\n') {
            lpNew[cbNew++] = '\r';
            lpNew[cbNew++] = '\n';
        }
    }
    if (lpDesc[0])
        cbNew += wsprintf(lpNew + cbNew, "%s %s\r\n", lpName, lpDesc);

    _dos_setfileattr(szFile, _A_NORMAL);
    if (cbNew == 0) {
        remove(szFile);
        bOK = TRUE;
    } else {
        hf = _lcreat(szFile, 0);
        bOK = hf != HFILE_ERROR && _lwrite(hf, lpNew, cbNew) == (UINT)cbNew;
        if (hf != HFILE_ERROR)
            _lclose(hf);
        _dos_setfileattr(szFile, _A_HIDDEN | _A_ARCH);
    }
    free(lpOld);
    free(lpNew);
    return bOK;
}

/* -------------------------------------------------------------------- */
/* Columns                                                              */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT ColumnsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    int i;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 1; i <= 6; i++)
            CheckDlgButton(hDlg, IDC_COL_FIRST + i - 1, !(s_uFlags & (1 << i)));
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDOK) {
            s_uFlags = 0;
            for (i = 1; i <= 6; i++)
                if (!IsDlgButtonChecked(hDlg, IDC_COL_FIRST + i - 1))
                    s_uFlags |= 1 << i;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (wParam == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Bit n of *puHidden hides details column n (1 Size ... 6 Description) */
BOOL ColumnsDialog(HWND hwndOwner, UINT FAR *puHidden)
{
    s_uFlags = *puHidden;
    if (DoDialog(IDD_COLUMNS, (FARPROC)ColumnsDlgProc, hwndOwner, 0L) != IDOK)
        return FALSE;
    *puHidden = s_uFlags;
    return TRUE;
}
