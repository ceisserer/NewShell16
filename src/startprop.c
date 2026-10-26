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
 * startprop.c - Start Menu Properties: the Start menu editor
 *
 * Like Calmira's, it shows NSSTART.INI as a tree that can be edited:
 * items and submenus are added, deleted, moved and given properties, and
 * Program Manager groups can be imported. The tree is a flat array of
 * nodes in menu order (a node's submenu follows it with a deeper level),
 * shown in an owner-drawn list box; OK writes NSSTART.INI anew. The
 * dialog also holds the look of the Start menu ([Start Menu] in
 * NEWSHELL.INI).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "newshell.h"
#include <commdlg.h>

#define MAX_NODES       500
#define ITEM_CY         18
#define INDENT          16
#define MAX_DEPTH       8

#define NF_SUB          0x01
#define NF_SEP          0x02
#define NF_OPEN         0x04

typedef struct tagSPNODE {
    char szText[40];
    BYTE bLevel;
    BYTE bFlags;
    BYTE bShow;
    BYTE bIconIndex;
    char szCmd[128];
    char szDir[80];
    char szIcon[80];
} SPNODE, FAR *LPSPNODE;

static LPSPNODE *s_lpNodes;         /* MAX_NODES pointers, in menu order */
static int       s_nNodes;
static HWND      s_hwndList;
static BOOL      s_bChanged;
static LOGFONT   s_lfMenu;
static BOOL      s_bFont;           /* s_lfMenu holds a chosen font */

/* -------------------------------------------------------------------- */
/* Nodes                                                                */
/* -------------------------------------------------------------------- */

static LPSPNODE NewNode(void)
{
    LPSPNODE p = (LPSPNODE)malloc(sizeof(SPNODE));

    if (p)
        _fmemset(p, 0, sizeof(SPNODE));
    return p;
}

static void FreeNodes(void)
{
    while (s_nNodes > 0)
        free(s_lpNodes[--s_nNodes]);
}

/* One past the last node of i's subtree */
static int SubtreeEnd(int i)
{
    int j = i + 1;

    while (j < s_nNodes && s_lpNodes[j]->bLevel > s_lpNodes[i]->bLevel)
        j++;
    return j;
}

static BOOL InsertNode(int nAt, LPSPNODE p)
{
    if (s_nNodes >= MAX_NODES) {
        free(p);
        return FALSE;
    }
    _fmemmove(&s_lpNodes[nAt + 1], &s_lpNodes[nAt], (s_nNodes - nAt) * sizeof(LPSPNODE));
    s_lpNodes[nAt] = p;
    s_nNodes++;
    return TRUE;
}

static void DeleteNodes(int nAt, int nCount)
{
    int i;

    for (i = 0; i < nCount; i++)
        free(s_lpNodes[nAt + i]);
    _fmemmove(&s_lpNodes[nAt], &s_lpNodes[nAt + nCount],
              (s_nNodes - nAt - nCount) * sizeof(LPSPNODE));
    s_nNodes -= nCount;
}

/* "cmd;dir;show;icon;index" */
static void ParseNode(LPSPNODE p, LPSTR lpValue)
{
    LPSTR f[5];
    int i, n = 0;

    f[0] = lpValue;
    for (i = 1; i < 5; i++)
        f[i] = "";
    for (; *lpValue && n < 4; lpValue++)
        if (*lpValue == ';') {
            *lpValue = '\0';
            f[++n] = lpValue + 1;
        }
    lstrcpyn(p->szCmd, f[0], sizeof(p->szCmd));
    lstrcpyn(p->szDir, f[1], sizeof(p->szDir));
    p->bShow = (BYTE)atoi(f[2]);
    lstrcpyn(p->szIcon, f[3], sizeof(p->szIcon));
    p->bIconIndex = (BYTE)atoi(f[4]);
}

static void LoadSection(LPCSTR lpSection, int nLevel)
{
    char *pKeys, *pKey;
    char szValue[300], szSub[160];
    LPSPNODE p;
    int n;

    if (nLevel > MAX_DEPTH)
        return;
    pKeys = (char *)malloc(SM_KEYBUF);
    if (!pKeys)
        return;
    GetPrivateProfileString(lpSection, NULL, "", pKeys, SM_KEYBUF, NS_STARTINI);
    for (pKey = pKeys; *pKey; pKey += lstrlen(pKey) + 1) {
        if (!(p = NewNode()) || !InsertNode(s_nNodes, p))
            break;
        p->bLevel = (BYTE)nLevel;
        if (pKey[0] == '-') {
            p->bFlags = NF_SEP;
            continue;
        }
        GetPrivateProfileString(lpSection, pKey, "", szValue, sizeof(szValue), NS_STARTINI);
        ParseNode(p, szValue);
        lstrcpyn(p->szText, pKey, sizeof(p->szText));
        n = lstrlen(pKey);
        if (n > 1 && pKey[n - 1] == '*') {
            if (n - 1 < sizeof(p->szText))
                p->szText[n - 1] = '\0';
            p->bFlags = NF_SUB;
            if (nLevel == 0)
                p->bFlags |= NF_OPEN;
            if (lstrlen(lpSection) + n + 2 < sizeof(szSub)) {
                wsprintf(szSub, "%s\\%s", lpSection, (LPSTR)p->szText);
                LoadSection(szSub, nLevel + 1);
            }
        }
    }
    free(pKeys);
}

/* Writes the nodes [i, end) at nLevel into lpSection; returns the end */
static int SaveSection(LPCSTR lpSection, int i, int nLevel)
{
    char szValue[300], szKey[48], szSub[160], szProbe[4];
    LPSPNODE p;
    int nSep = 1, n;

    while (i < s_nNodes && s_lpNodes[i]->bLevel >= nLevel) {
        p = s_lpNodes[i];
        if (p->bLevel > nLevel) {           /* orphan: skip */
            i++;
            continue;
        }
        if (p->bFlags & NF_SEP) {
            wsprintf(szKey, "-%d", nSep++);
            WritePrivateProfileString(lpSection, szKey, ";;0;;0", NS_STARTINI);
            i++;
            continue;
        }
        lstrcpy(szKey, p->szText);
        if (p->bFlags & NF_SUB)
            lstrcat(szKey, "*");
        /* keys of a section must differ */
        for (n = 2; n < 100 && GetPrivateProfileString(lpSection, szKey, "", szProbe,
                                                        sizeof(szProbe), NS_STARTINI); n++)
            wsprintf(szKey, (p->bFlags & NF_SUB) ? "%s %d*" : "%s %d", (LPSTR)p->szText, n);
        wsprintf(szValue, "%s;%s;%d;%s;%d", (LPSTR)p->szCmd, (LPSTR)p->szDir, p->bShow,
                 (LPSTR)p->szIcon, p->bIconIndex);
        /* separator-like keys and empty values would be misread */
        if (szValue[0] == '\0')
            lstrcpy(szValue, ";;0;;0");
        WritePrivateProfileString(lpSection, szKey, szValue, NS_STARTINI);
        i++;
        if (p->bFlags & NF_SUB) {
            szKey[lstrlen(szKey) - 1] = '\0';
            if (lstrlen(lpSection) + lstrlen(szKey) + 2 < sizeof(szSub)) {
                wsprintf(szSub, "%s\\%s", lpSection, (LPSTR)szKey);
                i = SaveSection(szSub, i, nLevel + 1);
            }
        }
    }
    return i;
}

static void SaveMenu(void)
{
    char szPath[MAXPATHLEN];

    /* write the cached file, remove it and write the tree from scratch */
    WritePrivateProfileString(NULL, NULL, NULL, NS_STARTINI);
    GetWindowsDirectory(szPath, sizeof(szPath));
    PathAppend(szPath, NS_STARTINI);
    remove(szPath);
    SaveSection("Start", 0, 0);
    if (s_nNodes == 0)
        WritePrivateProfileString("Start", "(Empty)", ";;0;;0", NS_STARTINI);
    WritePrivateProfileString(NULL, NULL, NULL, NS_STARTINI);
}

/* -------------------------------------------------------------------- */
/* The list                                                             */
/* -------------------------------------------------------------------- */

/* Shows the nodes whose ancestors are all open; keeps lpSel selected */
static void Rebuild(LPSPNODE lpSel)
{
    int i, nHidden = 0, nTop, nSel = -1;

    nTop = (int)SendMessage(s_hwndList, LB_GETTOPINDEX, 0, 0L);
    SendMessage(s_hwndList, WM_SETREDRAW, FALSE, 0L);
    SendMessage(s_hwndList, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < s_nNodes; i++) {
        LPSPNODE p = s_lpNodes[i];

        if (nHidden && p->bLevel >= nHidden)
            continue;
        nHidden = 0;
        if (p == lpSel)
            nSel = (int)SendMessage(s_hwndList, LB_GETCOUNT, 0, 0L);
        SendMessage(s_hwndList, LB_ADDSTRING, 0, (LPARAM)i);
        if ((p->bFlags & NF_SUB) && !(p->bFlags & NF_OPEN))
            nHidden = p->bLevel + 1;
    }
    SendMessage(s_hwndList, LB_SETTOPINDEX, nTop, 0L);
    if (nSel >= 0)
        SendMessage(s_hwndList, LB_SETCURSEL, nSel, 0L);
    SendMessage(s_hwndList, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(s_hwndList, NULL, TRUE);
}

/* The node selected in the list, or -1 */
static int SelectedNode(void)
{
    int i = (int)SendMessage(s_hwndList, LB_GETCURSEL, 0, 0L);

    if (i < 0)
        return -1;
    return (int)SendMessage(s_hwndList, LB_GETITEMDATA, i, 0L);
}

static void NodeToShortcut(LPSPNODE p, LPSHORTCUT lpsc)
{
    _fmemset(lpsc, 0, sizeof(SHORTCUT));
    lstrcpyn(lpsc->szCaption, p->szText, sizeof(lpsc->szCaption));
    lstrcpyn(lpsc->szCmd, p->szCmd, sizeof(lpsc->szCmd));
    lstrcpyn(lpsc->szDir, p->szDir, sizeof(lpsc->szDir));
    lpsc->nShow = p->bShow;
    if (p->szIcon[0] != '<')
        lstrcpyn(lpsc->szIconFile, p->szIcon, sizeof(lpsc->szIconFile));
    lpsc->nIconIndex = p->bIconIndex;
}

static void DrawNode(DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText;
    BOOL bSel = (lpdis->itemState & ODS_SELECTED) != 0;
    LPSPNODE p;
    HFONT hfOld;
    int i = (int)lpdis->itemData, x, cy;

    FillSolid(hdc, &rc, GetSysColor(COLOR_WINDOW));
    if ((int)lpdis->itemID < 0 || i < 0 || i >= s_nNodes)
        return;
    p = s_lpNodes[i];
    x = rc.left + 2 + p->bLevel * INDENT;
    cy = rc.top + (rc.bottom - rc.top) / 2;

    if (p->bFlags & NF_SUB) {
        HBRUSH hbr = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        RECT rcBox;

        SetRect(&rcBox, x, cy - 4, x + 9, cy + 5);
        FrameRect(hdc, &rcBox, hbr);
        DeleteObject(hbr);
        SetRect(&rcBox, x + 2, cy, x + 7, cy + 1);
        FillSolid(hdc, &rcBox, GetSysColor(COLOR_WINDOWTEXT));
        if (!(p->bFlags & NF_OPEN)) {
            SetRect(&rcBox, x + 4, cy - 2, x + 5, cy + 3);
            FillSolid(hdc, &rcBox, GetSysColor(COLOR_WINDOWTEXT));
        }
    }
    x += 12;
    if (p->bFlags & NF_SEP) {
        SetRect(&rcText, x, cy - 1, rc.right - 8, cy + 1);
        Draw3DEdge(hdc, &rcText, TRUE, FALSE);
        if (lpdis->itemState & ODS_FOCUS) {
            SetRect(&rcText, x - 2, rc.top + 1, rc.right - 6, rc.bottom - 1);
            DrawItemFocus(hdc, &rcText, FALSE);
        }
        return;
    }
    {
        int nImage = p->szIcon[0] == '<' ? StartMenuImage(p->szIcon) : -1;

        if (nImage < 0 && (p->bFlags & NF_SUB) && !p->szIcon[0])
            nImage = SMI_FOLDER;
        if (nImage >= 0)
            DrawSmallImage(hdc, x, rc.top + 1, nImage);
        else {
            SHORTCUT sc;

            NodeToShortcut(p, &sc);
            DrawIconSmall(hdc, x, rc.top + 1, ShortcutIconOf(&sc), GetSysColor(COLOR_WINDOW));
        }
    }
    x += 20;
    hfOld = SelectObject(hdc, g_hfontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetRect(&rcText, x, rc.top + 1, x + TextWidth(hdc, p->szText) + 6, rc.bottom - 1);
    if (bSel)
        FillSolid(hdc, &rcText, GetSysColor(COLOR_HIGHLIGHT));
    SetTextColor(hdc, GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    rcText.left += 2;
    DrawText(hdc, p->szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    rcText.left -= 2;
    if (lpdis->itemState & ODS_FOCUS)
        DrawItemFocus(hdc, &rcText, bSel);
    SelectObject(hdc, hfOld);
}

/* -------------------------------------------------------------------- */
/* Editing                                                              */
/* -------------------------------------------------------------------- */

static BOOL EditNode(HWND hDlg, LPSPNODE p, LPCSTR lpTitle)
{
    SHORTCUT sc;

    NodeToShortcut(p, &sc);
    sc.bNoCommand = (p->bFlags & NF_SUB) != 0;
    if (!ShortcutDialog(hDlg, &sc, lpTitle))
        return FALSE;
    lstrcpyn(p->szText, sc.szCaption, sizeof(p->szText));
    if (!(p->bFlags & NF_SUB)) {
        lstrcpyn(p->szCmd, sc.szCmd, sizeof(p->szCmd));
        lstrcpyn(p->szDir, sc.szDir, sizeof(p->szDir));
        p->bShow = (BYTE)sc.nShow;
    }
    /* keep a built-in image unless another icon was picked */
    if (sc.szIconFile[0] || p->szIcon[0] != '<') {
        lstrcpyn(p->szIcon, sc.szIconFile, sizeof(p->szIcon));
        p->bIconIndex = (BYTE)sc.nIconIndex;
    }
    s_bChanged = TRUE;
    return TRUE;
}

/*
 * Where a new node goes: into a selected submenu (at its end), or after
 * the selected item. Sets *pnLevel.
 */
static int NewPosition(int FAR *pnLevel)
{
    int i = SelectedNode();

    if (i < 0) {
        *pnLevel = 0;
        return s_nNodes;
    }
    if ((s_lpNodes[i]->bFlags & NF_SUB) && s_lpNodes[i]->bLevel < MAX_DEPTH) {
        s_lpNodes[i]->bFlags |= NF_OPEN;
        *pnLevel = s_lpNodes[i]->bLevel + 1;
    } else
        *pnLevel = s_lpNodes[i]->bLevel;
    return SubtreeEnd(i);
}

static void AddNode(HWND hDlg, BYTE bFlags)
{
    LPSPNODE p = NewNode();
    int nLevel, nAt;

    if (!p)
        return;
    nAt = NewPosition(&nLevel);
    p->bLevel = (BYTE)nLevel;
    p->bFlags = bFlags;
    if (!(bFlags & NF_SEP)) {
        if (!EditNode(hDlg, p, (bFlags & NF_SUB) ? "New Submenu" : "New Item")) {
            free(p);
            return;
        }
        if (bFlags & NF_SUB)
            p->bFlags |= NF_OPEN;
    }
    if (!InsertNode(nAt, p)) {
        MessageBox(hDlg, "The Start menu is full.", NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    s_bChanged = TRUE;
    Rebuild(p);
}

static void DeleteSelected(HWND hDlg)
{
    char szMsg[96];
    int i = SelectedNode(), nEnd;
    LPSPNODE pNext;

    if (i < 0)
        return;
    nEnd = SubtreeEnd(i);
    if (nEnd > i + 1) {
        wsprintf(szMsg, "Delete the submenu '%s' and everything in it?",
                 (LPSTR)s_lpNodes[i]->szText);
        if (MessageBox(hDlg, szMsg, "Start Menu", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }
    DeleteNodes(i, nEnd - i);
    pNext = i < s_nNodes ? s_lpNodes[i] : i > 0 ? s_lpNodes[i - 1] : NULL;
    s_bChanged = TRUE;
    Rebuild(pNext);
}

/* Moves the selected node (with its submenu) before the previous or
   after the next node of the same level */
static void MoveSelected(BOOL bDown)
{
    LPSPNODE *tmp;
    int i = SelectedNode(), nEnd, j, nLen, nOther, nOtherEnd;
    LPSPNODE p;

    if (i < 0)
        return;
    p = s_lpNodes[i];
    nEnd = SubtreeEnd(i);
    nLen = nEnd - i;
    tmp = (LPSPNODE *)malloc(nLen * sizeof(LPSPNODE));
    if (!tmp)
        return;
    if (bDown) {
        if (nEnd >= s_nNodes || s_lpNodes[nEnd]->bLevel != p->bLevel) {
            free(tmp);
            return;
        }
        nOther = nEnd;
        nOtherEnd = SubtreeEnd(nOther);
        /* [i..nEnd) [nOther..nOtherEnd) -> [nOther..) [i..) */
        _fmemcpy(tmp, &s_lpNodes[i], nLen * sizeof(LPSPNODE));
        _fmemmove(&s_lpNodes[i], &s_lpNodes[nOther], (nOtherEnd - nOther) * sizeof(LPSPNODE));
        _fmemcpy(&s_lpNodes[i + nOtherEnd - nOther], tmp, nLen * sizeof(LPSPNODE));
    } else {
        for (j = i - 1; j >= 0 && s_lpNodes[j]->bLevel > p->bLevel; j--)
            ;
        if (j < 0 || s_lpNodes[j]->bLevel != p->bLevel) {
            free(tmp);
            return;
        }
        /* [j..i) [i..nEnd) -> [i..nEnd) [j..i) */
        _fmemcpy(tmp, &s_lpNodes[i], nLen * sizeof(LPSPNODE));
        _fmemmove(&s_lpNodes[j + nLen], &s_lpNodes[j], (i - j) * sizeof(LPSPNODE));
        _fmemcpy(&s_lpNodes[j], tmp, nLen * sizeof(LPSPNODE));
    }
    free(tmp);
    s_bChanged = TRUE;
    Rebuild(p);
}

static void Toggle(int i)
{
    if (i < 0 || !(s_lpNodes[i]->bFlags & NF_SUB))
        return;
    s_lpNodes[i]->bFlags ^= NF_OPEN;
    Rebuild(s_lpNodes[i]);
}

/* The submenu Programs (by name, ignoring '&'), or -1 */
static int ProgramsNode(void)
{
    char szSection[64], szName[48];
    int i;

    StartMenuProgramsSection(szSection, sizeof(szSection));
    for (i = 0; i < s_nNodes; i++)
        if (s_lpNodes[i]->bLevel == 0 && (s_lpNodes[i]->bFlags & NF_SUB)) {
            wsprintf(szName, "Start\\%s", (LPSTR)s_lpNodes[i]->szText);
            if (lstrcmpi(szName, szSection) == 0)
                return i;
        }
    return -1;
}

/* Adds the Program Manager groups that are not in Programs yet */
static void ImportGroups(HWND hDlg)
{
    char szFile[MAXPATHLEN], szGroup[40], szMsg[80];
    LPPMITEM lpItems;
    LPSPNODE p;
    int nProg, g, i, j, n, nAt, nAdded = 0;

    nProg = ProgramsNode();
    if (nProg < 0) {
        MessageBox(hDlg, "The Start menu has no Programs submenu.", NS_APPNAME,
                   MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    lpItems = (LPPMITEM)malloc(64 * sizeof(PMITEM));
    if (!lpItems)
        return;
    for (g = 0; g < PmGroupCount(); g++) {
        if (!PmGroupFile(g, szFile, sizeof(szFile)))
            continue;
        n = PmLoadGroup(szFile, szGroup, sizeof(szGroup), lpItems, 64);
        if (n < 0)
            continue;
        for (j = nProg + 1; j < SubtreeEnd(nProg); j++)
            if (s_lpNodes[j]->bLevel == s_lpNodes[nProg]->bLevel + 1
                && lstrcmpi(s_lpNodes[j]->szText, szGroup) == 0)
                break;
        if (j < SubtreeEnd(nProg))
            continue;
        nAt = SubtreeEnd(nProg);
        if (!(p = NewNode()))
            break;
        lstrcpyn(p->szText, szGroup, sizeof(p->szText));
        p->bLevel = (BYTE)(s_lpNodes[nProg]->bLevel + 1);
        p->bFlags = NF_SUB;
        if (!InsertNode(nAt++, p))
            break;
        for (i = 0; i < n; i++) {
            if (!(p = NewNode()))
                break;
            lstrcpyn(p->szText, lpItems[i].szName, sizeof(p->szText));
            lstrcpyn(p->szCmd, lpItems[i].szCmd, sizeof(p->szCmd));
            lstrcpyn(p->szDir, lpItems[i].szDir, sizeof(p->szDir));
            lstrcpyn(p->szIcon, lpItems[i].szIconPath, sizeof(p->szIcon));
            p->bIconIndex = (BYTE)lpItems[i].nIconIndex;
            p->bShow = (BYTE)(lpItems[i].bMinimized ? 1 : 0);
            p->bLevel = (BYTE)(s_lpNodes[nProg]->bLevel + 2);
            if (!InsertNode(nAt++, p))
                break;
        }
        nAdded++;
    }
    free(lpItems);
    s_lpNodes[nProg]->bFlags |= NF_OPEN;
    if (nAdded)
        s_bChanged = TRUE;
    Rebuild(s_lpNodes[nProg]);
    wsprintf(szMsg, nAdded ? "%d group(s) were added to Programs."
                           : "All Program Manager groups are in the Start menu.", nAdded);
    MessageBox(hDlg, szMsg, "Import Groups", MB_OK | MB_ICONINFORMATION);
}

static void ChooseMenuFont(HWND hDlg)
{
    CHOOSEFONT cf;
    LOGFONT lf;

    if (s_bFont)
        lf = s_lfMenu;
    else
        GetObject(GetMenuFont(), sizeof(lf), &lf);
    _fmemset(&cf, 0, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hDlg;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
    if (ChooseFont(&cf)) {
        s_lfMenu = lf;
        s_bFont = TRUE;
    }
}

/* -------------------------------------------------------------------- */
/* Dialog                                                               */
/* -------------------------------------------------------------------- */

static void UpdateButtons(HWND hDlg)
{
    int i = SelectedNode();
    BOOL bSel = i >= 0;

    EnableWindow(GetDlgItem(hDlg, IDC_SPDELETE), bSel);
    EnableWindow(GetDlgItem(hDlg, IDC_SPEDIT), bSel && !(s_lpNodes[i]->bFlags & NF_SEP));
    EnableWindow(GetDlgItem(hDlg, IDC_SPUP), bSel);
    EnableWindow(GetDlgItem(hDlg, IDC_SPDOWN), bSel);
}

static void SaveOptions(HWND hDlg)
{
    char szFont[64];
    BOOL bOK;
    int n;

    CfgSetInt("Start Menu", "Win95Look", IsDlgButtonChecked(hDlg, IDC_SP3D) != 0);
    CfgSetInt("Start Menu", "ColouredBar", IsDlgButtonChecked(hDlg, IDC_SPCOLBAR) != 0);
    CfgSetInt("Start Menu", "BoldSelect", IsDlgButtonChecked(hDlg, IDC_SPBOLD) != 0);
    CfgSetInt("Start Menu", "LargeTop", IsDlgButtonChecked(hDlg, IDC_SPLARGE) != 0);
    CfgSetInt("Start Menu", "MouseUp", IsDlgButtonChecked(hDlg, IDC_SPMOUSEUP) != 0);
    CfgSetInt("Startup", "ProgmanDDE", IsDlgButtonChecked(hDlg, IDC_SPDDE) != 0);
    n = GetDlgItemInt(hDlg, IDC_SPHEIGHT, &bOK, FALSE);
    CfgSetInt("Start Menu", "ItemHeight", bOK && n >= 16 && n <= 64 ? n : 34);
    if (s_bFont) {
        HDC hdc = GetDC(NULL);
        int nPoints = MulDiv(s_lfMenu.lfHeight < 0 ? -s_lfMenu.lfHeight : s_lfMenu.lfHeight,
                             72, GetDeviceCaps(hdc, LOGPIXELSY));

        ReleaseDC(NULL, hdc);
        wsprintf(szFont, "%s,%d,%d,%d", (LPSTR)s_lfMenu.lfFaceName, nPoints,
                 s_lfMenu.lfWeight, s_lfMenu.lfItalic ? 1 : 0);
        CfgSetStr("Start Menu", "Font", szFont);
    }
}

BOOL CALLBACK NSEXPORT StartPropDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                        LPARAM lParam)
{
    int i;

    switch (msg) {
    case WM_INITDIALOG:
        s_hwndList = GetDlgItem(hDlg, IDC_SPTREE);
        s_bChanged = FALSE;
        s_bFont = FALSE;
        StartMenuEnsure();
        LoadSection("Start", 0);
        Rebuild(s_nNodes ? s_lpNodes[0] : NULL);
        CheckDlgButton(hDlg, IDC_SP3D, CfgGetInt("Start Menu", "Win95Look", 1));
        CheckDlgButton(hDlg, IDC_SPCOLBAR, CfgGetInt("Start Menu", "ColouredBar", 1));
        CheckDlgButton(hDlg, IDC_SPBOLD, CfgGetInt("Start Menu", "BoldSelect", 0));
        CheckDlgButton(hDlg, IDC_SPLARGE, CfgGetInt("Start Menu", "LargeTop", 1));
        CheckDlgButton(hDlg, IDC_SPMOUSEUP, CfgGetInt("Start Menu", "MouseUp", 0));
        CheckDlgButton(hDlg, IDC_SPDDE, CfgGetInt("Startup", "ProgmanDDE", 1));
        SetDlgItemInt(hDlg, IDC_SPHEIGHT, CfgGetInt("Start Menu", "ItemHeight", 34), FALSE);
        UpdateButtons(hDlg);
        return TRUE;

    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lParam)->itemHeight = ITEM_CY;
        return TRUE;

    case WM_DRAWITEM:
        DrawNode((DRAWITEMSTRUCT FAR *)lParam);
        return TRUE;

    case WM_VKEYTOITEM:
        if ((HWND)LOWORD(lParam) != s_hwndList)
            return -1;
        i = SelectedNode();
        switch (wParam) {
        case VK_RIGHT:
            if (i >= 0 && (s_lpNodes[i]->bFlags & NF_SUB) && !(s_lpNodes[i]->bFlags & NF_OPEN))
                Toggle(i);
            return -2;
        case VK_LEFT:
            if (i >= 0 && (s_lpNodes[i]->bFlags & NF_SUB) && (s_lpNodes[i]->bFlags & NF_OPEN))
                Toggle(i);
            return -2;
        case VK_DELETE:
            DeleteSelected(hDlg);
            UpdateButtons(hDlg);
            return -2;
        case VK_INSERT:
            AddNode(hDlg, 0);
            UpdateButtons(hDlg);
            return -2;
        }
        return -1;

    case WM_COMMAND:
        switch (wParam) {
        case IDC_SPTREE:
            if (HIWORD(lParam) == LBN_SELCHANGE)
                UpdateButtons(hDlg);
            else if (HIWORD(lParam) == LBN_DBLCLK) {
                i = SelectedNode();
                if (i >= 0 && (s_lpNodes[i]->bFlags & NF_SUB))
                    Toggle(i);
                else if (i >= 0 && !(s_lpNodes[i]->bFlags & NF_SEP)
                         && EditNode(hDlg, s_lpNodes[i], NULL))
                    InvalidateRect(s_hwndList, NULL, TRUE);
            }
            return TRUE;
        case IDC_SPNEWITEM:     AddNode(hDlg, 0); break;
        case IDC_SPNEWSUB:      AddNode(hDlg, NF_SUB); break;
        case IDC_SPNEWSEP:      AddNode(hDlg, NF_SEP); break;
        case IDC_SPDELETE:      DeleteSelected(hDlg); break;
        case IDC_SPUP:          MoveSelected(FALSE); break;
        case IDC_SPDOWN:        MoveSelected(TRUE); break;
        case IDC_SPIMPORT:      ImportGroups(hDlg); break;
        case IDC_SPFONT:        ChooseMenuFont(hDlg); break;
        case IDC_SPEDIT:
            i = SelectedNode();
            if (i >= 0 && EditNode(hDlg, s_lpNodes[i], NULL))
                InvalidateRect(s_hwndList, NULL, TRUE);
            break;
        case IDOK:
            if (s_bChanged)
                SaveMenu();
            SaveOptions(hDlg);
            FreeNodes();
            EndDialog(hDlg, IDOK);
            StartMenuOptionsChanged();
            return TRUE;
        case IDCANCEL:
            FreeNodes();
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
        UpdateButtons(hDlg);
        SetFocus(s_hwndList);
        return TRUE;
    }
    return FALSE;
}

void StartMenuPropertiesDialog(HWND hwndOwner)
{
    static BOOL s_bOpen;
    FARPROC lpfn;

    if (s_bOpen)
        return;
    if (!s_lpNodes)
        s_lpNodes = (LPSPNODE *)malloc(MAX_NODES * sizeof(LPSPNODE));
    if (!s_lpNodes)
        return;
    s_bOpen = TRUE;
    lpfn = MakeProcInstance((FARPROC)StartPropDlgProc, g_hInst);
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_STARTPROPS), hwndOwner, (DLGPROC)lpfn);
    FreeProcInstance(lpfn);
    s_bOpen = FALSE;
}
