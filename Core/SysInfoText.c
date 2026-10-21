/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SysInfoText - Text for the system facts on the menu's info card.
*/

#include "Core/SysInfoText.h"

#include "Base/Memory.h"
#include "Base/Print.h"

#define MIB ((UINT64)1 << 20)
#define GIB ((UINT64)1 << 30)

VOID
SysInfoFree (IN OUT SYS_INFO *Info)
{
  FreePool (Info->Soc);
  FreePool (Info->Ram);
  FreePool (Info->Storage);
  FreePool (Info->SdCard);
  FreePool (Info->Firmware);
  *Info = (SYS_INFO){0};
}

CHAR8 *
SysInfoFormatBytes (IN UINT64 Bytes)
{
  UINT64 Tenths;

  if (Bytes < GIB) {
    return SPrintAlloc ("%llu MB", (UINT64)((Bytes + MIB / 2) / MIB));
  }
  Tenths = (Bytes * 10 + GIB / 2) / GIB;
  if (Tenths % 10 == 0 || Tenths >= 100) {
    return SPrintAlloc ("%llu GB", (UINT64)((Tenths + 5) / 10));
  }
  return SPrintAlloc (
    "%llu.%llu GB", (UINT64)(Tenths / 10), (UINT64)(Tenths % 10)
  );
}

CONST CHAR8 *
SysInfoPlatformName (IN UINT32 Type)
{
  switch (Type) {
  case 0x01:
    return "CDP";
  case 0x02:
    return "FFA";
  case 0x03:
    return "FLUID";
  case 0x05:
    return "OEM";
  case 0x06:
    return "QT";
  case 0x08:
    return "MTP";
  case 0x09:
    return "LIQUID";
  case 0x0A:
    return "DragonBoard";
  case 0x0B:
    return "QRD";
  case 0x0C:
    return "EVB";
  case 0x0D:
    return "HRD";
  case 0x1F:
    return "HDK";
  case 0x20:
    return "IOT";
  case 0x21:
    return "ATP";
  case 0x22:
    return "IDP";
  case 0x28:
    return "CRD";
  default:
    return NULL;
  }
}

CONST CHAR8 *
SysInfoSocName (IN UINT32 ChipId)
{
  switch (ChipId) {
  case 321:
    return "Snapdragon 845";
  case 356:
    return "Snapdragon 865";
  case 444:
    return "Snapdragon 662";
  case 467:
    return "Dragonwing QCS6125";
  case 601:
    return "Snapdragon G3x Gen 2";
  case 603:
    return "Dragonwing QCS8550";
  case 634:
    return "Snapdragon G2 Gen 1";
  case 639:
    return "Snapdragon 8 Elite";
  case 654:
    return "Snapdragon G1 Gen 2";
  case 682:
    return "Snapdragon G3 Gen 3";
  case 700:
    return "Snapdragon G2 Gen 2";
  case 706:
    return "Dragonwing Q8750";
  case 732:
    return "Dragonwing Q7790";
  default:
    return NULL;
  }
}

CHAR8 *
SysInfoPanelFromCmdline (IN OABL_TEXT PanelConfig)
{
  STATIC CONST CHAR8 Key[] = "dsi_display0=";
  OABL_TEXT          Name;
  UINTN              Start;
  UINTN              Len;

  if (!TextFind (PanelConfig, Key, &Start)) {
    return NULL;
  }
  Start += sizeof (Key) - 1;
  for (Len = 0; Start + Len < PanelConfig.Len; Len++) {
    CHAR8 Char = PanelConfig.Ptr[Start + Len];
    if (Char == ':' || Char == ' ' || Char == ';' || Char == 0) {
      break;
    }
  }
  Name = TextSub (PanelConfig, Start, Start + Len);
  if (TextStartsWith (Name, "qcom,")) {
    Name = TextSub (Name, 5, Name.Len);
  }
  if (TextStartsWith (Name, "mdss_dsi_panel_")) {
    Name = TextSub (Name, 15, Name.Len);
  } else if (TextStartsWith (Name, "mdss_dsi_")) {
    Name = TextSub (Name, 9, Name.Len);
  }
  return Name.Len ? TextDup (Name) : NULL;
}

// Checks for Key followed by a NUL at offset At of Data.
STATIC BOOLEAN
KeyAt (
  IN CONST UINT8 *Data,
  IN UINTN        At,
  IN CONST CHAR8 *Key,
  IN UINTN        KeyLen
)
{
  for (UINTN Index = 0; Index < KeyLen; Index++) {
    if (Data[At + Index] != (UINT8)Key[Index]) {
      return FALSE;
    }
  }
  return Data[At + KeyLen] == 0;
}

CHAR8 *
SysInfoVbmetaProperty (
  IN CONST UINT8        *Image,
  IN UINTN               Size,
  IN CONST CHAR8 *CONST *Keys,
  IN UINTN               KeyCount
)
{
  for (UINTN Index = 0; Index < KeyCount; Index++) {
    UINTN   KeyLen = AsciiStrLen (Keys[Index]);
    UINTN   At;
    UINTN   Len;
    BOOLEAN Found = FALSE;
    // Find the first occurrence of the key and its NUL.
    for (At = 0; KeyLen + 1 <= Size && At <= Size - (KeyLen + 1); At++) {
      if (KeyAt (Image, At, Keys[Index], KeyLen)) {
        Found = TRUE;
        break;
      }
    }
    if (!Found) {
      continue;
    }
    At += KeyLen + 1;
    // The value runs to the next NUL and must be non-empty UTF-8.
    for (Len = 0; At + Len < Size && Image[At + Len]; Len++)
      ;
    if (At + Len == Size || !Utf8Valid (Image + At, Len) || !Len) {
      continue;
    }
    return TextDup ((OABL_TEXT){(CONST CHAR8 *)Image + At, Len});
  }
  return NULL;
}

STATIC CHAR8 *
Gb (IN UINT64 Gigabytes)
{
  return SPrintAlloc ("%llu GB", (UINT64)Gigabytes);
}

CHAR8 *
SysInfoRamSize (IN UINT64 Bytes)
{
  UINT64 Mb = Bytes / MIB;
  UINT64 Size;

  if (Mb >= 30001) {
    Size = 32;
  } else if (Mb >= 22001) {
    Size = 24;
  } else if (Mb >= 14001) {
    Size = 16;
  } else if (Mb >= 10501) {
    Size = 12;
  } else if (Mb >= 7001) {
    Size = 8;
  } else if (Mb >= 5001) {
    Size = 6;
  } else if (Mb >= 3001) {
    Size = 4;
  } else {
    Size = Mb / 1024 + (Mb % 1024 > 512);
  }
  return Gb (Size);
}

// A marketed size. Snap picks the first step whose Min, in GiB, the size
// reaches, or else prints "N GB".
typedef struct _STEP {
  UINT64       Min;
  CONST CHAR8 *Text;
} STEP;

STATIC CHAR8 *
Snap (
  IN UINT64      Bytes,
  IN CONST STEP *Steps,
  IN UINTN       Count
)
{
  UINT64 Gib = Bytes / GIB;

  if (Gib == 0) {
    return SysInfoFormatBytes (Bytes);
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    if (Gib >= Steps[Index].Min) {
      return AsciiStrDup (Steps[Index].Text);
    }
  }
  return Gb (Gib);
}

CHAR8 *
SysInfoStorageSize (IN UINT64 Bytes)
{
  STATIC CONST STEP Steps[] = {
    {1801, "2 TB"},  {901, "1 TB"}, {461, "512 GB"}, {221, "256 GB"},
    {111, "128 GB"}, {51, "64 GB"}, {26, "32 GB"},
  };
  return Snap (Bytes, Steps, sizeof (Steps) / sizeof (*Steps));
}

CHAR8 *
SysInfoCardSize (IN UINT64 Bytes)
{
  STATIC CONST STEP Steps[] = {
    {1801, "2 TB"},  {1351, "1.5 TB"}, {901, "1 TB"},   {461, "512 GB"},
    {361, "400 GB"}, {221, "256 GB"},  {171, "200 GB"}, {111, "128 GB"},
    {51, "64 GB"},   {26, "32 GB"},    {11, "16 GB"},   {6, "8 GB"},
  };
  return Snap (Bytes, Steps, sizeof (Steps) / sizeof (*Steps));
}
