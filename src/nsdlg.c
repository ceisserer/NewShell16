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
 * nsdlg.c - NSDLG.DLL: the Open and Save As dialogs of Windows 95, XP and
 * 11 in place of those of COMMDLG.DLL
 *
 * NsDlgInstall() overwrites the first five bytes of COMMDLG's
 * GetOpenFileName and GetSaveFileName with a JMP FAR to NsGetOpenFileName
 * and NsGetSaveFileName (the code segment is fixed and page-locked, and
 * written through a data alias, as Retheme16 patches USER and GDI). Every
 * program calling them gets this dialog; it runs in the program's task,
 * on its stack (so the working buffers are in the dialog's state, on the
 * global heap) and returns what COMMDLG would: 8.3 paths, the Windows 3.1
 * multiple selection format, nFileOffset, nFileExtension, the flags.
 *
 * Long names are shown and can be typed where the drive has them (lfn.c).
 * Saving under a new long name creates the file, empty, so that it gets
 * its 8.3 alias, which is returned (Windows 95 does the same for 16-bit
 * programs). Programs with a dialog template or hook procedure of their
 * own get the original COMMDLG dialog, whose controls they expect.
 *
 * Looks (as the shell's look, or NEWSHELL.INI [FileDialog] Style=):
 *   95  "Look in" list, Up / New Folder / List / Details, the file list
 *   XP  the same with Back and the places bar on the left; resizable
 *   11  Back, Up, an address bar and a search box, a navigation pane
 *       (Quick access, This PC with the drives), details; resizable
 */
#define STRICT
#include <windows.h>
#include <commdlg.h>
#include <dlgs.h>
#include <shellapi.h>
#include <string.h>
#include <dos.h>
#include "lfn.h"
#include "nsdlg.h"
#include "nsdlgres.h"

/* -------------------------------------------------------------------- */
/* Definitions                                                          */
/* -------------------------------------------------------------------- */

#define SPATH           144         /* an 8.3 path */
#define MAX_ITEMS       1200
#define MAX_PLACES      20
#define MAX_LOOKIN      24
#define MAX_HISTORY     10
#define MAX_EXTICONS    40
#define POOL_SIZE       32000U

/* control ids (the COMMDLG ones where there is one) */
#define ID_FILENAME     edt1
#define ID_TYPES        cmb1
#define ID_LOOKIN       cmb2
#define ID_LIST         lst1
#define ID_READONLY     chx1
#define ID_HELP         pshHelp
#define ID_LBLNAME      stc3
#define ID_LBLTYPES     stc2
#define ID_LBLLOOKIN    stc4
#define ID_BACK         0x500
#define ID_UP           0x501
#define ID_NEWDIR       0x502
#define ID_VLIST        0x503
#define ID_VDETAILS     0x504
#define ID_VIEWS        0x505
#define ID_PLACES       0x506
#define ID_ADDRESS      0x507
#define ID_SEARCH       0x508
#define ID_SEARCHICON   0x509
#define ID_RENAME       0x50A

/* list notifications (HIWORD of lParam) */
#define LN_SELCHANGE    1
#define LN_OPEN         2
#define LN_UP           3

#define IK_FILE         0
#define IK_DIR          1
#define IK_DRIVE        2

#define VM_LIST         0
#define VM_DETAILS      1

typedef struct tagDITEM {
    char  szShort[13];
    BYTE  bKind;                    /* IK_* */
    BYTE  bAttr;
    BYTE  bSel;
    BYTE  bDrive;                   /* drives: IDI_* */
    BYTE  bIcon;                    /* 0 not looked up, 1 hIcon, 2 none */
    BYTE  bOwnIcon;                 /* hIcon is the item's (a program's) */
    WORD  wLong;                    /* long name: offset + 1 in the pool */
    DWORD dwSize;
    WORD  wDate, wTime;
    HICON hIcon;
} DITEM, FAR *LPDITEM;

typedef struct tagPLACE {
    char  szText[28];
    char  szPath[SPATH];
    BYTE  bHeader;                  /* a group title (11) */
    BYTE  bIcon;                    /* IDI_* */
    BYTE  bSmi;                     /* SMI_* + 1, 0 = the icon */
} PLACE;

typedef struct tagEXTICON {
    char  szExt[4];
    HICON hIcon;
} EXTICON;

typedef struct tagFDLG {
    LPOPENFILENAME ofn;
    BOOL    bSave;
    int     nLook;
    HWND    hDlg, hList, hEdit, hTypes, hLookIn, hPlaces, hAddress, hSearch;
    HFONT   hFont;
    HFONT   hfNormal;               /* ours, to delete */
    int     cyFont;
    char    szDir[SPATH];           /* 8.3 path, "" = Computer */
    char    aHistory[MAX_HISTORY][SPATH];
    int     nHistory;
    /* items */
    HGLOBAL hItems;
    LPDITEM lpItems;
    int     nItems;
    HGLOBAL hPool;
    LPSTR   lpPool;
    WORD    cbPool;
    /* list */
    int     nMode, nFocus, nAnchor, nTop, nLeft;
    int     cyItem, cxCol, nRows, nVisCols, nVisRows, cyHeader;
    BOOL    bMulti;
    char    szTypeAhead[16];
    DWORD   dwTypeTime;
    HWND    hRename;
    int     nRenaming;
    /* filters */
    char    szFilter[128];          /* the patterns shown, "*.TXT;*.DOC" */
    char    szSearch[64];
    /* places / navigation pane / look in */
    PLACE   aPlace[MAX_PLACES];
    int     nPlaces;
    char    aLookIn[MAX_LOOKIN][SPATH];
    int     nLookIn;
    RECT    aSeg[12];               /* address bar segments (11) */
    char    aSegPath[12][SPATH];
    int     nSegs;
    /* icons */
    EXTICON aExt[MAX_EXTICONS];
    int     nExt;
    HBITMAP hbmTmp;
    HBRUSH  hbrPane;
    /* scratch (the stack is the program's, keep it small) */
    char    szWork[LFN_MAXPATH * 2];
    char    szWork2[LFN_MAXPATH];
    char    szShort[SPATH];
    BOOL    bInLayout;
    int     cxMin, cyMin;
} FDLG, FAR *LPFDLG;

#define PROP_FDLG       "NsDlgState"

#ifndef ODS_COMBOBOXEDIT
#define ODS_COMBOBOXEDIT 0x1000     /* the selection field of the combo box */
#endif

/* shared by every task (the DLL has one data segment) */
static HINSTANCE s_hInst;
static int       s_nLook = NSDLG_OFF;
static WORD      s_wFlags;
static char      s_szIni[80] = "NEWSHELL.INI";
static HBITMAP   s_hbmSmall, s_hbmMask;
static LPCSTR    s_lpSortPool;
static BOOL      s_bClasses;
static HINSTANCE s_hCommDlg;
static int       s_nRunning;        /* dialogs open, in any task */

/* -------------------------------------------------------------------- */
/* Patching COMMDLG                                                     */
/* -------------------------------------------------------------------- */

typedef BOOL (FAR PASCAL *GETFILENAMEPROC)(LPOPENFILENAME);

typedef struct tagPATCH {
    FARPROC lpTarget;
    UINT    uAlias;
    HGLOBAL hSeg;
    BYTE    abSaved[5];
    BYTE    abJmp[5];
    BOOL    bOk;
    BOOL    bWritten;
    BOOL    bLocked, bFixed;
    int     nOriginal;              /* calls of the original running */
} PATCH;

static PATCH s_patch[2];            /* 0 GetOpenFileName, 1 GetSaveFileName */

static BYTE FAR *PatchBytes(PATCH *p)
{
    return (BYTE FAR *)MK_FP(p->uAlias, FP_OFF(p->lpTarget));
}

static void PatchWrite(PATCH *p, BOOL bJmp)
{
    BYTE FAR *b;

    if (!p->bOk || p->bWritten == bJmp)
        return;
    b = PatchBytes(p);
    if (bJmp)
        _fmemcpy(b, p->abJmp, 5);
    else if (_fmemcmp(b, p->abJmp, 5) == 0)
        _fmemcpy(b, p->abSaved, 5);     /* not over someone else's patch */
    p->bWritten = bJmp;
}

static BOOL PatchInit(PATCH *p, WORD wOrdinal, FARPROC lpHandler)
{
    _fmemset(p, 0, sizeof(*p));
    p->lpTarget = GetProcAddress(s_hCommDlg, MAKEINTRESOURCE(wOrdinal));
    if (!p->lpTarget)
        return FALSE;
    /* the segment must neither move nor be discarded (a reload from the
       file would lose the JMP) */
    p->hSeg = GetCodeHandle(p->lpTarget);
    if (!p->hSeg)
        return FALSE;
    GlobalFix(p->hSeg);
    p->bFixed = TRUE;
    p->bLocked = GlobalPageLock(p->hSeg) != 0;
    p->uAlias = AllocSelector(FP_SEG(p->lpTarget));
    if (!p->uAlias)
        return FALSE;
    PrestoChangoSelector(FP_SEG(p->lpTarget), p->uAlias);
    _fmemcpy(p->abSaved, PatchBytes(p), 5);
    p->abJmp[0] = 0xEA;                 /* JMP FAR seg:ofs */
    p->abJmp[1] = (BYTE)FP_OFF(lpHandler);
    p->abJmp[2] = (BYTE)(FP_OFF(lpHandler) >> 8);
    p->abJmp[3] = (BYTE)FP_SEG(lpHandler);
    p->abJmp[4] = (BYTE)(FP_SEG(lpHandler) >> 8);
    p->bOk = TRUE;
    return TRUE;
}

static void PatchFree(PATCH *p)
{
    PatchWrite(p, FALSE);
    if (p->uAlias)
        FreeSelector(p->uAlias);
    if (p->bLocked)
        GlobalPageUnlock(p->hSeg);
    if (p->bFixed)
        GlobalUnfix(p->hSeg);
    _fmemset(p, 0, sizeof(*p));
}

/* The COMMDLG dialog, for programs with templates or hooks */
static BOOL CallOriginal(int n, LPOPENFILENAME lpofn)
{
    PATCH *p = &s_patch[n];
    BOOL bRet;

    if (!p->bOk)
        return FALSE;
    p->nOriginal++;
    PatchWrite(p, FALSE);
    bRet = ((GETFILENAMEPROC)p->lpTarget)(lpofn);
    if (--p->nOriginal == 0 && s_nLook != NSDLG_OFF)
        PatchWrite(p, TRUE);
    return bRet;
}

/* -------------------------------------------------------------------- */
/* Small helpers                                                        */
/* -------------------------------------------------------------------- */

static int CfgInt(LPCSTR lpKey, int nDef)
{
    return (int)GetPrivateProfileInt("FileDialog", lpKey, nDef, s_szIni);
}

static void FillSolid(HDC hdc, const RECT FAR *lprc, COLORREF clr)
{
    COLORREF clrOld = SetBkColor(hdc, clr);
    ExtTextOut(hdc, 0, 0, ETO_OPAQUE, lprc, NULL, 0, NULL);
    SetBkColor(hdc, clrOld);
}

static COLORREF Blend(COLORREF a, COLORREF b, int nA)   /* nA of 256 of a */
{
    return RGB((GetRValue(a) * nA + GetRValue(b) * (256 - nA)) >> 8,
               (GetGValue(a) * nA + GetGValue(b) * (256 - nA)) >> 8,
               (GetBValue(a) * nA + GetBValue(b) * (256 - nA)) >> 8);
}

static void Frame(HDC hdc, const RECT FAR *lprc, COLORREF clrTL, COLORREF clrBR)
{
    RECT r;

    SetRect(&r, lprc->left, lprc->top, lprc->right - 1, lprc->top + 1);
    FillSolid(hdc, &r, clrTL);
    SetRect(&r, lprc->left, lprc->top, lprc->left + 1, lprc->bottom - 1);
    FillSolid(hdc, &r, clrTL);
    SetRect(&r, lprc->left, lprc->bottom - 1, lprc->right, lprc->bottom);
    FillSolid(hdc, &r, clrBR);
    SetRect(&r, lprc->right - 1, lprc->top, lprc->right, lprc->bottom);
    FillSolid(hdc, &r, clrBR);
}

/* A rounded rectangle, drawn with the pen and brush colours given */
static void RoundBox(HDC hdc, const RECT FAR *lprc, int r, COLORREF clrFill, COLORREF clrLine)
{
    HBRUSH hbr = CreateSolidBrush(clrFill), hbrOld;
    HPEN hpen = CreatePen(PS_SOLID, 1, clrLine), hpenOld;

    hbrOld = SelectObject(hdc, hbr);
    hpenOld = SelectObject(hdc, hpen);
    RoundRect(hdc, lprc->left, lprc->top, lprc->right, lprc->bottom, r, r);
    SelectObject(hdc, hbrOld);
    SelectObject(hdc, hpenOld);
    DeleteObject(hbr);
    DeleteObject(hpen);
}

static void LoadSmallImages(void)
{
    HDC hdcScreen, hdcColor, hdcMask;
    HBITMAP hbmOld1, hbmOld2;
    BITMAP bm;

    if (s_hbmSmall)
        return;
    s_hbmSmall = LoadBitmap(s_hInst, MAKEINTRESOURCE(IDB_DLGSMALL));
    if (!s_hbmSmall)
        return;
    GetObject(s_hbmSmall, sizeof(bm), &bm);
    s_hbmMask = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);
    hdcScreen = GetDC(NULL);
    hdcColor = CreateCompatibleDC(hdcScreen);
    hdcMask = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);
    hbmOld1 = SelectObject(hdcColor, s_hbmSmall);
    hbmOld2 = SelectObject(hdcMask, s_hbmMask);
    SetBkColor(hdcColor, RGB(255, 0, 255));     /* the colour key */
    BitBlt(hdcMask, 0, 0, bm.bmWidth, bm.bmHeight, hdcColor, 0, 0, SRCCOPY);
    SetBkColor(hdcColor, RGB(255, 255, 255));
    SetTextColor(hdcColor, RGB(0, 0, 0));
    BitBlt(hdcColor, 0, 0, bm.bmWidth, bm.bmHeight, hdcMask, 0, 0, 0x00220326L);
    SelectObject(hdcColor, hbmOld1);
    SelectObject(hdcMask, hbmOld2);
    DeleteDC(hdcColor);
    DeleteDC(hdcMask);
}

static void DrawSmall(HDC hdc, int x, int y, int n)
{
    HDC hdcMem;
    HBITMAP hbmOld;
    COLORREF clrBk, clrText;

    LoadSmallImages();
    if (!s_hbmSmall)
        return;
    hdcMem = CreateCompatibleDC(hdc);
    clrBk = SetBkColor(hdc, RGB(255, 255, 255));
    clrText = SetTextColor(hdc, RGB(0, 0, 0));
    hbmOld = SelectObject(hdcMem, s_hbmMask);
    BitBlt(hdc, x, y, 16, 16, hdcMem, n * 16, 0, SRCAND);
    SelectObject(hdcMem, s_hbmSmall);
    BitBlt(hdc, x, y, 16, 16, hdcMem, n * 16, 0, SRCPAINT);
    SelectObject(hdcMem, hbmOld);
    SetBkColor(hdc, clrBk);
    SetTextColor(hdc, clrText);
    DeleteDC(hdcMem);
}

/* A 32x32 icon at 16x16 over the colour clrBk */
static void DrawIconSmall(LPFDLG d, HDC hdc, int x, int y, HICON hIcon, COLORREF clrBk)
{
    HDC hdcMem;
    HBITMAP hbmOld;
    RECT rc;
    int nOld;

    if (!hIcon)
        return;
    if (!d->hbmTmp)
        d->hbmTmp = CreateCompatibleBitmap(hdc, 32, 32);
    hdcMem = CreateCompatibleDC(hdc);
    hbmOld = SelectObject(hdcMem, d->hbmTmp);
    SetRect(&rc, 0, 0, 32, 32);
    FillSolid(hdcMem, &rc, clrBk);
    DrawIcon(hdcMem, 0, 0, hIcon);
    nOld = SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(hdc, x, y, 16, 16, hdcMem, 0, 0, 32, 32, SRCCOPY);
    SetStretchBltMode(hdc, nOld);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
}

static int TextWidth(HDC hdc, LPCSTR lp)
{
    return LOWORD(GetTextExtent(hdc, lp, lstrlen(lp)));
}

/* lpSrc shortened with "..." to cxMax pixels */
static void FitText(HDC hdc, LPCSTR lpSrc, LPSTR lpDst, int cbDst, int cxMax)
{
    int n;

    lstrcpyn(lpDst, lpSrc, cbDst);
    if (TextWidth(hdc, lpDst) <= cxMax)
        return;
    n = lstrlen(lpDst);
    if (n > cbDst - 4)
        n = cbDst - 4;
    for (; n > 0; n--) {
        lstrcpy(lpDst + n, "...");
        if (TextWidth(hdc, lpDst) <= cxMax)
            return;
    }
    lstrcpyn(lpDst, "...", cbDst);
}

static LPSTR FileNameOf(LPSTR lpPath)
{
    LPSTR p, lpName = lpPath;

    for (p = lpPath; *p; p++)
        if (*p == '\\' || *p == ':')
            lpName = p + 1;
    return lpName;
}

static LPSTR ExtOf(LPSTR lpName)
{
    LPSTR p, lpDot = NULL;

    for (p = FileNameOf(lpName); *p; p++)
        if (*p == '.')
            lpDot = p;
    return lpDot ? lpDot + 1 : p;
}

static void CatPath(LPSTR lpPath, LPCSTR lpName, int cb)
{
    int n = lstrlen(lpPath);

    if (n > 0 && lpPath[n - 1] != '\\' && n + 1 < cb)
        lstrcat(lpPath, "\\");
    n = lstrlen(lpPath);
    lstrcpyn(lpPath + n, lpName, cb - n);
}

/* "C:\A\B" -> "C:\A", "C:\A" -> "C:\"; FALSE at a root */
static BOOL ParentOf(LPSTR lpPath)
{
    LPSTR lpName;

    if (lstrlen(lpPath) <= 3)
        return FALSE;
    lpName = FileNameOf(lpPath);
    if (lpName > lpPath)
        lpName[-1] = '\0';
    if (lstrlen(lpPath) == 2 && lpPath[1] == ':')
        lstrcat(lpPath, "\\");
    return TRUE;
}

static BOOL HasWildcards(LPCSTR lp)
{
    return _fstrchr(lp, '*') || _fstrchr(lp, '?');
}

/* DOS wildcards, without case; "*.*" also matches names without a dot */
static BOOL WildMatch(LPCSTR p, LPCSTR s)
{
    while (*p) {
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (!*p)
                return TRUE;
            if (*p == '.' && p[1] == '*' && !p[2] && !_fstrchr(s, '.'))
                return TRUE;
            for (; *s; s++)
                if (WildMatch(p, s))
                    return TRUE;
            return *p == '.' && p[1] == '*' && !p[2];
        }
        if (!*s) {
            /* "NAME" matches "NAME.*" and "NAME." */
            return *p == '.' && (!p[1] || (p[1] == '*' && !p[2]));
        }
        if (*p != '?' && (BYTE)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)*p)
                         != (BYTE)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)*s))
            return FALSE;
        p++;
        s++;
    }
    return !*s;
}

static BOOL MatchList(LPCSTR lpName, LPCSTR lpList)
{
    char szPat[40];
    int n;

    while (*lpList) {
        while (*lpList == ';' || *lpList == ' ')
            lpList++;
        for (n = 0; *lpList && *lpList != ';' && n < (int)sizeof(szPat) - 1; )
            szPat[n++] = *lpList++;
        while (n > 0 && szPat[n - 1] == ' ')
            n--;
        szPat[n] = '\0';
        if (n && WildMatch(szPat, lpName))
            return TRUE;
    }
    return FALSE;
}

/* case-insensitive substring */
static BOOL Contains(LPCSTR lpText, LPCSTR lpPart)
{
    int n = lstrlen(lpPart), m = lstrlen(lpText), i;

    for (i = 0; i + n <= m; i++)
        if (_fstrnicmp(lpText + i, lpPart, n) == 0)
            return TRUE;
    return FALSE;
}

static int DriveIdi(int d, BOOL bForList)
{
    (void)bForList;
    switch (GetDriveType(d)) {
    case DRIVE_REMOVABLE:
        return IDI_DFLOPPY;
    case DRIVE_REMOTE:
        return IDI_DNETDRV;
    case DRIVE_FIXED:
        {
            /* MSCDEX: INT 2Fh 150Bh, BX = ADADh when it knows the drive */
            union REGS r;
            r.x.ax = 0x150B;
            r.x.bx = 0;
            r.x.cx = d;
            int86(0x2F, &r, &r);
            if (r.x.bx == 0xADAD && r.x.ax != 0)
                return IDI_DCDROM;
        }
        return IDI_DHDD;
    }
    return IDI_DHDD;
}

static LPCSTR DriveKind(int idi)
{
    switch (idi) {
    case IDI_DFLOPPY:
        return "Floppy";
    case IDI_DNETDRV:
        return "Network Drive";
    case IDI_DCDROM:
        return "CD Drive";
    }
    return "Local Disk";
}

static BOOL DriveReady(int d)
{
    struct find_t ft;
    char szPat[8];
    UINT uOld = SetErrorMode(1);
    unsigned rc;

    wsprintf(szPat, "%c:\\*.*", 'A' + d);
    rc = _dos_findfirst(szPat, _A_NORMAL | _A_SUBDIR | _A_VOLID, &ft);
    SetErrorMode(uOld);
    return rc == 0 || rc == 0x12;
}

static void FormatSize(DWORD dw, LPSTR lpBuf)
{
    if (dw < 1024L)
        wsprintf(lpBuf, "%lu bytes", dw);
    else if (dw < 1024L * 1024L)
        wsprintf(lpBuf, "%lu KB", (dw + 1023) / 1024);
    else
        wsprintf(lpBuf, "%lu.%lu MB", dw / 1048576L, (dw % 1048576L) * 10 / 1048576L);
}

static void FormatDateTime(WORD wDate, WORD wTime, LPSTR lpBuf)
{
    int nDate = GetProfileInt("intl", "iDate", 0);
    int d = wDate & 31, m = (wDate >> 5) & 15, y = (wDate >> 9) + 1980;
    int h = wTime >> 11, mi = (wTime >> 5) & 63;
    char szSep[4];

    GetProfileString("intl", "sDate", "/", szSep, sizeof(szSep));
    if (nDate == 1)
        wsprintf(lpBuf, "%02d%s%02d%s%04d", d, (LPSTR)szSep, m, (LPSTR)szSep, y);
    else if (nDate == 2)
        wsprintf(lpBuf, "%04d%s%02d%s%02d", y, (LPSTR)szSep, m, (LPSTR)szSep, d);
    else
        wsprintf(lpBuf, "%02d%s%02d%s%04d", m, (LPSTR)szSep, d, (LPSTR)szSep, y);
    wsprintf(lpBuf + lstrlen(lpBuf), " %02d:%02d", h, mi);
}

/* -------------------------------------------------------------------- */
/* Items                                                                */
/* -------------------------------------------------------------------- */

#define ITEMNAME(pool, p)   ((p)->wLong ? (LPCSTR)((pool) + (p)->wLong - 1) \
                                        : (LPCSTR)(p)->szShort)
#define LongOf(d, p)        ITEMNAME((d)->lpPool, p)

static void FreeItemIcons(LPFDLG d)
{
    int i;

    for (i = 0; i < d->nItems; i++)
        if (d->lpItems[i].bOwnIcon && d->lpItems[i].hIcon)
            DestroyIcon(d->lpItems[i].hIcon);
}

static WORD AddLong(LPFDLG d, LPCSTR lpName)
{
    WORD cb = (WORD)lstrlen(lpName) + 1, w;

    if (!d->lpPool || d->cbPool + cb > POOL_SIZE)
        return 0;
    w = d->cbPool + 1;
    lstrcpy(d->lpPool + d->cbPool, lpName);
    d->cbPool += cb;
    return w;
}

static int CompareItems(LPDITEM p, LPDITEM q)
{
    if (p->bKind != q->bKind)
        return (int)q->bKind - (int)p->bKind;   /* drives, folders, files */
    return lstrcmpi(ITEMNAME(s_lpSortPool, p), ITEMNAME(s_lpSortPool, q));
}

/* Shell sort (no C library qsort in the DLL) */
static void SortItems(LPFDLG d)
{
    int gap, i, j;
    DITEM t;

    s_lpSortPool = d->lpPool;
    for (gap = d->nItems / 2; gap > 0; gap /= 2)
        for (i = gap; i < d->nItems; i++) {
            t = d->lpItems[i];
            for (j = i; j >= gap && CompareItems(&d->lpItems[j - gap], &t) > 0; j -= gap)
                d->lpItems[j] = d->lpItems[j - gap];
            d->lpItems[j] = t;
        }
}

static LPDITEM NewItem(LPFDLG d)
{
    LPDITEM p;

    if (d->nItems >= MAX_ITEMS)
        return NULL;
    p = &d->lpItems[d->nItems++];
    _fmemset(p, 0, sizeof(*p));
    return p;
}

static void ReadItems(LPFDLG d)
{
    LFNFIND FAR *f;
    HGLOBAL hFind;
    LPDITEM p;
    UINT uOld;
    int i;

    FreeItemIcons(d);
    d->nItems = 0;
    d->cbPool = 0;
    if (!d->szDir[0]) {
        for (i = 0; i < 26; i++) {
            if (GetDriveType(i) == 0 || !(p = NewItem(d)))
                continue;
            wsprintf(p->szShort, "%c:", 'A' + i);
            p->bKind = IK_DRIVE;
            p->bDrive = (BYTE)DriveIdi(i, TRUE);
        }
        return;
    }
    hFind = GlobalAlloc(GMEM_MOVEABLE, sizeof(LFNFIND));
    if (!hFind)
        return;
    f = (LFNFIND FAR *)GlobalLock(hFind);
    lstrcpy(d->szWork, d->szDir);
    CatPath(d->szWork, "*.*", sizeof(d->szWork));
    uOld = SetErrorMode(1);
    if (LfnFindFirst(d->szWork, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_SUBDIR, f) == 0) {
        do {
            BOOL bDir = (f->bAttr & _A_SUBDIR) != 0;

            if (f->szShort[0] == '.')
                continue;
            if (!bDir && !MatchList(f->szName, d->szFilter)
                && !MatchList(f->szShort, d->szFilter))
                continue;
            if (d->szSearch[0] && !Contains(f->szName, d->szSearch))
                continue;
            if (!(p = NewItem(d)))
                break;
            lstrcpyn(p->szShort, f->szShort, sizeof(p->szShort));
            if (lstrcmp(f->szName, f->szShort) != 0)
                p->wLong = AddLong(d, f->szName);
            p->bKind = (BYTE)(bDir ? IK_DIR : IK_FILE);
            p->bAttr = f->bAttr;
            p->dwSize = f->dwSize;
            p->wDate = f->wDate;
            p->wTime = f->wTime;
        } while (LfnFindNext(f) == 0);
    }
    LfnFindClose(f);
    SetErrorMode(uOld);
    GlobalUnlock(hFind);
    GlobalFree(hFind);
    SortItems(d);
}

static BOOL IsProgram(LPCSTR lpExt)
{
    return lstrcmpi(lpExt, "EXE") == 0 || lstrcmpi(lpExt, "COM") == 0
        || lstrcmpi(lpExt, "BAT") == 0 || lstrcmpi(lpExt, "PIF") == 0;
}

/* The icon of a file, looked up when it is first drawn */
static void ItemIcon(LPFDLG d, LPDITEM p)
{
    LPSTR lpExt;
    int i;

    if (p->bIcon)
        return;
    p->bIcon = 2;
    if (p->bKind != IK_FILE)
        return;
    lpExt = ExtOf(p->szShort);
    lstrcpy(d->szShort, d->szDir);
    CatPath(d->szShort, p->szShort, sizeof(d->szShort));
    if (lstrcmpi(lpExt, "EXE") == 0) {
        HICON h = ExtractIcon(s_hInst, d->szShort, 0);
        if ((UINT)h > 1) {
            p->hIcon = h;
            p->bOwnIcon = TRUE;
            p->bIcon = 1;
        }
        return;
    }
    if (!*lpExt || IsProgram(lpExt))
        return;
    for (i = 0; i < d->nExt; i++)
        if (lstrcmpi(d->aExt[i].szExt, lpExt) == 0) {
            p->hIcon = d->aExt[i].hIcon;
            p->bIcon = (BYTE)(p->hIcon ? 1 : 2);
            return;
        }
    if (d->nExt < MAX_EXTICONS) {
        char szExe[SPATH];
        HICON h = NULL;
        UINT uOld = SetErrorMode(1);

        if ((UINT)FindExecutable(d->szShort, d->szDir, szExe) > 32) {
            h = ExtractIcon(s_hInst, szExe, 0);
            if ((UINT)h <= 1)
                h = NULL;
        }
        SetErrorMode(uOld);
        lstrcpyn(d->aExt[d->nExt].szExt, lpExt, 4);
        d->aExt[d->nExt++].hIcon = h;
        p->hIcon = h;
        p->bIcon = (BYTE)(h ? 1 : 2);
    }
}

static void DrawItemIcon(LPFDLG d, HDC hdc, LPDITEM p, int x, int y, COLORREF clrBk)
{
    switch (p->bKind) {
    case IK_DRIVE:
        DrawIconSmall(d, hdc, x, y, LoadIcon(s_hInst, MAKEINTRESOURCE(p->bDrive)), clrBk);
        return;
    case IK_DIR:
        DrawIconSmall(d, hdc, x, y, LoadIcon(s_hInst, MAKEINTRESOURCE(IDI_DFOLDER)), clrBk);
        return;
    }
    ItemIcon(d, p);
    if (p->bIcon == 1)
        DrawIconSmall(d, hdc, x, y, p->hIcon, clrBk);
    else
        DrawSmall(hdc, x, y, IsProgram(ExtOf(p->szShort)) ? SMI_APP : SMI_DOCUMENT);
}

/* the text shown for an item: long name, "Local Disk (C:)" for drives */
static void ItemText(LPFDLG d, LPDITEM p, LPSTR lpBuf, int cb)
{
    if (p->bKind == IK_DRIVE) {
        wsprintf(lpBuf, "%s (%s)", (LPSTR)DriveKind(p->bDrive), (LPSTR)p->szShort);
        return;
    }
    lstrcpyn(lpBuf, LongOf(d, p), cb);
}

static void ItemType(LPDITEM p, LPSTR lpBuf)
{
    LPSTR lpExt;

    if (p->bKind == IK_DRIVE) {
        lstrcpy(lpBuf, DriveKind(p->bDrive));
        return;
    }
    if (p->bKind == IK_DIR) {
        lstrcpy(lpBuf, "File Folder");
        return;
    }
    lpExt = ExtOf(p->szShort);
    if (IsProgram(lpExt) && lstrcmpi(lpExt, "PIF") != 0)
        lstrcpy(lpBuf, "Application");
    else if (*lpExt) {
        wsprintf(lpBuf, "%s File", lpExt);
        AnsiUpper(lpBuf);
        AnsiLower(lpBuf + lstrlen(lpExt) + 2);
    } else
        lstrcpy(lpBuf, "File");
}

/* -------------------------------------------------------------------- */
/* The file list                                                        */
/* -------------------------------------------------------------------- */

static LPFDLG ListState(HWND hwnd)
{
    return (LPFDLG)GetWindowLong(hwnd, 0);
}

static void ListNotify(LPFDLG d, WORD wCode)
{
    SendMessage(d->hDlg, WM_COMMAND, ID_LIST, MAKELPARAM(d->hList, wCode));
}

#define COLS_DETAILS    4
static void DetailWidths(LPFDLG d, int cx, int FAR *a)
{
    a[1] = 70;                          /* Size */
    a[2] = d->nLook == NSDLG_11 ? 110 : 100;    /* Type */
    a[3] = 110;                         /* Modified */
    a[0] = cx - a[1] - a[2] - a[3];
    if (a[0] < 130)
        a[0] = 130;
}

static void ListMetrics(LPFDLG d)
{
    RECT rc;
    HDC hdc;
    HFONT hfOld;
    int i, cx;
    char szText[LFN_MAXNAME];

    GetClientRect(d->hList, &rc);
    d->cyItem = d->cyFont + (d->nLook == NSDLG_11 ? 8 : 3);
    if (d->cyItem < 18)
        d->cyItem = 18;
    d->cyHeader = d->nMode == VM_DETAILS ? d->cyFont + 8 : 0;
    if (d->nMode == VM_DETAILS) {
        d->nVisRows = (rc.bottom - d->cyHeader) / d->cyItem;
        if (d->nVisRows < 1)
            d->nVisRows = 1;
        return;
    }
    d->nRows = rc.bottom / d->cyItem;
    if (d->nRows < 1)
        d->nRows = 1;
    hdc = GetDC(d->hList);
    hfOld = SelectObject(hdc, d->hFont);
    d->cxCol = 100;
    for (i = 0; i < d->nItems; i++) {
        ItemText(d, &d->lpItems[i], szText, sizeof(szText));
        cx = TextWidth(hdc, szText) + 30;
        if (cx > d->cxCol)
            d->cxCol = cx;
    }
    SelectObject(hdc, hfOld);
    ReleaseDC(d->hList, hdc);
    if (d->cxCol > 260)
        d->cxCol = 260;
    d->nVisCols = rc.right / d->cxCol;
    if (d->nVisCols < 1)
        d->nVisCols = 1;
}

static void ListScrollBars(LPFDLG d)
{
    if (d->nMode == VM_DETAILS) {
        int nMax = d->nItems - d->nVisRows;
        if (nMax < 0)
            nMax = 0;
        if (d->nTop > nMax)
            d->nTop = nMax;
        ShowScrollBar(d->hList, SB_HORZ, FALSE);
        SetScrollRange(d->hList, SB_VERT, 0, nMax, FALSE);
        SetScrollPos(d->hList, SB_VERT, d->nTop, TRUE);
        ShowScrollBar(d->hList, SB_VERT, nMax > 0);
    } else {
        int nCols = (d->nItems + d->nRows - 1) / d->nRows;
        int nMax = nCols - d->nVisCols;
        if (nMax < 0)
            nMax = 0;
        if (d->nLeft > nMax)
            d->nLeft = nMax;
        ShowScrollBar(d->hList, SB_VERT, FALSE);
        SetScrollRange(d->hList, SB_HORZ, 0, nMax, FALSE);
        SetScrollPos(d->hList, SB_HORZ, d->nLeft, TRUE);
        ShowScrollBar(d->hList, SB_HORZ, nMax > 0);
    }
}

/* Layout and scroll bars after the items or the size changed. Showing a
   scroll bar changes the client area, so it is done twice. */
static void ListUpdate(LPFDLG d)
{
    ListMetrics(d);
    ListScrollBars(d);
    ListMetrics(d);
    ListScrollBars(d);
    InvalidateRect(d->hList, NULL, FALSE);
}

static BOOL ItemRect(LPFDLG d, int i, RECT FAR *lprc)
{
    RECT rc;

    GetClientRect(d->hList, &rc);
    if (d->nMode == VM_DETAILS) {
        int y = d->cyHeader + (i - d->nTop) * d->cyItem;
        SetRect(lprc, 0, y, rc.right, y + d->cyItem);
    } else {
        int col = i / d->nRows - d->nLeft, row = i % d->nRows;
        SetRect(lprc, col * d->cxCol, row * d->cyItem, (col + 1) * d->cxCol,
                (row + 1) * d->cyItem);
    }
    return lprc->bottom > 0 && lprc->top < rc.bottom && lprc->right > 0
           && lprc->left < rc.right;
}

static int HitTest(LPFDLG d, int x, int y)
{
    int i;

    if (d->nMode == VM_DETAILS) {
        if (y < d->cyHeader)
            return -1;
        i = d->nTop + (y - d->cyHeader) / d->cyItem;
    } else {
        if (y / d->cyItem >= d->nRows)
            return -1;
        i = (x / d->cxCol + d->nLeft) * d->nRows + y / d->cyItem;
    }
    return i >= 0 && i < d->nItems ? i : -1;
}

static void EnsureVisible(LPFDLG d, int i)
{
    if (i < 0)
        return;
    if (d->nMode == VM_DETAILS) {
        if (i < d->nTop)
            d->nTop = i;
        else if (i >= d->nTop + d->nVisRows)
            d->nTop = i - d->nVisRows + 1;
    } else {
        int col = i / d->nRows;
        if (col < d->nLeft)
            d->nLeft = col;
        else if (col >= d->nLeft + d->nVisCols)
            d->nLeft = col - d->nVisCols + 1;
    }
    ListScrollBars(d);
    InvalidateRect(d->hList, NULL, FALSE);
}

static COLORREF SelColor(LPFDLG d, BOOL bFocus)
{
    if (d->nLook == NSDLG_11)
        return bFocus ? RGB(204, 232, 255) : RGB(229, 229, 229);
    return bFocus ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE);
}

static void PaintItem(LPFDLG d, HDC hdc, int i, const RECT FAR *lprc, BOOL bFocus)
{
    LPDITEM p = &d->lpItems[i];
    COLORREF clrWin = GetSysColor(COLOR_WINDOW);
    char szText[LFN_MAXNAME], szFit[LFN_MAXNAME];
    RECT rcText;
    int cxName, a[COLS_DETAILS], x, c;
    BOOL b11 = d->nLook == NSDLG_11;

    if (d->nMode == VM_DETAILS) {
        DetailWidths(d, lprc->right - lprc->left, a);
        cxName = a[0];
    } else
        cxName = lprc->right - lprc->left;
    if (b11 && p->bSel) {
        RECT rcSel = *lprc;
        InflateRect(&rcSel, -1, -1);
        RoundBox(hdc, &rcSel, 6, SelColor(d, bFocus), Blend(SelColor(d, bFocus), RGB(0, 0, 0), 220));
    }
    DrawItemIcon(d, hdc, p, lprc->left + 4, lprc->top + (d->cyItem - 16) / 2,
                 b11 && p->bSel ? SelColor(d, bFocus) : clrWin);
    ItemText(d, p, szText, sizeof(szText));
    FitText(hdc, szText, szFit, sizeof(szFit), cxName - 30);
    SetRect(&rcText, lprc->left + 22, lprc->top + 1,
            lprc->left + 26 + TextWidth(hdc, szFit), lprc->bottom - 1);
    if (p->bSel && !b11) {
        FillSolid(hdc, &rcText, SelColor(d, bFocus));
        SetTextColor(hdc, bFocus ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT));
    } else
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    /* hidden items are greyed, as in Windows 95 */
    if (p->bKind != IK_DRIVE && (p->bAttr & _A_HIDDEN) && !p->bSel)
        SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
    rcText.left += 2;
    DrawText(hdc, szFit, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    rcText.left -= 2;
    if (i == d->nFocus && bFocus && !b11)
        DrawFocusRect(hdc, &rcText);
    if (d->nMode != VM_DETAILS)
        return;
    SetTextColor(hdc, b11 ? RGB(96, 96, 96) : GetSysColor(COLOR_WINDOWTEXT));
    x = lprc->left + a[0];
    for (c = 1; c < COLS_DETAILS; x += a[c], c++) {
        UINT uFmt = DT_LEFT;
        szText[0] = '\0';
        switch (c) {
        case 1:
            if (p->bKind == IK_FILE)
                FormatSize(p->dwSize, szText);
            uFmt = DT_RIGHT;
            break;
        case 2:
            ItemType(p, szText);
            break;
        case 3:
            if (p->bKind != IK_DRIVE)
                FormatDateTime(p->wDate, p->wTime, szText);
            break;
        }
        SetRect(&rcText, x + 6, lprc->top, x + a[c] - 6, lprc->bottom);
        DrawText(hdc, szText, -1, &rcText, uFmt | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static void PaintHeader(LPFDLG d, HDC hdc, const RECT FAR *lprcClient)
{
    static const char *s_szCols[COLS_DETAILS] = { "Name", "Size", "Type", "Modified" };
    int a[COLS_DETAILS], x = 0, c;
    RECT rc;

    DetailWidths(d, lprcClient->right, a);
    for (c = 0; c <= COLS_DETAILS; c++) {
        int cx = c < COLS_DETAILS ? a[c] : lprcClient->right - x;
        if (cx <= 0)
            break;
        SetRect(&rc, x, 0, x + cx, d->cyHeader);
        if (d->nLook == NSDLG_11) {
            FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
            if (c < COLS_DETAILS) {
                RECT rl;
                SetRect(&rl, rc.right - 1, rc.top + 4, rc.right, rc.bottom - 4);
                FillSolid(hdc, &rl, RGB(229, 229, 229));
            }
            SetTextColor(hdc, RGB(96, 96, 96));
        } else {
            FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
            Frame(hdc, &rc, GetSysColor(COLOR_BTNHIGHLIGHT), GetSysColor(COLOR_BTNSHADOW));
            if (d->nLook == NSDLG_XP) {
                RECT rl = rc;
                rl.top = rl.bottom - 3;
                FillSolid(hdc, &rl, Blend(GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNSHADOW), 128));
            }
            SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        }
        if (c < COLS_DETAILS) {
            rc.left += 6;
            rc.right -= 6;
            DrawText(hdc, s_szCols[c], -1, &rc, (c == 1 ? DT_RIGHT : DT_LEFT)
                     | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        x += cx;
    }
    if (d->nLook == NSDLG_11) {
        SetRect(&rc, 0, d->cyHeader - 1, lprcClient->right, d->cyHeader);
        FillSolid(hdc, &rc, RGB(229, 229, 229));
    }
}

static void ListPaint(HWND hwnd, LPFDLG d)
{
    PAINTSTRUCT ps;
    RECT rc, rcItem;
    HDC hdc, hdcMem;
    HBITMAP hbm, hbmOld;
    HFONT hfOld;
    BOOL bFocus = GetFocus() == hwnd;
    int i;

    hdc = BeginPaint(hwnd, &ps);
    GetClientRect(hwnd, &rc);
    hdcMem = CreateCompatibleDC(hdc);
    hbm = CreateCompatibleBitmap(hdc, rc.right ? rc.right : 1, rc.bottom ? rc.bottom : 1);
    if (!hdcMem || !hbm) {
        if (hdcMem)
            DeleteDC(hdcMem);
        hdcMem = hdc;
    } else
        hbmOld = SelectObject(hdcMem, hbm);
    FillSolid(hdcMem, &rc, GetSysColor(COLOR_WINDOW));
    hfOld = SelectObject(hdcMem, d->hFont);
    SetBkMode(hdcMem, TRANSPARENT);
    if (d->nMode == VM_DETAILS)
        PaintHeader(d, hdcMem, &rc);
    for (i = 0; i < d->nItems; i++)
        if (i != d->nRenaming && ItemRect(d, i, &rcItem))
            PaintItem(d, hdcMem, i, &rcItem, bFocus);
    if (d->nItems == 0) {
        RECT rcEmpty = rc;
        rcEmpty.top = d->cyHeader + 8;
        SetTextColor(hdcMem, GetSysColor(COLOR_GRAYTEXT));
        DrawText(hdcMem, d->szSearch[0] ? "No items match your search." : "This folder is empty.",
                 -1, &rcEmpty, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(hdcMem, hfOld);
    if (hdcMem != hdc) {
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hbmOld);
        DeleteDC(hdcMem);
    }
    if (hbm)
        DeleteObject(hbm);
    EndPaint(hwnd, &ps);
}

static void SelectOnly(LPFDLG d, int n)
{
    int i;

    for (i = 0; i < d->nItems; i++)
        d->lpItems[i].bSel = (BYTE)(i == n);
}

static void MoveFocus(LPFDLG d, int n, BOOL bShift, BOOL bCtrl)
{
    int i, a, b;

    if (d->nItems == 0)
        return;
    if (n < 0)
        n = 0;
    if (n >= d->nItems)
        n = d->nItems - 1;
    d->nFocus = n;
    if (bShift && d->bMulti && d->nAnchor >= 0) {
        a = d->nAnchor < n ? d->nAnchor : n;
        b = d->nAnchor < n ? n : d->nAnchor;
        for (i = 0; i < d->nItems; i++)
            d->lpItems[i].bSel = (BYTE)(i >= a && i <= b);
    } else if (bCtrl && d->bMulti) {
        d->lpItems[n].bSel = (BYTE)!d->lpItems[n].bSel;
        d->nAnchor = n;
    } else {
        SelectOnly(d, n);
        d->nAnchor = n;
    }
    EnsureVisible(d, n);
    InvalidateRect(d->hList, NULL, FALSE);
    ListNotify(d, LN_SELCHANGE);
}

static void TypeAhead(LPFDLG d, char ch)
{
    DWORD dwNow = GetTickCount();
    int n = lstrlen(d->szTypeAhead), i, k;
    char szText[LFN_MAXNAME];

    if (dwNow - d->dwTypeTime > 1000 || n >= (int)sizeof(d->szTypeAhead) - 1)
        n = 0;
    d->dwTypeTime = dwNow;
    d->szTypeAhead[n++] = ch;
    d->szTypeAhead[n] = '\0';
    for (k = 0; k < d->nItems; k++) {
        /* a new first letter goes on from the next item */
        i = (d->nFocus + (n == 1 ? 1 : 0) + k) % d->nItems;
        if (i < 0)
            i = 0;
        ItemText(d, &d->lpItems[i], szText, sizeof(szText));
        if (_fstrnicmp(szText, d->szTypeAhead, n) == 0) {
            MoveFocus(d, i, FALSE, FALSE);
            return;
        }
    }
}

static void EndRename(LPFDLG d, BOOL bCommit);

LRESULT CALLBACK __export NsDlgListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LPFDLG d = ListState(hwnd);
    int i;

    if (!d)
        return DefWindowProc(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_PAINT:
        ListPaint(hwnd, d);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        ListUpdate(d);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (d->hRename)
            EndRename(d, TRUE);
        SetFocus(hwnd);
        i = HitTest(d, LOWORD(lParam), HIWORD(lParam));
        if (i < 0) {
            if (!(wParam & (MK_SHIFT | MK_CONTROL))) {
                SelectOnly(d, -1);
                InvalidateRect(hwnd, NULL, FALSE);
                ListNotify(d, LN_SELCHANGE);
            }
            return 0;
        }
        if (msg == WM_RBUTTONDOWN && d->lpItems[i].bSel)
            return 0;
        MoveFocus(d, i, (wParam & MK_SHIFT) != 0, (wParam & MK_CONTROL) != 0);
        return 0;
    case WM_LBUTTONDBLCLK:
        i = HitTest(d, LOWORD(lParam), HIWORD(lParam));
        if (i >= 0) {
            MoveFocus(d, i, FALSE, FALSE);
            ListNotify(d, LN_OPEN);
        }
        return 0;
    case WM_KEYDOWN:
        {
            BOOL bShift = GetKeyState(VK_SHIFT) < 0, bCtrl = GetKeyState(VK_CONTROL) < 0;
            int nPage = d->nMode == VM_DETAILS ? d->nVisRows : d->nRows * d->nVisCols;
            int nRow = d->nMode == VM_DETAILS ? 1 : d->nRows;

            i = d->nFocus;
            switch (wParam) {
            case VK_UP:
                MoveFocus(d, i < 0 ? 0 : i - 1, bShift, FALSE);
                return 0;
            case VK_DOWN:
                MoveFocus(d, i < 0 ? 0 : i + 1, bShift, FALSE);
                return 0;
            case VK_LEFT:
                if (d->nMode != VM_DETAILS)
                    MoveFocus(d, i < 0 ? 0 : i - nRow, bShift, FALSE);
                return 0;
            case VK_RIGHT:
                if (d->nMode != VM_DETAILS)
                    MoveFocus(d, i < 0 ? 0 : i + nRow, bShift, FALSE);
                return 0;
            case VK_HOME:
                MoveFocus(d, 0, bShift, FALSE);
                return 0;
            case VK_END:
                MoveFocus(d, d->nItems - 1, bShift, FALSE);
                return 0;
            case VK_PRIOR:
                MoveFocus(d, i - nPage, bShift, FALSE);
                return 0;
            case VK_NEXT:
                MoveFocus(d, i + nPage, bShift, FALSE);
                return 0;
            case VK_BACK:
                ListNotify(d, LN_UP);
                return 0;
            case VK_SPACE:
                if (bCtrl && i >= 0)
                    MoveFocus(d, i, FALSE, TRUE);
                return 0;
            case VK_F2:
                if (i >= 0 && d->lpItems[i].bKind != IK_DRIVE)
                    SendMessage(d->hDlg, WM_COMMAND, ID_RENAME, 0L);
                return 0;
            case VK_F5:
                SendMessage(d->hDlg, WM_COMMAND, ID_SEARCH, MAKELPARAM(0, 0xFFFF));
                return 0;
            }
        }
        break;
    case WM_CHAR:
        if (wParam > ' ')
            TypeAhead(d, (char)wParam);
        return 0;
    case WM_VSCROLL:
    case WM_HSCROLL:
        {
            int *pn = msg == WM_VSCROLL ? &d->nTop : &d->nLeft;
            int nPage = msg == WM_VSCROLL ? d->nVisRows : d->nVisCols;
            int nMin, nMax;

            GetScrollRange(hwnd, msg == WM_VSCROLL ? SB_VERT : SB_HORZ, &nMin, &nMax);
            switch (wParam) {
            case SB_LINEUP:
                (*pn)--;
                break;
            case SB_LINEDOWN:
                (*pn)++;
                break;
            case SB_PAGEUP:
                *pn -= nPage;
                break;
            case SB_PAGEDOWN:
                *pn += nPage;
                break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                *pn = LOWORD(lParam);
                break;
            case SB_TOP:
                *pn = 0;
                break;
            case SB_BOTTOM:
                *pn = nMax;
                break;
            }
            if (*pn < 0)
                *pn = 0;
            if (*pn > nMax)
                *pn = nMax;
            SetScrollPos(hwnd, msg == WM_VSCROLL ? SB_VERT : SB_HORZ, *pn, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* -------------------------------------------------------------------- */
/* Places bar (XP) and navigation pane (11)                             */
/* -------------------------------------------------------------------- */

static void AddPlace(LPFDLG d, LPCSTR lpText, LPCSTR lpPath, int nIcon, int nSmi, BOOL bHeader)
{
    PLACE FAR *p;

    if (d->nPlaces >= MAX_PLACES)
        return;
    p = &d->aPlace[d->nPlaces++];
    lstrcpyn(p->szText, lpText, sizeof(p->szText));
    lstrcpyn(p->szPath, lpPath, sizeof(p->szPath));
    p->bIcon = (BYTE)nIcon;
    p->bSmi = (BYTE)nSmi;
    p->bHeader = (BYTE)bHeader;
}

/* A folder for a place: [FileDialog] Key=, else the first that exists */
static BOOL PlaceDir(LPFDLG d, LPCSTR lpKey, LPCSTR lpTry1, LPCSTR lpTry2, LPSTR lpOut)
{
    WORD wAttr;
    LPCSTR aTry[3];
    int i;

    GetPrivateProfileString("FileDialog", lpKey, "", d->szWork2, sizeof(d->szWork2), s_szIni);
    aTry[0] = d->szWork2;
    aTry[1] = lpTry1;
    aTry[2] = lpTry2;
    for (i = 0; i < 3; i++) {
        if (!aTry[i] || !aTry[i][0])
            continue;
        if (LfnGetAttr(aTry[i], &wAttr) == 0 && (wAttr & _A_SUBDIR)) {
            if (!LfnShortPath(aTry[i], lpOut, SPATH))
                lstrcpyn(lpOut, aTry[i], SPATH);
            AnsiUpper(lpOut);
            return TRUE;
        }
    }
    return FALSE;
}

static void BuildPlaces(LPFDLG d)
{
    char szDir[SPATH], szKey[12], szValue[SPATH + 32];
    LPSTR lpBar;
    int i;
    BOOL b11 = d->nLook == NSDLG_11;

    d->nPlaces = 0;
    if (b11)
        AddPlace(d, "Quick access", "", 0, 0, TRUE);
    else
        AddPlace(d, "My Computer", "", IDI_DCOMPUTER, 0, FALSE);
    if (PlaceDir(d, "Documents", "C:\\My Documents", "C:\\DOCS", szDir))
        AddPlace(d, b11 ? "Documents" : "My Documents", szDir, IDI_DDOCS, 0, FALSE);
    GetWindowsDirectory(szDir, sizeof(szDir));
    AnsiUpper(szDir);
    AddPlace(d, "Windows", szDir, IDI_DFOLDER, 0, FALSE);
    /* places of one's own: PlaceN=Caption|Path */
    for (i = 1; i <= 6; i++) {
        wsprintf(szKey, "Place%d", i);
        GetPrivateProfileString("FileDialog", szKey, "", szValue, sizeof(szValue), s_szIni);
        lpBar = _fstrchr(szValue, '|');
        if (!lpBar)
            continue;
        *lpBar = '\0';
        if (!LfnShortPath(lpBar + 1, szDir, sizeof(szDir)))
            lstrcpyn(szDir, lpBar + 1, sizeof(szDir));
        AnsiUpper(szDir);
        AddPlace(d, szValue, szDir, IDI_DFOLDER, 0, FALSE);
    }
    if (b11) {
        AddPlace(d, "This PC", "", IDI_DCOMPUTER, 0, TRUE);
        for (i = 0; i < 26; i++)
            if (GetDriveType(i) != 0) {
                char szRoot[4];
                int idi = DriveIdi(i, FALSE);
                wsprintf(szRoot, "%c:\\", 'A' + i);
                wsprintf(szValue, "%s (%c:)", (LPSTR)DriveKind(idi), 'A' + i);
                AddPlace(d, szValue, szRoot, idi, 0, FALSE);
            }
    }
}

static int PlaceHeight(LPFDLG d, int i)
{
    if (d->nLook != NSDLG_11)
        return d->cyFont + 46;
    if (d->aPlace[i].bHeader)
        return d->cyFont + (i ? 16 : 10);
    return d->cyFont + 10;
}

static BOOL IsCurrentPlace(LPFDLG d, int i)
{
    if (d->aPlace[i].bHeader && d->aPlace[i].szPath[0] == '\0' && i == 0)
        return FALSE;           /* "Quick access" */
    return lstrcmpi(d->aPlace[i].szPath, d->szDir) == 0
           || (d->aPlace[i].bHeader && !d->szDir[0] && i > 0);
}

static void PlacesPaint(HWND hwnd, LPFDLG d)
{
    PAINTSTRUCT ps;
    RECT rc, rcItem;
    HDC hdc = BeginPaint(hwnd, &ps);
    HFONT hfOld = SelectObject(hdc, d->hFont);
    BOOL b11 = d->nLook == NSDLG_11;
    COLORREF clrBk = b11 ? GetSysColor(COLOR_WINDOW) : RGB(122, 140, 184);
    int i, y = b11 ? 4 : 2;

    GetClientRect(hwnd, &rc);
    FillSolid(hdc, &rc, clrBk);
    SetBkMode(hdc, TRANSPARENT);
    for (i = 0; i < d->nPlaces; i++) {
        PLACE FAR *p = &d->aPlace[i];
        BOOL bCur = IsCurrentPlace(d, i);
        int cy = PlaceHeight(d, i);

        SetRect(&rcItem, 2, y, rc.right - 2, y + cy);
        if (b11) {
            RECT rcText = rcItem;
            if (bCur && !p->bHeader) {
                RECT rcSel = rcItem;
                rcSel.left += 2;
                rcSel.right -= 2;
                RoundBox(hdc, &rcSel, 6, RGB(229, 238, 250), RGB(204, 222, 245));
            }
            if (p->bHeader) {
                rcText.left += 6;
                SetTextColor(hdc, RGB(0, 0, 0));
                DrawText(hdc, p->szText, -1, &rcText, DT_LEFT | DT_BOTTOM | DT_SINGLELINE
                         | DT_NOPREFIX);
            } else {
                if (p->bIcon)
                    DrawIconSmall(d, hdc, rcItem.left + 14, rcItem.top + (cy - 16) / 2,
                                  LoadIcon(s_hInst, MAKEINTRESOURCE(p->bIcon)),
                                  bCur ? RGB(229, 238, 250) : clrBk);
                rcText.left += 36;
                SetTextColor(hdc, RGB(32, 32, 32));
                DrawText(hdc, p->szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE
                         | DT_NOPREFIX);
            }
        } else {
            RECT rcText;
            if (bCur) {
                RECT rcSel = rcItem;
                InflateRect(&rcSel, -1, -1);
                FillSolid(hdc, &rcSel, RGB(160, 176, 216));
                Frame(hdc, &rcSel, RGB(80, 96, 140), RGB(220, 228, 246));
            }
            DrawIcon(hdc, (rcItem.left + rcItem.right - 32) / 2, rcItem.top + 4,
                     LoadIcon(s_hInst, MAKEINTRESOURCE(p->bIcon)));
            SetRect(&rcText, rcItem.left, rcItem.top + 38, rcItem.right, rcItem.bottom);
            SetTextColor(hdc, RGB(255, 255, 255));
            DrawText(hdc, p->szText, -1, &rcText, DT_CENTER | DT_TOP | DT_SINGLELINE
                     | DT_NOPREFIX);
        }
        y += cy;
    }
    SelectObject(hdc, hfOld);
    EndPaint(hwnd, &ps);
}

static void Navigate(LPFDLG d, LPCSTR lpDir, BOOL bHistory);

LRESULT CALLBACK __export NsDlgPlacesProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LPFDLG d = (LPFDLG)GetWindowLong(hwnd, 0);
    int i, y;

    if (!d)
        return DefWindowProc(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_PAINT:
        PlacesPaint(hwnd, d);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        y = d->nLook == NSDLG_11 ? 4 : 2;
        for (i = 0; i < d->nPlaces; i++) {
            int cy = PlaceHeight(d, i);
            if ((int)HIWORD(lParam) >= y && (int)HIWORD(lParam) < y + cy) {
                /* "Quick access" is only a title; "This PC" opens the drives */
                if (!(d->aPlace[i].bHeader && i == 0))
                    Navigate(d, d->aPlace[i].szPath, TRUE);
                break;
            }
            y += cy;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* -------------------------------------------------------------------- */
/* Address bar (11)                                                     */
/* -------------------------------------------------------------------- */

static void BuildSegments(LPFDLG d)
{
    char szPart[SPATH];
    LPSTR p;
    int n;

    d->nSegs = 0;
    lstrcpy(d->aSegPath[d->nSegs++], "");
    if (!d->szDir[0])
        return;
    lstrcpyn(szPart, d->szDir, 4);              /* "C:\" */
    lstrcpy(d->aSegPath[d->nSegs++], szPart);
    for (p = d->szDir + 3; *p && d->nSegs < 12; ) {
        while (*p && *p != '\\')
            p++;
        n = (int)(p - d->szDir);
        lstrcpyn(szPart, d->szDir, n + 1);
        lstrcpy(d->aSegPath[d->nSegs++], szPart);
        if (*p)
            p++;
    }
}

static void AddressPaint(HWND hwnd, LPFDLG d)
{
    PAINTSTRUCT ps;
    RECT rc, rcSeg;
    HDC hdc = BeginPaint(hwnd, &ps);
    HFONT hfOld = SelectObject(hdc, d->hFont);
    char szText[LFN_MAXNAME];
    int i, x;

    GetClientRect(hwnd, &rc);
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    RoundBox(hdc, &rc, 8, GetSysColor(COLOR_WINDOW), RGB(204, 204, 204));
    SetBkMode(hdc, TRANSPARENT);
    BuildSegments(d);
    x = 8;
    DrawIconSmall(d, hdc, x, (rc.bottom - 16) / 2,
                  LoadIcon(s_hInst, MAKEINTRESOURCE(d->szDir[0] ? IDI_DFOLDER : IDI_DCOMPUTER)),
                  GetSysColor(COLOR_WINDOW));
    x += 22;
    for (i = 0; i < d->nSegs; i++) {
        if (i == 0)
            lstrcpy(szText, "This PC");
        else if (i == 1) {
            int idi = DriveIdi((d->aSegPath[1][0] & ~0x20) - 'A', FALSE);
            wsprintf(szText, "%s (%c:)", (LPSTR)DriveKind(idi), d->aSegPath[1][0]);
        } else
            LfnLongName(d->aSegPath[i], szText, sizeof(szText));
        SetTextColor(hdc, RGB(32, 32, 32));
        SetRect(&rcSeg, x, 0, x + TextWidth(hdc, szText) + 4, rc.bottom);
        if (rcSeg.right > rc.right - 8)
            rcSeg.right = rc.right - 8;
        DrawText(hdc, szText, -1, &rcSeg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        d->aSeg[i] = rcSeg;
        x = rcSeg.right + 2;
        if (i + 1 < d->nSegs) {
            /* the chevron between the parts */
            HPEN hpen = CreatePen(PS_SOLID, 1, RGB(96, 96, 96)), hpenOld;
            int yc = rc.bottom / 2;
            hpenOld = SelectObject(hdc, hpen);
            MoveTo(hdc, x + 3, yc - 3);
            LineTo(hdc, x + 6, yc);
            LineTo(hdc, x + 2, yc + 4);
            SelectObject(hdc, hpenOld);
            DeleteObject(hpen);
            x += 14;
        }
        if (x > rc.right - 8)
            break;
    }
    SelectObject(hdc, hfOld);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK __export NsDlgAddressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LPFDLG d = (LPFDLG)GetWindowLong(hwnd, 0);
    POINT pt;
    int i;

    if (!d)
        return DefWindowProc(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_PAINT:
        AddressPaint(hwnd, d);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        for (i = 0; i < d->nSegs; i++)
            if (PtInRect(&d->aSeg[i], pt)) {
                Navigate(d, d->aSegPath[i], TRUE);
                break;
            }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* -------------------------------------------------------------------- */
/* Toolbar buttons                                                      */
/* -------------------------------------------------------------------- */

static void DrawArrow(HDC hdc, int cx, int cy, BOOL bUp, COLORREF clr, int s)
{
    POINT pt[7];
    HBRUSH hbr = CreateSolidBrush(clr), hbrOld;
    HPEN hpen = CreatePen(PS_SOLID, 1, clr), hpenOld;

    if (bUp) {
        pt[0].x = cx;         pt[0].y = cy - s;
        pt[1].x = cx + s;     pt[1].y = cy;
        pt[2].x = cx + s / 2; pt[2].y = cy;
        pt[3].x = cx + s / 2; pt[3].y = cy + s;
        pt[4].x = cx - s / 2; pt[4].y = cy + s;
        pt[5].x = cx - s / 2; pt[5].y = cy;
        pt[6].x = cx - s;     pt[6].y = cy;
    } else {                            /* left */
        pt[0].x = cx - s;     pt[0].y = cy;
        pt[1].x = cx;         pt[1].y = cy - s;
        pt[2].x = cx;         pt[2].y = cy - s / 2;
        pt[3].x = cx + s;     pt[3].y = cy - s / 2;
        pt[4].x = cx + s;     pt[4].y = cy + s / 2;
        pt[5].x = cx;         pt[5].y = cy + s / 2;
        pt[6].x = cx;         pt[6].y = cy + s;
    }
    hbrOld = SelectObject(hdc, hbr);
    hpenOld = SelectObject(hdc, hpen);
    Polygon(hdc, pt, 7);
    SelectObject(hdc, hbrOld);
    SelectObject(hdc, hpenOld);
    DeleteObject(hbr);
    DeleteObject(hpen);
}

static void DrawToolButton(LPFDLG d, DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem;
    BOOL bDown = (lpdis->itemState & ODS_SELECTED) != 0;
    BOOL bOff = (lpdis->itemState & ODS_DISABLED) != 0;
    BOOL bChecked = (lpdis->CtlID == ID_VLIST && d->nMode == VM_LIST)
                    || (lpdis->CtlID == ID_VDETAILS && d->nMode == VM_DETAILS);
    COLORREF clrFace = GetSysColor(COLOR_BTNFACE);
    int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2, o = bDown ? 1 : 0;
    char szText[24];

    FillSolid(hdc, &rc, clrFace);
    if (d->nLook == NSDLG_95) {
        if (bDown || bChecked) {
            Frame(hdc, &rc, RGB(0, 0, 0), GetSysColor(COLOR_BTNHIGHLIGHT));
            InflateRect(&rc, -1, -1);
            Frame(hdc, &rc, GetSysColor(COLOR_BTNSHADOW), clrFace);
            if (bChecked && !bDown) {
                RECT r = rc;
                InflateRect(&r, -1, -1);
                FillSolid(hdc, &r, Blend(clrFace, GetSysColor(COLOR_BTNHIGHLIGHT), 128));
            }
            o = 1;
        } else {
            Frame(hdc, &rc, GetSysColor(COLOR_BTNHIGHLIGHT), RGB(0, 0, 0));
            InflateRect(&rc, -1, -1);
            Frame(hdc, &rc, clrFace, GetSysColor(COLOR_BTNSHADOW));
        }
    } else if (bDown || bChecked) {
        RECT r = rc;
        InflateRect(&r, -1, -1);
        RoundBox(hdc, &r, 6, d->nLook == NSDLG_11 ? RGB(229, 238, 250)
                                                   : Blend(clrFace, RGB(255, 255, 255), 128),
                 d->nLook == NSDLG_11 ? RGB(204, 222, 245) : RGB(132, 152, 196));
    }
    switch (lpdis->CtlID) {
    case ID_BACK:
        if (d->nLook == NSDLG_XP) {
            /* the green round Back button */
            RECT r;
            SetRect(&r, cx - 10 + o, cy - 10 + o, cx + 10 + o, cy + 10 + o);
            RoundBox(hdc, &r, 20, bOff ? RGB(180, 180, 180) : RGB(58, 164, 60),
                     bOff ? RGB(150, 150, 150) : RGB(40, 110, 40));
            DrawArrow(hdc, cx + o, cy + o, FALSE, RGB(255, 255, 255), 6);
        } else
            DrawArrow(hdc, cx + o, cy + o, FALSE, bOff ? GetSysColor(COLOR_GRAYTEXT)
                      : (d->nLook == NSDLG_11 ? RGB(64, 64, 64) : GetSysColor(COLOR_BTNTEXT)), 6);
        return;
    case ID_UP:
        if (d->nLook == NSDLG_11) {
            DrawArrow(hdc, cx + o, cy + o, TRUE, bOff ? GetSysColor(COLOR_GRAYTEXT)
                      : RGB(64, 64, 64), 6);
            return;
        }
        DrawSmall(hdc, cx - 8 + o, cy - 8 + o, SMI_TB_UP);
        return;
    case ID_NEWDIR:
        if (d->nLook == NSDLG_11) {
            GetWindowText(lpdis->hwndItem, szText, sizeof(szText));
            DrawSmall(hdc, rc.left + 6 + o, cy - 8 + o, SMI_TB_NEWFOLDER);
            rc.left += 26 + o;
            rc.top += o;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(32, 32, 32));
            DrawText(hdc, szText, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return;
        }
        DrawSmall(hdc, cx - 8 + o, cy - 8 + o, SMI_TB_NEWFOLDER);
        return;
    case ID_VLIST:
        DrawSmall(hdc, cx - 8 + o, cy - 8 + o, SMI_TB_LIST);
        return;
    case ID_VDETAILS:
    case ID_VIEWS:
        DrawSmall(hdc, cx - 8 + o, cy - 8 + o, d->nMode == VM_DETAILS && lpdis->CtlID == ID_VIEWS
                  ? SMI_TB_LIST : SMI_TB_DETAILS);
        return;
    case ID_SEARCHICON:
        {
            /* a magnifying glass */
            HPEN hpen = CreatePen(PS_SOLID, 2, RGB(96, 96, 96)), hpenOld;
            HBRUSH hbrOld = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            hpenOld = SelectObject(hdc, hpen);
            Ellipse(hdc, cx - 6, cy - 6, cx + 3, cy + 3);
            MoveTo(hdc, cx + 2, cy + 2);
            LineTo(hdc, cx + 6, cy + 6);
            SelectObject(hdc, hpenOld);
            SelectObject(hdc, hbrOld);
            DeleteObject(hpen);
        }
        return;
    }
}

/* -------------------------------------------------------------------- */
/* Look in (95, XP)                                                     */
/* -------------------------------------------------------------------- */

static void AddLookIn(LPFDLG d, LPCSTR lpText, LPCSTR lpPath, int nIndent, int nIcon)
{
    int n;

    if (d->nLookIn >= MAX_LOOKIN)
        return;
    lstrcpyn(d->aLookIn[d->nLookIn], lpPath, SPATH);
    n = (int)SendMessage(d->hLookIn, CB_ADDSTRING, 0, (LPARAM)lpText);
    SendMessage(d->hLookIn, CB_SETITEMDATA, n, MAKELPARAM(nIndent, nIcon));
    d->nLookIn++;
}

static void FillLookIn(LPFDLG d)
{
    char szText[LFN_MAXNAME], szPart[SPATH];
    int i, nSel = 0, nCurDrive = d->szDir[0] ? (d->szDir[0] & ~0x20) - 'A' : -1;
    LPSTR p;

    if (!d->hLookIn)
        return;
    SendMessage(d->hLookIn, WM_SETREDRAW, FALSE, 0L);
    SendMessage(d->hLookIn, CB_RESETCONTENT, 0, 0L);
    d->nLookIn = 0;
    AddLookIn(d, "My Computer", "", 0, IDI_DCOMPUTER);
    for (i = 0; i < 26; i++) {
        int idi;
        if (GetDriveType(i) == 0)
            continue;
        idi = DriveIdi(i, FALSE);
        wsprintf(szText, "%s (%c:)", (LPSTR)DriveKind(idi), 'A' + i);
        wsprintf(szPart, "%c:\\", 'A' + i);
        if (i == nCurDrive && lstrlen(d->szDir) <= 3)
            nSel = d->nLookIn;
        AddLookIn(d, szText, szPart, 1, idi);
        if (i == nCurDrive && lstrlen(d->szDir) > 3) {
            int nIndent = 2;
            for (p = d->szDir + 3; *p; nIndent++) {
                while (*p && *p != '\\')
                    p++;
                lstrcpyn(szPart, d->szDir, (int)(p - d->szDir) + 1);
                LfnLongName(szPart, szText, sizeof(szText));
                if (!*p)
                    nSel = d->nLookIn;
                AddLookIn(d, szText, szPart, nIndent, *p ? IDI_DFOLDER : -1);
                if (*p)
                    p++;
            }
        }
    }
    SendMessage(d->hLookIn, CB_SETCURSEL, nSel, 0L);
    SendMessage(d->hLookIn, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(d->hLookIn, NULL, TRUE);
}

static void DrawLookIn(LPFDLG d, DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText;
    char szText[LFN_MAXNAME];
    BOOL bSel = (lpdis->itemState & ODS_SELECTED) != 0;
    int nIndent, nIcon, x;

    if ((int)lpdis->itemID < 0)
        return;
    nIndent = LOWORD(lpdis->itemData);
    nIcon = (int)HIWORD(lpdis->itemData);
    if (lpdis->itemState & ODS_COMBOBOXEDIT)
        nIndent = 0;
    FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
    SendMessage(d->hLookIn, CB_GETLBTEXT, lpdis->itemID, (LPARAM)(LPSTR)szText);
    x = rc.left + 2 + nIndent * 10;
    if (nIcon == -1)
        DrawSmall(hdc, x, rc.top + (rc.bottom - rc.top - 16) / 2, SMI_FOLDEROPEN);
    else if (nIcon == IDI_DFOLDER)
        DrawSmall(hdc, x, rc.top + (rc.bottom - rc.top - 16) / 2, SMI_FOLDER);
    else
        DrawIconSmall(d, hdc, x, rc.top + (rc.bottom - rc.top - 16) / 2,
                      LoadIcon(s_hInst, MAKEINTRESOURCE(nIcon)), GetSysColor(COLOR_WINDOW));
    SetRect(&rcText, x + 19, rc.top, x + 23 + TextWidth(hdc, szText), rc.bottom);
    if (bSel)
        FillSolid(hdc, &rcText, GetSysColor(COLOR_HIGHLIGHT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    rcText.left += 2;
    DrawText(hdc, szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (lpdis->itemState & ODS_FOCUS) {
        rcText.left -= 2;
        DrawFocusRect(hdc, &rcText);
    }
}

/* -------------------------------------------------------------------- */
/* Navigation                                                           */
/* -------------------------------------------------------------------- */

static void UpdateButtons(LPFDLG d)
{
    HWND h;

    if ((h = GetDlgItem(d->hDlg, ID_UP)) != NULL)
        EnableWindow(h, d->szDir[0] != '\0');
    if ((h = GetDlgItem(d->hDlg, ID_BACK)) != NULL)
        EnableWindow(h, d->nHistory > 0);
    if ((h = GetDlgItem(d->hDlg, ID_NEWDIR)) != NULL)
        EnableWindow(h, d->szDir[0] != '\0');
    if ((h = GetDlgItem(d->hDlg, ID_VLIST)) != NULL)
        InvalidateRect(h, NULL, FALSE);
    if ((h = GetDlgItem(d->hDlg, ID_VDETAILS)) != NULL)
        InvalidateRect(h, NULL, FALSE);
    if ((h = GetDlgItem(d->hDlg, ID_VIEWS)) != NULL)
        InvalidateRect(h, NULL, FALSE);
}

static void Reload(LPFDLG d, LPCSTR lpSelect)
{
    int i;

    ReadItems(d);
    d->nFocus = d->nAnchor = -1;
    d->nTop = d->nLeft = 0;
    if (lpSelect)
        for (i = 0; i < d->nItems; i++)
            if (lstrcmpi(d->lpItems[i].szShort, lpSelect) == 0) {
                d->nFocus = d->nAnchor = i;
                d->lpItems[i].bSel = TRUE;
                break;
            }
    if (d->nFocus < 0 && d->nItems > 0)
        d->nFocus = 0;
    ListUpdate(d);
    EnsureVisible(d, d->nFocus);
}

static void Navigate(LPFDLG d, LPCSTR lpDir, BOOL bHistory)
{
    char szOld[SPATH];
    HCURSOR hcur;

    if (lpDir[0]) {
        int nDrive = (lpDir[0] & ~0x20) - 'A';
        if (!DriveReady(nDrive)) {
            wsprintf(d->szWork, "%c:\\ is not accessible.\n\nThe device is not ready.",
                     'A' + nDrive);
            MessageBox(d->hDlg, d->szWork, d->bSave ? "Save As" : "Open",
                       MB_OK | MB_ICONEXCLAMATION);
            return;
        }
    }
    lstrcpy(szOld, d->szDir);
    if (bHistory && lstrcmpi(szOld, lpDir) != 0) {
        if (d->nHistory == MAX_HISTORY) {
            _fmemmove(d->aHistory[0], d->aHistory[1], (MAX_HISTORY - 1) * SPATH);
            d->nHistory--;
        }
        lstrcpy(d->aHistory[d->nHistory++], szOld);
    }
    lstrcpyn(d->szDir, lpDir, sizeof(d->szDir));
    AnsiUpper(d->szDir);
    if (d->hSearch && d->szSearch[0]) {
        d->szSearch[0] = '\0';
        SetWindowText(d->hSearch, "");
    }
    hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    /* going up selects the folder we came from */
    {
        char szChild[SPATH];
        LPSTR lpSel = NULL;
        lstrcpy(szChild, szOld);
        if (szChild[0] && ParentOf(szChild) && lstrcmpi(szChild, d->szDir) == 0)
            lpSel = FileNameOf(szOld);
        else if (!d->szDir[0] && szOld[0]) {
            szOld[2] = '\0';
            lpSel = szOld;
        }
        Reload(d, lpSel);
    }
    SetCursor(hcur);
    FillLookIn(d);
    if (d->hPlaces)
        InvalidateRect(d->hPlaces, NULL, FALSE);
    if (d->hAddress)
        InvalidateRect(d->hAddress, NULL, FALSE);
    UpdateButtons(d);
}

static void GoUp(LPFDLG d)
{
    char szDir[SPATH];

    if (!d->szDir[0])
        return;
    lstrcpy(szDir, d->szDir);
    if (!ParentOf(szDir))
        szDir[0] = '\0';
    Navigate(d, szDir, TRUE);
}

static void GoBack(LPFDLG d)
{
    char szDir[SPATH];

    if (d->nHistory == 0)
        return;
    lstrcpy(szDir, d->aHistory[--d->nHistory]);
    Navigate(d, szDir, FALSE);
}

/* -------------------------------------------------------------------- */
/* New folder and renaming                                              */
/* -------------------------------------------------------------------- */

static FARPROC s_lpfnOldEdit;

LRESULT CALLBACK __export NsDlgRenameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LPFDLG d = ListState(GetParent(hwnd));
    if (msg == WM_GETDLGCODE)
        return DLGC_WANTALLKEYS;
    if (msg == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        if (d)
            EndRename(d, wParam == VK_RETURN);
        return 0;
    }
    if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
        return 0;
    if (msg == WM_KILLFOCUS && d && d->hRename == hwnd)
        PostMessage(d->hDlg, WM_COMMAND, ID_RENAME, MAKELPARAM(0, 1));
    return CallWindowProc((WNDPROC)s_lpfnOldEdit, hwnd, msg, wParam, lParam);
}

static void BeginRename(LPFDLG d, int i)
{
    RECT rc;
    BOOL bLong = LfnDrive(d->szDir);

    if (i < 0 || i >= d->nItems || d->hRename || d->lpItems[i].bKind == IK_DRIVE)
        return;
    EnsureVisible(d, i);
    ItemRect(d, i, &rc);
    rc.left += 20;
    if (d->nMode == VM_DETAILS) {
        int a[COLS_DETAILS];
        DetailWidths(d, rc.right, a);
        rc.right = a[0];
    }
    d->nRenaming = i;
    d->hRename = CreateWindow("EDIT", LongOf(d, &d->lpItems[i]),
                              WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | (bLong ? 0 : ES_UPPERCASE),
                              rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                              d->hList, (HMENU)ID_RENAME, s_hInst, NULL);
    if (!d->hRename) {
        d->nRenaming = -1;
        return;
    }
    s_lpfnOldEdit = (FARPROC)SetWindowLong(d->hRename, GWL_WNDPROC, (LONG)(FARPROC)NsDlgRenameProc);
    SendMessage(d->hRename, WM_SETFONT, (WPARAM)d->hFont, 0L);
    SendMessage(d->hRename, EM_LIMITTEXT, bLong ? LFN_MAXNAME - 1 : 12, 0L);
    SendMessage(d->hRename, EM_SETSEL, 0, MAKELPARAM(0, -1));
    ShowWindow(d->hRename, SW_SHOW);
    SetFocus(d->hRename);
    InvalidateRect(d->hList, NULL, FALSE);
}

static void EndRename(LPFDLG d, BOOL bCommit)
{
    HWND hwnd = d->hRename;
    int i = d->nRenaming;
    char szNew[LFN_MAXNAME], szShort[14];

    if (!hwnd)
        return;
    d->hRename = NULL;
    d->nRenaming = -1;
    GetWindowText(hwnd, szNew, sizeof(szNew));
    SetFocus(d->hList);
    DestroyWindow(hwnd);
    if (bCommit && i >= 0 && i < d->nItems && szNew[0]
        && lstrcmp(szNew, LongOf(d, &d->lpItems[i])) != 0) {
        BOOL bLong = LfnDrive(d->szDir);
        if (!LfnValidName(szNew, bLong)) {
            MessageBox(d->hDlg, bLong ? "A file name cannot contain any of these characters:\n"
                                        "\\ / : * ? \" < > |"
                                      : "This is not a valid MS-DOS name (8 characters, a "
                                        "full stop and 3 more).",
                       "Rename", MB_OK | MB_ICONEXCLAMATION);
        } else {
            lstrcpy(d->szWork, d->szDir);
            CatPath(d->szWork, d->lpItems[i].szShort, sizeof(d->szWork));
            lstrcpy(d->szWork2, d->szDir);
            CatPath(d->szWork2, szNew, sizeof(d->szWork2));
            if (LfnRename(d->szWork, d->szWork2) != 0)
                MessageBox(d->hDlg, "The item could not be renamed.", "Rename",
                           MB_OK | MB_ICONEXCLAMATION);
            else if (LfnShortOf(d->szDir, szNew, szShort) == 0) {
                Reload(d, szShort);
                return;
            }
        }
    }
    InvalidateRect(d->hList, NULL, FALSE);
}

static void NewFolder(LPFDLG d)
{
    char szName[24], szShort[14];
    BOOL bLong = LfnDrive(d->szDir);
    int i;

    if (!d->szDir[0])
        return;
    for (i = 0; i < 50; i++) {
        if (bLong)
            wsprintf(szName, i ? "New Folder (%d)" : "New Folder", i + 1);
        else
            wsprintf(szName, i ? "NEWFOLD%d" : "NEWFOLDR", i);
        if (LfnShortOf(d->szDir, szName, szShort) != 0)
            break;
    }
    lstrcpy(d->szWork, d->szDir);
    CatPath(d->szWork, szName, sizeof(d->szWork));
    if (LfnMkDir(d->szWork) != 0 || LfnShortOf(d->szDir, szName, szShort) != 0) {
        MessageBox(d->hDlg, "Unable to create the folder.", d->bSave ? "Save As" : "Open",
                   MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    Reload(d, szShort);
    for (i = 0; i < d->nItems; i++)
        if (lstrcmpi(d->lpItems[i].szShort, szShort) == 0)
            BeginRename(d, i);
}

/* -------------------------------------------------------------------- */
/* Filters                                                              */
/* -------------------------------------------------------------------- */

/* The pattern string of filter n (1-based) of lpstrFilter */
static LPCSTR FilterPattern(LPCSTR lpFilter, int n)
{
    int i;

    if (!lpFilter)
        return NULL;
    for (i = 1; *lpFilter; i++) {
        LPCSTR lpPat = lpFilter + lstrlen(lpFilter) + 1;
        if (!*lpPat)
            return NULL;
        if (i == n)
            return lpPat;
        lpFilter = lpPat + lstrlen(lpPat) + 1;
    }
    return NULL;
}

/* The list of types; the item data is the filter index (1-based in
   lpstrFilter, 0 the custom filter, -1 none given) */
static void FillTypes(LPFDLG d)
{
    LPOPENFILENAME ofn = d->ofn;
    LPCSTR lp = ofn->lpstrFilter;
    int n = 0, nIdx = 1, nSel = 0, i;

    if (ofn->lpstrCustomFilter && ofn->lpstrCustomFilter[0]
        && ofn->lpstrCustomFilter[lstrlen(ofn->lpstrCustomFilter) + 1]) {
        i = (int)SendMessage(d->hTypes, CB_ADDSTRING, 0, (LPARAM)ofn->lpstrCustomFilter);
        SendMessage(d->hTypes, CB_SETITEMDATA, i, 0L);
        n++;
    }
    while (lp && *lp) {
        LPCSTR lpPat = lp + lstrlen(lp) + 1;
        i = (int)SendMessage(d->hTypes, CB_ADDSTRING, 0, (LPARAM)lp);
        SendMessage(d->hTypes, CB_SETITEMDATA, i, (LPARAM)(long)nIdx);
        nIdx++;
        n++;
        if (!*lpPat)
            break;
        lp = lpPat + lstrlen(lpPat) + 1;
    }
    if (n == 0) {
        SendMessage(d->hTypes, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"All Files (*.*)");
        SendMessage(d->hTypes, CB_SETITEMDATA, 0, (LPARAM)-1L);
        n = 1;
    }
    for (i = 0; i < n; i++)
        if ((int)SendMessage(d->hTypes, CB_GETITEMDATA, i, 0L) == (int)ofn->nFilterIndex)
            nSel = i;
    SendMessage(d->hTypes, CB_SETCURSEL, nSel, 0L);
}

static void ApplyType(LPFDLG d)
{
    int nSel = (int)SendMessage(d->hTypes, CB_GETCURSEL, 0, 0L);
    int nIdx = (int)SendMessage(d->hTypes, CB_GETITEMDATA, nSel, 0L);
    LPCSTR lpPat = NULL;

    if (nIdx == 0 && d->ofn->lpstrCustomFilter)
        lpPat = d->ofn->lpstrCustomFilter + lstrlen(d->ofn->lpstrCustomFilter) + 1;
    else if (nIdx > 0)
        lpPat = FilterPattern(d->ofn->lpstrFilter, nIdx);
    lstrcpyn(d->szFilter, lpPat && *lpPat ? lpPat : (LPCSTR)"*.*", sizeof(d->szFilter));
}

/* -------------------------------------------------------------------- */
/* The result                                                           */
/* -------------------------------------------------------------------- */

static void ShowError(LPFDLG d, LPCSTR lpFormat, LPCSTR lpName)
{
    wsprintf(d->szWork, lpFormat, lpName);
    MessageBox(d->hDlg, d->szWork, d->ofn->lpstrTitle ? d->ofn->lpstrTitle
               : (d->bSave ? (LPCSTR)"Save As" : (LPCSTR)"Open"), MB_OK | MB_ICONEXCLAMATION);
}

/* lpIn (as typed) as a full path, "." and ".." resolved, case kept */
static void FullPath(LPFDLG d, LPCSTR lpIn, LPSTR lpOut, int cbOut)
{
    char szLong[LFN_MAXPATH];
    LPSTR p, q;

    if (lpIn[0] && lpIn[1] == ':') {
        lstrcpyn(lpOut, lpIn, cbOut);
        if (lpIn[2] != '\\') {              /* "A:NAME" */
            wsprintf(lpOut, "%c:\\", lpIn[0]);
            CatPath(lpOut, lpIn + 2, cbOut);
        }
    } else if (lpIn[0] == '\\') {
        if (d->szDir[0])
            wsprintf(lpOut, "%c:", d->szDir[0]);
        else
            lstrcpy(lpOut, "C:");
        CatPath(lpOut, lpIn + 1, cbOut);
        if (lstrlen(lpOut) == 2)
            lstrcat(lpOut, "\\");
    } else {
        /* relative to the folder shown (in its long form) */
        LfnDisplayPath(d->szDir, szLong, sizeof(szLong));
        lstrcpyn(lpOut, szLong, cbOut);
        CatPath(lpOut, lpIn, cbOut);
    }
    /* "." and ".." */
    for (p = lpOut + 3; *p; ) {
        q = p;
        while (*q && *q != '\\')
            q++;
        if (q - p == 1 && p[0] == '.') {
            lstrcpy(p, *q ? q + 1 : q);
            if (!*p && p > lpOut + 3)
                p[-1] = '\0';
            continue;
        }
        if (q - p == 2 && p[0] == '.' && p[1] == '.') {
            LPSTR r = p - 1;            /* the backslash before */
            if (r > lpOut + 2) {
                r--;
                while (r > lpOut + 2 && *r != '\\')
                    r--;
            }
            lstrcpy(r + 1, *q ? q + 1 : q);
            p = r + 1;
            if (!*p && p > lpOut + 3)
                p[-1] = '\0';
            continue;
        }
        p = *q ? q + 1 : q;
    }
    if (lstrlen(lpOut) > 3 && lpOut[lstrlen(lpOut) - 1] == '\\')
        lpOut[lstrlen(lpOut) - 1] = '\0';
}

/* The 8.3 path of an existing folder; FALSE if there is none */
static BOOL ShortDir(LPCSTR lpLong, LPSTR lpShort)
{
    WORD wAttr;

    if (lstrlen(lpLong) == 3 && lpLong[1] == ':') {
        lstrcpy(lpShort, lpLong);
        AnsiUpper(lpShort);
        return GetDriveType((lpShort[0]) - 'A') != 0;
    }
    if (LfnGetAttr(lpLong, &wAttr) != 0 || !(wAttr & _A_SUBDIR))
        return FALSE;
    if (!LfnShortPath(lpLong, lpShort, SPATH))
        lstrcpyn(lpShort, lpLong, SPATH);
    AnsiUpper(lpShort);
    return TRUE;
}

static void SetCurrentDir(LPCSTR lpDir)
{
    union REGS r;
    struct SREGS s;
    char sz[SPATH];

    lstrcpyn(sz, lpDir, sizeof(sz));
    r.h.ah = 0x0E;                      /* select the drive */
    r.h.dl = (BYTE)((sz[0] & ~0x20) - 'A');
    intdos(&r, &r);
    segread(&s);
    r.h.ah = 0x3B;                      /* change the directory */
    s.ds = FP_SEG((LPSTR)sz);
    r.x.dx = FP_OFF((LPSTR)sz);
    intdosx(&r, &r, &s);
}

static BOOL AskYesNo(LPFDLG d, LPCSTR lpFormat, LPCSTR lpName)
{
    wsprintf(d->szWork, lpFormat, lpName);
    return MessageBox(d->hDlg, d->szWork, d->ofn->lpstrTitle ? d->ofn->lpstrTitle
                      : (d->bSave ? (LPCSTR)"Save As" : (LPCSTR)"Open"),
                      MB_YESNO | MB_ICONEXCLAMATION) == IDYES;
}

/* Puts the result into the OPENFILENAME and ends the dialog */
static BOOL Finish(LPFDLG d, LPCSTR lpDir, LPCSTR lpNames, int nNames)
{
    LPOPENFILENAME ofn = d->ofn;
    char szPath[SPATH * 2];
    LPSTR lpExt;
    int n;

    if (nNames == 1) {
        lstrcpy(szPath, lpDir);
        CatPath(szPath, lpNames, sizeof(szPath));
        AnsiUpper(szPath);
        n = lstrlen(szPath);
        if (ofn->lpstrFile && ofn->nMaxFile) {
            if ((DWORD)n + 1 > ofn->nMaxFile) {
                if (ofn->nMaxFile >= 2)
                    *(WORD FAR *)ofn->lpstrFile = (WORD)(n + 1);
                ShowError(d, "The name '%s' is too long for this program.", szPath);
                return FALSE;
            }
            lstrcpy(ofn->lpstrFile, szPath);
        }
        ofn->nFileOffset = (UINT)(FileNameOf(szPath) - (LPSTR)szPath);
        lpExt = ExtOf(szPath);
        ofn->nFileExtension = (UINT)(lpExt - (LPSTR)szPath);
        if (ofn->lpstrFileTitle && ofn->nMaxFileTitle)
            lstrcpyn(ofn->lpstrFileTitle, FileNameOf(szPath), (int)ofn->nMaxFileTitle);
        if (ofn->lpstrDefExt && *lpExt && lstrcmpi(lpExt, ofn->lpstrDefExt) != 0)
            ofn->Flags |= OFN_EXTENSIONDIFFERENT;
        else
            ofn->Flags &= ~OFN_EXTENSIONDIFFERENT;
    } else {
        /* Windows 3.1's multiple selection: "C:\DIR NAME1 NAME2" */
        LPCSTR lp;
        DWORD cb = lstrlen(lpDir) + 1;
        for (lp = lpNames, n = 0; n < nNames; n++, lp += lstrlen(lp) + 1)
            cb += lstrlen(lp) + 1;
        if (!ofn->lpstrFile || cb > ofn->nMaxFile) {
            if (ofn->lpstrFile && ofn->nMaxFile >= 2)
                *(WORD FAR *)ofn->lpstrFile = (WORD)cb;
            ShowError(d, "Too many files are selected%s.", " for this program");
            return FALSE;
        }
        lstrcpy(ofn->lpstrFile, lpDir);
        AnsiUpper(ofn->lpstrFile);
        ofn->nFileOffset = lstrlen(ofn->lpstrFile) + 1;
        for (lp = lpNames, n = 0; n < nNames; n++, lp += lstrlen(lp) + 1) {
            lstrcat(ofn->lpstrFile, " ");
            lstrcat(ofn->lpstrFile, lp);
        }
        AnsiUpper(ofn->lpstrFile);
        ofn->nFileExtension = 0;
        if (ofn->lpstrFileTitle && ofn->nMaxFileTitle)
            ofn->lpstrFileTitle[0] = '\0';
    }
    if (!(ofn->Flags & OFN_HIDEREADONLY) && IsDlgButtonChecked(d->hDlg, ID_READONLY))
        ofn->Flags |= OFN_READONLY;
    else
        ofn->Flags &= ~OFN_READONLY;
    {
        int nSel = (int)SendMessage(d->hTypes, CB_GETCURSEL, 0, 0L);
        int nIdx = (int)SendMessage(d->hTypes, CB_GETITEMDATA, nSel, 0L);
        if (nIdx >= 0)
            ofn->nFilterIndex = nIdx;
    }
    if (!(ofn->Flags & OFN_NOCHANGEDIR))
        SetCurrentDir(lpDir);
    {
        char szView[4];
        wsprintf(szView, "%d", d->nMode);
        WritePrivateProfileString("FileDialog", d->nLook == NSDLG_11 ? "View11"
                                  : d->nLook == NSDLG_XP ? "ViewXP" : "View95", szView, s_szIni);
    }
    EndDialog(d->hDlg, TRUE);
    return TRUE;
}

/* One name typed or chosen: checks it and returns its 8.3 name in lpShort
   (0 OK, 1 refused, 2 it is a folder: lpShort is its 8.3 path) */
static int CheckName(LPFDLG d, LPCSTR lpTyped, LPSTR lpDir, LPSTR lpShort)
{
    LPOPENFILENAME ofn = d->ofn;
    char szFull[LFN_MAXPATH], szName[LFN_MAXNAME];
    LPSTR lpName;
    BOOL bLong;

    FullPath(d, lpTyped, szFull, sizeof(szFull));
    /* a folder: open it */
    if (ShortDir(szFull, lpShort))
        return 2;
    lpName = FileNameOf(szFull);
    lstrcpyn(szName, lpName, sizeof(szName));
    if (lpName > (LPSTR)szFull)
        lpName[lpName[-1] == '\\' && lpName - 1 > (LPSTR)szFull + 2 ? -1 : 0] = '\0';
    if (lstrlen(szFull) == 2)
        lstrcat(szFull, "\\");
    if (!ShortDir(szFull, lpDir)) {
        ShowError(d, "%s\n\nThe path does not exist. Check the path and try again.", lpTyped);
        return 1;
    }
    bLong = LfnDrive(lpDir);
    /* the default extension */
    if (LfnShortOf(lpDir, szName, lpShort) != 0 && ofn->lpstrDefExt && *ofn->lpstrDefExt
        && !_fstrchr(szName, '.') && lstrlen(szName) + 4 < (int)sizeof(szName)) {
        LPSTR lpExt;
        BOOL bLower = FALSE;
        for (lpExt = szName; *lpExt; lpExt++)
            if (IsCharLower(*lpExt))
                bLower = TRUE;
        lstrcat(szName, ".");
        lpExt = szName + lstrlen(szName);
        lstrcat(szName, ofn->lpstrDefExt);
        if (bLower)                     /* "Report.txt" for "Report" */
            AnsiLower(lpExt);
    }
    if (LfnShortOf(lpDir, szName, lpShort) == 0) {
        /* it exists */
        if (d->bSave && (ofn->Flags & OFN_OVERWRITEPROMPT)
            && !AskYesNo(d, "%s already exists.\n\nDo you want to replace it?", szName))
            return 1;
        return 0;
    }
    if (!d->bSave && (ofn->Flags & OFN_FILEMUSTEXIST)) {
        ShowError(d, "%s\n\nFile not found.\nCheck the file name and try again.", szName);
        return 1;
    }
    if (!d->bSave && (ofn->Flags & OFN_CREATEPROMPT)
        && !AskYesNo(d, "%s\n\nThis file does not exist.\nDo you want to create it?", szName))
        return 1;
    if (LfnIsShortName(szName)) {
        lstrcpyn(lpShort, szName, 13);
        AnsiUpper(lpShort);
        return 0;
    }
    if (!bLong || !LfnValidName(szName, TRUE)) {
        ShowError(d, bLong ? "%s\n\nThe file name is not valid."
                           : "%s\n\nThis is not a valid MS-DOS file name: up to 8 characters, "
                             "a full stop and up to 3 more, without spaces.", szName);
        return 1;
    }
    /* a new long name: create the file now, so it has an 8.3 name for
       the program to open */
    lstrcpy(d->szWork2, lpDir);
    CatPath(d->szWork2, szName, sizeof(d->szWork2));
    if (LfnCreateEmpty(d->szWork2, d->szShort, sizeof(d->szShort)) != 0) {
        ShowError(d, "%s\n\nThe file cannot be created.", szName);
        return 1;
    }
    lstrcpyn(lpShort, FileNameOf(d->szShort), 13);
    return 0;
}

/* Splits '"one" "two" three' into NUL-separated names; returns the count */
static int SplitNames(LPSTR lpText)
{
    LPSTR p = lpText, q = lpText;
    int n = 0;

    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (*p == '"') {
            p++;
            while (*p && *p != '"')
                *q++ = *p++;
            if (*p)
                p++;
        } else
            while (*p && *p != ' ')
                *q++ = *p++;
        *q++ = '\0';
        n++;
    }
    *q = '\0';
    return n;
}

static void DoOK(LPFDLG d)
{
    char szDir[SPATH], szShort[SPATH];
    HGLOBAL hNames;
    LPSTR lpText, lpNames, lp, lpOut;
    int i, nSel = 0, nNames, rc;

    /* the list has the focus and a folder is selected: open it */
    for (i = 0; i < d->nItems; i++)
        if (d->lpItems[i].bSel)
            nSel++;
    if (GetFocus() == d->hList && nSel == 1 && d->nFocus >= 0
        && d->lpItems[d->nFocus].bSel && d->lpItems[d->nFocus].bKind != IK_FILE) {
        LPDITEM p = &d->lpItems[d->nFocus];
        if (p->bKind == IK_DRIVE)
            wsprintf(szDir, "%s\\", (LPSTR)p->szShort);
        else {
            lstrcpy(szDir, d->szDir);
            CatPath(szDir, p->szShort, sizeof(szDir));
        }
        Navigate(d, szDir, TRUE);
        return;
    }
    hNames = GlobalAlloc(GMEM_MOVEABLE, LFN_MAXPATH * 4 + 2);
    if (!hNames)
        return;
    lpText = (LPSTR)GlobalLock(hNames);
    GetWindowText(d->hEdit, lpText, LFN_MAXPATH * 4);
    /* unquoted text is one name, unless several can be chosen and the
       whole of it names nothing */
    if (!_fstrchr(lpText, '"') && !HasWildcards(lpText)) {
        WORD wAttr;
        int n = lstrlen(lpText);
        while (n > 0 && lpText[n - 1] == ' ')
            lpText[--n] = '\0';
        FullPath(d, lpText, d->szWork2, sizeof(d->szWork2));
        if (!(d->ofn->Flags & OFN_ALLOWMULTISELECT) || LfnGetAttr(d->szWork2, &wAttr) == 0
            || !_fstrchr(lpText, ' ')) {
            lp = lpText;
            while (*lp == ' ')
                lp++;
            if (lp != lpText)
                lstrcpy(lpText, lp);
            lpText[lstrlen(lpText) + 1] = '\0';
            nNames = lpText[0] ? 1 : 0;
        } else
            nNames = SplitNames(lpText);
    } else
        nNames = SplitNames(lpText);
    if (nNames == 0)
        goto done;
    /* a pattern: show those files */
    if (nNames == 1 && HasWildcards(lpText)) {
        LPSTR lpName = FileNameOf(lpText);
        if (lpName > lpText) {
            char c = *lpName;
            *lpName = '\0';
            FullPath(d, lpText, d->szWork2, sizeof(d->szWork2));
            *lpName = c;
            if (ShortDir(d->szWork2, szDir))
                Navigate(d, szDir, TRUE);
        }
        lstrcpyn(d->szFilter, lpName, sizeof(d->szFilter));
        if (d->ofn->lpstrCustomFilter && d->ofn->nMaxCustFilter > 4) {
            LPSTR lpc = d->ofn->lpstrCustomFilter;
            int n = lstrlen(lpc) + 1;
            if (n + lstrlen(lpName) + 2 <= (int)d->ofn->nMaxCustFilter) {
                lstrcpy(lpc + n, lpName);
                lpc[n + lstrlen(lpName) + 1] = '\0';
            }
        }
        Reload(d, NULL);
        goto done;
    }
    if (nNames > 1 && !(d->ofn->Flags & OFN_ALLOWMULTISELECT))
        nNames = 1;                     /* only the first */
    /* each name: its 8.3 name in the same buffer, behind the text */
    lpNames = lpText + LFN_MAXPATH * 2;
    lpOut = lpNames;
    szDir[0] = '\0';
    for (lp = lpText, i = 0; i < nNames; i++, lp += lstrlen(lp) + 1) {
        char szThisDir[SPATH];
        rc = CheckName(d, lp, szThisDir, szShort);
        if (rc == 2) {
            if (nNames == 1) {
                SetWindowText(d->hEdit, "");
                Navigate(d, szShort, TRUE);
            }
            goto done;
        }
        if (rc != 0)
            goto done;
        if (i == 0)
            lstrcpy(szDir, szThisDir);
        else if (lstrcmpi(szDir, szThisDir) != 0) {
            ShowError(d, "%s\n\nAll the files must be in the same folder.", lp);
            goto done;
        }
        if (lpOut + lstrlen(szShort) + 2 > lpText + LFN_MAXPATH * 4)
            break;
        lstrcpy(lpOut, szShort);
        lpOut += lstrlen(lpOut) + 1;
    }
    if (Finish(d, szDir, lpNames, nNames)) {
        GlobalUnlock(hNames);
        GlobalFree(hNames);
        return;
    }
done:
    /* what was typed stays, selected, to be typed over */
    SetFocus(d->hEdit);
    SendMessage(d->hEdit, EM_SETSEL, 0, MAKELPARAM(0, -1));
    GlobalUnlock(hNames);
    GlobalFree(hNames);
}

/* the edit shows the selected files: "name" or "one" "two" */
static void SelectionToEdit(LPFDLG d)
{
    int i, n = 0, nFiles = 0;
    LPSTR lp = d->szWork;

    for (i = 0; i < d->nItems; i++)
        if (d->lpItems[i].bSel && d->lpItems[i].bKind == IK_FILE)
            nFiles++;
    if (nFiles == 0)
        return;
    lp[0] = '\0';
    for (i = 0; i < d->nItems; i++) {
        LPCSTR lpName;
        if (!d->lpItems[i].bSel || d->lpItems[i].bKind != IK_FILE)
            continue;
        lpName = LongOf(d, &d->lpItems[i]);
        if (n + lstrlen(lpName) + 4 >= (int)sizeof(d->szWork))
            break;
        if (nFiles > 1)
            n += wsprintf(lp + n, n ? " \"%s\"" : "\"%s\"", lpName);
        else
            n += wsprintf(lp + n, "%s", lpName);
    }
    SetWindowText(d->hEdit, lp);
}

/* -------------------------------------------------------------------- */
/* Layout                                                               */
/* -------------------------------------------------------------------- */

static HWND MakeControl(LPFDLG d, LPCSTR lpClass, LPCSTR lpText, DWORD dwStyle, int id)
{
    HWND hwnd = CreateWindow(lpClass, lpText, WS_CHILD | WS_VISIBLE | dwStyle,
                             0, 0, 10, 10, d->hDlg, (HMENU)id, s_hInst, NULL);
    if (hwnd)
        SendMessage(hwnd, WM_SETFONT, (WPARAM)d->hFont, 0L);
    return hwnd;
}

static void Place(HWND hDlg, int id, int x, int y, int cx, int cy)
{
    HWND hwnd = GetDlgItem(hDlg, id);
    if (hwnd)
        MoveWindow(hwnd, x, y, cx, cy, TRUE);
}

static int LabelWidth(LPFDLG d, LPCSTR lp)
{
    HDC hdc = GetDC(d->hDlg);
    HFONT hfOld = SelectObject(hdc, d->hFont);
    int cx = TextWidth(hdc, lp);

    SelectObject(hdc, hfOld);
    ReleaseDC(d->hDlg, hdc);
    return cx;
}

static void Layout(LPFDLG d, int cx, int cy)
{
    HWND h = d->hDlg;
    int m = 8, cyEdit = d->cyFont + 9, cyBtn = 23, cxBtn = 76, tb = 25;
    int cxLbl = LabelWidth(d, "Files of type:") + 10, x0 = m, xList, yList, cyList;
    BOOL bRO = !(d->ofn->Flags & OFN_HIDEREADONLY);
    BOOL bHelp = (d->ofn->Flags & OFN_SHOWHELP) != 0;
    int nRows = 2, yRow;

    d->bInLayout = TRUE;
    if (d->nLook == NSDLG_11) {
        int cxNav = 160, xA;
        /* top: Back, Up, address bar, search */
        Place(h, ID_BACK, m, m, 28, 26);
        Place(h, ID_UP, m + 30, m, 28, 26);
        xA = m + 64;
        Place(h, ID_ADDRESS, xA, m, cx - xA - 200 - m, 26);
        Place(h, ID_SEARCH, cx - m - 192, m + 2, 166, cyEdit);
        Place(h, ID_SEARCHICON, cx - m - 24, m + 2, 24, cyEdit);
        /* command bar */
        Place(h, ID_NEWDIR, m, m + 34, 104, 26);
        Place(h, ID_VIEWS, cx - m - 30, m + 34, 30, 26);
        yList = m + 68;
        yRow = cy - m - cyBtn - 8 - cyEdit;
        cyList = yRow - 10 - yList;
        Place(h, ID_PLACES, m, yList, cxNav, cyList);
        xList = m + cxNav + 6;
        Place(h, ID_LIST, xList, yList, cx - xList - m, cyList);
        /* the name and the type on one line, the buttons below */
        cxLbl = LabelWidth(d, "File name:") + 10;
        Place(h, ID_LBLNAME, xList, yRow + 3, cxLbl, d->cyFont + 2);
        Place(h, ID_FILENAME, xList + cxLbl, yRow, cx - xList - cxLbl - m - 196, cyEdit);
        Place(h, ID_TYPES, cx - m - 190, yRow, 190, 160);
        Place(h, ID_LBLTYPES, 0, 0, 0, 0);
        yRow = cy - m - cyBtn;
        Place(h, IDCANCEL, cx - m - cxBtn, yRow, cxBtn, cyBtn);
        Place(h, IDOK, cx - m - 2 * cxBtn - 8, yRow, cxBtn, cyBtn);
        Place(h, ID_HELP, cx - m - 3 * cxBtn - 16, yRow, cxBtn, cyBtn);
        Place(h, ID_READONLY, xList, yRow + 3, 180, d->cyFont + 4);
    } else {
        int cxLook = LabelWidth(d, d->bSave ? "Save in:" : "Look in:") + 10;
        int nTb = d->nLook == NSDLG_XP ? 4 : 4, xTb;
        if (d->nLook == NSDLG_XP) {
            Place(h, ID_PLACES, m, m, 86, cy - 2 * m);
            x0 = m + 86 + 8;
        }
        xTb = cx - m - nTb * (tb + 1);
        Place(h, ID_LBLLOOKIN, x0, m + 4, cxLook, d->cyFont + 2);
        Place(h, ID_LOOKIN, x0 + cxLook, m, xTb - x0 - cxLook - 8, 200);
        if (d->nLook == NSDLG_XP) {
            Place(h, ID_BACK, xTb, m - 1, tb, tb);
            Place(h, ID_UP, xTb + tb + 1, m - 1, tb, tb);
            Place(h, ID_NEWDIR, xTb + 2 * (tb + 1), m - 1, tb, tb);
            Place(h, ID_VIEWS, xTb + 3 * (tb + 1), m - 1, tb, tb);
        } else {
            Place(h, ID_UP, xTb, m - 1, tb, tb - 2);
            Place(h, ID_NEWDIR, xTb + tb + 1, m - 1, tb, tb - 2);
            Place(h, ID_VLIST, xTb + 2 * (tb + 1) + 4, m - 1, tb, tb - 2);
            Place(h, ID_VDETAILS, xTb + 3 * (tb + 1) + 4, m - 1, tb, tb - 2);
        }
        if (bRO || bHelp)
            nRows = 3;
        yList = m + tb + 6;
        cyList = cy - yList - m - nRows * (cyEdit + 6) - 4;
        xList = x0;
        Place(h, ID_LIST, xList, yList, cx - xList - m, cyList);
        yRow = yList + cyList + 8;
        Place(h, ID_LBLNAME, x0, yRow + 3, cxLbl, d->cyFont + 2);
        Place(h, ID_FILENAME, x0 + cxLbl, yRow, cx - x0 - cxLbl - m - cxBtn - 10, cyEdit);
        Place(h, IDOK, cx - m - cxBtn, yRow - 1, cxBtn, cyBtn);
        yRow += cyEdit + 6;
        Place(h, ID_LBLTYPES, x0, yRow + 3, cxLbl, d->cyFont + 2);
        Place(h, ID_TYPES, x0 + cxLbl, yRow, cx - x0 - cxLbl - m - cxBtn - 10, 160);
        Place(h, IDCANCEL, cx - m - cxBtn, yRow - 1, cxBtn, cyBtn);
        yRow += cyEdit + 6;
        Place(h, ID_READONLY, x0 + cxLbl, yRow, 180, d->cyFont + 4);
        Place(h, ID_HELP, cx - m - cxBtn, yRow - 1, cxBtn, cyBtn);
    }
    d->bInLayout = FALSE;
    InvalidateRect(h, NULL, TRUE);
}

static void CreateControls(LPFDLG d)
{
    LPOPENFILENAME ofn = d->ofn;
    BOOL b11 = d->nLook == NSDLG_11;

    if (b11) {
        MakeControl(d, "BUTTON", "", BS_OWNERDRAW | WS_TABSTOP, ID_BACK);
        MakeControl(d, "BUTTON", "", BS_OWNERDRAW | WS_TABSTOP, ID_UP);
        d->hAddress = MakeControl(d, "NsDlgAddress", "", 0, ID_ADDRESS);
        d->hSearch = MakeControl(d, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SEARCH);
        MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_SEARCHICON);
        MakeControl(d, "BUTTON", "New folder", BS_OWNERDRAW | WS_TABSTOP, ID_NEWDIR);
        MakeControl(d, "BUTTON", "", BS_OWNERDRAW | WS_TABSTOP, ID_VIEWS);
        d->hPlaces = MakeControl(d, "NsDlgPlaces", "", 0, ID_PLACES);
    } else {
        MakeControl(d, "STATIC", d->bSave ? "Save &in:" : "Look &in:", SS_LEFT, ID_LBLLOOKIN);
        d->hLookIn = MakeControl(d, "COMBOBOX", "", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED
                                 | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, ID_LOOKIN);
        if (d->nLook == NSDLG_XP) {
            d->hPlaces = MakeControl(d, "NsDlgPlaces", "", 0, ID_PLACES);
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_BACK);
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_UP);
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_NEWDIR);
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_VIEWS);
        } else {
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_UP);
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_NEWDIR);
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_VLIST);
            MakeControl(d, "BUTTON", "", BS_OWNERDRAW, ID_VDETAILS);
        }
    }
    d->hList = MakeControl(d, "NsDlgList", "", WS_TABSTOP | WS_BORDER | WS_HSCROLL | WS_VSCROLL,
                           ID_LIST);
    MakeControl(d, "STATIC", "File &name:", SS_LEFT, ID_LBLNAME);
    d->hEdit = MakeControl(d, "EDIT", "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_FILENAME);
    if (!b11)
        MakeControl(d, "STATIC", d->bSave ? "Save as &type:" : "Files of &type:", SS_LEFT,
                    ID_LBLTYPES);
    d->hTypes = MakeControl(d, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                            ID_TYPES);
    MakeControl(d, "BUTTON", d->bSave ? "&Save" : "&Open", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
    MakeControl(d, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
    if (!(ofn->Flags & OFN_HIDEREADONLY)) {
        MakeControl(d, "BUTTON", "Open as &read-only", BS_AUTOCHECKBOX | WS_TABSTOP, ID_READONLY);
        CheckDlgButton(d->hDlg, ID_READONLY, (ofn->Flags & OFN_READONLY) != 0);
    }
    if (ofn->Flags & OFN_SHOWHELP)
        MakeControl(d, "BUTTON", "&Help", BS_PUSHBUTTON | WS_TABSTOP, ID_HELP);
    SendMessage(d->hEdit, EM_LIMITTEXT, LFN_MAXPATH * 2, 0L);
    if (d->hList)
        SetWindowLong(d->hList, 0, (LONG)d);
    if (d->hPlaces)
        SetWindowLong(d->hPlaces, 0, (LONG)d);
    if (d->hAddress)
        SetWindowLong(d->hAddress, 0, (LONG)d);
}

/* -------------------------------------------------------------------- */
/* The dialog                                                           */
/* -------------------------------------------------------------------- */

static void InitialState(LPFDLG d)
{
    LPOPENFILENAME ofn = d->ofn;
    char szDir[SPATH];
    LPSTR lpName = NULL;
    union REGS r;
    struct SREGS s;

    /* the folder: lpstrInitialDir, the path in lpstrFile, the current one */
    szDir[0] = '\0';
    if (ofn->lpstrFile && ofn->lpstrFile[0]) {
        lstrcpyn(d->szWork2, ofn->lpstrFile, sizeof(d->szWork2));
        lpName = FileNameOf(d->szWork2);
        if (lpName > (LPSTR)d->szWork2) {
            char c = *lpName;
            *lpName = '\0';
            FullPath(d, d->szWork2, d->szWork, sizeof(d->szWork));
            *lpName = c;
            if (!ShortDir(d->szWork, szDir))
                szDir[0] = '\0';
        }
    }
    if (!szDir[0] && ofn->lpstrInitialDir && ofn->lpstrInitialDir[0]) {
        lstrcpyn(d->szWork, ofn->lpstrInitialDir, sizeof(d->szWork));
        if (!ShortDir(d->szWork, szDir))
            szDir[0] = '\0';
    }
    if (!szDir[0]) {
        r.h.ah = 0x19;
        intdos(&r, &r);
        wsprintf(szDir, "%c:\\", 'A' + r.h.al);
        segread(&s);
        r.h.ah = 0x47;
        r.h.dl = 0;
        s.ds = FP_SEG((LPSTR)szDir);
        r.x.si = FP_OFF((LPSTR)(szDir + 3));
        intdosx(&r, &r, &s);
        if (r.x.cflag)
            szDir[3] = '\0';
    }
    lstrcpy(d->szDir, szDir);
    AnsiUpper(d->szDir);

    ApplyType(d);
    if (lpName && *lpName) {
        if (HasWildcards(lpName))
            lstrcpyn(d->szFilter, lpName, sizeof(d->szFilter));
        else {
            /* the long name of the file suggested, if it exists */
            char szShort[14];
            if (LfnShortOf(d->szDir, lpName, szShort) == 0) {
                lstrcpy(d->szWork, d->szDir);
                CatPath(d->szWork, szShort, sizeof(d->szWork));
                LfnLongName(d->szWork, d->szWork2, sizeof(d->szWork2));
                SetWindowText(d->hEdit, d->szWork2);
            } else
                SetWindowText(d->hEdit, lpName);
        }
    }
}

static void Center(HWND hDlg, HWND hwndOwner, int cx, int cy)
{
    RECT rc;
    int x, y, cxScr = GetSystemMetrics(SM_CXSCREEN), cyScr = GetSystemMetrics(SM_CYSCREEN);

    if (hwndOwner && IsWindowVisible(hwndOwner) && !IsIconic(hwndOwner))
        GetWindowRect(hwndOwner, &rc);
    else
        SetRect(&rc, 0, 0, cxScr, cyScr);
    x = (rc.left + rc.right - cx) / 2;
    y = (rc.top + rc.bottom - cy) / 2;
    if (x + cx > cxScr)
        x = cxScr - cx;
    if (y + cy > cyScr)
        y = cyScr - cy;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    MoveWindow(hDlg, x, y, cx, cy, FALSE);
}

static void InitDialog(HWND hDlg, LPFDLG d)
{
    LPOPENFILENAME ofn = d->ofn;
    TEXTMETRIC tm;
    HDC hdc;
    HFONT hfOld;
    RECT rc;
    int cx, cy;

    d->hDlg = hDlg;
    SetProp(hDlg, PROP_FDLG, (HANDLE)FP_SEG(d));
    d->hFont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0L);
    {
        /* Windows 3.1 makes dialog fonts bold; Windows 95 does not */
        LOGFONT lf;
        if (d->hFont && GetObject(d->hFont, sizeof(lf), &lf)) {
            lf.lfWeight = FW_NORMAL;
            d->hfNormal = CreateFontIndirect(&lf);
            if (d->hfNormal)
                d->hFont = d->hfNormal;
        }
    }
    if (!d->hFont)
        d->hFont = GetStockObject(SYSTEM_FONT);
    hdc = GetDC(hDlg);
    hfOld = SelectObject(hdc, d->hFont);
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, hfOld);
    ReleaseDC(hDlg, hdc);
    d->cyFont = tm.tmHeight;
    d->nRenaming = -1;
    d->bMulti = (ofn->Flags & OFN_ALLOWMULTISELECT) != 0;
    d->nMode = CfgInt(d->nLook == NSDLG_11 ? "View11" : d->nLook == NSDLG_XP ? "ViewXP" : "View95",
                      d->nLook == NSDLG_11 ? VM_DETAILS : VM_LIST) ? VM_DETAILS : VM_LIST;
    if (d->nLook == NSDLG_11)
        d->hbrPane = CreateSolidBrush(GetSysColor(COLOR_WINDOW));

    SetWindowText(hDlg, ofn->lpstrTitle ? ofn->lpstrTitle
                        : (d->bSave ? (LPCSTR)"Save As" : (LPCSTR)"Open"));
    CreateControls(d);
    FillTypes(d);
    BuildPlaces(d);
    InitialState(d);

    /* the size: the default of the look, or the one used last */
    switch (d->nLook) {
    case NSDLG_XP:
        cx = 560;
        cy = 360;
        break;
    case NSDLG_11:
        cx = 620;
        cy = 400;
        break;
    default:
        cx = 440;
        cy = !(ofn->Flags & OFN_HIDEREADONLY) ? 300 : 276;
        break;
    }
    d->cxMin = cx * 3 / 4;
    d->cyMin = cy * 3 / 4;
    if (d->nLook != NSDLG_95) {
        cx = CfgInt(d->nLook == NSDLG_11 ? "Width11" : "WidthXP", cx);
        cy = CfgInt(d->nLook == NSDLG_11 ? "Height11" : "HeightXP", cy);
    }
    if (cx > GetSystemMetrics(SM_CXSCREEN))
        cx = GetSystemMetrics(SM_CXSCREEN);
    if (cy > GetSystemMetrics(SM_CYSCREEN))
        cy = GetSystemMetrics(SM_CYSCREEN);
    Center(hDlg, ofn->hwndOwner, cx, cy);
    GetClientRect(hDlg, &rc);
    Layout(d, rc.right, rc.bottom);
    Navigate(d, d->szDir, FALSE);
    SetFocus(d->hEdit);
    SendMessage(d->hEdit, EM_SETSEL, 0, MAKELPARAM(0, -1));
}

static void Cleanup(LPFDLG d)
{
    int i;
    RECT rc;

    if (d->nLook != NSDLG_95 && d->hDlg && !IsIconic(d->hDlg) && !IsZoomed(d->hDlg)) {
        char szValue[8];
        GetWindowRect(d->hDlg, &rc);
        wsprintf(szValue, "%d", rc.right - rc.left);
        WritePrivateProfileString("FileDialog", d->nLook == NSDLG_11 ? "Width11" : "WidthXP",
                                  szValue, s_szIni);
        wsprintf(szValue, "%d", rc.bottom - rc.top);
        WritePrivateProfileString("FileDialog", d->nLook == NSDLG_11 ? "Height11" : "HeightXP",
                                  szValue, s_szIni);
    }
    FreeItemIcons(d);
    d->nItems = 0;
    for (i = 0; i < d->nExt; i++)
        if (d->aExt[i].hIcon)
            DestroyIcon(d->aExt[i].hIcon);
    d->nExt = 0;
    if (d->hbmTmp)
        DeleteObject(d->hbmTmp);
    d->hbmTmp = NULL;
    if (d->hbrPane)
        DeleteObject(d->hbrPane);
    d->hbrPane = NULL;
    RemoveProp(d->hDlg, PROP_FDLG);
    if (d->hfNormal)
        DeleteObject(d->hfNormal);
}

BOOL CALLBACK __export NsFileDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LPFDLG d = (LPFDLG)MK_FP(GetProp(hDlg, PROP_FDLG), 0);

    if (msg == WM_INITDIALOG) {
        d = (LPFDLG)lParam;
        InitDialog(hDlg, d);
        return FALSE;                   /* the focus is set */
    }
    if (!FP_SEG(d))
        return FALSE;
    switch (msg) {
    case WM_SIZE:
        if (!d->bInLayout && wParam != SIZE_MINIMIZED)
            Layout(d, LOWORD(lParam), HIWORD(lParam));
        return FALSE;
    case WM_GETMINMAXINFO:
        {
            MINMAXINFO FAR *lpmmi = (MINMAXINFO FAR *)lParam;
            lpmmi->ptMinTrackSize.x = d->cxMin;
            lpmmi->ptMinTrackSize.y = d->cyMin;
        }
        return FALSE;
    case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT FAR *lpmis = (MEASUREITEMSTRUCT FAR *)lParam;
            if (lpmis->CtlID == ID_LOOKIN) {
                lpmis->itemHeight = d->cyFont + 3 < 18 ? 18 : d->cyFont + 3;
                return TRUE;
            }
        }
        return FALSE;
    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT FAR *lpdis = (DRAWITEMSTRUCT FAR *)lParam;
            if (lpdis->CtlID == ID_LOOKIN)
                DrawLookIn(d, lpdis);
            else
                DrawToolButton(d, lpdis);
        }
        return TRUE;
    case WM_COMMAND:
        switch (wParam) {
        case IDOK:
            if (d->hRename) {
                EndRename(d, TRUE);
                return TRUE;
            }
            DoOK(d);
            return TRUE;
        case IDCANCEL:
            if (d->hRename) {
                EndRename(d, FALSE);
                return TRUE;
            }
            EndDialog(hDlg, FALSE);
            return TRUE;
        case ID_HELP:
            if (d->ofn->hwndOwner)
                SendMessage(d->ofn->hwndOwner, RegisterWindowMessage("commdlg_help"),
                            (WPARAM)hDlg, (LPARAM)d->ofn);
            return TRUE;
        case ID_UP:
            GoUp(d);
            return TRUE;
        case ID_BACK:
            GoBack(d);
            return TRUE;
        case ID_NEWDIR:
            NewFolder(d);
            return TRUE;
        case ID_VLIST:
        case ID_VDETAILS:
        case ID_VIEWS:
            d->nMode = wParam == ID_VLIST ? VM_LIST : wParam == ID_VDETAILS ? VM_DETAILS
                                                    : (d->nMode == VM_LIST ? VM_DETAILS : VM_LIST);
            d->nTop = d->nLeft = 0;
            ListUpdate(d);
            EnsureVisible(d, d->nFocus);
            UpdateButtons(d);
            return TRUE;
        case ID_RENAME:
            if (HIWORD(lParam) == 1)
                EndRename(d, TRUE);
            else
                BeginRename(d, d->nFocus);
            return TRUE;
        case ID_LOOKIN:
            if (HIWORD(lParam) == CBN_SELENDOK || HIWORD(lParam) == CBN_SELCHANGE) {
                int n = (int)SendMessage(d->hLookIn, CB_GETCURSEL, 0, 0L);
                /* a selection made in the closed list, or the list closed */
                if (n >= 0 && n < d->nLookIn
                    && (HIWORD(lParam) == CBN_SELENDOK
                        || !SendMessage(d->hLookIn, CB_GETDROPPEDSTATE, 0, 0L))
                    && lstrcmpi(d->aLookIn[n], d->szDir) != 0) {
                    char szDir[SPATH];
                    lstrcpy(szDir, d->aLookIn[n]);
                    Navigate(d, szDir, TRUE);
                }
            }
            return TRUE;
        case ID_TYPES:
            if (HIWORD(lParam) == CBN_SELCHANGE) {
                ApplyType(d);
                Reload(d, NULL);
            }
            return TRUE;
        case ID_SEARCH:
            if (HIWORD(lParam) == EN_CHANGE || HIWORD(lParam) == 0xFFFF) {
                if (d->hSearch)
                    GetWindowText(d->hSearch, d->szSearch, sizeof(d->szSearch));
                Reload(d, NULL);
            }
            return TRUE;
        case ID_LIST:
            switch (HIWORD(lParam)) {
            case LN_SELCHANGE:
                SelectionToEdit(d);
                break;
            case LN_OPEN:
                if (d->nFocus >= 0 && d->lpItems[d->nFocus].bKind != IK_FILE) {
                    LPDITEM p = &d->lpItems[d->nFocus];
                    char szDir[SPATH];
                    if (p->bKind == IK_DRIVE)
                        wsprintf(szDir, "%s\\", (LPSTR)p->szShort);
                    else {
                        lstrcpy(szDir, d->szDir);
                        CatPath(szDir, p->szShort, sizeof(szDir));
                    }
                    Navigate(d, szDir, TRUE);
                } else {
                    SelectionToEdit(d);
                    SetFocus(d->hEdit);
                    DoOK(d);
                }
                break;
            case LN_UP:
                GoUp(d);
                break;
            }
            return TRUE;
        }
        break;
    case WM_DESTROY:
        Cleanup(d);
        return FALSE;
    }
    return FALSE;
}

/* -------------------------------------------------------------------- */
/* Entry points                                                         */
/* -------------------------------------------------------------------- */

static BOOL RegisterClasses(void)
{
    WNDCLASS wc;

    if (s_bClasses)
        return TRUE;
    _fmemset(&wc, 0, sizeof(wc));
    wc.style = CS_GLOBALCLASS | CS_DBLCLKS;
    wc.lpfnWndProc = NsDlgListProc;
    wc.cbWndExtra = sizeof(LPVOID);
    wc.hInstance = s_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "NsDlgList";
    if (!RegisterClass(&wc))
        return FALSE;
    wc.style = CS_GLOBALCLASS;
    wc.lpfnWndProc = NsDlgPlacesProc;
    wc.lpszClassName = "NsDlgPlaces";
    RegisterClass(&wc);
    wc.lpfnWndProc = NsDlgAddressProc;
    wc.lpszClassName = "NsDlgAddress";
    RegisterClass(&wc);
    s_bClasses = TRUE;
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* The Windows 3.1 dialog with long names (programs with hooks)         */
/* -------------------------------------------------------------------- */

/*
 * Programs with a template or hook of their own (Write, Paintbrush...)
 * get COMMDLG's dialog, whose controls they use. NSDLG puts its own hook
 * in front of theirs (theirs still gets every message) and only changes
 * what is shown:
 *
 *   - the file and folder lists (owner-drawn by COMMDLG in its standard
 *     template) are painted with long names; their entries stay the 8.3
 *     names COMMDLG and the program read
 *   - the current folder line (stc1) paints the long path, its text stays
 *   - a file chosen in the list shows its long name in the name box
 *     (single selection only: COMMDLG splits multiple names at blanks)
 *   - on OK, a long name typed or shown is turned into its 8.3 path before
 *     COMMDLG checks it; a new long name in Save As is created first so it
 *     has an alias (and is deleted again if the dialog is cancelled)
 *
 * Without long names on the drive everything is left to COMMDLG.
 */

#define MAX_CLASSIC     6
#define CLASSIC_POOL    16000U
#define WM_NS_LONGEDIT  (WM_USER + 0x5E0)   /* posted: the name box shows the long name */
#define PROP_CLASSIC    "NsDlgClassic"
#define PROP_OLDLO      "NsDlgOldLo"
#define PROP_OLDHI      "NsDlgOldHi"

typedef UINT (CALLBACK *CLASSICHOOK)(HWND, UINT, WPARAM, LPARAM);

/* one per COMMDLG call in progress */
typedef struct tagCLASSICREG {
    LPOPENFILENAME ofn;
    CLASSICHOOK    lpfnHook;        /* the program's, NULL if none */
    BOOL  bSave;
    BOOL  bOverwriteCleared;        /* OFN_OVERWRITEPROMPT taken away for a file we made */
    char  szCreated[SPATH];         /* file created for a new long name */
} CLASSICREG;

static CLASSICREG s_reg[MAX_CLASSIC];
static int        s_nPending = -1;  /* registered, dialog not yet initialised */

typedef struct tagCLASSIC {
    int   nReg;                     /* index in s_reg */
    char  szDir[SPATH];             /* the folder whose names are cached */
    LPSTR lpPool;                   /* "SHORT\0Long\0" pairs */
    HGLOBAL hPool;
    WORD  cbPool;
    char  szWork[LFN_MAXPATH];
    char  szWork2[LFN_MAXPATH];
} CLASSIC, FAR *LPCLASSIC;

static LPCLASSIC ClassicOf(HWND hDlg)
{
    HANDLE h = GetProp(hDlg, PROP_CLASSIC);
    return h ? (LPCLASSIC)MK_FP((WORD)h, 0) : NULL;
}

static CLASSICREG *RegOf(HWND hDlg)
{
    LPCLASSIC c = ClassicOf(hDlg);

    if (c)
        return &s_reg[c->nReg];
    return s_nPending >= 0 ? &s_reg[s_nPending] : NULL;
}

static BOOL IsEmptyFile(LPCSTR lpPath)
{
    struct find_t ft;
    char sz[SPATH];

    lstrcpyn(sz, lpPath, sizeof(sz));
    return _dos_findfirst(sz, _A_NORMAL | _A_RDONLY | _A_ARCH, &ft) == 0 && ft.size == 0;
}

/* The task's current directory: COMMDLG changes it to the folder shown */
static void CurrentDir(LPSTR lpDir)
{
    union REGS r;
    struct SREGS s;
    char sz[SPATH];

    r.h.ah = 0x19;
    intdos(&r, &r);
    wsprintf(sz, "%c:\\", 'A' + r.h.al);
    segread(&s);
    r.h.ah = 0x47;
    r.h.dl = 0;
    s.ds = FP_SEG((LPSTR)sz);
    r.x.si = FP_OFF((LPSTR)(sz + 3));
    intdosx(&r, &r, &s);
    if (r.x.cflag)
        sz[3] = '\0';
    lstrcpy(lpDir, sz);
    AnsiUpper(lpDir);
}

/* Reads the long names of the current folder, if it changed */
static void ClassicNames(LPCLASSIC c)
{
    LFNFIND FAR *f;
    HGLOBAL hFind;
    char szDir[SPATH];
    UINT uOld;

    CurrentDir(szDir);
    if (lstrcmp(szDir, c->szDir) == 0)
        return;
    lstrcpy(c->szDir, szDir);
    c->cbPool = 0;
    if (!c->lpPool || !LfnDrive(szDir))
        return;
    hFind = GlobalAlloc(GMEM_MOVEABLE, sizeof(LFNFIND));
    if (!hFind)
        return;
    f = (LFNFIND FAR *)GlobalLock(hFind);
    lstrcpy(c->szWork, szDir);
    CatPath(c->szWork, "*.*", sizeof(c->szWork));
    uOld = SetErrorMode(1);
    if (LfnFindFirst(c->szWork, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_SUBDIR | _A_HIDDEN
                                | _A_SYSTEM, f) == 0)
        do {
            int cbS = lstrlen(f->szShort) + 1, cbL = lstrlen(f->szName) + 1;
            if (f->szShort[0] == '.' || lstrcmpi(f->szName, f->szShort) == 0)
                continue;
            if (c->cbPool + cbS + cbL + 1 > CLASSIC_POOL)
                break;
            lstrcpy(c->lpPool + c->cbPool, f->szShort);
            lstrcpy(c->lpPool + c->cbPool + cbS, f->szName);
            c->cbPool += cbS + cbL;
        } while (LfnFindNext(f) == 0);
    LfnFindClose(f);
    SetErrorMode(uOld);
    GlobalUnlock(hFind);
    GlobalFree(hFind);
}

/* The long name of an 8.3 name in the current folder, or the name itself */
static LPCSTR ClassicLong(LPCLASSIC c, LPCSTR lpShort)
{
    WORD o = 0;

    while (o < c->cbPool) {
        LPCSTR lpS = c->lpPool + o, lpL = lpS + lstrlen(lpS) + 1;
        if (lstrcmpi(lpS, lpShort) == 0)
            return lpL;
        o = (WORD)(lpL + lstrlen(lpL) + 1 - c->lpPool);
    }
    return lpShort;
}

/* Paints an entry of the file list (lst1) or the folder list (lst2) */
static BOOL ClassicDrawItem(HWND hDlg, LPCLASSIC c, DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText;
    char szItem[LFN_MAXPATH], szFit[LFN_MAXNAME];
    LPCSTR lpText;
    BOOL bSel = (lpdis->itemState & ODS_SELECTED) != 0;
    int x = rc.left + 2, nDepth = 0, nIcon = -1;
    LPSTR p;

    if (lpdis->CtlType != ODT_LISTBOX || (lpdis->CtlID != lst1 && lpdis->CtlID != lst2))
        return FALSE;
    ClassicNames(c);
    if (!LfnDrive(c->szDir))
        return FALSE;                   /* COMMDLG's own painting */
    if ((int)lpdis->itemID < 0) {
        if (lpdis->itemState & ODS_FOCUS)
            DrawFocusRect(hdc, &rc);
        return TRUE;
    }
    if (!(lpdis->itemAction & (ODA_DRAWENTIRE | ODA_SELECT | ODA_FOCUS)))
        return TRUE;
    SendMessage(lpdis->hwndItem, LB_GETTEXT, lpdis->itemID, (LPARAM)(LPSTR)szItem);
    lpText = szItem;
    if (lpdis->CtlID == lst2) {
        /* the path first ("c:\", "windows"...), then the subfolders */
        for (p = c->szDir + 3, nDepth = 1; *p; p++)
            if (*p == '\\')
                nDepth++;
        if (lstrlen(c->szDir) > 3)
            nDepth++;
        if ((int)lpdis->itemID < nDepth) {
            int i, n = 0;
            nIcon = SMI_FOLDEROPEN;
            x += (int)lpdis->itemID * 8;
            if (lpdis->itemID > 0) {
                /* the path down to this entry, then its long name */
                for (p = c->szDir, i = 0; *p && i <= (int)lpdis->itemID; p++)
                    if (*p == '\\' && ++i > (int)lpdis->itemID)
                        break;
                n = (int)(p - c->szDir);
                lstrcpyn(c->szWork2, c->szDir, n + 1);
                if (LfnLongName(c->szWork2, c->szWork, sizeof(c->szWork)))
                    lpText = c->szWork;
            }
        } else {
            nIcon = SMI_FOLDER;
            x += nDepth * 8;
            lpText = ClassicLong(c, szItem);
        }
    } else
        lpText = ClassicLong(c, szItem);

    /* names without a long one in lower case, as COMMDLG shows them */
    if (lpText == (LPCSTR)szItem || lstrcmpi(lpText, szItem) == 0) {
        lstrcpy(szItem, lpText);
        AnsiLower(szItem);
        lpText = szItem;
    }
    FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
    if (nIcon >= 0) {
        DrawSmall(hdc, x, rc.top + (rc.bottom - rc.top - 16) / 2, nIcon);
        x += 18;
    }
    SetBkMode(hdc, TRANSPARENT);
    FitText(hdc, lpText, szFit, sizeof(szFit), rc.right - x - 4);
    SetRect(&rcText, x, rc.top, x + TextWidth(hdc, szFit) + 4, rc.bottom);
    if (bSel)
        FillSolid(hdc, &rcText, GetSysColor(COLOR_HIGHLIGHT));
    SetTextColor(hdc, GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    rcText.left += 2;
    DrawText(hdc, szFit, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    rcText.left -= 2;
    if (lpdis->itemState & ODS_FOCUS)
        DrawFocusRect(hdc, &rcText);
    (void)hDlg;
    return TRUE;
}

/* The current folder line: paints the long path, its text stays */
LRESULT CALLBACK __export NsClassicStaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC lpfnOld = (WNDPROC)MAKELONG((WORD)GetProp(hwnd, PROP_OLDLO),
                                        (WORD)GetProp(hwnd, PROP_OLDHI));

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        RECT rc;
        char szShort[SPATH], szLong[LFN_MAXPATH], szFit[LFN_MAXPATH];
        HDC hdc = BeginPaint(hwnd, &ps);
        HFONT hf = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0L), hfOld = NULL;
        HBRUSH hbr;

        GetClientRect(hwnd, &rc);
        hbr = (HBRUSH)SendMessage(GetParent(hwnd), WM_CTLCOLOR, (WPARAM)hdc,
                                  MAKELPARAM(hwnd, CTLCOLOR_STATIC));
        FillRect(hdc, &rc, hbr ? hbr : (HBRUSH)GetStockObject(WHITE_BRUSH));
        if (hf)
            hfOld = SelectObject(hdc, hf);
        GetWindowText(hwnd, szShort, sizeof(szShort));
        /* COMMDLG writes "c:\windows": the long path, the drive as it was */
        if (!LfnLongPath(szShort, szLong, sizeof(szLong)) || lstrcmpi(szLong, szShort) == 0)
            lstrcpy(szLong, szShort);
        else if (szShort[0])
            szLong[0] = szShort[0];
        if (TextWidth(hdc, szLong) > rc.right) {
            /* "...\the end of the path" */
            LPSTR p = szLong;
            while (*p && TextWidth(hdc, p) + TextWidth(hdc, "...") > rc.right)
                p++;
            wsprintf(szFit, "...%s", p);
        } else
            lstrcpy(szFit, szLong);
        SetBkMode(hdc, TRANSPARENT);
        DrawText(hdc, szFit, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        if (hfOld)
            SelectObject(hdc, hfOld);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_SETTEXT) {
        /* a Windows 3.1 static paints its new text at once: set it
           without painting, and paint it here */
        LRESULT lr = DefWindowProc(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        return lr;
    }
    if (msg == WM_DESTROY) {
        SetWindowLong(hwnd, GWL_WNDPROC, (LONG)lpfnOld);
        RemoveProp(hwnd, PROP_OLDLO);
        RemoveProp(hwnd, PROP_OLDHI);
    }
    return CallWindowProc(lpfnOld, hwnd, msg, wParam, lParam);
}

/* The name box shows the long name of the file chosen in the list */
static void ClassicLongEdit(HWND hDlg, LPCLASSIC c)
{
    char szName[LFN_MAXPATH];
    LPCSTR lpLong;

    GetDlgItemText(hDlg, edt1, szName, sizeof(szName));
    if (!szName[0] || _fstrchr(szName, '\\') || _fstrchr(szName, ':') || HasWildcards(szName))
        return;
    ClassicNames(c);
    lpLong = ClassicLong(c, szName);
    if (lpLong != (LPCSTR)szName && lstrcmp(lpLong, szName) != 0) {
        SetDlgItemText(hDlg, edt1, lpLong);
        SendDlgItemMessage(hDlg, edt1, EM_SETSEL, 0, MAKELPARAM(0, -1));
    }
}

/*
 * OK: a long name in the name box becomes its 8.3 path before COMMDLG
 * checks it. Existing files and folders by their alias; for Save As a new
 * long name is created first (with the default extension), so it has one.
 */
static void ClassicConvert(HWND hDlg, LPCLASSIC c, CLASSICREG *r)
{
    char szText[LFN_MAXPATH], szShort[SPATH], szDir[SPATH], szName[LFN_MAXNAME];
    LPSTR lpName, p;
    WORD wAttr;
    BOOL bAllShort = TRUE;

    if (r->bOverwriteCleared) {         /* back for a second try */
        r->ofn->Flags |= OFN_OVERWRITEPROMPT;
        r->bOverwriteCleared = FALSE;
    }
    GetDlgItemText(hDlg, edt1, szText, sizeof(szText));
    p = szText;
    while (*p == ' ')
        p++;
    if (p != szText)
        lstrcpy(szText, p);
    if (!szText[0] || HasWildcards(szText) || _fstrchr(szText, '"'))
        return;
    CurrentDir(szDir);
    /* only names that COMMDLG cannot handle */
    for (p = szText; *p; ) {
        char szPart[LFN_MAXNAME];
        int n = 0;
        while (*p && *p != '\\' && n < (int)sizeof(szPart) - 1)
            szPart[n++] = *p++;
        szPart[n] = '\0';
        if (*p)
            p++;
        if (n && !(n == 2 && szPart[1] == ':') && lstrcmp(szPart, ".") && lstrcmp(szPart, "..")
            && !LfnIsShortName(szPart))
            bAllShort = FALSE;
    }
    if (bAllShort)
        return;
    /* the full long path */
    if (szText[0] && szText[1] == ':')
        lstrcpy(c->szWork, szText);
    else if (szText[0] == '\\')
        wsprintf(c->szWork, "%c:%s", szDir[0], (LPSTR)szText);
    else {
        lstrcpy(c->szWork, szDir);
        CatPath(c->szWork, szText, sizeof(c->szWork));
    }
    if (!LfnDrive(c->szWork))
        return;
    /* several names (multiple selection): COMMDLG's business */
    if ((r->ofn->Flags & OFN_ALLOWMULTISELECT) && _fstrchr(szText, ' ')
        && LfnGetAttr(c->szWork, &wAttr) != 0)
        return;
    if (LfnGetAttr(c->szWork, &wAttr) == 0 && LfnShortPath(c->szWork, szShort, sizeof(szShort))) {
        SetDlgItemText(hDlg, edt1, szShort);
        return;
    }
    /* not there: its folder must be, and for Save As it is created */
    lstrcpy(c->szWork2, c->szWork);
    lpName = FileNameOf(c->szWork2);
    lstrcpyn(szName, lpName, sizeof(szName));
    if (lpName > (LPSTR)c->szWork2)
        lpName[lpName - 1 > (LPSTR)c->szWork2 + 2 ? -1 : 0] = '\0';
    if (!LfnShortPath(c->szWork2, szDir, sizeof(szDir)))
        return;                         /* COMMDLG says the path is wrong */
    if (!_fstrchr(szName, '.') && r->ofn->lpstrDefExt && *r->ofn->lpstrDefExt
        && lstrlen(szName) + 4 < (int)sizeof(szName)) {
        /* "Report" is "Report.wri": COMMDLG would add it to the alias */
        BOOL bLower = FALSE;
        for (p = szName; *p; p++)
            if (IsCharLower(*p))
                bLower = TRUE;
        lstrcat(szName, ".");
        p = szName + lstrlen(szName);
        lstrcat(szName, r->ofn->lpstrDefExt);
        if (bLower)
            AnsiLower(p);
        if (LfnShortOf(szDir, szName, szShort) == 0) {
            lstrcpy(c->szWork, szDir);
            CatPath(c->szWork, szShort, sizeof(c->szWork));
            SetDlgItemText(hDlg, edt1, c->szWork);
            return;
        }
    }
    if (!r->bSave || !LfnValidName(szName, TRUE))
        return;                         /* COMMDLG reports it */
    lstrcpy(c->szWork, szDir);
    CatPath(c->szWork, szName, sizeof(c->szWork));
    if (LfnCreateEmpty(c->szWork, szShort, sizeof(szShort)) != 0)
        return;
    AnsiUpper(szShort);
    lstrcpy(r->szCreated, szShort);
    /* the file is ours, new: COMMDLG must not ask to replace it */
    if (r->ofn->Flags & OFN_OVERWRITEPROMPT) {
        r->ofn->Flags &= ~OFN_OVERWRITEPROMPT;
        r->bOverwriteCleared = TRUE;
    }
    SetDlgItemText(hDlg, edt1, szShort);
}

UINT CALLBACK __export NsClassicHook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CLASSICREG *r = RegOf(hDlg);
    LPCLASSIC c = ClassicOf(hDlg);
    UINT uRet;

    switch (msg) {
    case WM_INITDIALOG:
        {
            int i;
            HGLOBAL h;
            HWND hwndPath;
            for (i = 0; i < MAX_CLASSIC; i++)
                if (s_reg[i].ofn == (LPOPENFILENAME)lParam)
                    break;
            if (i == MAX_CLASSIC)
                i = s_nPending;
            if (i < 0)
                return TRUE;
            r = &s_reg[i];
            if (s_nPending == i)
                s_nPending = -1;
            h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(CLASSIC));
            if (h) {
                c = (LPCLASSIC)GlobalLock(h);
                c->nReg = i;
                c->hPool = GlobalAlloc(GMEM_MOVEABLE, CLASSIC_POOL);
                if (c->hPool)
                    c->lpPool = (LPSTR)GlobalLock(c->hPool);
                SetProp(hDlg, PROP_CLASSIC, (HANDLE)FP_SEG(c));
                hwndPath = GetDlgItem(hDlg, stc1);
                if (hwndPath) {
                    LONG l = GetWindowLong(hwndPath, GWL_WNDPROC);
                    SetProp(hwndPath, PROP_OLDLO, (HANDLE)LOWORD(l));
                    SetProp(hwndPath, PROP_OLDHI, (HANDLE)HIWORD(l));
                    SetWindowLong(hwndPath, GWL_WNDPROC, (LONG)(FARPROC)NsClassicStaticProc);
                }
            }
            return r->lpfnHook ? r->lpfnHook(hDlg, msg, wParam, lParam) : TRUE;
        }
    case WM_DRAWITEM:
        if (r && r->lpfnHook && (uRet = r->lpfnHook(hDlg, msg, wParam, lParam)) != 0)
            return uRet;
        if (c && ClassicDrawItem(hDlg, c, (DRAWITEMSTRUCT FAR *)lParam))
            return TRUE;
        return FALSE;
    case WM_COMMAND:
        if (c && r && wParam == IDOK)
            ClassicConvert(hDlg, c, r);
        else if (c && wParam == lst1 && HIWORD(lParam) == LBN_SELCHANGE && r
                 && !(r->ofn->Flags & OFN_ALLOWMULTISELECT))
            PostMessage(hDlg, WM_NS_LONGEDIT, 0, 0L);
        break;
    case WM_NS_LONGEDIT:
        if (c)
            ClassicLongEdit(hDlg, c);
        return TRUE;
    case WM_DESTROY:
        if (c) {
            HGLOBAL h = (HGLOBAL)GlobalHandle(FP_SEG(c));
            uRet = r && r->lpfnHook ? r->lpfnHook(hDlg, msg, wParam, lParam) : FALSE;
            RemoveProp(hDlg, PROP_CLASSIC);
            if (c->hPool) {
                GlobalUnlock(c->hPool);
                GlobalFree(c->hPool);
            }
            GlobalUnlock(h);
            GlobalFree(h);
            return uRet;
        }
        break;
    }
    return r && r->lpfnHook ? r->lpfnHook(hDlg, msg, wParam, lParam) : FALSE;
}

/* COMMDLG's dialog for a program with a hook or template, with NSDLG's
   hook in front of the program's */
static BOOL RunClassic(int n, LPOPENFILENAME lpofn)
{
    CLASSICREG *r = NULL;
    HINSTANCE hSelf;
    DWORD dwHookFlag = lpofn->Flags & OFN_ENABLEHOOK;
    CLASSICHOOK lpfnHook = (CLASSICHOOK)lpofn->lpfnHook;
    int i;
    BOOL bRet;

    if (!LfnActive())
        return CallOriginal(n, lpofn);
    for (i = 0; i < MAX_CLASSIC; i++)
        if (!s_reg[i].ofn) {
            r = &s_reg[i];
            break;
        }
    if (!r)
        return CallOriginal(n, lpofn);
    _fmemset(r, 0, sizeof(*r));
    r->ofn = lpofn;
    r->bSave = n == 1;
    r->lpfnHook = dwHookFlag ? lpfnHook : NULL;
    s_nPending = i;
    lpofn->Flags |= OFN_ENABLEHOOK;
    lpofn->lpfnHook = (LPOFNHOOKPROC)NsClassicHook;
    hSelf = LoadLibrary("NSDLG.DLL");
    s_nRunning++;
    bRet = CallOriginal(n, lpofn);
    s_nRunning--;
    /* the program gets its structure back as it gave it */
    lpofn->lpfnHook = (LPOFNHOOKPROC)lpfnHook;
    lpofn->Flags = (lpofn->Flags & ~OFN_ENABLEHOOK) | dwHookFlag;
    if (r->bOverwriteCleared)
        lpofn->Flags |= OFN_OVERWRITEPROMPT;
    /* a file made for a long name that was not chosen in the end */
    if (r->szCreated[0] && (!bRet || !lpofn->lpstrFile
                            || lstrcmpi(lpofn->lpstrFile, r->szCreated) != 0)
        && IsEmptyFile(r->szCreated))
        LfnDelete(r->szCreated);
    if (s_nPending == i)
        s_nPending = -1;
    r->ofn = NULL;
    if ((UINT)hSelf > 32)
        FreeLibrary(hSelf);
    return bRet;
}

/* Programs that rely on COMMDLG's own dialog get it (with long names
   when they bring a hook or template: RunClassic) */
static BOOL NeedsOriginal(LPOPENFILENAME lpofn)
{
    return s_nLook == NSDLG_OFF || !lpofn || lpofn->lStructSize != sizeof(OPENFILENAME);
}

static BOOL NeedsClassic(LPOPENFILENAME lpofn)
{
    return (lpofn->Flags & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE)) != 0;
}

static BOOL RunDialog(BOOL bSave, LPOPENFILENAME lpofn)
{
    HGLOBAL hState, hItems, hPool;
    HINSTANCE hSelf;
    LPFDLG d;
    int nRet;

    if (NeedsOriginal(lpofn))
        return CallOriginal(bSave ? 1 : 0, lpofn);
    if (NeedsClassic(lpofn))
        return RunClassic(bSave ? 1 : 0, lpofn);
    hState = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(FDLG));
    hItems = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)MAX_ITEMS * sizeof(DITEM));
    hPool = GlobalAlloc(GMEM_MOVEABLE, POOL_SIZE);
    if (!hState || !hItems || !hPool) {
        if (hState)
            GlobalFree(hState);
        if (hItems)
            GlobalFree(hItems);
        if (hPool)
            GlobalFree(hPool);
        return CallOriginal(bSave ? 1 : 0, lpofn);
    }
    d = (LPFDLG)GlobalLock(hState);
    d->lpItems = (LPDITEM)GlobalLock(hItems);
    d->lpPool = (LPSTR)GlobalLock(hPool);
    d->ofn = lpofn;
    d->bSave = bSave;
    d->nLook = s_nLook;
    /* the DLL stays loaded while the dialog runs, whatever NewShell16 does */
    hSelf = LoadLibrary("NSDLG.DLL");
    s_nRunning++;
    nRet = DialogBoxParam(s_hInst, MAKEINTRESOURCE(d->nLook == NSDLG_95 ? IDD_NSFILE
                                                                       : IDD_NSFILESIZE),
                          lpofn->hwndOwner, (DLGPROC)NsFileDlgProc, (LPARAM)d);
    GlobalUnlock(hPool);
    GlobalFree(hPool);
    GlobalUnlock(hItems);
    GlobalFree(hItems);
    GlobalUnlock(hState);
    GlobalFree(hState);
    s_nRunning--;
    if (nRet == -1)
        nRet = CallOriginal(bSave ? 1 : 0, lpofn);
    if ((UINT)hSelf > 32)
        FreeLibrary(hSelf);
    return nRet == TRUE;
}

BOOL FAR PASCAL __export NsGetOpenFileName(void FAR *lpofn)
{
    return RunDialog(FALSE, (LPOPENFILENAME)lpofn);
}

BOOL FAR PASCAL __export NsGetSaveFileName(void FAR *lpofn)
{
    return RunDialog(TRUE, (LPOPENFILENAME)lpofn);
}

/* Called by NewShell16 (at start-up and when the look or the options
   change); nLook NSDLG_OFF removes the patches */
BOOL FAR PASCAL __export NsDlgInstall(int nLook, WORD wFlags, LPCSTR lpIni)
{
    s_wFlags = wFlags;
    if (lpIni && *lpIni)
        lstrcpyn(s_szIni, lpIni, sizeof(s_szIni));
    LfnInit();                          /* in NewShell16's task, for good */
    LfnEnable((wFlags & NSDF_LONGNAMES) != 0);
    LfnForgetDrives();
    if (nLook == NSDLG_OFF) {
        s_nLook = NSDLG_OFF;
        PatchWrite(&s_patch[0], FALSE);
        PatchWrite(&s_patch[1], FALSE);
        return TRUE;
    }
    if (!RegisterClasses())
        return FALSE;
    if (!s_hCommDlg) {
        s_hCommDlg = LoadLibrary("COMMDLG.DLL");
        if ((UINT)s_hCommDlg <= 32) {
            s_hCommDlg = NULL;
            return FALSE;
        }
        /* ordinals 1 and 2: GetOpenFileName, GetSaveFileName */
        if (!PatchInit(&s_patch[0], 1, (FARPROC)NsGetOpenFileName)
            || !PatchInit(&s_patch[1], 2, (FARPROC)NsGetSaveFileName)) {
            PatchFree(&s_patch[0]);
            PatchFree(&s_patch[1]);
            FreeLibrary(s_hCommDlg);
            s_hCommDlg = NULL;
            return FALSE;
        }
    }
    s_nLook = nLook;
    if (!s_patch[0].nOriginal)
        PatchWrite(&s_patch[0], TRUE);
    if (!s_patch[1].nOriginal)
        PatchWrite(&s_patch[1], TRUE);
    return TRUE;
}

void FAR PASCAL __export NsDlgRemove(void)
{
    s_nLook = NSDLG_OFF;
    PatchFree(&s_patch[0]);
    PatchFree(&s_patch[1]);
    if (s_hCommDlg) {
        FreeLibrary(s_hCommDlg);
        s_hCommDlg = NULL;
    }
    if (s_hbmSmall) {
        DeleteObject(s_hbmSmall);
        DeleteObject(s_hbmMask);
        s_hbmSmall = s_hbmMask = NULL;
    }
    /* the DOS buffer belongs to NewShell16's task; a dialog still open
       elsewhere keeps using it (the task ends, Windows frees it) */
    if (!s_nRunning)
        LfnTerm();
}

int CALLBACK LibMain(HINSTANCE hInst, WORD wDS, WORD cbHeap, LPSTR lpCmd)
{
    (void)wDS;
    (void)cbHeap;
    (void)lpCmd;
    s_hInst = hInst;
    return 1;
}

int FAR PASCAL __export WEP(int nExit)
{
    (void)nExit;
    PatchFree(&s_patch[0]);
    PatchFree(&s_patch[1]);
    return 1;
}
