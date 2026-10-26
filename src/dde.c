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
 * dde.c - the Program Manager DDE server
 *
 * Setup programs create their groups by talking to Program Manager over
 * DDE (service and topic "PROGMAN"). While NewShell16 is the shell it
 * answers in Program Manager's place, as Calmira does, and turns the
 * commands into Start menu entries under Programs in NSSTART.INI:
 *
 *   [CreateGroup(name[,file])]     [ShowGroup(name,cmd)]
 *   [AddItem(cmd[,name[,icon[,index[,x,y[,dir[,key[,min]]]]]]])]
 *   [DeleteItem(name)]  [ReplaceItem(name)]  [DeleteGroup(name)]
 *   [Reload[(name)]]    [ExitProgman(save)]
 *
 * Requests for the item "Groups" return the group names, a group name
 * returns its items in Program Manager's format. Plain DDE messages are
 * used, which DDEML clients understand as well; each conversation gets a
 * hidden window of its own.
 */
#include <stdlib.h>
#include <string.h>
#include "newshell.h"
#include <dde.h>

#define CLS_DDE         "NSDdeConv"
#define MAX_CMDTEXT     4096
#define MAX_ARGS        10
#define ARG_LEN         128

static ATOM  s_aProgman;
static char  s_szGroup[48];         /* the group commands apply to */
static char  s_szReplace[48];       /* ReplaceItem: the next AddItem goes here */

BOOL DdeEnabled(void)
{
    return g_bIsShell && CfgGetInt("Startup", "ProgmanDDE", 1)
        && FindWindow("Progman", NULL) == NULL;
}

/* -------------------------------------------------------------------- */
/* The Programs menu in NSSTART.INI                                     */
/* -------------------------------------------------------------------- */

/* Characters that cannot be part of an INI key or section name */
static void CleanName(LPSTR lp)
{
    for (; *lp; lp++)
        if (*lp == '=' || *lp == '[' || *lp == ']' || *lp == ';' || *lp == '\\')
            *lp = '_';
}

/* The text of a menu key without '&' and the submenu '*' */
static void PlainName(LPCSTR lpKey, LPSTR lpBuf, int cbBuf)
{
    int n = 0;

    for (; *lpKey && n < cbBuf - 1; lpKey++)
        if (*lpKey != '&')
            lpBuf[n++] = *lpKey;
    if (n > 0 && lpBuf[n - 1] == '*')
        n--;
    lpBuf[n] = '\0';
}

/*
 * Finds the key of group lpName in the Programs menu (ignoring '&');
 * returns FALSE if there is none. lpKey receives the key ("Name*").
 */
static BOOL FindGroupKey(LPCSTR lpName, LPSTR lpKey, int cbKey)
{
    char szPrograms[64], szPlain[64];
    char *pKeys, *p;
    BOOL bFound = FALSE;

    StartMenuProgramsSection(szPrograms, sizeof(szPrograms));
    pKeys = (char *)malloc(SM_KEYBUF);
    if (!pKeys)
        return FALSE;
    GetPrivateProfileString(szPrograms, NULL, "", pKeys, SM_KEYBUF, NS_STARTINI);
    for (p = pKeys; *p; p += lstrlen(p) + 1) {
        int n = lstrlen(p);

        if (n < 2 || p[n - 1] != '*')
            continue;
        PlainName(p, szPlain, sizeof(szPlain));
        if (lstrcmpi(szPlain, lpName) == 0) {
            lstrcpyn(lpKey, p, cbKey);
            bFound = TRUE;
            break;
        }
    }
    free(pKeys);
    return bFound;
}

/* The section of group lpName, creating the group if bCreate */
static BOOL GroupSection(LPCSTR lpName, LPSTR lpSection, int cbSection, BOOL bCreate)
{
    char szPrograms[64], szKey[64], szName[48];

    if (!lpName[0])
        return FALSE;
    lstrcpyn(szName, lpName, sizeof(szName));
    CleanName(szName);
    StartMenuProgramsSection(szPrograms, sizeof(szPrograms));
    if (!FindGroupKey(szName, szKey, sizeof(szKey))) {
        if (!bCreate)
            return FALSE;
        wsprintf(szKey, "%s*", (LPSTR)szName);
        WritePrivateProfileString(szPrograms, szKey, ";;0;;0", NS_STARTINI);
    }
    lstrcpyn(szName, szKey, sizeof(szName));
    szName[lstrlen(szName) - 1] = '\0';
    if (lstrlen(szPrograms) + lstrlen(szName) + 2 > cbSection)
        return FALSE;
    wsprintf(lpSection, "%s\\%s", (LPSTR)szPrograms, (LPSTR)szName);
    return TRUE;
}

static BOOL DeleteGroup(LPCSTR lpName)
{
    char szPrograms[64], szKey[64], szSection[128];

    if (!GroupSection(lpName, szSection, sizeof(szSection), FALSE))
        return FALSE;
    StartMenuProgramsSection(szPrograms, sizeof(szPrograms));
    FindGroupKey(lpName, szKey, sizeof(szKey));
    WritePrivateProfileString(szSection, NULL, NULL, NS_STARTINI);
    WritePrivateProfileString(szPrograms, szKey, NULL, NS_STARTINI);
    return TRUE;
}

/*
 * Rewrites a section with lpOld replaced by lpNew=lpValue in its place
 * (Windows appends new keys at the end). Returns FALSE if lpOld is not
 * in the section.
 */
static BOOL ReplaceKey(LPCSTR lpSection, LPCSTR lpOld, LPCSTR lpNew, LPCSTR lpValue)
{
    char *pKeys, *p;
    char szValue[300];
    HGLOBAL hList = NULL;
    LPCSTR lp;
    BOOL bFound = FALSE;

    pKeys = (char *)malloc(SM_KEYBUF);
    if (!pKeys)
        return FALSE;
    GetPrivateProfileString(lpSection, NULL, "", pKeys, SM_KEYBUF, NS_STARTINI);
    for (p = pKeys; *p; p += lstrlen(p) + 1)
        if (lstrcmpi(p, lpOld) == 0)
            bFound = TRUE;
    if (!bFound) {
        free(pKeys);
        return FALSE;
    }
    /* key and value pairs, in order */
    for (p = pKeys; *p; p += lstrlen(p) + 1) {
        if (lstrcmpi(p, lpOld) == 0) {
            PathListAdd(&hList, lpNew);
            PathListAdd(&hList, lpValue);
        } else if (lstrcmpi(p, lpNew) != 0) {
            GetPrivateProfileString(lpSection, p, "", szValue, sizeof(szValue), NS_STARTINI);
            PathListAdd(&hList, p);
            PathListAdd(&hList, szValue);
        }
    }
    free(pKeys);
    WritePrivateProfileString(lpSection, NULL, NULL, NS_STARTINI);
    if (hList) {
        for (lp = PathListFirst(hList); lp; ) {
            LPCSTR lpVal = PathListNext(lp);

            if (!lpVal)
                break;
            WritePrivateProfileString(lpSection, lp, lpVal, NS_STARTINI);
            lp = PathListNext(lpVal);
        }
        PathListUnlock(hList);
        GlobalFree(hList);
    }
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* Commands                                                             */
/* -------------------------------------------------------------------- */

/* The file name of a command line without its extension */
static void DefaultItemName(LPCSTR lpCmd, LPSTR lpBuf, int cbBuf)
{
    char szFile[MAXPATHLEN];
    LPSTR lpName, lpDot;
    int i;

    while (*lpCmd == ' ')
        lpCmd++;
    for (i = 0; lpCmd[i] && lpCmd[i] != ' ' && i < sizeof(szFile) - 1; i++)
        szFile[i] = lpCmd[i];
    szFile[i] = '\0';
    lpName = PathFileName(szFile);
    lpDot = _fstrrchr(lpName, '.');
    if (lpDot)
        *lpDot = '\0';
    if (*lpName) {
        AnsiLower(lpName + 1);
        AnsiUpperBuff(lpName, 1);
    }
    lstrcpyn(lpBuf, lpName, cbBuf);
}

static BOOL AddItem(char (*args)[ARG_LEN], int nArgs)
{
    char szSection[128], szName[48], szValue[300];
    BOOL bMin = FALSE;

    if (nArgs < 1 || !args[0][0])
        return FALSE;
    if (!s_szGroup[0] || !GroupSection(s_szGroup, szSection, sizeof(szSection), TRUE))
        return FALSE;
    if (nArgs >= 2 && args[1][0])
        lstrcpyn(szName, args[1], sizeof(szName));
    else
        DefaultItemName(args[0], szName, sizeof(szName));
    CleanName(szName);
    if (szName[0] == '-')
        szName[0] = '_';
    if (nArgs >= 9)
        bMin = args[8][0] && args[8][0] != '0';
    wsprintf(szValue, "%s;%s;%d;%s;%s", (LPSTR)args[0],
             (LPSTR)(nArgs >= 7 ? args[6] : ""), bMin ? 1 : 0,
             (LPSTR)(nArgs >= 3 ? args[2] : ""),
             (LPSTR)(nArgs >= 4 && args[3][0] ? args[3] : "0"));
    if (s_szReplace[0]) {
        BOOL bDone = ReplaceKey(szSection, s_szReplace, szName, szValue);

        s_szReplace[0] = '\0';
        if (bDone)
            return TRUE;
    }
    WritePrivateProfileString(szSection, szName, szValue, NS_STARTINI);
    return TRUE;
}

static BOOL DeleteItem(LPCSTR lpName)
{
    char szSection[128], szValue[8];

    if (!s_szGroup[0] || !GroupSection(s_szGroup, szSection, sizeof(szSection), FALSE))
        return FALSE;
    if (GetPrivateProfileString(szSection, lpName, "", szValue, sizeof(szValue),
                                NS_STARTINI) == 0)
        return FALSE;
    WritePrivateProfileString(szSection, lpName, NULL, NS_STARTINI);
    return TRUE;
}

/* A pending ReplaceItem that no AddItem followed removes the item */
static void FlushReplace(void)
{
    if (s_szReplace[0]) {
        DeleteItem(s_szReplace);
        s_szReplace[0] = '\0';
    }
}

static BOOL RunCommand(LPCSTR lpName, char (*args)[ARG_LEN], int nArgs)
{
    char szSection[128];
    LPCSTR a0 = nArgs > 0 ? args[0] : "";

    if (lstrcmpi(lpName, "AddItem") == 0)
        return AddItem(args, nArgs);
    FlushReplace();
    if (lstrcmpi(lpName, "CreateGroup") == 0 || lstrcmpi(lpName, "ShowGroup") == 0) {
        if (!a0[0])
            return FALSE;
        lstrcpyn(s_szGroup, a0, sizeof(s_szGroup));
        return GroupSection(s_szGroup, szSection, sizeof(szSection),
                            lstrcmpi(lpName, "CreateGroup") == 0);
    }
    if (lstrcmpi(lpName, "DeleteGroup") == 0) {
        if (lstrcmpi(a0, s_szGroup) == 0)
            s_szGroup[0] = '\0';
        return DeleteGroup(a0);
    }
    if (lstrcmpi(lpName, "DeleteItem") == 0)
        return DeleteItem(a0);
    if (lstrcmpi(lpName, "ReplaceItem") == 0) {
        if (!s_szGroup[0] || !a0[0])
            return FALSE;
        lstrcpyn(s_szReplace, a0, sizeof(s_szReplace));
        return TRUE;
    }
    /* nothing to do for these: the Start menu is read when it opens */
    return lstrcmpi(lpName, "Reload") == 0 || lstrcmpi(lpName, "ExitProgman") == 0;
}

static LPCSTR SkipBlanks(LPCSTR lp)
{
    while (*lp == ' ' || *lp == '\t' || *lp == '\r' || *lp == '\n')
        lp++;
    return lp;
}

/*
 * Executes "[cmd(args)][cmd(args)]..."; arguments are separated by commas
 * and may be quoted. Returns FALSE if a command failed or was malformed.
 */
static BOOL Execute(LPCSTR lp)
{
    static char args[MAX_ARGS][ARG_LEN];
    char szName[24];
    BOOL bOK = TRUE;
    int nArgs, n;

    for (;;) {
        lp = SkipBlanks(lp);
        if (*lp == '\0')
            break;
        if (*lp++ != '[')
            return FALSE;
        lp = SkipBlanks(lp);
        for (n = 0; (*lp | 0x20) >= 'a' && (*lp | 0x20) <= 'z'; lp++)
            if (n < sizeof(szName) - 1)
                szName[n++] = *lp;
        szName[n] = '\0';
        lp = SkipBlanks(lp);
        nArgs = 0;
        if (*lp == '(') {
            int nDepth = 0;
            BOOL bQuote = FALSE;

            lp++;
            n = 0;
            nArgs = 1;
            args[0][0] = '\0';
            for (; *lp; lp++) {
                if (*lp == '"') {
                    bQuote = !bQuote;
                    continue;
                }
                if (!bQuote) {
                    if (*lp == '(')
                        nDepth++;
                    else if (*lp == ')' && nDepth-- == 0)
                        break;
                    else if (*lp == ',') {
                        if (nArgs >= MAX_ARGS)
                            continue;
                        args[nArgs - 1][n] = '\0';
                        args[nArgs][0] = '\0';
                        nArgs++;
                        n = 0;
                        continue;
                    } else if ((*lp == ' ' || *lp == '\t') && n == 0)
                        continue;
                }
                if (n < ARG_LEN - 1 && nArgs <= MAX_ARGS)
                    args[nArgs - 1][n++] = *lp;
            }
            if (*lp != ')')
                return FALSE;
            args[nArgs - 1][n] = '\0';
            /* trailing blanks of the last argument */
            while (n > 0 && args[nArgs - 1][n - 1] == ' ')
                args[nArgs - 1][--n] = '\0';
            lp++;
            if (nArgs == 1 && args[0][0] == '\0')
                nArgs = 0;
        }
        for (n = 0; n < nArgs; n++) {
            int k = lstrlen(args[n]);
            while (k > 0 && (args[n][k - 1] == ' ' || args[n][k - 1] == '\t'))
                args[n][--k] = '\0';
        }
        lp = SkipBlanks(lp);
        if (*lp++ != ']')
            return FALSE;
        if (!RunCommand(szName, args, nArgs))
            bOK = FALSE;
    }
    FlushReplace();
    WritePrivateProfileString(NULL, NULL, NULL, NS_STARTINI);   /* flush */
    return bOK;
}

/* -------------------------------------------------------------------- */
/* Requests                                                             */
/* -------------------------------------------------------------------- */

static void AppendText(HGLOBAL FAR *ph, UINT FAR *pcb, LPCSTR lpText)
{
    UINT n = lstrlen(lpText);
    HGLOBAL hNew;
    LPSTR lp;

    if (*ph == NULL)
        return;
    hNew = GlobalReAlloc(*ph, sizeof(DDEDATA) + *pcb + n + 1,
                         GMEM_MOVEABLE | GMEM_DDESHARE);
    if (!hNew) {
        GlobalFree(*ph);
        *ph = NULL;
        return;
    }
    *ph = hNew;
    lp = (LPSTR)((DDEDATA FAR *)GlobalLock(hNew))->Value;
    lstrcpy(lp + *pcb, lpText);
    *pcb += n;
    GlobalUnlock(hNew);
}

/* "Groups": one group name per line */
static void GroupList(HGLOBAL FAR *ph, UINT FAR *pcb)
{
    char szPrograms[64], szPlain[64];
    char *pKeys, *p;

    StartMenuProgramsSection(szPrograms, sizeof(szPrograms));
    pKeys = (char *)malloc(SM_KEYBUF);
    if (!pKeys)
        return;
    GetPrivateProfileString(szPrograms, NULL, "", pKeys, SM_KEYBUF, NS_STARTINI);
    for (p = pKeys; *p; p += lstrlen(p) + 1) {
        int n = lstrlen(p);

        if (n < 2 || p[n - 1] != '*')
            continue;
        PlainName(p, szPlain, sizeof(szPlain));
        AppendText(ph, pcb, szPlain);
        AppendText(ph, pcb, "\r\n");
    }
    free(pKeys);
}

/* A group: "name",file,count, then "name","cmd",dir,icon,x,y,index,key,min */
static BOOL GroupItems(LPCSTR lpGroup, HGLOBAL FAR *ph, UINT FAR *pcb)
{
    char szSection[128], szLine[400], szValue[300];
    char *pKeys, *p;
    LPSTR f[5], q;
    int nItems = 0, i, n;

    if (!GroupSection(lpGroup, szSection, sizeof(szSection), FALSE))
        return FALSE;
    pKeys = (char *)malloc(SM_KEYBUF);
    if (!pKeys)
        return FALSE;
    GetPrivateProfileString(szSection, NULL, "", pKeys, SM_KEYBUF, NS_STARTINI);
    for (p = pKeys; *p; p += lstrlen(p) + 1)
        if (*p != '-')
            nItems++;
    wsprintf(szLine, "\"%s\",%s,%d\r\n", lpGroup, (LPSTR)"", nItems);
    AppendText(ph, pcb, szLine);
    for (p = pKeys; *p; p += lstrlen(p) + 1) {
        if (*p == '-')
            continue;
        GetPrivateProfileString(szSection, p, "", szValue, sizeof(szValue), NS_STARTINI);
        f[0] = szValue;
        for (i = 1; i < 5; i++)
            f[i] = "";
        for (q = szValue, n = 0; *q && n < 4; q++)
            if (*q == ';') {
                *q = '\0';
                f[++n] = q + 1;
            }
        wsprintf(szLine, "\"%s\",\"%s\",%s,%s,0,0,%s,0,%s\r\n", (LPSTR)p, f[0], f[1],
                 f[3], (LPSTR)(f[4][0] ? f[4] : "0"), (LPSTR)(f[2][0] == '1' ? "1" : "0"));
        AppendText(ph, pcb, szLine);
    }
    free(pKeys);
    return TRUE;
}

/* The data for a request, as a DDEDATA block (NULL if unknown) */
static HGLOBAL RequestData(ATOM aItem)
{
    char szItem[64];
    HGLOBAL h;
    UINT cb = 0;
    DDEDATA FAR *lpData;

    if (!GlobalGetAtomName(aItem, szItem, sizeof(szItem)))
        return NULL;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DDEDATA) + 1);
    if (!h)
        return NULL;
    lpData = (DDEDATA FAR *)GlobalLock(h);
    _fmemset(lpData, 0, sizeof(DDEDATA));
    GlobalUnlock(h);
    if (lstrcmpi(szItem, "Groups") == 0)
        GroupList(&h, &cb);
    else if (!GroupItems(szItem, &h, &cb)) {
        GlobalFree(h);
        return NULL;
    }
    if (!h)
        return NULL;
    lpData = (DDEDATA FAR *)GlobalLock(h);
    lpData->fResponse = 1;
    lpData->fRelease = 1;
    lpData->cfFormat = CF_TEXT;
    GlobalUnlock(h);
    return h;
}

/* -------------------------------------------------------------------- */
/* Conversations                                                        */
/* -------------------------------------------------------------------- */

static void OnExecute(HWND hwnd, HWND hwndClient, HGLOBAL hCommands)
{
    LPSTR lpCmds, lpCopy;
    WORD wStatus = 0;
    UINT cb;

    lpCmds = (LPSTR)GlobalLock(hCommands);
    if (lpCmds) {
        cb = lstrlen(lpCmds);
        lpCopy = (LPSTR)malloc(cb < MAX_CMDTEXT ? cb + 1 : MAX_CMDTEXT);
        if (lpCopy) {
            lstrcpyn(lpCopy, lpCmds, cb < MAX_CMDTEXT ? cb + 1 : MAX_CMDTEXT);
            GlobalUnlock(hCommands);
            lpCmds = NULL;
            if (Execute(lpCopy))
                wStatus = 0x8000;           /* fAck */
            free(lpCopy);
        }
        if (lpCmds)
            GlobalUnlock(hCommands);
    }
    if (!PostMessage(hwndClient, WM_DDE_ACK, (WPARAM)hwnd,
                     MAKELPARAM(wStatus, hCommands)))
        GlobalFree(hCommands);
}

static void OnRequest(HWND hwnd, HWND hwndClient, UINT cfFormat, ATOM aItem)
{
    HGLOBAL hData = NULL;

    if (cfFormat == CF_TEXT)
        hData = RequestData(aItem);
    if (hData) {
        if (!PostMessage(hwndClient, WM_DDE_DATA, (WPARAM)hwnd,
                         MAKELPARAM(hData, aItem))) {
            GlobalFree(hData);
            GlobalDeleteAtom(aItem);
        }
    } else if (!PostMessage(hwndClient, WM_DDE_ACK, (WPARAM)hwnd,
                            MAKELPARAM(0, aItem)))
        GlobalDeleteAtom(aItem);
}

LRESULT CALLBACK NSEXPORT DdeConvWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam)
{
    HWND hwndClient = (HWND)GetWindowWord(hwnd, 0);

    switch (msg) {
    case WM_DDE_EXECUTE:
        OnExecute(hwnd, (HWND)wParam, (HGLOBAL)HIWORD(lParam));
        return 0;

    case WM_DDE_REQUEST:
        OnRequest(hwnd, (HWND)wParam, LOWORD(lParam), (ATOM)HIWORD(lParam));
        return 0;

    case WM_DDE_ADVISE:
    case WM_DDE_UNADVISE:
    case WM_DDE_POKE:
        /* not supported: a negative acknowledgement, the client frees */
        if (!PostMessage((HWND)wParam, WM_DDE_ACK, (WPARAM)hwnd,
                         MAKELPARAM(0, HIWORD(lParam))))
            GlobalDeleteAtom((ATOM)HIWORD(lParam));
        return 0;

    case WM_DDE_ACK:
    case WM_DDE_DATA:
        return 0;

    case WM_DDE_TERMINATE:
        if (hwndClient && IsWindow(hwndClient))
            PostMessage(hwndClient, WM_DDE_TERMINATE, (WPARAM)hwnd, 0L);
        SetWindowWord(hwnd, 0, 0);
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL DdeRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DdeConvWndProc;
    wc.cbWndExtra = sizeof(HWND);
    wc.hInstance = g_hInst;
    wc.lpszClassName = CLS_DDE;
    return RegisterClass(&wc);
}

/*
 * WM_DDE_INITIATE reaching the main window: answer for PROGMAN (or any
 * service/topic) from a new conversation window.
 */
void DdeInitiate(HWND hwndClient, ATOM aApp, ATOM aTopic)
{
    HWND hwnd;

    if (!DdeEnabled() || hwndClient == g_hwndMain)
        return;
    if (!s_aProgman)
        s_aProgman = GlobalAddAtom("PROGMAN");
    if ((aApp && aApp != s_aProgman) || (aTopic && aTopic != s_aProgman))
        return;
    hwnd = CreateWindow(CLS_DDE, "", WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
    if (!hwnd)
        return;
    SetWindowWord(hwnd, 0, (WORD)hwndClient);
    SendMessage(hwndClient, WM_DDE_ACK, (WPARAM)hwnd,
                MAKELPARAM(GlobalAddAtom("PROGMAN"), GlobalAddAtom("PROGMAN")));
}
