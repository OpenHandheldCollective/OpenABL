/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - the menu's strings.
*/

#include "Ui/Lang.h"
#include "Base/Text.h"
#include "Core/Settings.h"

_Static_assert (LANG_COUNT == SETTINGS_LANGUAGES, "languages");

STATIC CONST LANG_STRINGS mEn = {
  .Language          = "Language",
  .SectionBoot       = "Boot",
  .SectionSettings   = "Settings",
  .SectionSystem     = "System",
  .Start             = "Start",
  .StartLinux        = "Start Linux",
  .StartAndroid      = "Start Android",
  .BootEntries       = "Boot entries",
  .BootImage         = "Boot image",
  .DefaultFile       = "EFI loader",
  .RestartBootloader = "Restart bootloader",
  .RecoveryMode      = "Recovery mode",
  .Edl               = "Emergency download mode",
  .OtherSlot         = "Boot other slot",
  .BootMode          = "Boot mode",
  .AblMode           = "ABL mode",
  .Verbose           = "Verbose",
  .Silent            = "Silent",
  .VolumeUp          = "Volume up at boot",
  .VolLinuxExternal  = "Linux (external)",
  .VolRecovery       = "Recovery",
  .BootSource        = "Boot source",
  .Auto              = "Auto",
  .Internal          = "Internal",
  .SdCard            = "SD card",
  .Charging          = "Off-mode charging",
  .On                = "On",
  .Off               = "Off",
  .Rotation          = "Display rotation",
  .CustomLogo        = "Custom boot logo",
  .RotationAuto      = "Auto ({}°)",
  .Timeout           = "Menu timeout",
  .Seconds           = "{} s",
  .DeviceModel       = "Device model",
  .NotSet            = "Not set",
  .UninstallCfw      = "Uninstall CFW",
  .PowerOff          = "Power off",
  .ToastTargetExited = "The boot target exited",
  .ToastAndroidInAbl = "Android is chosen in the ABL's own menu",
  .ToastFirmwareDt   = "Using the firmware's devicetree",
  .ToastModelSet     = "Device model set to {}",
  .LookingOn         = "Looking for Linux on {}",
  .SrcFirstDrive     = "the first bootable drive",
  .SrcInternal       = "internal storage",
  .SrcSd             = "the SD card",
  .Countdown         = "Starting {0} in {1} s",
  .AnyKeyToStay      = "Any key to stay",
  .InfoRam           = "RAM",
  .InfoStorage       = "Storage",
  .InfoDisplay       = "Display",
  .InfoModel         = "Model",
  .InfoPanel         = "Panel",
  .DeviceInfo        = "Device info",
  .Log               = "Log",
  .SectionDebug      = "Debug",
  .DbgUsbLive        = "Live USB host",
  .DbgQuiesce        = "Park display",
  .DbgPanelLines     = "Panel lines off",
  .InfoChip          = "Chip",
  .InfoPlatform      = "Platform",
  .InfoPmic          = "PMIC",
  .InfoStorageChip   = "Storage chip",
  .InfoAndroid       = "Android build",
  .InfoBootloader    = "Bootloader",
  .InfoUefi          = "UEFI firmware",
  .HintMove          = "Up and Down",
  .HintSelect        = "Select",
  .HintBack          = "Back",
  .NoDeviceTrees     = "No device trees found",
  .OneModel          = "1 model found",
  .NModels           = "{} models found",
  .Current           = "Current",
  .FirmwareDefault   = "Firmware default",
  .Back              = "Back",
  .ModelExplain = "The model picks which device tree Linux boots with, and the "
                  "display rotation.",
  .FirmwareDtExplain =
    "Boot with the devicetree the firmware already provides, unchanged.",
  .FoundIn        = " Found in {}.",
  .EntriesExplain = "Pick one to start it now. Start tries them in this order.",
  .NoTriesLeft    = "No tries left",
  .Default        = "Default",
  .BackToMenu     = "Back to the menu.",
  .LookingForDts  = "Looking for device trees",
  .FewSeconds     = "This can take a few seconds.",
  .StartingAndroid = "Starting Android",
  .StartingLinux   = "Starting Linux",
  .Starting        = "Starting...",
  .ErrNoImageTitle = "Couldn't start PortareOS",
  .ErrNoImageMsg   = "PortareOS isn't on internal storage or an SD card.",
  .ErrNoImageHint  = "Insert a PortareOS card.",
  .ErrLoadTitle    = "Couldn't start PortareOS",
  .ErrLoadHint = "Its boot files may be damaged. Write the card or update again.",
  .ErrBootImageFailed =
    "The boot image didn't pass verification, or didn't start.",
  .ErrAndroidTitle       = "Android can't start here",
  .ErrAndroidMsg         = "This build can't start Android on this device yet.",
  .ErrAndroidHint        = "Choose PortareOS in the menu.",
  .ErrAndroidFailedTitle = "Couldn't start Android",
  .ErrAndroidFailedMsg   = "Android's boot images didn't start.",
  .ErrAndroidFailedHint  = "Its partitions may be damaged. PortareOS still starts.",
  .ErrModelTitle = "Device model isn't set",
  .ErrModelMsg =
    "Linux needs your device model to choose the right device tree.",
  .PressAnyKeyMenu       = "Press any key to return to the menu",
  .PressAnyKeyModel      = "Press any key to choose your model",
  .ConfirmUninstallTitle = "Uninstall PortareOS?",
  .ConfirmUninstallMsg   = "Erases PortareOS with its games and saves. Android gets the space back and starts fresh:",
  .ConfirmUninstall      = "Uninstall",
  .ConfirmUninstallFinalTitle = "Erase for good?",
  .ConfirmUninstallFinalMsg =
    "The device restarts into recovery, which wipes Android's data. This "
    "can't be undone.",
  .ConfirmUninstallFinal = "Erase",
  .ConfirmEdlTitle       = "Enter emergency download mode?",
  .ConfirmEdlMsg = "The screen stays off in this mode. You'll need a computer "
                   "with Qualcomm flashing tools to leave it.",
  .ConfirmEdl    = "Enter EDL",
  .Cancel        = "Cancel",
  .AblSettingFailed    = "The ABL couldn't change that setting",
  .AblModelFailed      = "The ABL couldn't save the device model",
  .AblNotAnswering     = "The ABL stopped answering",
  .AblRestartFailed    = "The ABL couldn't restart ({})",
  .TargetNotKept       = "Couldn't keep the choice for the restart ({})",
  .RestartingToScan    = "Restarting to scan",
  .RestartToScanFailed = "Couldn't restart to scan",
  .ModelScanFailed     = "Couldn't look for device models",
  .AblActionFailed     = "The ABL couldn't do that",
  .PowerOffFailed      = "Couldn't power off",
  .RestartingRecovery  = "Restarting into recovery",
  .CfwRemoved          = "Linux is removed. Recovery wipes Android's data next.",
  .WipeInRecovery      = "PortareOS is removed. Choose Factory reset in recovery to start Android fresh.",
  .UninstallFailed     = "Uninstall failed",
  .Uninstalling        = "Uninstalling PortareOS",
  .DontTurnOff         = "Don't turn off the Nova.",
  .UninstallNotFound   = "PortareOS is no longer on internal storage.",
  .UninstallUntrusted  = "The partition table can't be trusted, so nothing "
                         "was changed.",
  .UninstallWriteFailed = "Writing the partition table failed ({}).",
  .UninstallReadBack   = "The partition table didn't read back as written.",
  .UninstallError      = "Something went wrong ({}).",
  .ErrWipeTitle        = "PortareOS is removed",
  .WipeByHand          = "Android couldn't be made to start first. Choose Factory reset in Android's recovery.",
  .Unknown             = "Unknown",
  .NotInserted         = "Not inserted",
  .SrcAblScan          = "the ABL's scan",
  .EntryType1          = "BLS entry",
  .EntryUki            = "Unified kernel image",
  .InternalStorage     = "Internal storage",
  .RemovableStorage    = "Removable storage",
  // BIOS
  .SectionStart = "Start",
  .SectionStorage = "Storage",
  .PortareOs = "PortareOS",
  .Android = "Android",
  .BootOrder = "Boot order",
  .UninstallPortareOs = "Uninstall PortareOS",
  .Restart = "Restart",
  .ShutDown = "Shut down",
  .OrderPortareOsFirst = "PortareOS, Android",
  .OrderAndroidFirst = "Android, PortareOS",
  .HelpFirst = "Starts at power-on. Hold VOL+ for {}.",
  .HelpSecond = "Starts when VOL+ is held at power-on.",
  .HelpBootOrder = "Power-on starts the first. Hold VOL+ for the other.",
  .HelpUninstall = "Erases PortareOS. Android gets its space back.",
  .HelpEdl = "For flashing from a computer.",
  .KeyPower = "POWER",
  .KeyVolume = "VOL",
  .KeyEnter = "ENTER",
  .KeyArrows = "UP/DOWN",
  .VerbLaunch = "LAUNCH",
  .VerbChange = "CHANGE",
  .VerbSelect = "SELECT",
  .VerbMenu = "MENU",
  .VerbMove = "MOVE",
};

STATIC CONST LANG_STRINGS *CONST mLangs[LANG_COUNT] = {
  [LANG_EN] = &mEn,
};

// Each language's name in itself, for the Language setting.
STATIC CONST CHAR8 *CONST mNames[LANG_COUNT] = {
  [LANG_EN] = "English",
};

CONST LANG_STRINGS *gStrings = &mEn;

VOID
LangSet (IN UINT8 Lang)
{
  gStrings = mLangs[Lang < LANG_COUNT ? Lang : LANG_EN];
}

CONST CHAR8 *
LangName (IN UINT8 Lang)
{
  return mNames[Lang < LANG_COUNT ? Lang : LANG_EN];
}

VOID
LangFill (
  OUT CHAR8             *Out,
  IN UINTN               Cap,
  IN CONST CHAR8        *Template,
  IN CONST CHAR8 *CONST *Args,
  IN UINTN               ArgCount
)
{
  UINTN Length = 0;
  UINTN Next   = 0;

  if (!Cap) {
    return;
  }
  while (*Template && Length + 1 < Cap) {
    CONST CHAR8 *Arg = NULL;

    if (Template[0] == '{' && Template[1] == '}') {
      Arg = Next < ArgCount ? Args[Next] : "";
      Next++;
      Template += 2;
    } else if (Template[0] == '{' && Template[1] >= '0' && Template[1] <= '9' &&
               Template[2] == '}') {
      UINTN Which = (UINTN)(Template[1] - '0');
      Arg         = Which < ArgCount ? Args[Which] : "";
      Template += 3;
    }
    if (Arg) {
      while (*Arg && Length + 1 < Cap) {
        Out[Length++] = *Arg++;
      }
    } else {
      Out[Length++] = *Template++;
    }
  }
  Out[Length] = 0;
}
