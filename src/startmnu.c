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
 * The menu is described by NSSTART.INI in Calmira's START.INI format:
 *
 *   [Start]
 *   &Programs*=;;0;<programs>;0          a '*' marks a submenu, whose
 *   &Run...=$Run;;0;<run>;0              items are in [Start\&Programs]
 *   -1=;;0;;0                            keys starting with '-' separate
 *
 * Values are "command;directory;showmode;iconfile;iconindex". Commands
 * starting with '$' are built-in macros; iconfile may name one of the
 * built-in images such as <programs>. When NSSTART.INI does not exist it
 * is created from the Program Manager groups.
 *
 * All items are owner-drawn; the top level gets large rows and a vertical
 * banner like Calmira / Windows 95.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define START_INI       "NSSTART.INI"
#define SM_MAXITEMS     240
#define SM_MAXKEYS      4096
#define SM_GROUPMAX     64

#define TOP_H           24
#define SUB_H           20
#define SEP_H           8
#define BANNER_W        22

/* item flags */
#define SF_TOP          0x01
#define SF_SEP          0x02
#define SF_SUBMENU      0x04
#define SF_ICONDONE     0x08
#define SF_EMPTY        0x10

typedef struct tagSMITEM {
    char   szText[40];
    char   szCmd[80];
    char   szDir[48];
    char   szIcon[48];
    BYTE   bShow;
    BYTE   bIconIndex;
    BYTE   bFlags;
    signed char nImage;         /* SMI_* or -1 */
    HICON  hIcon;
    HMENU  hMenu;               /* menu holding the item */
    int    nPos;                /* position in that menu */
    int    yTop;                /* top-level items: offset from menu top */
} SMITEM, FAR *LPSMITEM;

static LPSMITEM s_lpItems;
static int      s_nItems;
static int      s_cyMenu;
static HFONT    s_hfBanner;

static const char *s_szImageNames[] = {
    "<programs>", "<documents>", "<settings>", "<find>", "<run>", "<shutdown>",
    "<system>", "<folder>", "<help>", "<printer>", "<control>", "<about>",
    "<dos>", "<application>", "<start>", "<explorer>", "<bin>", "<taskman>",
    "<favorites>", "<desktop>", "<taskbar>"
};
static const signed char s_nImageMap[] = {
    SMI_PROGRAMS, SMI_DOCUMENT, SMI_SETTINGS, SMI_FIND, SMI_RUN, SMI_SHUTDOWN,
    SMI_COMPUTER, SMI_FOLDER, SMI_HELP, SMI_PRINTER, SMI_CONTROL, SMI_ABOUT,
    SMI_DOS, SMI_APP, SMI_START, SMI_EXPLORER, SMI_BIN, SMI_TASKMAN,
    SMI_FOLDER, SMI_COMPUTER, SMI_SETTINGS
};

/* -------------------------------------------------------------------- */
/* Default menu                                                         */
/* -------------------------------------------------------------------- */

static void WriteItem(LPCSTR lpSection, LPCSTR lpKey, LPCSTR lpCmd, LPCSTR lpDir,
                      int nShow, LPCSTR lpIcon, int nIconIndex)
{
    char szValue[300];

    wsprintf(szValue, "%s;%s;%d;%s;%d", lpCmd, lpDir, nShow, lpIcon, nIconIndex);
    WritePrivateProfileString(lpSection, lpKey, szValue, START_INI);
}

static void CreateDefaultMenu(void)
{
    char szFile[MAXPATHLEN], szGroup[40], szSection[96], szKey[48];
    LPPMITEM lpItems;
    int g, i, n;

    lpItems = (LPPMITEM)malloc(SM_GROUPMAX * sizeof(PMITEM));

    WriteItem("Start", "&Programs*", "", "", 0, "<programs>", 0);
    WriteItem("Start", "&Settings*", "", "", 0, "<settings>", 0);
    WriteItem("Start", "&Find...", "$Find", "", 0, "<find>", 0);
    WriteItem("Start", "&Run...", "$Run", "", 0, "<run>", 0);
    WriteItem("Start", "-1", "", "", 0, "", 0);
    WriteItem("Start", "Sh&ut Down...", "$ShutDown", "", 0, "<shutdown>", 0);

    if (lpItems) {
        for (g = 0; g < PmGroupCount(); g++) {
            if (!PmGroupFile(g, szFile, sizeof(szFile)))
                continue;
            n = PmLoadGroup(szFile, szGroup, sizeof(szGroup), lpItems, SM_GROUPMAX);
            if (n < 0)
                continue;
            wsprintf(szKey, "%s*", (LPSTR)szGroup);
            WriteItem("Start\\&Programs", szKey, "", "", 0, "", 0);
            wsprintf(szSection, "Start\\&Programs\\%s", (LPSTR)szGroup);
            for (i = 0; i < n; i++)
                WriteItem(szSection, lpItems[i].szName, lpItems[i].szCmd,
                          lpItems[i].szDir, lpItems[i].bMinimized ? 1 : 0,
                          lpItems[i].szIconPath, lpItems[i].nIconIndex);
        }
        free(lpItems);
    }
    WriteItem("Start\\&Programs", "Explorer", "$Explore", "", 0, "<explorer>", 0);
    WriteItem("Start\\&Programs", "Task Manager", "$TaskManager", "", 0, "<taskman>", 0);

    WriteItem("Start\\&Settings", "&Control Panel", "CONTROL.EXE", "", 0, "<control>", 0);
    WriteItem("Start\\&Settings", "&Printers", "PRINTMAN.EXE", "", 0, "<printer>", 0);
    WriteItem("Start\\&Settings", "&Taskbar...", "$TaskbarProp", "", 0, "<taskbar>", 0);
    WriteItem("Start\\&Settings", "&Windows Setup", "SETUP.EXE", "", 0, "", 0);
    WritePrivateProfileString(NULL, NULL, NULL, START_INI);    /* flush */
}

/* -------------------------------------------------------------------- */
/* Building                                                             */
/* -------------------------------------------------------------------- */

static int ImageByName(LPCSTR lpName)
{
    int i;

    for (i = 0; i < sizeof(s_nImageMap); i++)
        if (lstrcmpi(lpName, s_szImageNames[i]) == 0)
            return s_nImageMap[i];
    return -1;
}

/* Splits "a;b;n;c;m" into the item's fields */
static void ParseValue(LPSMITEM p, LPSTR lpValue)
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
    p->nImage = (signed char)(p->szIcon[0] == '<' ? ImageByName(p->szIcon) : -1);
}

static LPSMITEM NewItem(HMENU hMenu, BOOL bTop)
{
    LPSMITEM p;

    if (s_nItems >= SM_MAXITEMS)
        return NULL;
    p = &s_lpItems[s_nItems];
    _fmemset(p, 0, sizeof(SMITEM));
    p->hMenu = hMenu;
    p->nPos = GetMenuItemCount(hMenu);
    p->nImage = -1;
    if (bTop)
        p->bFlags |= SF_TOP;
    s_nItems++;             /* reserved now: submenus are built recursively */
    return p;
}

static void AppendItem(HMENU hMenu, LPSMITEM p, HMENU hSub)
{
    int n = (int)(p - s_lpItems);

    if (p->bFlags & (SF_SEP | SF_EMPTY))
        AppendMenu(hMenu, MF_OWNERDRAW | MF_DISABLED, CMD_SM_SEPARATOR + n,
                   (LPCSTR)(DWORD)n);
    else if (hSub)
        AppendMenu(hMenu, MF_OWNERDRAW | MF_POPUP, (UINT)hSub, (LPCSTR)(DWORD)n);
    else
        AppendMenu(hMenu, MF_OWNERDRAW, CMD_SM_FIRST + n, (LPCSTR)(DWORD)n);
}

static HMENU BuildMenu(LPCSTR lpSection, BOOL bTop)
{
    HMENU hMenu = CreatePopupMenu();
    char szValue[300], szSub[128];
    char *pKeys, *pKey;
    LPSMITEM p;
    HMENU hSub;
    int n;

    pKeys = (char *)malloc(SM_MAXKEYS);
    if (!pKeys)
        return hMenu;
    GetPrivateProfileString(lpSection, NULL, "", pKeys, SM_MAXKEYS, START_INI);
    for (pKey = pKeys; *pKey; pKey += lstrlen(pKey) + 1) {
        p = NewItem(hMenu, bTop);
        if (!p)
            break;
        if (pKey[0] == '-') {
            p->bFlags |= SF_SEP;
            AppendItem(hMenu, p, NULL);
            continue;
        }
        GetPrivateProfileString(lpSection, pKey, "", szValue, sizeof(szValue), START_INI);
        ParseValue(p, szValue);
        lstrcpyn(p->szText, pKey, sizeof(p->szText));
        n = lstrlen(pKey);
        hSub = NULL;
        if (n > 1 && pKey[n - 1] == '*') {
            if (n - 1 < sizeof(p->szText))
                p->szText[n - 1] = '\0';
            p->bFlags |= SF_SUBMENU;
            if (p->nImage < 0 && p->szIcon[0] == '\0')
                p->nImage = SMI_FOLDER;
            if (lstrlen(lpSection) + n + 2 < sizeof(szSub)) {
                wsprintf(szSub, "%s\\%s", lpSection, (LPSTR)p->szText);
                hSub = BuildMenu(szSub, FALSE);
            } else
                hSub = CreatePopupMenu();
        }
        AppendItem(hMenu, p, hSub);
    }
    free(pKeys);

    if (GetMenuItemCount(hMenu) == 0 && (p = NewItem(hMenu, bTop)) != NULL) {
        lstrcpy(p->szText, "(Empty)");
        p->bFlags |= SF_EMPTY;
        AppendItem(hMenu, p, NULL);
    }
    return hMenu;
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
    int i, y = 0;

    for (i = 0; i < s_nItems; i++)
        if (s_lpItems[i].bFlags & SF_TOP) {
            s_lpItems[i].yTop = y;
            y += (s_lpItems[i].bFlags & SF_SEP) ? SEP_H : TOP_H;
        }
    s_cyMenu = y;
}

void StartMenuShow(HWND hwndOwner, int x, int y)
{
    HMENU hMenu;
    char szTest[8];

    if (!s_lpItems) {
        s_lpItems = (LPSMITEM)malloc(SM_MAXITEMS * sizeof(SMITEM));
        if (!s_lpItems)
            return;
    }
    if (GetPrivateProfileString("Start", NULL, "", szTest, sizeof(szTest),
                                START_INI) == 0)
        CreateDefaultMenu();

    s_nItems = 0;
    CreateBannerFont();
    hMenu = BuildMenu("Start", TRUE);
    LayoutTop();

    /* place the menu just above the taskbar (2 pixels of menu border) */
    TrackPopupMenu(hMenu, TPM_LEFTALIGN, x, y - s_cyMenu - 2, 0, hwndOwner, NULL);
    DestroyMenu(hMenu);
}

/* -------------------------------------------------------------------- */
/* Drawing                                                              */
/* -------------------------------------------------------------------- */

static LPSMITEM ItemFromData(DWORD dwData)
{
    int n = (int)dwData;
    return (s_lpItems && n >= 0 && n < s_nItems) ? &s_lpItems[n] : NULL;
}

static void LoadItemIcon(LPSMITEM p)
{
    char szFile[MAXPATHLEN];
    OFSTRUCT of;
    int i;

    p->bFlags |= SF_ICONDONE;
    if (p->nImage >= 0)
        return;
    if (p->szIcon[0]) {
        p->hIcon = IconFromFile(p->szIcon, p->bIconIndex);
        if (p->hIcon)
            return;
    }
    if (p->szCmd[0] == '$') {
        p->nImage = (_fstrnicmp(p->szCmd, "$Folder", 7) == 0
                     || _fstrnicmp(p->szCmd + 1, "LargeIconFolder", 15) == 0)
                    ? SMI_FOLDER : SMI_APP;
        return;
    }
    for (i = 0; p->szCmd[i] && p->szCmd[i] != ' ' && i < sizeof(szFile) - 1; i++)
        szFile[i] = p->szCmd[i];
    szFile[i] = '\0';
    if (szFile[0] == '\0') {
        p->nImage = SMI_APP;
        return;
    }
    if (PathIsDirectory(szFile)) {
        p->nImage = SMI_FOLDER;
        return;
    }
    if (OpenFile(szFile, &of, OF_EXIST) == HFILE_ERROR) {
        p->nImage = SMI_APP;
        return;
    }
    OemToAnsi(of.szPathName, szFile);
    p->hIcon = IconForFile(szFile, FALSE);
}

BOOL StartMenuMeasureItem(MEASUREITEMSTRUCT FAR *lpmis)
{
    LPSMITEM p;
    HDC hdc;
    HFONT hfOld;
    int cx;

    if (lpmis->CtlType != ODT_MENU || !(p = ItemFromData(lpmis->itemData)))
        return FALSE;
    hdc = GetDC(NULL);
    hfOld = SelectObject(hdc, g_hfontUI);
    cx = LOWORD(GetTextExtent(hdc, p->szText, lstrlen(p->szText)));
    SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);

    if (p->bFlags & SF_SEP)
        lpmis->itemHeight = SEP_H;
    else
        lpmis->itemHeight = (p->bFlags & SF_TOP) ? TOP_H : SUB_H;
    lpmis->itemWidth = ((p->bFlags & SF_TOP) ? BANNER_W : 0) + 6 + 16 + 8 + cx + 20;
    return TRUE;
}

static void DrawBanner(HDC hdc, const RECT FAR *lprcItem, LPSMITEM p)
{
    RECT rcBanner;

    SetRect(&rcBanner, lprcItem->left, lprcItem->top,
            lprcItem->left + BANNER_W, lprcItem->bottom);
    FillSolid(hdc, &rcBanner, GetSysColor(COLOR_BTNSHADOW));
    if (s_hfBanner) {
        HFONT hfOld = SelectObject(hdc, s_hfBanner);
        int nOldMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF clrOld = SetTextColor(hdc, GetSysColor(COLOR_BTNFACE));
        int yBase = lprcItem->top - p->yTop + s_cyMenu - 4;

        ExtTextOut(hdc, lprcItem->left + 2, yBase, ETO_CLIPPED, &rcBanner,
                   NS_APPNAME, lstrlen(NS_APPNAME), NULL);
        SetTextColor(hdc, clrOld);
        SetBkMode(hdc, nOldMode);
        SelectObject(hdc, hfOld);
    }
}

BOOL StartMenuDrawItem(DRAWITEMSTRUCT FAR *lpdis)
{
    HDC hdc = lpdis->hDC;
    RECT rc = lpdis->rcItem, rcText;
    LPSMITEM p;
    int cy;
    BOOL bSel;
    COLORREF clrBk;

    if (lpdis->CtlType != ODT_MENU || !(p = ItemFromData(lpdis->itemData)))
        return FALSE;
    bSel = (lpdis->itemState & ODS_SELECTED) && !(p->bFlags & (SF_SEP | SF_EMPTY));
    clrBk = GetSysColor(bSel ? COLOR_HIGHLIGHT : COLOR_MENU);

    if (p->bFlags & SF_TOP) {
        DrawBanner(hdc, &rc, p);
        rc.left += BANNER_W;
    }
    if (p->bFlags & SF_SEP) {
        FillSolid(hdc, &rc, GetSysColor(COLOR_MENU));
        cy = rc.top + (rc.bottom - rc.top) / 2 - 1;
        SetRect(&rcText, rc.left + 2, cy, rc.right - 2, cy + 2);
        Draw3DEdge(hdc, &rcText, TRUE, FALSE);
        return TRUE;
    }

    FillSolid(hdc, &rc, clrBk);
    if (!(p->bFlags & SF_ICONDONE))
        LoadItemIcon(p);
    cy = rc.top + (rc.bottom - rc.top - 16) / 2;
    if (p->nImage >= 0)
        DrawSmallImage(hdc, rc.left + 4, cy, p->nImage);
    else if (p->hIcon)
        DrawIconSmall(hdc, rc.left + 4, cy, p->hIcon, clrBk);

    {
        HFONT hfOld = SelectObject(hdc, g_hfontUI);
        int nOldMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF clrOld = SetTextColor(hdc,
            GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT
                             : (p->bFlags & SF_EMPTY) ? COLOR_GRAYTEXT : COLOR_MENUTEXT));

        SetRect(&rcText, rc.left + 4 + 16 + 8, rc.top, rc.right, rc.bottom);
        DrawText(hdc, p->szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
        || id >= CMD_SM_SEPARATOR + SM_MAXITEMS)
        return;
    if (GetKeyState(VK_DOWN) & 0x8000)
        PostMessage(hwnd, WM_KEYDOWN, VK_DOWN, 0L);
    else if (GetKeyState(VK_UP) & 0x8000)
        PostMessage(hwnd, WM_KEYDOWN, VK_UP, 0L);
}

/* Mnemonics for owner-drawn items: returns a WM_MENUCHAR result */
LRESULT StartMenuChar(UINT ch, HMENU hMenu)
{
    int i;
    LPCSTR lp;

    if (!s_lpItems)
        return 0;
    ch = (UINT)(DWORD)AnsiUpper((LPSTR)(DWORD)ch);
    for (i = 0; i < s_nItems; i++) {
        if (s_lpItems[i].hMenu != hMenu)
            continue;
        for (lp = s_lpItems[i].szText; *lp; lp++)
            if (lp[0] == '&' && lp[1] && lp[1] != '&') {
                if ((UINT)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)lp[1]) == ch)
                    return MAKELRESULT(s_lpItems[i].nPos, 2);   /* MNC_EXECUTE */
                break;
            }
    }
    return 0;
}

/* -------------------------------------------------------------------- */
/* Commands                                                             */
/* -------------------------------------------------------------------- */

static int ShowMode(BYTE bShow)
{
    switch (bShow) {
    case 1:  return SW_SHOWMINNOACTIVE;
    case 2:  return SW_SHOWMAXIMIZED;
    default: return SW_SHOWNORMAL;
    }
}

/* Runs a '$' macro from the Start menu or a desktop shortcut */
BOOL ExecMacro(HWND hwnd, LPCSTR lpCmd)
{
    char szName[24], szMsg[80];
    LPCSTR lpArg;
    int i;

    for (i = 0; lpCmd[i] && lpCmd[i] != ' ' && i < sizeof(szName) - 1; i++)
        szName[i] = lpCmd[i];
    szName[i] = '\0';
    lpArg = lpCmd + i;
    while (*lpArg == ' ')
        lpArg++;

    if (!lstrcmpi(szName, "$Run"))              RunDialog(hwnd);
    else if (!lstrcmpi(szName, "$ShutDown"))    DoShutdownDialog();
    else if (!lstrcmpi(szName, "$Find"))        FindOpen(lpArg);
    else if (!lstrcmpi(szName, "$Explore"))     ExplorerOpen(lpArg);
    else if (!lstrcmpi(szName, "$System"))      FolderOpen("");
    else if (!lstrcmpi(szName, "$Bin"))         BinOpen();
    else if (!lstrcmpi(szName, "$TaskManager")) TaskManOpen();
    else if (!lstrcmpi(szName, "$MinimizePrograms")) TasksMinimizeAll();
    else if (!lstrcmpi(szName, "$ArrangeIcons") || !lstrcmpi(szName, "$LineUpIcons"))
        DesktopArrange();
    else if (!lstrcmpi(szName, "$Folder") || !lstrcmpi(szName, "$LargeIconFolder")
             || !lstrcmpi(szName, "$SmallIconFolder") || !lstrcmpi(szName, "$ListFolder"))
        FolderOpen(lpArg);
    else if (!lstrcmpi(szName, "$TaskbarProp") || !lstrcmpi(szName, "$DesktopProp")
             || !lstrcmpi(szName, "$StartMenuProp") || !lstrcmpi(szName, "$BinProp")
             || !lstrcmpi(szName, "$SystemProp") || !lstrcmpi(szName, "$FileSystemProp"))
        OptionsDialog(hwnd);
    else if (!lstrcmpi(szName, "$Help"))
        WinHelp(hwnd, "WINHELP.HLP", HELP_CONTENTS, 0L);
    else {
        wsprintf(szMsg, "The command %s is not supported.", (LPSTR)szName);
        MessageBox(hwnd, szMsg, NS_APPNAME, MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }
    return TRUE;
}

BOOL StartMenuCommand(HWND hwnd, UINT id)
{
    if (id >= CMD_SM_FIRST && id < CMD_SM_FIRST + SM_MAXITEMS) {
        int k = id - CMD_SM_FIRST;
        LPSMITEM p;

        if (!s_lpItems || k >= s_nItems)
            return TRUE;
        p = &s_lpItems[k];
        if (p->szCmd[0] == '$')
            ExecMacro(hwnd, p->szCmd);
        else if (p->szCmd[0])
            LaunchCommand(hwnd, p->szCmd, p->szDir, ShowMode(p->bShow));
        return TRUE;
    }

    switch (id) {
    case CMD_RUN:           RunDialog(hwnd); break;
    case CMD_SHUTDOWN:      DoShutdownDialog(); break;
    case CMD_ABOUT:         AboutDialog(hwnd); break;
    case CMD_COMPUTER:      FolderOpen(""); break;
    case CMD_EXPLORER:      ExplorerOpen(NULL); break;
    case CMD_BIN:           BinOpen(); break;
    case CMD_FIND:          FindOpen(NULL); break;
    case CMD_TASKMAN:       TaskManOpen(); break;
    case CMD_OPTIONS:       OptionsDialog(hwnd); break;
    case CMD_DESKTOPPROPS:  LaunchCommand(hwnd, "CONTROL.EXE DESKTOP", NULL, SW_SHOWNORMAL); break;
    case CMD_CONTROLPANEL:  LaunchCommand(hwnd, "CONTROL.EXE", NULL, SW_SHOWNORMAL); break;
    case CMD_CLOSESHELL:    CloseShell(); break;
    default:
        return FALSE;
    }
    return TRUE;
}

void StartMenuFree(void)
{
    if (s_lpItems) {
        free(s_lpItems);
        s_lpItems = NULL;
    }
    if (s_hfBanner) {
        DeleteObject(s_hfBanner);
        s_hfBanner = NULL;
    }
}
