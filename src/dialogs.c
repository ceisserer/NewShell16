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

BOOL CALLBACK NSEXPORT RunDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                  LPARAM lParam)
{
    char szCmd[256];

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        CfgGetStr("Run", "Last", "", szCmd, sizeof(szCmd));
        SetDlgItemText(hDlg, IDC_RUNCMD, szCmd);
        SendDlgItemMessage(hDlg, IDC_RUNCMD, EM_LIMITTEXT, sizeof(szCmd) - 1, 0L);
        SendDlgItemMessage(hDlg, IDC_RUNCMD, EM_SETSEL, 0, MAKELONG(0, -1));
        EnableWindow(GetDlgItem(hDlg, IDOK), szCmd[0] != '\0');
        return TRUE;

    case WM_COMMAND:
        switch (wParam) {
        case IDC_RUNCMD:
            if (HIWORD(lParam) == EN_CHANGE)
                EnableWindow(GetDlgItem(hDlg, IDOK),
                             GetWindowTextLength(GetDlgItem(hDlg, IDC_RUNCMD)) > 0);
            return TRUE;
        case IDC_BROWSE:
            BrowseForProgram(hDlg);
            return TRUE;
        case IDOK:
            GetDlgItemText(hDlg, IDC_RUNCMD, szCmd, sizeof(szCmd));
            if (LaunchCommand(hDlg, szCmd, NULL,
                              IsDlgButtonChecked(hDlg, IDC_RUNMIN)
                                  ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL)) {
                CfgSetStr("Run", "Last", szCmd);
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
        CheckRadioButton(hDlg, IDC_EXITWIN, IDC_CLOSESHELL, IDC_EXITWIN);
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDOK) {
            for (id = IDC_EXITWIN; id <= IDC_CLOSESHELL; id++)
                if (IsDlgButtonChecked(hDlg, id))
                    break;
            EndDialog(hDlg, id <= IDC_CLOSESHELL ? id : 0);
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
