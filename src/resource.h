/*
 * resource.h - resource identifiers shared by the C sources and newshell.rc
 */
#ifndef RESOURCE_H
#define RESOURCE_H

/* Icons */
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
#define SMI_COUNT           15

/* Menus */
#define IDM_FOLDER          200

/* Dialogs */
#define IDD_RUN             300
#define IDD_SHUTDOWN        301
#define IDD_ABOUT           302

/* Dialog controls */
#define IDC_RUNCMD          400
#define IDC_RUNMIN          401
#define IDC_BROWSE          402
#define IDC_EXITWIN         410
#define IDC_RESTARTWIN      411
#define IDC_REBOOT          412
#define IDC_CLOSESHELL      413
#define IDC_VERSION         420

/* Commands (WM_COMMAND ids) */
#define CMD_RUN             500
#define CMD_SHUTDOWN        501
#define CMD_ABOUT           502
#define CMD_COMPUTER        503
#define CMD_CONTROLPANEL    504
#define CMD_PRINTERS        505
#define CMD_DOSPROMPT       506
#define CMD_FILEMANAGER     507
#define CMD_STARTMENU       508
#define CMD_CLOSESHELL      509
#define CMD_TASKMAN         510
#define CMD_CASCADE         511
#define CMD_TILE            512
#define CMD_ARRANGEICONS    513
#define CMD_MINIMIZEALL     514
#define CMD_WINSETUP        515
#define CMD_HELP            516

/* Folder window commands */
#define CMD_F_OPEN          600
#define CMD_F_UP            601
#define CMD_F_REFRESH       602
#define CMD_F_CLOSE         603
#define CMD_F_SELALL        604
#define CMD_F_LARGEICONS    605
#define CMD_F_LIST          606
#define CMD_F_COMPUTER      607

/* Dynamic start menu items: CMD_SM_FIRST .. CMD_SM_FIRST + SM_MAXITEMS - 1 */
#define CMD_SM_FIRST        1000

#endif
