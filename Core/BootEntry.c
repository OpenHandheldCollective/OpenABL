/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootEntry - a boot entry the loader can start by itself.
*/

#include "Core/BootEntry.h"

#include "Base/Memory.h"
#include "Ui/Lang.h"

CONST CHAR8 *
BootEntrySourceName (IN BOOT_ENTRY_SOURCE Source)
{
  switch (Source) {
  case BOOT_ENTRY_SOURCE_TYPE1:
    return gStrings->EntryType1;
  case BOOT_ENTRY_SOURCE_UKI:
    return gStrings->EntryUki;
  case BOOT_ENTRY_SOURCE_EXTLINUX:
    break;
  }
  return "extlinux.conf";
}

CONST CHAR8 *
BootEntrySourceDebug (IN BOOT_ENTRY_SOURCE Source)
{
  switch (Source) {
  case BOOT_ENTRY_SOURCE_TYPE1:
    return "Type1";
  case BOOT_ENTRY_SOURCE_UKI:
    return "Uki";
  case BOOT_ENTRY_SOURCE_EXTLINUX:
    break;
  }
  return "Extlinux";
}

VOID
BootEntryStringsFree (IN OUT BOOT_ENTRY_STRINGS *Strings)
{
  for (UINTN Index = 0; Index < Strings->Count; Index++) {
    FreePool (Strings->Items[Index]);
  }
  VecFree (Strings);
}

VOID
BootEntryActionFree (IN OUT BOOT_ENTRY_ACTION *Action)
{
  FreePool (Action->Kernel);
  BootEntryStringsFree (&Action->Initrds);
  FreePool (Action->Cmdline);
  BootEntryStringsFree (&Action->Overlays);
  FreePool (Action->FdtDir);
  FreePool (Action->Path);
  FreePool (Action->Options);
  FreePool (Action->Devicetree);
  *Action = (BOOT_ENTRY_ACTION){0};
}

VOID
BootEntryFree (IN OUT BOOT_ENTRY *Entry)
{
  FreePool (Entry->Id);
  FreePool (Entry->Title);
  FreePool (Entry->Version);
  FreePool (Entry->SortKey);
  FreePool (Entry->MachineId);
  FreePool (Entry->File);
  BootEntryActionFree (&Entry->Action);
  *Entry = (BOOT_ENTRY){0};
}

VOID
BootEntryListFree (IN OUT BOOT_ENTRY_LIST *Entries)
{
  for (UINTN Index = 0; Index < Entries->Count; Index++) {
    BootEntryFree (&Entries->Items[Index]);
  }
  VecFree (Entries);
}

STATIC BOOLEAN
CloneStr (
  OUT CHAR8     **Out,
  IN CONST CHAR8 *String OPTIONAL
)
{
  *Out = String ? AsciiStrDup (String) : NULL;
  return !String || *Out;
}

STATIC BOOLEAN
CloneStrings (
  IN OUT BOOT_ENTRY_STRINGS   *Out,
  IN CONST BOOT_ENTRY_STRINGS *In
)
{
  for (UINTN Index = 0; Index < In->Count; Index++) {
    CHAR8 *Copy = AsciiStrDup (In->Items[Index]);
    if (!Copy || !VecPush (Out, Copy)) {
      FreePool (Copy);
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
BootEntryClone (
  IN CONST BOOT_ENTRY *Entry,
  OUT BOOT_ENTRY      *Out
)
{
  CONST BOOT_ENTRY_ACTION *From = &Entry->Action;
  BOOT_ENTRY_ACTION       *To   = &Out->Action;
  BOOLEAN                  Ok;

  // Owned fields start out NULL, so a failure part way frees only the
  // copies.
  *Out     = (BOOT_ENTRY){.Source     = Entry->Source,
                          .HasCounter = Entry->HasCounter,
                          .Counter    = Entry->Counter};
  To->Kind = From->Kind;
  Ok = CloneStr (&Out->Id, Entry->Id) && CloneStr (&Out->Title, Entry->Title) &&
       CloneStr (&Out->Version, Entry->Version) &&
       CloneStr (&Out->SortKey, Entry->SortKey) &&
       CloneStr (&Out->MachineId, Entry->MachineId) &&
       CloneStr (&Out->File, Entry->File) &&
       CloneStr (&To->Kernel, From->Kernel) &&
       CloneStrings (&To->Initrds, &From->Initrds) &&
       CloneStr (&To->Cmdline, From->Cmdline) &&
       CloneStrings (&To->Overlays, &From->Overlays) &&
       CloneStr (&To->FdtDir, From->FdtDir) &&
       CloneStr (&To->Path, From->Path) &&
       CloneStr (&To->Options, From->Options) &&
       CloneStr (&To->Devicetree, From->Devicetree);
  if (!Ok) {
    BootEntryFree (Out);
  }
  return Ok;
}

BOOLEAN
BootEntryIsBad (IN CONST BOOT_ENTRY *Entry)
{
  return Entry->HasCounter && Entry->Counter.Left == 0;
}

BOOLEAN
BootEntryIsBls (IN CONST BOOT_ENTRY *Entry)
{
  return Entry->Source == BOOT_ENTRY_SOURCE_TYPE1 ||
         Entry->Source == BOOT_ENTRY_SOURCE_UKI;
}

CHAR8 *
BootEntryUefiPath (IN OABL_TEXT Path)
{
  OABL_STRING Out   = {0};
  UINTN       Start = 0;
  BOOLEAN     First = TRUE;

  StringAppendChar (&Out, '\\');
  // Split at both / and \, dropping empty pieces and ".".
  for (UINTN Index = 0; Index <= Path.Len; Index++) {
    OABL_TEXT Part;

    if (Index < Path.Len && Path.Ptr[Index] != '/' && Path.Ptr[Index] != '\\') {
      continue;
    }
    Part  = TextSub (Path, Start, Index);
    Start = Index + 1;
    if (!Part.Len || TextEqStr (Part, ".")) {
      continue;
    }
    if (!First) {
      StringAppendChar (&Out, '\\');
    }
    First = FALSE;
    StringAppendText (&Out, Part);
  }
  if (Out.Failed) {
    StringFree (&Out);
    return NULL;
  }
  return StringTake (&Out);
}

BOOLEAN
BootEntryUefiPaths (
  IN CONST BOOT_ENTRY_STRINGS *In,
  OUT BOOT_ENTRY_STRINGS      *Out
)
{
  *Out = (BOOT_ENTRY_STRINGS){0};
  for (UINTN Index = 0; Index < In->Count; Index++) {
    CHAR8 *Path = BootEntryUefiPath (TextOf (In->Items[Index]));
    if (!Path || !VecPush (Out, Path)) {
      FreePool (Path);
      BootEntryStringsFree (Out);
      return FALSE;
    }
  }
  return TRUE;
}

OABL_TEXT
BootEntryParent (IN CONST CHAR8 *Path)
{
  OABL_TEXT Text = TextOf (Path);
  UINTN     Slash;

  if (!TextFindLastChar (Text, '\\', &Slash) || Slash == 0) {
    return TextOf ("\\");
  }
  return TextSub (Text, 0, Slash);
}

OABL_TEXT
BootEntryFileName (IN CONST CHAR8 *Path)
{
  OABL_TEXT Text = TextOf (Path);
  UINTN     Slash;

  if (!TextFindLastChar (Text, '\\', &Slash)) {
    return Text;
  }
  return TextSub (Text, Slash + 1, Text.Len);
}

// Entry file parsing helpers

VOID
BootEntryKeyValue (
  IN OABL_TEXT   Line,
  OUT OABL_TEXT *Key,
  OUT OABL_TEXT *Value
)
{
  for (UINTN Index = 0; Index < Line.Len; Index++) {
    if (IsAsciiSpace (Line.Ptr[Index])) {
      *Key   = TextSub (Line, 0, Index);
      *Value = TextTrim (TextSub (Line, Index + 1, Line.Len));
      return;
    }
  }
  *Key   = Line;
  *Value = TextSub (Line, Line.Len, Line.Len);
}

BOOLEAN
BootEntryFirstWord (
  IN OABL_TEXT   Text,
  OUT OABL_TEXT *Word
)
{
  UINTN End = 0;

  while (Text.Len && IsAsciiSpace (*Text.Ptr)) {
    Text.Ptr++;
    Text.Len--;
  }
  if (!Text.Len) {
    return FALSE;
  }
  while (End < Text.Len && !IsAsciiSpace (Text.Ptr[End])) {
    End++;
  }
  *Word = TextSub (Text, 0, End);
  return TRUE;
}

BOOLEAN
BootEntryPushWords (
  IN OUT BOOT_ENTRY_STRINGS *List,
  IN OABL_TEXT               Text,
  IN BOOLEAN (*IsSeparator) (IN CHAR8 C)
)
{
  UINTN Index = 0;

  while (Index < Text.Len) {
    UINTN  Start;
    CHAR8 *Word;

    while (Index < Text.Len && IsSeparator (Text.Ptr[Index])) {
      Index++;
    }
    Start = Index;
    while (Index < Text.Len && !IsSeparator (Text.Ptr[Index])) {
      Index++;
    }
    if (Index == Start) {
      continue;
    }
    Word = TextDup (TextSub (Text, Start, Index));
    if (!Word || !VecPush (List, Word)) {
      FreePool (Word);
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
BootEntrySetText (
  IN OUT CHAR8 **Field,
  IN OABL_TEXT   Value
)
{
  CHAR8 *Copy = TextDup (Value);

  if (!Copy) {
    return FALSE;
  }
  FreePool (*Field);
  *Field = Copy;
  return TRUE;
}

BOOLEAN
BootEntryOptUefiPath (
  OUT CHAR8     **Out,
  IN CONST CHAR8 *Path OPTIONAL
)
{
  *Out = Path ? BootEntryUefiPath (TextOf (Path)) : NULL;
  return !Path || *Out;
}
