/*
 * settings.c - NEWSHELL.INI access
 *
 * The INI file lives in the Windows directory (the default location for
 * private profile files without a path).
 */
#include "newshell.h"

NSCONFIG g_cfg;

int CfgGetInt(LPCSTR lpSection, LPCSTR lpKey, int nDefault)
{
    return (int)GetPrivateProfileInt(lpSection, lpKey, nDefault, NS_INIFILE);
}

void CfgSetInt(LPCSTR lpSection, LPCSTR lpKey, int nValue)
{
    char buf[16];

    wsprintf(buf, "%d", nValue);
    WritePrivateProfileString(lpSection, lpKey, buf, NS_INIFILE);
}

void CfgGetStr(LPCSTR lpSection, LPCSTR lpKey, LPCSTR lpDefault,
               LPSTR lpBuf, int cbBuf)
{
    GetPrivateProfileString(lpSection, lpKey, lpDefault, lpBuf, cbBuf,
                            NS_INIFILE);
}

void CfgSetStr(LPCSTR lpSection, LPCSTR lpKey, LPCSTR lpValue)
{
    WritePrivateProfileString(lpSection, lpKey, lpValue, NS_INIFILE);
}

void CfgLoad(void)
{
    g_cfg.bClock           = CfgGetInt("Taskbar", "Clock", 1);
    g_cfg.bHideMinApps     = CfgGetInt("Taskbar", "HideMinApps", 1);
    g_cfg.bDesktopIcons    = CfgGetInt("Desktop", "Icons", 1);
    g_cfg.bRunStartup      = CfgGetInt("Startup", "Run", 1);
    g_cfg.bConfirmExit     = CfgGetInt("Preferences", "ConfirmExit", 1);
    g_cfg.bFolderNewWindow = CfgGetInt("Folders", "NewWindow", 1);
    g_cfg.bShowHidden      = CfgGetInt("Folders", "ShowHidden", 0);
}
