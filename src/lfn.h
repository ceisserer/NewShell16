/*
 * NewShell16 - a Calmira-style shell for Windows 3.1
 * Copyright (C) 2026 NewShell16 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * lfn.h - long file names (the LFN API of INT 21h, functions 71xxh)
 *
 * Windows 95 (also for 16-bit programs and in DOS boxes), DOS 7 and the
 * DOS drivers LFNTSR, DOSLFN and LFNDOS provide the same set of INT 21h
 * functions for long file names. NewShell16 and NSDLG.DLL call them in real mode
 * through DPMI (INT 31h 0300h) with a buffer in conventional memory, as
 * Windows 3.1 does not translate their pointers in protected mode.
 *
 * Every function falls back to the plain DOS functions (8.3 names) when
 * no LFN driver answers, when the drive has no long names (network
 * drives, CD-ROMs...) or when long names are switched off, so callers
 * need no second code path. Paths given to Windows and programs stay
 * the short ones; long names are for showing and for naming things.
 */
#ifndef LFN_H
#define LFN_H

#define LFN_MAXPATH     260         /* a long path, with the NUL */
#define LFN_MAXNAME     256         /* a long name, with the NUL */

typedef struct tagLFNFIND {
    char  szName[LFN_MAXNAME];      /* long name (the short one without LFN) */
    char  szShort[14];              /* 8.3 name */
    BYTE  bAttr;
    DWORD dwSize;
    WORD  wTime, wDate;             /* DOS format */
    /* search state */
    WORD  hFind;                    /* LFN search handle, 0xFFFF = none */
    BYTE  bLfn;                     /* searching with the LFN functions */
    BYTE  dta[43];                  /* the DOS search (struct find_t) */
} LFNFIND, FAR *LPLFNFIND;

BOOL LfnInit(void);                 /* allocates the DOS buffer */
void LfnTerm(void);
void LfnEnable(BOOL bOn);           /* NEWSHELL.INI [Folders] LongNames= */
BOOL LfnPresent(void);              /* a driver answers (whatever the setting) */
BOOL LfnActive(void);               /* present and switched on */
BOOL LfnDrive(LPCSTR lpPath);       /* long names on the drive of the path */
void LfnForgetDrives(void);         /* drives may have changed */

/* searches: 0 = found; always end with LfnFindClose */
int  LfnFindFirst(LPCSTR lpPattern, WORD wAttr, LPLFNFIND lpf);
int  LfnFindNext(LPLFNFIND lpf);
void LfnFindClose(LPLFNFIND lpf);

/* path conversions; without LFN the path is copied. FALSE: not found */
BOOL LfnShortPath(LPCSTR lpPath, LPSTR lpOut, int cbOut);
BOOL LfnLongPath(LPCSTR lpPath, LPSTR lpOut, int cbOut);
BOOL LfnLongName(LPCSTR lpPath, LPSTR lpOut, int cbOut);  /* last part only */
void LfnDisplayPath(LPCSTR lpPath, LPSTR lpOut, int cbOut); /* long, or as is */

/* file operations (0 = done, else a DOS error code) */
int  LfnRename(LPCSTR lpFrom, LPCSTR lpTo);
int  LfnMkDir(LPCSTR lpPath);
int  LfnRmDir(LPCSTR lpPath);
int  LfnDelete(LPCSTR lpPath);
int  LfnGetAttr(LPCSTR lpPath, WORD FAR *pwAttr);
int  LfnSetAttr(LPCSTR lpPath, WORD wAttr);
int  LfnCreateEmpty(LPCSTR lpPath, LPSTR lpShort, int cbShort);

/* names */
BOOL LfnIsShortName(LPCSTR lpName);         /* a valid 8.3 name */
BOOL LfnValidName(LPCSTR lpName, BOOL bLong);
int  LfnShortOf(LPCSTR lpDir, LPCSTR lpLong, LPSTR lpShort);  /* 0 = found */

#endif
