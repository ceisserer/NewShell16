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
 * dialogs.c - Run, Shut Down and About dialogs
 */
#include <string.h>
#include "newshell.h"
#include <commdlg.h>

/* -------------------------------------------------------------------- */
/* Run                                                                  */
/* -------------------------------------------------------------------- */

static void BrowseForProgram(HWND hDlg)
{
    OPENFILENAME ofn;
    char szFile[MAXPATHLEN];

    szFile[0] = '\0';
    _fmemset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg;
    ofn.lpstrFilter = "Programs\0*.exe;*.pif;*.com;*.bat\0"
                      "All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrTitle = "Browse";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileName(&ofn)) {
        SetDlgItemText(hDlg, IDC_RUNCMD, szFile);
        SetFocus(GetDlgItem(hDlg, IDOK));
    }
}

#define RUN_HISTORY     12

/* The command goes to the top of the history ([Run] 1= ... 12=) */
static void AddRunHistory(LPCSTR lpCmd)
{
    char szKey[8], szOld[256], szCarry[256];
    int i;

    CfgSetStr("Run", "Last", lpCmd);
    lstrcpyn(szCarry, lpCmd, sizeof(szCarry));
    for (i = 1; i <= RUN_HISTORY; i++) {
        wsprintf(szKey, "%d", i);
        CfgGetStr("Run", szKey, "", szOld, sizeof(szOld));
        CfgSetStr("Run", szKey, szCarry);
        if (!szOld[0] || lstrcmpi(szOld, lpCmd) == 0)
            break;
        lstrcpy(szCarry, szOld);
    }
}

BOOL CALLBACK NSEXPORT RunDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                  LPARAM lParam)
{
    char szCmd[256], szDos[280], szKey[8];
    int i;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 1; i <= RUN_HISTORY; i++) {
            wsprintf(szKey, "%d", i);
            CfgGetStr("Run", szKey, "", szCmd, sizeof(szCmd));
            if (szCmd[0])
                SendDlgItemMessage(hDlg, IDC_RUNCMD, CB_ADDSTRING, 0, (LPARAM)(LPSTR)szCmd);
        }
        CfgGetStr("Run", "Last", "", szCmd, sizeof(szCmd));
        SetDlgItemText(hDlg, IDC_RUNCMD, szCmd);
        SendDlgItemMessage(hDlg, IDC_RUNCMD, CB_LIMITTEXT, sizeof(szCmd) - 1, 0L);
        SendDlgItemMessage(hDlg, IDC_RUNCMD, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        EnableWindow(GetDlgItem(hDlg, IDOK), szCmd[0] != '\0');
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDC_RUNCMD:
            if (HIWORD(lParam) == CBN_EDITCHANGE || HIWORD(lParam) == CBN_SELCHANGE)
                PostMessage(hDlg, WM_COMMAND, IDC_RUNCMD, MAKELPARAM(0, 0xFFFF));
            else if (HIWORD(lParam) == 0xFFFF)
                EnableWindow(GetDlgItem(hDlg, IDOK),
                             GetWindowTextLength(GetDlgItem(hDlg, IDC_RUNCMD)) > 0);
            return TRUE;
        case IDC_BROWSE:
            BrowseForProgram(hDlg);
            return TRUE;
        case IDOK:
            GetDlgItemText(hDlg, IDC_RUNCMD, szCmd, sizeof(szCmd));
            if (!szCmd[0])
                return TRUE;
            if (IsDlgButtonChecked(hDlg, IDC_RUNDOS))
                /* a DOS command or program, in a DOS window that stays */
                wsprintf(szDos, "COMMAND.COM /K %s", (LPSTR)szCmd);
            else
                lstrcpy(szDos, szCmd);
            if (LaunchCommand(hDlg, szDos, NULL,
                              IsDlgButtonChecked(hDlg, IDC_RUNMIN)
                                  ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL)) {
                AddRunHistory(szCmd);
                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void RunDialog(HWND hwndOwner)
{
    FARPROC lpfn = MakeProcInstance((FARPROC)RunDlgProc, g_hInst);

    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_RUN), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
}

/* -------------------------------------------------------------------- */
/* Shut Down                                                            */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT ShutdownDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    int id;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        if (g_bIsShell)
            EnableWindow(GetDlgItem(hDlg, IDC_CLOSESHELL), FALSE);
        if (UsersEnabled() && g_szUser[0]) {
            char szText[48];

            wsprintf(szText, "&Log off %s", (LPSTR)g_szUser);
            SetDlgItemText(hDlg, IDC_LOGOFF, szText);
        } else if (UsersEnabled())
            SetDlgItemText(hDlg, IDC_LOGOFF, "&Log on as another user");
        else
            EnableWindow(GetDlgItem(hDlg, IDC_LOGOFF), FALSE);
        CheckRadioButton(hDlg, IDC_EXITWIN, IDC_LOGOFF, IDC_EXITWIN);
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDOK) {
            for (id = IDC_EXITWIN; id <= IDC_LOGOFF; id++)
                if (IsDlgButtonChecked(hDlg, id))
                    break;
            EndDialog(hDlg, id <= IDC_LOGOFF ? id : 0);
            return TRUE;
        }
        if (wParam == IDCANCEL) {
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int ShutdownDialog(HWND hwndOwner)
{
    FARPROC lpfn = MakeProcInstance((FARPROC)ShutdownDlgProc, g_hInst);
    int nResult;

    nResult = DialogBox(g_hInst, MAKEINTRESOURCE(IDD_SHUTDOWN), hwndOwner,
                        (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
    return nResult;
}

/* -------------------------------------------------------------------- */
/* About                                                                */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam)
{
    char szText[160];
    DWORD dwFree;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        dwFree = GetFreeSpace(0);
        wsprintf(szText, "Version %s\n\n%lu KB free memory\n%u%% system resources free",
                 (LPSTR)NS_VERSION, dwFree / 1024L,
                 GetFreeSystemResources(GFSR_SYSTEMRESOURCES));
        SetDlgItemText(hDlg, IDC_VERSION, szText);
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDOK || wParam == IDCANCEL) {
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void AboutDialog(HWND hwndOwner)
{
    FARPROC lpfn = MakeProcInstance((FARPROC)AboutDlgProc, g_hInst);

    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUT), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
}
