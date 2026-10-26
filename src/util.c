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

/*
 * The focus rectangle of a list item. DrawFocusRect inverts, which turns
 * into a yellow frame on a highlighted (dark blue) item; there the dots
 * are drawn in the highlight text colour instead.
 */
void DrawItemFocus(HDC hdc, const RECT FAR *lprc, BOOL bSel)
{
    COLORREF clr;
    int x, y;

    if (!bSel) {
        DrawFocusRect(hdc, lprc);
        return;
    }
    clr = GetSysColor(COLOR_HIGHLIGHTTEXT);
    for (x = lprc->left; x < lprc->right; x += 2) {
        SetPixel(hdc, x, lprc->top, clr);
        SetPixel(hdc, x, lprc->bottom - 1, clr);
    }
    for (y = lprc->top; y < lprc->bottom; y += 2) {
        SetPixel(hdc, lprc->left, y, clr);
        SetPixel(hdc, lprc->right - 1, y, clr);
    }
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

/*
 * Win95 "client edge": a 2-pixel sunken border around a pane. The pane
 * window is placed inside it and the parent paints the edge.
 */
void MovePane(HWND hwndPane, int x, int y, int cx, int cy)
{
    MoveWindow(hwndPane, x + CLIENT_EDGE, y + CLIENT_EDGE,
               cx - 2 * CLIENT_EDGE, cy - 2 * CLIENT_EDGE, TRUE);
}

void DrawPaneEdge(HDC hdc, int x, int y, int cx, int cy)
{
    RECT rc;

    SetRect(&rc, x, y, x + cx, y + cy);
    Draw3DEdge(hdc, &rc, TRUE, TRUE);
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

/* A small image as disabled: embossed in the highlight and shadow colours */
void DrawSmallImageDisabled(HDC hdc, int x, int y, int nIndex)
{
    HDC hdcMem;
    HBITMAP hbmOld;
    HBRUSH hbrOld;
    COLORREF clrBk, clrText;
    int i;

    if (!s_hbmSmall)
        LoadSmallImages();
    if (!s_hbmSmall)
        return;
    hdcMem = CreateCompatibleDC(hdc);
    hbmOld = SelectObject(hdcMem, s_hbmSmallMask);
    clrBk = SetBkColor(hdc, RGB(255, 255, 255));
    clrText = SetTextColor(hdc, RGB(0, 0, 0));
    /* the brush where the mask is 0 (the image), the destination elsewhere */
    for (i = 0; i < 2; i++) {
        HBRUSH hbr = CreateSolidBrush(GetSysColor(i ? COLOR_BTNSHADOW : COLOR_BTNHIGHLIGHT));

        hbrOld = SelectObject(hdc, hbr);
        BitBlt(hdc, x + 1 - i, y + 1 - i, 16, 16, hdcMem, nIndex * 16, 0, 0x00B8074AL);
        SelectObject(hdc, hbrOld);
        DeleteObject(hbr);
    }
    SetBkColor(hdc, clrBk);
    SetTextColor(hdc, clrText);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
}

/*
 * Shades an icon drawn at x, y as selected (Windows 95 style): every
 * other pixel of the icon, not of the background clrBk, becomes the
 * highlight colour.
 */
void ShadeArea(HDC hdc, int x, int y, int cx, int cy, COLORREF clrBk)
{
    static WORD wChecker[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
    HDC hdcMask;
    HBITMAP hbmMask, hbmPat, hbmOld;
    HBRUSH hbrPat, hbrHi, hbrOld;
    COLORREF clrBkOld, clrTextOld;

    hdcMask = CreateCompatibleDC(hdc);
    hbmMask = CreateBitmap(cx, cy, 1, 1, NULL);
    hbmPat = CreateBitmap(8, 8, 1, 1, wChecker);
    if (!hdcMask || !hbmMask || !hbmPat) {
        if (hdcMask)
            DeleteDC(hdcMask);
        if (hbmMask)
            DeleteObject(hbmMask);
        if (hbmPat)
            DeleteObject(hbmPat);
        return;
    }
    hbmOld = SelectObject(hdcMask, hbmMask);
    /* 1 = background (clrBk), 0 = icon */
    clrBkOld = SetBkColor(hdc, clrBk);
    BitBlt(hdcMask, 0, 0, cx, cy, hdc, x, y, SRCCOPY);
    /* leave out every other pixel: OR in the checker */
    hbrPat = CreatePatternBrush(hbmPat);
    hbrOld = SelectObject(hdcMask, hbrPat);
    SetBrushOrg(hdcMask, 0, 0);
    PatBlt(hdcMask, 0, 0, cx, cy, 0x00FA0089L);        /* DPo */
    SelectObject(hdcMask, hbrOld);
    DeleteObject(hbrPat);
    /* the highlight where the mask is 0 */
    SetBkColor(hdc, RGB(255, 255, 255));
    clrTextOld = SetTextColor(hdc, RGB(0, 0, 0));
    hbrHi = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    hbrOld = SelectObject(hdc, hbrHi);
    BitBlt(hdc, x, y, cx, cy, hdcMask, 0, 0, 0x00B8074AL);  /* PSDPxax */
    SelectObject(hdc, hbrOld);
    DeleteObject(hbrHi);
    SetBkColor(hdc, clrBkOld);
    SetTextColor(hdc, clrTextOld);
    SelectObject(hdcMask, hbmOld);
    DeleteDC(hdcMask);
    DeleteObject(hbmMask);
    DeleteObject(hbmPat);
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
/* Hints                                                                */
/* -------------------------------------------------------------------- */

/* One small yellow popup with text, used by folder views and the taskbar */
static HWND s_hwndHint;
static char s_szHint[256];

LRESULT CALLBACK NSEXPORT HintWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam)
{
    PAINTSTRUCT ps;
    RECT rc;
    HFONT hfOld;

    switch (msg) {
    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        FillSolid(ps.hdc, &rc, RGB(255, 255, 225));
        hfOld = SelectObject(ps.hdc, g_hfontUI);
        SetBkMode(ps.hdc, TRANSPARENT);
        SetTextColor(ps.hdc, RGB(0, 0, 0));
        InflateRect(&rc, -3, -1);
        DrawText(ps.hdc, s_szHint, -1, &rc, DT_LEFT | DT_NOPREFIX);
        SelectObject(ps.hdc, hfOld);
        EndPaint(hwnd, &ps);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL HintRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.style = CS_SAVEBITS;
    wc.lpfnWndProc = HintWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLS_HINT;
    return RegisterClass(&wc);
}

/* Shows lpText (lines split by \n) with its top left near x, y on screen */
void HintShow(LPCSTR lpText, int x, int y)
{
    RECT rc;
    HDC hdc;
    HFONT hfOld;
    int cxScreen = GetSystemMetrics(SM_CXSCREEN);
    int cyScreen = GetSystemMetrics(SM_CYSCREEN);

    if (!s_hwndHint)
        s_hwndHint = CreateWindow(CLS_HINT, "", WS_POPUP | WS_BORDER, 0, 0, 0, 0,
                                  NULL, NULL, g_hInst, NULL);
    if (!s_hwndHint)
        return;
    lstrcpyn(s_szHint, lpText, sizeof(s_szHint));
    hdc = GetDC(NULL);
    hfOld = SelectObject(hdc, g_hfontUI);
    SetRect(&rc, 0, 0, 300, 0);
    DrawText(hdc, s_szHint, -1, &rc, DT_CALCRECT | DT_LEFT | DT_NOPREFIX);
    SelectObject(hdc, hfOld);
    ReleaseDC(NULL, hdc);
    if (x + rc.right + 8 > cxScreen)
        x = cxScreen - rc.right - 8;
    if (x < 0)
        x = 0;
    if (y + rc.bottom + 4 > cyScreen)
        y = cyScreen - rc.bottom - 4;
    SetWindowPos(s_hwndHint, HWND_TOPMOST, x, y, rc.right + 8, rc.bottom + 4,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(s_hwndHint, NULL, FALSE);
    UpdateWindow(s_hwndHint);
}

void HintHide(void)
{
    if (s_hwndHint && IsWindowVisible(s_hwndHint))
        ShowWindow(s_hwndHint, SW_HIDE);
}

BOOL HintVisible(void)
{
    return s_hwndHint && IsWindowVisible(s_hwndHint);
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

/* Case-insensitive wildcard match supporting * and ? */
BOOL WildMatch(LPCSTR p, LPCSTR s)
{
    while (*p) {
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (!*p)
                return TRUE;
            for (; *s; s++)
                if (WildMatch(p, s))
                    return TRUE;
            return FALSE;
        }
        if (!*s)
            return FALSE;
        if (*p != '?' && (*p & ~0x20) != (*s & ~0x20)
            && !(*p == *s))
            return FALSE;
        p++;
        s++;
    }
    return *s == '\0';
}

/*
 * Whether a file name matches one of the patterns of a list separated by
 * ';', ',' or blanks ("*.TXT;*.DOC"). "*.*" and "X.*" also match names
 * without an extension, as in DOS.
 */
BOOL MatchSpecList(LPCSTR lpName, LPCSTR lpList)
{
    char szPat[32], szName[16];
    int n;

    lstrcpyn(szName, lpName, sizeof(szName) - 1);
    if (!_fstrchr(szName, '.'))
        lstrcat(szName, ".");
    while (*lpList) {
        while (*lpList == ';' || *lpList == ',' || *lpList == ' ')
            lpList++;
        for (n = 0; *lpList && *lpList != ';' && *lpList != ',' && *lpList != ' '; lpList++)
            if (n < sizeof(szPat) - 1)
                szPat[n++] = *lpList;
        szPat[n] = '\0';
        if (n && (WildMatch(szPat, lpName) || WildMatch(szPat, szName)))
            return TRUE;
    }
    return FALSE;
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
    if ((UINT)hInst == 31)              /* no association: pick a program */
        return OpenWithDialog(hwnd, lpPath);
    if ((UINT)hInst <= 32) {
        ShowLaunchError(hwnd, (UINT)hInst, lpPath);
        return FALSE;
    }
    return TRUE;
}

/* -------------------------------------------------------------------- */
/* Retheme16 theme engine                                               */
/* -------------------------------------------------------------------- */

/*
 * The Retheme16 engine (RETHEME.DLL, loaded by RETHEME.EXE while it runs)
 * redraws system menus, captions and dialog controls in the Windows 95
 * style with its own fonts. Our owner-drawn menus follow whatever the
 * system menus currently look like.
 */
BOOL ThemeEngineActive(void)
{
    return GetModuleHandle("RETHEME") != NULL;
}

/* Reads "face,points,weight[,italic]" for lpName from a theme directory,
   following its Base= chain */
static BOOL ReadThemeFont(LPCSTR lpThemeDir, LPCSTR lpName, LOGFONT FAR *lplf,
                          int nDepth)
{
    char szIni[MAXPATHLEN], szValue[64], szBase[16];
    LPSTR p, q;
    int nPoints = 8, nWeight = FW_NORMAL;
    HDC hdc;

    lstrcpyn(szIni, lpThemeDir, sizeof(szIni) - 12);
    PathAppend(szIni, "THEME.INI");
    if (GetPrivateProfileString("Fonts", lpName, "", szValue, sizeof(szValue),
                                szIni) == 0) {
        if (nDepth >= 4 || GetPrivateProfileString("Theme", "Base", "", szBase,
                                                   sizeof(szBase), szIni) == 0)
            return FALSE;
        lstrcpyn(szIni, lpThemeDir, sizeof(szIni) - 16);
        PathParent(szIni);
        PathAppend(szIni, szBase);
        return ReadThemeFont(szIni, lpName, lplf, nDepth + 1);
    }
    for (p = szValue; *p && *p != ','; p++)
        ;
    if (*p == ',') {
        *p++ = '\0';
        nPoints = 0;
        for (q = p; *q >= '0' && *q <= '9'; q++)
            nPoints = nPoints * 10 + (*q - '0');
        if (*q == ',') {
            nWeight = 0;
            for (q++; *q >= '0' && *q <= '9'; q++)
                nWeight = nWeight * 10 + (*q - '0');
            if (*q == ',' && q[1] == '1')
                lplf->lfItalic = TRUE;
        }
    }
    hdc = GetDC(NULL);
    lplf->lfHeight = -MulDiv(nPoints ? nPoints : 8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    lplf->lfWeight = nWeight;
    lplf->lfCharSet = ANSI_CHARSET;
    lplf->lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    lstrcpyn(lplf->lfFaceName, szValue, sizeof(lplf->lfFaceName));
    return TRUE;
}

/*
 * The font menus are drawn with: the theme's Menu font while the engine
 * runs, otherwise the System font Windows 3.1 uses for menus. The handle
 * stays valid until the engine is started or stopped.
 */
HFONT GetMenuFont(void)
{
    static HFONT s_hfTheme;
    static BOOL  s_bThemed;
    char szHome[MAXPATHLEN], szIni[MAXPATHLEN], szTheme[MAXPATHLEN];
    HMODULE hmod = GetModuleHandle("RETHEME");
    LOGFONT lf;

    if (!hmod) {
        if (s_hfTheme) {
            DeleteObject(s_hfTheme);
            s_hfTheme = NULL;
        }
        s_bThemed = FALSE;
        return (HFONT)GetStockObject(SYSTEM_FONT);
    }
    if (s_bThemed && s_hfTheme)
        return s_hfTheme;

    GetModuleFileName((HINSTANCE)hmod, szHome, sizeof(szHome));
    *PathFileName(szHome) = '\0';
    lstrcpy(szIni, szHome);
    lstrcat(szIni, "RETHEME.INI");
    lstrcpy(szTheme, szHome);
    lstrcat(szTheme, "THEMES\\WIN95");
    GetPrivateProfileString("Engine", "Theme", szTheme, szTheme, sizeof(szTheme), szIni);

    _fmemset(&lf, 0, sizeof(lf));
    if (!ReadThemeFont(szTheme, "Menu", &lf, 0)) {
        _fmemset(&lf, 0, sizeof(lf));
        lstrcpy(lf.lfFaceName, "MS Sans Serif");
        lf.lfHeight = -MulDiv(8, 96, 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = ANSI_CHARSET;
    }
    s_hfTheme = CreateFontIndirect(&lf);
    s_bThemed = TRUE;
    return s_hfTheme ? s_hfTheme : g_hfontUI;
}

/* The font Windows uses for icon titles (Control Panel > Desktop) */
HFONT CreateIconTitleFont(void)
{
    LOGFONT lf;

    _fmemset(&lf, 0, sizeof(lf));
    if (!SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0))
        return CreateUIFont(8, FALSE);
    return CreateFontIndirect(&lf);
}
