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
 * util.c - drawing helpers, path helpers and program launching
 */
#include <string.h>
#include <dos.h>
#include "newshell.h"

/* -------------------------------------------------------------------- */
/* Drawing                                                              */
/* -------------------------------------------------------------------- */

void FillSolid(HDC hdc, const RECT FAR *lprc, COLORREF clr)
{
    COLORREF clrOld = SetBkColor(hdc, clr);
    ExtTextOut(hdc, 0, 0, ETO_OPAQUE, lprc, NULL, 0, NULL);
    SetBkColor(hdc, clrOld);
}

static void FillXY(HDC hdc, int l, int t, int r, int b, COLORREF clr)
{
    RECT rc;
    SetRect(&rc, l, t, r, b);
    FillSolid(hdc, &rc, clr);
}

static void Frame(HDC hdc, const RECT FAR *lprc, COLORREF clrTL, COLORREF clrBR)
{
    int l = lprc->left, t = lprc->top, r = lprc->right, b = lprc->bottom;

    FillXY(hdc, l, t, r - 1, t + 1, clrTL);
    FillXY(hdc, l, t, l + 1, b - 1, clrTL);
    FillXY(hdc, l, b - 1, r, b, clrBR);
    FillXY(hdc, r - 1, t, r, b, clrBR);
}

/*
 * Draws a Windows 95 style 3D edge inside the rectangle.
 * bThick selects the two-pixel button edge, otherwise a one-pixel edge
 * (used for status fields and the clock) is drawn.
 */
void Draw3DEdge(HDC hdc, const RECT FAR *lprc, BOOL bSunken, BOOL bThick)
{
    COLORREF clrFace   = GetSysColor(COLOR_BTNFACE);
    COLORREF clrShadow = GetSysColor(COLOR_BTNSHADOW);
    COLORREF clrHilite = GetSysColor(COLOR_BTNHIGHLIGHT);
    COLORREF clrDark   = RGB(0, 0, 0);
    RECT rc = *lprc;

    if (!bThick) {
        if (bSunken)
            Frame(hdc, &rc, clrShadow, clrHilite);
        else
            Frame(hdc, &rc, clrHilite, clrShadow);
        return;
    }
    if (bSunken) {
        Frame(hdc, &rc, clrDark, clrHilite);
        InflateRect(&rc, -1, -1);
        Frame(hdc, &rc, clrShadow, clrFace);
    } else {
        Frame(hdc, &rc, clrFace, clrDark);
        InflateRect(&rc, -1, -1);
        Frame(hdc, &rc, clrHilite, clrShadow);
    }
}

/* Small image strip with a magenta colour key, converted on first use */
static HBITMAP s_hbmSmall;
static HBITMAP s_hbmSmallMask;

static void LoadSmallImages(void)
{
    HDC hdcScreen, hdcColor, hdcMask;
    HBITMAP hbmOld1, hbmOld2;
    BITMAP bm;

    s_hbmSmall = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_SMALL));
    if (!s_hbmSmall)
        return;
    GetObject(s_hbmSmall, sizeof(bm), &bm);
    s_hbmSmallMask = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);

    hdcScreen = GetDC(NULL);
    hdcColor = CreateCompatibleDC(hdcScreen);
    hdcMask = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);
    hbmOld1 = SelectObject(hdcColor, s_hbmSmall);
    hbmOld2 = SelectObject(hdcMask, s_hbmSmallMask);

    /* mask: 1 where the colour key is, 0 elsewhere */
    SetBkColor(hdcColor, RGB(255, 0, 255));
    BitBlt(hdcMask, 0, 0, bm.bmWidth, bm.bmHeight, hdcColor, 0, 0, SRCCOPY);

    /* turn the key colour black in the colour image (dest AND NOT mask) */
    SetBkColor(hdcColor, RGB(255, 255, 255));
    SetTextColor(hdcColor, RGB(0, 0, 0));
    BitBlt(hdcColor, 0, 0, bm.bmWidth, bm.bmHeight, hdcMask, 0, 0, 0x00220326L);

    SelectObject(hdcColor, hbmOld1);
    SelectObject(hdcMask, hbmOld2);
    DeleteDC(hdcColor);
    DeleteDC(hdcMask);
}

void DrawSmallImage(HDC hdc, int x, int y, int nIndex)
{
    HDC hdcMem;
    HBITMAP hbmOld;
    COLORREF clrBk, clrText;

    if (!s_hbmSmall)
        LoadSmallImages();
    if (!s_hbmSmall)
        return;

    hdcMem = CreateCompatibleDC(hdc);
    clrBk = SetBkColor(hdc, RGB(255, 255, 255));
    clrText = SetTextColor(hdc, RGB(0, 0, 0));

    hbmOld = SelectObject(hdcMem, s_hbmSmallMask);
    BitBlt(hdc, x, y, 16, 16, hdcMem, nIndex * 16, 0, SRCAND);
    SelectObject(hdcMem, s_hbmSmall);
    BitBlt(hdc, x, y, 16, 16, hdcMem, nIndex * 16, 0, SRCPAINT);
    SelectObject(hdcMem, hbmOld);

    SetBkColor(hdc, clrBk);
    SetTextColor(hdc, clrText);
    DeleteDC(hdcMem);
}

/* Windows 3.1 can only draw icons at 32x32; shrink one via a memory DC. */
void DrawIconSmall(HDC hdc, int x, int y, HICON hIcon, COLORREF clrBk)
{
    static HBITMAP hbmTmp;
    HDC hdcMem;
    HBITMAP hbmOld;
    RECT rc;
    int nOldMode;

    if (!hIcon)
        return;
    if (!hbmTmp)
        hbmTmp = CreateCompatibleBitmap(hdc, 32, 32);

    hdcMem = CreateCompatibleDC(hdc);
    hbmOld = SelectObject(hdcMem, hbmTmp);
    SetRect(&rc, 0, 0, 32, 32);
    FillSolid(hdcMem, &rc, clrBk);
    DrawIcon(hdcMem, 0, 0, hIcon);
    nOldMode = SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(hdc, x, y, 16, 16, hdcMem, 0, 0, 32, 32, SRCCOPY);
    SetStretchBltMode(hdc, nOldMode);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
}

HFONT CreateUIFont(int nPoints, BOOL bBold)
{
    LOGFONT lf;
    HDC hdc = GetDC(NULL);

    _fmemset(&lf, 0, sizeof(lf));
    lf.lfHeight = -MulDiv(nPoints, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight = bBold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, "MS Sans Serif");
    ReleaseDC(NULL, hdc);
    return CreateFontIndirect(&lf);
}

int TextWidth(HDC hdc, LPCSTR lpsz)
{
    return LOWORD(GetTextExtent(hdc, lpsz, lstrlen(lpsz)));
}

/* Copies lpSrc to lpDst, shortened with "..." to fit cxMax pixels */
void FitText(HDC hdc, LPCSTR lpSrc, LPSTR lpDst, int cbDst, int cxMax)
{
    int n;

    lstrcpyn(lpDst, lpSrc, cbDst);
    if (TextWidth(hdc, lpDst) <= cxMax)
        return;
    for (n = lstrlen(lpDst); n > 0; n--) {
        lpDst[n] = '\0';
        lstrcpy(lpDst + n, "...");
        if (TextWidth(hdc, lpDst) <= cxMax)
            return;
        lpDst[n] = '\0';
    }
    lstrcpyn(lpDst, "...", cbDst);
}

/* -------------------------------------------------------------------- */
/* Paths                                                                */
/* -------------------------------------------------------------------- */

LPSTR PathFileName(LPSTR lpPath)
{
    LPSTR p = lpPath, lpName = lpPath;

    for (; *p; p++)
        if (*p == '\\' || *p == ':')
            lpName = p + 1;
    return lpName;
}

/* Returns a pointer to the extension (without the dot), or to "" */
LPSTR PathExtension(LPSTR lpPath)
{
    LPSTR lpName = PathFileName(lpPath);
    LPSTR lpDot = NULL;

    for (; *lpName; lpName++)
        if (*lpName == '.')
            lpDot = lpName;
    return lpDot ? lpDot + 1 : lpName;
}

void PathAppend(LPSTR lpPath, LPCSTR lpName)
{
    int n = lstrlen(lpPath);

    if (n > 0 && lpPath[n - 1] != '\\')
        lstrcat(lpPath, "\\");
    lstrcat(lpPath, lpName);
}

/* Strips the last component. Returns FALSE if already at a root. */
BOOL PathParent(LPSTR lpPath)
{
    LPSTR lpName;

    PathNormalize(lpPath);
    if (lstrlen(lpPath) <= 3)
        return FALSE;
    lpName = PathFileName(lpPath);
    if (lpName > lpPath)
        lpName[-1] = '\0';
    if (lstrlen(lpPath) == 2 && lpPath[1] == ':')
        lstrcat(lpPath, "\\");
    return TRUE;
}

/* Upper-cases and removes a trailing backslash (except from "X:\") */
void PathNormalize(LPSTR lpPath)
{
    int n;

    AnsiUpper(lpPath);
    n = lstrlen(lpPath);
    if (n == 2 && lpPath[1] == ':')
        lstrcat(lpPath, "\\");
    else if (n > 3 && lpPath[n - 1] == '\\')
        lpPath[n - 1] = '\0';
}

BOOL PathIsDirectory(LPCSTR lpPath)
{
    char szPath[MAXPATHLEN];
    unsigned attr;

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    PathNormalize(szPath);
    if (lstrlen(szPath) == 3 && szPath[1] == ':')
        return GetDriveType(szPath[0] - 'A') != 0;
    if (_dos_getfileattr(szPath, &attr) != 0)
        return FALSE;
    return (attr & _A_SUBDIR) != 0;
}

BOOL IsShiftDown(void)
{
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

/* -------------------------------------------------------------------- */
/* Launching                                                            */
/* -------------------------------------------------------------------- */

void ShowLaunchError(HWND hwnd, UINT uErr, LPCSTR lpWhat)
{
    char szMsg[256];
    LPCSTR lpReason;

    switch (uErr) {
    case 0:
    case 8:  lpReason = "There is not enough memory to run this application."; break;
    case 2:  lpReason = "The file could not be found."; break;
    case 3:  lpReason = "The path could not be found."; break;
    case 5:  lpReason = "Access was denied."; break;
    case 10: lpReason = "This application requires a newer version of Windows."; break;
    case 11: lpReason = "This is not a valid Windows application."; break;
    case 12:
    case 13: lpReason = "This application was designed for a different operating system."; break;
    case 14: lpReason = "The type of this file is unknown."; break;
    case 15: lpReason = "This application was designed for real mode."; break;
    case 16: lpReason = "A second instance of this application cannot be started."; break;
    case 21: lpReason = "This application requires Windows NT or Win32s."; break;
    case 31: lpReason = "There is no application associated with this file."; break;
    default: lpReason = "The application could not be started."; break;
    }
    wsprintf(szMsg, "Cannot open %s.\n\n%s (error %u)", lpWhat, lpReason, uErr);
    MessageBox(hwnd, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
}

/*
 * Runs a command line ("program [parameters]"). Folders open in a folder
 * window; documents open via their association.
 */
BOOL LaunchCommand(HWND hwnd, LPCSTR lpCmdLine, LPCSTR lpDir, int nShow)
{
    char szFile[MAXPATHLEN];
    LPCSTR lpParams;
    HINSTANCE hInst;
    int i = 0;

    while (*lpCmdLine == ' ')
        lpCmdLine++;
    while (*lpCmdLine && *lpCmdLine != ' ' && i < sizeof(szFile) - 1)
        szFile[i++] = *lpCmdLine++;
    szFile[i] = '\0';
    while (*lpCmdLine == ' ')
        lpCmdLine++;
    lpParams = lpCmdLine;

    if (szFile[0] == '\0')
        return FALSE;

    if (*lpParams == '\0' && PathIsDirectory(szFile)) {
        PathNormalize(szFile);
        return FolderOpen(szFile) != NULL;
    }

    hInst = ShellExecute(hwnd, NULL, szFile, *lpParams ? lpParams : NULL,
                         (lpDir && *lpDir) ? lpDir : NULL, nShow);
    if ((UINT)hInst <= 32) {
        ShowLaunchError(hwnd, (UINT)hInst, szFile);
        return FALSE;
    }
    return TRUE;
}

/* Opens a file or folder by full path, using its directory as working dir */
BOOL OpenPath(HWND hwnd, LPCSTR lpPath)
{
    char szDir[MAXPATHLEN];
    HINSTANCE hInst;

    if (PathIsDirectory(lpPath))
        return FolderOpen(lpPath) != NULL;

    lstrcpyn(szDir, lpPath, sizeof(szDir));
    PathParent(szDir);
    hInst = ShellExecute(hwnd, NULL, lpPath, NULL, szDir, SW_SHOWNORMAL);
    if ((UINT)hInst <= 32) {
        ShowLaunchError(hwnd, (UINT)hInst, lpPath);
        return FALSE;
    }
    return TRUE;
}
