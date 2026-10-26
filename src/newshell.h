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
 * newshell.h - common declarations for NewShell16
 *
 * NewShell16 is a re-implementation of the Calmira shell for Windows 3.1,
 * written in plain C against the Win16 API.
 */
#ifndef NEWSHELL_H
#define NEWSHELL_H

#define STRICT
#include <windows.h>
#include <shellapi.h>
#include "resource.h"

#define NS_VERSION      "0.2"
#define NS_APPNAME      "NewShell16"
#define NS_INIFILE      "NEWSHELL.INI"
#define NS_STARTINI     "NSSTART.INI"

/* Window procedures and callbacks must be exported in Win16 */
#define NSEXPORT        __export

/* Window class names */
#define CLS_MAIN        "NSMain"
#define CLS_TASKBAR     "NSTaskbar"
#define CLS_DESKICON    "NSDeskIcon"
#define CLS_FOLDER      "NSFolder"
#define CLS_FVIEW       "NSFView"
#define CLS_EXPLORER    "NSExplorer"
#define CLS_TASKMAN     "NSTaskMan"
#define CLS_BIN         "NSBin"
#define CLS_PROGRESS    "NSProgress"
#define CLS_TOOLBAR     "NSToolbar"
#define CLS_HINT        "NSHint"

/* Private messages */
#define WM_NS_OPENPATH  (WM_USER + 100)     /* wParam = global atom with path */
#define WM_NS_REFRESH   (WM_USER + 101)
#define WM_NS_FVNOTIFY  (WM_USER + 102)     /* folder view -> parent, wParam = FVN_* */
#define WM_NS_ENDRENAME (WM_USER + 103)
#define WM_NS_SETPROGRESS (WM_USER + 104)   /* wParam = pos, LOWORD(lParam) = max */

#define MAXPATHLEN      144

/* ---- main.c ---------------------------------------------------------- */
extern HINSTANCE g_hInst;
extern HWND      g_hwndMain;
extern HWND      g_hwndTaskbar;
extern BOOL      g_bIsShell;        /* running as SHELL= in SYSTEM.INI */
extern char      g_szWinDir[MAXPATHLEN];
extern HFONT     g_hfontUI;         /* MS Sans Serif 8 */
extern HFONT     g_hfontBold;       /* MS Sans Serif 8 bold */

void DoShutdownDialog(void);
void CloseShell(void);
void HookApplySettings(void);

/* ---- settings.c ------------------------------------------------------ */
typedef struct tagNSCONFIG {
    BOOL bClock;            /* show clock in taskbar */
    BOOL bHideMinApps;      /* move minimised application icons off screen */
    BOOL bDesktopIcons;     /* show desktop icons */
    BOOL bRunStartup;       /* run load=/run= and the StartUp group */
    BOOL bConfirmExit;      /* ask before shutting down */
    BOOL bFolderNewWindow;  /* open sub-folders in new windows */
    BOOL bShowHidden;       /* show hidden/system files in folders */
    BOOL bDeleteToBin;      /* Delete sends to the Bin */
    BOOL bConfirmDelete;    /* ask before deleting */
    BOOL bCtrlEscStart;     /* Ctrl+Esc opens the Start menu (else Task Manager) */
    BOOL bMaxClamp;         /* keep maximised windows above the taskbar */
    BOOL bDesktopMenu;      /* right-click menu on the desktop */
    BOOL bWinKey;           /* Windows key opens the Start menu */
    BOOL bFileMonitor;      /* refresh folders on file system changes */
    BOOL bSnapToGrid;       /* desktop icons snap to a grid */
    BOOL bSingleClick;      /* desktop icons open with a single click */
    BOOL bShortcutArrows;   /* arrows on shortcuts */
    BOOL bFolderToolbar;    /* folder windows: toolbar */
    BOOL bFolderStatusBar;  /* folder windows: status bar */
    BOOL bCapitalize;       /* folder windows: "Calc.exe" for CALC.EXE */
    BOOL bShadeSelected;    /* folder windows: shade selected icons */
    BOOL bFileHints;        /* folder windows: hints with file details */
    BOOL bAskDrop;          /* ask Copy/Move/Alias after a drag */
    BOOL bDragCopy;         /* drags copy unless Shift is held */
    BOOL bRightClose;       /* right click on minimise/maximise boxes closes */
} NSCONFIG;

extern NSCONFIG g_cfg;

void CfgLoad(void);
void CfgSave(void);
int  CfgGetInt(LPCSTR lpSection, LPCSTR lpKey, int nDefault);
void CfgSetInt(LPCSTR lpSection, LPCSTR lpKey, int nValue);
void CfgGetStr(LPCSTR lpSection, LPCSTR lpKey, LPCSTR lpDefault,
               LPSTR lpBuf, int cbBuf);
void CfgSetStr(LPCSTR lpSection, LPCSTR lpKey, LPCSTR lpValue);

/* ---- util.c ---------------------------------------------------------- */
void FillSolid(HDC hdc, const RECT FAR *lprc, COLORREF clr);
void DrawItemFocus(HDC hdc, const RECT FAR *lprc, BOOL bSel);
void DrawSmallImageDisabled(HDC hdc, int x, int y, int nIndex);
void Draw3DEdge(HDC hdc, const RECT FAR *lprc, BOOL bSunken, BOOL bThick);
#define CLIENT_EDGE     2
void MovePane(HWND hwndPane, int x, int y, int cx, int cy);
void DrawPaneEdge(HDC hdc, int x, int y, int cx, int cy);
void DrawSmallImage(HDC hdc, int x, int y, int nIndex);
void DrawIconSmall(HDC hdc, int x, int y, HICON hIcon, COLORREF clrBk);
void ShadeArea(HDC hdc, int x, int y, int cx, int cy, COLORREF clrBk);
HFONT CreateUIFont(int nPoints, BOOL bBold);
int  TextWidth(HDC hdc, LPCSTR lpsz);
void FitText(HDC hdc, LPCSTR lpSrc, LPSTR lpDst, int cbDst, int cxMax);
BOOL ThemeEngineActive(void);
HFONT GetMenuFont(void);
HFONT CreateIconTitleFont(void);

LPSTR PathFileName(LPSTR lpPath);
LPSTR PathExtension(LPSTR lpPath);
void PathAppend(LPSTR lpPath, LPCSTR lpName);
BOOL PathParent(LPSTR lpPath);
BOOL PathIsDirectory(LPCSTR lpPath);
void PathNormalize(LPSTR lpPath);

BOOL LaunchCommand(HWND hwnd, LPCSTR lpCmdLine, LPCSTR lpDir, int nShow);
BOOL OpenPath(HWND hwnd, LPCSTR lpPath);
void ShowLaunchError(HWND hwnd, UINT uErr, LPCSTR lpWhat);
BOOL IsShiftDown(void);
BOOL NetShareAvailable(void);
void NetShareDialog(HWND hwnd, LPCSTR lpPath, BOOL bShare);
BOOL HintRegister(void);
void HintShow(LPCSTR lpText, int x, int y);
void HintHide(void);
BOOL HintVisible(void);
BOOL WildMatch(LPCSTR lpPattern, LPCSTR lpName);
BOOL MatchSpecList(LPCSTR lpName, LPCSTR lpList);

/* ---- icons.c --------------------------------------------------------- */
void  IconsInit(void);
void  IconsFree(void);
HICON IconForFile(LPCSTR lpPath, BOOL bDir);
HICON IconFromFile(LPCSTR lpFile, int nIndex);
HICON IconStock(int idi);
void  IconsForget(void);
BOOL  IsProgramFile(LPCSTR lpPath);

/* ---- progman.c -------------------------------------------------------- */
typedef struct tagPMITEM {
    char szName[40];
    char szCmd[128];
    char szDir[80];
    char szIconPath[80];
    int  nIconIndex;
    BOOL bMinimized;
} PMITEM, FAR *LPPMITEM;

int  PmGroupCount(void);
BOOL PmGroupFile(int nIndex, LPSTR lpFile, int cbFile);
int  PmLoadGroup(LPCSTR lpFile, LPSTR lpGroupName, int cbGroupName,
                 LPPMITEM lpItems, int nMax);
void PmRunStartupGroup(void);
void RunWinIniLoadRun(void);

/* ---- taskbar.c ------------------------------------------------------- */
BOOL TaskbarCreate(void);
void TaskbarRefresh(void);
int  TaskbarHeight(void);
void TaskbarApplySettings(void);
void TasksMinimizeAll(void);
void TasksClearDesktop(void);
void ShellCommand(UINT id);
int  TasksGetList(HWND FAR *lpOut, int nMax);
LRESULT CALLBACK NSEXPORT TaskbarWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- startmnu.c ------------------------------------------------------ */
#define CMD_SM_SEPARATOR    0x7000  /* ids of owner-drawn separators */
#define SM_KEYBUF           4096    /* buffer for the keys of a section */
void StartMenuEnsure(void);
void StartMenuProgramsSection(LPSTR lpBuf, int cbBuf);
BOOL StartMenuRunGroup(LPCSTR lpGroup);
int  StartMenuImage(LPCSTR lpName);
void StartMenuOptionsChanged(void);
void StartMenuPropertiesDialog(HWND hwndOwner);
void StartMenuShow(HWND hwndOwner, int x, int y);
BOOL StartMenuCommand(HWND hwnd, UINT id);
void StartMenuSelect(HWND hwnd, UINT id, UINT uFlags);
LRESULT StartMenuChar(UINT ch, HMENU hMenu);
BOOL StartMenuMeasureItem(MEASUREITEMSTRUCT FAR *lpmis);
BOOL StartMenuDrawItem(DRAWITEMSTRUCT FAR *lpdis);
BOOL ExecMacro(HWND hwnd, LPCSTR lpCmd);
void StartMenuFree(void);

/* ---- dde.c ---------------------------------------------------------- */
BOOL DdeRegister(void);
BOOL DdeEnabled(void);
void DdeInitiate(HWND hwndClient, ATOM aApp, ATOM aTopic);

/* ---- shortcut.c ------------------------------------------------------ */
typedef struct tagSHORTCUT {
    char szCaption[40];
    char szCmd[128];
    char szDir[80];
    int  nShow;             /* 0 normal, 1 minimised, 2 maximised */
    char szIconFile[80];    /* "" = the icon of what it runs */
    int  nIconIndex;
    BOOL bNoCommand;        /* dialog: only the caption can be changed */
} SHORTCUT, FAR *LPSHORTCUT;
typedef const SHORTCUT FAR *LPCSHORTCUT;

BOOL  BrowseForFile(HWND hDlg, int idEdit, LPCSTR lpFilter, LPCSTR lpTitle);
BOOL  ChangeIconDialog(HWND hwndOwner, LPSTR lpFile, int cbFile, int FAR *pnIndex);
BOOL  ShortcutDialog(HWND hwndOwner, LPSHORTCUT lpsc, LPCSTR lpTitle);
HICON ShortcutIconOf(LPCSHORTCUT lpsc);
void  ShortcutParse(LPSTR lpValue, LPSHORTCUT lpsc);
void  ShortcutFormat(LPCSHORTCUT lpsc, LPSTR lpBuf);
BOOL  ShortcutRun(HWND hwnd, LPCSHORTCUT lpsc);

/* ---- desktop.c ------------------------------------------------------- */
void DesktopDefaultColor(void);
BOOL DesktopCreate(void);
void DesktopDestroy(void);
void DesktopArrange(void);
void DesktopBinChanged(void);
void DesktopMenu(int x, int y);
int  DesktopDropTarget(HWND hwnd, LPSTR lpDir);
void DesktopAddShortcuts(HGLOBAL hList, POINT pt);
void DesktopDropFiles(HDROP hDrop);
void DesktopLineUp(void);
void DesktopRepaint(void);
void DesktopCaptionsChanged(void);
void DrawShortcutArrow(HDC hdc, int x, int y);
void DrawShortcutArrowSmall(HDC hdc, int x, int y);
LRESULT CALLBACK NSEXPORT DeskIconWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- fview.c (folder view control) ------------------------------------ */
#define FV_LARGE        0
#define FV_SMALL        1
#define FV_DETAILS      2
#define FV_LIST         3

#define FS_NAME         0
#define FS_TYPE         1
#define FS_SIZE         2
#define FS_DATE         3

#define FK_FILE         0
#define FK_DIR          1
#define FK_DRIVE        2

/* WM_NS_FVNOTIFY codes */
#define FVN_SELCHANGE   1
#define FVN_OPENFOLDER  2       /* lParam = LPCSTR path */
#define FVN_GOUP        3
#define FVN_PATHCHANGED 4
#define FVN_GONE        5       /* the folder no longer exists */
#define FVN_TAB         6       /* Tab pressed: move the focus on */

/* drop targets */
#define DT_NONE         0
#define DT_FOLDER       1
#define DT_BIN          2
#define DT_DESKTOP      3       /* the wallpaper: make shortcuts */
#define DT_PROGRAM      4       /* a program: lpDir = its command line */

BOOL   FViewRegister(void);
HWND   FViewCreate(HWND hwndParent, LPCSTR lpPath);
LPCSTR FViewGetPath(HWND hwnd);
LPCSTR FViewGetFilter(HWND hwnd);
void   FViewSetPath(HWND hwnd, LPCSTR lpPath);
void   FViewRefresh(HWND hwnd);
void   FViewSetMode(HWND hwnd, int nMode);
void   FViewSetSort(HWND hwnd, int nSort);
int    FViewSelCount(HWND hwnd);
HGLOBAL FViewGetSelection(HWND hwnd);
void   FViewGetStatus(HWND hwnd, LPSTR lpBuf, int cbBuf);
BOOL   FViewCommand(HWND hwnd, UINT id);
void   FViewInitMenu(HWND hwnd, HMENU hMenu);
void   FViewRefreshPath(LPCSTR lpDir);
void   FViewApplyOptions(void);
int    DropTargetAt(POINT pt, LPSTR lpDir);
int    DriveIcon(int nDrive, BYTE FAR *lpbType);

/* ---- folder.c -------------------------------------------------------- */
BOOL FolderRegister(void);
HWND FolderOpen(LPCSTR lpPath);     /* "" opens the Computer window */
void FolderCloseAll(void);
void FolderBarsChanged(void);
int  FolderGetWindows(HWND FAR *lpOut, int nMax);
void PaintStatusBar(HWND hwnd, HDC hdc, int cyStatus, LPCSTR lpText, int xPane);
int  StatusBarHeight(void);
LRESULT CALLBACK NSEXPORT FolderWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- toolbar.c ------------------------------------------------------- */
#define TB_MAXBUTTONS   24

typedef struct tagTBBTN {
    UINT uCmd;              /* WM_COMMAND id; 0 = a gap */
    int  nImage;            /* SMI_* */
} TBBTN;

BOOL ToolbarRegister(void);
int  ToolbarHeight(void);
HWND ToolbarCreate(HWND hwndParent, const TBBTN FAR *lpButtons, int nButtons);
void ToolbarSetState(HWND hwnd, UINT uCmd, BOOL bChecked, BOOL bEnabled);
void ToolbarSyncView(HWND hwndToolbar, HWND hwndView);
extern const TBBTN g_aFolderButtons[];
extern const int g_nFolderButtons;

/* ---- explorer.c ------------------------------------------------------ */
BOOL ExplorerRegister(void);
HWND ExplorerOpen(LPCSTR lpPath);
HWND ExplorerOpenTree(LPCSTR lpPath);
void ExplorerRefreshPath(LPCSTR lpDir);
int  ExplorerDropTarget(HWND hwnd, POINT pt, LPSTR lpDir);
void ExplorerCloseAll(void);
void ExplorerRefreshDrives(void);
int  ExplorerGetWindows(HWND FAR *lpOut, int nMax);

/* ---- fileops.c ------------------------------------------------------- */
BOOL   PathListAdd(HGLOBAL FAR *phList, LPCSTR lpPath);
LPCSTR PathListFirst(HGLOBAL hList);
LPCSTR PathListNext(LPCSTR lp);
void   PathListUnlock(HGLOBAL hList);
int    PathListCount(HGLOBAL hList);
void   FormatBytes(DWORD dw, LPSTR lpBuf);

BOOL FileOpRegister(void);
BOOL FileOpCopyMove(HWND hwndOwner, HGLOBAL hList, LPCSTR lpDestDir, BOOL bMove);
BOOL FileOpDelete(HWND hwndOwner, HGLOBAL hList, BOOL bToBin);
BOOL FileOpMoveTo(HWND hwndOwner, LPCSTR lpFrom, LPCSTR lpTo);
BOOL FileOpRemove(HWND hwndOwner, LPCSTR lpPath);
BOOL FileOpRename(HWND hwndOwner, LPCSTR lpPath, LPCSTR lpNewName);
BOOL FileOpNewFolder(HWND hwndOwner, LPCSTR lpDir, LPSTR lpName);
void FileOpMeasure(LPCSTR lpPath, DWORD FAR *pdwSize, int FAR *pnFiles,
                   int FAR *pnDirs);

/* ---- alias.c --------------------------------------------------------- */
BOOL  AliasRead(LPCSTR lpPath, LPSHORTCUT lpsc);
BOOL  AliasCreate(LPCSTR lpTarget, LPCSTR lpAlias);
int   AliasCreateList(HWND hwnd, HGLOBAL hList, LPCSTR lpDir);
BOOL  IsAliasFile(LPCSTR lpName);
HICON AliasIcon(LPCSTR lpPath);
BOOL  AliasOpen(HWND hwnd, LPCSTR lpPath);

/* ---- folddlg.c ------------------------------------------------------- */
int  SelectFilesDialog(HWND hwndOwner, LPSTR lpSpec, int cbSpec);
BOOL FilterDialog(HWND hwndOwner, LPSTR lpSpec, int cbSpec, BOOL FAR *pbHidden);
BOOL GotoDialog(HWND hwndOwner, LPSTR lpPath, int cbPath);
BOOL OpenWithDialog(HWND hwndOwner, LPCSTR lpFile);
BOOL OpenWithDialogEx(HWND hwndOwner, LPCSTR lpFile, BOOL bAssocOnly);
BOOL DescribeDialog(HWND hwndOwner, LPCSTR lpName, LPSTR lpDesc, int cbDesc);
BOOL DescriptionSet(LPCSTR lpDir, LPCSTR lpName, LPCSTR lpDesc);
BOOL ColumnsDialog(HWND hwndOwner, UINT FAR *puHidden);

/* ---- bin.c ----------------------------------------------------------- */
BOOL BinRegister(void);
BOOL BinCanTake(LPCSTR lpPath);
BOOL BinSend(LPCSTR lpPath);
BOOL BinIsEmpty(void);
void BinChanged(void);
void BinOpen(void);
void BinClose(void);
void BinPropertiesDialog(HWND hwndOwner);
void BinExit(void);
BOOL BinUndelete(HWND hwndOwner);

/* ---- props.c --------------------------------------------------------- */
void PropertiesDialog(HWND hwndOwner, HGLOBAL hList);

/* ---- taskman.c ------------------------------------------------------- */
BOOL TaskManRegister(void);
void TaskManOpen(void);
void TaskManClose(void);
void TaskManTerminate(HWND hwndTask, HWND hwndOwner);
BOOL TaskManIsDialogMessage(MSG FAR *lpMsg);
void GetWorkArea(RECT FAR *lprc);
void CascadeList(HWND FAR *lpList, int n);
void TileList(HWND FAR *lpList, int n, BOOL bVertical);

/* ---- find.c ---------------------------------------------------------- */
void FindOpen(LPCSTR lpDir);
void FindClose(void);
BOOL FindIsDialogMessage(MSG FAR *lpMsg);

/* ---- dialogs.c ------------------------------------------------------- */
void RunDialog(HWND hwndOwner);
int  ShutdownDialog(HWND hwndOwner);    /* returns IDC_* choice or 0 */
void AboutDialog(HWND hwndOwner);
void OptionsDialog(HWND hwndOwner);

#endif /* NEWSHELL_H */
