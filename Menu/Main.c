/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Main - entry point of the OpenABL menu program.

  The ABL starts us with its menu-host protocol installed: FLAG_EARLY
  patches XBL's drivers and stays loaded, FLAG_MAIN runs the whole boot.
*/

#include "Menu/Menu.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/BootDecision.h"
#include "Core/DeviceTree.h"
#include "Core/UsbHostRegs.h"
#include "Efi/Abl.h"
#include "Efi/BootImage.h"
#include "Efi/Efi.h"
#include "Efi/DefaultFiles.h"
#include "Efi/DisplayHandover.h"
#include "Efi/Handover.h"
#include "Efi/Input.h"
#include "Efi/Qcom.h"
#include "Efi/Quirks.h"
#include "Efi/Variables.h"
#include "Efi/UsbHost.h"
#include "Efi/Files.h"
#ifdef OABL_SYSFONT
#include "Efi/SysFont.h"
#endif
#ifdef OABL_STANDARD_BOOT
#include "Boot/Bli.h"
#include "Boot/Booter.h"
#endif

MENU_HOST gMenuHost;
BOOLEAN   gMenuHostSet;
BOOLEAN   gFastbootUp;
BOOLEAN   gUsbLive;

VOID
MenuLog (
  IN CONST CHAR8 *Fmt,
  ...
)
{
  OABL_STRING Line = {0};
  va_list     Args;

  if (!gMenuHostSet) {
    return;
  }
  StringAppend (&Line, "openabl-menu: ");
  va_start (Args, Fmt);
  StringVPrint (&Line, Fmt, Args);
  va_end (Args);
  MenuHostLog (&gMenuHost, StringGet (&Line));
  StringFree (&Line);
}

STATIC VOID
LogToSink (
  IN VOID        *Context OPTIONAL,
  IN CONST CHAR8 *Line
)
{
  (VOID)Context;
  MenuLog ("%s", Line);
}

CONST LOG_SINK gMenuEfiLog = {LogToSink, NULL};

CONST CHAR8 *
MenuStatus (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[MENU_STATUS_SIZE]
)
{
  SPrint (
    Out, MENU_STATUS_SIZE, "%s%llu", EFI_ERROR (Status) ? "error " : "",
    (UINT64)(Status & 0xffff)
  );
  return Out;
}

// Booting

STATIC BOOLEAN
BootContextOf (
  IN CONST MENU_HOST    *Host,
  OUT MENU_BOOT_CONTEXT *Ctx
)
{
  MENU_HOST_STATE State;
  SETTINGS_CAPS   Caps;
  SETTINGS        Settings;

  if (EFI_ERROR (MenuHostState (Host, &State))) {
    return FALSE;
  }
  Caps = MenuCapsOf (&State);
  if (!MenuStoredSettings (&State, &Caps, &Settings)) {
    MenuSettingsOf (&State, &Settings);
  }
  *Ctx = (MENU_BOOT_CONTEXT){
    .Source        = Settings.BootSource,
    .CfwOnInternal = Caps.CfwOnInternal,
    .AltVolumeBoot = State.AltVolumeBoot,
    .DevMode       = Settings.AblMode == ABL_MODE_VERBOSE,
    .VmEnabled     = (State.Flags & OPENABL_MENU_STATE_VM_ENABLED) != 0,
  };
  AsciiStrCopy (Ctx->Model, sizeof (Ctx->Model), Settings.Model);
  MenuLoadPrefs (
    Settings.Model, &Ctx->Rotation, &Ctx->ModelRotation, &Settings.Language
  );
  AsciiStrCopy (Ctx->Version, sizeof (Ctx->Version), MENU_VERSION);
  if (State.Version[0]) {
    CHAR8 *Version = MenuHostText (State.Version, sizeof (State.Version));

    if (Version) {
      AsciiStrCopy (Ctx->Version, sizeof (Ctx->Version), Version);
      FreePool (Version);
    }
  }
  return TRUE;
}

VOID
MenuBootVolumes (
  IN CONST MENU_BOOT_CONTEXT *Ctx,
  OUT EFI_HANDLE            **Volumes,
  OUT UINTN                  *Count
)
{
  VOLUMES_HANDLE_LIST Handles = {0};

  VolumesAllowed (
    Ctx->Source, Ctx->CfwOnInternal, Ctx->AltVolumeBoot, &Handles
  );
  *Volumes = Handles.Items;
  *Count   = Handles.Count;
}

// Installs the model's devicetree and reclaims memory just before an image
// starts. If it returns, ReturnedToMenu reserves XBL's staging region again.
STATIC VOID
HandoverBefore (
  IN CONST MENU_BOOT_CONTEXT *Ctx,
  IN EFI_HANDLE               Fs,
  IN BOOLEAN                  OwnDevicetree
)
{
  HANDOVER_PREPARE Prepare = {
    .Fs            = Fs,
    .OwnDevicetree = OwnDevicetree,
    .Model         = Ctx->Model,
    .DevMode       = Ctx->DevMode,
    .Vm            = Ctx->VmEnabled,
    .Version       = Ctx->Version,
  };

  HandoverPrepare (&Prepare, gMenuEfiLog);
}

STATIC VOID
FilesBeforeStart (
  IN VOID      *Context,
  IN EFI_HANDLE Fs
)
{
  HandoverBefore (Context, Fs, FALSE);
}

STATIC VOID
ReturnedToMenu (IN VOID *Context)
{
  (VOID)Context;
  HandoverBootReturned (gMenuEfiLog);
}

STATIC VOID
FilesLog (
  IN VOID        *Context,
  IN CONST CHAR8 *Line
)
{
  (VOID)Context;
  MenuLog ("%s", Line);
}

STATIC DEFAULT_FILES_HOST
FilesHost (IN CONST MENU_BOOT_CONTEXT *Ctx)
{
  return (DEFAULT_FILES_HOST){FilesBeforeStart, ReturnedToMenu, FilesLog,
                              (VOID *)Ctx};
}

#ifdef OABL_STANDARD_BOOT
// Returns none. The model's devicetree is installed in BeforeStart, from
// the image's drive.
STATIC BOOLEAN
StdModelDevicetree (
  IN VOID         *Context,
  OUT OABL_BUFFER *Out
)
{
  (VOID)Context;
  (VOID)Out;
  return FALSE;
}

// Returns the installed devicetree's compatible, or NULL. Caller frees it.
STATIC CHAR8 *
StdModelCompatible (IN VOID *Context)
{
  OABL_BUFFER Dt = {0};
  DT_INFO     Info;
  CHAR8      *Compatible = NULL;

  (VOID)Context;
  if (HandoffInstalledDevicetree (&Dt) && DtInspect (Dt.Data, Dt.Len, &Info)) {
    Compatible      = Info.Compatible;
    Info.Compatible = NULL;
    DtInfoFree (&Info);
  }
  BufferFree (&Dt);
  return Compatible;
}

STATIC VOID
StdBeforeStart (
  IN VOID      *Context,
  IN EFI_HANDLE Fs,
  IN BOOLEAN    OwnDevicetree
)
{
  HandoverBefore (Context, Fs, OwnDevicetree);
}

// Boots the standard entries and boot options on Volumes. Returns
// EFI_SUCCESS once one has run, or EFI_NOT_FOUND if there is nothing.
STATIC EFI_STATUS
StandardBoot (
  IN CONST MENU_HOST         *Host,
  IN CONST EFI_HANDLE        *Volumes,
  IN UINTN                    Count,
  IN CONST MENU_BOOT_CONTEXT *Ctx
)
{
  STATIC CHAR8 Info[96];
  BOOT_SCOPE   Scope    = {.Only         = TRUE,
                           .Volumes      = Volumes,
                           .VolumeCount  = Count,
                           .DefaultFiles = FALSE};
  BOOT_HOST    BootHost = {
    .Context         = (VOID *)Ctx,
    .ModelDevicetree = StdModelDevicetree,
    .ModelCompatible = StdModelCompatible,
    .BeforeStart     = StdBeforeStart,
    .AfterReturn     = ReturnedToMenu,
  };
  BOOTER     Booter;
  CHAR8     *Why = NULL;
  EFI_STATUS Status;

  MenuLog ("standard boot on %llu drive(s)", (UINT64)Count);
  // LoaderInfo names the ABL, since that is the loader the OS knows.
  AsciiStrCopy (Info, sizeof (Info), "OpenABL ");
  AsciiStrAppend (Info, sizeof (Info), Ctx->Version);
  BooterNew (&Booter, Info, ClockNowUs (), &Scope);
  if (!Booter.Plan.Count) {
    MenuLog ("no standard boot entries");
    BooterFree (&Booter);
    return EFI_NOT_FOUND;
  }
  for (UINTN Index = 0; Index < Booter.Plan.Count; Index++) {
    CHAR8 *Description = BooterDescribe (&Booter, &Booter.Plan.Items[Index]);
    MenuLog ("plan %llu: %s", (UINT64)Index, Description ? Description : "");
    FreePool (Description);
  }
  switch (BooterBoot (&Booter, &BootHost, FALSE, 0, &Why)) {
  case BOOT_FAILURE_OK:
    MenuLog ("the booted image returned");
    Status = EFI_SUCCESS;
    break;
  case BOOT_FAILURE_NOTHING_FOUND:
    Status = EFI_NOT_FOUND;
    break;
  default:
    MenuLog ("standard boot failed: %s", Why ? Why : "");
    Status = EFI_LOAD_ERROR;
  }
  FreePool (Why);
  BooterFree (&Booter);
  return Status;
}
#else
STATIC EFI_STATUS
StandardBoot (
  IN CONST MENU_HOST         *Host,
  IN CONST EFI_HANDLE        *Volumes,
  IN UINTN                    Count,
  IN CONST MENU_BOOT_CONTEXT *Ctx
)
{
  (VOID)Host, (VOID)Volumes, (VOID)Count, (VOID)Ctx;
  MenuLog ("built without standard boot");
  return EFI_UNSUPPORTED;
}
#endif

// Finds the first boot image on Volumes and hands it to the ABL to start.
// Returns FALSE if there is none or the ABL refused it.
STATIC BOOLEAN
HandOverBootImageFrom (
  IN CONST MENU_HOST         *Host,
  IN CONST MENU_BOOT_CONTEXT *Ctx,
  IN CONST EFI_HANDLE        *Volumes,
  IN UINTN                    Count,
  OUT BOOT_IMAGE_FOUND       *Image
)
{
  DEFAULT_FILES_HOST Files = FilesHost (Ctx);
  EFI_STATUS         Status;

  if (!BootImageFind (Volumes, Count, &Files, Image)) {
    MenuLog ("no boot image");
    return FALSE;
  }
  Status = MenuHostSetBootImage (Host, Image->Buffer, Image->Size);
  if (Status != EFI_SUCCESS) {
    MenuLog ("handing the ABL the boot image: %s", MENU_STATUS (Status));
    // The ABL didn't take BootImageFind's buffer, so free it here.
    gBS->FreePool (Image->Buffer);
    return FALSE;
  }
  return TRUE;
}

// HandOverBootImageFrom the drives the boot source allows.
STATIC BOOLEAN
HandOverBootImage (
  IN CONST MENU_HOST         *Host,
  IN CONST MENU_BOOT_CONTEXT *Ctx,
  OUT BOOT_IMAGE_FOUND       *Image
)
{
  EFI_HANDLE *Volumes;
  UINTN       Count;
  BOOLEAN     Handed;

  MenuBootVolumes (Ctx, &Volumes, &Count);
  Handed = HandOverBootImageFrom (Host, Ctx, Volumes, Count, Image);
  FreePool (Volumes);
  return Handed;
}

#ifdef OABL_STANDARD_BOOT
typedef enum {
  PICK_NONE,    // nothing was picked
  PICK_STARTED, // it started, or its boot image went to the ABL
  PICK_GONE,    // its drive is gone; nothing else may start instead
  PICK_FAILED,  // it didn't start; nothing else may start instead
} PICK_OUTCOME;

// Starts what the boot list picked (an entry, a drive's default file or its
// boot image) from the drive the pick names, and clears the pick. Internal
// storage and the SD card are searched whatever the boot source, which only
// decides what power-on starts.
STATIC PICK_OUTCOME
BootPick (
  IN CONST MENU_HOST         *Host,
  IN CONST MENU_BOOT_CONTEXT *Ctx,
  OUT BOOLEAN                *HasImage,
  OUT BOOT_IMAGE_FOUND       *Image
)
{
  VOLUMES_HANDLE_LIST Volumes = {0};
  OABL_BUFFER         Pick    = {0};
  OABL_BUFFER         Path    = {0};
  EFI_HANDLE          Drive   = NULL;
  CHAR8               Id[256] = {0};
  UINT16              Len;
  BOOLEAN             Started;

  if (!MenuVarGet (MENU_PICK_VAR, &Pick)) {
    return PICK_NONE;
  }
  MenuVarPut (MENU_PICK_VAR, NULL, 0);
  MenuFlush ();
  VolumesAllowed (BOOT_SOURCE_AUTO, Ctx->CfwOnInternal, FALSE, &Volumes);
  // A damaged pick matches nothing.
  if (GetLe16 (Pick.Data, Pick.Len, 0, &Len) && Pick.Len > 2u + Len &&
      Pick.Len - 2 - Len < sizeof (Id)) {
    CopyMem (Id, Pick.Data + 2 + Len, Pick.Len - 2 - Len);
    for (UINTN Index = 0; Index < Volumes.Count && !Drive; Index++) {
      if (PathsHandlePath (Volumes.Items[Index], &Path) && Path.Len == Len &&
          CompareMem (Path.Data, Pick.Data + 2, Len) == 0) {
        Drive = Volumes.Items[Index];
      }
      BufferFree (&Path);
    }
  }
  BufferFree (&Pick);
  MenuLog ("Boot entries pick: %s on %s", Id, Drive ? "its drive" : "no drive");
  if (!Drive) {
    VecFree (&Volumes);
    return PICK_GONE;
  }
  if (AsciiStrEq (Id, MENU_PICK_BOOT_IMAGE)) {
    Started = *HasImage = HandOverBootImageFrom (Host, Ctx, &Drive, 1, Image);
  } else if (AsciiStrEq (Id, MENU_PICK_DEFAULT_FILE)) {
    DEFAULT_FILES_HOST Files = FilesHost (Ctx);

    Started = DefaultFilesBoot (&Drive, 1, &Files, NULL) ==
              DEFAULT_FILES_OUTCOME_RETURNED;
  } else {
    BliSet ("LoaderEntryOneShot", Id);
    Started = StandardBoot (Host, &Drive, 1, Ctx) == EFI_SUCCESS;
  }
  VecFree (&Volumes);
  return Started ? PICK_STARTED : PICK_FAILED;
}
#endif

// Runs the Linux boot. If a boot image was handed to the ABL, returns it in
// *Image with *HasImage set, and the selected model in Model. *Pick is
// MENU_START_RESULT_PICK_GONE or _PICK_FAILED when the boot list's pick
// didn't start, and nothing else was tried; else MENU_START_RESULT_NONE.
STATIC EFI_STATUS
BootWithImage (
  IN CONST MENU_HOST   *Host,
  OUT BOOLEAN          *HasImage,
  OUT UINT8            *Pick,
  OUT BOOT_IMAGE_FOUND *Image,
  OUT CHAR8            *Model,
  IN UINTN              ModelSize
)
{
  MENU_BOOT_CONTEXT  Ctx;
  DEFAULT_FILES_HOST Files;
  EFI_HANDLE        *Volumes;
  UINTN              Count;
  EFI_STATUS         Status;
  EFI_STATUS         Returned;
#ifdef OABL_STANDARD_BOOT
  PICK_OUTCOME Outcome;
#endif

  *HasImage = FALSE;
  *Pick     = MENU_START_RESULT_NONE;
  if (!BootContextOf (Host, &Ctx)) {
    return EFI_NOT_READY;
  }
  Files = FilesHost (&Ctx);
  // Draw our splash only with "Custom boot logo" on. Otherwise the
  // manufacturer's logo stays up.
  if (MenuVarIs (SETTINGS_KEY_CUSTOM_LOGO, 1)) {
    MenuBootSplash (Host, &Ctx);
  } else {
    MenuLog ("splash: the manufacturer's logo kept (Custom boot logo is off)");
  }
#ifdef OABL_STANDARD_BOOT
  Outcome = BootPick (Host, &Ctx, HasImage, Image);
  switch (Outcome) {
  case PICK_STARTED:
    Status = EFI_SUCCESS;
    goto Picked;
  case PICK_GONE:
  case PICK_FAILED:
    // After an explicit choice, never another system.
    MenuLog ("the pick didn't start, and nothing else is tried");
    *Pick  = Outcome == PICK_GONE ? MENU_START_RESULT_PICK_GONE
                                  : MENU_START_RESULT_PICK_FAILED;
    Status = EFI_NOT_FOUND;
    goto Picked;
  case PICK_NONE:
    break;
  }
#endif
  MenuBootVolumes (&Ctx, &Volumes, &Count);
  if (!Count) {
    FreePool (Volumes);
    MenuLog ("no drive the boot source allows");
    return EFI_NOT_FOUND;
  }
  Status = StandardBoot (Host, Volumes, Count, &Ctx);
  FreePool (Volumes);
  MenuBootVolumes (&Ctx, &Volumes, &Count);
  if (DefaultFilesBoot (Volumes, Count, &Files, &Returned) ==
      DEFAULT_FILES_OUTCOME_RETURNED) {
    Status = EFI_SUCCESS;
  } else if (Status != EFI_SUCCESS && Status != EFI_LOAD_ERROR) {
    MenuLog ("no default file");
    Status = EFI_NOT_FOUND;
  }
  FreePool (Volumes);
  *HasImage = HandOverBootImage (Host, &Ctx, Image);
#ifdef OABL_STANDARD_BOOT
Picked:
#endif
  if (*HasImage) {
    AsciiStrCopy (Model, ModelSize, Ctx.Model);
  }
#ifdef OABL_CARD_LOGO
  // End the animation on its last frame, which the panel keeps through the
  // boot image's start and the hand-over.
  MenuAnimFinish ();
#endif
  return Status;
}

// USB host

// Returns TRUE if we are a boot-service driver. Unlike an application, it
// stays loaded after returning EFI_SUCCESS, so callbacks can remain.
STATIC BOOLEAN
Resident (VOID)
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage =
    GetProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid);

  return LoadedImage && LoadedImage->ImageCodeType == EfiBootServicesCode;
}

// Brings USB host up and installs its protocol. EFI_SUCCESS means something
// stays on, so we must stay loaded.
STATIC EFI_STATUS
UsbHostUpQuirked (VOID)
{
  QCOM_CHIP        Chip;
  USB_HOST_BOARD   Board;
  USB_HOST_OUTCOME Outcome;
  EFI_STATUS       Status;
  UINT32           Id;

  if (!Resident ()) {
    MenuLog ("built as an application, so it can't stay loaded: no USB host");
    return EFI_UNSUPPORTED;
  }
  if (!QcomChip (&Chip)) {
    MenuLog ("no ChipInfo: no USB host");
    return EFI_UNSUPPORTED;
  }
  Id = EFI_ERROR (Chip.Id.Status) ? 0 : Chip.Id.Value;
  if (!UsbHostBoardOf (Id, Chip.Name, &Board)) {
    MenuLog ("no USB host on chip %u (%s)", Id, Chip.Name);
    return EFI_UNSUPPORTED;
  }
  Outcome = UsbHostBringUp (Board, gMenuEfiLog);
  if (!Outcome.Resident) {
    return EFI_NOT_FOUND;
  }
  Status = UsbHostInstallProtocol ();
  if (EFI_ERROR (Status)) {
    MenuLog ("offering the USB host protocol failed: %s", MENU_STATUS (Status));
  }
  return EFI_SUCCESS;
}

// Brings USB host up for booting from a USB drive. On error nothing is left
// on.
STATIC EFI_STATUS
UsbHostUp (VOID)
{
  UINT32     Chip = QuirksChipId ();
  EFI_STATUS Status;

  // The GPT quirk must be in place before the drive's partitions are read.
  // The FAT quirk applies to what gets mounted.
  QuirksGptClobber (Chip, gMenuEfiLog);
  Status = UsbHostUpQuirked ();
  QuirksFatCache (Chip, gMenuEfiLog);
  return Status;
}

// Brings USB host up for a USB boot or the model list, unless it is up
// already. Returns TRUE if a drive is up.
STATIC BOOLEAN
UsbHostOn (VOID)
{
  EFI_STATUS Status;
  BOOLEAN    Up;

  if (UsbHostIsUp ()) {
    return TRUE;
  }
  Status = UsbHostUp ();
  Up     = UsbHostIsUp ();
  MenuLog (
    "USB host: %s%s", MENU_STATUS (Status),
    Up ? ", a USB drive is up" : ", no USB drive"
  );
  return Up;
}

EFI_STATUS
MenuLiveUsbHostUp (IN CONST MENU_HOST *Host)
{
  EFI_STATUS Status;

  if (gUsbLive) {
    return EFI_SUCCESS;
  }
  Status = MenuHostAction (Host, OPENABL_MENU_ACTION_GADGET_PAUSE);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  if (UsbHostOn ()) {
    gUsbLive = TRUE;
    return EFI_SUCCESS;
  }
  UsbHostTeardown ();
  Status = MenuHostAction (Host, OPENABL_MENU_ACTION_GADGET_RESUME);
  MenuLog ("fastboot back: %s", MENU_STATUS (Status));
  return EFI_NOT_FOUND;
}

EFI_STATUS
MenuLiveUsbHostDown (IN CONST MENU_HOST *Host)
{
  if (!gUsbLive) {
    return EFI_SUCCESS;
  }
  gUsbLive = FALSE;
  UsbHostTeardown ();
  return MenuHostAction (Host, OPENABL_MENU_ACTION_GADGET_RESUME);
}

// FLAG_MAIN: the whole boot

// In verbose mode the log on screen says why nothing booted, so keep it up
// until a key is pressed.
STATIC VOID
HoldLogUntilKey (IN CONST MENU_HOST_STATE *State)
{
  INPUT_KEYS Keys = InputKeysNew ();

  if (State->AblMode == 0) {
    return;
  }
  MenuLog ("Press any key to go to the menu");
  for (;;) {
    INPUT_EVENTS Events = {0};
    UINTN        Count;

    InputKeysPoll (&Keys, ClockNowMs (), &Events);
    Count = Events.Count;
    VecFree (&Events);
    if (Count) {
      return;
    }
    gBS->Stall (10000);
  }
}

// Boots Linux: standard entries, default files, then the boot image through
// Qualcomm's code for verification. Returns only if nothing booted, with a
// MENU_START_RESULT_* reason. *UsbUp says whether USB host came up.
STATIC UINT8
LinuxBoot (
  IN CONST MENU_HOST *Host,
  IN BOOLEAN          Usb,
  OUT BOOLEAN        *UsbUp
)
{
  BOOT_IMAGE_FOUND Image;
  CHAR8            Model[MODEL_LEN + 1];
  EFI_STATUS       Status;
  EFI_STATUS       Refused;
  UINT8            Result;
  BOOLEAN          HasImage;
  UINT8            Pick;

  DisplayHandoverArm (MenuHostLogFn);
  MenuHostAction (Host, OPENABL_MENU_ACTION_BOOT_PREP);
  *UsbUp = Usb && UsbHostOn ();
  Status =
    BootWithImage (Host, &HasImage, &Pick, &Image, Model, sizeof (Model));
  if (!HasImage) {
    Result = Pick != MENU_START_RESULT_NONE ? Pick
             : Status == EFI_SUCCESS        ? MENU_START_RESULT_RETURNED
                                            : MENU_START_RESULT_NOTHING;
  } else {
    Result = BootImageStart (Host, &Image, Model, gMenuEfiLog, &Refused) ==
                 BOOT_IMAGE_NOT_STARTED_NO_MODEL
               ? MENU_START_RESULT_NO_MODEL
               : MENU_START_RESULT_FAILED;
  }
  if (Usb) {
    UsbHostTeardown ();
  }
  return Result;
}

// Starts Linux from the menu without a restart, leaving fastboot running.
// Returns only if nothing booted, with the reason.
STATIC UINT8
StartHere (IN CONST MENU_HOST *Host)
{
  MENU_HOST_STATE State;
  UINT8           Result;
  BOOLEAN         Usb;
  BOOLEAN         UsbUp = FALSE;

  if (EFI_ERROR (MenuHostState (Host, &State))) {
    return MENU_START_RESULT_NOTHING;
  }
  Usb = State.BootSource == MENU_HOST_BOOT_SOURCE_USB;
  MenuLog (
    "starting Linux in this boot%s", Usb ? ", fastboot paused for USB host" : ""
  );
  if (Usb &&
      MenuHostAction (Host, OPENABL_MENU_ACTION_GADGET_PAUSE) != EFI_SUCCESS) {
    Result = MENU_START_RESULT_NO_USB;
  } else {
    Result = LinuxBoot (Host, Usb, &UsbUp);
    if (Usb) {
      MenuHostAction (Host, OPENABL_MENU_ACTION_GADGET_RESUME);
    }
    if (Usb && !UsbUp) {
      Result = MENU_START_RESULT_NO_USB;
    }
  }
  MenuLog ("nothing booted (%u)", Result);
  HoldLogUntilKey (&State);
  return Result;
}

// Starts fastboot and runs the menu, which serves fastboot from its poll,
// until fastboot ends. Qualcomm's menu is shown if ours can't run. If Start
// Linux boots nothing, the menu comes back and says why.
STATIC EFI_STATUS
FastbootPath (
  IN CONST MENU_HOST *Host,
  IN UINT8            CameBack
)
{
  EFI_STATUS Status = MenuHostAction (Host, OPENABL_MENU_ACTION_FASTBOOT_START);

  if (Status != EFI_SUCCESS) {
    MenuLog ("fastboot didn't start (%s)", MENU_STATUS (Status));
    return Status;
  }
  gFastbootUp = TRUE;
#ifdef OABL_SYSFONT
  // Qualcomm's fastboot screens need XBL's scaled system fonts. Provide
  // them where XBL has none (SM8750).
  SysFontEnsure (MenuHostLogFn);
#endif
  for (;;) {
    MENU_END End;

    // Keep the ABL's log off the menu. The Log screen shows it.
    AblConsoleHold (TRUE);
    End = MenuRun (Host, FALSE, CameBack);
    AblConsoleHold (FALSE);
    // If the menu left USB host up, bring fastboot's gadget back.
    MenuLiveUsbHostDown (Host);
    if (End.Kind == MENU_END_LEFT) {
      break;
    }
    if (End.Kind == MENU_END_FAILED) {
      MenuLog (
        "the menu couldn't run (%s): Qualcomm's fastboot menu",
        MENU_STATUS (End.Status)
      );
      MenuHostAction (Host, OPENABL_MENU_ACTION_FASTBOOT_MENU);
      break;
    }
    CameBack = StartHere (Host);
  }
  gFastbootUp = FALSE;
  return MenuHostAction (Host, OPENABL_MENU_ACTION_FASTBOOT_STOP);
}

// Consumes the menu's ShowModelMenu request, made because fastboot holds
// the USB controller the scan needs. It counts only with a USB boot source
// on an ABL with USB host.
STATIC BOOLEAN
ModelListRequested (IN CONST MENU_HOST_STATE *State)
{
  if (!(State->Flags & OPENABL_MENU_STATE_USB_HOST)) {
    return FALSE;
  }
  if (!MenuVarIs (SETTINGS_KEY_SHOW_MODEL_MENU, 1)) {
    return FALSE;
  }
  MenuVarPut (SETTINGS_KEY_SHOW_MODEL_MENU, NULL, 0);
  MenuFlush ();
  if (State->BootSource != MENU_HOST_BOOT_SOURCE_USB) {
    MenuLog ("model list asked for, but the boot source isn't USB: ignored");
    return FALSE;
  }
  return TRUE;
}

// Prepares storage, brings USB host up and runs the menu on its model list.
// Restarts to the bootloader afterwards, even if the menu couldn't run.
STATIC EFI_STATUS
ModelListAfterRestart (IN CONST MENU_HOST *Host)
{
  MENU_END End;

  MenuLog ("the model list, after the restart for it");
  QuirksPrepareStorage (QuirksChipId (), gMenuEfiLog);
  UsbHostOn ();
  AblConsoleHold (TRUE);
  End = MenuRun (Host, TRUE, MENU_START_RESULT_NONE);
  AblConsoleHold (FALSE);
  if (End.Kind == MENU_END_FAILED) {
    MenuLog ("the menu couldn't run (%s)", MENU_STATUS (End.Status));
  }
  return MenuHostAction (Host, OPENABL_MENU_ACTION_RESTART_BOOTLOADER);
}

// Takes the system the menu picked for this boot (MENU_ONCE_*), or 0 for
// none, and clears it, so a key held now can't leave it for a later boot.
STATIC UINT8
TargetOnce (VOID)
{
  OABL_BUFFER Once   = {0};
  UINT8       Target = 0;

  if (!MenuVarGet (MENU_ONCE_VAR, &Once)) {
    return 0;
  }
  MenuVarPut (MENU_ONCE_VAR, NULL, 0);
  MenuFlush ();
  // A damaged value picks nothing.
  if (Once.Len == 1 &&
      (Once.Data[0] == MENU_ONCE_LINUX || Once.Data[0] == MENU_ONCE_ANDROID)) {
    Target = Once.Data[0];
  }
  BufferFree (&Once);
  MenuLog (
    "the menu picked %s for this boot", Target == MENU_ONCE_ANDROID ? "Android"
                                        : Target                    ? "Linux"
                                                                    : "nothing"
  );
  return Target;
}

// Runs the boot described at MainBoot.
STATIC EFI_STATUS
RunBoot (
  IN CONST MENU_HOST       *Host,
  IN CONST MENU_HOST_STATE *State
)
{
  BOOT_DECISION_START Start = MenuHostStateStart (State);
  UINT8               Once  = TargetOnce ();
  BOOLEAN             Charger;
  BOOLEAN             Android;
  BOOLEAN             Alt;
  BOOLEAN             Usb;
  BOOLEAN             UsbUp;
  UINT8               Result;

  if (ModelListRequested (State)) {
    return ModelListAfterRestart (Host);
  }
  if (State->Entry & OPENABL_MENU_ENTRY_FASTBOOT) {
    MenuLog ("fastboot was asked for");
    return FastbootPath (Host, MENU_START_RESULT_NONE);
  }
  if (BootDecisionFastboot (&Start)) {
    MenuLog ("fastboot was asked for (Delete held)");
    return FastbootPath (Host, MENU_START_RESULT_NONE);
  }
  // Qualcomm's charger check, if the charger screen is enabled.
  Charger =
    State->OffmodeCharging != 0 &&
    MenuHostAction (Host, OPENABL_MENU_ACTION_CHARGER_BOOT) == EFI_SUCCESS;
  // BIOS shows its own charging screen, with or without Android, unless
  // the menu picked something to start.
  if (Charger && !Once) {
    MenuLog ("powered on by the charger: the charging screen");
    return FastbootPath (Host, MENU_START_RESULT_CHARGING);
  }
  Alt = State->AltVolumeBoot;
  // The menu's pick wins over the boot mode and Volume Up. Only the keys
  // for fastboot, above, still apply.
  Android = Once ? Once == MENU_ONCE_ANDROID : State->BootMode != 0 && !Alt;
  if (Android) {
    BOOLEAN Recovery = BootDecisionRecovery (
      &Start, Once == MENU_ONCE_ANDROID || QuirksAblSettings ().BootMode != 0
    );
    EFI_STATUS Status;

    MenuLog ("%s", Recovery ? "booting Android's recovery" : "booting Android");
    Status = MenuHostAction (
      Host, Recovery ? OPENABL_MENU_ACTION_BOOT_ANDROID_RECOVERY
                     : OPENABL_MENU_ACTION_BOOT_ANDROID
    );
    MenuLog ("Android didn't boot (%s)", MENU_STATUS (Status));
    // Report Android's failure, not a reason left over from an earlier
    // Linux boot.
    return FastbootPath (Host, MENU_START_RESULT_ANDROID_FAILED);
  }
  Usb    = (State->Flags & OPENABL_MENU_STATE_USB_HOST) &&
           (State->BootSource == MENU_HOST_BOOT_SOURCE_USB || Alt);
  Result = LinuxBoot (Host, Usb, &UsbUp);
  MenuLog ("nothing booted (%u); the menu says why", Result);
  HoldLogUntilKey (State);
  if (Usb) {
    // USB host was up, so fastboot needs a restart. Keep the reason across
    // it.
    if (EFI_ERROR (MenuVarPut (SETTINGS_KEY_BOOT_FAILURE, &Result, 1))) {
      MenuLog ("the reason can't go across the restart");
    }
    MenuFlush ();
    MenuHostAction (Host, OPENABL_MENU_ACTION_RESTART_BOOTLOADER);
  }
  return FastbootPath (Host, Result);
}

// FLAG_MAIN: runs the whole boot. Fastboot if a key or the reboot reason
// asks for it, else Android or recovery if the boot mode says so (unless
// the alternate Volume Up boot applies), else Linux, falling back to
// fastboot. Returns EFI_UNSUPPORTED, having done nothing, if the ABL's
// state can't be read.
STATIC EFI_STATUS
MainBoot (IN CONST MENU_HOST *Host)
{
  MENU_HOST_STATE State;
  EFI_STATUS      Status;

  if (EFI_ERROR (MenuHostState (Host, &State))) {
    return EFI_UNSUPPORTED;
  }
  // RunBoot always acts, and EFI_UNSUPPORTED would make the ABL boot as if
  // we hadn't run.
  Status = RunBoot (Host, &State);
  return Status == EFI_UNSUPPORTED ? EFI_ABORTED : Status;
}

STATIC EFI_STATUS EFIAPI
RunHere (IN OPENABL_STORAGE_PROTOCOL *This);

// FLAG_EARLY: patches out SdccDxe's 1.8 V request, then stays loaded with
// the storage protocol the ABL connects its storage through. The GPT quirk
// can't run this early (it bootloops), only before a connect. It also makes
// XBL's Security protocols accept every image.
STATIC EFI_STATUS
Early (IN CONST MENU_HOST *Host)
{
  UINT32      Chip;
  UINT32      Patched;
  OABL_BUFFER Mode = {0};
  EFI_STATUS  Status;

#ifdef OABL_DEVINFO_SETTINGS
  // Where XBL can't commit variables, restore the settings from devinfo's
  // copy before anything reads them.
  VariablesRestoreFromDevinfo (gMenuEfiLog);
#endif
  // Before the ABL reads devinfo.
  VariablesMoveOutOfDevinfo (gMenuEfiLog);
  VariablesChargerScreenToDevinfo (gMenuEfiLog);
  if (VariablesGet (SETTINGS_KEY_ABL_MODE, &Mode) && Mode.Len &&
      Mode.Data[0] != 0) {
    AblConsoleShow ();
  }
  BufferFree (&Mode);
  Chip    = QuirksChipId ();
  Patched = QuirksSdccUhs (Chip, gMenuEfiLog);
  MenuLog ("early: %u SdccDxe site(s) patched", Patched);
  if (!Resident ()) {
    MenuLog (
      "built as an application, so it can't stay loaded: no storage protocol"
    );
    Status = EFI_UNSUPPORTED;
  } else {
    // From now on the ABL runs this copy through the storage protocol.
    QuirksSetRun (RunHere);
    Status = QuirksInstallProtocol (Chip);
    if (!EFI_ERROR (Status)) {
      // Accept the images we boot from drives. This waits until we stay
      // loaded, since the replacement checks are our own functions.
      SecurityAcceptAll (gMenuEfiLog);
      Status = EFI_SUCCESS;
    } else {
      MenuLog (
        "offering the storage protocol failed: %s", MENU_STATUS (Status)
      );
    }
  }
  // Run XBL's SD card detection once more as the ABL goes on.
  QuirksSignalSdDetect ();
  return Status;
}

// Entry

// Does what the host's flags ask. Again is TRUE when the ABL runs the copy
// left loaded at its start.
STATIC EFI_STATUS
Run (
  IN CONST MENU_HOST *Host,
  IN BOOLEAN          Again
)
{
  CHAR8 Hello[96];

  gMenuHost    = *Host;
  gMenuHostSet = TRUE;
  // Set up the ABL's libfdt and inflate before anything edits a devicetree.
  AblLibInit ();
  SPrint (
    Hello, sizeof (Hello), "openabl-menu %s%s", MENU_VERSION,
    Again ? " running again (the copy loaded at the start)" : " starting"
  );
  if (MenuHostFlags (Host) & OPENABL_MENU_FLAG_EARLY) {
    EFI_STATUS Status;
    // The ABL's log is ours. Keep it from here.
    AblConsoleDirect (TRUE);
    MenuHostLog (Host, Hello);
    Status = Early (Host);
    AblConsoleDirect (FALSE);
    return Status;
  }
  MenuHostLog (Host, Hello);
  if (!(MenuHostFlags (Host) & OPENABL_MENU_FLAG_MAIN)) {
    return EFI_UNSUPPORTED;
  }
  return MainBoot (Host);
}

// The storage protocol's Run. The ABL calls it to run the copy left loaded
// at the early start instead of loading another.
STATIC EFI_STATUS EFIAPI
RunHere (IN OPENABL_STORAGE_PROTOCOL *This)
{
  MENU_HOST Host;

  (VOID)This;
  gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
  if (!MenuHostLocate (&Host)) {
    return EFI_NOT_FOUND;
  }
  return Run (&Host, TRUE);
}

EFI_STATUS EFIAPI
efi_main (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
)
{
  MENU_HOST Host;

  UefiInit (ImageHandle, SystemTable);
  gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
  // Started by something other than the ABL.
  if (!MenuHostLocate (&Host)) {
    return EFI_NOT_FOUND;
  }
  return Run (&Host, FALSE);
}
