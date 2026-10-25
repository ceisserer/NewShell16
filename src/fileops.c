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
    BOOL    bYesAll;
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
    lstrcpy(szText, lpFrom);
    SetDlgItemText(s_op.hwndProgress, IDC_PROGTEXT1, PathFileName(szText));
    if (lpTo) {
        wsprintf(szText, "to %s", lpTo);
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
    char szMsg[MAXPATHLEN + 100];

    wsprintf(szMsg, lpFormat, lpPath);
    MessageBox(s_op.hwndProgress ? s_op.hwndProgress : s_op.hwndOwner,
               szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
}

/* -------------------------------------------------------------------- */
/* Replace confirmation                                                 */
/* -------------------------------------------------------------------- */

BOOL CALLBACK NSEXPORT ReplaceDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    char szText[MAXPATHLEN + 100];

    switch (msg) {
    case WM_INITDIALOG:
        wsprintf(szText, "This folder already contains a file called '%s'.\n\n"
                         "Would you like to replace the existing file?",
                 (LPSTR)lParam);
        SetDlgItemText(hDlg, IDC_REPLACETEXT, szText);
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDYES || wParam == IDNO || wParam == IDCANCEL
            || wParam == IDC_YESALL) {
            EndDialog(hDlg, wParam);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Returns IDYES, IDNO or IDCANCEL */
static int AskReplace(LPCSTR lpPath)
{
    char szName[MAXPATHLEN];
    FARPROC lpfn;
    int n;

    if (s_op.bYesAll)
        return IDYES;
    lstrcpy(szName, lpPath);
    lpfn = MakeProcInstance((FARPROC)ReplaceDlgProc, g_hInst);
    n = DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_REPLACE),
                       s_op.hwndProgress ? s_op.hwndProgress : s_op.hwndOwner,
                       (DLGPROC)lpfn, (LPARAM)(LPSTR)PathFileName(szName));
    FreeProcInstance(lpfn);
    if (n == IDC_YESALL) {
        s_op.bYesAll = TRUE;
        n = IDYES;
    }
    return n;
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

static BOOL Exists(LPCSTR lpPath)
{
    unsigned uAttr;
    return GetAttr(lpPath, &uAttr);
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

static BOOL DosRename(LPCSTR lpFrom, LPCSTR lpTo)
{
    char szFrom[MAXPATHLEN], szTo[MAXPATHLEN];

    lstrcpyn(szFrom, lpFrom, sizeof(szFrom));
    lstrcpyn(szTo, lpTo, sizeof(szTo));
    return rename(szFrom, szTo) == 0;
}

static BOOL DeleteFile(LPCSTR lpPath)
{
    char szPath[MAXPATHLEN];

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    _dos_setfileattr(szPath, _A_NORMAL);
    return remove(szPath) == 0;
}

static BOOL MakeDir(LPCSTR lpPath)
{
    char szPath[MAXPATHLEN];

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    return mkdir(szPath) == 0;
}

static BOOL RemoveDir(LPCSTR lpPath)
{
    char szPath[MAXPATHLEN];

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    _dos_setfileattr(szPath, _A_SUBDIR);
    return rmdir(szPath) == 0;
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

static int CountTree(LPCSTR lpPath)
{
    struct find_t ft;
    char szPath[MAXPATHLEN];
    unsigned uAttr;
    int n = 1;

    if (!GetAttr(lpPath, &uAttr) || !(uAttr & _A_SUBDIR))
        return 1;
    lstrcpy(szPath, lpPath);
    PathAppend(szPath, "*.*");
    if (_dos_findfirst(szPath, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM
                       | _A_SUBDIR | _A_ARCH, &ft) == 0) {
        do {
            if (ft.name[0] == '.')
                continue;
            lstrcpy(szPath, lpPath);
            PathAppend(szPath, ft.name);
            if (ft.attrib & _A_SUBDIR)
                n += CountTree(szPath);
            else
                n++;
        } while (_dos_findnext(&ft) == 0);
    }
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

/* Deletes a file or a whole directory tree */
static BOOL DeleteTree(LPCSTR lpPath)
{
    struct find_t ft;
    char szPath[MAXPATHLEN];
    unsigned uAttr;
    BOOL bOK = TRUE;

    if (!Pump())
        return FALSE;
    Step(lpPath, NULL);
    if (!GetAttr(lpPath, &uAttr))
        return TRUE;                    /* already gone */
    if (!(uAttr & _A_SUBDIR)) {
        if (!DeleteFile(lpPath)) {
            OpError("Cannot delete '%s'. It may be in use or write-protected.", lpPath);
            return FALSE;
        }
        return TRUE;
    }
    lstrcpy(szPath, lpPath);
    PathAppend(szPath, "*.*");
    if (_dos_findfirst(szPath, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM
                       | _A_SUBDIR | _A_ARCH, &ft) == 0) {
        do {
            if (ft.name[0] == '.')
                continue;
            lstrcpy(szPath, lpPath);
            PathAppend(szPath, ft.name);
            if (!DeleteTree(szPath)) {
                bOK = FALSE;
                break;
            }
        } while (_dos_findnext(&ft) == 0);
    }
    if (bOK && !RemoveDir(lpPath)) {
        OpError("Cannot remove the folder '%s'.", lpPath);
        bOK = FALSE;
    }
    return bOK;
}

/* Copies or moves one file or directory tree to the full path lpTo */
static BOOL CopyTree(LPCSTR lpFrom, LPCSTR lpTo, BOOL bMove)
{
    struct find_t ft;
    char szFrom[MAXPATHLEN], szTo[MAXPATHLEN];
    unsigned uAttr;
    BOOL bOK = TRUE;

    if (!Pump())
        return FALSE;
    if (!GetAttr(lpFrom, &uAttr)) {
        OpError("'%s' could not be found.", lpFrom);
        return FALSE;
    }

    if (!(uAttr & _A_SUBDIR)) {
        Step(lpFrom, lpTo);
        if (lstrcmpi(lpFrom, lpTo) == 0) {
            OpError("Cannot copy '%s' onto itself.", lpFrom);
            return FALSE;
        }
        if (Exists(lpTo)) {
            switch (AskReplace(lpTo)) {
            case IDNO:
                return TRUE;
            case IDCANCEL:
                s_op.bCancel = TRUE;
                return FALSE;
            }
            if (bMove && SameDrive(lpFrom, lpTo))
                DeleteFile(lpTo);
        }
        if (bMove && SameDrive(lpFrom, lpTo) && DosRename(lpFrom, lpTo))
            return TRUE;
        if (!CopyFileData(lpFrom, lpTo))
            return FALSE;
        if (bMove && !DeleteFile(lpFrom))
            OpError("'%s' was copied but could not be removed.", lpFrom);
        return TRUE;
    }

    /* directory */
    if (IsInside(lpTo, lpFrom)) {
        OpError("Cannot copy or move '%s' into itself.", lpFrom);
        return FALSE;
    }
    Step(lpFrom, lpTo);
    if (bMove && SameDrive(lpFrom, lpTo) && !Exists(lpTo)
        && DosRename(lpFrom, lpTo))
        return TRUE;
    if (!Exists(lpTo) && !MakeDir(lpTo)) {
        OpError("Cannot create the folder '%s'.", lpTo);
        return FALSE;
    }
    lstrcpy(szFrom, lpFrom);
    PathAppend(szFrom, "*.*");
    if (_dos_findfirst(szFrom, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM
                       | _A_SUBDIR | _A_ARCH, &ft) == 0) {
        do {
            if (ft.name[0] == '.')
                continue;
            lstrcpy(szFrom, lpFrom);
            PathAppend(szFrom, ft.name);
            lstrcpy(szTo, lpTo);
            PathAppend(szTo, ft.name);
            if (!CopyTree(szFrom, szTo, bMove)) {
                bOK = FALSE;
                break;
            }
        } while (_dos_findnext(&ft) == 0);
    }
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
    char szTo[MAXPATHLEN], szFrom[MAXPATHLEN];
    LPCSTR lp;
    BOOL bOK = TRUE;

    if (!BeginOp(hwndOwner, bMove ? "Moving" : "Copying", CountList(hList), TRUE))
        return FALSE;
    for (lp = PathListFirst(hList); lp && bOK; lp = PathListNext(lp)) {
        lstrcpyn(szFrom, lp, sizeof(szFrom));
        if (lstrlen(szFrom) <= 3) {
            OpError("Cannot copy or move a whole drive (%s).", szFrom);
            continue;
        }
        lstrcpy(szTo, lpDestDir);
        PathAppend(szTo, PathFileName(szFrom));
        bOK = CopyTree(szFrom, szTo, bMove);
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
    char szMsg[MAXPATHLEN + 80], szName[MAXPATHLEN];
    LPCSTR lp;
    int n = PathListCount(hList);
    BOOL bOK = TRUE;

    if (n == 0)
        return FALSE;
    lp = PathListFirst(hList);
    lstrcpy(szName, lp);
    PathListUnlock(hList);
    if (g_cfg.bConfirmDelete) {
        if (n == 1)
            wsprintf(szMsg, bToBin ? "Are you sure you want to send '%s' to the Bin?"
                                   : "Are you sure you want to delete '%s'?",
                     (LPSTR)PathFileName(szName));
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

/* Moves a file or folder with the progress/confirmation machinery; used
   by the Bin to send and restore items */
BOOL FileOpMoveTo(HWND hwndOwner, LPCSTR lpFrom, LPCSTR lpTo)
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
    bOK = CopyTree(lpFrom, lpTo, TRUE);
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

BOOL FileOpRename(HWND hwndOwner, LPCSTR lpPath, LPCSTR lpNewName)
{
    char szNew[MAXPATHLEN], szMsg[MAXPATHLEN + 80];
    LPCSTR p;

    for (p = lpNewName; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':' || *p == '*' || *p == '?'
            || *p == '"' || *p == '<' || *p == '>' || *p == '|' || *p == ' ') {
            MessageBox(hwndOwner, "A file name cannot contain spaces or any of "
                       "these characters:\n\\ / : * ? \" < > |",
                       NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
    lstrcpyn(szNew, lpPath, sizeof(szNew));
    PathParent(szNew);
    PathAppend(szNew, lpNewName);
    AnsiUpper(szNew);
    if (Exists(szNew)) {
        wsprintf(szMsg, "Cannot rename: '%s' already exists.", (LPSTR)lpNewName);
        MessageBox(hwndOwner, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (!DosRename(lpPath, szNew)) {
        wsprintf(szMsg, "Cannot rename '%s'.", lpPath);
        MessageBox(hwndOwner, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    RefreshParent(lpPath);
    return TRUE;
}

/* Creates "NEWFOLDR" (or NEWFOLD1...) in lpDir; returns its name */
BOOL FileOpNewFolder(HWND hwndOwner, LPCSTR lpDir, LPSTR lpName)
{
    char szPath[MAXPATHLEN];
    int i;

    for (i = 0; i < 100; i++) {
        if (i == 0)
            lstrcpy(lpName, "NEWFOLDR");
        else
            wsprintf(lpName, i < 10 ? "NEWFOLD%d" : "NEWFOL%d", i);
        lstrcpy(szPath, lpDir);
        PathAppend(szPath, lpName);
        if (!Exists(szPath)) {
            if (MakeDir(szPath)) {
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
    if (_dos_findfirst(szPath, _A_NORMAL | _A_RDONLY | _A_HIDDEN | _A_SYSTEM
                       | _A_SUBDIR | _A_ARCH, &ft) != 0)
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
