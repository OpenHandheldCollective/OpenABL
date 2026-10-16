/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - the menu's strings, in every language the menu offers.

  The Language setting picks one at run time. The CJK face (made from these
  strings at build time) is a fallback for the characters the menu font
  lacks. {}, {0} and {1} mark where LangFill puts values.
*/

#ifndef __UI_LANG_H__
#define __UI_LANG_H__

#include "Base/OablBase.h"

// The stored values of the Language setting. Add new ones at the end.
typedef enum { LANG_EN, LANG_ZH_HANS, LANG_COUNT } LANG_ID;

typedef struct _LANG_STRINGS {
  CONST CHAR8 *Language;
  CONST CHAR8 *SectionBoot;
  CONST CHAR8 *SectionSettings;
  CONST CHAR8 *SectionSystem;
  CONST CHAR8 *Start;
  CONST CHAR8 *StartLinux;
  CONST CHAR8 *StartAndroid;
  CONST CHAR8 *BootEntries;
  CONST CHAR8 *BootImage;
  CONST CHAR8 *RestartBootloader;
  CONST CHAR8 *RecoveryMode;
  CONST CHAR8 *Edl;
  CONST CHAR8 *OtherSlot;
  CONST CHAR8 *BootMode;
  CONST CHAR8 *AblMode;
  CONST CHAR8 *Verbose;
  CONST CHAR8 *Silent;
  CONST CHAR8 *VolumeUp;
  CONST CHAR8 *VolLinuxExternal;
  CONST CHAR8 *VolRecovery;
  CONST CHAR8 *BootSource;
  CONST CHAR8 *Auto;
  CONST CHAR8 *Internal;
  CONST CHAR8 *SdCard;
  CONST CHAR8 *Charging;
  CONST CHAR8 *On;
  CONST CHAR8 *Off;
  CONST CHAR8 *Rotation;
  CONST CHAR8 *CustomLogo;
  CONST CHAR8 *RotationAuto;
  CONST CHAR8 *Timeout;
  CONST CHAR8 *Seconds;
  CONST CHAR8 *DeviceModel;
  CONST CHAR8 *NotSet;
  CONST CHAR8 *UninstallCfw;
  CONST CHAR8 *PowerOff;
  CONST CHAR8 *ToastTargetExited;
  CONST CHAR8 *ToastAndroidInAbl;
  CONST CHAR8 *ToastFirmwareDt;
  CONST CHAR8 *ToastModelSet;
  CONST CHAR8 *LookingOn;
  CONST CHAR8 *SrcFirstDrive;
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
  CONST CHAR8 *HintMove;
  CONST CHAR8 *HintSelect;
  CONST CHAR8 *HintBack;
  CONST CHAR8 *NoDeviceTrees;
  CONST CHAR8 *OneModel;
  CONST CHAR8 *NModels;
  CONST CHAR8 *Current;
  CONST CHAR8 *FirmwareDefault;
  CONST CHAR8 *Back;
  CONST CHAR8 *ModelExplain;
  CONST CHAR8 *FirmwareDtExplain;
  CONST CHAR8 *FoundIn;
  CONST CHAR8 *EntriesExplain;
  CONST CHAR8 *NoTriesLeft;
  CONST CHAR8 *Default;
  CONST CHAR8 *BackToMenu;
  CONST CHAR8 *LookingForDts;
  CONST CHAR8 *FewSeconds;
  CONST CHAR8 *StartingAndroid;
  CONST CHAR8 *StartingLinux;
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
  CONST CHAR8 *PressAnyKeyMenu;
  CONST CHAR8 *PressAnyKeyModel;
  CONST CHAR8 *ConfirmUninstallTitle;
  CONST CHAR8 *ConfirmUninstallMsg;
  CONST CHAR8 *ConfirmUninstall;
  CONST CHAR8 *ConfirmUninstallFinalTitle;
  CONST CHAR8 *ConfirmUninstallFinalMsg;
  CONST CHAR8 *ConfirmUninstallFinal;
  CONST CHAR8 *ConfirmEdlTitle;
  CONST CHAR8 *ConfirmEdlMsg;
  CONST CHAR8 *ConfirmEdl;
  CONST CHAR8 *Cancel;
  CONST CHAR8 *AblSettingFailed;
  CONST CHAR8 *AblModelFailed;
  CONST CHAR8 *AblNotAnswering;
  CONST CHAR8 *AblRestartFailed;
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
