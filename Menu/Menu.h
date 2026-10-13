/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Menu - declarations shared by the OpenABL menu.

  Main.c is the entry point and runs the boot, Menu.c is the menu itself,
  and BootSplash.c draws the boot splash.
*/

#ifndef __MENU_MENU_H__
#define __MENU_MENU_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Core/Settings.h"
#include "Efi/MenuHost.h"
#include "Efi/Serial.h"

// The ABL's menu-host protocol, set once at start so anything can log.
extern MENU_HOST gMenuHost;
extern BOOLEAN   gMenuHostSet;

// gFastbootUp is set while fastboot runs. gUsbLive is set while USB host is
// up for the menu, with fastboot's gadget paused.
extern BOOLEAN gFastbootUp;
extern BOOLEAN gUsbLive;

// Writes a line, prefixed "openabl-menu: ", to the ABL's debug log. The log may
// be shown on screen in verbose mode, so log events, not every frame or key.
VOID
MenuLog (
  IN CONST CHAR8 *Fmt,
  ...
)
  __attribute__ ((format (
    printf,
    1,
    2
  )));

// MenuLog as a LOG_SINK, for the Efi modules' log lines.
extern CONST LOG_SINK gMenuEfiLog;

#define MENU_STATUS_SIZE 24

// Writes Status to Out as its low bits, prefixed "error " for errors, so
// EFI_NOT_FOUND gives "error 14". Returns Out.
CONST CHAR8 *
MenuStatus (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[MENU_STATUS_SIZE]
);

// MenuStatus into a buffer that lasts as long as the enclosing block.
#define MENU_STATUS(Status) MenuStatus ((Status), (CHAR8[MENU_STATUS_SIZE]){0})

// Reads a settings variable. Returns FALSE if unset. Caller frees Out.
BOOLEAN
MenuVarGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
);

// Writes a settings variable without flushing. An empty value deletes it,
// and deleting a missing variable is not an error.
EFI_STATUS
MenuVarPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
);

// Returns TRUE if Name holds the single byte Value.
BOOLEAN
MenuVarIs (
  IN CONST CHAR8 *Name,
  IN UINT8        Value
);

// Commits written variables to flash (XBL's FlushVariableNV).
VOID
MenuFlush (VOID);

// Returns what the ABL supports, and whether Linux is on internal storage.
SETTINGS_CAPS
MenuCapsOf (IN CONST MENU_HOST_STATE *State);

// Reads the settings from their variables, if they have been moved there.
BOOLEAN
MenuStoredSettings (
  IN CONST MENU_HOST_STATE *State,
  IN CONST SETTINGS_CAPS   *Caps,
  OUT SETTINGS             *Out
);

// Fills Out with the settings from the ABL's state.
VOID
MenuSettingsOf (
  IN CONST MENU_HOST_STATE *State,
  OUT SETTINGS             *Out
);

// Reads the rotation choice, and Model's cached panel rotation if saved.
VOID
MenuLoadPrefs (
  IN CONST CHAR8 *Model,
  OUT UINT16     *Rotation,
  OUT UINT16     *ModelRotation
);

// Saves the rotation choice, the model and its panel rotation, and flushes.
VOID
MenuSavePrefs (IN CONST SETTINGS *Settings);

typedef enum {
  MENU_END_LEFT,       // the ABL asked the menu to leave
  MENU_END_START_HERE, // Start Linux was chosen, booting without a restart
  MENU_END_FAILED,     // the menu couldn't run
} MENU_END_KIND;

typedef struct _MENU_END {
  MENU_END_KIND Kind;
  EFI_STATUS    Status;
} MENU_END;

// Runs the menu until the ABL asks it to leave or Start Linux is chosen.
// StartModels opens the model list. CameBack is how the last Start Linux
// returned (MENU_START_RESULT_*).
MENU_END
MenuRun (
  IN CONST MENU_HOST *Host,
  IN BOOLEAN          StartModels,
  IN UINT8            CameBack
);

// Brings USB host up for the menu, pausing fastboot's gadget. If no drive
// comes up, undoes that and returns EFI_NOT_FOUND.
EFI_STATUS
MenuLiveUsbHostUp (IN CONST MENU_HOST *Host);

// Takes USB host down and resumes fastboot's gadget.
EFI_STATUS
MenuLiveUsbHostDown (IN CONST MENU_HOST *Host);

// What booting needs from the ABL's state and the settings.
typedef struct _MENU_BOOT_CONTEXT {
  BOOT_SOURCE Source;
  BOOLEAN     CfwOnInternal;
  // Volume Up was held with "Volume up at boot" set to Linux (external).
  BOOLEAN AltVolumeBoot;
  CHAR8   Model[MODEL_LEN + 1];
  BOOLEAN DevMode;
  BOOLEAN VmEnabled;
  // Display preferences, for the splash's orientation.
  UINT16 Rotation, ModelRotation;
  // The ABL's version, or the program's without one.
  CHAR8 Version[16];
} MENU_BOOT_CONTEXT;

// Lists the allowed drives in boot source order. Caller frees *Volumes.
VOID
MenuBootVolumes (
  IN CONST MENU_BOOT_CONTEXT *Ctx,
  OUT EFI_HANDLE            **Volumes,
  OUT UINTN                  *Count
);

// Draws the boot splash over the firmware's logo.
VOID
MenuBootSplash (
  IN CONST MENU_HOST         *Host,
  IN CONST MENU_BOOT_CONTEXT *Ctx
);

// Shows the animation's last frame now, if it is still running, and stops
// its timer. Call it before the boot returns: an ABL that loads a fresh
// copy of us for each boot unloads it on return, which would leave the
// timer pointing into a freed image.
VOID
MenuAnimFinish (VOID);

#endif
