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
 * startmnu.c - the Start menu
 *
 * The top level is owner-drawn (small images and a vertical banner, like
 * Calmira / Windows 95). "Programs" lists the Program Manager groups, and
 * extra entries can be added in NEWSHELL.INI:
 *
 *   [Start Menu]
 *   Item1=File Manager|WINFILE.EXE
 *   Item2=MS-DOS Prompt|DOSPRMPT.PIF
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define SM_MAXITEMS     400     /* launchable entries (CMD_SM_FIRST + n) */
#define SM_MAXTOP       24      /* owner-drawn top-level entries */
#define SM_MAXCUSTOM    16
#define SM_GROUPMAX     64      /* items read per group */

/* ids for the owner-drawn separators, so keyboard navigation can skip them */
#define CMD_SM_SEPARATOR    0x7F00

#define ITEM_H          24
#define SEP_H           8
#define BANNER_W        22

typedef struct tagSMCMD {
    char szCmd[128];
    char szDir[80];
} SMCMD;

typedef struct tagSMTOP {
    char  szText[40];
    int   nImage;       /* SMI_* or -1 to use hIcon */
    HICON hIcon;
    BOOL  bSeparator;
    int   yTop;         /* offset from the top of the menu */
} SMTOP;

static SMCMD FAR *s_lpCmds;
static int    s_nCmds;
static SMTOP  s_top[SM_MAXTOP];
static int    s_nTop;
static int    s_cyMenu;
static int    s_cxText;
static HFONT  s_hfBanner;

static int AddCmd(LPCSTR lpCmd, LPCSTR lpDir)
{
    if (!s_lpCmds) {
        s_lpCmds = (SMCMD FAR *)malloc(SM_MAXITEMS * sizeof(SMCMD));
        if (!s_lpCmds)
            return -1;
    }
    if (s_nCmds >= SM_MAXITEMS)
        return -1;
    lstrcpyn(s_lpCmds[s_nCmds].szCmd, lpCmd, sizeof(s_lpCmds[0].szCmd));
    lstrcpyn(s_lpCmds[s_nCmds].szDir, lpDir ? lpDir : "",
             sizeof(s_lpCmds[0].szDir));
    return s_nCmds++;
}

static int AddTop(LPCSTR lpText, int nImage, HICON hIcon)
{
    SMTOP *p;

    if (s_nTop >= SM_MAXTOP)
        return -1;
    p = &s_top[s_nTop];
    lstrcpyn(p->szText, lpText ? lpText : "", sizeof(p->szText));
    p->nImage = nImage;
    p->hIcon = hIcon;
    p->bSeparator = (lpText == NULL);
    return s_nTop++;
}

static void AppendTop(HMENU hMenu, UINT uFlags, UINT id, LPCSTR lpText,
                      int nImage, HICON hIcon)
{
    int n = AddTop(lpText, nImage, hIcon);

    if (n < 0)
        return;
    if (lpText == NULL) {
        uFlags |= MF_DISABLED;
        id = CMD_SM_SEPARATOR + n;
    }
    AppendMenu(hMenu, uFlags | MF_OWNERDRAW, id, (LPCSTR)(DWORD)n);
}

static int CompareItems(const void *a, const void *b)
{
    return lstrcmpi(((LPPMITEM)a)->szName, ((LPPMITEM)b)->szName);
}

static HMENU BuildProgramsMenu(void)
{
    HMENU hMenu = CreatePopupMenu();
    HMENU hGroup;
    LPPMITEM lpItems;
    char szFile[MAXPATHLEN], szName[40];
    int g, i, n, k;

    lpItems = (LPPMITEM)malloc(SM_GROUPMAX * sizeof(PMITEM));
    if (!lpItems)
        return hMenu;

    for (g = 0; g < PmGroupCount(); g++) {
        if (!PmGroupFile(g, szFile, sizeof(szFile)))
            continue;
        n = PmLoadGroup(szFile, szName, sizeof(szName), lpItems, SM_GROUPMAX);
        if (n < 0)
            continue;
        qsort(lpItems, n, sizeof(PMITEM), CompareItems);
        hGroup = CreatePopupMenu();
        for (i = 0; i < n; i++) {
            k = AddCmd(lpItems[i].szCmd, lpItems[i].szDir);
            if (k < 0)
                break;
            AppendMenu(hGroup, MF_STRING, CMD_SM_FIRST + k, lpItems[i].szName);
        }
        if (n == 0)
            AppendMenu(hGroup, MF_STRING | MF_GRAYED, 0, "(Empty)");
        AppendMenu(hMenu, MF_POPUP, (UINT)hGroup, szName);
    }
    free(lpItems);

    if (GetMenuItemCount(hMenu) == 0)
        AppendMenu(hMenu, MF_STRING | MF_GRAYED, 0, "(No groups)");
    return hMenu;
}

static HMENU BuildSettingsMenu(void)
{
    HMENU hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, CMD_CONTROLPANEL, "&Control Panel");
    AppendMenu(hMenu, MF_STRING, CMD_PRINTERS, "&Print Manager");
    AppendMenu(hMenu, MF_STRING, CMD_WINSETUP, "Windows &Setup");
    return hMenu;
}

/* Finds the full path of a program (searching the PATH) to get its icon */
static HICON CommandIcon(LPCSTR lpCmd)
{
    char szFile[MAXPATHLEN];
    OFSTRUCT of;
    int i;

    for (i = 0; lpCmd[i] && lpCmd[i] != ' ' && i < sizeof(szFile) - 1; i++)
        szFile[i] = lpCmd[i];
    szFile[i] = '\0';
    if (OpenFile(szFile, &of, OF_EXIST) == HFILE_ERROR)
        return IconStock(IDI_APP);
    OemToAnsi(of.szPathName, szFile);
    return IconForFile(szFile, PathIsDirectory(szFile));
}

static void AddCustomItems(HMENU hMenu)
{
    char szKey[16], szValue[200];
    LPSTR lpCmd;
    int i, k, nAdded = 0;
    BOOL bDefaults;

    bDefaults = GetPrivateProfileString("Start Menu", NULL, "", szValue,
                                        sizeof(szValue), NS_INIFILE) == 0;
    for (i = 1; i <= SM_MAXCUSTOM; i++) {
        if (bDefaults) {
            if (i == 1)
                lstrcpy(szValue, "File Manager|WINFILE.EXE");
            else if (i == 2)
                lstrcpy(szValue, "MS-DOS Prompt|DOSPRMPT.PIF");
            else
                break;
        } else {
            wsprintf(szKey, "Item%d", i);
            CfgGetStr("Start Menu", szKey, "", szValue, sizeof(szValue));
            if (szValue[0] == '\0')
                continue;
        }
        for (lpCmd = szValue; *lpCmd && *lpCmd != '|'; lpCmd++)
            ;
        if (*lpCmd != '|')
            continue;
        *lpCmd++ = '\0';
        k = AddCmd(lpCmd, NULL);
        if (k < 0)
            break;
        AppendTop(hMenu, MF_STRING, CMD_SM_FIRST + k, szValue, -1,
                  CommandIcon(lpCmd));
        nAdded++;
    }
    if (nAdded > 0)
        AppendTop(hMenu, MF_STRING, 0, NULL, -1, NULL);
}

static void CreateBannerFont(void)
{
    LOGFONT lf;

    if (s_hfBanner)
        return;
    _fmemset(&lf, 0, sizeof(lf));
    lf.lfHeight = -16;
    lf.lfWeight = FW_BOLD;
    lf.lfEscapement = 900;
    lf.lfOrientation = 900;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, "Arial");
    s_hfBanner = CreateFontIndirect(&lf);
}

static void LayoutTop(void)
{
    HDC hdc = GetDC(NULL);
    HFONT hfOld = SelectObject(hdc, g_hfontUI);
    int i, y = 0, cx;

    s_cxText = 0;
    for (i = 0; i < s_nTop; i++) {
        s_top[i].yTop = y;
        y += s_top[i].bSeparator ? SEP_H : ITEM_H;
        cx = TextWidth(hdc, s_top[i].szText);
        if (cx > s_cxText)
            s_cxText = cx;
    }
    s_cyMenu = y;
    SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);
}

void StartMenuShow(HWND hwndOwner, int x, int y)
{
    HMENU hMenu = CreatePopupMenu();

    s_nCmds = 0;
    s_nTop = 0;
    CreateBannerFont();

    AppendTop(hMenu, MF_POPUP, (UINT)BuildProgramsMenu(), "Programs",
              SMI_PROGRAMS, NULL);
    AppendTop(hMenu, MF_STRING, 0, NULL, -1, NULL);
    AddCustomItems(hMenu);
    AppendTop(hMenu, MF_STRING, CMD_COMPUTER, "Computer", SMI_COMPUTER, NULL);
    AppendTop(hMenu, MF_POPUP, (UINT)BuildSettingsMenu(), "Settings",
              SMI_SETTINGS, NULL);
    AppendTop(hMenu, MF_STRING, CMD_RUN, "Run...", SMI_RUN, NULL);
    AppendTop(hMenu, MF_STRING, 0, NULL, -1, NULL);
    AppendTop(hMenu, MF_STRING, CMD_SHUTDOWN, "Shut Down...", SMI_SHUTDOWN, NULL);
    LayoutTop();

    /* place the menu just above the taskbar (2 pixels of menu border) */
    TrackPopupMenu(hMenu, TPM_LEFTALIGN, x, y - s_cyMenu - 2, 0, hwndOwner,
                   NULL);
    DestroyMenu(hMenu);
}

BOOL StartMenuMeasureItem(MEASUREITEMSTRUCT FAR *lpmis)
{
    int n;

    if (lpmis->CtlType != ODT_MENU)
        return FALSE;
    n = (int)lpmis->itemData;
    if (n < 0 || n >= s_nTop)
        return FALSE;
    lpmis->itemHeight = s_top[n].bSeparator ? SEP_H : ITEM_H;
    lpmis->itemWidth = BANNER_W + 8 + 16 + 8 + s_cxText + 24;
    return TRUE;
}

BOOL StartMenuDrawItem(DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem;
    RECT rcBanner, rcText;
    SMTOP *p;
    int n, cy;
    BOOL bSel;

    if (lpdis->CtlType != ODT_MENU)
        return FALSE;
    n = (int)lpdis->itemData;
    if (n < 0 || n >= s_nTop)
        return FALSE;
    p = &s_top[n];
    bSel = (lpdis->itemState & ODS_SELECTED) && !p->bSeparator;

    /* banner strip: each item paints its slice of the vertical title */
    SetRect(&rcBanner, rc.left, rc.top, rc.left + BANNER_W, rc.bottom);
    FillSolid(hdc, &rcBanner, GetSysColor(COLOR_BTNSHADOW));
    if (s_hfBanner) {
        HFONT hfOld = SelectObject(hdc, s_hfBanner);
        int nOldMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF clrOld = SetTextColor(hdc, GetSysColor(COLOR_BTNFACE));
        int yBase = rc.top - p->yTop + s_cyMenu - 4;

        ExtTextOut(hdc, rc.left + 2, yBase, ETO_CLIPPED, &rcBanner,
                   NS_APPNAME, lstrlen(NS_APPNAME), NULL);
        SetTextColor(hdc, clrOld);
        SetBkMode(hdc, nOldMode);
        SelectObject(hdc, hfOld);
    }

    rc.left += BANNER_W;
    if (p->bSeparator) {
        FillSolid(hdc, &rc, GetSysColor(COLOR_MENU));
        cy = rc.top + (rc.bottom - rc.top) / 2 - 1;
        SetRect(&rcText, rc.left + 2, cy, rc.right - 2, cy + 2);
        Draw3DEdge(hdc, &rcText, TRUE, FALSE);
        return TRUE;
    }

    FillSolid(hdc, &rc, GetSysColor(bSel ? COLOR_HIGHLIGHT : COLOR_MENU));
    cy = rc.top + (rc.bottom - rc.top - 16) / 2;
    if (p->nImage >= 0)
        DrawSmallImage(hdc, rc.left + 6, cy, p->nImage);
    else if (p->hIcon)
        DrawIconSmall(hdc, rc.left + 6, cy, p->hIcon,
                      GetSysColor(bSel ? COLOR_HIGHLIGHT : COLOR_MENU));

    {
        HFONT hfOld = SelectObject(hdc, g_hfontUI);
        int nOldMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF clrOld = SetTextColor(hdc,
            GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));

        SetRect(&rcText, rc.left + 6 + 16 + 8, rc.top, rc.right, rc.bottom);
        DrawText(hdc, p->szText, -1, &rcText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(hdc, clrOld);
        SetBkMode(hdc, nOldMode);
        SelectObject(hdc, hfOld);
    }
    return TRUE;
}

/*
 * Windows 3.1 lets the keyboard select disabled owner-drawn items, so when a
 * separator is reached with the arrow keys, move on in the same direction.
 */
void StartMenuSelect(HWND hwnd, UINT id, UINT uFlags)
{
    if (!(uFlags & MF_OWNERDRAW) || id < CMD_SM_SEPARATOR
        || id >= CMD_SM_SEPARATOR + SM_MAXTOP)
        return;
    if (GetKeyState(VK_DOWN) & 0x8000)
        PostMessage(hwnd, WM_KEYDOWN, VK_DOWN, 0L);
    else if (GetKeyState(VK_UP) & 0x8000)
        PostMessage(hwnd, WM_KEYDOWN, VK_UP, 0L);
}

BOOL StartMenuCommand(HWND hwnd, UINT id)
{
    if (id >= CMD_SM_FIRST && id < CMD_SM_FIRST + SM_MAXITEMS) {
        int k = id - CMD_SM_FIRST;
        if (s_lpCmds && k < s_nCmds)
            LaunchCommand(hwnd, s_lpCmds[k].szCmd, s_lpCmds[k].szDir,
                          SW_SHOWNORMAL);
        return TRUE;
    }

    switch (id) {
    case CMD_RUN:           RunDialog(hwnd); break;
    case CMD_SHUTDOWN:      DoShutdownDialog(); break;
    case CMD_ABOUT:         AboutDialog(hwnd); break;
    case CMD_COMPUTER:      FolderOpen(""); break;
    case CMD_CONTROLPANEL:  LaunchCommand(hwnd, "CONTROL.EXE", NULL, SW_SHOWNORMAL); break;
    case CMD_PRINTERS:      LaunchCommand(hwnd, "PRINTMAN.EXE", NULL, SW_SHOWNORMAL); break;
    case CMD_WINSETUP:      LaunchCommand(hwnd, "SETUP.EXE", NULL, SW_SHOWNORMAL); break;
    case CMD_FILEMANAGER:   LaunchCommand(hwnd, "WINFILE.EXE", NULL, SW_SHOWNORMAL); break;
    case CMD_DOSPROMPT:     LaunchCommand(hwnd, "DOSPRMPT.PIF", NULL, SW_SHOWNORMAL); break;
    case CMD_TASKMAN:       LaunchCommand(hwnd, "TASKMAN.EXE", NULL, SW_SHOWNORMAL); break;
    case CMD_CLOSESHELL:    CloseShell(); break;
    default:
        return FALSE;
    }
    return TRUE;
}

void StartMenuFree(void)
{
    if (s_lpCmds) {
        free(s_lpCmds);
        s_lpCmds = NULL;
    }
    if (s_hfBanner) {
        DeleteObject(s_hfBanner);
        s_hfBanner = NULL;
    }
}
