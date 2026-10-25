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
