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
 * nshook.h - interface of NSHOOK.DLL, the system hook library
 */
#ifndef NSHOOK_H
#define NSHOOK_H

/* Features passed to NsHookStart */
#define NSH_SHELL       0x0001  /* report top-level window creation/destruction */
#define NSH_MAXIMIZE    0x0002  /* keep maximised windows above the taskbar */
#define NSH_DESKTOP     0x0004  /* report right clicks on the desktop */
#define NSH_WINKEY      0x0008  /* report the Windows key */
#define NSH_FILES       0x0010  /* report file system changes (KERNEL.FileCdr) */

/* Messages posted to the notification window */
#define WM_NS_SHELLHOOK (WM_USER + 120)     /* wParam = HSHELL_*, lParam = hwnd */
#define WM_NS_DESKMENU  (WM_USER + 121)     /* lParam = screen position */
#define WM_NS_WINKEY    (WM_USER + 122)
#define WM_NS_FILECHANGE (WM_USER + 123)    /* call NsHookGetFileChange */
#define WM_NS_DESKDROP  (WM_USER + 124)     /* wParam = HDROP dropped on the desktop */

typedef UINT (FAR PASCAL *NSHOOKSTART)(HWND hwndNotify, int yLimit, UINT uFlags);
typedef void (FAR PASCAL *NSHOOKSTOP)(void);
typedef void (FAR PASCAL *NSHOOKSETLIMIT)(int yLimit);
typedef int  (FAR PASCAL *NSHOOKGETFILECHANGE)(LPSTR lpBuf, int cbBuf);

#endif
