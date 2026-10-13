/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - the menu's strings in English and Simplified Chinese.
*/

#include "Ui/Lang.h"
#include "Base/Text.h"

#ifndef OABL_ZH_HANS
STATIC CONST LANG_STRINGS mEn = {
  .SectionBoot       = "Boot",
  .SectionSettings   = "Settings",
  .SectionSystem     = "System",
  .Start             = "Start",
  .StartLinux        = "Start Linux",
  .StartAndroid      = "Start Android",
  .BootEntries       = "Boot entries",
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
  .KeepLogo          = "Keep logo through boot",
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
  .ConfirmUninstallMsg   = "Removes the Linux install from internal storage. "
                           "Android's data stays; Android gets the space back "
                           "after its next factory reset. This can't be undone.",
  .ConfirmUninstall      = "Uninstall",
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
  .CfwRemoved          = "Linux was removed from internal storage",
  .UninstallFailed     = "Uninstall failed",
  .Unknown             = "Unknown",
  .NotInserted         = "Not inserted",
  .SrcAblScan          = "the ABL's scan",
};
#endif

#ifdef OABL_ZH_HANS
STATIC CONST LANG_STRINGS mZhHans = {
  .SectionBoot           = "启动",
  .SectionSettings       = "设置",
  .SectionSystem         = "系统",
  .Start                 = "启动",
  .StartLinux            = "启动 Linux",
  .StartAndroid          = "启动 Android",
  .BootEntries           = "启动项",
  .RestartBootloader     = "重启到引导程序",
  .RecoveryMode          = "恢复模式",
  .Edl                   = "紧急下载模式",
  .OtherSlot             = "从另一槽位启动",
  .BootMode              = "启动模式",
  .AblMode               = "ABL 模式",
  .Verbose               = "详细",
  .Silent                = "静默",
  .VolumeUp              = "开机时按音量+",
  .VolLinuxExternal      = "Linux（外部）",
  .VolRecovery           = "恢复模式",
  .BootSource            = "启动来源",
  .Auto                  = "自动",
  .Internal              = "内部存储",
  .SdCard                = "SD 卡",
  .Charging              = "关机充电",
  .On                    = "开",
  .Off                   = "关",
  .Rotation              = "屏幕旋转",
  .CustomLogo            = "自定义启动徽标",
  .KeepLogo              = "启动全程保留徽标",
  .RotationAuto          = "自动（{}°）",
  .Timeout               = "菜单超时",
  .Seconds               = "{} 秒",
  .DeviceModel           = "设备型号",
  .NotSet                = "未设置",
  .UninstallCfw          = "卸载 CFW",
  .PowerOff              = "关机",
  .ToastTargetExited     = "启动目标已退出",
  .ToastAndroidInAbl     = "Android 需在 ABL 自己的菜单中选择",
  .ToastFirmwareDt       = "使用固件自带的设备树",
  .ToastModelSet         = "设备型号已设为 {}",
  .LookingOn             = "正在{}上查找 Linux",
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
  .DbgPanelLines         = "关闭面板供电",
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
  .NoTriesLeft           = "已无剩余尝试",
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
  .ConfirmUninstallTitle = "卸载 CFW？",
  .ConfirmUninstallMsg =
    "将从内部存储中删除 Linux，Android 的数据保留；释放的空间在 Android "
    "下次恢复出厂设置后可用。此操作无法撤销。",
  .ConfirmUninstall = "卸载",
  .ConfirmEdlTitle  = "进入紧急下载模式？",
  .ConfirmEdlMsg = "此模式下屏幕保持关闭，需要用电脑上的高通刷机工具才能退出。",
  .ConfirmEdl    = "进入 EDL",
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
  .CfwRemoved          = "已从内部存储删除 Linux",
  .UninstallFailed     = "卸载失败",
  .Unknown             = "未知",
  .NotInserted         = "未插入",
  .SrcAblScan          = "ABL 的扫描",
};
#endif

#ifdef OABL_ZH_HANS
CONST LANG_STRINGS *CONST gStrings = &mZhHans;
#else
CONST LANG_STRINGS *CONST gStrings = &mEn;
#endif

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
