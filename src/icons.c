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
 * icons.c - icon lookup and caching for files and folders
 *
 * Icons extracted from executables are cached by executable path and shared
 * between all windows; document icons are cached by extension. USER heap
 * space is precious in Windows 3.1, so the caches are bounded.
 */
#include <string.h>
#include "newshell.h"

#define MAX_EXE_ICONS   96
#define MAX_EXT_ICONS   48

typedef struct {
    char  szPath[80];
    HICON hIcon;
} EXEICON;

typedef struct {
    char  szExt[4];
    HICON hIcon;
} EXTICON;

static EXEICON s_exe[MAX_EXE_ICONS];
static int     s_nExe;
static EXTICON s_ext[MAX_EXT_ICONS];
static int     s_nExt;

static HICON   s_hStock[IDI_BIN + 1];

void IconsInit(void)
{
    int i;

    for (i = IDI_NEWSHELL; i <= IDI_BIN; i++)
        s_hStock[i] = LoadIcon(g_hInst, MAKEINTRESOURCE(i));
}

void IconsFree(void)
{
    int i;

    for (i = 0; i < s_nExe; i++)
        if (s_exe[i].hIcon)
            DestroyIcon(s_exe[i].hIcon);
    s_nExe = 0;
    s_nExt = 0;
}

HICON IconStock(int idi)
{
    if (idi < IDI_NEWSHELL || idi > IDI_BIN)
        idi = IDI_DOCUMENT;
    return s_hStock[idi];
}

/* Returns the first icon of a Windows executable, or NULL if it has none */
static HICON ExeIcon(LPCSTR lpExe)
{
    HICON hIcon;
    int i;

    for (i = 0; i < s_nExe; i++)
        if (lstrcmpi(s_exe[i].szPath, lpExe) == 0)
            return s_exe[i].hIcon;
    if (s_nExe >= MAX_EXE_ICONS)
        return NULL;

    hIcon = ExtractIcon(g_hInst, lpExe, 0);
    if ((UINT)hIcon <= 1)       /* 1 = not a Windows executable */
        hIcon = NULL;
    lstrcpyn(s_exe[s_nExe].szPath, lpExe, sizeof(s_exe[0].szPath));
    s_exe[s_nExe].hIcon = hIcon;
    s_nExe++;
    return hIcon;
}

static BOOL IsProgramExt(LPCSTR lpExt)
{
    char szPrograms[64];
    char szExt[5];
    LPSTR p;
    int i;

    GetProfileString("windows", "Programs", "com exe bat pif",
                     szPrograms, sizeof(szPrograms));
    AnsiLower(szPrograms);
    lstrcpyn(szExt, lpExt, sizeof(szExt));
    AnsiLower(szExt);

    for (p = szPrograms; *p; ) {
        while (*p == ' ')
            p++;
        for (i = 0; p[i] && p[i] != ' '; i++)
            ;
        if (i > 0 && i == lstrlen(szExt) && _fstrncmp(p, szExt, i) == 0)
            return TRUE;
        p += i;
    }
    return FALSE;
}

HICON IconForFile(LPCSTR lpPath, BOOL bDir)
{
    char szPath[MAXPATHLEN];
    char szExe[MAXPATHLEN];
    LPSTR lpExt;
    HICON hIcon;
    int i;

    if (bDir)
        return s_hStock[IDI_FOLDER];

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    lpExt = PathExtension(szPath);

    if (IsProgramExt(lpExt)) {
        if (lstrcmpi(lpExt, "exe") == 0) {
            hIcon = ExeIcon(szPath);
            if (hIcon)
                return hIcon;
        }
        return s_hStock[IDI_APP];
    }

    for (i = 0; i < s_nExt; i++)
        if (lstrcmpi(s_ext[i].szExt, lpExt) == 0)
            return s_ext[i].hIcon;

    hIcon = NULL;
    if (*lpExt && (UINT)FindExecutable(szPath, NULL, szExe) > 32)
        hIcon = ExeIcon(szExe);
    if (!hIcon)
        hIcon = s_hStock[IDI_DOCUMENT];

    if (*lpExt && s_nExt < MAX_EXT_ICONS) {
        lstrcpyn(s_ext[s_nExt].szExt, lpExt, sizeof(s_ext[0].szExt));
        s_ext[s_nExt].hIcon = hIcon;
        s_nExt++;
    }
    return hIcon;
}
