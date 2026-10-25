/*
 * folder.c - folder windows (large icon view of a directory) and the
 * Computer window (list of drives)
 */
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "newshell.h"

#define MAX_FOLDERS     48
#define CELL_CX         80
#define CELL_CY         58
#define MARGIN          4

/* item kinds */
#define FK_FILE         0
#define FK_DIR          1
#define FK_DRIVE        2

typedef struct tagFITEM {
    char  szName[24];       /* file name, or "C:" style label for drives */
    BYTE  bKind;
    BYTE  bAttr;
    DWORD dwSize;
    HICON hIcon;
} FITEM, FAR *LPFITEM;

typedef struct tagFOLDER {
    char    szPath[MAXPATHLEN];     /* "" = Computer */
    LPFITEM lpItems;
    int     nItems;
    int     nCap;
    int     nSel;
    int     nTop;                   /* first visible row */
    int     nCols;
    int     nVisRows;
    int     cyStatus;
    DWORD   dwTotal;
} FOLDER, FAR *LPFOLDER;

static HWND s_hwndFolders[MAX_FOLDERS];
static int  s_nFolders;

static LPFOLDER GetFolder(HWND hwnd)
{
    return (LPFOLDER)GetWindowLong(hwnd, 0);
}

/* -------------------------------------------------------------------- */
/* Reading                                                              */
/* -------------------------------------------------------------------- */

static LPFITEM AddItem(LPFOLDER f)
{
    if (f->nItems >= f->nCap) {
        int nCap = f->nCap ? f->nCap * 2 : 64;
        LPFITEM lpNew = (LPFITEM)realloc(f->lpItems, nCap * sizeof(FITEM));
        if (!lpNew)
            return NULL;
        f->lpItems = lpNew;
        f->nCap = nCap;
    }
    _fmemset(&f->lpItems[f->nItems], 0, sizeof(FITEM));
    return &f->lpItems[f->nItems++];
}

static int CompareItems(const void *a, const void *b)
{
    LPFITEM p = (LPFITEM)a, q = (LPFITEM)b;

    if (p->bKind != q->bKind)
        return (int)q->bKind - (int)p->bKind;   /* drives, dirs, files */
    return lstrcmpi(p->szName, q->szName);
}

static BOOL IsCDROM(int nDrive)
{
    union REGS r;

    r.x.ax = 0x150B;
    r.x.bx = 0;
    r.x.cx = nDrive;
    int86(0x2F, &r, &r);
    return r.x.bx == 0xADAD && r.x.ax != 0;
}

static void ReadDrives(LPFOLDER f)
{
    int d, nType, idi;
    LPFITEM p;

    for (d = 0; d < 26; d++) {
        nType = GetDriveType(d);
        if (nType == 0)
            continue;
        switch (nType) {
        case DRIVE_REMOVABLE: idi = IDI_FLOPPY; break;
        case DRIVE_FIXED:     idi = IDI_HDD; break;
        default:              idi = IsCDROM(d) ? IDI_CDROM : IDI_NETDRV; break;
        }
        p = AddItem(f);
        if (!p)
            break;
        wsprintf(p->szName, "%c:", 'A' + d);
        p->bKind = FK_DRIVE;
        p->hIcon = IconStock(idi);
    }
}

static void ReadDirectory(LPFOLDER f)
{
    struct find_t ft;
    char szPattern[MAXPATHLEN + 4];
    char szFull[MAXPATHLEN + 14];
    unsigned uAttr = _A_NORMAL | _A_RDONLY | _A_SUBDIR | _A_ARCH;
    LPFITEM p;

    if (g_cfg.bShowHidden)
        uAttr |= _A_HIDDEN | _A_SYSTEM;

    lstrcpy(szPattern, f->szPath);
    PathAppend(szPattern, "*.*");
    if (_dos_findfirst(szPattern, uAttr, &ft) != 0)
        return;
    do {
        if (ft.name[0] == '.')
            continue;
        if (!g_cfg.bShowHidden && (ft.attrib & (_A_HIDDEN | _A_SYSTEM)))
            continue;
        p = AddItem(f);
        if (!p)
            break;
        lstrcpyn(p->szName, ft.name, sizeof(p->szName));
        p->bAttr = ft.attrib;
        p->dwSize = ft.size;
        p->bKind = (ft.attrib & _A_SUBDIR) ? FK_DIR : FK_FILE;
        if (p->bKind == FK_FILE)
            f->dwTotal += ft.size;
    } while (_dos_findnext(&ft) == 0);

    /* icons are looked up after reading so FindExecutable cannot upset the
       DOS find state */
    for (p = f->lpItems; p < f->lpItems + f->nItems; p++) {
        lstrcpy(szFull, f->szPath);
        PathAppend(szFull, p->szName);
        p->hIcon = IconForFile(szFull, p->bKind == FK_DIR);
    }
}

static void ReadFolder(HWND hwnd, LPFOLDER f)
{
    HCURSOR hcurOld = SetCursor(LoadCursor(NULL, IDC_WAIT));

    f->nItems = 0;
    f->dwTotal = 0;
    f->nSel = -1;
    f->nTop = 0;
    if (f->szPath[0] == '\0')
        ReadDrives(f);
    else
        ReadDirectory(f);
    if (f->nItems > 1)
        qsort(f->lpItems, f->nItems, sizeof(FITEM), CompareItems);
    SetCursor(hcurOld);
    (void)hwnd;
}

/* -------------------------------------------------------------------- */
/* Layout and painting                                                  */
/* -------------------------------------------------------------------- */

static void UpdateLayout(HWND hwnd, LPFOLDER f)
{
    RECT rc;
    int nRows, nMax;

    GetClientRect(hwnd, &rc);
    f->nCols = (rc.right - 2 * MARGIN) / CELL_CX;
    if (f->nCols < 1)
        f->nCols = 1;
    f->nVisRows = (rc.bottom - f->cyStatus - MARGIN) / CELL_CY;
    if (f->nVisRows < 1)
        f->nVisRows = 1;
    nRows = (f->nItems + f->nCols - 1) / f->nCols;
    nMax = nRows - f->nVisRows;
    if (nMax < 0)
        nMax = 0;
    if (f->nTop > nMax)
        f->nTop = nMax;
    SetScrollRange(hwnd, SB_VERT, 0, nMax, FALSE);
    SetScrollPos(hwnd, SB_VERT, f->nTop, TRUE);
}

static void ItemRect(LPFOLDER f, int i, RECT FAR *lprc)
{
    int x = MARGIN + (i % f->nCols) * CELL_CX;
    int y = MARGIN + (i / f->nCols - f->nTop) * CELL_CY;

    SetRect(lprc, x, y, x + CELL_CX, y + CELL_CY);
}

static void PaintItem(HWND hwnd, HDC hdc, LPFOLDER f, int i)
{
    RECT rc, rcText;
    LPFITEM p = &f->lpItems[i];
    BOOL bSel = (i == f->nSel);
    char szLabel[sizeof(p->szName) + 4];
    int cx;

    ItemRect(f, i, &rc);
    DrawIcon(hdc, rc.left + (CELL_CX - 32) / 2, rc.top + 2, p->hIcon);

    FitText(hdc, p->szName, szLabel, sizeof(szLabel), CELL_CX - 6);
    cx = TextWidth(hdc, szLabel);
    rcText.left = rc.left + (CELL_CX - cx) / 2 - 2;
    rcText.right = rcText.left + cx + 4;
    rcText.top = rc.top + 37;
    rcText.bottom = rcText.top + HIWORD(GetTextExtent(hdc, "Ay", 2)) + 2;

    if (bSel) {
        FillSolid(hdc, &rcText, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    } else
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    DrawText(hdc, szLabel, -1, &rcText,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (bSel && GetFocus() == hwnd)
        DrawFocusRect(hdc, &rcText);
}

static void FormatSize(DWORD dw, LPSTR lpBuf)
{
    if (dw < 1024L)
        wsprintf(lpBuf, "%lu bytes", dw);
    else
        wsprintf(lpBuf, "%luKB", (dw + 1023L) / 1024L);
}

static void PaintStatus(HWND hwnd, HDC hdc, LPFOLDER f)
{
    RECT rc, rcField;
    char szText[96], szSize[24];

    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - f->cyStatus;
    FillSolid(hdc, &rc, GetSysColor(COLOR_BTNFACE));
    SetRect(&rcField, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 1);
    Draw3DEdge(hdc, &rcField, TRUE, FALSE);

    if (f->nSel >= 0 && f->lpItems[f->nSel].bKind == FK_FILE) {
        FormatSize(f->lpItems[f->nSel].dwSize, szSize);
        wsprintf(szText, "%s  %s", (LPSTR)f->lpItems[f->nSel].szName,
                 (LPSTR)szSize);
    } else if (f->szPath[0] == '\0') {
        wsprintf(szText, "%d drive(s)", f->nItems);
    } else {
        FormatSize(f->dwTotal, szSize);
        wsprintf(szText, "%d object(s)  %s", f->nItems, (LPSTR)szSize);
    }
    InflateRect(&rcField, -4, 0);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    DrawText(hdc, szText, -1, &rcField,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void PaintFolder(HWND hwnd, HDC hdc, LPFOLDER f, const RECT FAR *lprcPaint)
{
    HFONT hfOld = SelectObject(hdc, g_hfontUI);
    RECT rc, rcClip, rcClient;
    HRGN hrgn;
    int i, iFirst, iLast;

    GetClientRect(hwnd, &rcClient);
    SetBkMode(hdc, TRANSPARENT);

    /* clip item drawing so labels never spill over the status bar */
    hrgn = CreateRectRgn(0, 0, rcClient.right, rcClient.bottom - f->cyStatus);
    SelectClipRgn(hdc, hrgn);
    iFirst = f->nTop * f->nCols;
    iLast = iFirst + (f->nVisRows + 1) * f->nCols;
    if (iLast > f->nItems)
        iLast = f->nItems;
    for (i = iFirst; i < iLast; i++) {
        ItemRect(f, i, &rc);
        if (IntersectRect(&rcClip, &rc, lprcPaint))
            PaintItem(hwnd, hdc, f, i);
    }
    SelectClipRgn(hdc, NULL);
    DeleteObject(hrgn);

    PaintStatus(hwnd, hdc, f);
    SelectObject(hdc, hfOld);
}

static void InvalidateItem(HWND hwnd, LPFOLDER f, int i)
{
    RECT rc;

    if (i < 0 || i >= f->nItems)
        return;
    ItemRect(f, i, &rc);
    InvalidateRect(hwnd, &rc, TRUE);
}

static void InvalidateStatus(HWND hwnd, LPFOLDER f)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - f->cyStatus;
    InvalidateRect(hwnd, &rc, FALSE);
}

static void ScrollTo(HWND hwnd, LPFOLDER f, int nTop)
{
    int nMin, nMax;

    GetScrollRange(hwnd, SB_VERT, &nMin, &nMax);
    if (nTop > nMax)
        nTop = nMax;
    if (nTop < 0)
        nTop = 0;
    if (nTop == f->nTop)
        return;
    f->nTop = nTop;
    SetScrollPos(hwnd, SB_VERT, nTop, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void Select(HWND hwnd, LPFOLDER f, int i)
{
    int nRow;

    if (i >= f->nItems)
        i = f->nItems - 1;
    if (i == f->nSel)
        return;
    InvalidateItem(hwnd, f, f->nSel);
    f->nSel = i;
    InvalidateItem(hwnd, f, i);
    InvalidateStatus(hwnd, f);
    if (i < 0)
        return;
    nRow = i / f->nCols;
    if (nRow < f->nTop)
        ScrollTo(hwnd, f, nRow);
    else if (nRow >= f->nTop + f->nVisRows)
        ScrollTo(hwnd, f, nRow - f->nVisRows + 1);
}

static int HitTest(LPFOLDER f, int x, int y)
{
    int col, row, i;
    RECT rc;
    POINT pt;

    if (x < MARGIN || y < MARGIN)
        return -1;
    col = (x - MARGIN) / CELL_CX;
    row = (y - MARGIN) / CELL_CY + f->nTop;
    if (col >= f->nCols)
        return -1;
    i = row * f->nCols + col;
    if (i >= f->nItems)
        return -1;
    ItemRect(f, i, &rc);
    InflateRect(&rc, -6, 0);
    pt.x = x;
    pt.y = y;
    return PtInRect(&rc, pt) ? i : -1;
}

/* -------------------------------------------------------------------- */
/* Actions                                                              */
/* -------------------------------------------------------------------- */

static void SetTitle(HWND hwnd, LPFOLDER f)
{
    SetWindowText(hwnd, f->szPath[0] ? f->szPath : "Computer");
}

static void Navigate(HWND hwnd, LPFOLDER f, LPCSTR lpPath)
{
    lstrcpyn(f->szPath, lpPath, sizeof(f->szPath));
    ReadFolder(hwnd, f);
    SetTitle(hwnd, f);
    UpdateLayout(hwnd, f);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void OpenFolderPath(HWND hwnd, LPFOLDER f, LPCSTR lpPath)
{
    if (g_cfg.bFolderNewWindow)
        FolderOpen(lpPath);
    else
        Navigate(hwnd, f, lpPath);
}

static BOOL DriveReady(int nDrive)
{
    struct find_t ft;
    char szPattern[8];
    UINT uOldMode = SetErrorMode(1);    /* SEM_FAILCRITICALERRORS */
    unsigned rc;

    wsprintf(szPattern, "%c:\\*.*", 'A' + nDrive);
    rc = _dos_findfirst(szPattern, _A_NORMAL | _A_SUBDIR | _A_VOLID, &ft);
    SetErrorMode(uOldMode);
    return rc == 0 || rc == 0x12;       /* 0x12 = no more files (empty) */
}

static void OpenItem(HWND hwnd, LPFOLDER f, int i)
{
    char szPath[MAXPATHLEN];
    char szMsg[80];
    LPFITEM p;

    if (i < 0 || i >= f->nItems)
        return;
    p = &f->lpItems[i];
    if (p->bKind == FK_DRIVE) {
        if (!DriveReady(p->szName[0] - 'A')) {
            wsprintf(szMsg, "Drive %s is not ready.", (LPSTR)p->szName);
            MessageBox(hwnd, szMsg, NS_APPNAME, MB_OK | MB_ICONEXCLAMATION);
            return;
        }
        wsprintf(szPath, "%s\\", (LPSTR)p->szName);
        OpenFolderPath(hwnd, f, szPath);
        return;
    }
    lstrcpy(szPath, f->szPath);
    PathAppend(szPath, p->szName);
    if (p->bKind == FK_DIR)
        OpenFolderPath(hwnd, f, szPath);
    else
        OpenPath(hwnd, szPath);
}

static void GoUp(HWND hwnd, LPFOLDER f)
{
    char szPath[MAXPATHLEN];

    if (f->szPath[0] == '\0')
        return;
    lstrcpy(szPath, f->szPath);
    if (!PathParent(szPath))
        szPath[0] = '\0';               /* root: go to Computer */
    OpenFolderPath(hwnd, f, szPath);
}

static void ContextMenu(HWND hwnd, LPFOLDER f, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();

    if (f->nSel >= 0) {
        AppendMenu(hMenu, MF_STRING, CMD_F_OPEN, "&Open");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    }
    AppendMenu(hMenu, MF_STRING | (f->szPath[0] ? 0 : MF_GRAYED), CMD_F_UP,
               "&Up One Level");
    AppendMenu(hMenu, MF_STRING, CMD_F_REFRESH, "&Refresh");
    ClientToScreen(hwnd, &pt);
    TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

static void OnKeyDown(HWND hwnd, LPFOLDER f, WPARAM vk)
{
    int i = f->nSel;

    switch (vk) {
    case VK_LEFT:   Select(hwnd, f, i < 0 ? 0 : (i > 0 ? i - 1 : 0)); break;
    case VK_RIGHT:  Select(hwnd, f, i < 0 ? 0 : i + 1); break;
    case VK_UP:     Select(hwnd, f, i < 0 ? 0 : (i >= f->nCols ? i - f->nCols : i)); break;
    case VK_DOWN:
        if (i < 0)
            Select(hwnd, f, 0);
        else if (i + f->nCols < f->nItems)
            Select(hwnd, f, i + f->nCols);
        break;
    case VK_HOME:   Select(hwnd, f, 0); break;
    case VK_END:    Select(hwnd, f, f->nItems - 1); break;
    case VK_PRIOR:  Select(hwnd, f, i - f->nCols * f->nVisRows < 0 ? 0 : i - f->nCols * f->nVisRows); break;
    case VK_NEXT:   Select(hwnd, f, i < 0 ? 0 : i + f->nCols * f->nVisRows); break;
    case VK_RETURN: OpenItem(hwnd, f, f->nSel); break;
    case VK_BACK:   GoUp(hwnd, f); break;
    case VK_F5:     Navigate(hwnd, f, f->szPath); break;
    }
}

/* -------------------------------------------------------------------- */
/* Window procedure                                                     */
/* -------------------------------------------------------------------- */

static void RemoveFolder(HWND hwnd)
{
    int i;

    for (i = 0; i < s_nFolders; i++)
        if (s_hwndFolders[i] == hwnd) {
            s_hwndFolders[i] = s_hwndFolders[--s_nFolders];
            return;
        }
}

LRESULT CALLBACK NSEXPORT FolderWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam)
{
    LPFOLDER f = GetFolder(hwnd);
    PAINTSTRUCT ps;
    POINT pt;
    int i;

    switch (msg) {
    case WM_CREATE:
        {
            CREATESTRUCT FAR *lpcs = (CREATESTRUCT FAR *)lParam;
            TEXTMETRIC tm;
            HDC hdc;
            HFONT hfOld;

            f = (LPFOLDER)malloc(sizeof(FOLDER));
            if (!f)
                return -1;
            _fmemset(f, 0, sizeof(FOLDER));
            SetWindowLong(hwnd, 0, (LONG)f);

            hdc = GetDC(hwnd);
            hfOld = SelectObject(hdc, g_hfontUI);
            GetTextMetrics(hdc, &tm);
            SelectObject(hdc, hfOld);
            ReleaseDC(hwnd, hdc);
            f->cyStatus = tm.tmHeight + 8;

            lstrcpyn(f->szPath, (LPCSTR)lpcs->lpCreateParams, sizeof(f->szPath));
            ReadFolder(hwnd, f);
            SetTitle(hwnd, f);
            if (s_nFolders < MAX_FOLDERS)
                s_hwndFolders[s_nFolders++] = hwnd;
        }
        return 0;

    case WM_SIZE:
        if (f) {
            UpdateLayout(hwnd, f);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        if (f)
            PaintFolder(hwnd, ps.hdc, f, &ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (f)
            InvalidateItem(hwnd, f, f->nSel);
        return 0;

    case WM_VSCROLL:
        if (!f)
            break;
        switch (wParam) {
        case SB_LINEUP:         ScrollTo(hwnd, f, f->nTop - 1); break;
        case SB_LINEDOWN:       ScrollTo(hwnd, f, f->nTop + 1); break;
        case SB_PAGEUP:         ScrollTo(hwnd, f, f->nTop - f->nVisRows); break;
        case SB_PAGEDOWN:       ScrollTo(hwnd, f, f->nTop + f->nVisRows); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:  ScrollTo(hwnd, f, LOWORD(lParam)); break;
        case SB_TOP:            ScrollTo(hwnd, f, 0); break;
        case SB_BOTTOM:         ScrollTo(hwnd, f, 32767); break;
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (!f)
            break;
        SetFocus(hwnd);
        Select(hwnd, f, HitTest(f, LOWORD(lParam), HIWORD(lParam)));
        return 0;

    case WM_LBUTTONDBLCLK:
        if (!f)
            break;
        i = HitTest(f, LOWORD(lParam), HIWORD(lParam));
        if (i >= 0)
            OpenItem(hwnd, f, i);
        return 0;

    case WM_RBUTTONUP:
        if (!f)
            break;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        ContextMenu(hwnd, f, pt);
        return 0;

    case WM_KEYDOWN:
        if (f)
            OnKeyDown(hwnd, f, wParam);
        return 0;

    case WM_COMMAND:
        if (!f)
            break;
        switch (wParam) {
        case CMD_F_OPEN:     OpenItem(hwnd, f, f->nSel); break;
        case CMD_F_UP:       GoUp(hwnd, f); break;
        case CMD_F_REFRESH:  Navigate(hwnd, f, f->szPath); break;
        case CMD_F_COMPUTER: FolderOpen(""); break;
        case CMD_F_CLOSE:    DestroyWindow(hwnd); break;
        case CMD_ABOUT:      AboutDialog(hwnd); break;
        }
        return 0;

    case WM_NS_REFRESH:
        if (f)
            Navigate(hwnd, f, f->szPath);
        return 0;

    case WM_DESTROY:
        RemoveFolder(hwnd);
        if (f) {
            if (f->lpItems)
                free(f->lpItems);
            free(f);
            SetWindowLong(hwnd, 0, 0L);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL FolderRegister(void)
{
    WNDCLASS wc;

    _fmemset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = FolderWndProc;
    wc.cbWndExtra = sizeof(LPFOLDER);
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_FOLDER));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_FOLDER);
    wc.lpszClassName = CLS_FOLDER;
    return RegisterClass(&wc);
}

HWND FolderOpen(LPCSTR lpPath)
{
    char szPath[MAXPATHLEN];
    LPFOLDER f;
    HWND hwnd;
    int i;

    lstrcpyn(szPath, lpPath, sizeof(szPath));
    if (szPath[0])
        PathNormalize(szPath);

    /* activate an existing window for the same folder */
    for (i = 0; i < s_nFolders; i++) {
        f = GetFolder(s_hwndFolders[i]);
        if (f && lstrcmpi(f->szPath, szPath) == 0) {
            hwnd = s_hwndFolders[i];
            if (IsIconic(hwnd))
                ShowWindow(hwnd, SW_RESTORE);
            BringWindowToTop(hwnd);
            SetActiveWindow(hwnd);
            return hwnd;
        }
    }

    hwnd = CreateWindow(CLS_FOLDER, "", WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                        CW_USEDEFAULT, 0, 420, 280, NULL, NULL, g_hInst,
                        (LPVOID)szPath);
    if (!hwnd)
        return NULL;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    TaskbarRefresh();
    return hwnd;
}

void FolderCloseAll(void)
{
    while (s_nFolders > 0)
        DestroyWindow(s_hwndFolders[s_nFolders - 1]);
}
