# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# NewShell16 - Open Watcom makefile (wmake)
#
#   wmake               build build/newshell.exe
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
          $(OBJDIR)/taskbar.obj  &
          $(OBJDIR)/startmnu.obj &
          $(OBJDIR)/desktop.obj  &
          $(OBJDIR)/folder.obj   &
          $(OBJDIR)/dialogs.obj

RESFILES = $(RESDIR)/newshell.rc $(SRCDIR)/resource.h $(RESDIR)/*.ico $(RESDIR)/small.bmp

CC      = wcc
RC      = wrc
LD      = wlink

# -bt=windows   target Windows 3.x       -ml    large memory model
# -zWs          Windows prologs, smart callbacks (DS from SS)
# -2            286 instructions         -s     no stack checks
# -os           optimise for size        -w4    all warnings, -we as errors
CFLAGS  = -q -bt=windows -ml -zWs -2 -s -os -w4 -we -zq &
          -i="$(%WATCOM)/h" -i="$(%WATCOM)/h/win" -i=$(SRCDIR)

RCFLAGS = -q -r -bt=windows -31 -i="$(%WATCOM)/h" -i="$(%WATCOM)/h/win" -i=$(SRCDIR) -i=$(RESDIR)

.c: $(SRCDIR)

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
    @%append $@ library commdlg.lib, shell.lib
    @for %i in ($(OBJS)) do @%append $@ file %i

$(OBJDIR)/$(NAME).res : $(RESFILES)
    $(RC) $(RCFLAGS) $(RESDIR)/$(NAME).rc -fo=$@

.c.obj: .AUTODEPEND
    $(CC) $(CFLAGS) -fo=$@ $[@

$(OBJDIR)/main.obj     : $(SRCDIR)/main.c
$(OBJDIR)/settings.obj : $(SRCDIR)/settings.c
$(OBJDIR)/util.obj     : $(SRCDIR)/util.c
$(OBJDIR)/icons.obj    : $(SRCDIR)/icons.c
$(OBJDIR)/progman.obj  : $(SRCDIR)/progman.c
$(OBJDIR)/taskbar.obj  : $(SRCDIR)/taskbar.c
$(OBJDIR)/startmnu.obj : $(SRCDIR)/startmnu.c
$(OBJDIR)/desktop.obj  : $(SRCDIR)/desktop.c
$(OBJDIR)/folder.obj   : $(SRCDIR)/folder.c
$(OBJDIR)/dialogs.obj  : $(SRCDIR)/dialogs.c

clean : .SYMBOLIC
    -rm -f $(OBJDIR)/*.obj $(OBJDIR)/*.res $(OBJDIR)/*.exe $(OBJDIR)/*.map $(OBJDIR)/*.lnk $(OBJDIR)/*.err
