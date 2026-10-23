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
  .SectionSystem     = "System",
  .Boot              = "Boot",
  .Edl               = "Emergency download mode",
  .Internal = "Internal storage",
  .SdCard            = "SD card",
  .On                = "On",
  .Off               = "Off",
  .DeviceModel       = "Device model",
  .NotSet            = "Not set",
  .ToastTargetExited = "Returned to the boot menu.",
  .ToastFirmwareDt = "Using the firmware default.",
  .ToastModelSet = "Device model set to {}.",
  .SrcInternal       = "internal storage",
  .SrcSd             = "the SD card",
  .Countdown         = "Starting {0} in {1} s",
  .AnyKeyToStay      = "Any key to stay",
  .InfoRam           = "RAM",
  .InfoStorage = "Internal storage",
  .InfoDisplay       = "Display",
  .InfoModel = "Device model",
  .InfoPanel         = "Panel",
  .DeviceInfo = "Device information",
  .Log = "Boot log",
  .SectionDebug      = "Debug",
  .DbgUsbLive = "USB host without restart",
  .DbgQuiesce = "Stop display before boot",
  .DbgPanelLines = "Panel power off before boot",
  .InfoChip          = "Chip",
  .InfoPlatform      = "Platform",
  .InfoPmic          = "PMIC",
  .InfoStorageChip   = "Storage chip",
  .InfoAndroid       = "Android build",
  .InfoBootloader    = "Bootloader",
  .InfoUefi          = "UEFI firmware",
  .Current           = "Current",
  .FirmwareDefault   = "Firmware default",
  .Back              = "Back",
  .NoTriesLeft = "No boot attempts left",
  .LookingForDts = "Looking for device models",
  .FewSeconds     = "This can take a few seconds.",
  .Starting        = "Starting...",
  .ErrNoImageTitle = "Couldn't start PortareOS",
  .ErrNoImageMsg = "No usable PortareOS installation was found.",
  .ErrNoImageHint = "Insert a PortareOS card, or reinstall PortareOS.",
  .ErrLoadTitle    = "Couldn't start PortareOS",
  .ErrLoadHint = "Its boot files may be damaged. Reinstall or update PortareOS.",
  .ErrBootImageFailed = "PortareOS failed verification or couldn't start.",
  .ErrAndroidTitle       = "Android can't start here",
  .ErrAndroidMsg = "This firmware can't start Android on this device.",
  .ErrAndroidHint = "Choose another system under Boot.",
  .ErrAndroidFailedTitle = "Couldn't start Android",
  .ErrAndroidFailedMsg = "Android didn't start from internal storage.",
  .ErrAndroidFailedHint = "Its partitions may be damaged. Choose another system under Boot.",
  .ErrModelTitle = "Device model isn't set",
  .ErrModelMsg = "Linux needs your device model to start. Choose it on the next screen.",
  .ConfirmUninstallTitle = "Uninstall PortareOS?",
  .ConfirmUninstallMsg   = "Erases PortareOS with its games and saves. Android gets the space back and starts fresh:",
  .ConfirmUninstall      = "Uninstall",
  .ConfirmEdlTitle       = "Enter emergency download mode?",
  .ConfirmEdlMsg = "The screen stays off in this mode. It's for Qualcomm flashing tools on a computer.",
  .ConfirmEdl    = "Enter EDL",
  .Cancel        = "Cancel",
  .AblSettingFailed = "Couldn't save the setting.",
  .AblModelFailed = "Couldn't save the device model.",
  .AblNotAnswering = "Couldn't read the current settings.",
  .AblRestartFailed = "Couldn't restart ({}).",
  .TargetNotKept = "Couldn't save the boot choice ({}).",
  .RestartingToScan = "Restarting to find device models.",
  .RestartToScanFailed = "Couldn't restart to find device models.",
  .ModelScanFailed = "Couldn't scan for device models.",
  .AblActionFailed = "Couldn't complete the request.",
  .PowerOffFailed = "Couldn't shut down.",
  .RestartingRecovery = "Restarting into Android recovery",
  .CfwRemoved = "PortareOS is removed. Recovery resets Android next.",
  .WipeInRecovery      = "PortareOS is removed. Choose Factory reset in recovery to start Android fresh.",
  .UninstallFailed = "Couldn't uninstall PortareOS",
  .Uninstalling        = "Uninstalling PortareOS",
  .DontTurnOff         = "Don't turn off the Nova.",
  .UninstallNotFound   = "PortareOS is no longer on internal storage.",
  .UninstallUntrusted = "The storage layout couldn't be verified, so nothing was changed.",
  .UninstallWriteFailed = "Couldn't update the storage layout ({}).",
  .UninstallReadBack = "The storage layout didn't read back as written.",
  .UninstallError = "Not enough memory to finish ({}).",
  .ErrWipeTitle        = "PortareOS is removed",
  .WipeByHand          = "Android couldn't be made to start first. Choose Factory reset in Android's recovery.",
  .Unknown             = "Unknown",
  .NotInserted         = "Not inserted",
  .SrcAblScan = "the storage scan",
  // BIOS
  .SectionStart = "Start",
  .SectionStorage = "Storage",
  .PortareOs = "PortareOS",
  .Android = "Android",
  .BootOrder = "Boot order",
  .UninstallPortareOs = "Uninstall PortareOS",
  .Restart = "Restart",
  .ShutDown = "Shut down",
  .OrderPortareOsFirst = "PortareOS first",
  .OrderAndroidFirst = "Android first",
  .HelpBoot = "Choose a system for this boot only.",
  .FromInternal = "From internal storage",
  .FromSdCard = "From the SD card",
  .ErrPickTitle = "Couldn't start {}",
  .ErrPickTitleAny = "Couldn't start the chosen system",
  .ErrPickGoneSd = "The SD card is no longer available.",
  .ErrPickGoneSdHint = "Reinsert the SD card, then try again.",
  .ErrPickGone = "Internal storage is no longer available.",
  .ErrPickFailed = "{} didn't start from {}.",
  .ErrPickFailedAny = "It didn't start.",
  .ErrPickHint = "Try again or choose another system.",
  .HelpBootOrder = "Sets the default system at power-on.",
  .HelpUninstall = "Erases PortareOS, then resets Android.",
  .HelpEdl = "For Qualcomm flashing tools on a computer.",
  .KeyPower = "POWER",
  .KeyVolume = "VOL+/-",
  .KeyEnter = "ENTER",
  .KeyArrows = "UP/DOWN",
  .VerbLaunch = "START",
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
