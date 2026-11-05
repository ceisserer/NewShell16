/*
 * NewShell16 - Copyright (C) 2026 NewShell16 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ofntest.c - calls GetOpenFileName / GetSaveFileName with a few flag sets
 * and writes what comes back to C:\OFN.TXT, for checking NSDLG.DLL
 * against COMMDLG (8.3 paths, the multiple selection format, offsets).
 * Build and use: docs/testing.md.
 */
#define STRICT
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
static char file[512], title[64];
static void run(FILE *f, int save, DWORD flags, const char *init, const char *defext)
{
    OPENFILENAME o;
    BOOL r;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.lpstrFilter = "Text (*.TXT)\0*.TXT\0All files\0*.*\0";
    o.nFilterIndex = 1;
    strcpy(file, init);
    o.lpstrFile = file; o.nMaxFile = sizeof file;
    o.lpstrFileTitle = title; o.nMaxFileTitle = sizeof title;
    o.Flags = flags; o.lpstrDefExt = defext;
    r = save ? GetSaveFileName(&o) : GetOpenFileName(&o);
    fprintf(f, "%s r=%d file=[%s] title=[%s] off=%u ext=%u idx=%lu flags=%08lx\n", save ? "SAVE" : "OPEN", r, file, title,
            o.nFileOffset, o.nFileExtension, o.nFilterIndex, o.Flags);
    fflush(f);
}
int PASCAL WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d)
{
    FILE *f = fopen("C:\\OFN.TXT", "w");
    (void)a; (void)b; (void)c; (void)d;
    run(f, 0, OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, "", NULL);
    run(f, 1, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, "", "TXT");
    run(f, 0, OFN_FILEMUSTEXIST, "C:\\MYDOCU~1\\*.WRI", NULL);
    fclose(f);
    return 0;
}
