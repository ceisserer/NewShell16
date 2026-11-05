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
 * lfn.c - long file names through the LFN API of INT 21h (71xxh)
 *
 * The functions are called in real mode (DPMI "simulate real mode
 * interrupt", INT 31h AX=0300h) with the strings in a buffer from
 * GlobalDosAlloc: Windows 3.1 only translates the pointers of the DOS
 * functions it knows, and an LFN driver loaded before Windows (DOSLFN,
 * LFNDOS) only exists in real mode. Under Windows 95 the same calls reach
 * IFSMgr. Compiled into NEWSHELL.EXE and NSDLG.DLL, each with its own
 * buffer.
 */
#define STRICT
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <direct.h>
#include <dos.h>
#include <i86.h>
#include "lfn.h"

/* the DOS buffer */
#define BUF_SIZE        1024
#define OFS_P1          0           /* first path */
#define OFS_P2          264         /* second path, or a result */
#define OFS_FIND        528         /* WIN32_FIND_DATA (318 bytes) */

/* the DPMI real mode call structure */
typedef struct tagRMCS {
    DWORD edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    WORD  flags, es, ds, fs, gs, ip, cs, sp, ss;
} RMCS;

static DWORD s_dwDos;               /* GlobalDosAlloc: segment:selector */
static BOOL  s_bPresent;
static BOOL  s_bOn = TRUE;
static BYTE  s_bDrive[26];          /* 0 unknown, 1 long names, 2 not */

#define BUFSEL          LOWORD(s_dwDos)
#define BUFSEG          HIWORD(s_dwDos)
#define BUFPTR(o)       ((LPSTR)MK_FP(BUFSEL, (o)))

/* INT 21h in real mode with the registers in r; DS and ES are the
   buffer's segment. Returns 0, or the DOS error (7100h: no LFN API) */
static WORD RealDos(RMCS *r)
{
    union REGS rg;
    struct SREGS sr;
    RMCS FAR *lp = (RMCS FAR *)r;

    r->ds = r->es = BUFSEG;
    r->fs = r->gs = 0;
    r->sp = r->ss = 0;              /* the DPMI host provides a stack */
    r->flags = 0x0001;              /* carry set: unsupported unless cleared */
    segread(&sr);
    rg.x.ax = 0x0300;
    rg.x.bx = 0x0021;
    rg.x.cx = 0;
    sr.es = FP_SEG(lp);
    rg.x.di = FP_OFF(lp);
    int86x(0x31, &rg, &rg, &sr);
    if (rg.x.cflag)
        return 0x7100;
    if ((WORD)r->eax == 0x7100)     /* DOS before 7: AL = 0 for AH = 71h */
        return 0x7100;
    if (r->flags & 0x0001)
        return (WORD)r->eax ? (WORD)r->eax : 0x7100;
    return 0;
}

static void ClearRegs(RMCS *r, WORD ax)
{
    memset(r, 0, sizeof(*r));
    r->eax = ax;
}

/* copies a path into the buffer at the offset */
static void PutPath(WORD ofs, LPCSTR lpPath)
{
    lstrcpyn(BUFPTR(ofs), lpPath, LFN_MAXPATH);
}

static int DriveIndex(LPCSTR lpPath)
{
    char c;
    if (lpPath && lpPath[0] && lpPath[1] == ':') {
        c = lpPath[0];
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
    }
    return _getdrive() - 1;
}

/* Does the drive (0 = A:) have long names? 71A0h: volume information */
static BOOL CheckDrive(int d)
{
    RMCS r;
    char szRoot[4];
    WORD w;

    wsprintf(szRoot, "%c:\\", 'A' + d);
    PutPath(OFS_P1, szRoot);
    ClearRegs(&r, 0x71A0);
    r.edx = OFS_P1;
    r.edi = OFS_P2;
    r.ecx = 32;
    if (RealDos(&r) != 0)
        return FALSE;
    w = (WORD)r.ebx;
    /* bit 14: the LFN functions are supported on the volume */
    return (w & 0x4000) != 0;
}

BOOL LfnInit(void)
{
    char szWin[144];
    int d;

    if (s_dwDos)
        return s_bPresent;
    s_dwDos = GlobalDosAlloc(BUF_SIZE);
    if (!s_dwDos)
        return FALSE;
    _fmemset(s_bDrive, 0, sizeof(s_bDrive));
    GetWindowsDirectory(szWin, sizeof(szWin));
    d = DriveIndex(szWin);
    s_bPresent = CheckDrive(d);
    s_bDrive[d] = (BYTE)(s_bPresent ? 1 : 2);
    if (!s_bPresent) {
        /* the Windows drive may be one without long names (network):
           an LFN driver still answers 71A0h for another drive */
        RMCS r;
        ClearRegs(&r, 0x71A0);
        PutPath(OFS_P1, "C:\\");
        r.edx = OFS_P1;
        r.edi = OFS_P2;
        r.ecx = 32;
        s_bPresent = RealDos(&r) == 0;
    }
    return s_bPresent;
}

void LfnTerm(void)
{
    if (s_dwDos) {
        GlobalDosFree(BUFSEL);
        s_dwDos = 0;
    }
    s_bPresent = FALSE;
}

void LfnEnable(BOOL bOn)
{
    s_bOn = bOn;
}

BOOL LfnPresent(void)
{
    return s_bPresent;
}

BOOL LfnActive(void)
{
    return s_bPresent && s_bOn && s_dwDos;
}

void LfnForgetDrives(void)
{
    _fmemset(s_bDrive, 0, sizeof(s_bDrive));
}

BOOL LfnDrive(LPCSTR lpPath)
{
    int d;

    if (!LfnActive())
        return FALSE;
    if (lpPath && lpPath[0] == '\\' && lpPath[1] == '\\')
        return FALSE;               /* network paths: never */
    d = DriveIndex(lpPath);
    if (d < 0 || d >= 26)
        return FALSE;
    if (!s_bDrive[d]) {
        UINT uOld = SetErrorMode(1);    /* SEM_FAILCRITICALERRORS */
        BOOL bOk = CheckDrive(d);
        SetErrorMode(uOld);
        if (bOk)
            s_bDrive[d] = 1;
        else if (GetDriveType(d) != DRIVE_REMOVABLE)
            s_bDrive[d] = 2;        /* a floppy is asked again next time */
        return bOk;
    }
    return s_bDrive[d] == 1;
}

/* ---- names ---------------------------------------------------------- */

static BOOL IsShortChar(char c)
{
    if ((BYTE)c < 0x21)
        return FALSE;
    return _fstrchr("\"*+,./:;<=>?[\\]|", c) == NULL;
}

BOOL LfnIsShortName(LPCSTR lpName)
{
    int n = 0, e = -1;

    if (!lpName || !*lpName)
        return FALSE;
    if (lstrcmp(lpName, ".") == 0 || lstrcmp(lpName, "..") == 0)
        return TRUE;
    for (; *lpName; lpName++) {
        if (*lpName == '.') {
            if (e >= 0 || n == 0)
                return FALSE;
            e = 0;
            continue;
        }
        if (!IsShortChar(*lpName))
            return FALSE;
        if (e >= 0) {
            if (++e > 3)
                return FALSE;
        } else if (++n > 8)
            return FALSE;
    }
    return e != 0;                  /* "NAME." is not one */
}

BOOL LfnValidName(LPCSTR lpName, BOOL bLong)
{
    LPCSTR p;
    BOOL bOther = FALSE;

    if (!bLong)
        return LfnIsShortName(lpName);
    if (!lpName || !*lpName || lstrlen(lpName) >= LFN_MAXNAME - 1)
        return FALSE;
    for (p = lpName; *p; p++) {
        if ((BYTE)*p < 0x20 || _fstrchr("\\/:*?\"<>|", *p))
            return FALSE;
        if (*p != '.' && *p != ' ')
            bOther = TRUE;
    }
    return bOther;
}

/* ---- searches -------------------------------------------------------- */

/* the WIN32_FIND_DATA in the buffer to lpf */
static void TakeFind(LPLFNFIND lpf)
{
    BYTE FAR *b = (BYTE FAR *)BUFPTR(OFS_FIND);

    lpf->bAttr  = b[0];
    lpf->wTime  = *(WORD FAR *)(b + 20);    /* last write, DOS format */
    lpf->wDate  = *(WORD FAR *)(b + 22);
    lpf->dwSize = *(DWORD FAR *)(b + 32);
    lstrcpyn(lpf->szName, (LPCSTR)(b + 44), LFN_MAXNAME);
    lstrcpyn(lpf->szShort, (LPCSTR)(b + 304), sizeof(lpf->szShort));
    if (!lpf->szShort[0]) {
        /* the name is an 8.3 name itself */
        lstrcpyn(lpf->szShort, lpf->szName, sizeof(lpf->szShort));
        AnsiUpper(lpf->szShort);
    }
}

static void TakeDos(LPLFNFIND lpf)
{
    struct find_t FAR *ft = (struct find_t FAR *)lpf->dta;

    lpf->bAttr  = ft->attrib;
    lpf->wTime  = ft->wr_time;
    lpf->wDate  = ft->wr_date;
    lpf->dwSize = ft->size;
    lstrcpyn(lpf->szShort, ft->name, sizeof(lpf->szShort));
    lstrcpyn(lpf->szName, ft->name, sizeof(lpf->szName));
}

int LfnFindFirst(LPCSTR lpPattern, WORD wAttr, LPLFNFIND lpf)
{
    struct find_t ft;
    unsigned rc;

    lpf->hFind = 0xFFFF;
    lpf->bLfn = 0;
    if (LfnDrive(lpPattern)) {
        RMCS r;
        WORD err;
        PutPath(OFS_P1, lpPattern);
        ClearRegs(&r, 0x714E);
        r.ecx = wAttr & 0xFF;       /* CH = 0: no attributes required */
        r.esi = 1;                  /* DOS date and time */
        r.edx = OFS_P1;
        r.edi = OFS_FIND;
        err = RealDos(&r);
        if (err == 0) {
            lpf->hFind = (WORD)r.eax;
            lpf->bLfn = 1;
            TakeFind(lpf);
            return 0;
        }
        if (err != 0x7100)
            return err;
        /* no LFN after all: the DOS search */
    }
    rc = _dos_findfirst(lpPattern, wAttr, &ft);
    _fmemcpy(lpf->dta, &ft, sizeof(lpf->dta) < sizeof(ft) ? sizeof(lpf->dta) : sizeof(ft));
    if (rc)
        return rc;
    TakeDos(lpf);
    return 0;
}

int LfnFindNext(LPLFNFIND lpf)
{
    struct find_t ft;
    unsigned rc;

    if (lpf->bLfn) {
        RMCS r;
        WORD err;
        if (lpf->hFind == 0xFFFF)
            return 18;
        ClearRegs(&r, 0x714F);
        r.ebx = lpf->hFind;
        r.esi = 1;
        r.edi = OFS_FIND;
        err = RealDos(&r);
        if (err)
            return err;
        TakeFind(lpf);
        return 0;
    }
    _fmemcpy(&ft, lpf->dta, sizeof(lpf->dta) < sizeof(ft) ? sizeof(lpf->dta) : sizeof(ft));
    rc = _dos_findnext(&ft);
    _fmemcpy(lpf->dta, &ft, sizeof(lpf->dta) < sizeof(ft) ? sizeof(lpf->dta) : sizeof(ft));
    if (rc)
        return rc;
    TakeDos(lpf);
    return 0;
}

void LfnFindClose(LPLFNFIND lpf)
{
    if (lpf->bLfn && lpf->hFind != 0xFFFF) {
        RMCS r;
        ClearRegs(&r, 0x71A1);
        r.ebx = lpf->hFind;
        RealDos(&r);
    }
    lpf->hFind = 0xFFFF;
    lpf->bLfn = 0;
}

/* ---- paths ----------------------------------------------------------- */

/* 7160h: CL = 1 short, 2 long */
static BOOL TruePath(LPCSTR lpPath, LPSTR lpOut, int cbOut, BYTE bHow)
{
    RMCS r;
    int n;

    if (!LfnDrive(lpPath)) {
        lstrcpyn(lpOut, lpPath, cbOut);
        return FALSE;
    }
    PutPath(OFS_P1, lpPath);
    ClearRegs(&r, 0x7160);
    r.ecx = bHow;                   /* CH = 0: SUBST drives as such */
    r.esi = OFS_P1;
    r.edi = OFS_P2;
    if (RealDos(&r) != 0) {
        lstrcpyn(lpOut, lpPath, cbOut);
        return FALSE;
    }
    if (lstrlen(BUFPTR(OFS_P2)) >= cbOut) {
        lstrcpyn(lpOut, lpPath, cbOut);
        return FALSE;
    }
    lstrcpy(lpOut, BUFPTR(OFS_P2));
    /* "C:\DIR\" from "C:\DIR\" only; else no trailing backslash */
    n = lstrlen(lpOut);
    if (n > 3 && lpOut[n - 1] == '\\' && lpPath[lstrlen(lpPath) - 1] != '\\')
        lpOut[n - 1] = '\0';
    return TRUE;
}

BOOL LfnShortPath(LPCSTR lpPath, LPSTR lpOut, int cbOut)
{
    if (!TruePath(lpPath, lpOut, cbOut, 1))
        return FALSE;
    AnsiUpper(lpOut);
    return TRUE;
}

BOOL LfnLongPath(LPCSTR lpPath, LPSTR lpOut, int cbOut)
{
    return TruePath(lpPath, lpOut, cbOut, 2);
}

BOOL LfnLongName(LPCSTR lpPath, LPSTR lpOut, int cbOut)
{
    char szLong[LFN_MAXPATH];
    LPSTR p;
    BOOL bOk = LfnLongPath(lpPath, szLong, sizeof(szLong));

    p = _fstrrchr(szLong, '\\');
    if (p && p[1])
        p++;
    else if (!p)
        p = szLong;
    lstrcpyn(lpOut, p, cbOut);
    return bOk;
}

void LfnDisplayPath(LPCSTR lpPath, LPSTR lpOut, int cbOut)
{
    if (!LfnLongPath(lpPath, lpOut, cbOut))
        lstrcpyn(lpOut, lpPath, cbOut);
}

/* ---- file operations --------------------------------------------------- */

static int Simple(WORD ax, LPCSTR lpPath, DWORD ecx, DWORD ebx)
{
    RMCS r;
    PutPath(OFS_P1, lpPath);
    ClearRegs(&r, ax);
    r.edx = OFS_P1;
    r.ecx = ecx;
    r.ebx = ebx;
    return RealDos(&r);
}

int LfnRename(LPCSTR lpFrom, LPCSTR lpTo)
{
    if (LfnDrive(lpFrom)) {
        RMCS r;
        WORD err;
        PutPath(OFS_P1, lpFrom);
        PutPath(OFS_P2, lpTo);
        ClearRegs(&r, 0x7156);
        r.edx = OFS_P1;
        r.edi = OFS_P2;
        err = RealDos(&r);
        if (err != 0x7100)
            return err;
    }
    {
        char szFrom[LFN_MAXPATH], szTo[LFN_MAXPATH];
        lstrcpyn(szFrom, lpFrom, sizeof(szFrom));
        lstrcpyn(szTo, lpTo, sizeof(szTo));
        return rename(szFrom, szTo) == 0 ? 0 : 5;
    }
}

int LfnMkDir(LPCSTR lpPath)
{
    char sz[LFN_MAXPATH];

    if (LfnDrive(lpPath)) {
        WORD err = Simple(0x7139, lpPath, 0, 0);
        if (err != 0x7100)
            return err;
    }
    lstrcpyn(sz, lpPath, sizeof(sz));
    return mkdir(sz) == 0 ? 0 : 5;
}

int LfnRmDir(LPCSTR lpPath)
{
    char sz[LFN_MAXPATH];

    if (LfnDrive(lpPath)) {
        WORD err = Simple(0x713A, lpPath, 0, 0);
        if (err != 0x7100)
            return err;
    }
    lstrcpyn(sz, lpPath, sizeof(sz));
    return rmdir(sz) == 0 ? 0 : 5;
}

int LfnDelete(LPCSTR lpPath)
{
    char sz[LFN_MAXPATH];

    if (LfnDrive(lpPath)) {
        RMCS r;
        WORD err;
        PutPath(OFS_P1, lpPath);
        ClearRegs(&r, 0x7141);
        r.edx = OFS_P1;
        r.esi = 0;                  /* no wildcards */
        r.ecx = 0x0027;             /* normal, read-only, hidden, system, archive */
        err = RealDos(&r);
        if (err != 0x7100)
            return err;
    }
    lstrcpyn(sz, lpPath, sizeof(sz));
    return remove(sz) == 0 ? 0 : 5;
}

int LfnGetAttr(LPCSTR lpPath, WORD FAR *pwAttr)
{
    unsigned u;
    char sz[LFN_MAXPATH];

    if (LfnDrive(lpPath)) {
        RMCS r;
        WORD err;
        PutPath(OFS_P1, lpPath);
        ClearRegs(&r, 0x7143);
        r.edx = OFS_P1;
        r.ebx = 0;
        err = RealDos(&r);
        if (err == 0) {
            *pwAttr = (WORD)r.ecx;
            return 0;
        }
        if (err != 0x7100)
            return err;
    }
    lstrcpyn(sz, lpPath, sizeof(sz));
    if (_dos_getfileattr(sz, &u) != 0)
        return 2;
    *pwAttr = (WORD)u;
    return 0;
}

int LfnSetAttr(LPCSTR lpPath, WORD wAttr)
{
    char sz[LFN_MAXPATH];

    if (LfnDrive(lpPath)) {
        WORD err = Simple(0x7143, lpPath, wAttr, 1);
        if (err != 0x7100)
            return err;
    }
    lstrcpyn(sz, lpPath, sizeof(sz));
    return _dos_setfileattr(sz, wAttr) == 0 ? 0 : 5;
}

/* Creates an empty file under its long name and returns the short path
   (for programs that only know 8.3 names). Fails if it exists. */
int LfnCreateEmpty(LPCSTR lpPath, LPSTR lpShort, int cbShort)
{
    char sz[LFN_MAXPATH];
    int h;

    if (LfnDrive(lpPath)) {
        RMCS r;
        WORD err;
        PutPath(OFS_P1, lpPath);
        ClearRegs(&r, 0x716C);
        r.esi = OFS_P1;
        r.ebx = 0x0001;             /* write only */
        r.ecx = 0;                  /* normal attributes */
        r.edx = 0x0010;             /* create; fail if it exists */
        err = RealDos(&r);
        if (err == 0) {
            WORD hFile = (WORD)r.eax;
            ClearRegs(&r, 0x3E00);
            r.ebx = hFile;
            RealDos(&r);
            LfnShortPath(lpPath, lpShort, cbShort);
            return 0;
        }
        if (err != 0x7100)
            return err;
    }
    lstrcpyn(sz, lpPath, sizeof(sz));
    if (_dos_creatnew(sz, _A_NORMAL, &h) != 0)
        return 80;
    _dos_close(h);
    lstrcpyn(lpShort, lpPath, cbShort);
    AnsiUpper(lpShort);
    return 0;
}

/* The 8.3 name of the item lpLong (long or short) in lpDir */
int LfnShortOf(LPCSTR lpDir, LPCSTR lpLong, LPSTR lpShort)
{
    char sz[LFN_MAXPATH];
    LFNFIND f;
    int n, rc;

    lstrcpyn(sz, lpDir, sizeof(sz) - 1);
    n = lstrlen(sz);
    if (n && sz[n - 1] != '\\')
        lstrcat(sz, "\\");
    if (lstrlen(sz) + lstrlen(lpLong) >= (int)sizeof(sz))
        return 3;
    lstrcat(sz, lpLong);
    rc = LfnFindFirst(sz, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM
                          | _A_SUBDIR | _A_ARCH, &f);
    if (rc == 0)
        lstrcpy(lpShort, f.szShort);
    LfnFindClose(&f);
    return rc;
}
