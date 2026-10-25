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
 * progman.c - Program Manager compatibility
 *
 * Reads Program Manager group files (*.GRP, "PMCC" format) listed in
 * PROGMAN.INI so the Start menu can offer the user's existing groups, and
 * performs the start-up duties Program Manager normally has as the shell:
 * running the WIN.INI load= and run= lines and the StartUp group.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define MAX_GROUPS      40

/* Offsets in the group header */
#define GH_CBGROUP      6
#define GH_PNAME        22
#define GH_CITEMS       32
#define GH_RGIITEMS     34

/* Offsets in an item */
#define IT_PNAME        18
#define IT_PCOMMAND     20
#define IT_PICONPATH    22
#define IT_IICON        4
#define IT_SIZE         24

/* Tag ids in the extension area after the items */
#define TAG_DIR         0x8101
#define TAG_MINIMIZED   0x8103
#define TAG_END         0xFFFF

static int s_nGroups = -1;
static int s_nGroupNum[MAX_GROUPS];

static void ReadGroupOrder(void)
{
    char szOrder[128];
    char szKey[16], szFile[MAXPATHLEN];
    LPSTR p;
    int n;

    s_nGroups = 0;
    GetPrivateProfileString("Settings", "Order", "", szOrder, sizeof(szOrder),
                            "PROGMAN.INI");
    for (p = szOrder; *p && s_nGroups < MAX_GROUPS; ) {
        while (*p == ' ')
            p++;
        if (*p < '0' || *p > '9')
            break;
        n = 0;
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        s_nGroupNum[s_nGroups++] = n;
    }
    if (s_nGroups > 0)
        return;

    /* No Order= line: take Group1..GroupN as found */
    for (n = 1; n <= MAX_GROUPS; n++) {
        wsprintf(szKey, "Group%d", n);
        if (GetPrivateProfileString("Groups", szKey, "", szFile,
                                    sizeof(szFile), "PROGMAN.INI") > 0)
            s_nGroupNum[s_nGroups++] = n;
    }
}

int PmGroupCount(void)
{
    if (s_nGroups < 0)
        ReadGroupOrder();
    return s_nGroups;
}

BOOL PmGroupFile(int nIndex, LPSTR lpFile, int cbFile)
{
    char szKey[16];

    if (nIndex < 0 || nIndex >= PmGroupCount())
        return FALSE;
    wsprintf(szKey, "Group%d", s_nGroupNum[nIndex]);
    return GetPrivateProfileString("Groups", szKey, "", lpFile, cbFile,
                                   "PROGMAN.INI") > 0;
}

static WORD GetW(const BYTE FAR *lpData, UINT uOff)
{
    return (WORD)lpData[uOff] | ((WORD)lpData[uOff + 1] << 8);
}

/* Copies a zero-terminated string at uOff, bounds-checked against cbData */
static void GetS(const BYTE FAR *lpData, UINT cbData, UINT uOff,
                 LPSTR lpBuf, int cbBuf)
{
    int i = 0;

    if (uOff != 0)
        while (uOff < cbData && lpData[uOff] && i < cbBuf - 1)
            lpBuf[i++] = lpData[uOff++];
    lpBuf[i] = '\0';
}

/*
 * Loads up to nMax items of a group file. Returns the number of items or -1
 * if the file could not be read.
 */
int PmLoadGroup(LPCSTR lpFile, LPSTR lpGroupName, int cbGroupName,
                LPPMITEM lpItems, int nMax)
{
    HFILE hf;
    LONG lSize;
    HGLOBAL hMem;
    BYTE FAR *lpData;
    UINT cbData, cItems, uOff, uPos, i;
    int nItems = 0;
    int nSlotMap[128];

    lpGroupName[0] = '\0';
    hf = _lopen(lpFile, OF_READ);
    if (hf == HFILE_ERROR)
        return -1;
    lSize = _llseek(hf, 0L, 2);
    _llseek(hf, 0L, 0);
    if (lSize < GH_RGIITEMS || lSize > 65000L) {
        _lclose(hf);
        return -1;
    }
    cbData = (UINT)lSize;
    hMem = GlobalAlloc(GMEM_MOVEABLE, cbData);
    if (!hMem) {
        _lclose(hf);
        return -1;
    }
    lpData = (BYTE FAR *)GlobalLock(hMem);
    if (_lread(hf, lpData, cbData) != cbData
        || _fmemcmp(lpData, "PMCC", 4) != 0) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
        _lclose(hf);
        return -1;
    }
    _lclose(hf);

    GetS(lpData, cbData, GetW(lpData, GH_PNAME), lpGroupName, cbGroupName);
    cItems = GetW(lpData, GH_CITEMS);

    for (i = 0; i < cItems && i < 128; i++) {
        nSlotMap[i] = -1;
        if (GH_RGIITEMS + 2 * i + 2 > cbData)
            break;
        uOff = GetW(lpData, GH_RGIITEMS + 2 * i);
        if (uOff == 0 || uOff + IT_SIZE > cbData || nItems >= nMax)
            continue;
        _fmemset(&lpItems[nItems], 0, sizeof(PMITEM));
        GetS(lpData, cbData, GetW(lpData, uOff + IT_PNAME),
             lpItems[nItems].szName, sizeof(lpItems[0].szName));
        GetS(lpData, cbData, GetW(lpData, uOff + IT_PCOMMAND),
             lpItems[nItems].szCmd, sizeof(lpItems[0].szCmd));
        GetS(lpData, cbData, GetW(lpData, uOff + IT_PICONPATH),
             lpItems[nItems].szIconPath, sizeof(lpItems[0].szIconPath));
        lpItems[nItems].nIconIndex = GetW(lpData, uOff + IT_IICON);
        nSlotMap[i] = nItems++;
    }

    /* Extension tags: working directory, run minimised, ... */
    uPos = GetW(lpData, GH_CBGROUP);
    while (uPos + 6 <= cbData) {
        WORD wID = GetW(lpData, uPos);
        WORD wItem = GetW(lpData, uPos + 2);
        WORD cb = GetW(lpData, uPos + 4);
        int nIdx;

        if (wID == TAG_END || cb < 6)
            break;
        nIdx = (wItem < cItems && wItem < 128) ? nSlotMap[wItem] : -1;
        if (nIdx >= 0) {
            if (wID == TAG_DIR)
                GetS(lpData, cbData, uPos + 6, lpItems[nIdx].szDir,
                     sizeof(lpItems[0].szDir));
            else if (wID == TAG_MINIMIZED)
                lpItems[nIdx].bMinimized = TRUE;
        }
        uPos += cb;
    }

    GlobalUnlock(hMem);
    GlobalFree(hMem);
    return nItems;
}

#define MAX_STARTUP_ITEMS   64

void PmRunStartupGroup(void)
{
    char szStartup[40], szName[40], szFile[MAXPATHLEN];
    LPPMITEM lpItems;
    int g, i, n;

    GetPrivateProfileString("Settings", "Startup", "StartUp", szStartup,
                            sizeof(szStartup), "PROGMAN.INI");
    lpItems = (LPPMITEM)malloc(MAX_STARTUP_ITEMS * sizeof(PMITEM));
    if (!lpItems)
        return;

    for (g = 0; g < PmGroupCount(); g++) {
        if (!PmGroupFile(g, szFile, sizeof(szFile)))
            continue;
        n = PmLoadGroup(szFile, szName, sizeof(szName), lpItems,
                        MAX_STARTUP_ITEMS);
        if (n <= 0 || lstrcmpi(szName, szStartup) != 0)
            continue;
        for (i = 0; i < n; i++)
            LaunchCommand(NULL, lpItems[i].szCmd, lpItems[i].szDir,
                          lpItems[i].bMinimized ? SW_SHOWMINNOACTIVE
                                                : SW_SHOWNORMAL);
        break;
    }
    free(lpItems);
}

static void RunList(LPCSTR lpKey, int nShow)
{
    char szList[256], szItem[MAXPATHLEN];
    LPSTR p;
    int i;

    GetProfileString("windows", lpKey, "", szList, sizeof(szList));
    for (p = szList; *p; ) {
        while (*p == ' ' || *p == ',')
            p++;
        for (i = 0; *p && *p != ' ' && *p != ',' && i < sizeof(szItem) - 1; )
            szItem[i++] = *p++;
        szItem[i] = '\0';
        if (i > 0)
            LaunchCommand(NULL, szItem, NULL, nShow);
    }
}

void RunWinIniLoadRun(void)
{
    RunList("load", SW_SHOWMINNOACTIVE);
    RunList("run", SW_SHOWNORMAL);
}
