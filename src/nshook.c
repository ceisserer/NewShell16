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
 * nshook.c - NSHOOK.DLL, system-wide hooks for NewShell16
 *
 * System hooks in Windows 3.1 must live in a DLL. Like Calmira's
 * WNDHOOKS.DLL this library provides:
 *
 *  - a shell hook reporting top-level windows being created and destroyed,
 *    so the taskbar updates at once;
 *  - a WH_CALLWNDPROC hook that reduces the maximised size of top-level
 *    windows (WM_GETMINMAXINFO) so they do not cover the taskbar;
 *  - a subclass of the desktop window reporting right clicks;
 *  - a keyboard hook for the Windows key (VK 0xFF under Windows 3.1);
 *  - KERNEL's undocumented file change callback (FileCdr), queued in a
 *    small ring buffer so the shell can refresh folder windows.
 */
#define STRICT
#include <windows.h>
#include "nshook.h"

#define RING_SIZE       16
#define RING_PATHLEN    128

typedef struct tagHOOKMSG {     /* lParam of a Win16 WH_CALLWNDPROC hook */
    LPARAM lParam;
    WPARAM wParam;
    UINT   uMsg;
    HWND   hwnd;
} HOOKMSG, FAR *LPHOOKMSG;

/* KERNEL.130: registers the file change callback (NULL to remove) */
extern BOOL FAR PASCAL FileCdr(FARPROC lpfnCallback);

static HINSTANCE s_hInst;
static HWND      s_hwndNotify;
static UINT      s_uFlags;
static int       s_yLimit;
static HHOOK     s_hShellHook;
static HHOOK     s_hWndHook;
static HHOOK     s_hKeyHook;
static WNDPROC   s_lpfnOldDesk;
static BOOL      s_bFileCdr;

static char      s_szRing[RING_SIZE][RING_PATHLEN];
static WORD      s_wRingAction[RING_SIZE];
static int       s_nHead, s_nTail;
static BOOL      s_bPosted;

int FAR PASCAL LibMain(HINSTANCE hInst, WORD wDataSeg, WORD cbHeap, LPSTR lpCmd)
{
    (void)wDataSeg;
    (void)cbHeap;
    (void)lpCmd;
    s_hInst = hInst;
    return 1;
}

int FAR PASCAL __export WEP(int nParam)
{
    (void)nParam;
    return 1;
}

LRESULT CALLBACK __export NsShellProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if ((nCode == HSHELL_WINDOWCREATED || nCode == HSHELL_WINDOWDESTROYED)
        && s_hwndNotify)
        PostMessage(s_hwndNotify, WM_NS_SHELLHOOK, nCode, (LPARAM)(UINT)wParam);
    return CallNextHookEx(s_hShellHook, nCode, wParam, lParam);
}

LRESULT CALLBACK __export NsWndProcHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && (s_uFlags & NSH_MAXIMIZE) && s_yLimit > 0) {
        LPHOOKMSG lpMsg = (LPHOOKMSG)lParam;

        if (lpMsg->uMsg == WM_GETMINMAXINFO
            && !(GetWindowLong(lpMsg->hwnd, GWL_STYLE) & WS_CHILD)) {
            MINMAXINFO FAR *lpmmi = (MINMAXINFO FAR *)lpMsg->lParam;
            int cyScreen = GetSystemMetrics(SM_CYSCREEN);

            /* only touch the default, full-screen maximised size */
            if (lpmmi->ptMaxSize.y >= cyScreen && lpmmi->ptMaxPosition.y <= 0)
                lpmmi->ptMaxSize.y -= cyScreen - s_yLimit;
        }
    }
    return CallNextHookEx(s_hWndHook, nCode, wParam, lParam);
}

LRESULT CALLBACK __export NsKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    /* key down (bit 31 clear), not a repeat (bit 30 clear) */
    if (nCode >= 0 && (wParam == 0xFF || wParam == 0x5B || wParam == 0x5C)
        && !(lParam & 0xC0000000L) && s_hwndNotify) {
        if (!(GetSystemDebugState() & (SDS_MENU | SDS_SYSMODAL)))
            PostMessage(s_hwndNotify, WM_NS_WINKEY, 0, 0L);
        return 1;
    }
    return CallNextHookEx(s_hKeyHook, nCode, wParam, lParam);
}

LRESULT CALLBACK __export NsDesktopProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam)
{
    if (msg == WM_RBUTTONUP && s_hwndNotify) {
        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        ClientToScreen(hwnd, &pt);
        PostMessage(s_hwndNotify, WM_NS_DESKMENU, 0, MAKELPARAM(pt.x, pt.y));
    }
    return CallWindowProc(s_lpfnOldDesk, hwnd, msg, wParam, lParam);
}

void FAR PASCAL __export NsFileCdrProc(WORD wAction, LPSTR lpPath)
{
    int nNext = (s_nHead + 1) % RING_SIZE;

    if (nNext != s_nTail) {
        lstrcpyn(s_szRing[s_nHead], lpPath, RING_PATHLEN);
        s_wRingAction[s_nHead] = wAction;
        s_nHead = nNext;
    }
    if (!s_bPosted && s_hwndNotify) {
        s_bPosted = TRUE;
        PostMessage(s_hwndNotify, WM_NS_FILECHANGE, 0, 0L);
    }
}

/* Pops one queued change; returns its action code or -1 if none */
int FAR PASCAL __export NsHookGetFileChange(LPSTR lpBuf, int cbBuf)
{
    int nAction;

    s_bPosted = FALSE;
    if (s_nTail == s_nHead)
        return -1;
    lstrcpyn(lpBuf, s_szRing[s_nTail], cbBuf);
    nAction = s_wRingAction[s_nTail];
    s_nTail = (s_nTail + 1) % RING_SIZE;
    return nAction;
}

void FAR PASCAL __export NsHookSetLimit(int yLimit)
{
    s_yLimit = yLimit;
}

void FAR PASCAL __export NsHookStop(void)
{
    if (s_hShellHook) {
        UnhookWindowsHookEx(s_hShellHook);
        s_hShellHook = NULL;
    }
    if (s_hWndHook) {
        UnhookWindowsHookEx(s_hWndHook);
        s_hWndHook = NULL;
    }
    if (s_hKeyHook) {
        UnhookWindowsHookEx(s_hKeyHook);
        s_hKeyHook = NULL;
    }
    if (s_lpfnOldDesk) {
        SetWindowLong(GetDesktopWindow(), GWL_WNDPROC, (LONG)s_lpfnOldDesk);
        s_lpfnOldDesk = NULL;
    }
    if (s_bFileCdr) {
        FileCdr(NULL);
        s_bFileCdr = FALSE;
    }
    s_hwndNotify = NULL;
}

/* Installs the requested hooks; returns the features actually enabled */
UINT FAR PASCAL __export NsHookStart(HWND hwndNotify, int yLimit, UINT uFlags)
{
    UINT uDone = 0;

    NsHookStop();
    s_hwndNotify = hwndNotify;
    s_yLimit = yLimit;
    s_uFlags = uFlags;

    if (uFlags & NSH_SHELL) {
        s_hShellHook = SetWindowsHookEx(WH_SHELL, (HOOKPROC)NsShellProc, s_hInst, NULL);
        if (s_hShellHook)
            uDone |= NSH_SHELL;
    }
    if (uFlags & NSH_MAXIMIZE) {
        s_hWndHook = SetWindowsHookEx(WH_CALLWNDPROC, (HOOKPROC)NsWndProcHook, s_hInst, NULL);
        if (s_hWndHook)
            uDone |= NSH_MAXIMIZE;
    }
    if (uFlags & NSH_WINKEY) {
        s_hKeyHook = SetWindowsHookEx(WH_KEYBOARD, (HOOKPROC)NsKeyboardProc, s_hInst, NULL);
        if (s_hKeyHook)
            uDone |= NSH_WINKEY;
    }
    if (uFlags & NSH_DESKTOP) {
        s_lpfnOldDesk = (WNDPROC)SetWindowLong(GetDesktopWindow(), GWL_WNDPROC,
                                               (LONG)(FARPROC)NsDesktopProc);
        if (s_lpfnOldDesk)
            uDone |= NSH_DESKTOP;
    }
    if (uFlags & NSH_FILES) {
        s_nHead = s_nTail = 0;
        s_bPosted = FALSE;
        s_bFileCdr = FileCdr((FARPROC)NsFileCdrProc);
        if (s_bFileCdr)
            uDone |= NSH_FILES;
    }
    return uDone;
}
