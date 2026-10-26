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
 *  - a subclass of the desktop window reporting right clicks and files
 *    dropped on it;
 *  - a keyboard hook for the Windows key (VK 0xFF under Windows 3.1),
 *    alone (on its release) or with another key, and for hot keys;
 *  - a WH_GETMESSAGE hook that closes a window when its minimise or
 *    maximise box is clicked with the right mouse button (as Calmira);
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
static HHOOK     s_hMsgHook;
static WORD      s_wHotkeys[MAX_HOTKEYS];
static int       s_nHotkeys;
static BOOL      s_bWinDown;        /* the Windows key is held */
static BOOL      s_bWinUsed;        /* ... and another key was pressed with it */
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

static BOOL IsWinKey(WPARAM vk)
{
    return vk == 0xFF || vk == 0x5B || vk == 0x5C;
}

LRESULT CALLBACK __export NsKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    BOOL bUp = (lParam & 0x80000000L) != 0, bRepeat = (lParam & 0x40000000L) != 0;
    WORD wKey;
    int i;

    if (nCode < 0 || !s_hwndNotify)
        return CallNextHookEx(s_hKeyHook, nCode, wParam, lParam);
    if (IsWinKey(wParam) && (s_uFlags & NSH_WINKEY)) {
        /* alone it opens the Start menu when released */
        if (!bUp && !bRepeat) {
            s_bWinDown = TRUE;
            s_bWinUsed = FALSE;
        } else if (bUp) {
            if (s_bWinDown && !s_bWinUsed
                && !(GetSystemDebugState() & (SDS_MENU | SDS_SYSMODAL)))
                PostMessage(s_hwndNotify, WM_NS_WINKEY, 0, 0L);
            s_bWinDown = FALSE;
        }
        return 1;
    }
    if (!bUp && !bRepeat && (s_uFlags & NSH_HOTKEYS) && s_nHotkeys) {
        wKey = (WORD)(wParam & 0xFF);
        if (GetKeyState(VK_SHIFT) < 0)
            wKey |= HK_SHIFT;
        if (GetKeyState(VK_CONTROL) < 0)
            wKey |= HK_CTRL;
        if (GetKeyState(VK_MENU) < 0)
            wKey |= HK_ALT;
        if (s_bWinDown)
            wKey |= HK_WIN;
        for (i = 0; i < s_nHotkeys; i++)
            if (s_wHotkeys[i] == wKey) {
                s_bWinUsed = TRUE;
                if (!(GetSystemDebugState() & SDS_SYSMODAL))
                    PostMessage(s_hwndNotify, WM_NS_HOTKEY, i, 0L);
                return 1;
            }
    }
    if (s_bWinDown && !bUp)
        s_bWinUsed = TRUE;
    return CallNextHookEx(s_hKeyHook, nCode, wParam, lParam);
}

void FAR PASCAL __export NsHookSetHotkeys(const WORD FAR *lpKeys, int nKeys)
{
    int i;

    s_nHotkeys = nKeys < MAX_HOTKEYS ? nKeys : MAX_HOTKEYS;
    for (i = 0; i < s_nHotkeys; i++)
        s_wHotkeys[i] = lpKeys[i];
}

LRESULT CALLBACK __export NsGetMsgProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    MSG FAR *lpmsg = (MSG FAR *)lParam;

    if (nCode >= 0 && lpmsg->message == WM_NCRBUTTONUP
        && (lpmsg->wParam == HTREDUCE || lpmsg->wParam == HTZOOM)) {
        PostMessage(lpmsg->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0L);
        lpmsg->message = WM_NULL;
    }
    return CallNextHookEx(s_hMsgHook, nCode, wParam, lParam);
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
    /* files from File Manager: the shell makes shortcuts of them */
    if (msg == WM_DROPFILES && s_hwndNotify) {
        PostMessage(s_hwndNotify, WM_NS_DESKDROP, wParam, 0L);
        return 0;
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
    if (s_hMsgHook) {
        UnhookWindowsHookEx(s_hMsgHook);
        s_hMsgHook = NULL;
    }
    s_bWinDown = FALSE;
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
    if (uFlags & (NSH_WINKEY | NSH_HOTKEYS)) {
        s_hKeyHook = SetWindowsHookEx(WH_KEYBOARD, (HOOKPROC)NsKeyboardProc, s_hInst, NULL);
        if (s_hKeyHook)
            uDone |= uFlags & (NSH_WINKEY | NSH_HOTKEYS);
    }
    if (uFlags & NSH_RCLOSE) {
        s_hMsgHook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)NsGetMsgProc, s_hInst, NULL);
        if (s_hMsgHook)
            uDone |= NSH_RCLOSE;
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
