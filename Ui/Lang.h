/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - the menu's strings. English only: the menu draws CP437 glyphs.

  {}, {0} and {1} mark where LangFill puts values.
*/

#ifndef __UI_LANG_H__
#define __UI_LANG_H__

#include "Base/OablBase.h"

// The stored values of the Language setting. Add new ones at the end.
typedef enum { LANG_EN, LANG_COUNT } LANG_ID;

typedef struct _LANG_STRINGS {
  CONST CHAR8 *SectionSystem;
  CONST CHAR8 *Boot;
  CONST CHAR8 *Edl;
  CONST CHAR8 *Internal;
  CONST CHAR8 *SdCard;
  CONST CHAR8 *On;
  CONST CHAR8 *Off;
  CONST CHAR8 *DeviceModel;
  CONST CHAR8 *NotSet;
  CONST CHAR8 *ToastTargetExited;
  CONST CHAR8 *ToastFirmwareDt;
  CONST CHAR8 *ToastModelSet;
  CONST CHAR8 *SrcInternal;
  CONST CHAR8 *SrcSd;
  CONST CHAR8 *Countdown;
  CONST CHAR8 *AnyKeyToStay;
  CONST CHAR8 *InfoRam;
  CONST CHAR8 *InfoStorage;
  CONST CHAR8 *InfoDisplay;
  CONST CHAR8 *InfoModel;
  CONST CHAR8 *InfoPanel;
  CONST CHAR8 *DeviceInfo;
  CONST CHAR8 *Log;
  CONST CHAR8 *SectionDebug;
  CONST CHAR8 *DbgUsbLive;
  CONST CHAR8 *DbgQuiesce;
  CONST CHAR8 *DbgPanelLines;
  CONST CHAR8 *InfoChip;
  CONST CHAR8 *InfoPlatform;
  CONST CHAR8 *InfoPmic;
  CONST CHAR8 *InfoStorageChip;
  CONST CHAR8 *InfoAndroid;
  CONST CHAR8 *InfoBootloader;
  CONST CHAR8 *InfoUefi;
  CONST CHAR8 *Current;
  CONST CHAR8 *FirmwareDefault;
  CONST CHAR8 *Back;
  CONST CHAR8 *NoTriesLeft;
  CONST CHAR8 *LookingForDts;
  CONST CHAR8 *FewSeconds;
  CONST CHAR8 *Starting;
  CONST CHAR8 *ErrNoImageTitle;
  CONST CHAR8 *ErrNoImageMsg;
  CONST CHAR8 *ErrNoImageHint;
  CONST CHAR8 *ErrLoadTitle;
  CONST CHAR8 *ErrLoadHint;
  CONST CHAR8 *ErrBootImageFailed;
  CONST CHAR8 *ErrAndroidTitle;
  CONST CHAR8 *ErrAndroidMsg;
  CONST CHAR8 *ErrAndroidHint;
  CONST CHAR8 *ErrAndroidFailedTitle;
  CONST CHAR8 *ErrAndroidFailedMsg;
  CONST CHAR8 *ErrAndroidFailedHint;
  CONST CHAR8 *ErrModelTitle;
  CONST CHAR8 *ErrModelMsg;
  CONST CHAR8 *ConfirmUninstallTitle;
  CONST CHAR8 *ConfirmUninstallMsg;
  CONST CHAR8 *ConfirmUninstall;
  CONST CHAR8 *ConfirmEdlTitle;
  CONST CHAR8 *ConfirmEdlMsg;
  CONST CHAR8 *ConfirmEdl;
  CONST CHAR8 *Cancel;
  CONST CHAR8 *AblSettingFailed;
  CONST CHAR8 *AblModelFailed;
  CONST CHAR8 *AblNotAnswering;
  CONST CHAR8 *AblRestartFailed;
  CONST CHAR8 *TargetNotKept;
  CONST CHAR8 *RestartingToScan;
  CONST CHAR8 *RestartToScanFailed;
  CONST CHAR8 *ModelScanFailed;
  CONST CHAR8 *AblActionFailed;
  CONST CHAR8 *PowerOffFailed;
  CONST CHAR8 *RestartingRecovery;
  CONST CHAR8 *CfwRemoved;
  CONST CHAR8 *WipeInRecovery;
  CONST CHAR8 *UninstallFailed;
  CONST CHAR8 *Uninstalling;
  CONST CHAR8 *DontTurnOff;
  CONST CHAR8 *UninstallNotFound;
  CONST CHAR8 *UninstallUntrusted;
  CONST CHAR8 *UninstallWriteFailed;
  CONST CHAR8 *UninstallReadBack;
  CONST CHAR8 *UninstallError;
  CONST CHAR8 *ErrWipeTitle;
  CONST CHAR8 *WipeByHand;
  CONST CHAR8 *Unknown;
  CONST CHAR8 *NotInserted;
  CONST CHAR8 *SrcAblScan;
  // BIOS
  CONST CHAR8 *SectionStart;
  CONST CHAR8 *SectionStorage;
  CONST CHAR8 *PortareOs;
  CONST CHAR8 *Android;
  CONST CHAR8 *BootOrder;
  CONST CHAR8 *UninstallPortareOs;
  CONST CHAR8 *Restart;
  CONST CHAR8 *ShutDown;
  CONST CHAR8 *OrderPortareOsFirst;
  CONST CHAR8 *OrderAndroidFirst;
  CONST CHAR8 *HelpBoot;
  CONST CHAR8 *FromInternal;
  CONST CHAR8 *FromSdCard;
  CONST CHAR8 *ErrPickTitle;
  CONST CHAR8 *ErrPickTitleAny;
  CONST CHAR8 *ErrPickGoneSd;
  CONST CHAR8 *ErrPickGoneSdHint;
  CONST CHAR8 *ErrPickGone;
  CONST CHAR8 *ErrPickFailed;
  CONST CHAR8 *ErrPickFailedAny;
  CONST CHAR8 *ErrPickHint;
  CONST CHAR8 *HelpBootOrder;
  CONST CHAR8 *HelpUninstall;
  CONST CHAR8 *HelpEdl;
  CONST CHAR8 *InstallPortareOs;
  CONST CHAR8 *HelpInstall;
  CONST CHAR8 *Dualboot;
  CONST CHAR8 *ReplaceAndroid;
  CONST CHAR8 *ValueKeepAndroid;
  CONST CHAR8 *ValueUpdate;
  CONST CHAR8 *ValueAllSpace;
  CONST CHAR8 *Unavailable;
  CONST CHAR8 *NoteDualboot;
  CONST CHAR8 *NoteUpdate;
  CONST CHAR8 *NoteReplace;
  CONST CHAR8 *NoteReplaceAll;
  CONST CHAR8 *NoteNoAndroid;
  CONST CHAR8 *NoteUnknownLayout;
  CONST CHAR8 *NoteNoRoom;
  CONST CHAR8 *SizeTitle;
  CONST CHAR8 *SizeAndroid;
  CONST CHAR8 *SizePortareOs;
  CONST CHAR8 *ConfirmInstallTitle;
  CONST CHAR8 *ConfirmDualbootMsg;
  CONST CHAR8 *ConfirmUpdateMsg;
  CONST CHAR8 *ConfirmReplaceMsg;
  CONST CHAR8 *ConfirmReplaceAllMsg;
  CONST CHAR8 *ConfirmInstall;
  CONST CHAR8 *Installing;
  CONST CHAR8 *StagePrepare;
  CONST CHAR8 *StageStorage;
  CONST CHAR8 *StageCopy;
  CONST CHAR8 *StageCheck;
  CONST CHAR8 *Installed;
  CONST CHAR8 *InstalledMsg;
  CONST CHAR8 *VerbRestart;
  CONST CHAR8 *NoCardTitle;
  CONST CHAR8 *NoCardMsg;
  CONST CHAR8 *NoCardHint;
  CONST CHAR8 *InstallFailed;
  CONST CHAR8 *InstallNoCard;
  CONST CHAR8 *InstallUntrusted;
  CONST CHAR8 *InstallNoRoom;
  CONST CHAR8 *InstallLowBattery;
  CONST CHAR8 *InstallReadBack;
  CONST CHAR8 *InstallWriteFailed;
  CONST CHAR8 *InstallChangedHint;
  CONST CHAR8 *Charging;
  CONST CHAR8 *ChargerOut;
  CONST CHAR8 *BatteryUnknown;
  CONST CHAR8 *ChargingHint;
  CONST CHAR8 *KeyPower;
  CONST CHAR8 *KeyVolume;
  CONST CHAR8 *KeyEnter;
  CONST CHAR8 *KeyArrows;
  CONST CHAR8 *VerbLaunch;
  CONST CHAR8 *VerbChange;
  CONST CHAR8 *VerbSelect;
  CONST CHAR8 *VerbMenu;
  CONST CHAR8 *VerbMove;
} LANG_STRINGS;

// Strings in the current language, English until LangSet.
extern CONST LANG_STRINGS *gStrings;

// Switches the strings to Lang. An unknown Lang is English.
VOID
LangSet (IN UINT8 Lang);

// Returns Lang's name, in Lang.
CONST CHAR8 *
LangName (IN UINT8 Lang);

// Writes Template to Out with Args filled in. {} takes the next argument
// and {0} or {1} a given one, so word order can differ between languages.
// Placeholders past ArgCount are left empty.
VOID
LangFill (
  OUT CHAR8             *Out,
  IN UINTN               Cap,
  IN CONST CHAR8        *Template,
  IN CONST CHAR8 *CONST *Args,
  IN UINTN               ArgCount
);

#endif
