/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  FwUpdate - checks for a staged ABL image.
*/

#include "Core/FwUpdate.h"

#include <stdarg.h>

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Core/EfiData.h"

CONST CHAR8 *
FwUpdateStatusName (IN UINT32 Status)
{
  switch (Status) {
  case FW_UPDATE_SUCCESS:
    return "success";
  case FW_UPDATE_ERROR_UNSUCCESSFUL:
    return "unsuccessful";
  case FW_UPDATE_ERROR_RESOURCES:
    return "too large";
  case FW_UPDATE_ERROR_VERSION:
    return "not a newer version";
  case FW_UPDATE_ERROR_FORMAT:
    return "not a valid image for this device";
  case FW_UPDATE_ERROR_AUTH:
    return "damaged";
  case FW_UPDATE_ERROR_BATTERY:
    return "battery too low";
  }
  return "?";
}

STATIC INT32
HexDigit (IN CHAR8 Char)
{
  if (Char >= '0' && Char <= '9') {
    return Char - '0';
  }
  if ((Char | 0x20) >= 'a' && (Char | 0x20) <= 'f') {
    return (Char | 0x20) - 'a' + 10;
  }
  return -1;
}

BOOLEAN
FwUpdateSha256Line (
  IN OABL_TEXT    Line,
  IN CONST CHAR8 *Name,
  OUT UINT8       Digest[SHA256_SIZE]
)
{
  OABL_TEXT File;
  UINTN     At;

  if (TextFindChar (Line, '\n', &At)) {
    Line = TextSub (Line, 0, At);
  }
  if (Line.Len < 2 * SHA256_SIZE) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < SHA256_SIZE; Index++) {
    INT32 High = HexDigit (Line.Ptr[2 * Index]);
    INT32 Low  = HexDigit (Line.Ptr[2 * Index + 1]);

    if (High < 0 || Low < 0) {
      return FALSE;
    }
    Digest[Index] = (UINT8)(High << 4 | Low);
  }
  // "  name", or " *name" in binary mode.
  File = TextTrim (TextSub (Line, 2 * SHA256_SIZE, Line.Len));
  if (File.Len && File.Ptr[0] == '*') {
    File = TextSub (File, 1, File.Len);
  }
  if (TextFindLastChar (File, '/', &At)) {
    File = TextSub (File, At + 1, File.Len);
  }
  return File.Len == 0 || TextEqStr (File, Name);
}

STATIC VOID
Refuse (
  OUT FW_UPDATE_CHECK *Out,
  IN UINT32            Status,
  IN CONST CHAR8      *Format,
  ...
)
{
  va_list Args;

  Out->Status = Status;
  va_start (Args, Format);
  VSPrint (Out->Why, sizeof (Out->Why), Format, Args);
  va_end (Args);
}

VOID
FwUpdateCheck (
  IN CONST FW_UPDATE_INPUT *In,
  OUT FW_UPDATE_CHECK      *Out
)
{
  UINT8            Want[SHA256_SIZE];
  UINT8            Got[SHA256_SIZE];
  ABL_IMAGE_RESULT Result;
  UINT32           Bad = 0;
  CONST CHAR8     *Differs;

  ZeroMem (Out, sizeof (*Out));
  if (!In->Sha256.Len) {
    Refuse (Out, FW_UPDATE_ERROR_AUTH, "no %s.sha256", In->Name);
    return;
  }
  if (!FwUpdateSha256Line (In->Sha256, In->Name, Want)) {
    Refuse (
      Out, FW_UPDATE_ERROR_AUTH, "no SHA-256 for it in %s.sha256", In->Name
    );
    return;
  }
  Sha256 (In->Staged.Ptr, In->Staged.Len, Got);
  if (CompareMem (Want, Got, SHA256_SIZE) != 0) {
    Refuse (
      Out, FW_UPDATE_ERROR_AUTH, "SHA-256 doesn't match %s.sha256", In->Name
    );
    return;
  }
  if (In->Staged.Len > In->PartitionSize) {
    Refuse (
      Out, FW_UPDATE_ERROR_RESOURCES, "%llu bytes, abl is only %llu",
      (UINT64)In->Staged.Len, In->PartitionSize
    );
    return;
  }
  Result = AblImageRead (In->Staged.Ptr, In->Staged.Len, TRUE, &Out->New, &Bad);
  if (Result == ABL_IMAGE_BAD_DIGEST) {
    Refuse (
      Out, FW_UPDATE_ERROR_FORMAT, "%s (program header %u)",
      AblImageResultName (Result), Bad
    );
    return;
  }
  if (Result != ABL_IMAGE_OK) {
    Refuse (Out, FW_UPDATE_ERROR_FORMAT, "%s", AblImageResultName (Result));
    return;
  }
  if (!Out->New.Chipset[0]) {
    Refuse (Out, FW_UPDATE_ERROR_FORMAT, "no image tag, not an OpenABL image");
    return;
  }
  Out->Version = UefiVersionPair (Out->New.Version);
  if (!Out->Version) {
    Refuse (
      Out, FW_UPDATE_ERROR_FORMAT, "version \"%s\" isn't MAJOR.MINOR",
      Out->New.Version
    );
    return;
  }
  Result =
    AblImageRead (In->Installed.Ptr, In->Installed.Len, FALSE, &Out->Old, NULL);
  if (Result != ABL_IMAGE_OK || !Out->Old.Chipset[0]) {
    Refuse (
      Out, FW_UPDATE_ERROR_UNSUCCESSFUL, "the installed ABL: %s",
      Result != ABL_IMAGE_OK ? AblImageResultName (Result) : "no image tag"
    );
    return;
  }
  if (!TextEqNoCase (TextOf (Out->New.Chipset), Out->Old.Chipset)) {
    Refuse (
      Out, FW_UPDATE_ERROR_FORMAT, "built for %s, not %s", Out->New.Chipset,
      Out->Old.Chipset
    );
    return;
  }
  Differs = AblImageMismatch (&Out->Old, &Out->New);
  if (Differs) {
    Refuse (
      Out, FW_UPDATE_ERROR_FORMAT,
      "%s differs from the installed ABL (MBN v%u vs v%u)", Differs,
      Out->New.MbnVersion, Out->Old.MbnVersion
    );
    return;
  }
  if (In->Installed.Len >= In->Staged.Len &&
      CompareMem (In->Installed.Ptr, In->Staged.Ptr, In->Staged.Len) == 0) {
    Out->AlreadyInstalled = TRUE;
    SPrint (Out->Why, sizeof (Out->Why), "%s is installed", Out->New.Version);
    return;
  }
  if (Out->Version <= UefiVersionPair (Out->Old.Version) && !In->Force) {
    Refuse (
      Out, FW_UPDATE_ERROR_VERSION, "%s isn't newer than the installed %s",
      Out->New.Version, Out->Old.Version
    );
    return;
  }
  SPrint (
    Out->Why, sizeof (Out->Why), "%s %s over %s, MBN v%u%s", Out->New.Chipset,
    Out->New.Version, Out->Old.Version, Out->New.MbnVersion,
    In->Force ? ", forced" : ""
  );
}
