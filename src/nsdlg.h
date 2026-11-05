/*
 * NewShell16 - a Calmira-style shell for Windows 3.1
 * Copyright (C) 2026 NewShell16 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * nsdlg.h - NSDLG.DLL, the Open and Save As dialogs of Windows 95, XP and
 * 11 for every Windows 3.1 program.
 *
 * NEWSHELL.EXE loads the DLL and calls NsDlgInstall(): it writes a JMP to
 * its own dialog over the entry points of COMMDLG's GetOpenFileName and
 * GetSaveFileName, so programs get the new dialog without any change.
 * The dialog shows long file names where an LFN driver has them, but
 * returns 8.3 names (the program only knows those). Programs that bring
 * their own template or hook procedure get the original dialog.
 */
#ifndef NSDLG_H
#define NSDLG_H

/* looks */
#define NSDLG_OFF       0       /* not installed: the Windows 3.1 dialogs */
#define NSDLG_95        1
#define NSDLG_XP        2
#define NSDLG_11        3

/* flags */
#define NSDF_LONGNAMES  0x0001  /* use long names where there are any */

typedef BOOL (FAR PASCAL *NSDLGINSTALLPROC)(int nLook, WORD wFlags, LPCSTR lpIni);
typedef void (FAR PASCAL *NSDLGREMOVEPROC)(void);

/* exported by NSDLG.DLL */
BOOL FAR PASCAL NsDlgInstall(int nLook, WORD wFlags, LPCSTR lpIni);
void FAR PASCAL NsDlgRemove(void);
BOOL FAR PASCAL NsGetOpenFileName(void FAR *lpofn);
BOOL FAR PASCAL NsGetSaveFileName(void FAR *lpofn);

#endif
