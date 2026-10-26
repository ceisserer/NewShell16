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
 * bin.c - the Bin (recycle bin)
 *
 * Deleted items are moved into a hidden NSBIN folder on the root of their
 * own drive (so sending to the Bin is a fast rename), under a generated
 * name. NSBIN\NSBIN.INI records the original location:
 *
 *   [Files]                 [Sizes]
 *   NS000001.TXT=C:\A.TXT   NS000001.TXT=1234
 *
 * Only fixed drives get a Bin; other items are deleted directly. The Bin
 * Properties set its caption, a capacity (the oldest items are deleted
 * when it is exceeded) and whether it is emptied when Windows exits:
 *
 *   [Bin]
 *   Capacity=0         ; MB, 0 = no limit
 *   EmptyOnExit=0
 */
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <dos.h>
#include "newshell.h"

#define BIN_DIR         "NSBIN"
#define BIN_INI         "NSBIN.INI"
#define MAX_BIN         256
#define ID_LIST         1

#define COL_NAME_CX     120
#define COL_FROM_CX     200

typedef struct tagBINITEM {
    char  szBinName[13];
    char  szOrig[MAXPATHLEN];
    DWORD dwSize;
    BOOL  bDir;
} BINITEM, FAR *LPBINITEM;

static HWND      s_hwndBin;
static HWND      s_hwndList;
static LPBINITEM s_lpItems;
static int       s_nItems;
static int       s_cyHeader, s_cyStatus;

static void BinDir(char cDrive, LPSTR lpBuf)
{
    wsprintf(lpBuf, "%c:\\%s", cDrive, (LPSTR)BIN_DIR);
}

static void BinIni(char cDrive, LPSTR lpBuf)
{
    wsprintf(lpBuf, "%c:\\%s\\%s", cDrive, (LPSTR)BIN_DIR, (LPSTR)BIN_INI);
}

BOOL BinCanTake(LPCSTR lpPath)
{
    char c = (char)(lpPath[0] & ~0x20);

    if (c < 'A' || c > 'Z' || lpPath[1] != ':')
        return FALSE;
    if (GetDriveType(c - 'A') != DRIVE_FIXED)
        return FALSE;
    /* items already in the Bin are deleted for real */
    return !(lstrlen(lpPath) > 9 && _fstrnicmp(lpPath + 3, BIN_DIR "\\", 6) == 0);
}

BOOL BinSend(LPCSTR lpPath)
{
    char szDir[MAXPATHLEN], szIni[MAXPATHLEN], szDest[MAXPATHLEN];
    char szName[16], szNum[16], szSize[16];
    char szPath[MAXPATHLEN];
    LPSTR lpExt;
    unsigned uAttr;
    DWORD dwSize = 0;
    int nFiles = 0, nDirs = 0;
    UINT uNext;
    BOOL bDir;

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    if (_dos_getfileattr(szPath, &uAttr) != 0)
        return TRUE;
    bDir = (uAttr & _A_SUBDIR) != 0;

    BinDir(szPath[0], szDir);
    if (_dos_getfileattr(szDir, &uAttr) != 0) {
        if (mkdir(szDir) != 0)
            return FALSE;
        _dos_setfileattr(szDir, _A_SUBDIR | _A_HIDDEN);
    }
    BinIni(szPath[0], szIni);
    uNext = GetPrivateProfileInt("Bin", "Next", 1, szIni);
    wsprintf(szNum, "%u", uNext + 1);
    WritePrivateProfileString("Bin", "Next", szNum, szIni);

    lpExt = PathExtension(szPath);
    wsprintf(szName, *lpExt ? "NS%06u.%s" : "NS%06u", uNext, lpExt);
    lstrcpy(szDest, szDir);
    PathAppend(szDest, szName);

    if (bDir)
        FileOpMeasure(szPath, &dwSize, &nFiles, &nDirs);
    else {
        struct find_t ft;
        if (_dos_findfirst(szPath, _A_NORMAL | _A_RDONLY | _A_HIDDEN
                           | _A_SYSTEM | _A_ARCH, &ft) == 0)
            dwSize = ft.size;
    }

    if (!FileOpMoveTo(NULL, szPath, szDest))
        return FALSE;
    WritePrivateProfileString("Files", szName, szPath, szIni);
    wsprintf(szSize, "%lu%s", dwSize, (LPSTR)(bDir ? ",D" : ""));
    WritePrivateProfileString("Sizes", szName, szSize, szIni);
    /* Undelete starts at the drive of the last item */
    wsprintf(szSize, "%c", szPath[0]);
    CfgSetStr("Bin", "LastDrive", szSize);
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* Reading the Bin                                                      */
/* -------------------------------------------------------------------- */

static void LoadDrive(char cDrive)
{
    char szIni[MAXPATHLEN], szSize[24];
    char *pKeys, *p;
    LPBINITEM b;
    LPSTR q;

    BinIni(cDrive, szIni);
    pKeys = (char *)malloc(4096);
    if (!pKeys)
        return;
    if (GetPrivateProfileString("Files", NULL, "", pKeys, 4096, szIni) > 0) {
        for (p = pKeys; *p && s_nItems < MAX_BIN; p += lstrlen(p) + 1) {
            b = &s_lpItems[s_nItems];
            lstrcpyn(b->szBinName, p, sizeof(b->szBinName));
            GetPrivateProfileString("Files", p, "", b->szOrig,
                                    sizeof(b->szOrig), szIni);
            GetPrivateProfileString("Sizes", p, "0", szSize, sizeof(szSize), szIni);
            b->dwSize = 0;
            for (q = szSize; *q >= '0' && *q <= '9'; q++)
                b->dwSize = b->dwSize * 10 + (*q - '0');
            b->bDir = (*q == ',');
            if (b->szOrig[0])
                s_nItems++;
        }
    }
    free(pKeys);
}

static void LoadBin(void)
{
    int d;

    if (!s_lpItems)
        s_lpItems = (LPBINITEM)malloc(MAX_BIN * sizeof(BINITEM));
    s_nItems = 0;
    if (!s_lpItems)
        return;
    for (d = 2; d < 26; d++)
        if (GetDriveType(d) == DRIVE_FIXED)
            LoadDrive((char)('A' + d));
}

BOOL BinIsEmpty(void)
{
    char szIni[MAXPATHLEN], szBuf[16];
    int d;

    for (d = 2; d < 26; d++) {
        if (GetDriveType(d) != DRIVE_FIXED)
            continue;
        BinIni((char)('A' + d), szIni);
        if (GetPrivateProfileString("Files", NULL, "", szBuf, sizeof(szBuf), szIni) > 0)
            return FALSE;
    }
    return TRUE;
}

static void BinItemPath(LPBINITEM b, LPSTR lpBuf)
{
    BinDir(b->szOrig[0], lpBuf);
    PathAppend(lpBuf, b->szBinName);
}

static void Forget(LPBINITEM b)
{
    char szIni[MAXPATHLEN];

    BinIni(b->szOrig[0], szIni);
    WritePrivateProfileString("Files", b->szBinName, NULL, szIni);
    WritePrivateProfileString("Sizes", b->szBinName, NULL, szIni);
}

/* Creates the missing folders of a path */
static void MakeParents(LPCSTR lpPath)
{
    char szDir[MAXPATHLEN];
    unsigned uAttr;

    lstrcpy(szDir, lpPath);
    if (!PathParent(szDir) || _dos_getfileattr(szDir, &uAttr) == 0)
        return;
    MakeParents(szDir);
    mkdir(szDir);
}

static void Restore(HWND hwnd, LPBINITEM b)
{
    char szFrom[MAXPATHLEN], szDir[MAXPATHLEN];

    BinItemPath(b, szFrom);
    MakeParents(b->szOrig);
    if (FileOpMoveTo(hwnd, szFrom, b->szOrig)) {
        Forget(b);
        lstrcpy(szDir, b->szOrig);
        if (PathParent(szDir)) {
            FViewRefreshPath(szDir);
            ExplorerRefreshPath(szDir);
        }
    }
}

static void Destroy(HWND hwnd, LPBINITEM b)
{
    char szPath[MAXPATHLEN];

    BinItemPath(b, szPath);
    if (FileOpRemove(hwnd, szPath))
        Forget(b);
}

/* -------------------------------------------------------------------- */
/* Bin window                                                           */
/* -------------------------------------------------------------------- */

static void FillList(void)
{
    int i;

    LoadBin();
    if (!s_hwndList)
        return;
    SendMessage(s_hwndList, WM_SETREDRAW, FALSE, 0L);
    SendMessage(s_hwndList, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < s_nItems; i++)
        SendMessage(s_hwndList, LB_ADDSTRING, 0, (LPARAM)i);
    SendMessage(s_hwndList, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(s_hwndList, NULL, TRUE);
    InvalidateRect(s_hwndBin, NULL, TRUE);
}

static void Destroy(HWND hwnd, LPBINITEM b);

static int CompareAge(const void *a, const void *b)
{
    /* the generated names count up: NS000001 is the oldest */
    return lstrcmpi(((LPBINITEM)a)->szBinName, ((LPBINITEM)b)->szBinName);
}

/* Deletes the oldest items while the Bin holds more than its capacity */
static void Trim(void)
{
    DWORD dwCap = (DWORD)CfgGetInt("Bin", "Capacity", 0) * 1048576L;
    DWORD dwTotal = 0;
    int i;

    if (dwCap == 0)
        return;
    LoadBin();
    for (i = 0; i < s_nItems; i++)
        dwTotal += s_lpItems[i].dwSize;
    if (dwTotal <= dwCap)
        return;
    if (s_nItems > 1)
        qsort(s_lpItems, s_nItems, sizeof(BINITEM), CompareAge);
    for (i = 0; i < s_nItems && dwTotal > dwCap; i++) {
        dwTotal -= s_lpItems[i].dwSize;
        Destroy(NULL, &s_lpItems[i]);
    }
}

void BinChanged(void)
{
    Trim();
    if (s_hwndBin)
        FillList();
    DesktopBinChanged();
}

typedef void (*BINACTION)(HWND, LPBINITEM);

static void ForSelected(HWND hwnd, BINACTION pfn)
{
    int aSel[MAX_BIN];
    int i, n;

    n = (int)SendMessage(s_hwndList, LB_GETSELITEMS, MAX_BIN, (LPARAM)(int FAR *)aSel);
    for (i = 0; i < n; i++)
        if (aSel[i] >= 0 && aSel[i] < s_nItems)
            pfn(hwnd, &s_lpItems[aSel[i]]);
    BinChanged();
}

static void EmptyBin(HWND hwnd)
{
    char szMsg[80];
    int i;

    LoadBin();
    if (s_nItems == 0)
        return;
    wsprintf(szMsg, "Are you sure you want to delete the %d item(s) in the Bin?",
             s_nItems);
    if (MessageBox(hwnd, szMsg, "Empty Bin", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    for (i = 0; i < s_nItems; i++)
        Destroy(hwnd, &s_lpItems[i]);
    PlayEventSound("EmptyBin");
    BinChanged();
}

static void DrawEntry(DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText;
    LPBINITEM b;
    char szName[MAXPATHLEN], szSize[24];
    BOOL bSel = (lpdis->itemState & ODS_SELECTED) != 0;
    HFONT hfOld;

    if ((int)lpdis->itemID < 0 || (int)lpdis->itemID >= s_nItems) {
        FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
        return;
    }
    b = &s_lpItems[lpdis->itemID];
    FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
    hfOld = SelectObject(hdc, g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);

    DrawIconSmall(hdc, rc.left + 2, rc.top + 1,
                  b->bDir ? IconStock(IDI_FOLDER) : IconForFile(b->szOrig, FALSE),
                  GetSysColor(COLOR_WINDOW));
    lstrcpy(szName, b->szOrig);
    SetRect(&rcText, rc.left + 20, rc.top, rc.left + COL_NAME_CX, rc.bottom);
    {
        RECT rcSel = rcText;
        rcSel.right = rcSel.left + TextWidth(hdc, PathFileName(szName)) + 4;
        if (bSel)
            FillSolid(hdc, &rcSel, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(hdc, GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        rcSel.left += 2;
        DrawText(hdc, PathFileName(szName), -1, &rcSel,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        rcSel.left -= 2;
        if (lpdis->itemState & ODS_FOCUS)
            DrawItemFocus(hdc, &rcSel, bSel);
    }
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    PathParent(szName);
    SetRect(&rcText, rc.left + COL_NAME_CX, rc.top,
            rc.left + COL_NAME_CX + COL_FROM_CX - 4, rc.bottom);
    DrawText(hdc, szName, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    FormatBytes(b->dwSize, szSize);
    SetRect(&rcText, rc.left + COL_NAME_CX + COL_FROM_CX, rc.top, rc.right - 4, rc.bottom);
    DrawText(hdc, szSize, -1, &rcText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hfOld);
}

static void PaintFrame(HWND hwnd, HDC hdc)
{
    static const char *s_szCols[] = { "Name", "Original location", "Size" };
    RECT rcClient, rc;
    char szText[64], szSize[24];
    DWORD dwTotal = 0;
    HFONT hfOld = SelectObject(hdc, g_hfontUI);
    int i, x = CLIENT_EDGE, cx;

    GetClientRect(hwnd, &rcClient);
    DrawPaneEdge(hdc, 0, 0, rcClient.right, rcClient.bottom - s_cyStatus);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    for (i = 0; i < 3; i++) {
        cx = i == 0 ? COL_NAME_CX : i == 1 ? COL_FROM_CX
                    : rcClient.right - CLIENT_EDGE - x;
        SetRect(&rc, x, CLIENT_EDGE, x + cx, CLIENT_EDGE + s_cyHeader);
        FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
        Draw3DEdge(hdc, &rc, FALSE, FALSE);
        InflateRect(&rc, -4, 0);
        DrawText(hdc, s_szCols[i], -1, &rc,
                 (i == 2 ? DT_RIGHT : DT_LEFT) | DT_SINGLELINE | DT_VCENTER);
        x += cx;
    }

    for (i = 0; i < s_nItems; i++)
        dwTotal += s_lpItems[i].dwSize;
    FormatBytes(dwTotal, szSize);
    wsprintf(szText, "%d object(s)  %s", s_nItems, (LPSTR)szSize);
    SetRect(&rc, 0, rcClient.bottom - s_cyStatus, rcClient.right, rcClient.bottom);
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    InflateRect(&rc, -2, -2);
    rc.bottom++;
    Draw3DEdge(hdc, &rc, TRUE, FALSE);
    InflateRect(&rc, -4, 0);
    DrawText(hdc, szText, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, hfOld);
}

LRESULT CALLBACK NSEXPORT BinWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam)
{
    PAINTSTRUCT ps;
    TEXTMETRIC tm;
    HDC hdc;
    HFONT hfOld;

    switch (msg) {
    case WM_CREATE:
        hdc = GetDC(hwnd);
        hfOld = SelectObject(hdc, g_hfontUI);
        GetTextMetrics(hdc, &tm);
        SelectObject(hdc, hfOld);
        ReleaseDC(hwnd, hdc);
        s_cyHeader = tm.tmHeight + 6;
        s_cyStatus = tm.tmHeight + 8;
        s_hwndList = CreateWindow("LISTBOX", "",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL
                                  | LBS_OWNERDRAWFIXED | LBS_EXTENDEDSEL
                                  | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                  0, 0, 0, 0, hwnd, (HMENU)ID_LIST, g_hInst, NULL);
        return 0;

    case WM_SIZE:
        /* the header and the list share one sunken pane */
        MoveWindow(s_hwndList, CLIENT_EDGE, CLIENT_EDGE + s_cyHeader,
                   LOWORD(lParam) - 2 * CLIENT_EDGE,
                   HIWORD(lParam) - s_cyStatus - s_cyHeader - 2 * CLIENT_EDGE, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        PaintFrame(hwnd, ps.hdc);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SETFOCUS:
        SetFocus(s_hwndList);
        return 0;

    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lParam)->itemHeight = 18;
        return TRUE;

    case WM_DRAWITEM:
        DrawEntry((DRAWITEMSTRUCT FAR *)lParam);
        return TRUE;

    case WM_INITMENUPOPUP:
        {
            int n = (int)SendMessage(s_hwndList, LB_GETSELCOUNT, 0, 0L);
            EnableMenuItem((HMENU)wParam, CMD_BIN_RESTORE, n > 0 ? MF_ENABLED : MF_GRAYED);
            EnableMenuItem((HMENU)wParam, CMD_BIN_DELETE, n > 0 ? MF_ENABLED : MF_GRAYED);
            EnableMenuItem((HMENU)wParam, CMD_BIN_EMPTY, s_nItems > 0 ? MF_ENABLED : MF_GRAYED);
        }
        return 0;

    case WM_COMMAND:
        switch (wParam) {
        case ID_LIST:
            if (HIWORD(lParam) == LBN_DBLCLK)
                ForSelected(hwnd, Restore);
            return 0;
        case CMD_BIN_RESTORE:   ForSelected(hwnd, Restore); return 0;
        case CMD_BIN_DELETE:
            if (MessageBox(hwnd, "Delete the selected items permanently?", "Bin",
                           MB_YESNO | MB_ICONQUESTION) == IDYES)
                ForSelected(hwnd, Destroy);
            return 0;
        case CMD_BIN_EMPTY:     EmptyBin(hwnd); return 0;
        case CMD_BIN_PROPS:     BinPropertiesDialog(hwnd); return 0;
        case CMD_F_REFRESH:     FillList(); return 0;
        case CMD_F_CLOSE:       DestroyWindow(hwnd); return 0;
        case CMD_ABOUT:         AboutDialog(hwnd); return 0;
        }
        break;

    case WM_DESTROY:
        s_hwndBin = NULL;
        s_hwndList = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL BinRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = BinWndProc;
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_BIN));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_BIN);
    wc.lpszClassName = CLS_BIN;
    return RegisterClass(&wc);
}

void BinOpen(void)
{
    if (s_hwndBin) {
        if (IsIconic(s_hwndBin))
            ShowWindow(s_hwndBin, SW_RESTORE);
        BringWindowToTop(s_hwndBin);
        SetActiveWindow(s_hwndBin);
        return;
    }
    {
        char szCaption[40];

        CfgGetStr("Desktop", "BinCaption", "Bin", szCaption, sizeof(szCaption));
        s_hwndBin = CreateWindow(CLS_BIN, szCaption, WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, 0, 460, 260, NULL, NULL, g_hInst, NULL);
    }
    if (!s_hwndBin)
        return;
    FillList();
    ShowWindow(s_hwndBin, SW_SHOWNORMAL);
    UpdateWindow(s_hwndBin);
    TaskbarRefresh();
}

/* Undelete (Ctrl+U): restores the newest item, from the drive deleted
   from last if it has any */
BOOL BinUndelete(HWND hwndOwner)
{
    char szDrive[4];
    LPBINITEM b = NULL;
    int i, nPass;

    LoadBin();
    CfgGetStr("Bin", "LastDrive", "", szDrive, sizeof(szDrive));
    for (nPass = 0; nPass < 2 && !b; nPass++)
        for (i = 0; i < s_nItems; i++) {
            if (nPass == 0 && (s_lpItems[i].szOrig[0] & ~0x20) != (szDrive[0] & ~0x20))
                continue;
            if (!b || lstrcmpi(s_lpItems[i].szBinName, b->szBinName) > 0)
                b = &s_lpItems[i];
        }
    if (!b) {
        MessageBox(hwndOwner, "The Bin is empty.", NS_APPNAME, MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }
    Restore(hwndOwner, b);
    BinChanged();
    return TRUE;
}

/* Windows is exiting: empty the Bin if the properties say so */
void BinExit(void)
{
    int i;

    if (!CfgGetInt("Bin", "EmptyOnExit", 0))
        return;
    LoadBin();
    for (i = 0; i < s_nItems; i++)
        Destroy(NULL, &s_lpItems[i]);
}

/* -------------------------------------------------------------------- */
/* Bin Properties                                                       */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT BinPropsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    char szText[96], szSize[24];
    DWORD dwTotal = 0;
    BOOL bOK;
    int i;

    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        CfgGetStr("Desktop", "BinCaption", "Bin", szText, 40);
        SetDlgItemText(hDlg, IDC_BINCAPTION, szText);
        SendDlgItemMessage(hDlg, IDC_BINCAPTION, EM_LIMITTEXT, 39, 0L);
        SetDlgItemInt(hDlg, IDC_BINCAPACITY, CfgGetInt("Bin", "Capacity", 0), FALSE);
        CheckDlgButton(hDlg, IDC_BINEMPTYEXIT, CfgGetInt("Bin", "EmptyOnExit", 0));
        CheckDlgButton(hDlg, IDC_BINDIRECT, !g_cfg.bDeleteToBin);
        LoadBin();
        for (i = 0; i < s_nItems; i++)
            dwTotal += s_lpItems[i].dwSize;
        FormatBytes(dwTotal, szSize);
        wsprintf(szText, "The Bin holds %d item(s), %s.", s_nItems, (LPSTR)szSize);
        SetDlgItemText(hDlg, IDC_BINSTATUS, szText);
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDOK) {
            i = GetDlgItemInt(hDlg, IDC_BINCAPACITY, &bOK, FALSE);
            if (!bOK) {
                MessageBox(hDlg, "Please enter the capacity in MB (0 for no limit).",
                           "Bin Properties", MB_OK | MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(hDlg, IDC_BINCAPACITY));
                return TRUE;
            }
            CfgSetInt("Bin", "Capacity", i);
            CfgSetInt("Bin", "EmptyOnExit", IsDlgButtonChecked(hDlg, IDC_BINEMPTYEXIT) != 0);
            g_cfg.bDeleteToBin = !IsDlgButtonChecked(hDlg, IDC_BINDIRECT);
            CfgSave();
            GetDlgItemText(hDlg, IDC_BINCAPTION, szText, 40);
            if (!szText[0])
                lstrcpy(szText, "Bin");
            CfgSetStr("Desktop", "BinCaption", szText);
            if (s_hwndBin)
                SetWindowText(s_hwndBin, szText);
            EndDialog(hDlg, IDOK);
            DesktopCaptionsChanged();
            BinChanged();
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

void BinPropertiesDialog(HWND hwndOwner)
{
    FARPROC lpfn = MakeProcInstance((FARPROC)BinPropsDlgProc, g_hInst);

    if (!s_lpItems)
        s_lpItems = (LPBINITEM)malloc(MAX_BIN * sizeof(BINITEM));
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_BINPROPS), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
}

void BinClose(void)
{
    if (s_hwndBin)
        DestroyWindow(s_hwndBin);
    if (s_lpItems) {
        free(s_lpItems);
        s_lpItems = NULL;
    }
}
