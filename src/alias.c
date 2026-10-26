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
 * alias.c - aliases: .ALS files that point at another file or folder
 *
 * The format is Calmira's, so aliases can be shared with it. All numbers
 * are 16-bit little-endian, strings are Pascal strings (a length byte and
 * the characters):
 *
 *   "CMSR"                     signature (a string)
 *   left, top, kind            kind: 0 drive, 1 folder, 2 file, 3 URL
 *   target, caption, iconfile
 *   iconindex
 *   params, workingfolder
 *   showmode                   0 normal, 1 minimised, 2 maximised
 *   usedocfolder               one byte
 *   an icon file (.ICO data)
 */
#include <string.h>
#include "newshell.h"

#define AK_DRIVE        0
#define AK_FOLDER       1
#define AK_FILE         2
#define AK_INTERNET     3

static BOOL ReadWord(HFILE hf, int FAR *pn)
{
    BYTE b[2];

    if (_lread(hf, b, 2) != 2)
        return FALSE;
    *pn = (int)(b[0] | (b[1] << 8));
    return TRUE;
}

static BOOL ReadPStr(HFILE hf, LPSTR lpBuf, int cbBuf)
{
    char szTmp[256];
    BYTE n;

    if (_lread(hf, &n, 1) != 1)
        return FALSE;
    if (n && _lread(hf, szTmp, n) != n)
        return FALSE;
    szTmp[n] = '\0';
    lstrcpyn(lpBuf, szTmp, cbBuf);
    return TRUE;
}

static void WriteWord(HFILE hf, int n)
{
    BYTE b[2];

    b[0] = (BYTE)(n & 0xFF);
    b[1] = (BYTE)((n >> 8) & 0xFF);
    _lwrite(hf, (LPCSTR)b, 2);
}

static void WritePStr(HFILE hf, LPCSTR lp)
{
    BYTE n = (BYTE)lstrlen(lp);

    _lwrite(hf, (LPCSTR)&n, 1);
    if (n)
        _lwrite(hf, lp, n);
}

/*
 * Reads an alias into a shortcut. Returns FALSE if the file is not an
 * alias.
 */
BOOL AliasRead(LPCSTR lpPath, LPSHORTCUT lpsc)
{
    char szSig[8], szTarget[MAXPATHLEN], szParams[MAXPATHLEN];
    int nLeft, nTop, nKind, nIndex, nShow;
    HFILE hf;
    BOOL bOK;

    _fmemset(lpsc, 0, sizeof(SHORTCUT));
    hf = _lopen(lpPath, OF_READ | OF_SHARE_DENY_NONE);
    if (hf == HFILE_ERROR)
        return FALSE;
    bOK = ReadPStr(hf, szSig, sizeof(szSig)) && lstrcmp(szSig, "CMSR") == 0
        && ReadWord(hf, &nLeft) && ReadWord(hf, &nTop) && ReadWord(hf, &nKind)
        && ReadPStr(hf, szTarget, sizeof(szTarget))
        && ReadPStr(hf, lpsc->szCaption, sizeof(lpsc->szCaption))
        && ReadPStr(hf, lpsc->szIconFile, sizeof(lpsc->szIconFile))
        && ReadWord(hf, &nIndex)
        && ReadPStr(hf, szParams, sizeof(szParams))
        && ReadPStr(hf, lpsc->szDir, sizeof(lpsc->szDir))
        && ReadWord(hf, &nShow);
    _lclose(hf);
    if (!bOK)
        return FALSE;
    lpsc->nIconIndex = nIndex;
    lpsc->nShow = nShow >= 0 && nShow <= 2 ? nShow : 0;
    if (nKind == AK_DRIVE || nKind == AK_FOLDER)
        wsprintf(lpsc->szCmd, "$Folder %s", (LPSTR)szTarget);
    else if (szParams[0] && lstrlen(szTarget) + lstrlen(szParams) + 2 < sizeof(lpsc->szCmd))
        wsprintf(lpsc->szCmd, "%s %s", (LPSTR)szTarget, (LPSTR)szParams);
    else
        lstrcpyn(lpsc->szCmd, szTarget, sizeof(lpsc->szCmd));
    return TRUE;
}

/*
 * Writes the .ICO data of one of our own icons (the first image of the
 * group), which Calmira reads back as the alias' picture.
 */
static void WriteStockIcon(HFILE hf, int idi)
{
    HRSRC hrs;
    HGLOBAL hg;
    BYTE FAR *lpDir;
    BYTE FAR *lpImage;
    BYTE hdr[22];
    DWORD cb;
    int nID;

    hrs = FindResource(g_hInst, MAKEINTRESOURCE(idi), RT_GROUP_ICON);
    if (!hrs || !(hg = LoadResource(g_hInst, hrs)))
        return;
    lpDir = (BYTE FAR *)LockResource(hg);
    /* NEWHEADER (6 bytes), then 14-byte entries ending in the image id */
    nID = lpDir[6 + 12] | (lpDir[6 + 13] << 8);
    _fmemset(hdr, 0, sizeof(hdr));
    hdr[2] = 1;                         /* type: icon */
    hdr[4] = 1;                         /* one image */
    _fmemcpy(hdr + 6, lpDir + 6, 12);   /* size, colours, planes, bits, bytes */
    hdr[18] = 22;                       /* offset of the image */
    cb = *(DWORD FAR *)(lpDir + 6 + 8);
    UnlockResource(hg);
    FreeResource(hg);

    hrs = FindResource(g_hInst, MAKEINTRESOURCE(nID), RT_ICON);
    if (!hrs || !(hg = LoadResource(g_hInst, hrs)))
        return;
    lpImage = (BYTE FAR *)LockResource(hg);
    if (cb > SizeofResource(g_hInst, hrs))
        cb = SizeofResource(g_hInst, hrs);
    _lwrite(hf, (LPCSTR)hdr, sizeof(hdr));
    _lwrite(hf, (LPCSTR)lpImage, (UINT)cb);
    UnlockResource(hg);
    FreeResource(hg);
}

/* Creates an alias file for lpTarget (a file, folder or drive) */
BOOL AliasCreate(LPCSTR lpTarget, LPCSTR lpAlias)
{
    char szCaption[40], szTarget[MAXPATHLEN];
    HFILE hf;
    int nKind, idi;

    lstrcpyn(szTarget, lpTarget, sizeof(szTarget));
    if (lstrlen(szTarget) <= 3) {
        nKind = AK_DRIVE;
        idi = IDI_HDD;
        wsprintf(szCaption, "Drive %c", szTarget[0] & ~0x20);
    } else {
        nKind = PathIsDirectory(szTarget) ? AK_FOLDER : AK_FILE;
        idi = nKind == AK_FOLDER ? IDI_FOLDER : IsProgramFile(szTarget) ? IDI_APP : IDI_DOCUMENT;
        lstrcpyn(szCaption, PathFileName(szTarget), sizeof(szCaption));
        AnsiLower(szCaption + 1);
    }
    hf = _lcreat(lpAlias, 0);
    if (hf == HFILE_ERROR)
        return FALSE;
    WritePStr(hf, "CMSR");
    WriteWord(hf, 0);
    WriteWord(hf, 0);
    WriteWord(hf, nKind);
    WritePStr(hf, szTarget);
    WritePStr(hf, szCaption);
    WritePStr(hf, "");
    WriteWord(hf, 0);
    WritePStr(hf, "");
    if (nKind == AK_FILE) {
        char szDir[MAXPATHLEN];

        lstrcpy(szDir, szTarget);
        PathParent(szDir);
        WritePStr(hf, szDir);
    } else
        WritePStr(hf, "");
    WriteWord(hf, 0);
    _lwrite(hf, "\0", 1);               /* usedocfolder */
    WriteStockIcon(hf, idi);
    _lclose(hf);
    return TRUE;
}

BOOL IsAliasFile(LPCSTR lpName)
{
    char szName[MAXPATHLEN];

    lstrcpyn(szName, lpName, sizeof(szName));
    return lstrcmpi(PathExtension(szName), "ALS") == 0;
}

HICON AliasIcon(LPCSTR lpPath)
{
    SHORTCUT sc;

    if (!AliasRead(lpPath, &sc))
        return IconStock(IDI_DOCUMENT);
    return ShortcutIconOf(&sc);
}

BOOL AliasOpen(HWND hwnd, LPCSTR lpPath)
{
    SHORTCUT sc;
    char szMsg[MAXPATHLEN + 40];

    if (!AliasRead(lpPath, &sc)) {
        wsprintf(szMsg, "%s is not a valid alias.", lpPath);
        MessageBox(hwnd, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    return ShortcutRun(hwnd, &sc);
}

/*
 * Makes an alias for each path of the list in lpDir: NAME.ALS, or
 * NAME1.ALS... if taken. Returns the number made.
 */
int AliasCreateList(HWND hwnd, HGLOBAL hList, LPCSTR lpDir)
{
    char szTarget[MAXPATHLEN], szAlias[MAXPATHLEN], szBase[16];
    LPCSTR lp;
    LPSTR lpDot;
    OFSTRUCT of;
    int nMade = 0, i;

    for (lp = PathListFirst(hList); lp; lp = PathListNext(lp)) {
        lstrcpyn(szTarget, lp, sizeof(szTarget));
        if (lstrlen(szTarget) <= 3)
            wsprintf(szBase, "DRIVE%c", szTarget[0] & ~0x20);
        else {
            lstrcpyn(szBase, PathFileName(szTarget), 9);
            lpDot = _fstrchr(szBase, '.');
            if (lpDot)
                *lpDot = '\0';
        }
        for (i = 0; i < 100; i++) {
            char szName[16];

            if (i == 0)
                wsprintf(szName, "%s.ALS", (LPSTR)szBase);
            else {
                szBase[i < 10 ? 7 : 6] = '\0';
                wsprintf(szName, "%s%d.ALS", (LPSTR)szBase, i);
            }
            lstrcpy(szAlias, lpDir);
            PathAppend(szAlias, szName);
            if (OpenFile(szAlias, &of, OF_EXIST) == HFILE_ERROR)
                break;
        }
        if (AliasCreate(szTarget, szAlias))
            nMade++;
        else {
            wsprintf(szTarget, "Cannot create the alias %s.", (LPSTR)szAlias);
            MessageBox(hwnd, szTarget, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
            break;
        }
    }
    PathListUnlock(hList);
    FViewRefreshPath(lpDir);
    return nMade;
}
