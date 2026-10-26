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
 * toolbar.c - a row of push buttons with small images
 *
 * The buttons send WM_COMMAND to the parent; the toolbar never takes the
 * focus. The parent sets which buttons are checked (sunken: the current
 * view and sort order) or disabled with ToolbarSetState.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define BTN_CX      23
#define BTN_CY      22
#define SEP_CX      8
#define TOP         3           /* etched line and gap above the buttons */

typedef struct tagTOOLBAR {
    int        nButtons;
    TBBTN   aButtons[TB_MAXBUTTONS];
    BYTE       aState[TB_MAXBUTTONS];
    int        nPressed;        /* button held down with the mouse, -1 */
    BOOL       bInside;         /* the mouse is over the held button */
} TOOLBAR, FAR *LPTOOLBAR;

#define TBS_CHECKED     0x01
#define TBS_DISABLED    0x02

static LPTOOLBAR GetToolbar(HWND hwnd)
{
    return (LPTOOLBAR)GetWindowLong(hwnd, 0);
}

static BOOL ButtonRect(LPTOOLBAR t, int i, RECT FAR *lprc)
{
    int j, x = 4;

    for (j = 0; j < i; j++)
        x += t->aButtons[j].uCmd ? BTN_CX : SEP_CX;
    SetRect(lprc, x, TOP, x + BTN_CX, TOP + BTN_CY);
    return t->aButtons[i].uCmd != 0;
}

static int HitButton(LPTOOLBAR t, int x, int y)
{
    RECT rc;
    POINT pt;
    int i;

    pt.x = x;
    pt.y = y;
    for (i = 0; i < t->nButtons; i++)
        if (ButtonRect(t, i, &rc) && PtInRect(&rc, pt))
            return i;
    return -1;
}

static void InvalidateButton(HWND hwnd, LPTOOLBAR t, int i)
{
    RECT rc;

    ButtonRect(t, i, &rc);
    InvalidateRect(hwnd, &rc, FALSE);
}

static void PaintToolbar(HWND hwnd, HDC hdc, LPTOOLBAR t)
{
    RECT rc;
    int i;

    GetClientRect(hwnd, &rc);
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    /* etched line below the menu bar */
    SetRect(&rc, rc.left, 0, rc.right, 1);
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNSHADOW));
    OffsetRect(&rc, 0, 1);
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNHIGHLIGHT));

    for (i = 0; i < t->nButtons; i++) {
        BOOL bDown, bDisabled;
        int off;

        if (!ButtonRect(t, i, &rc))
            continue;
        bDisabled = (t->aState[i] & TBS_DISABLED) != 0;
        bDown = (i == t->nPressed && t->bInside) || (t->aState[i] & TBS_CHECKED);
        Draw3DEdge(hdc, &rc, bDown, FALSE);
        off = bDown ? 1 : 0;
        if (bDisabled)
            DrawSmallImageDisabled(hdc, rc.left + 3 + off, rc.top + 3 + off,
                                   t->aButtons[i].nImage);
        else
            DrawSmallImage(hdc, rc.left + 3 + off, rc.top + 3 + off,
                           t->aButtons[i].nImage);
    }
}

LRESULT CALLBACK NSEXPORT ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam)
{
    LPTOOLBAR t = GetToolbar(hwnd);
    PAINTSTRUCT ps;
    int i;

    switch (msg) {
    case WM_CREATE:
        t = (LPTOOLBAR)malloc(sizeof(TOOLBAR));
        if (!t)
            return -1;
        _fmemset(t, 0, sizeof(TOOLBAR));
        t->nPressed = -1;
        SetWindowLong(hwnd, 0, (LONG)t);
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        if (t)
            PaintToolbar(hwnd, ps.hdc, t);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!t)
            break;
        i = HitButton(t, (int)LOWORD(lParam), (int)HIWORD(lParam));
        if (i >= 0 && !(t->aState[i] & TBS_DISABLED)) {
            t->nPressed = i;
            t->bInside = TRUE;
            SetCapture(hwnd);
            InvalidateButton(hwnd, t, i);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (t && t->nPressed >= 0) {
            BOOL bInside = HitButton(t, (int)LOWORD(lParam), (int)HIWORD(lParam)) == t->nPressed;

            if (bInside != t->bInside) {
                t->bInside = bInside;
                InvalidateButton(hwnd, t, t->nPressed);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (t && t->nPressed >= 0) {
            i = t->nPressed;
            t->nPressed = -1;
            ReleaseCapture();
            InvalidateButton(hwnd, t, i);
            UpdateWindow(hwnd);
            if (t->bInside)
                SendMessage(GetParent(hwnd), WM_COMMAND, t->aButtons[i].uCmd, 0L);
        }
        return 0;

    case WM_CANCELMODE:
        if (t && t->nPressed >= 0) {
            i = t->nPressed;
            t->nPressed = -1;
            ReleaseCapture();
            InvalidateButton(hwnd, t, i);
        }
        return 0;

    case WM_SYSCOLORCHANGE:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_DESTROY:
        if (t) {
            free(t);
            SetWindowLong(hwnd, 0, 0L);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* The buttons of folder windows and the File System window (Calmira's
   Explorer buttons); 0 is a gap */
const TBBTN g_aFolderButtons[] = {
    { CMD_F_UP, SMI_TB_UP }, { CMD_F_GOTO, SMI_TB_GOTO }, { 0, 0 },
    { CMD_F_NEWFOLDER, SMI_TB_NEWFOLDER }, { 0, 0 },
    { CMD_F_CUT, SMI_TB_CUT }, { CMD_F_COPY, SMI_TB_COPY }, { CMD_F_PASTE, SMI_TB_PASTE },
    { 0, 0 },
    { CMD_F_DELETE, SMI_TB_DELETE }, { CMD_F_RENAME, SMI_TB_RENAME },
    { CMD_F_PROPERTIES, SMI_TB_PROPS }, { 0, 0 },
    { CMD_F_LARGEICONS, SMI_TB_LARGE }, { CMD_F_SMALLICONS, SMI_TB_SMALL },
    { CMD_F_LIST, SMI_TB_LIST }, { CMD_F_DETAILS, SMI_TB_DETAILS }, { 0, 0 },
    { CMD_F_SORTNAME, SMI_TB_SORTNAME }, { CMD_F_SORTTYPE, SMI_TB_SORTTYPE },
    { CMD_F_SORTSIZE, SMI_TB_SORTSIZE }, { CMD_F_SORTDATE, SMI_TB_SORTDATE }
};
const int g_nFolderButtons = sizeof(g_aFolderButtons) / sizeof(g_aFolderButtons[0]);

/* The buttons follow the menu states of a folder view */
void ToolbarSyncView(HWND hwndToolbar, HWND hwndView)
{
    LPTOOLBAR t = GetToolbar(hwndToolbar);
    HMENU hMenu;
    UINT uState;
    int i;

    if (!hwndToolbar || !t)
        return;
    hMenu = CreatePopupMenu();
    for (i = 0; i < t->nButtons; i++)
        if (t->aButtons[i].uCmd)
            AppendMenu(hMenu, MF_STRING, t->aButtons[i].uCmd, "");
    FViewInitMenu(hwndView, hMenu);
    for (i = 0; i < t->nButtons; i++)
        if (t->aButtons[i].uCmd) {
            uState = GetMenuState(hMenu, t->aButtons[i].uCmd, MF_BYCOMMAND);
            ToolbarSetState(hwndToolbar, t->aButtons[i].uCmd,
                            (uState & MF_CHECKED) != 0, !(uState & MF_GRAYED));
        }
    DestroyMenu(hMenu);
}

/* -------------------------------------------------------------------- */
/* Public interface                                                     */
/* -------------------------------------------------------------------- */

BOOL ToolbarRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ToolbarWndProc;
    wc.cbWndExtra = sizeof(LPTOOLBAR);
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLS_TOOLBAR;
    return RegisterClass(&wc);
}

/* The height of a toolbar */
int ToolbarHeight(void)
{
    return TOP + BTN_CY + 3;
}

/* Creates a toolbar; a button with uCmd 0 is a gap. */
HWND ToolbarCreate(HWND hwndParent, const TBBTN FAR *lpButtons, int nButtons)
{
    HWND hwnd = CreateWindow(CLS_TOOLBAR, "", WS_CHILD | WS_VISIBLE,
                             0, 0, 0, 0, hwndParent, NULL, g_hInst, NULL);
    LPTOOLBAR t;

    if (!hwnd)
        return NULL;
    t = GetToolbar(hwnd);
    if (nButtons > TB_MAXBUTTONS)
        nButtons = TB_MAXBUTTONS;
    _fmemcpy(t->aButtons, lpButtons, nButtons * sizeof(TBBTN));
    t->nButtons = nButtons;
    return hwnd;
}

/* Sets whether the button for uCmd is checked (drawn sunken) and enabled */
void ToolbarSetState(HWND hwnd, UINT uCmd, BOOL bChecked, BOOL bEnabled)
{
    LPTOOLBAR t = GetToolbar(hwnd);
    BYTE bState = (BYTE)((bChecked ? TBS_CHECKED : 0) | (bEnabled ? 0 : TBS_DISABLED));
    int i;

    if (!t)
        return;
    for (i = 0; i < t->nButtons; i++)
        if (t->aButtons[i].uCmd == uCmd && t->aState[i] != bState) {
            t->aState[i] = bState;
            InvalidateButton(hwnd, t, i);
        }
}
