/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Ui - the menu's state: which screen shows, what is on it, and what each
  input does.

  Nothing here draws or touches the firmware. The caller feeds in inputs and
  clock ticks, carries out the command each call returns, and reports
  results back through the AppOn* calls. View/View.h draws the state.
*/

#ifndef __UI_UI_H__
#define __UI_UI_H__

#include "Base/OablBase.h"

#include "Core/Settings.h"

typedef struct _DEVICE_INFO {
  CHAR8 Soc[64];
  CHAR8 Ram[32];
  CHAR8 Storage[48];
  CHAR8 SdCard[48];
  CHAR8 Firmware[64];
} DEVICE_INFO;

// Input and commands

typedef enum { INPUT_UP, INPUT_DOWN, INPUT_SELECT, INPUT_BACK } INPUT;

// How the user drives the menu. It only changes the footer hints.
typedef enum {
  SCHEME_HANDHELD, // volume up/down and power
  SCHEME_KEYBOARD, // arrow keys, Enter and Esc
} SCHEME;

typedef enum {
  REBOOT_NORMAL,
  REBOOT_LOADER, // back into the menu
  REBOOT_RECOVERY,
  REBOOT_EDL,
  REBOOT_OTHER_SLOT,
} REBOOT_TARGET;

// Switches in the Debug section, which only debug builds have.
typedef struct _EXPERIMENTS {
  BOOLEAN UsbLive;
  BOOLEAN Quiesce;
  BOOLEAN PanelLinesOff;
} EXPERIMENTS;

// What COMMAND_BOOT starts. ORDER is what power-on starts, the first of
// the boot order.
typedef enum {
  BOOT_TARGET_ORDER,
  BOOT_TARGET_PORTAREOS,
  BOOT_TARGET_ANDROID,
} BOOT_TARGET;

typedef enum {
  COMMAND_NONE,
  COMMAND_BOOT,       // Arg is a BOOT_TARGET
  COMMAND_BOOT_ENTRY, // Arg is the entry index
  COMMAND_REBOOT,     // Arg is a REBOOT_TARGET
  COMMAND_POWER_OFF,
  COMMAND_SAVE_SETTINGS,
  COMMAND_SCAN_MODELS,
  COMMAND_INSTALL, // Arg is an INSTALL_CHOICE; InstallAndroid is Android's share
  COMMAND_DEVICE_INFO,
  COMMAND_LOG,
  COMMAND_SAVE_EXPERIMENTS, // saves Experiments
} COMMAND_KIND;

// Work for the platform. Run it after the next frame, so the screen
// already shows what's happening.
typedef struct _COMMAND {
  COMMAND_KIND Kind;
  INT32        Arg;
  EXPERIMENTS  Experiments;
} COMMAND;

typedef enum {
  BOOT_ERROR_NO_IMAGE,       // nothing bootable on the source
  BOOT_ERROR_LOAD_FAILED,    // an image failed to load or start
  BOOT_ERROR_ANDROID,        // Android boot isn't available
  BOOT_ERROR_MODEL_NOT_SET,  // a model is needed for the devicetree
  BOOT_ERROR_ANDROID_FAILED, // booting Android (boot_a/b) returned
  // The boot list's pick: its drive is gone, or it didn't start. ErrorDetail
  // names it, or is empty after a restart.
  BOOT_ERROR_PICK_GONE,
  BOOT_ERROR_PICK_FAILED,
  BOOT_ERROR_NO_CARD, // Install PortareOS without a PortareOS card
  // Installing failed; ErrorDetail says why, InstallChanged whether
  // internal storage was changed.
  BOOT_ERROR_INSTALL,
} BOOT_ERROR;

// What the view draws

// Gfx/Icons.c draws these; the menu itself no longer does.
typedef enum {
  ICON_PLAY,
  ICON_REFRESH,
  ICON_LIFEBUOY,
  ICON_CPU,
  ICON_SWITCH,
  ICON_TERMINAL,
  ICON_VOLUME,
  ICON_SD_CARD,
  ICON_BATTERY,
  ICON_GAMEPAD,
  ICON_TRASH,
  ICON_POWER,
  ICON_CHEVRON_UP,
  ICON_CHEVRON_DOWN,
  ICON_CHEVRON_RIGHT,
  ICON_SELECTOR,
  ICON_ARROWS_VERTICAL,
  ICON_ARROW_BACK,
  ICON_ARROW_LEFT,
  ICON_INFO,
  ICON_ALERT,
  ICON_CIRCLE,
  ICON_CIRCLE_CHECK,
  ICON_DATABASE,
  ICON_STACK,
  ICON_MONITOR,
  ICON_ROTATE,
  ICON_ENTER,
  ICON_CLOCK,
  ICON_WORLD,
  ICON_FORK,
  ICON_MESSAGE,
  ICON_PHOTO,
  ICON_USB,
  ICON_MONITOR_OFF,
  ICON_PLUG,
  ICON_COUNT
} ICON;

typedef enum {
  ITEM_BOOT, // opens the boot list
  ITEM_BOOT_ORDER,
  ITEM_INSTALL,   // PortareOS, from the SD card to internal storage
  ITEM_EDL,
  ITEM_RESTART,
  ITEM_POWER_OFF,
  // Debug builds only.
  ITEM_USB_LIVE,
  ITEM_QUIESCE,
  ITEM_PANEL_LINES,
} ITEM;

typedef struct _ROW {
  CONST CHAR8 *Header; // a section's title, NULL for an item
  ITEM         Item;
} ROW;

typedef struct _ITEM_VIEW {
  CONST CHAR8 *Label;
  CHAR8        Value[MODEL_LEN + 16]; // "" for none
  // The status line while the row is focused, or NULL for none.
  CONST CHAR8 *Help;
  // What Power does on the row, for the footer.
  CONST CHAR8 *Verb;
} ITEM_VIEW;

typedef enum {
  SCREEN_MENU,
  SCREEN_SCANNING,
  SCREEN_MODELS,
  SCREEN_ENTRIES, // the boot list
  SCREEN_CONFIRM,
  SCREEN_BOOTING,
  SCREEN_ERROR,
  SCREEN_LINES,        // the device info, or the log
  SCREEN_INSTALL,      // Install PortareOS's two choices
  SCREEN_INSTALL_SIZE, // a first Dualboot: what Android keeps
  SCREEN_INSTALLING,   // takes no input
  SCREEN_INSTALLED,    // Select restarts
  // The charger powered the Nova on. Select starts the default system; Up
  // and Down open the menu.
  SCREEN_CHARGING,
} SCREEN;

typedef enum {
  CONFIRM_EDL,
  CONFIRM_INSTALL,
} CONFIRM;

typedef enum {
  INSTALL_CHOICE_DUALBOOT,
  INSTALL_CHOICE_REPLACE,
} INSTALL_CHOICE;

// How far an install is, for the installing screen.
typedef enum {
  INSTALL_STAGE_PREPARE,
  INSTALL_STAGE_STORAGE,
  INSTALL_STAGE_COPY,
  INSTALL_STAGE_CHECK,
} INSTALL_STAGE;

// What Install PortareOS can do, from the platform's look at the SD card
// and internal storage when the menu started. Sizes are in bytes.
typedef struct _INSTALL_INFO {
  BOOLEAN Card;     // a PortareOS card is in
  BOOLEAN Known;    // internal storage's layout was read and understood
  BOOLEAN Android;  // Android is installed
  BOOLEAN Dualboot; // Dualboot can run
  // Dualboot only replaces PortareOS's system: it's next to Android
  // already, and its storage stays.
  BOOLEAN Update;
  BOOLEAN Replace;   // Replace Android can run
  BOOLEAN PortareOs; // PortareOS is on internal storage
  // A first Dualboot splits Shared between Android, which keeps AndroidMin
  // to AndroidMax, PortareOS's system (BootBytes) and its storage.
  UINT64 Shared;
  UINT64 AndroidMin;
  UINT64 AndroidMax;
  UINT64 BootBytes;
  UINT64 UpdateStorage;  // an update keeps this storage
  UINT64 ReplaceStorage; // Replace Android gives storage this
} INSTALL_INFO;

// Scroll is the first row the list shows.
typedef struct _LIST_STATE {
  UINTN Focus;
  UINTN Scroll;
} LIST_STATE;

#define MAX_ROWS    40
#define MAX_MODELS  64
#define MAX_ENTRIES 32
#define MAX_LINES   512
// What a first Dualboot offers Android to keep.
#define MAX_ANDROID_SIZES 6

typedef struct _MODEL {
  CHAR8  Name[MODEL_LEN];
  UINT16 Rotation;   // or ROTATION_NONE
  CHAR8  Source[64]; // where the devicetree was found
} MODEL;

// What a boot entry is. Named in the current language when drawn.
typedef enum {
  ENTRY_KIND_BLS,
  ENTRY_KIND_UKI,
  ENTRY_KIND_EXTLINUX,
  ENTRY_KIND_DEFAULT_FILE,
  ENTRY_KIND_BOOT_IMAGE,
  ENTRY_KIND_ANDROID, // Android's own boot partitions
} ENTRY_KIND;

// A system the boot list offers: its name, and where it boots from.
typedef struct _ENTRY_VIEW {
  CHAR8      Title[96];   // the system, such as PortareOS
  CHAR8      Version[64]; // "" for none
  CHAR8      Drive[64];   // the drive's name
  ENTRY_KIND Kind;
  BOOLEAN    Removable; // on the SD card, not internal storage
  BOOLEAN    Bad;       // no tries left (Automatic Boot Assessment)
} ENTRY_VIEW;

typedef struct _TEXT_LINE {
  CONST CHAR8 *Label; // "" for a bare line (the log)
  CHAR8        Value[256];
} TEXT_LINE;

typedef struct _APP {
  SETTINGS      Settings;
  SETTINGS_CAPS Caps;
  DEVICE_INFO   Info;
  SCHEME        Scheme;
  CONST CHAR8  *Version;
  BOOLEAN       Debug; // show the Debug section
  // The panel's size in pixels, before any rotation.
  UINT32 PhysicalW, PhysicalH;
  UINT16 FirmwareRotation;

  SCREEN     Screen;
  CONFIRM    Confirm;
  BOOLEAN    ConfirmYes;
  BOOT_ERROR Error;
  CHAR8      ErrorDetail[192];
  // For a failed pick: it was on the SD card.
  BOOLEAN PickFromSd;
  // What the starting screen and a boot error are about.
  BOOT_TARGET Starting;

  ITEM       FocusItem;
  LIST_STATE Menu;

  MODEL      Models[MAX_MODELS];
  UINTN      ModelCount;
  LIST_STATE ModelList;

  ENTRY_VIEW Entries[MAX_ENTRIES];
  UINTN      EntryCount;
  LIST_STATE EntryList;

  // Install PortareOS: what it can do, the choice and Android's share.
  INSTALL_INFO   Install;
  LIST_STATE     InstallList;
  LIST_STATE     SizeList;
  INSTALL_CHOICE InstallChoice;
  UINT64         InstallAndroid;
  INSTALL_STAGE  InstallStage;
  UINT32         InstallPermille;
  BOOLEAN        InstallChanged;

  // The charging screen: whether the battery could be read, whether a
  // charger is in, and the battery's voltage.
  BOOLEAN BatteryKnown;
  BOOLEAN Charger;
  UINT32  BatteryMv;

  // The device info or the log. LinesFrom is the menu item it was opened
  // from, focused again on the way back.
  TEXT_LINE    Lines[MAX_LINES];
  UINTN        LineCount;
  CONST CHAR8 *LinesTitle;
  ITEM         LinesFrom;
  LIST_STATE   InfoList;

  EXPERIMENTS Experiments;
  INT32       Booting; // the entry picked from the list, or -1

  CHAR8   Toast[160];
  UINT64  ToastUntil; // 0 for no toast
  BOOLEAN Countdown;
  UINT64  CountdownUntil;
  // Clock in milliseconds, from the last call that passed it.
  UINT64 Now;
  // Set when the state changes. ViewSync clears it.
  BOOLEAN Dirty;
  // Whole seconds left at the last tick. A change marks the state dirty.
  UINT64 ShownCountdown;
} APP;

// Starts the app on the menu. Version is kept, not copied.
VOID
AppInit (
  OUT APP                *A,
  IN CONST SETTINGS      *Settings,
  IN CONST SETTINGS_CAPS *Caps,
  IN CONST DEVICE_INFO   *Info,
  IN SCHEME               Scheme,
  IN CONST CHAR8         *Version
);

// Decides what happens at power-on. With ForceMenu (a key held, or a
// restart into the menu) the menu stays. Otherwise it counts down the menu
// timeout, or boots at once if the timeout is 0.
COMMAND
AppStart (
  IN OUT APP *A,
  IN UINT64   Now,
  IN BOOLEAN  ForceMenu
);

// Advances the clock to Now. Hides a toast that is due, and starts the
// boot when the countdown runs out.
COMMAND
AppTick (
  IN OUT APP *A,
  IN UINT64   Now
);

// Acts on one input. The first input during the countdown only stops it.
COMMAND
AppInput (
  IN OUT APP *A,
  IN INPUT    Input
);

COMMAND
AppOpenModels (IN OUT APP *A);

VOID
AppSetSettings (
  IN OUT APP        *A,
  IN CONST SETTINGS *Settings
);

VOID
AppSetCaps (
  IN OUT APP             *A,
  IN CONST SETTINGS_CAPS *Caps
);

// W and H are the panel's size before any rotation.
VOID
AppSetPhysicalSize (
  IN OUT APP *A,
  IN UINT32   W,
  IN UINT32   H
);

// Sets the rotation the firmware already applies (180 where XBL scans a
// panel out upside down). Auto adds it to the devicetree's rotation.
VOID
AppSetFirmwareRotation (
  IN OUT APP *A,
  IN UINT16   Degrees
);

VOID
AppSetExperiments (
  IN OUT APP           *A,
  IN CONST EXPERIMENTS *Experiments
);

// Lists from the platform are copied, up to MAX_ENTRIES, MAX_MODELS and
// MAX_LINES.

// Sets the boot entries and scrolls their list back to the top.
VOID
AppSetEntries (
  IN OUT APP          *A,
  IN CONST ENTRY_VIEW *Entries,
  IN UINTN             Count
);

// Shows the models the scan found, focused on the current one.
VOID
AppOnModels (
  IN OUT APP     *A,
  IN CONST MODEL *Models,
  IN UINTN        Count
);

VOID
AppOnDeviceInfo (
  IN OUT APP         *A,
  IN CONST TEXT_LINE *Lines,
  IN UINTN            Count
);

// Shows the log scrolled to the newest line, keeping the newest that fit.
VOID
AppOnLog (
  IN OUT APP            *A,
  IN CONST CHAR8 *CONST *Lines,
  IN UINTN               Count
);

// Ok FALSE means the boot failed for Error, with Detail for
// BOOT_ERROR_LOAD_FAILED. Ok TRUE means the started image returned.
VOID
AppOnBootResult (
  IN OUT APP     *A,
  IN BOOLEAN      Ok,
  IN BOOT_ERROR   Error,
  IN CONST CHAR8 *Detail OPTIONAL
);

// After the boot list's pick didn't start: Gone if its drive was missing.
// Name is the system, "" if unknown (after a restart).
VOID
AppOnPickFailed (
  IN OUT APP     *A,
  IN BOOLEAN      Gone,
  IN CONST CHAR8 *Name,
  IN BOOLEAN      FromSd
);

VOID
AppSetInstall (
  IN OUT APP              *A,
  IN CONST INSTALL_INFO   *Info
);

// The sizes a first Dualboot offers Android, smallest first, at most
// MAX_ANDROID_SIZES; Default gets the one to focus.
UINTN
AppAndroidSizes (
  IN CONST APP *A,
  OUT UINT64   *Sizes,
  OUT UINTN    *Default
);

// What PortareOS's storage gets from a first Dualboot leaving Android
// AndroidBytes.
UINT64
AppDualbootStorage (
  IN CONST APP *A,
  IN UINT64     AndroidBytes
);

// While installing: the stage and how much of the whole is done, in
// thousandths. Marks the state dirty only when the shown percent changes.
VOID
AppOnInstallProgress (
  IN OUT APP       *A,
  IN INSTALL_STAGE  Stage,
  IN UINT32         Permille
);

// After installing: the installed screen, or the error screen with Why,
// saying whether internal storage was Changed.
VOID
AppOnInstalled (
  IN OUT APP     *A,
  IN BOOLEAN      Ok,
  IN CONST CHAR8 *Why OPTIONAL,
  IN BOOLEAN      Changed
);

// Shows the charging screen instead of the menu, with no countdown.
VOID
AppStartCharging (IN OUT APP *A);

// The battery as the platform last read it. Marks the state dirty only when
// the charger comes or goes or the level shown changes.
VOID
AppOnBattery (
  IN OUT APP *A,
  IN BOOLEAN  Known,
  IN BOOLEAN  Charger,
  IN UINT32   MilliVolts
);

// The battery's charge in percent, estimated from its voltage for the
// Nova's 4.5 V cell, less what charging adds to it. 0 when unknown.
UINT32
AppBatteryLevel (IN CONST APP *A);

// Shows why a command failed, returning to the menu from a screen that was
// waiting for it.
VOID
AppOnFailed (
  IN OUT APP     *A,
  IN CONST CHAR8 *Message
);

// Shows Message as a toast.
VOID
AppNotify (
  IN OUT APP     *A,
  IN CONST CHAR8 *Message
);

// Returns the rotation to present with, in degrees clockwise. With Auto and
// no model, a portrait framebuffer gets 270. Handhelds usually mount
// portrait panels sideways, and 270 is the common case (Pocket Max, RP5).
UINT16
AppDesiredRotation (IN CONST APP *A);

// For the view

// Fills Rows (MAX_ROWS long) with headers and items and returns the count.
UINTN
AppRows (
  IN CONST APP *A,
  OUT ROW      *Rows
);

// Returns the focused item's row, or 1 (the first item) if it isn't shown.
UINTN
AppFocusedRow (
  IN CONST APP *A,
  IN CONST ROW *Rows,
  IN UINTN      Count
);

VOID
AppItemView (
  IN CONST APP  *A,
  IN ITEM        Item,
  OUT ITEM_VIEW *View
);

// Returns the title of the entry being started (the first one for Start),
// or NULL when there are no entries.
CONST CHAR8 *
AppBootTitle (IN CONST APP *A);



// Returns TRUE during the countdown, with the seconds left rounded up.
BOOLEAN
AppCountdownSeconds (
  IN CONST APP *A,
  OUT UINT64   *Seconds
);

// For callers that allocate APP and SETTINGS elsewhere.
UINTN
AppSize (VOID);

UINTN
AppSettingsSize (VOID);

// Keys
//
// Firmware key protocols report presses but not releases. USB keyboards
// repeat a held key and handheld keypads don't (the Mangmi Pocket Max sends
// one event per press). So every action is a single press, and holding a
// key only scrolls where the firmware repeats it. A key counts as held
// while its reports arrive less than KEY_GAP_MS apart.

// Reports closer together than this are one key held.
#define KEY_GAP_MS 200
// A held Up or Down starts repeating after the delay, then this often.
#define KEY_REPEAT_DELAY_MS 400
#define KEY_REPEAT_EVERY_MS 120

// The held key, if any, with the times (ms) of its reports and last input.
typedef struct _KEYS {
  BOOLEAN Held;
  INPUT   Key;
  UINT64  Start, Last, LastEmit;
} KEYS;

// Takes a key report. Returns TRUE with an input in Out when it makes one.
BOOLEAN
KeysPress (
  IN OUT KEYS *Keys,
  IN INPUT     Key,
  IN UINT64    Now,
  OUT INPUT   *Out
);

// Call on a poll with no key report, so a released key stops being held.
VOID
KeysIdle (
  IN OUT KEYS *Keys,
  IN UINT64    Now
);

#endif
