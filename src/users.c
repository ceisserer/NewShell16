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
 * users.c - users: a logon dialog and settings for each user
 *
 * Like Calmira, NewShell16 can keep separate settings and Start menus for
 * several people sharing a computer. The list is in the [Users] section
 * of the shared NEWSHELL.INI:
 *
 *   [Users]
 *   Enabled=1
 *   1=Anna             ; the users; user n keeps NSUSERn.INI (settings) and
 *   Password1=4F2A0C11 ; NSMENUn.INI (Start menu) in the Windows directory
 *   Last=Anna
 *
 * A password keeps the next user from picking up someone's settings by
 * accident; it is a hash in a text file, not protection. With no user
 * chosen (Cancel) the shared files are used.
 */
#include <string.h>
#include "newshell.h"

#define MAX_USERS       16
#define USERS_INI       "NEWSHELL.INI"

char g_szIniFile[16] = "NEWSHELL.INI";
char g_szStartIni[16] = "NSSTART.INI";
char g_szUser[24];

static int s_nUser;     /* the user logging on (dialog) */

BOOL UsersEnabled(void)
{
    return GetPrivateProfileInt("Users", "Enabled", 0, USERS_INI) != 0;
}

/* A 32-bit hash of a password (FNV-1a), as 8 hex digits */
static void HashPassword(LPCSTR lp, LPSTR lpBuf)
{
    DWORD h = 2166136261UL;

    for (; *lp; lp++) {
        h ^= (BYTE)*lp;
        h *= 16777619UL;
    }
    wsprintf(lpBuf, "%08lX", h);
}

static int FindUser(LPCSTR lpName)
{
    char szKey[8], szName[24];
    int i;

    for (i = 1; i <= MAX_USERS; i++) {
        wsprintf(szKey, "%d", i);
        GetPrivateProfileString("Users", szKey, "", szName, sizeof(szName), USERS_INI);
        if (szName[0] && lstrcmpi(szName, lpName) == 0)
            return i;
    }
    return 0;
}

/* Switches NEWSHELL.INI and NSSTART.INI to those of user n (0 = shared) */
static void UseFilesOf(int n, LPCSTR lpName)
{
    if (n <= 0) {
        lstrcpy(g_szIniFile, "NEWSHELL.INI");
        lstrcpy(g_szStartIni, "NSSTART.INI");
        g_szUser[0] = '\0';
        return;
    }
    wsprintf(g_szIniFile, "NSUSER%d.INI", n);
    wsprintf(g_szStartIni, "NSMENU%d.INI", n);
    lstrcpyn(g_szUser, lpName, sizeof(g_szUser));
    WritePrivateProfileString("Users", "Last", lpName, USERS_INI);
}

static void FillUsers(HWND hDlg)
{
    char szKey[8], szName[24];
    int i;

    SendDlgItemMessage(hDlg, IDC_LOGUSER, CB_RESETCONTENT, 0, 0L);
    for (i = 1; i <= MAX_USERS; i++) {
        wsprintf(szKey, "%d", i);
        GetPrivateProfileString("Users", szKey, "", szName, sizeof(szName), USERS_INI);
        if (szName[0])
            SendDlgItemMessage(hDlg, IDC_LOGUSER, CB_ADDSTRING, 0, (LPARAM)(LPSTR)szName);
    }
}

/* New User: the name typed, with the password typed */
static BOOL NewUser(HWND hDlg, LPCSTR lpName, LPCSTR lpPassword)
{
    char szKey[16], szName[24], szHash[12];
    int i;

    if (!lpName[0] || FindUser(lpName)) {
        MessageBox(hDlg, lpName[0] ? "There already is a user with this name."
                                   : "Type the name of the new user first.",
                   "New User", MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    for (i = 1; i <= MAX_USERS; i++) {
        wsprintf(szKey, "%d", i);
        GetPrivateProfileString("Users", szKey, "", szName, sizeof(szName), USERS_INI);
        if (!szName[0])
            break;
    }
    if (i > MAX_USERS) {
        MessageBox(hDlg, "There can be no more users.", "New User", MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    WritePrivateProfileString("Users", szKey, lpName, USERS_INI);
    HashPassword(lpPassword, szHash);
    wsprintf(szKey, "Password%d", i);
    WritePrivateProfileString("Users", szKey, lpPassword[0] ? szHash : "", USERS_INI);
    return TRUE;
}

BOOL CALLBACK NSEXPORT LogonDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam)
{
    char szName[24], szPassword[32], szHash[12], szStored[12], szKey[16];
    int n;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        FillUsers(hDlg);
        GetPrivateProfileString("Users", "Last", "", szName, sizeof(szName), USERS_INI);
        SetDlgItemText(hDlg, IDC_LOGUSER, szName);
        SendDlgItemMessage(hDlg, IDC_LOGPASSWORD, EM_LIMITTEXT, sizeof(szPassword) - 1, 0L);
        SetFocus(GetDlgItem(hDlg, szName[0] ? IDC_LOGPASSWORD : IDC_LOGUSER));
        return FALSE;
    case WM_COMMAND:
        GetDlgItemText(hDlg, IDC_LOGUSER, szName, sizeof(szName));
        GetDlgItemText(hDlg, IDC_LOGPASSWORD, szPassword, sizeof(szPassword));
        switch (wParam) {
        case IDOK:
            n = FindUser(szName);
            if (!n) {
                MessageBox(hDlg, "There is no such user. Choose New User to add one.",
                           "Log On", MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            wsprintf(szKey, "Password%d", n);
            GetPrivateProfileString("Users", szKey, "", szStored, sizeof(szStored), USERS_INI);
            HashPassword(szPassword, szHash);
            if (szStored[0] && lstrcmp(szStored, szHash) != 0) {
                MessageBox(hDlg, "The password is not right.", "Log On",
                           MB_OK | MB_ICONEXCLAMATION);
                SetDlgItemText(hDlg, IDC_LOGPASSWORD, "");
                SetFocus(GetDlgItem(hDlg, IDC_LOGPASSWORD));
                return TRUE;
            }
            s_nUser = n;
            lstrcpyn(g_szUser, szName, sizeof(g_szUser));
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDC_LOGNEW:
            if (NewUser(hDlg, szName, szPassword)) {
                FillUsers(hDlg);
                SetDlgItemText(hDlg, IDC_LOGUSER, szName);
                MessageBox(hDlg, "The user was added. Choose OK to log on.", "New User",
                           MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        case IDCANCEL:
            s_nUser = 0;
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/*
 * Asks who is using the computer (if users are enabled) and switches to
 * their settings. Returns FALSE if no user was chosen.
 */
BOOL LogonDialog(void)
{
    FARPROC lpfn;

    UseFilesOf(0, "");
    if (!UsersEnabled())
        return FALSE;
    s_nUser = 0;
    lpfn = MakeProcInstance((FARPROC)LogonDlgProc, g_hInst);
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_LOGON), NULL, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
    if (s_nUser)
        UseFilesOf(s_nUser, g_szUser);
    else
        UseFilesOf(0, "");
    return s_nUser != 0;
}
