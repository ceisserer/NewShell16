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
 * All items are owner-drawn, after Calmira's 3D Start menu: large icons
 * and a vertical banner on the top level, button-face colours, and without
 * a theme engine a raised frame painted over the Windows 3.1 menu border.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"

#define START_INI       NS_STARTINI
#define SM_MAXITEMS     240
#define SM_MAXKEYS      SM_KEYBUF
#define SM_GROUPMAX     64

#define TOP_MIN_H       34      /* item heights: at least this, or font + gap */
#define SUB_MIN_H       20
#define SEP_H           8
#define BANNER_W        24
#define TOP_ICON_X      8       /* offsets from the left of an item (top */
#define TOP_TEXT_X      48      /* level: from the right of the banner) */
#define SUB_ICON_X      6
#define SUB_TEXT_X      28

/* item flags */
#define SF_TOP          0x01
#define SF_SEP          0x02
#define SF_SUBMENU      0x04
#define SF_ICONDONE     0x08
#define SF_EMPTY        0x10

typedef struct tagSMITEM {
    char   szText[40];
    char   szIcon[80];
    BYTE   bShow;
    BYTE   bIconIndex;
    BYTE   bFlags;
    signed char nImage;         /* SMI_* or -1 */
    HICON  hIcon;
    HMENU  hMenu;               /* menu holding the item */
    int    nPos;                /* position in that menu */
    int    yTop;                /* top-level items: offset from menu top */
} SMITEM, FAR *LPSMITEM;

/* what an item runs, kept apart to stay within 64 KB per array */
typedef struct tagSMCMD {
    char   szCmd[128];
    char   szDir[80];
} SMCMD, FAR *LPSMCMD;

static LPSMITEM s_lpItems;
static LPSMCMD  s_lpCmds;
static int      s_nItems;
static int      s_cyMenu;
static int      s_cyTop, s_cySub;   /* item heights for the current font */
static HFONT    s_hfMenu;           /* GetMenuFont() when the menu opened */
static BOOL     s_bThemed;          /* the theme engine draws the menus */
static HFONT    s_hfBanner, s_hfBannerLight;
static int      s_cyBannerFont;

static const char *s_szImageNames[] = {
    "<programs>", "<documents>", "<settings>", "<find>", "<run>", "<shutdown>",
    "<system>", "<folder>", "<help>", "<printer>", "<control>", "<about>",
    "<dos>", "<application>", "<start>", "<explorer>", "<bin>", "<taskman>",
    "<favorites>", "<desktop>", "<taskbar>", "<internet>"
};
static const signed char s_nImageMap[] = {
    SMI_PROGRAMS, SMI_DOCUMENT, SMI_SETTINGS, SMI_FIND, SMI_RUN, SMI_SHUTDOWN,
    SMI_COMPUTER, SMI_FOLDER, SMI_HELP, SMI_PRINTER, SMI_CONTROL, SMI_ABOUT,
    SMI_DOS, SMI_APP, SMI_START, SMI_EXPLORER, SMI_BIN, SMI_TASKMAN,
    SMI_FAVORITES, SMI_COMPUTER, SMI_SETTINGS, SMI_INTERNET
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
    WriteItem("Start\\&Programs", "File System", "$Explore", "", 0, "<explorer>", 0);
    WriteItem("Start\\&Programs", "Task Manager", "$TaskManager", "", 0, "<taskman>", 0);

    WriteItem("Start\\&Settings", "&Control Panel", "CONTROL.EXE", "", 0, "<control>", 0);
    WriteItem("Start\\&Settings", "&Printers", "PRINTMAN.EXE", "", 0, "<printer>", 0);
    WriteItem("Start\\&Settings", "&Taskbar...", "$TaskbarProp", "", 0, "<taskbar>", 0);
    WriteItem("Start\\&Settings", "&Windows Setup", "SETUP.EXE", "", 0, "", 0);
    WritePrivateProfileString(NULL, NULL, NULL, START_INI);    /* flush */
}

/* Creates NSSTART.INI from the Program Manager groups if it is missing */
void StartMenuEnsure(void)
{
    char szTest[8];

    if (GetPrivateProfileString("Start", NULL, "", szTest, sizeof(szTest),
                                START_INI) == 0)
        CreateDefaultMenu();
}

/*
 * The section of the Programs submenu, where Program Manager groups go:
 * "Start\" and the first top-level submenu named Programs (ignoring '&'),
 * or [Start Menu] ProgramsGroup= in NEWSHELL.INI.
 */
void StartMenuProgramsSection(LPSTR lpBuf, int cbBuf)
{
    char szName[48], szPlain[48];
    char *pKeys, *p;
    int i, n;

    CfgGetStr("Start Menu", "ProgramsGroup", "", szName, sizeof(szName));
    if (!szName[0]) {
        lstrcpy(szName, "&Programs");
        pKeys = (char *)malloc(SM_MAXKEYS);
        if (pKeys) {
            GetPrivateProfileString("Start", NULL, "", pKeys, SM_MAXKEYS, START_INI);
            for (p = pKeys; *p; p += lstrlen(p) + 1) {
                for (i = n = 0; p[i] && n < sizeof(szPlain) - 1; i++)
                    if (p[i] != '&')
                        szPlain[n++] = p[i];
                szPlain[n] = '\0';
                if (lstrcmpi(szPlain, "Programs*") == 0) {
                    lstrcpyn(szName, p, sizeof(szName));
                    szName[lstrlen(szName) - 1] = '\0';
                    break;
                }
            }
            free(pKeys);
        }
    }
    if (lstrlen(szName) + 7 > cbBuf)
        szName[cbBuf - 7] = '\0';
    wsprintf(lpBuf, "Start\\%s", (LPSTR)szName);
}

/*
 * Runs the items of group lpGroup of the Programs menu (the StartUp
 * group). Returns FALSE if NSSTART.INI has no such group.
 */
BOOL StartMenuRunGroup(LPCSTR lpGroup)
{
    char szSection[128], szValue[300], szProbe[8];
    char *pKeys, *p;
    LPSTR f[5], q;
    int i, n;

    if (GetPrivateProfileString("Start", NULL, "", szProbe, sizeof(szProbe),
                                START_INI) == 0)
        return FALSE;
    StartMenuProgramsSection(szSection, sizeof(szSection) - 48);
    lstrcat(szSection, "\\");
    lstrcat(szSection, lpGroup);
    pKeys = (char *)malloc(SM_MAXKEYS);
    if (!pKeys)
        return FALSE;
    if (GetPrivateProfileString(szSection, NULL, "", pKeys, SM_MAXKEYS, START_INI) == 0) {
        free(pKeys);
        return FALSE;
    }
    for (p = pKeys; *p; p += lstrlen(p) + 1) {
        if (*p == '-')
            continue;
        GetPrivateProfileString(szSection, p, "", szValue, sizeof(szValue), START_INI);
        f[0] = szValue;
        for (i = 1; i < 5; i++)
            f[i] = "";
        for (q = szValue, n = 0; *q && n < 4; q++)
            if (*q == ';') {
                *q = '\0';
                f[++n] = q + 1;
            }
        if (f[0][0] == '$')
            ExecMacro(g_hwndMain, f[0]);
        else if (f[0][0])
            LaunchCommand(NULL, f[0], f[1], f[2][0] == '1' ? SW_SHOWMINNOACTIVE
                                                         : SW_SHOWNORMAL);
    }
    free(pKeys);
    return TRUE;
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
    LPSMCMD c = &s_lpCmds[p - s_lpItems];
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
    lstrcpyn(c->szCmd, f[0], sizeof(c->szCmd));
    lstrcpyn(c->szDir, f[1], sizeof(c->szDir));
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
    _fmemset(&s_lpCmds[s_nItems], 0, sizeof(SMCMD));
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

/* The banner reads "NewShell16" upwards, with a light "16" */
static void CreateBannerFont(void)
{
    LOGFONT lf;

    if (s_hfBanner)
        return;
    _fmemset(&lf, 0, sizeof(lf));
    lf.lfHeight = -20;
    lf.lfWeight = FW_BOLD;
    lf.lfEscapement = 900;
    lf.lfOrientation = 900;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, "Arial");
    s_hfBanner = CreateFontIndirect(&lf);
    lf.lfWeight = FW_NORMAL;
    s_hfBannerLight = CreateFontIndirect(&lf);
    if (s_hfBanner) {
        TEXTMETRIC tm;
        HDC hdc = GetDC(NULL);
        HFONT hfOld = SelectObject(hdc, s_hfBanner);

        GetTextMetrics(hdc, &tm);
        s_cyBannerFont = tm.tmHeight - tm.tmInternalLeading;
        SelectObject(hdc, hfOld);
        ReleaseDC(NULL, hdc);
    }
}

static void LayoutTop(void)
{
    int i, y = 0;

    for (i = 0; i < s_nItems; i++)
        if (s_lpItems[i].bFlags & SF_TOP) {
            s_lpItems[i].yTop = y;
            y += (s_lpItems[i].bFlags & SF_SEP) ? SEP_H : s_cyTop;
        }
    s_cyMenu = y;
}

void StartMenuShow(HWND hwndOwner, int x, int y)
{
    HMENU hMenu;

    if (!s_lpItems) {
        s_lpItems = (LPSMITEM)malloc(SM_MAXITEMS * sizeof(SMITEM));
        s_lpCmds = (LPSMCMD)malloc(SM_MAXITEMS * sizeof(SMCMD));
        if (!s_lpItems || !s_lpCmds) {
            StartMenuFree();
            return;
        }
    }
    StartMenuEnsure();

    s_nItems = 0;
    CreateBannerFont();
    s_hfMenu = GetMenuFont();
    s_bThemed = ThemeEngineActive();
    {
        TEXTMETRIC tm;
        HDC hdc = GetDC(NULL);
        HFONT hfOld = SelectObject(hdc, s_hfMenu);

        GetTextMetrics(hdc, &tm);
        SelectObject(hdc, hfOld);
        ReleaseDC(NULL, hdc);
        s_cyTop = tm.tmHeight + 10 > TOP_MIN_H ? tm.tmHeight + 10 : TOP_MIN_H;
        s_cySub = tm.tmHeight + 4 > SUB_MIN_H ? tm.tmHeight + 4 : SUB_MIN_H;
    }
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
    LPSMCMD c = &s_lpCmds[p - s_lpItems];
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
    if (c->szCmd[0] == '$') {
        p->nImage = (_fstrnicmp(c->szCmd, "$Folder", 7) == 0
                     || _fstrnicmp(c->szCmd + 1, "LargeIconFolder", 15) == 0)
                    ? SMI_FOLDER : SMI_APP;
        return;
    }
    for (i = 0; c->szCmd[i] && c->szCmd[i] != ' ' && i < sizeof(szFile) - 1; i++)
        szFile[i] = c->szCmd[i];
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
    hfOld = SelectObject(hdc, s_hfMenu);
    cx = LOWORD(GetTextExtent(hdc, p->szText, lstrlen(p->szText)));
    SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);

    if (p->bFlags & SF_SEP)
        lpmis->itemHeight = SEP_H;
    else
        lpmis->itemHeight = (p->bFlags & SF_TOP) ? s_cyTop : s_cySub;
    lpmis->itemWidth = ((p->bFlags & SF_TOP) ? BANNER_W + TOP_TEXT_X : SUB_TEXT_X) + cx + 20;
    return TRUE;
}

/* The 32x32 icon for a built-in image, or 0 */
static int LargeIcon(int nImage)
{
    switch (nImage) {
    case SMI_PROGRAMS:  return IDI_SM_PROGRAMS;
    case SMI_DOCUMENT:  return IDI_SM_DOCUMENTS;
    case SMI_SETTINGS:  return IDI_SM_SETTINGS;
    case SMI_FIND:      return IDI_SM_FIND;
    case SMI_RUN:       return IDI_SM_RUN;
    case SMI_SHUTDOWN:  return IDI_SM_SHUTDOWN;
    case SMI_HELP:      return IDI_SM_HELP;
    case SMI_FAVORITES: return IDI_SM_FAVORITES;
    case SMI_INTERNET:  return IDI_SM_INTERNET;
    case SMI_COMPUTER:  return IDI_COMPUTER;
    case SMI_FOLDER:    return IDI_FOLDER;
    case SMI_EXPLORER:  return IDI_EXPLORER;
    case SMI_BIN:       return IDI_BIN;
    case SMI_TASKMAN:   return IDI_TASKMAN;
    case SMI_APP:       return IDI_APP;
    }
    return 0;
}

/*
 * The banner: a gradient from black at the top to the active caption colour
 * at the bottom (dithered by GDI on 16-colour displays), in bands of 4
 * pixels counted from the top of the menu, so the items' parts join up.
 */
static void DrawBanner(HDC hdc, const RECT FAR *lprcItem, LPSMITEM p)
{
    COLORREF clr = GetSysColor(COLOR_ACTIVECAPTION);
    RECT rc;
    int y, yMenu, yEnd;

    yMenu = lprcItem->top - p->yTop;    /* the top of the menu in hdc */
    for (y = p->yTop & ~3; y < p->yTop + (lprcItem->bottom - lprcItem->top); y += 4) {
        long f = s_cyMenu > 0 ? (long)y * 256 / s_cyMenu : 256;
        HBRUSH hbr = CreateSolidBrush(RGB(GetRValue(clr) * f / 256,
                                          GetGValue(clr) * f / 256,
                                          GetBValue(clr) * f / 256));

        yEnd = yMenu + y + 4;
        SetRect(&rc, lprcItem->left, max(yMenu + y, lprcItem->top),
                lprcItem->left + BANNER_W, min(yEnd, lprcItem->bottom));
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);
    }
    if (s_hfBanner && s_hfBannerLight) {
        static const char szBold[] = "NewShell", szLight[] = "16";
        HFONT hfOld = SelectObject(hdc, s_hfBanner);
        int nOldMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF clrOld = SetTextColor(hdc, GetSysColor(COLOR_CAPTIONTEXT));
        int x = lprcItem->left + (BANNER_W - s_cyBannerFont) / 2 - 1;
        int yBase = yMenu + s_cyMenu - 6;

        SetRect(&rc, lprcItem->left, lprcItem->top, lprcItem->left + BANNER_W,
                lprcItem->bottom);
        ExtTextOut(hdc, x, yBase, ETO_CLIPPED, &rc, szBold, sizeof(szBold) - 1, NULL);
        yBase -= LOWORD(GetTextExtent(hdc, szBold, sizeof(szBold) - 1));
        SelectObject(hdc, s_hfBannerLight);
        ExtTextOut(hdc, x, yBase, ETO_CLIPPED, &rc, szLight, sizeof(szLight) - 1, NULL);
        SetTextColor(hdc, clrOld);
        SetBkMode(hdc, nOldMode);
        SelectObject(hdc, hfOld);
    }
}

static BOOL IsLastItem(LPSMITEM p)
{
    return p->nPos == GetMenuItemCount(p->hMenu) - 1;
}

/*
 * Paints one item into hdc (rc in hdc coordinates). Without a theme engine
 * the inner half of a raised frame is part of the items: a highlight on the
 * left (the banner on the top level) and top, a shadow on the right and
 * bottom. DrawOuterFrame adds the outer half.
 */
static void PaintItem(HDC hdc, RECT rc, LPSMITEM p, BOOL bSel)
{
    BOOL bTop = (p->bFlags & SF_TOP) != 0;
    COLORREF clrFace = GetSysColor(COLOR_BTNFACE);
    RECT rcIn, rcText;
    int cy;

    FillSolid(hdc, &rc, clrFace);
    if (bTop) {
        DrawBanner(hdc, &rc, p);
        rc.left += BANNER_W;
    }
    rcIn = rc;
    if (!s_bThemed) {
        RECT rcLine;

        if (!bTop) {
            SetRect(&rcLine, rc.left, rc.top, rc.left + 1, rc.bottom);
            FillSolid(hdc, &rcLine, GetSysColor(COLOR_BTNHIGHLIGHT));
            rcIn.left++;
        }
        if (p->nPos == 0) {
            SetRect(&rcLine, rc.left - (bTop ? BANNER_W : 0), rc.top, rc.right, rc.top + 1);
            FillSolid(hdc, &rcLine, GetSysColor(COLOR_BTNHIGHLIGHT));
            rcIn.top++;
        }
        SetRect(&rcLine, rc.right - 1, rc.top, rc.right, rc.bottom);
        FillSolid(hdc, &rcLine, GetSysColor(COLOR_BTNSHADOW));
        rcIn.right--;
        if (IsLastItem(p)) {
            SetRect(&rcLine, rc.left - (bTop ? BANNER_W : 0), rc.bottom - 1, rc.right, rc.bottom);
            FillSolid(hdc, &rcLine, GetSysColor(COLOR_BTNSHADOW));
            rcIn.bottom--;
        }
    }

    if (p->bFlags & SF_SEP) {
        cy = rc.top + (rc.bottom - rc.top) / 2 - 1;
        SetRect(&rcText, rcIn.left + 2, cy, rcIn.right - 2, cy + 2);
        Draw3DEdge(hdc, &rcText, TRUE, FALSE);
        return;
    }

    if (bSel)
        FillSolid(hdc, &rcIn, GetSysColor(COLOR_HIGHLIGHT));
    if (!(p->bFlags & SF_ICONDONE))
        LoadItemIcon(p);
    if (bTop) {
        int x = rc.left + TOP_ICON_X, idi = LargeIcon(p->nImage);

        cy = rc.top + (rc.bottom - rc.top - 32) / 2;
        if (p->hIcon)
            DrawIcon(hdc, x, cy, p->hIcon);
        else if (idi)
            DrawIcon(hdc, x, cy, IconStock(idi));
        else if (p->nImage >= 0)
            DrawSmallImage(hdc, x + 8, cy + 8, p->nImage);
    } else {
        cy = rc.top + (rc.bottom - rc.top - 16) / 2;
        if (p->nImage >= 0)
            DrawSmallImage(hdc, rc.left + SUB_ICON_X, cy, p->nImage);
        else if (p->hIcon)
            DrawIconSmall(hdc, rc.left + SUB_ICON_X, cy, p->hIcon,
                          bSel ? GetSysColor(COLOR_HIGHLIGHT) : clrFace);
    }

    {
        HFONT hfOld = SelectObject(hdc, s_hfMenu);
        int nOldMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF clrOld = SetTextColor(hdc,
            GetSysColor(bSel ? COLOR_HIGHLIGHTTEXT
                             : (p->bFlags & SF_EMPTY) ? COLOR_GRAYTEXT : COLOR_BTNTEXT));

        SetRect(&rcText, rc.left + (bTop ? TOP_TEXT_X : SUB_TEXT_X), rc.top,
                rc.right, rc.bottom);
        DrawText(hdc, p->szText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, clrOld);
        SetBkMode(hdc, nOldMode);
        SelectObject(hdc, hfOld);
    }
}

/*
 * The outer half of the raised frame, painted over the black border Windows
 * 3.1 draws around menus: the button face on the left and at the top (the
 * black remains on the right and at the bottom, as in Windows 95 menus).
 */
static void DrawOuterFrame(HDC hdc, const RECT FAR *lprc, LPSMITEM p)
{
    RECT rc;

    SetRect(&rc, lprc->left - 1, lprc->top, lprc->left, lprc->bottom);
    if (p->nPos == 0)
        rc.top--;
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    if (p->nPos == 0) {
        SetRect(&rc, lprc->left - 1, lprc->top - 1, lprc->right, lprc->top);
        FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    }
}

/*
 * Items are painted into a memory bitmap and copied to the menu in one
 * blit: this avoids flicker, and keeps a theme engine that redraws the
 * text and fills of system menus from reinterpreting our drawing.
 */
BOOL StartMenuDrawItem(DRAWITEMSTRUCT FAR *lpdis)
{
    RECT rc = lpdis->rcItem;
    LPSMITEM p;
    BOOL bSel;
    HDC hdcMem;
    HBITMAP hbm, hbmOld;
    int cx = rc.right - rc.left, cy = rc.bottom - rc.top;

    if (lpdis->CtlType != ODT_MENU || !(p = ItemFromData(lpdis->itemData)))
        return FALSE;
    bSel = (lpdis->itemState & ODS_SELECTED) && !(p->bFlags & (SF_SEP | SF_EMPTY));

    hdcMem = CreateCompatibleDC(lpdis->hDC);
    hbm = CreateCompatibleBitmap(lpdis->hDC, cx, cy);
    if (!hdcMem || !hbm) {
        if (hdcMem)
            DeleteDC(hdcMem);
        PaintItem(lpdis->hDC, rc, p, bSel);
        if (!s_bThemed)
            DrawOuterFrame(lpdis->hDC, &rc, p);
        return TRUE;
    }
    hbmOld = SelectObject(hdcMem, hbm);
    SetWindowOrg(hdcMem, rc.left, rc.top);
    PaintItem(hdcMem, rc, p, bSel);
    SetWindowOrg(hdcMem, 0, 0);
    BitBlt(lpdis->hDC, rc.left, rc.top, cx, cy, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    if (!s_bThemed)
        DrawOuterFrame(lpdis->hDC, &rc, p);
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
        LPSMCMD c;

        if (!s_lpItems || k >= s_nItems)
            return TRUE;
        c = &s_lpCmds[k];
        if (c->szCmd[0] == '$')
            ExecMacro(hwnd, c->szCmd);
        else if (c->szCmd[0])
            LaunchCommand(hwnd, c->szCmd, c->szDir, ShowMode(s_lpItems[k].bShow));
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
    if (s_lpCmds) {
        free(s_lpCmds);
        s_lpCmds = NULL;
    }
    if (s_hfBanner) {
        DeleteObject(s_hfBanner);
        s_hfBanner = NULL;
    }
    if (s_hfBannerLight) {
        DeleteObject(s_hfBannerLight);
        s_hfBannerLight = NULL;
    }
}
