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
 * fileops.c - file operations: copy, move, delete, rename, new folder
 *
 * Operations work on "path lists" (a global memory block of zero-terminated
 * paths ended by an empty string) and show a progress dialog with a Cancel
 * button while they run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <dos.h>
#include "newshell.h"

#define COPY_BUFSIZE    32768U
#define PROGRESS_DELAY  400         /* ms before the progress box appears */

/* -------------------------------------------------------------------- */
/* Path lists                                                           */
/* -------------------------------------------------------------------- */

static UINT PathListUsed(LPCSTR lp)
{
    LPCSTR p = lp;

    while (*p)
        p += lstrlen(p) + 1;
    return (UINT)(p - lp);          /* excluding the final terminator */
}

BOOL PathListAdd(HGLOBAL FAR *phList, LPCSTR lpPath)
{
    UINT cbUsed = 0, cbPath = lstrlen(lpPath) + 1;
    HGLOBAL hNew;
    LPSTR lp;

    if (*phList) {
        lp = (LPSTR)GlobalLock(*phList);
        cbUsed = PathListUsed(lp);
        GlobalUnlock(*phList);
        if ((DWORD)cbUsed + cbPath + 1 > 65000L)
            return FALSE;
        hNew = GlobalReAlloc(*phList, cbUsed + cbPath + 1, GMEM_MOVEABLE);
    } else
        hNew = GlobalAlloc(GMEM_MOVEABLE, cbPath + 1);
    if (!hNew)
        return FALSE;
    *phList = hNew;
    lp = (LPSTR)GlobalLock(hNew);
    lstrcpy(lp + cbUsed, lpPath);
    lp[cbUsed + cbPath] = '\0';
    GlobalUnlock(hNew);
    return TRUE;
}

/* Locks the list and returns the first path (NULL if empty) */
LPCSTR PathListFirst(HGLOBAL hList)
{
    LPCSTR lp = (LPCSTR)GlobalLock(hList);
    return (lp && *lp) ? lp : NULL;
}

LPCSTR PathListNext(LPCSTR lp)
{
    lp += lstrlen(lp) + 1;
    return *lp ? lp : NULL;
}

void PathListUnlock(HGLOBAL hList)
{
    GlobalUnlock(hList);
}

int PathListCount(HGLOBAL hList)
{
    LPCSTR lp;
    int n = 0;

    for (lp = PathListFirst(hList); lp; lp = PathListNext(lp))
        n++;
    PathListUnlock(hList);
    return n;
}

void FormatBytes(DWORD dw, LPSTR lpBuf)
{
    if (dw < 1024L)
        wsprintf(lpBuf, "%lu bytes", dw);
    else if (dw < 10L * 1024L * 1024L)
        wsprintf(lpBuf, "%luKB", (dw + 1023L) / 1024L);
    else
        wsprintf(lpBuf, "%lu.%luMB", dw / 1048576L,
                 (dw % 1048576L) * 10L / 1048576L);
}

/* -------------------------------------------------------------------- */
/* Progress                                                             */
/* -------------------------------------------------------------------- */

typedef struct tagOPSTATE {
    HWND    hwndOwner;
    HWND    hwndProgress;
    FARPROC lpfnDlg;
    DWORD   dwStart;
    BOOL    bCancel;
    BOOL    bYesAll;            /* replace: Yes to All, No to All */
    BOOL    bNoAll;
    BOOL    bFolderAll;         /* existing folders: copy into all */
    BOOL    bProtAll;           /* protected files: Yes to All */
    int     nDone;
    int     nTotal;
    LPSTR   lpBuf;
    HGLOBAL hBuf;
    LPCSTR  lpVerb;
} OPSTATE;

static OPSTATE s_op;
static BOOL    s_bBusy;

BOOL CALLBACK NSEXPORT ProgressDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam)
{
    (void)hDlg;
    (void)lParam;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && wParam == IDCANCEL) {
        s_op.bCancel = TRUE;
        return TRUE;
    }
    return FALSE;
}

/* Lets the user interact with the progress box; returns FALSE on cancel */
static BOOL Pump(void)
{
    MSG msg;

    if (!s_op.hwndProgress && GetTickCount() - s_op.dwStart > PROGRESS_DELAY) {
        s_op.lpfnDlg = MakeProcInstance((FARPROC)ProgressDlgProc, g_hInst);
        s_op.hwndProgress = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_PROGRESS),
                                         s_op.hwndOwner, (DLGPROC)s_op.lpfnDlg);
        if (s_op.hwndProgress) {
            SetWindowText(s_op.hwndProgress, s_op.lpVerb);
            ShowWindow(s_op.hwndProgress, SW_SHOW);
        }
    }
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (s_op.hwndProgress && IsDialogMessage(s_op.hwndProgress, &msg))
            continue;
        /* keep the user out of the rest of the shell meanwhile */
        if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST)
            continue;
        if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST
            && msg.message != WM_MOUSEMOVE)
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return !s_op.bCancel;
}

static void Step(LPCSTR lpFrom, LPCSTR lpTo)
{
    char szText[MAXPATHLEN + 20];

    s_op.nDone++;
    if (!s_op.hwndProgress)
        return;
    LfnLongName(lpFrom, szText, sizeof(szText));
    SetDlgItemText(s_op.hwndProgress, IDC_PROGTEXT1, szText);
    if (lpTo) {
        char szLong[MAXPATHLEN];
        LfnDisplayPath(lpTo, szLong, sizeof(szLong));
        wsprintf(szText, "to %s", (LPSTR)szLong);
        SetDlgItemText(s_op.hwndProgress, IDC_PROGTEXT2, szText);
    }
    SendDlgItemMessage(s_op.hwndProgress, IDC_PROGBAR, WM_NS_SETPROGRESS,
                       s_op.nDone, (LPARAM)s_op.nTotal);
}

static BOOL BeginOp(HWND hwndOwner, LPCSTR lpVerb, int nTotal, BOOL bBuffer)
{
    if (s_bBusy)
        return FALSE;
    _fmemset(&s_op, 0, sizeof(s_op));
    s_op.hwndOwner = hwndOwner;
    s_op.dwStart = GetTickCount();
    s_op.nTotal = nTotal;
    s_op.lpVerb = lpVerb;
    if (bBuffer) {
        s_op.hBuf = GlobalAlloc(GMEM_MOVEABLE, COPY_BUFSIZE);
        if (!s_op.hBuf)
            return FALSE;
        s_op.lpBuf = (LPSTR)GlobalLock(s_op.hBuf);
    }
    s_bBusy = TRUE;
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    return TRUE;
}

static void EndOp(void)
{
    if (s_op.hwndProgress)
        DestroyWindow(s_op.hwndProgress);
    if (s_op.lpfnDlg)
        FreeProcInstance(s_op.lpfnDlg);
    if (s_op.hBuf) {
        GlobalUnlock(s_op.hBuf);
        GlobalFree(s_op.hBuf);
    }
    s_bBusy = FALSE;
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

static void OpError(LPCSTR lpFormat, LPCSTR lpPath)
{
    char szMsg[LFN_MAXPATH + 100], szLong[LFN_MAXPATH];

    LfnDisplayPath(lpPath, szLong, sizeof(szLong));
    wsprintf(szMsg, lpFormat, (LPSTR)szLong);
    MessageBox(s_op.hwndProgress ? s_op.hwndProgress : s_op.hwndOwner,
               szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
}

/* -------------------------------------------------------------------- */
/* Replace confirmation                                                 */
/* -------------------------------------------------------------------- */

static LPCSTR s_lpAskTitle;
static BOOL   s_bAskNoAll;          /* offer No to All */

BOOL CALLBACK NSEXPORT ReplaceDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowText(hDlg, s_lpAskTitle);
        SetDlgItemText(hDlg, IDC_REPLACETEXT, (LPCSTR)lParam);
        if (!s_bAskNoAll)
            ShowWindow(GetDlgItem(hDlg, IDC_NOALL), SW_HIDE);
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDYES || wParam == IDNO || wParam == IDCANCEL
            || wParam == IDC_YESALL || wParam == IDC_NOALL) {
            EndDialog(hDlg, wParam);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/*
 * Asks with Yes, Yes to All, No, No to All and Cancel; the "to All"
 * answers are remembered in *pbYesAll and *pbNoAll (which may be NULL to
 * offer no No to All). Returns IDYES, IDNO or IDCANCEL.
 */
static int AskAll(LPCSTR lpTitle, LPCSTR lpText, BOOL *pbYesAll, BOOL *pbNoAll)
{
    FARPROC lpfn;
    int n;

    if (*pbYesAll)
        return IDYES;
    if (pbNoAll && *pbNoAll)
        return IDNO;
    s_lpAskTitle = lpTitle;
    s_bAskNoAll = pbNoAll != NULL;
    lpfn = MakeProcInstance((FARPROC)ReplaceDlgProc, g_hInst);
    n = DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_REPLACE),
                       s_op.hwndProgress ? s_op.hwndProgress : s_op.hwndOwner,
                       (DLGPROC)lpfn, (LPARAM)lpText);
    FreeProcInstance(lpfn);
    if (n == IDC_YESALL) {
        *pbYesAll = TRUE;
        n = IDYES;
    } else if (n == IDC_NOALL) {
        if (pbNoAll)
            *pbNoAll = TRUE;
        n = IDNO;
    }
    return n;
}

/* Returns IDYES, IDNO or IDCANCEL */
static int AskReplace(LPCSTR lpPath)
{
    char szName[MAXPATHLEN], szText[MAXPATHLEN + 100];

    if (!CfgGetInt("Folders", "ConfirmReplace", 1))
        return IDYES;
    lstrcpy(szName, lpPath);
    wsprintf(szText, "This folder already contains a file called '%s'.\n\n"
                     "Would you like to replace the existing file?",
             (LPSTR)PathFileName(szName));
    return AskAll("Confirm File Replace", szText, &s_op.bYesAll, &s_op.bNoAll);
}

/* Read-only, hidden or system files: delete or move them? */
static int AskProtected(LPCSTR lpPath, unsigned uAttr, BOOL bMove)
{
    char szName[MAXPATHLEN], szText[MAXPATHLEN + 120];

    if (!(uAttr & (_A_RDONLY | _A_HIDDEN | _A_SYSTEM))
        || !CfgGetInt("Folders", "ConfirmProtected", 1))
        return IDYES;
    lstrcpy(szName, lpPath);
    wsprintf(szText, "'%s' is a %s file.\n\nAre you sure you want to %s it?",
             (LPSTR)PathFileName(szName),
             (LPSTR)((uAttr & _A_SYSTEM) ? "system" : (uAttr & _A_HIDDEN) ? "hidden"
                                                                          : "read-only"),
             (LPSTR)(bMove ? "move" : "delete"));
    return AskAll("Confirm", szText, &s_op.bProtAll, NULL);
}

/* -------------------------------------------------------------------- */
/* Primitives                                                           */
/* -------------------------------------------------------------------- */

static BOOL GetAttr(LPCSTR lpPath, unsigned *puAttr)
{
    char szPath[MAXPATHLEN];

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    return _dos_getfileattr(szPath, puAttr) == 0;
}

static BOOL SameDrive(LPCSTR a, LPCSTR b)
{
    return (a[0] & ~0x20) == (b[0] & ~0x20);
}

/* TRUE if lpChild is lpParent or lies inside it */
static BOOL IsInside(LPCSTR lpChild, LPCSTR lpParent)
{
    int n = lstrlen(lpParent);
    char szChild[MAXPATHLEN];

    lstrcpyn(szChild, lpChild, sizeof(szChild));
    if (lstrlen(szChild) < n)
        return FALSE;
    szChild[n] = '\0';
    if (lstrcmpi(szChild, lpParent) != 0)
        return FALSE;
    return lpChild[n] == '\0' || lpChild[n] == '\\' || lpParent[n - 1] == '\\';
}

/* The LFN functions keep the long names (and remove their directory
   entries with the files); without LFN they are the DOS ones */
static BOOL DosRename(LPCSTR lpFrom, LPCSTR lpTo)
{
    return LfnRename(lpFrom, lpTo) == 0;
}

static BOOL DeleteFile(LPCSTR lpPath)
{
    LfnSetAttr(lpPath, _A_NORMAL);
    return LfnDelete(lpPath) == 0;
}

static BOOL MakeDir(LPCSTR lpPath)
{
    return LfnMkDir(lpPath) == 0;
}

static BOOL RemoveDir(LPCSTR lpPath)
{
    LfnSetAttr(lpPath, 0);
    return LfnRmDir(lpPath) == 0;
}

/*
 * The destination of an item called lpName (long or short) in lpDir:
 * the short path of what is there already (TRUE), else where the new
 * item goes: lpDir\lpName, a long path when the drive has long names.
 */
static BOOL DestPath(LPCSTR lpDir, LPCSTR lpName, LPSTR lpTo, LPSTR lpNew)
{
    char szShort[14];

    lstrcpyn(lpNew, lpDir, LFN_MAXPATH);
    if (lstrlen(lpNew) + lstrlen(lpName) + 2 >= LFN_MAXPATH)
        return FALSE;
    PathAppend(lpNew, lpName);
    if (LfnShortOf(lpDir, lpName, szShort) == 0) {
        lstrcpyn(lpTo, lpDir, MAXPATHLEN);
        PathAppend(lpTo, szShort);
        return TRUE;
    }
    if (!LfnDrive(lpDir))
        AnsiUpper(lpNew);
    lstrcpyn(lpTo, lpNew, MAXPATHLEN);
    return FALSE;
}

static BOOL CopyFileData(LPCSTR lpFrom, LPCSTR lpTo)
{
    HFILE hSrc, hDst;
    unsigned uDate, uTime, uAttr;
    UINT n;
    BOOL bOK = TRUE;

    hSrc = _lopen(lpFrom, OF_READ | OF_SHARE_DENY_WRITE);
    if (hSrc == HFILE_ERROR) {
        OpError("Cannot read '%s'.", lpFrom);
        return FALSE;
    }
    if (GetAttr(lpTo, &uAttr)) {
        char szTo[MAXPATHLEN];
        lstrcpyn(szTo, lpTo, sizeof(szTo));
        _dos_setfileattr(szTo, _A_NORMAL);
    }
    hDst = _lcreat(lpTo, 0);
    if (hDst == HFILE_ERROR) {
        _lclose(hSrc);
        OpError("Cannot create '%s'.", lpTo);
        return FALSE;
    }
    _dos_getftime(hSrc, &uDate, &uTime);
    for (;;) {
        n = _lread(hSrc, s_op.lpBuf, COPY_BUFSIZE);
        if (n == (UINT)HFILE_ERROR) {
            OpError("Error reading '%s'.", lpFrom);
            bOK = FALSE;
            break;
        }
        if (n == 0)
            break;
        if (_lwrite(hDst, s_op.lpBuf, n) != n) {
            OpError("Error writing '%s'. The disk may be full.", lpTo);
            bOK = FALSE;
            break;
        }
        if (!Pump()) {
            bOK = FALSE;
            break;
        }
    }
    _dos_setftime(hDst, uDate, uTime);
    _lclose(hDst);
    _lclose(hSrc);
    if (!bOK) {
        DeleteFile(lpTo);
        return FALSE;
    }
    if (GetAttr(lpFrom, &uAttr)) {
        char szTo[MAXPATHLEN];
        lstrcpyn(szTo, lpTo, sizeof(szTo));
        _dos_setfileattr(szTo, uAttr & (_A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_ARCH));
    }
    return TRUE;
}

#define FIND_ALL    (_A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_SUBDIR | _A_ARCH)

/* a search per level of a tree walk, from the heap (it is 340 bytes) */
static LPLFNFIND NewFind(void)
{
    return (LPLFNFIND)malloc(sizeof(LFNFIND));
}

static int CountTree(LPCSTR lpPath)
{
    LPLFNFIND f;
    char szPath[MAXPATHLEN];
    unsigned uAttr;
    int n = 1;

    if (!GetAttr(lpPath, &uAttr) || !(uAttr & _A_SUBDIR) || !(f = NewFind()))
        return 1;
    lstrcpy(szPath, lpPath);
    PathAppend(szPath, "*.*");
    if (LfnFindFirst(szPath, FIND_ALL, f) == 0) {
        do {
            if (f->szShort[0] == '.')
                continue;
            lstrcpy(szPath, lpPath);
            PathAppend(szPath, f->szShort);
            if (f->bAttr & _A_SUBDIR)
                n += CountTree(szPath);
            else
                n++;
        } while (LfnFindNext(f) == 0);
    }
    LfnFindClose(f);
    free(f);
    return n;
}

static int CountList(HGLOBAL hList)
{
    LPCSTR lp;
    int n = 0;

    for (lp = PathListFirst(hList); lp; lp = PathListNext(lp))
        n += CountTree(lp);
    PathListUnlock(hList);
    return n;
}

/* The 8.3 names in a directory, collected before it is changed */
static HGLOBAL ListChildren(LPCSTR lpDir)
{
    LPLFNFIND f = NewFind();
    char szPath[MAXPATHLEN];
    HGLOBAL hNames = NULL;

    if (!f)
        return NULL;
    lstrcpy(szPath, lpDir);
    PathAppend(szPath, "*.*");
    if (LfnFindFirst(szPath, FIND_ALL, f) == 0)
        do {
            if (f->szShort[0] != '.' && !PathListAdd(&hNames, f->szShort))
                break;
        } while (LfnFindNext(f) == 0);
    LfnFindClose(f);
    free(f);
    return hNames;
}

static BOOL IsEmptyDir(LPCSTR lpDir)
{
    HGLOBAL h = ListChildren(lpDir);

    if (!h)
        return TRUE;
    GlobalFree(h);
    return FALSE;
}

/* Deletes a file or a whole directory tree */
static BOOL DeleteTree(LPCSTR lpPath)
{
    HGLOBAL hNames;
    LPCSTR lp;
    char szPath[MAXPATHLEN];
    unsigned uAttr;
    BOOL bOK = TRUE;

    if (!Pump())
        return FALSE;
    Step(lpPath, NULL);
    if (!GetAttr(lpPath, &uAttr))
        return TRUE;                    /* already gone */
    if (!(uAttr & _A_SUBDIR)) {
        switch (AskProtected(lpPath, uAttr, FALSE)) {
        case IDNO:
            return TRUE;
        case IDCANCEL:
            s_op.bCancel = TRUE;
            return FALSE;
        }
        if (!DeleteFile(lpPath)) {
            OpError("Cannot delete '%s'. It may be in use or write-protected.", lpPath);
            return FALSE;
        }
        return TRUE;
    }
    hNames = ListChildren(lpPath);
    if (hNames) {
        for (lp = PathListFirst(hNames); lp && bOK; lp = PathListNext(lp)) {
            lstrcpy(szPath, lpPath);
            PathAppend(szPath, lp);
            if (!DeleteTree(szPath))
                bOK = FALSE;
        }
        PathListUnlock(hNames);
        GlobalFree(hNames);
    }
    if (bOK && GetAttr(lpPath, &uAttr) && !IsEmptyDir(lpPath))
        return TRUE;                /* something was kept (No) */
    if (bOK && !RemoveDir(lpPath)) {
        OpError("Cannot remove the folder '%s'.", lpPath);
        bOK = FALSE;
    }
    return bOK;
}

/*
 * Copies or moves one file or directory tree into lpDestDir, under the
 * name lpName (the long name when the source has one).
 */
static BOOL CopyTree(LPCSTR lpFrom, LPCSTR lpDestDir, LPCSTR lpName, BOOL bMove)
{
    LPLFNFIND f;
    char szFrom[MAXPATHLEN], szTo[MAXPATHLEN], szNew[LFN_MAXPATH];
    unsigned uAttr;
    BOOL bOK = TRUE, bExists;

    if (!Pump())
        return FALSE;
    if (!GetAttr(lpFrom, &uAttr)) {
        OpError("'%s' could not be found.", lpFrom);
        return FALSE;
    }
    bExists = DestPath(lpDestDir, lpName, szTo, szNew);

    if (!(uAttr & _A_SUBDIR)) {
        Step(lpFrom, lpDestDir);
        if (lstrcmpi(lpFrom, szTo) == 0) {
            OpError("Cannot copy '%s' onto itself.", lpFrom);
            return FALSE;
        }
        if (bMove)
            switch (AskProtected(lpFrom, uAttr, TRUE)) {
            case IDNO:
                return TRUE;
            case IDCANCEL:
                s_op.bCancel = TRUE;
                return FALSE;
            }
        if (bExists) {
            switch (AskReplace(szTo)) {
            case IDNO:
                return TRUE;
            case IDCANCEL:
                s_op.bCancel = TRUE;
                return FALSE;
            }
            if (bMove && SameDrive(lpFrom, szTo)) {
                DeleteFile(szTo);
                bExists = FALSE;
            }
        }
        if (bMove && SameDrive(lpFrom, szTo) && DosRename(lpFrom, szNew))
            return TRUE;
        if (!bExists && lstrcmp(szTo, szNew) != 0) {
            /* a long name: create the file under it, fill it by its 8.3 name */
            if (LfnCreateEmpty(szNew, szTo, sizeof(szTo)) != 0) {
                OpError("Cannot create '%s'.", szNew);
                return FALSE;
            }
        }
        if (!CopyFileData(lpFrom, szTo))
            return FALSE;
        if (bMove && !DeleteFile(lpFrom))
            OpError("'%s' was copied but could not be removed.", lpFrom);
        return TRUE;
    }

    /* directory */
    if (IsInside(szTo, lpFrom)) {
        OpError("Cannot copy or move '%s' into itself.", lpFrom);
        return FALSE;
    }
    Step(lpFrom, lpDestDir);
    if (bExists && CfgGetInt("Folders", "ConfirmFolders", 1)) {
        char szText[LFN_MAXPATH + 120];

        wsprintf(szText, "There already is a folder called '%s'.\n\n"
                         "Would you like to %s the files into it?",
                 lpName, (LPSTR)(bMove ? "move" : "copy"));
        switch (AskAll("Confirm Folder Replace", szText, &s_op.bFolderAll, NULL)) {
        case IDNO:
            return TRUE;
        case IDCANCEL:
            s_op.bCancel = TRUE;
            return FALSE;
        }
    }
    if (bMove && SameDrive(lpFrom, szTo) && !bExists && DosRename(lpFrom, szNew))
        return TRUE;
    if (!bExists) {
        if (!MakeDir(szNew)) {
            OpError("Cannot create the folder '%s'.", szNew);
            return FALSE;
        }
        LfnShortPath(szNew, szTo, sizeof(szTo));
    }
    f = NewFind();
    if (!f)
        return FALSE;
    lstrcpy(szFrom, lpFrom);
    PathAppend(szFrom, "*.*");
    if (LfnFindFirst(szFrom, FIND_ALL, f) == 0) {
        /* moving changes the directory being searched: collect first */
        HGLOBAL hNames = NULL;
        LPCSTR lp;
        do {
            if (f->szShort[0] == '.')
                continue;
            lstrcpy(szFrom, f->szShort);
            lstrcat(szFrom, "|");
            if (lstrlen(szFrom) + lstrlen(f->szName) < (int)sizeof(szNew)) {
                lstrcpy(szNew, szFrom);
                lstrcat(szNew, f->szName);
                if (!PathListAdd(&hNames, szNew)) {
                    bOK = FALSE;
                    break;
                }
            }
        } while (LfnFindNext(f) == 0);
        LfnFindClose(f);
        if (hNames) {
            for (lp = PathListFirst(hNames); lp && bOK; lp = PathListNext(lp)) {
                LPCSTR lpBar = _fstrchr(lp, '|');
                int n = (int)(lpBar - lp);
                lstrcpy(szFrom, lpFrom);
                PathAppend(szFrom, "");
                lstrcpyn(szFrom + lstrlen(szFrom), lp, n + 1);
                lstrcpyn(szNew, lpBar + 1, sizeof(szNew));
                if (!CopyTree(szFrom, szTo, szNew, bMove))
                    bOK = FALSE;
            }
            PathListUnlock(hNames);
            GlobalFree(hNames);
        }
    }
    LfnFindClose(f);
    free(f);
    if (bOK && bMove)
        RemoveDir(lpFrom);
    return bOK;
}

static void RefreshParent(LPCSTR lpPath)
{
    char szDir[MAXPATHLEN];

    lstrcpyn(szDir, lpPath, sizeof(szDir));
    if (PathParent(szDir)) {
        FViewRefreshPath(szDir);
        ExplorerRefreshPath(szDir);
    }
}

/* -------------------------------------------------------------------- */
/* Public operations                                                    */
/* -------------------------------------------------------------------- */

BOOL FileOpCopyMove(HWND hwndOwner, HGLOBAL hList, LPCSTR lpDestDir, BOOL bMove)
{
    char szFrom[MAXPATHLEN], szName[LFN_MAXNAME];
    LPCSTR lp;
    BOOL bOK = TRUE;

    if (CfgGetInt("Folders", "ConfirmCopy", 0)) {
        char szMsg[LFN_MAXPATH * 2 + 80], szDest[LFN_MAXPATH];
        int n = PathListCount(hList);

        lp = PathListFirst(hList);
        lstrcpyn(szFrom, lp ? lp : "", sizeof(szFrom));
        PathListUnlock(hList);
        LfnLongName(szFrom, szName, sizeof(szName));
        LfnDisplayPath(lpDestDir, szDest, sizeof(szDest));
        if (n == 1)
            wsprintf(szMsg, "%s '%s' to %s?", (LPSTR)(bMove ? "Move" : "Copy"),
                     (LPSTR)szName, (LPSTR)szDest);
        else
            wsprintf(szMsg, "%s these %d items to %s?", (LPSTR)(bMove ? "Move" : "Copy"),
                     n, (LPSTR)szDest);
        if (MessageBox(hwndOwner, szMsg, bMove ? "Confirm Move" : "Confirm Copy",
                       MB_YESNO | MB_ICONQUESTION) != IDYES)
            return FALSE;
    }
    if (!BeginOp(hwndOwner, bMove ? "Moving" : "Copying", CountList(hList), TRUE))
        return FALSE;
    for (lp = PathListFirst(hList); lp && bOK; lp = PathListNext(lp)) {
        lstrcpyn(szFrom, lp, sizeof(szFrom));
        if (lstrlen(szFrom) <= 3) {
            OpError("Cannot copy or move a whole drive (%s).", szFrom);
            continue;
        }
        /* the long name goes with it (onto a drive without long names
           the 8.3 name) */
        if (!LfnDrive(lpDestDir) || !LfnLongName(szFrom, szName, sizeof(szName)))
            lstrcpy(szName, PathFileName(szFrom));
        bOK = CopyTree(szFrom, lpDestDir, szName, bMove);
    }
    PathListUnlock(hList);
    EndOp();

    FViewRefreshPath(lpDestDir);
    ExplorerRefreshPath(lpDestDir);
    if (bMove) {
        for (lp = PathListFirst(hList); lp; lp = PathListNext(lp))
            RefreshParent(lp);
        PathListUnlock(hList);
    }
    return bOK;
}

BOOL FileOpDelete(HWND hwndOwner, HGLOBAL hList, BOOL bToBin)
{
    char szMsg[LFN_MAXPATH + 80], szName[MAXPATHLEN], szLong[LFN_MAXNAME];
    LPCSTR lp;
    int n = PathListCount(hList);
    BOOL bOK = TRUE;

    if (n == 0)
        return FALSE;
    lp = PathListFirst(hList);
    lstrcpy(szName, lp);
    PathListUnlock(hList);
    if (g_cfg.bConfirmDelete) {
        LfnLongName(szName, szLong, sizeof(szLong));
        if (n == 1)
            wsprintf(szMsg, bToBin ? "Are you sure you want to send '%s' to the Bin?"
                                   : "Are you sure you want to delete '%s'?",
                     (LPSTR)szLong);
        else
            wsprintf(szMsg, bToBin ? "Are you sure you want to send these %d items to the Bin?"
                                   : "Are you sure you want to delete these %d items?", n);
        if (MessageBox(hwndOwner, szMsg, bToBin ? "Send to Bin" : "Confirm Delete",
                       MB_YESNO | MB_ICONQUESTION) != IDYES)
            return FALSE;
    }

    if (!BeginOp(hwndOwner, "Deleting", bToBin ? n : CountList(hList), FALSE))
        return FALSE;
    for (lp = PathListFirst(hList); lp && bOK; lp = PathListNext(lp)) {
        lstrcpyn(szName, lp, sizeof(szName));
        if (lstrlen(szName) <= 3)
            continue;
        if (bToBin && BinCanTake(szName)) {
            Step(szName, NULL);
            bOK = BinSend(szName) && Pump();
        } else
            bOK = DeleteTree(szName);
    }
    PathListUnlock(hList);
    EndOp();

    for (lp = PathListFirst(hList); lp; lp = PathListNext(lp))
        RefreshParent(lp);
    PathListUnlock(hList);
    if (bToBin)
        BinChanged();
    return bOK;
}

/* Moves a file or folder into lpToDir, named lpName (a long name where
   the drive has them); used by the Bin to send and restore items */
BOOL FileOpMoveTo(HWND hwndOwner, LPCSTR lpFrom, LPCSTR lpToDir, LPCSTR lpName)
{
    BOOL bOK, bNested = s_bBusy;

    if (!bNested && !BeginOp(hwndOwner, "Moving", CountTree(lpFrom), TRUE))
        return FALSE;
    if (bNested && !s_op.lpBuf) {
        s_op.hBuf = GlobalAlloc(GMEM_MOVEABLE, COPY_BUFSIZE);
        if (!s_op.hBuf)
            return FALSE;
        s_op.lpBuf = (LPSTR)GlobalLock(s_op.hBuf);
    }
    bOK = CopyTree(lpFrom, lpToDir, lpName, TRUE);
    if (!bNested)
        EndOp();
    return bOK;
}

/* Deletes a file or folder permanently (no confirmation) */
BOOL FileOpRemove(HWND hwndOwner, LPCSTR lpPath)
{
    BOOL bOK, bNested = s_bBusy;

    if (!bNested && !BeginOp(hwndOwner, "Deleting", CountTree(lpPath), FALSE))
        return FALSE;
    bOK = DeleteTree(lpPath);
    if (!bNested)
        EndOp();
    return bOK;
}

/* Renames lpPath (a short path) to lpNewName: a long name on drives with
   long names, else an 8.3 name */
BOOL FileOpRename(HWND hwndOwner, LPCSTR lpPath, LPCSTR lpNewName)
{
    char szNew[LFN_MAXPATH], szDir[MAXPATHLEN], szMsg[LFN_MAXPATH + 80];
    char szShort[14];
    BOOL bLong = LfnDrive(lpPath);

    if (!LfnValidName(lpNewName, bLong)) {
        MessageBox(hwndOwner, bLong
                   ? "A file name cannot contain any of these characters:\n"
                     "\\ / : * ? \" < > |"
                   : "A file name has up to 8 characters, a full stop and up to 3 "
                     "more, without spaces and any of these characters:\n"
                     "\\ / : * ? \" < > | + , ; = [ ]",
                   NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    lstrcpyn(szDir, lpPath, sizeof(szDir));
    PathParent(szDir);
    lstrcpy(szNew, szDir);
    PathAppend(szNew, lpNewName);
    if (!bLong)
        AnsiUpper(szNew);
    /* something else by that name (a new case of the same name is fine) */
    if (LfnShortOf(szDir, lpNewName, szShort) == 0
        && lstrcmpi(szShort, PathFileName((LPSTR)lpPath)) != 0) {
        wsprintf(szMsg, "Cannot rename: '%s' already exists.", (LPSTR)lpNewName);
        MessageBox(hwndOwner, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (!DosRename(lpPath, szNew)) {
        char szLong[LFN_MAXPATH];
        LfnDisplayPath(lpPath, szLong, sizeof(szLong));
        wsprintf(szMsg, "Cannot rename '%s'.", (LPSTR)szLong);
        MessageBox(hwndOwner, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    RefreshParent(lpPath);
    return TRUE;
}

/* Creates "New Folder" (with long names) or "NEWFOLDR" (NEWFOLD1...) in
   lpDir; returns its 8.3 name */
BOOL FileOpNewFolder(HWND hwndOwner, LPCSTR lpDir, LPSTR lpName)
{
    char szPath[LFN_MAXPATH], szName[24];
    BOOL bLong = LfnDrive(lpDir);
    int i;

    for (i = 0; i < 100; i++) {
        if (bLong)
            wsprintf(szName, i ? "New Folder (%d)" : "New Folder", i + 1);
        else if (i == 0)
            lstrcpy(szName, "NEWFOLDR");
        else
            wsprintf(szName, i < 10 ? "NEWFOLD%d" : "NEWFOL%d", i);
        lstrcpy(szPath, lpDir);
        PathAppend(szPath, szName);
        if (LfnShortOf(lpDir, szName, lpName) != 0) {
            if (MakeDir(szPath)) {
                if (LfnShortOf(lpDir, szName, lpName) != 0)
                    lstrcpy(lpName, szName);
                ExplorerRefreshPath(lpDir);
                return TRUE;
            }
            break;
        }
    }
    MessageBox(hwndOwner, "Cannot create a new folder here.", NS_APPNAME,
               MB_OK | MB_ICONEXCLAMATION);
    return FALSE;
}

/* Total size, file and folder count of a tree */
void FileOpMeasure(LPCSTR lpPath, DWORD FAR *pdwSize, int FAR *pnFiles,
                   int FAR *pnDirs)
{
    struct find_t ft;
    char szPath[MAXPATHLEN];

    lstrcpy(szPath, lpPath);
    PathAppend(szPath, "*.*");
    if (_dos_findfirst(szPath, FIND_ALL, &ft) != 0)
        return;
    do {
        if (ft.name[0] == '.')
            continue;
        if (ft.attrib & _A_SUBDIR) {
            (*pnDirs)++;
            lstrcpy(szPath, lpPath);
            PathAppend(szPath, ft.name);
            FileOpMeasure(szPath, pdwSize, pnFiles, pnDirs);
        } else {
            (*pnFiles)++;
            *pdwSize += ft.size;
        }
    } while (_dos_findnext(&ft) == 0);
}

/* -------------------------------------------------------------------- */
/* Progress bar control                                                 */
/* -------------------------------------------------------------------- */

LRESULT CALLBACK NSEXPORT ProgressBarWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                             LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT rc, rcFill;
    int nPos, nMax;

    switch (msg) {
    case WM_NS_SETPROGRESS:
        SetWindowWord(hwnd, 0, wParam);
        SetWindowWord(hwnd, 2, LOWORD(lParam));
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        return 0;
    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        Draw3DEdge(ps.hdc, &rc, TRUE, FALSE);
        InflateRect(&rc, -1, -1);
        nPos = GetWindowWord(hwnd, 0);
        nMax = GetWindowWord(hwnd, 2);
        rcFill = rc;
        if (nMax > 0)
            rcFill.right = rc.left + (int)((long)(rc.right - rc.left)
                           * (nPos > nMax ? nMax : nPos) / nMax);
        else
            rcFill.right = rc.left;
        FillSolid(ps.hdc, &rcFill, GetSysColor(COLOR_HIGHLIGHT));
        rc.left = rcFill.right;
        FillSolid(ps.hdc, &rc, GetSysColor(COLOR_BTNFACE));
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL FileOpRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ProgressBarWndProc;
    wc.cbWndExtra = 4;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLS_PROGRESS;
    return RegisterClass(&wc);
}
