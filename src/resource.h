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
 * resource.h - resource identifiers shared by the C sources and newshell.rc
 */
#ifndef RESOURCE_H
#define RESOURCE_H

/* Icons (IDI_NEWSHELL..IDI_LAST are loaded by IconsInit) */
#define IDI_NEWSHELL        1
#define IDI_FOLDER          2
#define IDI_DOCUMENT        3
#define IDI_APP             4
#define IDI_COMPUTER        5
#define IDI_FLOPPY          6
#define IDI_HDD             7
#define IDI_CDROM           8
#define IDI_NETDRV          9
#define IDI_BIN             10
#define IDI_BINFULL         11
#define IDI_EXPLORER        12
#define IDI_TASKMAN         13
#define IDI_FIND            14
/* large Start menu images */
#define IDI_SM_PROGRAMS     15
#define IDI_SM_DOCUMENTS    16
#define IDI_SM_SETTINGS     17
#define IDI_SM_FIND         18
#define IDI_SM_RUN          19
#define IDI_SM_SHUTDOWN     20
#define IDI_SM_HELP         21
#define IDI_SM_FAVORITES    22
#define IDI_SM_INTERNET     23
#define IDI_LAST            23

/* Bitmaps */
#define IDB_SMALL           100     /* strip of 16x16 images, magenta key */

/* Indices into IDB_SMALL (order must match tools/mkicons.py) */
#define SMI_PROGRAMS        0
#define SMI_DOCUMENT        1
#define SMI_SETTINGS        2
#define SMI_FIND            3
#define SMI_RUN             4
#define SMI_SHUTDOWN        5
#define SMI_COMPUTER        6
#define SMI_FOLDER          7
#define SMI_HELP            8
#define SMI_PRINTER         9
#define SMI_CONTROL         10
#define SMI_ABOUT           11
#define SMI_DOS             12
#define SMI_APP             13
#define SMI_START           14
#define SMI_EXPLORER        15
#define SMI_BIN             16
#define SMI_TASKMAN         17
/* toolbar glyphs */
#define SMI_TB_UP           18
#define SMI_TB_NEWFOLDER    19
#define SMI_TB_CUT          20
#define SMI_TB_COPY         21
#define SMI_TB_PASTE        22
#define SMI_TB_DELETE       23
#define SMI_TB_PROPS        24
#define SMI_TB_LARGE        25
#define SMI_TB_SMALL        26
#define SMI_TB_LIST         27
#define SMI_TB_DETAILS      28
#define SMI_TB_SORTNAME     29
#define SMI_TB_SORTTYPE     30
#define SMI_TB_SORTSIZE     31
#define SMI_TB_SORTDATE     32
#define SMI_FAVORITES       33
#define SMI_INTERNET        34
#define SMI_TB_RENAME       35
#define SMI_TB_GOTO         36
#define SMI_TB_INSERT       37
#define SMI_TB_NEWITEM      38
#define SMI_FOLDEROPEN      39
#define SMI_COUNT           40

/* Cursors */
#define IDC_DRAGMOVE        150
#define IDC_DRAGCOPY        151
#define IDC_NODROP          152
#define IDC_SPLIT           153

/* Menus */
#define IDM_FOLDER          200
#define IDM_BIN             201
#define IDM_TASKMAN         202
#define IDM_TREE            203

/* Dialogs */
#define IDD_RUN             300
#define IDD_SHUTDOWN        301
#define IDD_ABOUT           302
#define IDD_PROGRESS        303
#define IDD_REPLACE         304
#define IDD_PROPERTIES      305
#define IDD_FIND            306
#define IDD_OPTIONS         307
#define IDD_TASKMAN         308
#define IDD_SHORTCUT        309
#define IDD_CHANGEICON      310
#define IDD_BINPROPS        311
#define IDD_SELECT          312
#define IDD_FILTER          313
#define IDD_GOTO            314
#define IDD_OPENWITH        315
#define IDD_DESCRIBE        316
#define IDD_COLUMNS         317
#define IDD_STARTPROPS      318
#define IDD_VERSION         319

/* Dialog controls */
#define IDC_RUNCMD          400
#define IDC_RUNMIN          401
#define IDC_BROWSE          402
#define IDC_EXITWIN         410
#define IDC_RESTARTWIN      411
#define IDC_REBOOT          412
#define IDC_CLOSESHELL      413
#define IDC_VERSION         420
#define IDC_PROGTEXT1       430
#define IDC_PROGTEXT2       431
#define IDC_PROGBAR         432
#define IDC_REPLACETEXT     440
#define IDC_YESALL          441
#define IDC_NOALL           442
#define IDC_PROPICON        450
#define IDC_PROPNAME        451
#define IDC_PROPL1          452     /* IDC_PROPL1..+4: labels */
#define IDC_PROPV1          460     /* IDC_PROPV1..+4: values */
#define IDC_PROPASSOC       466
#define IDC_PROPPIE         467
#define IDC_PROPVERSION     468
#define IDC_VERTEXT         469
#define IDC_ATTR_R          470
#define IDC_ATTR_H          471
#define IDC_ATTR_S          472
#define IDC_ATTR_A          473
#define IDC_FINDNAME        480
#define IDC_FINDDIR         481
#define IDC_FINDSUB         482
#define IDC_FINDLIST        483
#define IDC_FINDSTOP        484
#define IDC_FINDOPEN        485
#define IDC_FINDOPENDIR     486
#define IDC_FINDSTATUS      487
#define IDC_FINDICON        488
#define IDC_OPT_FIRST       490
#define IDC_OPT_CLOCK       490
#define IDC_OPT_HIDEMIN     491
#define IDC_OPT_MAXCLAMP    492
#define IDC_OPT_CTRLESC     493
#define IDC_OPT_WINKEY      494
#define IDC_OPT_DESKICONS   495
#define IDC_OPT_DESKMENU    496
#define IDC_OPT_NEWWINDOW   497
#define IDC_OPT_SHOWHIDDEN  498
#define IDC_OPT_TOBIN       499
#define IDC_OPT_CONFIRMDEL  500
#define IDC_OPT_FILEMON     501
#define IDC_OPT_STARTUP     502
#define IDC_OPT_LAST        502
#define IDC_TMLIST          510
#define IDC_TMSWITCH        511
#define IDC_TMENDTASK       512
#define IDC_TMNEWTASK       513
#define IDC_TMSTATUS        514
#define IDC_TMHDR1          515
#define IDC_TMHDR2          516

#define IDC_SCICON          520
#define IDC_SCCAPTION       521
#define IDC_SCCMD           522
#define IDC_SCBROWSE        523
#define IDC_SCDIR           524
#define IDC_SCSHOW          525
#define IDC_SCCHANGEICON    526
#define IDC_BINCAPTION      540
#define IDC_BINCAPACITY     541
#define IDC_BINEMPTYEXIT    542
#define IDC_BINDIRECT       543
#define IDC_BINSTATUS       544
#define IDC_SELSPEC         550
#define IDC_SELNONE         551
#define IDC_FILTERSPEC      552
#define IDC_FILTERHIDDEN    553
#define IDC_FILTERALL       554
#define IDC_GOTOPATH        555
#define IDC_OWTEXT          556
#define IDC_OWLIST          557
#define IDC_OWPROG          558
#define IDC_OWBROWSE        559
#define IDC_OWALWAYS        560
#define IDC_DESCTEXT        561
#define IDC_DESCEDIT        562
#define IDC_COL_FIRST       570     /* .. +5: Size .. Description */
#define IDC_SPTREE          580
#define IDC_SPNEWITEM       581
#define IDC_SPNEWSUB        582
#define IDC_SPNEWSEP        583
#define IDC_SPDELETE        584
#define IDC_SPEDIT          585
#define IDC_SPUP            586
#define IDC_SPDOWN          587
#define IDC_SPIMPORT        588
#define IDC_SP3D            589
#define IDC_SPCOLBAR        590
#define IDC_SPBOLD          591
#define IDC_SPLARGE         592
#define IDC_SPHEIGHT        593
#define IDC_SPFONT          594
#define IDC_SPMOUSEUP       595
#define IDC_SPDDE           596
#define IDC_CIFILE          530
#define IDC_CIBROWSE        531
#define IDC_CILIST          532
#define IDC_CISTATUS        533

/* Commands (WM_COMMAND ids) */
#define CMD_RUN             600
#define CMD_SHUTDOWN        601
#define CMD_ABOUT           602
#define CMD_COMPUTER        603
#define CMD_CONTROLPANEL    604
#define CMD_STARTMENU       608
#define CMD_CLOSESHELL      609
#define CMD_TASKMAN         610
#define CMD_CASCADE         611
#define CMD_TILE            612
#define CMD_TILEVERT        613
#define CMD_MINIMIZEALL     614
#define CMD_EXPLORER        615
#define CMD_BIN             616
#define CMD_FIND            617
#define CMD_OPTIONS         618
#define CMD_DESKTOPPROPS    619
#define CMD_ARRANGEICONS    620
#define CMD_LINEUPICONS     621
#define CMD_STARTPROPS      622
#define CMD_TB_WININFO      623
#define CMD_TB_TERMINATE    624
#define CMD_TB_AUTOHIDE     625
#define CMD_TOGGLETASKBAR   626
#define CMD_CLEARDESKTOP    627
#define CMD_CLOSEFOLDERS    628
#define CMD_CASCADEFOLDERS  629
#define CMD_SCREENSAVER     630
#define CMD_BROWSEFOLDERS   631

/* Folder view commands */
#define CMD_F_OPEN          700
#define CMD_F_UP            701
#define CMD_F_REFRESH       702
#define CMD_F_CLOSE         703
#define CMD_F_SELALL        704
#define CMD_F_LARGEICONS    705
#define CMD_F_SMALLICONS    706
#define CMD_F_DETAILS       707
#define CMD_F_COMPUTER      708
#define CMD_F_EXPLORE       709
#define CMD_F_SORTNAME      710
#define CMD_F_SORTTYPE      711
#define CMD_F_SORTSIZE      712
#define CMD_F_SORTDATE      713
#define CMD_F_CUT           714
#define CMD_F_COPY          715
#define CMD_F_PASTE         716
#define CMD_F_DELETE        717
#define CMD_F_RENAME        718
#define CMD_F_NEWFOLDER     719
#define CMD_F_PROPERTIES    720
#define CMD_F_INVERTSEL     721
#define CMD_F_LIST          722
#define CMD_F_TOOLBAR       723
#define CMD_F_STATUSBAR     724
#define CMD_F_OPENWITH      725
#define CMD_F_SELECTFILES   726
#define CMD_F_FILTER        727
#define CMD_F_GOTO          728
#define CMD_F_DESCRIBE      729
#define CMD_F_INSPECT       730
#define CMD_F_PRINT         731
#define CMD_F_NEWALIAS      732
#define CMD_F_UNDELETE      733
#define CMD_F_COLUMNS       734
#define CMD_F_MAPDRIVE      735
#define CMD_F_DISCONNECT    736
#define CMD_F_PARENTWIN     737
#define CMD_F_COPYNAMES     738

/* Folder tree commands */
#define CMD_T_FIRST         740
#define CMD_T_OPEN          740
#define CMD_T_EXPLORE       741
#define CMD_T_FIND          742
#define CMD_T_EXPAND        743
#define CMD_T_EXPANDBRANCH  744
#define CMD_T_EXPANDALL     745
#define CMD_T_COLLAPSE      746
#define CMD_T_NEWFOLDER     747
#define CMD_T_REFRESH       748
#define CMD_T_SHARE         749
#define CMD_T_STOPSHARE     739
#define CMD_T_PROPERTIES    738
#define CMD_T_LAST          749

/* Bin and Task Manager commands */
#define CMD_BIN_RESTORE     750
#define CMD_BIN_DELETE      751
#define CMD_BIN_EMPTY       752
#define CMD_BIN_PROPS       753
#define CMD_TM_SWITCH       760
#define CMD_TM_HIDE         761
#define CMD_TM_MINIMIZE     762
#define CMD_TM_ENDTASK      763
#define CMD_TM_TERMINATE    764

/* Dynamic start menu items: CMD_SM_FIRST .. CMD_SM_FIRST + 239 */
#define CMD_SM_FIRST        1000

#endif
