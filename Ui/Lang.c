/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - the menu's strings in English and Simplified Chinese.

  Scripts/mkfont.py reads the string literals here for the characters the
  CJK face needs, so every string the menu draws belongs in this file.
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
  .Starting        = "Starting {}",
  .ErrNoImageTitle = "Couldn't boot Linux",
  .ErrNoImageMsg   = "No bootable image was found on the selected boot source.",
  .ErrNoImageHint  = "Check the drive, or change Boot source in the menu.",
  .ErrLoadTitle    = "Couldn't start Linux",
  .ErrLoadHint =
    "Check the boot files on the drive, or change Boot source in the menu.",
  .ErrBootImageFailed =
    "The boot image didn't pass verification, or didn't start.",
  .ErrAndroidTitle       = "Android boot isn't available",
  .ErrAndroidMsg         = "This build can't start Android on this device yet.",
  .ErrAndroidHint        = "Set Boot mode to Linux in the menu.",
  .ErrAndroidFailedTitle = "Couldn't start Android",
  .ErrAndroidFailedMsg   = "Android's boot images didn't start.",
  .ErrAndroidFailedHint  = "The Log screen says why. Check the Android "
                          "partitions, or set Boot mode to Linux.",
  .ErrModelTitle = "Device model isn't set",
  .ErrModelMsg =
    "Linux needs your device model to choose the right device tree.",
  .PressAnyKeyMenu       = "Press any key to return to the menu",
  .PressAnyKeyModel      = "Press any key to choose your model",
  .ConfirmUninstallTitle = "Uninstall CFW?",
  .ConfirmUninstallMsg   = "Erases these partitions, and all of Android's "
                           "data with them:",
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
  .RestartingToScan    = "Restarting to scan",
  .RestartToScanFailed = "Couldn't restart to scan",
  .ModelScanFailed     = "Couldn't look for device models",
  .AblActionFailed     = "The ABL couldn't do that",
  .PowerOffFailed      = "Couldn't power off",
  .RestartingRecovery  = "Restarting into recovery",
  .CfwRemoved          = "Linux is removed. Recovery wipes Android's data next.",
  .WipeInRecovery      = "Linux is removed. Choose Factory reset in recovery to "
                         "wipe Android's data.",
  .UninstallFailed     = "Uninstall failed",
  .Uninstalling        = "Uninstalling CFW",
  .DontTurnOff         = "Don't turn off the device.",
  .UninstallNotFound   = "Linux is no longer on internal storage.",
  .UninstallUntrusted  = "The partition table can't be trusted, so nothing "
                         "was changed.",
  .UninstallWriteFailed = "Writing the partition table failed ({}).",
  .UninstallReadBack   = "The partition table didn't read back as written.",
  .UninstallError      = "Something went wrong ({}).",
  .ErrWipeTitle        = "Linux is removed",
  .WipeByHand          = "Android couldn't be made the boot mode. Set Boot Mode "
                         "to Android, then choose Factory reset in recovery.",
  .Unknown             = "Unknown",
  .NotInserted         = "Not inserted",
  .SrcAblScan          = "the ABL's scan",
  .EntryType1          = "BLS entry",
  .EntryUki            = "Unified kernel image",
  .InternalStorage     = "Internal storage",
  .RemovableStorage    = "Removable storage",
};

STATIC CONST LANG_STRINGS mZhHans = {
  .Language              = "语言",
  .SectionBoot           = "启动",
  .SectionSettings       = "设置",
  .SectionSystem         = "系统",
  .Start                 = "启动",
  .StartLinux            = "启动 Linux",
  .StartAndroid          = "启动 Android",
  .BootEntries           = "启动项",
  .BootImage             = "启动镜像",
  .DefaultFile           = "EFI 引导程序",
  .RestartBootloader     = "重启到引导程序",
  .RecoveryMode          = "进入 Recovery 模式",
  .Edl                   = "进入 9008 模式（EDL）",
  .OtherSlot             = "从另一槽位启动",
  .BootMode              = "启动模式",
  .AblMode               = "ABL 模式",
  .Verbose               = "详细",
  .Silent                = "静默",
  .VolumeUp              = "开机时按音量+按键",
  .VolLinuxExternal      = "Linux（外置存储）",
  .VolRecovery           = "Recovery 模式",
  .BootSource            = "启动来源",
  .Auto                  = "自动",
  .Internal              = "内部存储",
  .SdCard                = "SD 卡",
  .Charging              = "关机充电",
  .On                    = "开",
  .Off                   = "关",
  .Rotation              = "屏幕旋转",
  .CustomLogo            = "自定义启动徽标",
  .RotationAuto          = "自动（{}°）",
  .Timeout               = "自动启动倒计时",
  .Seconds               = "{} 秒",
  .DeviceModel           = "设备型号",
  .NotSet                = "未设置",
  .UninstallCfw          = "卸载 Linux",
  .PowerOff              = "关机",
  .ToastTargetExited     = "启动目标已退出",
  .ToastAndroidInAbl     = "Android 需在 ABL 自己的菜单中选择",
  .ToastFirmwareDt       = "使用固件自带的设备树",
  .ToastModelSet         = "设备型号已设为 {}",
  .LookingOn             = "正在从 {} 查找 Linux",
  .SrcFirstDrive         = "第一个可启动的驱动器",
  .SrcInternal           = "内部存储",
  .SrcSd                 = "SD 卡",
  .Countdown             = "{1} 秒后启动 {0}",
  .AnyKeyToStay          = "按任意键停留",
  .InfoRam               = "内存",
  .InfoStorage           = "存储",
  .InfoDisplay           = "屏幕",
  .InfoModel             = "型号",
  .InfoPanel             = "面板",
  .DeviceInfo            = "设备信息",
  .Log                   = "日志",
  .SectionDebug          = "调试",
  .DbgUsbLive            = "USB 主机热切换",
  .DbgQuiesce            = "交接时关闭显示",
  .DbgPanelLines         = "关闭面板背光与供电",
  .InfoChip              = "芯片",
  .InfoPlatform          = "平台",
  .InfoPmic              = "电源管理芯片",
  .InfoStorageChip       = "存储芯片",
  .InfoAndroid           = "Android 版本",
  .InfoBootloader        = "引导程序",
  .InfoUefi              = "UEFI 固件",
  .HintMove              = "上下",
  .HintSelect            = "选择",
  .HintBack              = "返回",
  .NoDeviceTrees         = "未找到设备树",
  .OneModel              = "找到 1 个型号",
  .NModels               = "找到 {} 个型号",
  .Current               = "当前",
  .FirmwareDefault       = "固件默认",
  .Back                  = "返回",
  .ModelExplain          = "型号决定 Linux 启动时使用的设备树和屏幕方向。",
  .FirmwareDtExplain     = "直接使用固件提供的设备树启动，不做更改。",
  .FoundIn               = "来自 {}。",
  .EntriesExplain        = "选择一项立即启动。“启动”按此顺序依次尝试。",
  .NoTriesLeft           = "启动尝试次数已用尽",
  .Default               = "默认",
  .BackToMenu            = "返回菜单。",
  .LookingForDts         = "正在查找设备树",
  .FewSeconds            = "这可能需要几秒钟。",
  .StartingAndroid       = "正在启动 Android",
  .StartingLinux         = "正在启动 Linux",
  .Starting              = "正在启动 {}",
  .ErrNoImageTitle       = "无法启动 Linux",
  .ErrNoImageMsg         = "所选启动来源上没有可启动的镜像。",
  .ErrNoImageHint        = "请检查驱动器，或在菜单中更改启动来源。",
  .ErrLoadTitle          = "无法运行 Linux",
  .ErrLoadHint           = "请检查驱动器上的启动文件，或在菜单中更改启动来源。",
  .ErrBootImageFailed    = "启动镜像未通过验证，或未能启动。",
  .ErrAndroidTitle       = "无法启动 Android",
  .ErrAndroidMsg         = "此版本暂不支持在本设备上启动 Android。",
  .ErrAndroidHint        = "请在菜单中将启动模式设为 Linux。",
  .ErrAndroidFailedTitle = "无法运行 Android",
  .ErrAndroidFailedMsg   = "Android 启动镜像未能启动。",
  .ErrAndroidFailedHint =
    "原因见日志页面。请检查 Android 分区，或将启动模式设为 Linux。",
  .ErrModelTitle         = "未设置设备型号",
  .ErrModelMsg           = "Linux 需要知道设备型号才能选择正确的设备树。",
  .PressAnyKeyMenu       = "按任意键返回菜单",
  .PressAnyKeyModel      = "按任意键选择型号",
  .ConfirmUninstallTitle = "确认卸载 Linux？",
  .ConfirmUninstallMsg = "将清除以下分区，以及 Android 的全部数据：",
  .ConfirmUninstall = "卸载",
  .ConfirmUninstallFinalTitle = "确认永久清除这些数据？",
  .ConfirmUninstallFinalMsg =
    "设备将重启进入 Recovery 模式，清除 Android 的全部数据。此操作无法撤销。",
  .ConfirmUninstallFinal = "清除",
  .ConfirmEdlTitle  = "确认进入 9008 模式（EDL）？",
  .ConfirmEdlMsg = "此模式下屏幕保持关闭，需要用电脑上的高通刷机工具才能退出。",
  .ConfirmEdl    = "进入",
  .Cancel        = "取消",
  .AblSettingFailed    = "ABL 无法更改该设置",
  .AblModelFailed      = "ABL 无法保存设备型号",
  .AblNotAnswering     = "ABL 没有响应",
  .AblRestartFailed    = "ABL 无法重启（{}）",
  .RestartingToScan    = "正在重启以扫描",
  .RestartToScanFailed = "无法重启以扫描",
  .ModelScanFailed     = "无法查找设备型号",
  .AblActionFailed     = "ABL 无法执行该操作",
  .PowerOffFailed      = "无法关机",
  .RestartingRecovery  = "正在重启进入 Recovery 模式",
  .CfwRemoved          = "已卸载 Linux。接下来将通过 Recovery 模式清除 Android 的数据。",
  .WipeInRecovery      = "已卸载 Linux。请在 Recovery 模式中选择恢复出厂设置，"
                         "清除 Android 的数据。",
  .UninstallFailed     = "卸载失败",
  .Uninstalling        = "正在卸载 Linux",
  .DontTurnOff         = "请勿关闭设备。",
  .UninstallNotFound   = "内部存储中已没有 Linux。",
  .UninstallUntrusted  = "分区表不可信，因此未做任何更改。",
  .UninstallWriteFailed = "写入分区表失败（{}）。",
  .UninstallReadBack   = "分区表读回的内容与写入的不一致。",
  .UninstallError      = "出现错误（{}）。",
  .ErrWipeTitle        = "已卸载 Linux",
  .WipeByHand          = "无法将启动模式设为 Android。请将启动模式设为 Android，"
                         "然后在 Recovery 模式中选择恢复出厂设置。",
  .Unknown             = "未知",
  .NotInserted         = "未插入",
  .SrcAblScan          = "ABL 的扫描",
  .EntryType1          = "BLS 启动项",
  .EntryUki            = "统一内核镜像（UKI）",
  .InternalStorage     = "内部存储",
  .RemovableStorage    = "外置存储",
};

STATIC CONST LANG_STRINGS *CONST mLangs[LANG_COUNT] = {
  [LANG_EN]      = &mEn,
  [LANG_ZH_HANS] = &mZhHans,
};

// Each language's name in itself, for the Language setting.
STATIC CONST CHAR8 *CONST mNames[LANG_COUNT] = {
  [LANG_EN]      = "English",
  [LANG_ZH_HANS] = "简体中文",
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
