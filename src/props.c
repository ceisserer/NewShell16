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
 * props.c - Properties dialog for files, folders, drives and selections
 */
#include <string.h>
#include <dos.h>
#include "newshell.h"

#define ATTR_MASK   (_A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_ARCH)

typedef struct tagPROPDATA {
    HGLOBAL  hList;
    int      nCount;
    char     szPath[MAXPATHLEN];    /* the single item, if nCount == 1 */
    unsigned uAttr;
    BOOL     bAttrValid;
} PROPDATA;

static PROPDATA s_pd;

static void SetLine(HWND hDlg, int nLine, LPCSTR lpLabel, LPCSTR lpValue)
{
    SetDlgItemText(hDlg, IDC_PROPL1 + nLine, lpLabel);
    SetDlgItemText(hDlg, IDC_PROPV1 + nLine, lpValue);
}

static void FormatDate(unsigned uDate, unsigned uTime, LPSTR lpBuf)
{
    wsprintf(lpBuf, "%02u.%02u.%04u  %02u:%02u",
             uDate & 31, (uDate >> 5) & 15, (uDate >> 9) + 1980,
             uTime >> 11, (uTime >> 5) & 63);
}

static void FillDrive(HWND hDlg)
{
    static const char *s_szType[] = { "Drive", "Floppy disk", "Hard disk",
                                      "Network drive", "CD-ROM" };
    struct diskfree_t df;
    struct find_t ft;
    char szText[64], szSize[24], szPattern[8];
    DWORD dwCluster, dwTotal, dwFree;
    BYTE bType;
    int nDrive = (s_pd.szPath[0] & ~0x20) - 'A';
    UINT uOldMode;

    SendDlgItemMessage(hDlg, IDC_PROPICON, STM_SETICON,
                       (WPARAM)IconStock(DriveIcon(nDrive, &bType)), 0L);
    SetDlgItemText(hDlg, IDC_PROPNAME, s_pd.szPath);
    SetLine(hDlg, 0, "Type:", s_szType[bType < 5 ? bType : 0]);

    uOldMode = SetErrorMode(1);
    wsprintf(szPattern, "%c:\\*.*", 'A' + nDrive);
    if (_dos_findfirst(szPattern, _A_VOLID, &ft) == 0)
        SetLine(hDlg, 1, "Label:", ft.name);
    else
        SetLine(hDlg, 1, "Label:", "(none)");
    if (_dos_getdiskfree(nDrive + 1, &df) == 0) {
        dwCluster = (DWORD)df.sectors_per_cluster * df.bytes_per_sector;
        dwTotal = dwCluster * df.total_clusters;
        dwFree = dwCluster * df.avail_clusters;
        FormatBytes(dwTotal - dwFree, szSize);
        SetLine(hDlg, 2, "Used space:", szSize);
        FormatBytes(dwFree, szSize);
        SetLine(hDlg, 3, "Free space:", szSize);
        FormatBytes(dwTotal, szSize);
        wsprintf(szText, "%s", (LPSTR)szSize);
        SetLine(hDlg, 4, "Capacity:", szText);
    }
    SetErrorMode(uOldMode);
}

static void FillItem(HWND hDlg)
{
    struct find_t ft;
    char szText[MAXPATHLEN], szSize[24], szDir[MAXPATHLEN];
    DWORD dwSize = 0;
    int nFiles = 0, nDirs = 0;
    BOOL bDir;

    if (_dos_findfirst(s_pd.szPath, _A_NORMAL | _A_RDONLY | _A_HIDDEN
                       | _A_SYSTEM | _A_SUBDIR | _A_ARCH, &ft) != 0) {
        SetDlgItemText(hDlg, IDC_PROPNAME, s_pd.szPath);
        return;
    }
    bDir = (ft.attrib & _A_SUBDIR) != 0;
    s_pd.uAttr = ft.attrib;
    s_pd.bAttrValid = TRUE;

    SendDlgItemMessage(hDlg, IDC_PROPICON, STM_SETICON,
                       (WPARAM)IconForFile(s_pd.szPath, bDir), 0L);
    SetDlgItemText(hDlg, IDC_PROPNAME, ft.name);
    if (bDir)
        lstrcpy(szText, "Folder");
    else {
        lstrcpy(szDir, ft.name);
        wsprintf(szText, *PathExtension(szDir) ? "%s File" : "File",
                 PathExtension(szDir));
    }
    SetLine(hDlg, 0, "Type:", szText);
    lstrcpy(szDir, s_pd.szPath);
    PathParent(szDir);
    SetLine(hDlg, 1, "Location:", szDir);
    if (bDir) {
        FileOpMeasure(s_pd.szPath, &dwSize, &nFiles, &nDirs);
        FormatBytes(dwSize, szSize);
        SetLine(hDlg, 2, "Size:", szSize);
        wsprintf(szText, "%d file(s), %d folder(s)", nFiles, nDirs);
        SetLine(hDlg, 3, "Contains:", szText);
    } else {
        FormatBytes(ft.size, szSize);
        if (ft.size >= 1024L)
            wsprintf(szText, "%s (%lu bytes)", (LPSTR)szSize, ft.size);
        else
            lstrcpy(szText, szSize);
        SetLine(hDlg, 2, "Size:", szText);
        SetLine(hDlg, 3, "", "");
    }
    FormatDate(ft.wr_date, ft.wr_time, szText);
    SetLine(hDlg, 4, "Modified:", szText);

    CheckDlgButton(hDlg, IDC_ATTR_R, (ft.attrib & _A_RDONLY) != 0);
    CheckDlgButton(hDlg, IDC_ATTR_H, (ft.attrib & _A_HIDDEN) != 0);
    CheckDlgButton(hDlg, IDC_ATTR_S, (ft.attrib & _A_SYSTEM) != 0);
    CheckDlgButton(hDlg, IDC_ATTR_A, (ft.attrib & _A_ARCH) != 0);
}

static void FillMultiple(HWND hDlg)
{
    struct find_t ft;
    char szText[64], szSize[24], szPath[MAXPATHLEN];
    DWORD dwSize = 0;
    int nFiles = 0, nDirs = 0;
    LPCSTR lp;

    for (lp = PathListFirst(s_pd.hList); lp; lp = PathListNext(lp)) {
        lstrcpy(szPath, lp);
        if (_dos_findfirst(szPath, _A_NORMAL | _A_RDONLY | _A_HIDDEN
                           | _A_SYSTEM | _A_SUBDIR | _A_ARCH, &ft) != 0)
            continue;
        if (ft.attrib & _A_SUBDIR) {
            nDirs++;
            FileOpMeasure(szPath, &dwSize, &nFiles, &nDirs);
        } else {
            nFiles++;
            dwSize += ft.size;
        }
    }
    PathListUnlock(s_pd.hList);

    SendDlgItemMessage(hDlg, IDC_PROPICON, STM_SETICON,
                       (WPARAM)IconStock(IDI_DOCUMENT), 0L);
    wsprintf(szText, "%d items selected", s_pd.nCount);
    SetDlgItemText(hDlg, IDC_PROPNAME, szText);
    FormatBytes(dwSize, szSize);
    SetLine(hDlg, 0, "Size:", szSize);
    wsprintf(szText, "%d file(s), %d folder(s)", nFiles, nDirs);
    SetLine(hDlg, 1, "Contains:", szText);
}

BOOL CALLBACK NSEXPORT PropsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam)
{
    unsigned uNew;
    int id;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        for (id = 0; id < 5; id++)
            SetLine(hDlg, id, "", "");
        if (s_pd.nCount == 1 && lstrlen(s_pd.szPath) == 3) {
            FillDrive(hDlg);
            SetWindowText(hDlg, "Drive Properties");
        } else if (s_pd.nCount == 1)
            FillItem(hDlg);
        else
            FillMultiple(hDlg);
        if (!s_pd.bAttrValid)
            for (id = IDC_ATTR_R; id <= IDC_ATTR_A; id++)
                EnableWindow(GetDlgItem(hDlg, id), FALSE);
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDOK) {
            if (s_pd.bAttrValid) {
                uNew = s_pd.uAttr & ~ATTR_MASK;
                if (IsDlgButtonChecked(hDlg, IDC_ATTR_R)) uNew |= _A_RDONLY;
                if (IsDlgButtonChecked(hDlg, IDC_ATTR_H)) uNew |= _A_HIDDEN;
                if (IsDlgButtonChecked(hDlg, IDC_ATTR_S)) uNew |= _A_SYSTEM;
                if (IsDlgButtonChecked(hDlg, IDC_ATTR_A)) uNew |= _A_ARCH;
                if (uNew != s_pd.uAttr) {
                    char szDir[MAXPATHLEN];
                    if (_dos_setfileattr(s_pd.szPath, uNew & (ATTR_MASK | _A_SUBDIR)) != 0)
                        MessageBox(hDlg, "The attributes could not be changed.",
                                   NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
                    lstrcpy(szDir, s_pd.szPath);
                    if (PathParent(szDir))
                        FViewRefreshPath(szDir);
                }
            }
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

void PropertiesDialog(HWND hwndOwner, HGLOBAL hList)
{
    FARPROC lpfn;
    LPCSTR lp;

    _fmemset(&s_pd, 0, sizeof(s_pd));
    s_pd.hList = hList;
    s_pd.nCount = PathListCount(hList);
    if (s_pd.nCount == 0)
        return;
    lp = PathListFirst(hList);
    lstrcpyn(s_pd.szPath, lp, sizeof(s_pd.szPath));
    PathListUnlock(hList);

    lpfn = MakeProcInstance((FARPROC)PropsDlgProc, g_hInst);
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_PROPERTIES), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
}
