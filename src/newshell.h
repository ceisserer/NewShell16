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

#define NS_VERSION      "0.1"
#define NS_APPNAME      "NewShell"
#define NS_INIFILE      "NEWSHELL.INI"

/* Window procedures and callbacks must be exported in Win16 */
#define NSEXPORT        __export

/* Window class names */
#define CLS_MAIN        "NSMain"
#define CLS_TASKBAR     "NSTaskbar"
#define CLS_DESKICON    "NSDeskIcon"
#define CLS_FOLDER      "NSFolder"

/* Private messages */
#define WM_NS_OPENPATH  (WM_USER + 100)     /* wParam = global atom with path */
#define WM_NS_REFRESH   (WM_USER + 101)

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

/* ---- settings.c ------------------------------------------------------ */
typedef struct tagNSCONFIG {
    BOOL bClock;            /* show clock in taskbar */
    BOOL bHideMinApps;      /* move minimised application icons off screen */
    BOOL bDesktopIcons;     /* show desktop icons */
    BOOL bRunStartup;       /* run load=/run= and the StartUp group */
    BOOL bConfirmExit;      /* ask before shutting down */
    BOOL bFolderNewWindow;  /* open sub-folders in new windows */
    BOOL bShowHidden;       /* show hidden/system files in folders */
} NSCONFIG;

extern NSCONFIG g_cfg;

void CfgLoad(void);
int  CfgGetInt(LPCSTR lpSection, LPCSTR lpKey, int nDefault);
void CfgSetInt(LPCSTR lpSection, LPCSTR lpKey, int nValue);
void CfgGetStr(LPCSTR lpSection, LPCSTR lpKey, LPCSTR lpDefault,
               LPSTR lpBuf, int cbBuf);
void CfgSetStr(LPCSTR lpSection, LPCSTR lpKey, LPCSTR lpValue);

/* ---- util.c ---------------------------------------------------------- */
void FillSolid(HDC hdc, const RECT FAR *lprc, COLORREF clr);
void Draw3DEdge(HDC hdc, const RECT FAR *lprc, BOOL bSunken, BOOL bThick);
void DrawSmallImage(HDC hdc, int x, int y, int nIndex);
void DrawIconSmall(HDC hdc, int x, int y, HICON hIcon, COLORREF clrBk);
HFONT CreateUIFont(int nPoints, BOOL bBold);
int  TextWidth(HDC hdc, LPCSTR lpsz);
void FitText(HDC hdc, LPCSTR lpSrc, LPSTR lpDst, int cbDst, int cxMax);

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

/* ---- icons.c --------------------------------------------------------- */
void  IconsInit(void);
void  IconsFree(void);
HICON IconForFile(LPCSTR lpPath, BOOL bDir);
HICON IconStock(int idi);

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
LRESULT CALLBACK NSEXPORT TaskbarWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- startmnu.c ------------------------------------------------------ */
void StartMenuShow(HWND hwndOwner, int x, int y);
BOOL StartMenuCommand(HWND hwnd, UINT id);
void StartMenuSelect(HWND hwnd, UINT id, UINT uFlags);
BOOL StartMenuMeasureItem(MEASUREITEMSTRUCT FAR *lpmis);
BOOL StartMenuDrawItem(DRAWITEMSTRUCT FAR *lpdis);
void StartMenuFree(void);

/* ---- desktop.c ------------------------------------------------------- */
BOOL DesktopCreate(void);
void DesktopDestroy(void);
LRESULT CALLBACK NSEXPORT DeskIconWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- folder.c -------------------------------------------------------- */
BOOL FolderRegister(void);
HWND FolderOpen(LPCSTR lpPath);     /* "" opens the Computer window */
void FolderCloseAll(void);
LRESULT CALLBACK NSEXPORT FolderWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- dialogs.c ------------------------------------------------------- */
void RunDialog(HWND hwndOwner);
int  ShutdownDialog(HWND hwndOwner);    /* returns IDC_* choice or 0 */
void AboutDialog(HWND hwndOwner);

#endif /* NEWSHELL_H */
