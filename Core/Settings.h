/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Settings - Loader settings and the rules for changing them.

  Each setting has its own small variable, so a value with the wrong size or
  an unknown code falls back to the default without affecting the others.
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

#define SETTINGS_MAX_MODEL_LEN 96
#define MODEL_LEN              SETTINGS_MAX_MODEL_LEN

// Platform features that decide which choices are offered.
typedef struct _SETTINGS_CAPS {
  BOOLEAN Android;
  BOOLEAN UsbHost;
  // Linux is installed on internal storage.
  BOOLEAN CfwOnInternal;
  // Opened from inside the ABL, so there is no menu timeout.
  BOOLEAN Embedded;
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
  BOOLEAN     OffmodeCharging;
  UINT16      Rotation;
  // Seconds the menu waits before booting. 0 boots at once.
  UINT8 MenuTimeoutS;
  // Empty when not set. A stored DeviceModel containing a NUL is cut there.
  CHAR8  Model[MODEL_LEN + 1];
  UINT16 ModelRotation;
  // Draw the card's /logo/ pictures, or the OpenABL wordmark, at boot.
  BOOLEAN CustomLogo;
  // The menu's language, a LANG_ID from Ui/Lang.h.
  UINT8 Language;
} SETTINGS;

// Languages the menu offers (LANG_COUNT), counted from Ui/Lang/Languages.h.
#define SETTINGS_LANGUAGE_ONE(Id, Symbol) +1
#define SETTINGS_LANGUAGES                (0 LANG_LIST (SETTINGS_LANGUAGE_ONE))

// Menu timeout choices, in seconds.
extern CONST UINT8 gSettingsTimeoutChoices[4];

#define SETTINGS_KEY_BOOT_MODE        "BootMode"
#define SETTINGS_KEY_ABL_MODE         "AblMode"
#define SETTINGS_KEY_ALT_VOLUME_UP    "AltVolumeUp"
#define SETTINGS_KEY_BOOT_SOURCE      "BootSource"
#define SETTINGS_KEY_OFFMODE_CHARGING "OffmodeCharging"
#define SETTINGS_KEY_ROTATION         "DisplayRotation"
#define SETTINGS_KEY_MENU_TIMEOUT     "MenuTimeout"
#define SETTINGS_KEY_MODEL            "DeviceModel"
#define SETTINGS_KEY_MODEL_ROTATION   "DeviceModelRotation"
// Set by the menu: the model that DeviceModelRotation belongs to.
#define SETTINGS_KEY_MODEL_ROTATION_FOR "DeviceModelRotationFor"
// Set by the menu to open the model list once, at the next boot.
#define SETTINGS_KEY_SHOW_MODEL_MENU "ShowModelMenu"
// Set by the menu before restarting into fastboot: why nothing could boot
// (MENU_START_RESULT_*). Deleted once read.
#define SETTINGS_KEY_BOOT_FAILURE "BootFailure"
// DISPLAY_HANDOVER_* bits from Core/Panel.h. Absent means 0.
#define SETTINGS_KEY_DISPLAY_QUIESCE "DisplayQuiesce"
#define SETTINGS_KEY_CUSTOM_LOGO     "CustomLogo"
#define SETTINGS_KEY_LANGUAGE        "MenuLanguage"
// Experimental, and no menu sets it yet: 1 starts USB host in the same
// boot as fastboot instead of restarting. On the glink SoCs XBL died when a
// drive was attached after the gadget had run.
#define SETTINGS_KEY_USB_HOST_LIVE "UsbHostLive"

VOID
SettingsDefault (OUT SETTINGS *Settings);

// Switches between Linux and Android. Android boots from internal storage.
// Back in Linux, internal storage stays only if Linux is installed there.
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

BOOLEAN
SettingsModelVisible (IN CONST SETTINGS *Settings);

// Cycles Auto -> 0 -> 90 -> 180 -> 270 -> Auto.
VOID
SettingsNextRotation (IN OUT SETTINGS *Settings);

// Returns the rotation to apply now, in degrees clockwise.
UINT16
SettingsEffectiveRotation (IN CONST SETTINGS *Settings);

// Moves to the next timeout choice. An unlisted value counts as the first.
VOID
SettingsNextTimeout (IN OUT SETTINGS *Settings);

// A stored variable. An empty value deletes it.
typedef struct _SETTINGS_VAR {
  CONST CHAR8 *Name;
  UINT8        Value[SETTINGS_MAX_MODEL_LEN];
  UINTN        Len;
} SETTINGS_VAR;

#define SETTINGS_VARS 11

// Converts the settings to variables. Returns the count, SETTINGS_VARS.
UINTN
SettingsToVars (
  IN CONST SETTINGS *Settings,
  OUT SETTINGS_VAR   Out[SETTINGS_VARS]
);

// Returns the variables that differ between Settings and Next.
UINTN
SettingsChangedVars (
  IN CONST SETTINGS *Settings,
  IN CONST SETTINGS *Next,
  OUT SETTINGS_VAR   Out[SETTINGS_VARS]
);

// Reads variable Name into the empty Out. The caller frees Out either way.
typedef BOOLEAN
SETTINGS_GET (
  IN VOID            *Context,
  IN CONST CHAR8     *Name,
  IN OUT OABL_BUFFER *Out
);

// Reads the settings from variables. Defaults fills in bad or missing ones.
VOID
SettingsFromVars (
  IN CONST SETTINGS *Defaults,
  IN SETTINGS_GET   *Get,
  IN VOID           *Context OPTIONAL,
  OUT SETTINGS      *Out
);

#endif
