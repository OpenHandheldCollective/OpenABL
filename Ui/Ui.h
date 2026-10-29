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

#include "Core/Gpt.h"
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

typedef enum {
  COMMAND_NONE,
  COMMAND_BOOT,       // try everything bootable, in order
  COMMAND_BOOT_ENTRY, // Arg is the entry index
  COMMAND_REBOOT,     // Arg is a REBOOT_TARGET
  COMMAND_POWER_OFF,
  COMMAND_SAVE_SETTINGS,
  COMMAND_SCAN_MODELS,
  COMMAND_UNINSTALL_CFW,
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
  BOOT_ERROR_ANDROID_FAILED, // booting Android (boot_a/b) returned
  BOOT_ERROR_WIPE_BY_HAND,   // Linux removed, but Android isn't the boot mode
  BOOT_ERROR_UNINSTALL,      // removing Linux failed; ErrorDetail says why
} BOOT_ERROR;

// What the view draws

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
  ITEM_START,
  ITEM_ENTRIES,
  ITEM_RESTART_LOADER,
  ITEM_RECOVERY,
  ITEM_EDL,
  ITEM_OTHER_SLOT,
  ITEM_LANGUAGE,
  ITEM_BOOT_MODE,
  ITEM_ABL_MODE,
  ITEM_ALT_VOLUME_UP,
  ITEM_BOOT_SOURCE,
  ITEM_CHARGING,
  ITEM_ROTATION,
  ITEM_CUSTOM_LOGO,
  ITEM_TIMEOUT,
  ITEM_MODEL,
  ITEM_UNINSTALL,
  ITEM_DEVICE_INFO,
  ITEM_LOG,
  ITEM_USB_LIVE,
  ITEM_QUIESCE,
  ITEM_PANEL_LINES,
  ITEM_POWER_OFF,
} ITEM;

typedef enum { TONE_NORMAL, TONE_PRIMARY, TONE_DANGER } TONE;

// The mark at the end of a row. CYCLE is a selector (Select cycles the
// value) and NAVIGATE a chevron (Select opens another screen).
typedef enum { TRAILING_NONE, TRAILING_CYCLE, TRAILING_NAVIGATE } TRAILING;

typedef struct _ROW {
  CONST CHAR8 *Header; // NULL for an item
  ITEM         Item;
} ROW;

typedef struct _ITEM_VIEW {
  ICON         Icon;
  CONST CHAR8 *Label;
  CHAR8        Value[MODEL_LEN + 16]; // "" for none
  TONE         Tone;
  TRAILING     Trailing;
} ITEM_VIEW;

typedef enum {
  SCREEN_MENU,
  SCREEN_SCANNING,
  SCREEN_MODELS,
  SCREEN_ENTRIES,
  SCREEN_LANGUAGES,
  SCREEN_CONFIRM,
  SCREEN_BOOTING,
  SCREEN_ERROR,
  SCREEN_LINES,        // the device info, or the log
  SCREEN_UNINSTALLING, // while Linux is removed; takes no input
  SCREEN_RESTARTING,   // into recovery after an uninstall; takes no input
} SCREEN;

// An uninstall asks twice: the partitions it erases, then once more.
typedef enum {
  CONFIRM_UNINSTALL,
  CONFIRM_UNINSTALL_FINAL,
  CONFIRM_EDL
} CONFIRM;

// Scroll is the first row the list shows.
typedef struct _LIST_STATE {
  UINTN Focus;
  UINTN Scroll;
} LIST_STATE;

#define MAX_ROWS    40
#define MAX_MODELS  64
#define MAX_ENTRIES 32
#define MAX_LINES   512
#define MAX_CFW_PARTS 8

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
} ENTRY_KIND;

// A boot entry the platform found. Start boots the first.
typedef struct _ENTRY_VIEW {
  CHAR8      Title[96];
  CHAR8      Version[64]; // "" for none
  CHAR8      Drive[64];   // the drive's name
  ENTRY_KIND Kind;
  BOOLEAN    Removable; // the drive is removable
  BOOLEAN    Bad;       // no tries left (Automatic Boot Assessment)
} ENTRY_VIEW;

// Size for AppEntryDetail's text.
#define ENTRY_DETAIL_SIZE 192

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
  // On the restarting screen: whether recovery was asked to wipe data.
  BOOLEAN    Wiped;

  ITEM       FocusItem;
  LIST_STATE Menu;

  MODEL      Models[MAX_MODELS];
  UINTN      ModelCount;
  LIST_STATE ModelList;

  // Linux's partitions on internal storage, listed before an uninstall.
  GPT_CFW_PART CfwParts[MAX_CFW_PARTS];
  UINTN        CfwPartCount;

  ENTRY_VIEW Entries[MAX_ENTRIES];
  UINTN      EntryCount;
  LIST_STATE EntryList;

  // Rows: each language in LANG_LIST order, then "Back".
  LIST_STATE LanguageList;

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

// Sets the partitions the uninstall confirmation lists, at most
// MAX_CFW_PARTS of them.
VOID
AppSetCfwParts (
  IN OUT APP                *A,
  IN CONST GPT_CFW_PART     *Parts OPTIONAL,
  IN UINTN                   Count
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

// After Linux was removed. Restarting shows the restarting screen, which
// ignores input until the platform restarts; Wiped says whether recovery
// was asked to wipe Android's data. Without a restart, the error screen
// says to set the boot mode and wipe by hand.
VOID
AppOnUninstalled (
  IN OUT APP *A,
  IN BOOLEAN  Restarting,
  IN BOOLEAN  Wiped
);

// After removing Linux failed: the error screen, with Why as its message.
VOID
AppOnUninstallFailed (
  IN OUT APP     *A,
  IN CONST CHAR8 *Why
);

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

// Writes an entry's version, kind and drive in the current language.
VOID
AppEntryDetail (
  IN CONST ENTRY_VIEW *Entry,
  OUT CHAR8           *Out,
  IN UINTN             Cap
);

// Writes the line under the boot screen's title: the entry's detail, or
// where the boot looks.
VOID
AppBootDetail (
  IN CONST APP *A,
  OUT CHAR8    *Out,
  IN UINTN      Cap
);

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
// repeat a held key. Qualcomm's keypad reports a press once, but reports a
// key that is still down again after a reset of the input (Efi/Input.c
// resets it after each read, as Qualcomm's own menu does). So a held key
// arrives as a stream of reports, and counts as held while they arrive less
// than KEY_GAP_MS apart. Every action is a single press; only Up and Down
// repeat while held, to scroll.

// Reports closer together than this are one key held.
#define KEY_GAP_MS 200
// A held Up or Down starts repeating after the delay, then this often.
#define KEY_REPEAT_DELAY_MS 400
#define KEY_REPEAT_EVERY_MS 120

// The held key, if any, with the times (ms) of its reports and last input.
// A muted key makes no input until it is released.
typedef struct _KEYS {
  BOOLEAN Held;
  BOOLEAN Muted;
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

// Takes a report of a key that was already down, as one held to open the
// menu: it is held but makes no input until released.
VOID
KeysMute (
  IN OUT KEYS *Keys,
  IN INPUT     Key,
  IN UINT64    Now
);

// Call on a poll with no key report, so a released key stops being held.
VOID
KeysIdle (
  IN OUT KEYS *Keys,
  IN UINT64    Now
);

#endif
