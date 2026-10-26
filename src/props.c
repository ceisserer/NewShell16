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
 *
 * Files show the program that opens them (which can be changed) and, for
 * programs and libraries, their version information; drives show their
 * usage as a pie.
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
    BOOL     bDrive;
    DWORD    dwUsed, dwFree;
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
        s_pd.dwUsed = dwTotal - dwFree;
        s_pd.dwFree = dwFree;
        s_pd.bDrive = TRUE;
    }
    SetErrorMode(uOldMode);
}

/* The drive usage: a pie in the highlight colour (used) and white (free) */
static void PaintPie(HWND hDlg)
{
    HWND hwndPie = GetDlgItem(hDlg, IDC_PROPPIE);
    HDC hdc;
    RECT rc;
    HBRUSH hbrUsed, hbrFree, hbrOld;
    int cx, cy, r, x, y;
    long lAngleNum, lAngleDen;

    if (!s_pd.bDrive || !hwndPie || s_pd.dwUsed + s_pd.dwFree == 0)
        return;
    GetWindowRect(hwndPie, &rc);
    ScreenToClient(hDlg, (POINT FAR *)&rc.left);
    ScreenToClient(hDlg, (POINT FAR *)&rc.right);
    hdc = GetDC(hDlg);
    cx = (rc.left + rc.right) / 2;
    cy = (rc.top + rc.bottom) / 2;
    r = (rc.right - rc.left) / 2 - 2;
    /* the end of the used part, counter-clockwise from three o'clock */
    lAngleNum = (long)(s_pd.dwUsed / 1024L);
    lAngleDen = (long)((s_pd.dwUsed + s_pd.dwFree) / 1024L);
    {
        /* sin(k * 90 / 16 degrees) * 1000, k = 0..16 */
        static const int s_nSin[17] = { 0, 98, 195, 290, 383, 471, 556, 634, 707,
                                        773, 831, 882, 924, 957, 981, 995, 1000 };
        int k = (int)(lAngleNum * 64L / (lAngleDen ? lAngleDen : 1)), q = k / 16, m = k % 16;
        int nCos, nSin;

        switch (q & 3) {
        case 0:  nCos = s_nSin[16 - m]; nSin = s_nSin[m]; break;
        case 1:  nCos = -s_nSin[m]; nSin = s_nSin[16 - m]; break;
        case 2:  nCos = -s_nSin[16 - m]; nSin = -s_nSin[m]; break;
        default: nCos = s_nSin[m]; nSin = -s_nSin[16 - m]; break;
        }
        x = cx + (int)((long)nCos * r * 2 / 1000);
        y = cy - (int)((long)nSin * r * 2 / 1000);
    }
    hbrUsed = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    hbrFree = CreateSolidBrush(RGB(255, 255, 255));
    hbrOld = SelectObject(hdc, hbrFree);
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    if (s_pd.dwUsed > 0) {
        SelectObject(hdc, hbrUsed);
        if (s_pd.dwFree == 0)
            Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
        else
            Pie(hdc, cx - r, cy - r, cx + r, cy + r, cx + r, cy, x, y);
    }
    SelectObject(hdc, hbrOld);
    DeleteObject(hbrUsed);
    DeleteObject(hbrFree);
    ReleaseDC(hDlg, hdc);
}

/* -------------------------------------------------------------------- */
/* Version information (VER.DLL, loaded when needed)                    */
/* -------------------------------------------------------------------- */

typedef DWORD (FAR PASCAL *VERSIZEPROC)(LPCSTR, DWORD FAR *);
typedef BOOL  (FAR PASCAL *VERINFOPROC)(LPCSTR, DWORD, DWORD, void FAR *);
typedef BOOL  (FAR PASCAL *VERQUERYPROC)(const void FAR *, LPCSTR, void FAR * FAR *,
                                         UINT FAR *);

static HINSTANCE    s_hVer;
static VERSIZEPROC  s_lpfnVerSize;
static VERINFOPROC  s_lpfnVerInfo;
static VERQUERYPROC s_lpfnVerQuery;

static BOOL LoadVer(void)
{
    UINT uOldMode;

    if (s_lpfnVerQuery)
        return TRUE;
    uOldMode = SetErrorMode(0x8000);
    s_hVer = LoadLibrary("VER.DLL");
    SetErrorMode(uOldMode);
    if ((UINT)s_hVer < 32)
        return FALSE;
    s_lpfnVerSize = (VERSIZEPROC)GetProcAddress(s_hVer, "GetFileVersionInfoSize");
    s_lpfnVerInfo = (VERINFOPROC)GetProcAddress(s_hVer, "GetFileVersionInfo");
    s_lpfnVerQuery = (VERQUERYPROC)GetProcAddress(s_hVer, "VerQueryValue");
    return s_lpfnVerSize && s_lpfnVerInfo && s_lpfnVerQuery;
}

/* The version resource of lpPath (free it with GlobalFree), or NULL */
static HGLOBAL ReadVersion(LPCSTR lpPath)
{
    DWORD dwHandle, cb;
    HGLOBAL h;

    if (!LoadVer())
        return NULL;
    cb = s_lpfnVerSize(lpPath, &dwHandle);
    if (cb == 0 || cb > 32000L)
        return NULL;
    h = GlobalAlloc(GMEM_MOVEABLE, cb);
    if (!h)
        return NULL;
    if (!s_lpfnVerInfo(lpPath, dwHandle, cb, GlobalLock(h))) {
        GlobalUnlock(h);
        GlobalFree(h);
        return NULL;
    }
    GlobalUnlock(h);
    return h;
}

BOOL CALLBACK NSEXPORT VersionDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    static const char *s_szKeys[] = {
        "FileDescription", "FileVersion", "CompanyName", "LegalCopyright",
        "ProductName", "ProductVersion", "InternalName", "OriginalFilename",
        "LegalTrademarks", "Comments"
    };
    static const char *s_szNames[] = {
        "Description", "File version", "Company", "Copyright", "Product",
        "Product version", "Internal name", "Original name", "Trademarks", "Comments"
    };
    char szQuery[64], szText[1024];
    void FAR *lpData, FAR *lpValue;
    UINT cb;
    WORD wLang = 0x0409, wCode = 0x04E4;
    int i, n = 0;

    switch (msg) {
    case WM_INITDIALOG:
        lpData = GlobalLock((HGLOBAL)LOWORD(lParam));
        if (s_lpfnVerQuery(lpData, "\\VarFileInfo\\Translation", &lpValue, &cb) && cb >= 4) {
            wLang = ((WORD FAR *)lpValue)[0];
            wCode = ((WORD FAR *)lpValue)[1];
        }
        szText[0] = '\0';
        for (i = 0; i < sizeof(s_szKeys) / sizeof(s_szKeys[0]); i++) {
            wsprintf(szQuery, "\\StringFileInfo\\%04X%04X\\%s", wLang, wCode,
                     (LPSTR)s_szKeys[i]);
            if (s_lpfnVerQuery(lpData, szQuery, &lpValue, &cb) && cb > 1
                && n + lstrlen((LPSTR)lpValue) + 24 < sizeof(szText))
                n += wsprintf(szText + n, "%s:\t%s\r\n", (LPSTR)s_szNames[i], (LPSTR)lpValue);
        }
        if (s_lpfnVerQuery(lpData, "\\", &lpValue, &cb) && cb >= 16) {
            DWORD FAR *lpdw = (DWORD FAR *)lpValue;

            /* VS_FIXEDFILEINFO: signature, struct version, file version ms, ls */
            wsprintf(szText + n, "Version number:\t%u.%u.%u.%u", HIWORD(lpdw[2]),
                     LOWORD(lpdw[2]), HIWORD(lpdw[3]), LOWORD(lpdw[3]));
        }
        GlobalUnlock((HGLOBAL)LOWORD(lParam));
        SetDlgItemText(hDlg, IDC_VERTEXT, szText);
        {
            int nTabs = 64;
            SendDlgItemMessage(hDlg, IDC_VERTEXT, EM_SETTABSTOPS, 1, (LPARAM)(int FAR *)&nTabs);
        }
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDOK || wParam == IDCANCEL) {
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void ShowVersion(HWND hDlg)
{
    HGLOBAL h = ReadVersion(s_pd.szPath);
    FARPROC lpfn;

    if (!h) {
        MessageBox(hDlg, "This file has no version information.", "Version",
                   MB_OK | MB_ICONINFORMATION);
        return;
    }
    lpfn = MakeProcInstance((FARPROC)VersionDlgProc, g_hInst);
    DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_VERSION), hDlg, (DLGPROC)lpfn,
                   (LPARAM)(UINT)h);
    FreeProcInstance(lpfn);
    GlobalFree(h);
}

/* The program that opens a document, for the "Opens with" line */
static void ShowAssociation(HWND hDlg)
{
    char szExe[MAXPATHLEN], szDir[MAXPATHLEN];

    lstrcpy(szDir, s_pd.szPath);
    PathParent(szDir);
    if (IsProgramFile(s_pd.szPath))
        lstrcpy(szExe, "(a program)");
    else if ((UINT)FindExecutable(s_pd.szPath, szDir, szExe) <= 32)
        lstrcpy(szExe, "(nothing)");
    else
        AnsiLower(szExe);
    SetLine(hDlg, 5, "Opens with:", PathFileName(szExe));
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

    if (!bDir) {
        HGLOBAL hVer = ReadVersion(s_pd.szPath);

        ShowAssociation(hDlg);
        EnableWindow(GetDlgItem(hDlg, IDC_PROPASSOC), !IsProgramFile(s_pd.szPath)
                     && *PathExtension(szDir));
        EnableWindow(GetDlgItem(hDlg, IDC_PROPVERSION), hVer != NULL);
        if (hVer)
            GlobalFree(hVer);
    }
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
        for (id = 0; id < 6; id++)
            SetLine(hDlg, id, "", "");
        EnableWindow(GetDlgItem(hDlg, IDC_PROPASSOC), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_PROPVERSION), FALSE);
        ShowWindow(GetDlgItem(hDlg, IDC_PROPPIE), SW_HIDE);
        if (s_pd.nCount == 1 && lstrlen(s_pd.szPath) == 3) {
            FillDrive(hDlg);
            SetWindowText(hDlg, "Drive Properties");
            /* the value column makes room for the pie */
            for (id = 0; id < 5; id++) {
                RECT rc;
                HWND hwndV = GetDlgItem(hDlg, IDC_PROPV1 + id);

                GetWindowRect(hwndV, &rc);
                ScreenToClient(hDlg, (POINT FAR *)&rc.left);
                ScreenToClient(hDlg, (POINT FAR *)&rc.right);
                MoveWindow(hwndV, rc.left, rc.top, (rc.right - rc.left) * 2 / 3,
                           rc.bottom - rc.top, FALSE);
            }
        } else if (s_pd.nCount == 1)
            FillItem(hDlg);
        else
            FillMultiple(hDlg);
        if (!s_pd.bAttrValid)
            for (id = IDC_ATTR_R; id <= IDC_ATTR_A; id++)
                EnableWindow(GetDlgItem(hDlg, id), FALSE);
        return TRUE;

    case WM_PAINT:
        if (s_pd.bDrive) {
            /* the dialog paints first, then the pie on top */
            PAINTSTRUCT ps;

            BeginPaint(hDlg, &ps);
            EndPaint(hDlg, &ps);
            PaintPie(hDlg);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        if (wParam == IDC_PROPVERSION) {
            ShowVersion(hDlg);
            return TRUE;
        }
        if (wParam == IDC_PROPASSOC) {
            if (OpenWithDialogEx(hDlg, s_pd.szPath, TRUE))
                ShowAssociation(hDlg);
            return TRUE;
        }
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
