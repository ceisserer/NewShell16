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
 * explorer.c - the File System window: a folder tree beside a folder view
 *
 * The tree is an owner-drawn list box whose items mirror a flat array of
 * nodes in display order (drives at level 0, folders below). Expanding a
 * node inserts its sub-folders after it; collapsing removes them. A right
 * click on the tree has Calmira's tree menu. Browse Folders is the same
 * window with the tree only; there a double click opens a folder window.
 *
 *   [Explorer]
 *   RememberLast=1     ; open at the folder shown last ([Explorer] Last=)
 */
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include "newshell.h"

#define MAX_EXPLORERS   16
#define MAX_NODES       1500
#define ID_TREE         2
#define ID_VIEW         1
#define TIMER_SELECT    1
#define SELECT_DELAY    250
#define ITEM_CY         18
#define INDENT          16
#define SPLIT_CX        4

#define NF_EXPANDED     0x01
#define NF_NOCHILDREN   0x02
#define NF_DRIVE        0x04

typedef struct tagTNODE {
    char szName[13];
    BYTE bLevel;
    BYTE bFlags;
    BYTE bIcon;                     /* IDI_* for drives */
} TNODE, FAR *LPTNODE;

typedef struct tagEXPLORER {
    LPTNODE lpNodes;
    int     nNodes;
    HWND    hwndTree;
    HWND    hwndView;
    int     xSplit;
    BOOL    bSplitting;
    BOOL    bNoSync;                /* suppress tree -> view updates */
    HWND    hwndLastFocus;          /* pane to refocus on activation */
    HWND    hwndToolbar;
    BOOL    bToolbar, bStatusBar;   /* View > Toolbar, Status Bar */
    BOOL    bTreeOnly;              /* Browse Folders: only the tree */
} EXPLORER, FAR *LPEXPLORER;

#define WM_NS_TREEMENU  (WM_USER + 140)     /* tree right click, lParam = point */

static BOOL    s_bCreateTreeOnly;   /* for the window being created */
static FARPROC s_lpfnTreeProc;
static WNDPROC s_lpfnOldTree;

static HWND s_hwndExplorers[MAX_EXPLORERS];
static int  s_nExplorers;

static LPEXPLORER GetExplorer(HWND hwnd)
{
    return (LPEXPLORER)GetWindowLong(hwnd, 0);
}

/* -------------------------------------------------------------------- */
/* Nodes                                                                */
/* -------------------------------------------------------------------- */

static void NodePath(LPEXPLORER e, int i, LPSTR lpBuf)
{
    int aChain[64], n = 0, nLevel;

    if (i < 0 || i >= e->nNodes) {
        lpBuf[0] = '\0';
        return;
    }
    nLevel = e->lpNodes[i].bLevel;
    aChain[n++] = i;
    while (nLevel > 0 && i > 0 && n < 64) {
        i--;
        if (e->lpNodes[i].bLevel == nLevel - 1) {
            aChain[n++] = i;
            nLevel--;
        }
    }
    lpBuf[0] = '\0';
    while (n-- > 0) {
        if (lpBuf[0] == '\0')
            wsprintf(lpBuf, "%s\\", (LPSTR)e->lpNodes[aChain[n]].szName);
        else
            PathAppend(lpBuf, e->lpNodes[aChain[n]].szName);
    }
    PathNormalize(lpBuf);
}

static BOOL InsertNodes(LPEXPLORER e, int nAt, LPTNODE lpNew, int nNew)
{
    int i;

    if (e->nNodes + nNew > MAX_NODES)
        return FALSE;
    _fmemmove(&e->lpNodes[nAt + nNew], &e->lpNodes[nAt],
              (e->nNodes - nAt) * sizeof(TNODE));
    _fmemcpy(&e->lpNodes[nAt], lpNew, nNew * sizeof(TNODE));
    e->nNodes += nNew;
    for (i = 0; i < nNew; i++)
        SendMessage(e->hwndTree, LB_INSERTSTRING, nAt + i, 0L);
    return TRUE;
}

static void DeleteNodes(LPEXPLORER e, int nAt, int nCount)
{
    int i;

    _fmemmove(&e->lpNodes[nAt], &e->lpNodes[nAt + nCount],
              (e->nNodes - nAt - nCount) * sizeof(TNODE));
    e->nNodes -= nCount;
    for (i = 0; i < nCount; i++)
        SendMessage(e->hwndTree, LB_DELETESTRING, nAt, 0L);
}

static int CompareNodes(const void *a, const void *b)
{
    return lstrcmpi(((LPTNODE)a)->szName, ((LPTNODE)b)->szName);
}

static BOOL HasSubdirs(LPCSTR lpPath)
{
    struct find_t ft;
    char szPattern[MAXPATHLEN + 4];

    lstrcpy(szPattern, lpPath);
    PathAppend(szPattern, "*.*");
    if (_dos_findfirst(szPattern, _A_SUBDIR | _A_HIDDEN | _A_SYSTEM | _A_RDONLY, &ft) != 0)
        return FALSE;
    do {
        if ((ft.attrib & _A_SUBDIR) && ft.name[0] != '.')
            return TRUE;
    } while (_dos_findnext(&ft) == 0);
    return FALSE;
}

static void Collapse(LPEXPLORER e, int i)
{
    int j = i + 1;

    while (j < e->nNodes && e->lpNodes[j].bLevel > e->lpNodes[i].bLevel)
        j++;
    if (j > i + 1)
        DeleteNodes(e, i + 1, j - i - 1);
    e->lpNodes[i].bFlags &= ~NF_EXPANDED;
}

static void Expand(LPEXPLORER e, int i)
{
    struct find_t ft;
    char szPath[MAXPATHLEN], szPattern[MAXPATHLEN + 4], szChild[MAXPATHLEN];
    LPTNODE lpNew;
    int nNew = 0, j, nDrive;
    BOOL bLocal;
    UINT uOldMode;
    unsigned uAttr = _A_SUBDIR | _A_RDONLY | _A_ARCH;

    if (e->lpNodes[i].bFlags & NF_EXPANDED)
        return;
    lpNew = (LPTNODE)malloc(256 * sizeof(TNODE));
    if (!lpNew)
        return;
    if (g_cfg.bShowHidden)
        uAttr |= _A_HIDDEN | _A_SYSTEM;

    NodePath(e, i, szPath);
    nDrive = (szPath[0] & ~0x20) - 'A';
    bLocal = GetDriveType(nDrive) == DRIVE_FIXED;
    lstrcpy(szPattern, szPath);
    PathAppend(szPattern, "*.*");
    uOldMode = SetErrorMode(1);
    if (_dos_findfirst(szPattern, uAttr, &ft) == 0) {
        do {
            if (!(ft.attrib & _A_SUBDIR) || ft.name[0] == '.')
                continue;
            if (!g_cfg.bShowHidden && (ft.attrib & (_A_HIDDEN | _A_SYSTEM)))
                continue;
            _fmemset(&lpNew[nNew], 0, sizeof(TNODE));
            lstrcpyn(lpNew[nNew].szName, ft.name, sizeof(lpNew[0].szName));
            lpNew[nNew].bLevel = (BYTE)(e->lpNodes[i].bLevel + 1);
            if (++nNew >= 256)
                break;
        } while (_dos_findnext(&ft) == 0);
    }
    if (nNew > 1)
        qsort(lpNew, nNew, sizeof(TNODE), CompareNodes);
    if (bLocal)
        for (j = 0; j < nNew; j++) {
            lstrcpy(szChild, szPath);
            PathAppend(szChild, lpNew[j].szName);
            if (!HasSubdirs(szChild))
                lpNew[j].bFlags |= NF_NOCHILDREN;
        }
    SetErrorMode(uOldMode);

    e->lpNodes[i].bFlags |= NF_EXPANDED;
    if (nNew == 0)
        e->lpNodes[i].bFlags |= NF_NOCHILDREN;
    else
        InsertNodes(e, i + 1, lpNew, nNew);
    free(lpNew);
}

/* One past the last node below node i */
static int SubtreeEnd(LPEXPLORER e, int i)
{
    int j = i + 1;

    while (j < e->nNodes && e->lpNodes[j].bLevel > e->lpNodes[i].bLevel)
        j++;
    return j;
}

/* Expands node i and everything below it (as far as the node table goes) */
static void ExpandBranch(LPEXPLORER e, int i)
{
    HCURSOR hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int j;

    for (j = i; j < SubtreeEnd(e, i) && e->nNodes < MAX_NODES - 256; j++)
        if (!(e->lpNodes[j].bFlags & (NF_EXPANDED | NF_NOCHILDREN))) {
            if ((e->lpNodes[j].bFlags & NF_DRIVE)
                && GetDriveType(e->lpNodes[j].szName[0] - 'A') != DRIVE_FIXED)
                continue;
            Expand(e, j);
        }
    SetCursor(hcurOld);
}

static void Toggle(LPEXPLORER e, int i)
{
    SendMessage(e->hwndTree, WM_SETREDRAW, FALSE, 0L);
    if (e->lpNodes[i].bFlags & NF_EXPANDED)
        Collapse(e, i);
    else
        Expand(e, i);
    SendMessage(e->hwndTree, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(e->hwndTree, NULL, TRUE);
}

static void BuildDrives(LPEXPLORER e)
{
    TNODE n;
    int d;

    for (d = 0; d < 26; d++) {
        if (GetDriveType(d) == 0)
            continue;
        _fmemset(&n, 0, sizeof(n));
        wsprintf(n.szName, "%c:", 'A' + d);
        n.bFlags = NF_DRIVE;
        n.bIcon = (BYTE)DriveIcon(d, &n.bLevel);
        n.bLevel = 0;
        InsertNodes(e, e->nNodes, &n, 1);
    }
}

/* Expands the tree down to lpPath and selects it */
static void SelectPath(HWND hwnd, LPEXPLORER e, LPCSTR lpPath)
{
    char szPath[MAXPATHLEN], szNode[MAXPATHLEN];
    int i, nLen, nBest = -1;
    BOOL bFound;

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    PathNormalize(szPath);
    SendMessage(e->hwndTree, WM_SETREDRAW, FALSE, 0L);
    do {
        bFound = FALSE;
        for (i = nBest + 1; i < e->nNodes; i++) {
            NodePath(e, i, szNode);
            nLen = lstrlen(szNode);
            if (lstrcmpi(szNode, szPath) == 0) {
                nBest = i;
                bFound = FALSE;
                break;
            }
            /* szNode is a proper prefix of szPath? */
            if (nLen < lstrlen(szPath) && _fstrnicmp(szNode, szPath, nLen) == 0
                && (szNode[nLen - 1] == '\\' || szPath[nLen] == '\\')) {
                nBest = i;
                if (!(e->lpNodes[i].bFlags & NF_EXPANDED)) {
                    Expand(e, i);
                }
                bFound = TRUE;
                break;
            }
        }
    } while (bFound);
    if (nBest >= 0 && !(e->lpNodes[nBest].bFlags & NF_EXPANDED)
        && (e->lpNodes[nBest].bFlags & NF_DRIVE))
        Expand(e, nBest);
    SendMessage(e->hwndTree, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(e->hwndTree, NULL, TRUE);
    if (nBest >= 0) {
        e->bNoSync = TRUE;
        SendMessage(e->hwndTree, LB_SETCURSEL, nBest, 0L);
        e->bNoSync = FALSE;
    }
    if (lstrcmpi(FViewGetPath(e->hwndView), szPath) != 0)
        FViewSetPath(e->hwndView, szPath);
    (void)hwnd;
}

/* -------------------------------------------------------------------- */
/* Painting                                                             */
/* -------------------------------------------------------------------- */

static void DrawNode(LPEXPLORER e, DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText, rcBox;
    LPTNODE n;
    BOOL bSel = (lpdis->itemState & ODS_SELECTED) != 0;
    int x, cy;
    HFONT hfOld;

    FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
    if ((int)lpdis->itemID < 0 || (int)lpdis->itemID >= e->nNodes)
        return;
    n = &e->lpNodes[lpdis->itemID];
    x = rc.left + 2 + n->bLevel * INDENT;
    cy = rc.top + (rc.bottom - rc.top) / 2;

    /* expand/collapse box */
    if (!(n->bFlags & NF_NOCHILDREN)) {
        HBRUSH hbr = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        SetRect(&rcBox, x, cy - 4, x + 9, cy + 5);
        FrameRect(hdc, &rcBox, hbr);
        DeleteObject(hbr);
        SetRect(&rcBox, x + 2, cy, x + 7, cy + 1);
        FillSolid(hdc, &rcBox, GetSysColor(COLOR_WINDOWTEXT));
        if (!(n->bFlags & NF_EXPANDED)) {
            SetRect(&rcBox, x + 4, cy - 2, x + 5, cy + 3);
            FillSolid(hdc, &rcBox, GetSysColor(COLOR_WINDOWTEXT));
        }
    }
    x += 12;
    if (n->bFlags & NF_DRIVE)
        DrawIconSmall(hdc, x, rc.top + 1, IconStock(n->bIcon), GetSysColor(COLOR_WINDOW));
    else
        DrawSmallImage(hdc, x, rc.top + 1, bSel ? SMI_FOLDEROPEN : SMI_FOLDER);
    x += 19;

    hfOld = SelectObject(hdc, g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetRect(&rcText, x, rc.top + 1, x + TextWidth(hdc, n->szName) + 4, rc.bottom - 1);
    if (bSel)
        FillSolid(hdc, &rcText, GetSysColor(GetFocus() == e->hwndTree
                                            ? COLOR_HIGHLIGHT : COLOR_BTNFACE));
    SetTextColor(hdc, GetSysColor(bSel && GetFocus() == e->hwndTree
                                  ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    DrawText(hdc, n->szName, -1, &rcText,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (lpdis->itemState & ODS_FOCUS)
        DrawItemFocus(hdc, &rcText, bSel && GetFocus() == e->hwndTree);
    SelectObject(hdc, hfOld);
}

/* -------------------------------------------------------------------- */
/* Window procedure                                                     */
/* -------------------------------------------------------------------- */

/* The panes' top and bottom, inside the toolbar and status bar */
static int PaneTop(LPEXPLORER e)
{
    return e->bToolbar ? ToolbarHeight() : 0;
}

static int PaneBottom(HWND hwnd, LPEXPLORER e)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    return rc.bottom - (e->bStatusBar ? StatusBarHeight() : 0);
}

static void Layout(HWND hwnd, LPEXPLORER e)
{
    RECT rc;
    int y = PaneTop(e), cy = PaneBottom(hwnd, e) - y;

    GetClientRect(hwnd, &rc);
    if (e->bTreeOnly) {
        MovePane(e->hwndTree, 0, 0, rc.right, rc.bottom);
        MoveWindow(e->hwndView, 0, 0, 0, 0, FALSE);
        return;
    }
    if (e->xSplit > rc.right - 60)
        e->xSplit = rc.right - 60;
    if (e->xSplit < 60)
        e->xSplit = 60;
    if (e->hwndToolbar)
        MoveWindow(e->hwndToolbar, 0, 0, rc.right, ToolbarHeight(), TRUE);
    MovePane(e->hwndTree, 0, y, e->xSplit, cy);
    MovePane(e->hwndView, e->xSplit + SPLIT_CX, y,
             rc.right - e->xSplit - SPLIT_CX, cy);
}

static void InvalidateStatus(HWND hwnd)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - StatusBarHeight();
    InvalidateRect(hwnd, &rc, FALSE);
}

static void UpdateToolbar(LPEXPLORER e)
{
    if (e->bToolbar)
        ToolbarSyncView(e->hwndToolbar, e->hwndView);
}

static void ShowBars(HWND hwnd, LPEXPLORER e)
{
    ShowWindow(e->hwndToolbar, e->bToolbar ? SW_SHOWNA : SW_HIDE);
    Layout(hwnd, e);
    UpdateToolbar(e);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void SetTitle(HWND hwnd, LPEXPLORER e)
{
    char szTitle[MAXPATHLEN + 16];
    LPCSTR lpPath = FViewGetPath(e->hwndView);

    if (e->bTreeOnly) {
        wsprintf(szTitle, "Browse Folders - %s", *lpPath ? lpPath : (LPCSTR)"Computer");
        SetWindowText(hwnd, szTitle);
        return;
    }
    wsprintf(szTitle, "File System - %s", *lpPath ? lpPath : (LPCSTR)"Computer");
    SetWindowText(hwnd, szTitle);
}

static void OnTreeClick(HWND hwnd, LPEXPLORER e)
{
    DWORD dwPos = GetMessagePos();
    POINT pt;
    int i = (int)SendMessage(e->hwndTree, LB_GETCURSEL, 0, 0L);
    int x;

    if (i < 0 || i >= e->nNodes || e->bNoSync)
        return;
    pt.x = LOWORD(dwPos);
    pt.y = HIWORD(dwPos);
    ScreenToClient(e->hwndTree, &pt);
    x = 2 + e->lpNodes[i].bLevel * INDENT;
    if (pt.x >= x - 2 && pt.x < x + 11 && (GetKeyState(VK_LBUTTON) & 0x8000))
        Toggle(e, i);
    KillTimer(hwnd, TIMER_SELECT);
    SetTimer(hwnd, TIMER_SELECT, SELECT_DELAY, NULL);
}

static int OnTreeKey(HWND hwnd, LPEXPLORER e, UINT vk)
{
    int i = (int)SendMessage(e->hwndTree, LB_GETCURSEL, 0, 0L);
    int j;

    if (i < 0 || i >= e->nNodes)
        return -1;
    switch (vk) {
    case VK_RIGHT:
        if (!(e->lpNodes[i].bFlags & NF_EXPANDED)) {
            Toggle(e, i);
            return -2;
        }
        return (i + 1 < e->nNodes && e->lpNodes[i + 1].bLevel > e->lpNodes[i].bLevel)
               ? i + 1 : -2;
    case VK_LEFT:
        if (e->lpNodes[i].bFlags & NF_EXPANDED) {
            Toggle(e, i);
            return -2;
        }
        for (j = i - 1; j >= 0; j--)
            if (e->lpNodes[j].bLevel < e->lpNodes[i].bLevel)
                return j;
        return -2;
    case VK_RETURN:
        if (e->bTreeOnly) {
            char szPath[MAXPATHLEN];

            NodePath(e, i, szPath);
            FolderOpen(szPath);
        } else
            Toggle(e, i);
        return -2;
    case VK_TAB:
        SetFocus(e->hwndView);
        return -2;
    case VK_BACK:
        for (j = i - 1; j >= 0; j--)
            if (e->lpNodes[j].bLevel < e->lpNodes[i].bLevel)
                return j;
        return -2;
    }
    (void)hwnd;
    return -1;
}

/* -------------------------------------------------------------------- */
/* Tree menu                                                            */
/* -------------------------------------------------------------------- */

/* Right clicks on the tree: select the node, then the menu */
LRESULT CALLBACK NSEXPORT TreeSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                           LPARAM lParam)
{
    if (msg == WM_RBUTTONDOWN) {
        int i = (int)SendMessage(hwnd, LB_GETTOPINDEX, 0, 0L)
                + (int)HIWORD(lParam) / ITEM_CY;

        SetFocus(hwnd);
        if (i < (int)SendMessage(hwnd, LB_GETCOUNT, 0, 0L)
            && i != (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0L)) {
            SendMessage(hwnd, LB_SETCURSEL, i, 0L);
            SendMessage(GetParent(hwnd), WM_COMMAND, GetWindowWord(hwnd, GWW_ID),
                        MAKELPARAM(hwnd, LBN_SELCHANGE));
        }
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        POINT pt;

        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        ClientToScreen(hwnd, &pt);
        SendMessage(GetParent(hwnd), WM_NS_TREEMENU, 0, MAKELPARAM(pt.x, pt.y));
        return 0;
    }
    return CallWindowProc(s_lpfnOldTree, hwnd, msg, wParam, lParam);
}

static void TreeMenu(HWND hwnd, LPEXPLORER e, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    int i = (int)SendMessage(e->hwndTree, LB_GETCURSEL, 0, 0L);
    UINT uNode = i >= 0 ? MF_ENABLED : MF_GRAYED;
    UINT uExp = MF_GRAYED, uCol = MF_GRAYED;

    if (i >= 0 && !(e->lpNodes[i].bFlags & NF_NOCHILDREN)) {
        uExp = MF_ENABLED;
        if (e->lpNodes[i].bFlags & NF_EXPANDED)
            uCol = MF_ENABLED;
    }
    AppendMenu(hMenu, MF_STRING | uNode, CMD_T_OPEN, "&Open");
    AppendMenu(hMenu, MF_STRING | uNode, CMD_T_EXPLORE, "&Explore");
    AppendMenu(hMenu, MF_STRING | uNode, CMD_T_FIND, "&Find...");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING | uExp, CMD_T_EXPAND, "Expand One &Level");
    AppendMenu(hMenu, MF_STRING | uExp, CMD_T_EXPANDBRANCH, "Expand &Branch");
    AppendMenu(hMenu, MF_STRING, CMD_T_EXPANDALL, "Expand &All");
    AppendMenu(hMenu, MF_STRING | uCol, CMD_T_COLLAPSE, "&Collapse Branch");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING | uNode, CMD_T_NEWFOLDER, "New Fol&der");
    AppendMenu(hMenu, MF_STRING, CMD_T_REFRESH, "&Refresh");
    if (NetShareAvailable()) {
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING | uNode, CMD_T_SHARE, "&Share As...");
        AppendMenu(hMenu, MF_STRING | uNode, CMD_T_STOPSHARE, "S&top Sharing...");
    }
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING | uNode, CMD_T_PROPERTIES, "P&roperties");
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

static void TreeCommand(HWND hwnd, LPEXPLORER e, UINT id)
{
    char szPath[MAXPATHLEN], szName[16];
    int i = (int)SendMessage(e->hwndTree, LB_GETCURSEL, 0, 0L), j;
    HGLOBAL hList = NULL;

    if (i >= 0)
        NodePath(e, i, szPath);
    else if (id != CMD_T_EXPANDALL && id != CMD_T_REFRESH)
        return;
    switch (id) {
    case CMD_T_OPEN:        FolderOpen(szPath); return;
    case CMD_T_EXPLORE:     ExplorerOpen(szPath); return;
    case CMD_T_FIND:        FindOpen(szPath); return;
    case CMD_T_SHARE:       NetShareDialog(hwnd, szPath, TRUE); return;
    case CMD_T_STOPSHARE:   NetShareDialog(hwnd, szPath, FALSE); return;
    case CMD_T_PROPERTIES:
        PathListAdd(&hList, szPath);
        if (hList) {
            PropertiesDialog(hwnd, hList);
            GlobalFree(hList);
        }
        return;
    case CMD_T_NEWFOLDER:
        if (FileOpNewFolder(hwnd, szPath, szName)) {
            ExplorerRefreshPath(szPath);
            FViewRefreshPath(szPath);
        }
        return;
    }
    SendMessage(e->hwndTree, WM_SETREDRAW, FALSE, 0L);
    switch (id) {
    case CMD_T_EXPAND:
        Expand(e, i);
        break;
    case CMD_T_EXPANDBRANCH:
        ExpandBranch(e, i);
        break;
    case CMD_T_EXPANDALL:
        for (j = 0; j < e->nNodes; j++)
            if (e->lpNodes[j].bLevel == 0)
                ExpandBranch(e, j);
        break;
    case CMD_T_COLLAPSE:
        Collapse(e, i);
        break;
    case CMD_T_REFRESH:
        lstrcpy(szPath, FViewGetPath(e->hwndView));
        DeleteNodes(e, 0, e->nNodes);
        BuildDrives(e);
        SendMessage(e->hwndTree, WM_SETREDRAW, TRUE, 0L);
        if (!szPath[0] || !PathIsDirectory(szPath)) {
            GetWindowsDirectory(szPath, sizeof(szPath));
            szPath[3] = '\0';
        }
        SelectPath(hwnd, e, szPath);
        FViewRefresh(e->hwndView);
        return;
    }
    SendMessage(e->hwndTree, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(e->hwndTree, NULL, TRUE);
}

static void RemoveExplorer(HWND hwnd)
{
    int i;

    for (i = 0; i < s_nExplorers; i++)
        if (s_hwndExplorers[i] == hwnd) {
            s_hwndExplorers[i] = s_hwndExplorers[--s_nExplorers];
            return;
        }
}

LRESULT CALLBACK NSEXPORT ExplorerWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam)
{
    LPEXPLORER e = GetExplorer(hwnd);
    char szPath[MAXPATHLEN], szStatus[96];
    PAINTSTRUCT ps;
    RECT rc;
    int i;

    switch (msg) {
    case WM_CREATE:
        {
            CREATESTRUCT FAR *lpcs = (CREATESTRUCT FAR *)lParam;

            e = (LPEXPLORER)malloc(sizeof(EXPLORER));
            if (!e)
                return -1;
            _fmemset(e, 0, sizeof(EXPLORER));
            e->lpNodes = (LPTNODE)malloc(MAX_NODES * sizeof(TNODE));
            if (!e->lpNodes) {
                free(e);
                return -1;
            }
            SetWindowLong(hwnd, 0, (LONG)e);
            e->xSplit = CfgGetInt("Explorer", "Split", 170);
            e->bTreeOnly = s_bCreateTreeOnly;
            e->bToolbar = CfgGetInt("Explorer", "Toolbar", 1) && !e->bTreeOnly;
            e->bStatusBar = CfgGetInt("Explorer", "StatusBar", 1) && !e->bTreeOnly;
            if (e->bTreeOnly)
                SetMenu(hwnd, LoadMenu(g_hInst, MAKEINTRESOURCE(IDM_TREE)));
            e->hwndToolbar = ToolbarCreate(hwnd, g_aFolderButtons, g_nFolderButtons);
            if (e->hwndToolbar && !e->bToolbar)
                ShowWindow(e->hwndToolbar, SW_HIDE);
            e->hwndTree = CreateWindow("LISTBOX", "",
                                       WS_CHILD | WS_VISIBLE | WS_VSCROLL
                                       | LBS_OWNERDRAWFIXED | LBS_NOTIFY
                                       | LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT,
                                       0, 0, 0, 0, hwnd, (HMENU)ID_TREE, g_hInst, NULL);
            e->hwndView = FViewCreate(hwnd, (LPCSTR)lpcs->lpCreateParams);
            if (!e->hwndTree || !e->hwndView)
                return -1;
            if (e->bTreeOnly)
                ShowWindow(e->hwndView, SW_HIDE);
            if (!s_lpfnTreeProc)
                s_lpfnTreeProc = MakeProcInstance((FARPROC)TreeSubclassProc, g_hInst);
            s_lpfnOldTree = (WNDPROC)SetWindowLong(e->hwndTree, GWL_WNDPROC,
                                                   (LONG)s_lpfnTreeProc);
            BuildDrives(e);
            UpdateToolbar(e);
            if (s_nExplorers < MAX_EXPLORERS)
                s_hwndExplorers[s_nExplorers++] = hwnd;
        }
        return 0;

    case WM_SIZE:
        if (e) {
            Layout(hwnd, e);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        if (e) {
            int y = PaneTop(e), yBottom = PaneBottom(hwnd, e);

            GetClientRect(hwnd, &rc);
            DrawPaneEdge(ps.hdc, 0, y, e->xSplit, yBottom - y);
            DrawPaneEdge(ps.hdc, e->xSplit + SPLIT_CX, y,
                         rc.right - e->xSplit - SPLIT_CX, yBottom - y);
            rc.left = e->xSplit;
            rc.right = e->xSplit + SPLIT_CX;
            rc.top = y;
            rc.bottom = yBottom;
            FillSolid(ps.hdc, &rc, GetSysColor(COLOR_BTNFACE));
            if (e->bStatusBar) {
                FViewGetStatus(e->hwndView, szStatus, sizeof(szStatus));
                PaintStatusBar(hwnd, ps.hdc, StatusBarHeight(), szStatus, e->xSplit);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SETCURSOR:
        if (e && (HWND)wParam == hwnd && LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (pt.x >= e->xSplit && pt.x < e->xSplit + SPLIT_CX
                && pt.y >= PaneTop(e) && pt.y < PaneBottom(hwnd, e)) {
                SetCursor(LoadCursor(g_hInst, MAKEINTRESOURCE(IDC_SPLIT)));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN:
        if (e && (int)LOWORD(lParam) >= e->xSplit
            && (int)LOWORD(lParam) < e->xSplit + SPLIT_CX
            && (int)HIWORD(lParam) >= PaneTop(e)
            && (int)HIWORD(lParam) < PaneBottom(hwnd, e)) {
            e->bSplitting = TRUE;
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (e && e->bSplitting) {
            e->xSplit = (int)LOWORD(lParam) - SPLIT_CX / 2;
            Layout(hwnd, e);
            UpdateWindow(hwnd);
        }
        return 0;

    case WM_LBUTTONUP:
        if (e && e->bSplitting) {
            e->bSplitting = FALSE;
            ReleaseCapture();
            CfgSetInt("Explorer", "Split", e->xSplit);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_ACTIVATE:
        if (e && wParam != WA_INACTIVE)
            UpdateToolbar(e);
        if (e && wParam == WA_INACTIVE) {
            HWND hwndFocus = GetFocus();
            if (hwndFocus == e->hwndTree || hwndFocus == e->hwndView)
                e->hwndLastFocus = hwndFocus;
        }
        break;

    case WM_SETFOCUS:
        if (e)
            SetFocus(e->hwndLastFocus ? e->hwndLastFocus : e->hwndTree);
        return 0;

    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lParam)->itemHeight = ITEM_CY;
        return TRUE;

    case WM_DRAWITEM:
        if (e)
            DrawNode(e, (DRAWITEMSTRUCT FAR *)lParam);
        return TRUE;

    case WM_VKEYTOITEM:
        if (e && (HWND)LOWORD(lParam) == e->hwndTree)
            return OnTreeKey(hwnd, e, wParam);
        return -1;

    case WM_TIMER:
        KillTimer(hwnd, TIMER_SELECT);
        if (!e)
            return 0;
        i = (int)SendMessage(e->hwndTree, LB_GETCURSEL, 0, 0L);
        if (i >= 0) {
            NodePath(e, i, szPath);
            if (lstrcmpi(szPath, FViewGetPath(e->hwndView)) != 0) {
                if (e->lpNodes[i].bFlags & NF_DRIVE && !PathIsDirectory(szPath)) {
                    MessageBox(hwnd, "The drive is not ready.", NS_APPNAME,
                               MB_OK | MB_ICONEXCLAMATION);
                    return 0;
                }
                FViewSetPath(e->hwndView, szPath);
            }
        }
        return 0;

    case WM_INITMENUPOPUP:
        if (e) {
            FViewInitMenu(e->hwndView, (HMENU)wParam);
            CheckMenuItem((HMENU)wParam, CMD_F_TOOLBAR, e->bToolbar ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem((HMENU)wParam, CMD_F_STATUSBAR, e->bStatusBar ? MF_CHECKED : MF_UNCHECKED);
        }
        return 0;

    case WM_NS_FVNOTIFY:
        if (!e)
            return 0;
        switch (wParam) {
        case FVN_SELCHANGE:
            InvalidateStatus(hwnd);
            UpdateToolbar(e);
            break;
        case FVN_OPENFOLDER:
            SelectPath(hwnd, e, (LPCSTR)lParam);
            break;
        case FVN_GOUP:
            lstrcpy(szPath, FViewGetPath(e->hwndView));
            if (PathParent(szPath))
                SelectPath(hwnd, e, szPath);
            break;
        case FVN_PATHCHANGED:
            SetTitle(hwnd, e);
            InvalidateStatus(hwnd);
            UpdateToolbar(e);
            break;
        case FVN_TAB:
            SetFocus(e->hwndTree);
            break;
        case FVN_GONE:
            lstrcpy(szPath, FViewGetPath(e->hwndView));
            while (PathParent(szPath) && !PathIsDirectory(szPath))
                ;
            ExplorerRefreshPath(szPath);
            SelectPath(hwnd, e, szPath);
            break;
        }
        return 0;

    case WM_COMMAND:
        if (!e)
            break;
        if (wParam == ID_TREE) {
            if (HIWORD(lParam) == LBN_SELCHANGE)
                OnTreeClick(hwnd, e);
            else if (HIWORD(lParam) == LBN_DBLCLK) {
                i = (int)SendMessage(e->hwndTree, LB_GETCURSEL, 0, 0L);
                if (i >= 0 && e->bTreeOnly) {
                    NodePath(e, i, szPath);
                    FolderOpen(szPath);
                } else if (i >= 0)
                    Toggle(e, i);
            }
            return 0;
        }
        if (wParam >= CMD_T_FIRST && wParam <= CMD_T_LAST) {
            TreeCommand(hwnd, e, wParam);
            return 0;
        }
        switch (wParam) {
        case CMD_F_COMPUTER:    FolderOpen(""); return 0;
        case CMD_F_CLOSE:       DestroyWindow(hwnd); return 0;
        case CMD_ABOUT:         AboutDialog(hwnd); return 0;
        case CMD_FIND:          FindOpen(FViewGetPath(e->hwndView)); return 0;
        case CMD_RUN:           RunDialog(hwnd); return 0;
        case CMD_F_EXPLORE:
            if (FViewSelCount(e->hwndView) == 0)
                return 0;
            break;
        case CMD_F_TOOLBAR:
            e->bToolbar = !e->bToolbar;
            CfgSetInt("Explorer", "Toolbar", e->bToolbar);
            ShowBars(hwnd, e);
            return 0;
        case CMD_F_STATUSBAR:
            e->bStatusBar = !e->bStatusBar;
            CfgSetInt("Explorer", "StatusBar", e->bStatusBar);
            ShowBars(hwnd, e);
            return 0;
        }
        if (FViewCommand(e->hwndView, wParam)) {
            UpdateToolbar(e);
            return 0;
        }
        if (StartMenuCommand(hwnd, wParam))
            return 0;
        break;

    case WM_NS_TREEMENU:
        if (e)
            TreeMenu(hwnd, e, MAKEPOINT(lParam));
        return 0;

    case WM_DESTROY:
        RemoveExplorer(hwnd);
        if (e && e->hwndView && FViewGetPath(e->hwndView)[0])
            CfgSetStr("Explorer", "Last", FViewGetPath(e->hwndView));
        if (e) {
            free(e->lpNodes);
            free(e);
            SetWindowLong(hwnd, 0, 0L);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* -------------------------------------------------------------------- */
/* Public interface                                                     */
/* -------------------------------------------------------------------- */

BOOL ExplorerRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ExplorerWndProc;
    wc.cbWndExtra = sizeof(LPEXPLORER);
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_EXPLORER));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_FOLDER);
    wc.lpszClassName = CLS_EXPLORER;
    return RegisterClass(&wc);
}

static HWND OpenWindow(LPCSTR lpPath, BOOL bTreeOnly);

HWND ExplorerOpen(LPCSTR lpPath)
{
    return OpenWindow(lpPath, FALSE);
}

/* Browse Folders: a window with only the folder tree */
HWND ExplorerOpenTree(LPCSTR lpPath)
{
    return OpenWindow(lpPath, TRUE);
}

static HWND OpenWindow(LPCSTR lpPath, BOOL bTreeOnly)
{
    char szPath[MAXPATHLEN];
    LPEXPLORER e;
    HWND hwnd;

    lstrcpyn(szPath, lpPath ? lpPath : "", sizeof(szPath));
    if (szPath[0] == '\0' && CfgGetInt("Explorer", "RememberLast", 1)) {
        CfgGetStr("Explorer", "Last", "", szPath, sizeof(szPath));
        if (szPath[0] && !PathIsDirectory(szPath))
            szPath[0] = '\0';
    }
    if (szPath[0] == '\0') {
        GetWindowsDirectory(szPath, sizeof(szPath));
        szPath[3] = '\0';
    }
    PathNormalize(szPath);
    s_bCreateTreeOnly = bTreeOnly;
    hwnd = CreateWindow(CLS_EXPLORER, bTreeOnly ? "Browse Folders" : "File System",
                        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                        bTreeOnly ? 240 : 560, 340, NULL, NULL, g_hInst, (LPVOID)"");
    s_bCreateTreeOnly = FALSE;
    if (!hwnd)
        return NULL;
    e = GetExplorer(hwnd);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SelectPath(hwnd, e, szPath);
    UpdateWindow(hwnd);
    SetFocus(e->hwndTree);
    TaskbarRefresh();
    return hwnd;
}

/* Re-reads the sub-folders of lpDir in every File System tree */
void ExplorerRefreshPath(LPCSTR lpDir)
{
    char szDir[MAXPATHLEN], szNode[MAXPATHLEN], szSel[MAXPATHLEN];
    LPEXPLORER e;
    int w, i, nSel;

    lstrcpyn(szDir, lpDir, sizeof(szDir));
    PathNormalize(szDir);
    for (w = 0; w < s_nExplorers; w++) {
        e = GetExplorer(s_hwndExplorers[w]);
        if (!e)
            continue;
        for (i = 0; i < e->nNodes; i++) {
            NodePath(e, i, szNode);
            if (lstrcmpi(szNode, szDir) != 0)
                continue;
            nSel = (int)SendMessage(e->hwndTree, LB_GETCURSEL, 0, 0L);
            NodePath(e, nSel, szSel);
            SendMessage(e->hwndTree, WM_SETREDRAW, FALSE, 0L);
            e->lpNodes[i].bFlags &= ~NF_NOCHILDREN;
            if (e->lpNodes[i].bFlags & NF_EXPANDED) {
                Collapse(e, i);
                Expand(e, i);
            }
            SendMessage(e->hwndTree, WM_SETREDRAW, TRUE, 0L);
            InvalidateRect(e->hwndTree, NULL, TRUE);
            /* restore the selection without re-reading the view */
            if (szSel[0]) {
                int j;
                char szTmp[MAXPATHLEN];
                for (j = 0; j < e->nNodes; j++) {
                    NodePath(e, j, szTmp);
                    if (lstrcmpi(szTmp, szSel) == 0) {
                        e->bNoSync = TRUE;
                        SendMessage(e->hwndTree, LB_SETCURSEL, j, 0L);
                        e->bNoSync = FALSE;
                        break;
                    }
                }
            }
            break;
        }
    }
}

/* Drop target in a File System tree */
int ExplorerDropTarget(HWND hwnd, POINT pt, LPSTR lpDir)
{
    HWND hwndParent = GetParent(hwnd);
    char szClass[32];
    LPEXPLORER e;
    int i;

    if (!hwndParent)
        return DT_NONE;
    GetClassName(hwndParent, szClass, sizeof(szClass));
    if (lstrcmpi(szClass, CLS_EXPLORER) != 0)
        return DT_NONE;
    e = GetExplorer(hwndParent);
    if (!e || hwnd != e->hwndTree)
        return DT_NONE;
    ScreenToClient(hwnd, &pt);
    i = (int)SendMessage(hwnd, LB_GETTOPINDEX, 0, 0L) + pt.y / ITEM_CY;
    if (i < 0 || i >= e->nNodes)
        return DT_NONE;
    NodePath(e, i, lpDir);
    return DT_FOLDER;
}

/* Drives were connected or disconnected: rebuild the trees */
void ExplorerRefreshDrives(void)
{
    char szPath[MAXPATHLEN];
    LPEXPLORER e;
    int w;

    for (w = 0; w < s_nExplorers; w++) {
        e = GetExplorer(s_hwndExplorers[w]);
        if (!e)
            continue;
        lstrcpy(szPath, FViewGetPath(e->hwndView));
        SendMessage(e->hwndTree, WM_SETREDRAW, FALSE, 0L);
        DeleteNodes(e, 0, e->nNodes);
        BuildDrives(e);
        SendMessage(e->hwndTree, WM_SETREDRAW, TRUE, 0L);
        if (!PathIsDirectory(szPath)) {
            GetWindowsDirectory(szPath, sizeof(szPath));
            szPath[3] = '\0';
        }
        SelectPath(s_hwndExplorers[w], e, szPath);
    }
}

int ExplorerGetWindows(HWND FAR *lpOut, int nMax)
{
    int i;

    for (i = 0; i < s_nExplorers && i < nMax; i++)
        lpOut[i] = s_hwndExplorers[i];
    return i;
}

void ExplorerCloseAll(void)
{
    while (s_nExplorers > 0)
        DestroyWindow(s_hwndExplorers[s_nExplorers - 1]);
}
