# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# NewShell16 - Open Watcom makefile (wmake)
#
#   wmake               build build/newshell.exe and build/nshook.dll
#   wmake clean         remove build output
#
# Requires the WATCOM environment variable to point at an Open Watcom 1.9 or
# 2.0 installation, with its host binaries on the PATH.

NAME    = newshell
SRCDIR  = src
RESDIR  = res
OBJDIR  = build

OBJS    = $(OBJDIR)/main.obj     &
          $(OBJDIR)/settings.obj &
          $(OBJDIR)/util.obj     &
          $(OBJDIR)/icons.obj    &
          $(OBJDIR)/progman.obj  &
          $(OBJDIR)/dde.obj      &
          $(OBJDIR)/taskbar.obj  &
          $(OBJDIR)/startmnu.obj &
          $(OBJDIR)/desktop.obj  &
          $(OBJDIR)/shortcut.obj &
          $(OBJDIR)/folder.obj   &
          $(OBJDIR)/fview.obj    &
          $(OBJDIR)/explorer.obj &
          $(OBJDIR)/toolbar.obj  &
          $(OBJDIR)/fileops.obj  &
          $(OBJDIR)/bin.obj      &
          $(OBJDIR)/props.obj    &
          $(OBJDIR)/taskman.obj  &
          $(OBJDIR)/find.obj     &
          $(OBJDIR)/dialogs.obj

RESFILES = $(RESDIR)/newshell.rc $(SRCDIR)/resource.h $(RESDIR)/*.ico &
           $(RESDIR)/*.cur $(RESDIR)/small.bmp

CC      = wcc
RC      = wrc
LD      = wlink

# -bt=windows   target Windows 3.x       -ml    large memory model
# -zWs          Windows prologs, smart callbacks (DS from SS)
# -2            286 instructions         -s     no stack checks
# -os           optimise for size        -w4    all warnings, -we as errors
CFLAGS  = -q -bt=windows -ml -zWs -2 -s -os -w4 -we -zq &
          -i="$(%WATCOM)/h" -i="$(%WATCOM)/h/win" -i=$(SRCDIR)

# the hook DLL: DS != SS (-zu)
DLLFLAGS = -q -bt=windows -ml -zu -zW -2 -s -os -w4 -we -zq &
          -i="$(%WATCOM)/h" -i="$(%WATCOM)/h/win" -i=$(SRCDIR)

RCFLAGS = -q -r -bt=windows -31 -i="$(%WATCOM)/h" -i="$(%WATCOM)/h/win" -i=$(SRCDIR) -i=$(RESDIR)

.c: $(SRCDIR)

all : $(OBJDIR) $(OBJDIR)/$(NAME).exe $(OBJDIR)/nshook.dll .SYMBOLIC

$(OBJDIR)/$(NAME).exe : $(OBJDIR) $(OBJS) $(OBJDIR)/$(NAME).res $(OBJDIR)/$(NAME).lnk
    $(LD) @$(OBJDIR)/$(NAME).lnk
    $(RC) -q -bt=windows -31 $(OBJDIR)/$(NAME).res $(OBJDIR)/$(NAME).exe

$(OBJDIR) : .ALWAYS .EXISTSONLY
    -mkdir $(OBJDIR)

$(OBJDIR)/$(NAME).lnk : makefile
    %create $@
    @%append $@ system windows
    @%append $@ name $(OBJDIR)/$(NAME).exe
    @%append $@ option quiet, map=$(OBJDIR)/$(NAME).map
    @%append $@ option stack=12k, heapsize=8k
    @%append $@ option description 'NewShell16'
    @%append $@ library commdlg.lib, shell.lib, toolhelp.lib
    @for %i in ($(OBJS)) do @%append $@ file %i

$(OBJDIR)/nshook.dll : $(OBJDIR)/nshook.obj makefile
    %create $(OBJDIR)/nshook.lnk
    @%append $(OBJDIR)/nshook.lnk system windows_dll
    @%append $(OBJDIR)/nshook.lnk name $(OBJDIR)/nshook.dll
    @%append $(OBJDIR)/nshook.lnk option quiet, map=$(OBJDIR)/nshook.map
    @%append $(OBJDIR)/nshook.lnk option description 'NewShell16 hooks'
    @%append $(OBJDIR)/nshook.lnk segment type code fixed
    @%append $(OBJDIR)/nshook.lnk import FileCdr KERNEL.130
    @%append $(OBJDIR)/nshook.lnk file $(OBJDIR)/nshook.obj
    $(LD) @$(OBJDIR)/nshook.lnk
    $(RC) -q -bt=windows -31 $(OBJDIR)/nshook.dll

$(OBJDIR)/nshook.obj : $(SRCDIR)/nshook.c $(SRCDIR)/nshook.h
    $(CC) $(DLLFLAGS) -fo=$@ $(SRCDIR)/nshook.c

$(OBJDIR)/$(NAME).res : $(RESFILES)
    $(RC) $(RCFLAGS) $(RESDIR)/$(NAME).rc -fo=$@

.c.obj: .AUTODEPEND
    $(CC) $(CFLAGS) -fo=$@ $[@

$(OBJDIR)/main.obj     : $(SRCDIR)/main.c
$(OBJDIR)/settings.obj : $(SRCDIR)/settings.c
$(OBJDIR)/util.obj     : $(SRCDIR)/util.c
$(OBJDIR)/icons.obj    : $(SRCDIR)/icons.c
$(OBJDIR)/progman.obj  : $(SRCDIR)/progman.c
$(OBJDIR)/dde.obj      : $(SRCDIR)/dde.c
$(OBJDIR)/taskbar.obj  : $(SRCDIR)/taskbar.c
$(OBJDIR)/startmnu.obj : $(SRCDIR)/startmnu.c
$(OBJDIR)/desktop.obj  : $(SRCDIR)/desktop.c
$(OBJDIR)/shortcut.obj : $(SRCDIR)/shortcut.c
$(OBJDIR)/folder.obj   : $(SRCDIR)/folder.c
$(OBJDIR)/fview.obj    : $(SRCDIR)/fview.c
$(OBJDIR)/explorer.obj : $(SRCDIR)/explorer.c
$(OBJDIR)/toolbar.obj  : $(SRCDIR)/toolbar.c
$(OBJDIR)/fileops.obj  : $(SRCDIR)/fileops.c
$(OBJDIR)/bin.obj      : $(SRCDIR)/bin.c
$(OBJDIR)/props.obj    : $(SRCDIR)/props.c
$(OBJDIR)/taskman.obj  : $(SRCDIR)/taskman.c
$(OBJDIR)/find.obj     : $(SRCDIR)/find.c
$(OBJDIR)/dialogs.obj  : $(SRCDIR)/dialogs.c

# the installer (needs the submodules: git submodule update --init)
release : .SYMBOLIC
    python3 tools/release/mkrelease.py

clean : .SYMBOLIC
    -rm -f $(OBJDIR)/*.obj $(OBJDIR)/*.res $(OBJDIR)/*.exe $(OBJDIR)/*.dll &
           $(OBJDIR)/*.map $(OBJDIR)/*.lnk $(OBJDIR)/*.err
