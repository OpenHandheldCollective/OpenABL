/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Settings - the loader's settings, the rules for changing them, and, on
  the device, reading and saving them.

  Each setting is its own entry in misc's settings store
  (Core/SettingsStore), and one at its default isn't stored. The charger
  screen is the exception: it is Qualcomm's devinfo flag.
*/

#ifndef __CORE_SETTINGS_H__
#define __CORE_SETTINGS_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Ui/Lang/Languages.h"

typedef enum { BOOT_MODE_LINUX, BOOT_MODE_ANDROID } BOOT_MODE;

typedef enum { ABL_MODE_SILENT, ABL_MODE_VERBOSE } ABL_MODE;

typedef enum {
  BOOT_SOURCE_AUTO,
  BOOT_SOURCE_INTERNAL,
  BOOT_SOURCE_SD,
  BOOT_SOURCE_USB
} BOOT_SOURCE;

// Rotation is degrees clockwise, or ROTATION_AUTO to follow the devicetree.
// ModelRotation is ROTATION_NONE when the devicetree gives no rotation.
#define ROTATION_AUTO 0xffffu
#define ROTATION_NONE 0xffffu

// Longest device model, in bytes, without the NUL.
#define SETTINGS_MAX_MODEL_LEN 96

// Languages the menu offers (LANG_COUNT), counted from Ui/Lang/Languages.h.
#define SETTINGS_LANGUAGE_ONE(Id, Symbol) +1
#define SETTINGS_LANGUAGES                (0 LANG_LIST (SETTINGS_LANGUAGE_ONE))

// Platform features that decide which choices are offered.
typedef struct _SETTINGS_CAPS {
  BOOLEAN Android;
  BOOLEAN UsbHost;
  // Linux is installed on internal storage.
  BOOLEAN CfwOnInternal;
  // The A/B slots are unbootable. Offer the ABL's "Boot to Alternate Slot".
  BOOLEAN AlternateSlot;
  BOOLEAN DeviceInfo;
  // The ABL's log can be shown.
  BOOLEAN Log;
} SETTINGS_CAPS;

typedef struct _SETTINGS {
  BOOT_MODE BootMode;
  ABL_MODE  AblMode;
  // Volume up at boot starts external Linux instead of Android or Recovery.
  BOOLEAN     AltVolumeUp;
  BOOT_SOURCE BootSource;
  // Qualcomm's charger screen flag. It lives in devinfo, not in misc.
  BOOLEAN OffmodeCharging;
  UINT16  Rotation;
  // Empty when not set. A stored DeviceModel containing a NUL is cut there.
  CHAR8  Model[SETTINGS_MAX_MODEL_LEN + 1];
  UINT16 ModelRotation;
  // ModelRotation was looked up for Model, even if it is ROTATION_NONE.
  // Until it is, the menu looks it up again at each start.
  BOOLEAN ModelRotationKnown;
  // Draw OpenABL's boot splash (the card's /logo/ pictures, or the
  // wordmark). Off keeps the manufacturer's logo up.
  BOOLEAN CustomLogo;
  // The menu's language, a LANG_ID from Ui/Lang.h. Stored as its Symbol.
  UINT8 Language;

  // The Debug section's switches, all off by default.

  // Experimental: start USB host in the same boot as fastboot instead of
  // restarting. On the glink SoCs XBL died when a drive was attached after
  // the gadget had run.
  BOOLEAN UsbHostLive;
  // Turn the display off through XBL's display power protocol at
  // ExitBootServices, even when the devicetree keeps the splash.
  BOOLEAN DisplayOff;
  // Turn the panel lines off around that: the backlight before the
  // display, the supplies after it.
  BOOLEAN PanelLinesOff;
  // The boot's log lines also go to the screen, which isn't cleared before
  // the image starts, to see where a boot stops.
  BOOLEAN BootLog;
  // Log the SD slot's state before and after the SdccDxe patches.
  BOOLEAN SdDiagnostics;
} SETTINGS;

// Names in misc's store

#define SETTINGS_KEY_BOOT_MODE          "BootMode"
#define SETTINGS_KEY_ABL_MODE           "AblMode"
#define SETTINGS_KEY_ALT_VOLUME_UP      "AltVolumeUp"
#define SETTINGS_KEY_BOOT_SOURCE        "BootSource"
#define SETTINGS_KEY_ROTATION           "DisplayRotation"
#define SETTINGS_KEY_MODEL              "DeviceModel"
#define SETTINGS_KEY_MODEL_ROTATION     "DeviceModelRotation"
#define SETTINGS_KEY_MODEL_ROTATION_FOR "DeviceModelRotationFor"
#define SETTINGS_KEY_CUSTOM_LOGO        "CustomLogo"
#define SETTINGS_KEY_LANGUAGE           "MenuLanguage"
#define SETTINGS_KEY_USB_HOST_LIVE      "UsbHostLive"
#define SETTINGS_KEY_DISPLAY_OFF        "DisplayOff"
#define SETTINGS_KEY_PANEL_LINES_OFF    "PanelLinesOff"
#define SETTINGS_KEY_BOOT_LOG           "BootLog"
#define SETTINGS_KEY_SD_DIAGNOSTICS     "SdDiagnostics"

// The menu's one-off flags: each is set for the next boot and cleared once
// used.

// Opens the model list once, at the next boot.
#define SETTINGS_FLAG_SHOW_MODEL_MENU "ShowModelMenu"
// Why nothing could boot (MENU_START_RESULT_*), kept across the restart
// into fastboot.
#define SETTINGS_FLAG_BOOT_FAILURE "BootFailure"
// What Boot entries picked for the next Start, kept across a restart: the
// drive's device path length (Le16), the path, then the entry's id.
#define SETTINGS_FLAG_BOOT_ENTRY_ONCE "BootEntryOnce"

// Rules

VOID
SettingsDefault (OUT SETTINGS *Settings);

// Returns the language with this LANG_LIST Symbol, or SETTINGS_LANGUAGES.
UINT8
SettingsLanguageFromSymbol (
  IN CONST UINT8 *Symbol,
  IN UINTN        Len
);

// Switches between Linux and Android.
VOID
SettingsToggleBootMode (
  IN OUT SETTINGS        *Settings,
  IN CONST SETTINGS_CAPS *Caps
);

// Cycles Auto, Internal, SD card, USB, skipping what the platform lacks.
VOID
SettingsNextBootSource (
  IN OUT SETTINGS        *Settings,
  IN CONST SETTINGS_CAPS *Caps
);

// Hidden for Android, and when the SD card is the only real choice.
BOOLEAN
SettingsBootSourceVisible (
  IN CONST SETTINGS      *Settings,
  IN CONST SETTINGS_CAPS *Caps
);

// Hidden for Android.
BOOLEAN
SettingsModelVisible (IN CONST SETTINGS *Settings);

// Cycles Auto, 0, 90, 180, 270, Auto.
VOID
SettingsNextRotation (IN OUT SETTINGS *Settings);

// Returns the rotation to apply now, in degrees clockwise.
UINT16
SettingsEffectiveRotation (IN CONST SETTINGS *Settings);

// Reading and saving, on the device

#ifdef OABL_UEFI

// Only EFI_STATUS, so the UI including this gets no other EFI headers.
#include <Uefi/UefiBaseType.h>

#include "Efi/Serial.h"

// MENU_HOST is Efi/MenuHost.h's, which includes this header.
struct _MENU_HOST;

typedef enum {
  SETTINGS_SAVED,
  // A setting wasn't saved.
  SETTINGS_NOT_SAVED,
  // The device model wasn't saved.
  SETTINGS_MODEL_NOT_SAVED,
  // The ABL didn't answer, so nothing was saved.
  SETTINGS_NO_ABL,
} SETTINGS_SAVE_RESULT;

// Reads misc's store, creating it the first time with any device model an
// older loader stored, and logs to Log from now on. Run early at each boot.
VOID
SettingsSetUp (IN LOG_SINK Log);

// Reads misc now, so SettingsLoad works at ExitBootServices.
VOID
SettingsPreload (VOID);

// Reads the settings, with defaults for bad or missing ones. The charger
// screen is left off: MenuHostState fills it in from devinfo.
VOID
SettingsLoad (OUT SETTINGS *Out);

// Saves what changed in one write to misc, and the charger screen through
// the ABL. A setting at its default is deleted. Stored gets what is stored
// afterwards, except with SETTINGS_NO_ABL.
SETTINGS_SAVE_RESULT
SettingsSave (
  IN CONST struct _MENU_HOST *Host,
  IN CONST SETTINGS          *Want,
  OUT SETTINGS               *Stored
);

// Reads flag Name. FALSE if unset. Caller frees Out.
BOOLEAN
SettingsFlagGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
);

// TRUE if flag Name holds the single byte Value.
BOOLEAN
SettingsFlagIs (
  IN CONST CHAR8 *Name,
  IN UINT8        Value
);

// Sets flag Name and writes misc. Size 0 clears it.
EFI_STATUS
SettingsFlagSet (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
);

#endif

#endif
