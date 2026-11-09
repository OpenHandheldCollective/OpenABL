/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Ko - the menu's strings in Korean.
*/

#include "Ui/Lang.h"

CONST LANG_STRINGS gLangKo = {
  .Name                      = "한국어",
  .LanguageExplain           = "메뉴에 표시할 언어를 선택합니다.",
  .Language                  = "언어",
  .SectionBoot               = "부팅",
  .SectionSettings           = "설정",
  .SectionSystem             = "시스템",
  .Start                     = "시작",
  .StartLinux                = "Linux 시작",
  .StartAndroid              = "Android 시작",
  .BootEntries               = "부팅 항목",
  .DefaultFile               = "EFI 로더",
  .RestartBootloader         = "부트로더로 재시작",
  .RecoveryMode              = "복구 모드",
  .Edl                       = "긴급 다운로드 모드 (EDL)",
  .OtherSlot                 = "다른 슬롯으로 부팅",
  .BootMode                  = "부팅 모드",
  .AblMode                   = "ABL 모드",
  .Verbose                   = "상세 출력",
  .Silent                    = "출력 안 함",
  .VolumeUp                  = "부팅 시 볼륨+ 버튼",
  .VolLinuxExternal          = "Linux (외부 저장소)",
  .VolRecovery               = "복구 모드",
  .BootSource                = "부팅 위치",
  .Auto                      = "자동",
  .Internal                  = "내부 저장소",
  .SdCard                    = "SD 카드",
  .Charging                  = "전원이 꺼진 상태에서 충전",
  .On                        = "켜짐",
  .Off                       = "꺼짐",
  .Rotation                  = "화면 회전",
  .ReverseVolume             = "볼륨 버튼 반전",
  .CustomLogo                = "사용자 부팅 로고",
  .RotationAuto              = "자동 ({}°)",
  .DeviceModel               = "기기 모델",
  .NotSet                    = "선택 안 함",
  .UninstallCfw              = "CFW 제거",
  .PowerOff                  = "전원 끄기",
  .ToastTargetExited         = "부팅 대상이 종료되었습니다",
  .ToastAndroidInAbl         = "Android는 ABL 자체 메뉴에서 선택합니다",
  .ToastFirmwareDt           = "펌웨어의 장치 트리를 사용합니다",
  .ToastModelSet             = "기기 모델을 {}(으)로 설정했습니다",
  .LookingOn                 = "{}에서 Linux 찾는 중",
  .SrcFirstDrive             = "첫 번째 부팅 가능한 드라이브",
  .SrcInternal               = "내부 저장소",
  .SrcSd                     = "SD 카드",
  .Countdown                 = "{1}초 후 {0} 시작",
  .AnyKeyToStay              = "아무 키나 누르면 메뉴에 머뭅니다",
  .InfoRam                   = "메모리",
  .InfoStorage               = "저장소",
  .InfoDisplay               = "화면",
  .InfoModel                 = "모델",
  .InfoPanel                 = "패널",
  .DeviceInfo                = "기기 정보",
  .DeveloperOptions             = "개발자 옵션",
  .DeveloperOptionsUsbLive       = "USB 호스트 즉시 시작",
  .DeveloperOptionsQuiesce       = "인계 시 화면 끄기",
  .DeveloperOptionsPanelLines    = "패널 전원도 끄기",
  .DeveloperOptionsBootLog       = "화면에 부팅 로그 표시",
  .DeveloperOptionsSdDiagnostics = "SD 카드 진단",
  .DeveloperOptionsReset         = "OpenABL 설정 초기화",
  .DeveloperOptionsResetFailed   = "설정을 초기화하지 못했습니다",
  .DeveloperOptionsStepsAway =
    "{}번 더 선택하면 개발자 옵션이 잠금 해제됩니다",
  .DeveloperOptionsStepAway =
    "한 번 더 선택하면 개발자 옵션이 잠금 해제됩니다",
  .DeveloperOptionsUnlocked      = "개발자 옵션이 잠금 해제되었습니다!",
  .Log                       = "로그",
  .InfoChip                  = "칩",
  .InfoPlatform              = "플랫폼",
  .InfoPmic                  = "전원 관리 칩",
  .InfoStorageChip           = "저장소 칩",
  .InfoAndroid               = "Android 빌드",
  .InfoBootloader            = "부트로더",
  .InfoUefi                  = "UEFI 펌웨어",
  .HintMove                  = "위아래 이동",
  .HintSelect                = "선택",
  .HintBack                  = "뒤로",
  .NoDeviceTrees             = "장치 트리를 찾지 못했습니다",
  .OneModel                  = "모델 1개 발견",
  .NModels                   = "모델 {}개 발견",
  .Current                   = "현재",
  .FirmwareDefault           = "펌웨어 기본값",
  .Back                      = "뒤로",
  .ModelExplain =
    "모델에 따라 Linux 부팅에 사용할 장치 트리와 화면 방향이 "
    "결정됩니다.",
  .FirmwareDtExplain =
    "펌웨어가 제공하는 장치 트리를 변경 없이 사용하여 "
    "부팅합니다.",
  .FoundIn                   = " {}에서 발견했습니다.",
  .EntriesExplain =
    "항목을 선택하면 바로 시작합니다. '시작'은 이 순서대로 부팅을 "
    "시도합니다.",
  .NoTriesLeft               = "남은 부팅 시도 횟수 없음",
  .Default                   = "기본값",
  .BackToMenu                = "메뉴로 돌아갑니다.",
  .LookingForDts             = "장치 트리 찾는 중",
  .FewSeconds                = "몇 초 정도 걸릴 수 있습니다.",
  .StartingAndroid           = "Android 시작 중",
  .StartingLinux             = "Linux 시작 중",
  .Starting                  = "{} 시작 중",
  .ErrNoImageTitle           = "Linux 부팅 실패",
  .ErrNoImageMsg =
    "선택한 부팅 위치에서 부팅 가능한 이미지를 찾지 못했습니다.",
  .ErrNoImageHint =
    "드라이브를 확인하거나 메뉴에서 부팅 위치를 변경하세요.",
  .ErrLoadTitle              = "Linux 시작 실패",
  .ErrLoadHint =
    "드라이브의 부팅 파일을 확인하거나 메뉴에서 부팅 위치를 "
    "변경하세요.",
  .ErrAndroidTitle           = "Android 부팅을 사용할 수 없음",
  .ErrAndroidMsg =
    "이 빌드는 아직 이 기기의 Android 부팅을 지원하지 않습니다.",
  .ErrAndroidHint            = "메뉴에서 부팅 모드를 Linux로 설정하세요.",
  .ErrAndroidFailedTitle     = "Android 시작 실패",
  .ErrAndroidFailedMsg       = "Android 부팅 이미지를 시작하지 못했습니다.",
  .ErrAndroidFailedHint =
    "로그 화면에서 원인을 확인하세요. Android 파티션을 확인하거나 "
    "부팅 모드를 Linux로 설정하세요.",
  .PressAnyKeyMenu           = "아무 키나 누르면 메뉴로 돌아갑니다",
  .ConfirmUninstallTitle     = "CFW를 제거할까요?",
  .ConfirmUninstallMsg =
    "다음 파티션과 Android의 모든 데이터가 삭제됩니다:",
  .ConfirmUninstall          = "제거",
  .ConfirmUninstallFinalTitle = "데이터를 영구 삭제할까요?",
  .ConfirmUninstallFinalMsg =
    "기기가 복구 모드로 재시작하여 Android의 모든 데이터를 "
    "삭제합니다. 이 작업은 되돌릴 수 없습니다.",
  .ConfirmUninstallFinal     = "삭제",
  .ConfirmEdlTitle           = "긴급 다운로드 모드로 진입할까요?",
  .ConfirmEdlMsg =
    "이 모드에서는 화면이 꺼져 있습니다. 모드를 종료하려면 Qualcomm "
    "플래싱 도구가 설치된 컴퓨터가 필요합니다.",
  .ConfirmEdl                = "EDL 진입",
  .Cancel                    = "취소",
  .AblSettingFailed          = "ABL이 설정을 변경하지 못했습니다",
  .AblModelFailed            = "ABL이 기기 모델을 저장하지 못했습니다",
  .AblNotAnswering           = "ABL이 응답하지 않습니다",
  .AblRestartFailed          = "ABL 재시작 실패 ({})",
  .RestartingToScan          = "검색을 위해 재시작 중",
  .RestartToScanFailed       = "검색을 위한 재시작 실패",
  .ModelScanFailed           = "기기 모델을 검색하지 못했습니다",
  .AblActionFailed           = "ABL이 작업을 수행하지 못했습니다",
  .PowerOffFailed            = "전원을 끄지 못했습니다",
  .RestartingRecovery        = "복구 모드로 재시작 중",
  .CfwRemoved =
    "Linux가 제거되었습니다. 다음으로 복구 모드에서 Android 데이터를 "
    "삭제합니다.",
  .WipeInRecovery =
    "Linux가 제거되었습니다. 복구 모드에서 Factory reset을 선택하여 "
    "Android 데이터를 삭제하세요.",
  .UninstallFailed           = "제거 실패",
  .Uninstalling              = "CFW 제거 중",
  .DontTurnOff               = "기기의 전원을 끄지 마세요.",
  .UninstallNotFound         = "내부 저장소에 Linux가 없습니다.",
  .UninstallUntrusted =
    "파티션 테이블을 신뢰할 수 없어 아무것도 변경하지 "
    "않았습니다.",
  .UninstallWriteFailed      = "파티션 테이블 쓰기 실패 ({}).",
  .UninstallReadBack =
    "다시 읽은 파티션 테이블이 기록한 내용과 일치하지 않습니다.",
  .UninstallError            = "오류가 발생했습니다 ({}).",
  .ErrWipeTitle              = "Linux 제거 완료",
  .WipeByHand =
    "부팅 모드를 Android로 설정하지 못했습니다. 부팅 모드를 Android로 "
    "설정한 뒤 복구 모드에서 Factory reset을 선택하세요.",
  .Unknown                   = "알 수 없음",
  .NotInserted               = "삽입 안 됨",
  .SrcAblScan                = "ABL 검색",
  .EntryType1                = "BLS 부팅 항목",
  .EntryUki                  = "통합 커널 이미지",
  .InternalStorage           = "내부 저장소",
  .RemovableStorage          = "이동식 저장소",
};
