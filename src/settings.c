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

typedef struct tagCFGENTRY {
    LPCSTR lpSection;
    LPCSTR lpKey;
    BOOL  *pValue;
    BOOL   bDefault;
} CFGENTRY;

static const CFGENTRY s_entries[] = {
    { "Taskbar",     "Clock",         &g_cfg.bClock,           TRUE  },
    { "Taskbar",     "HideMinApps",   &g_cfg.bHideMinApps,     TRUE  },
    { "Taskbar",     "MaxClamp",      &g_cfg.bMaxClamp,        TRUE  },
    { "Taskbar",     "CtrlEscStart",  &g_cfg.bCtrlEscStart,    FALSE },
    { "Taskbar",     "WinKey",        &g_cfg.bWinKey,          TRUE  },
    { "Desktop",     "RightClose",    &g_cfg.bRightClose,      FALSE },
    { "Desktop",     "Icons",         &g_cfg.bDesktopIcons,    TRUE  },
    { "Desktop",     "Menu",          &g_cfg.bDesktopMenu,     TRUE  },
    { "Desktop",     "SnapToGrid",    &g_cfg.bSnapToGrid,      TRUE  },
    { "Desktop",     "SingleClick",   &g_cfg.bSingleClick,     FALSE },
    { "Desktop",     "ShortcutArrows", &g_cfg.bShortcutArrows, TRUE  },
    { "Startup",     "Run",           &g_cfg.bRunStartup,      TRUE  },
    { "Preferences", "ConfirmExit",   &g_cfg.bConfirmExit,     TRUE  },
    { "Folders",     "NewWindow",     &g_cfg.bFolderNewWindow, TRUE  },
    { "Folders",     "ShowHidden",    &g_cfg.bShowHidden,      FALSE },
    { "Folders",     "DeleteToBin",   &g_cfg.bDeleteToBin,     TRUE  },
    { "Folders",     "ConfirmDelete", &g_cfg.bConfirmDelete,   TRUE  },
    { "Folders",     "FileMonitor",   &g_cfg.bFileMonitor,     TRUE  },
    { "Folders",     "Toolbar",       &g_cfg.bFolderToolbar,   TRUE  },
    { "Folders",     "StatusBar",     &g_cfg.bFolderStatusBar, TRUE  },
    { "Folders",     "Capitalize",    &g_cfg.bCapitalize,      FALSE },
    { "Folders",     "ShadeSelected", &g_cfg.bShadeSelected,   TRUE  },
    { "Folders",     "FileHints",     &g_cfg.bFileHints,       TRUE  },
    { "Folders",     "AskOnDrop",     &g_cfg.bAskDrop,         FALSE },
    { "Folders",     "DragCopy",      &g_cfg.bDragCopy,        FALSE },
};

void CfgLoad(void)
{
    int i;

    for (i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); i++)
        *s_entries[i].pValue = CfgGetInt(s_entries[i].lpSection,
                                         s_entries[i].lpKey,
                                         s_entries[i].bDefault) != 0;
}

void CfgSave(void)
{
    int i;

    for (i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); i++)
        CfgSetInt(s_entries[i].lpSection, s_entries[i].lpKey,
                  *s_entries[i].pValue ? 1 : 0);
}
