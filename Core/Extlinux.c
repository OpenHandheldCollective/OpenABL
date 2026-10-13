/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Extlinux - parser for extlinux.conf, the syslinux-style menu U-Boot reads.
*/

#include "Core/Extlinux.h"

#include "Base/Memory.h"
#include "Base/Print.h"

CONST CHAR8 *CONST gExtlinuxPaths[2] = {"\\extlinux\\extlinux.conf",
                                        "\\boot\\extlinux\\extlinux.conf"};

typedef struct _READER {
  CHAR8 *(*Read) (
    IN OABL_TEXT Path,
    OUT UINTN   *Len,
    IN VOID     *Context
  );
  VOID *Context;
} READER;

// Initrd lists are separated by commas or spaces.
STATIC BOOLEAN
IsInitrdSep (IN CHAR8 Char)
{
  return Char == ',' || Char == ' ';
}

// Replaces *Field with a copy of String, or NULL.
STATIC BOOLEAN
SetOpt (
  IN OUT CHAR8  **Field,
  IN CONST CHAR8 *String OPTIONAL
)
{
  CHAR8 *Copy = NULL;

  if (String && !(Copy = AsciiStrDup (String))) {
    return FALSE;
  }
  FreePool (*Field);
  *Field = Copy;
  return TRUE;
}

// Replaces *Field with the first word of Value, or NULL.
STATIC BOOLEAN
First (
  IN OUT CHAR8 **Field,
  IN OABL_TEXT   Value
)
{
  OABL_TEXT Word;
  CHAR8    *Copy = NULL;

  if (BootEntryFirstWord (Value, &Word) && !(Copy = TextDup (Word))) {
    return FALSE;
  }
  FreePool (*Field);
  *Field = Copy;
  return TRUE;
}

STATIC BOOLEAN
Words (
  IN OUT BOOT_ENTRY_STRINGS *List,
  IN OABL_TEXT               Value,
  IN BOOLEAN (*IsSeparator) (IN CHAR8 Char)
)
{
  BOOT_ENTRY_STRINGS Split = {0};

  if (!BootEntryPushWords (&Split, Value, IsSeparator)) {
    BootEntryStringsFree (&Split);
    return FALSE;
  }
  BootEntryStringsFree (List);
  *List = Split;
  return TRUE;
}

STATIC BOOLEAN
ParseInto (
  IN OUT EXTLINUX_CONFIG *Config,
  IN OABL_TEXT            Text,
  IN CONST READER        *Reader,
  IN UINT32               Depth
);

// Parses the file named by the first word of Arg, one level deeper.
STATIC BOOLEAN
ParseInclude (
  IN OUT EXTLINUX_CONFIG *Config,
  IN OABL_TEXT            Arg,
  IN CONST READER        *Reader,
  IN UINT32               Depth
)
{
  OABL_TEXT Path;
  UINTN     Len = 0;
  CHAR8    *Included;
  BOOLEAN   Ok;

  if (!BootEntryFirstWord (Arg, &Path)) {
    Path = TextOf ("");
  }
  if (!Reader->Read ||
      !(Included = Reader->Read (Path, &Len, Reader->Context))) {
    return TRUE;
  }
  Ok = ParseInto (Config, (OABL_TEXT){Included, Len}, Reader, Depth + 1);
  FreePool (Included);
  return Ok;
}

STATIC BOOLEAN
ParseInto (
  IN OUT EXTLINUX_CONFIG *Config,
  IN OABL_TEXT            Text,
  IN CONST READER        *Reader,
  IN UINT32               Depth
)
{
  OABL_TEXT Line;
  OABL_TEXT Keyword;
  OABL_TEXT Rest;
  OABL_TEXT SubKeyword;
  OABL_TEXT Arg;

  while (TextNextLine (&Text, &Line)) {
    EXTLINUX_LABEL *Label = Config->Labels.Count
                              ? &Config->Labels.Items[Config->Labels.Count - 1]
                              : NULL;
    BOOLEAN         Ok    = TRUE;

    Line = TextTrim (Line);
    if (!Line.Len || Line.Ptr[0] == '#') {
      continue;
    }
    BootEntryKeyValue (Line, &Keyword, &Rest);
    if (TextEqNoCase (Keyword, "label")) {
      EXTLINUX_LABEL NewLabel = {.Name = TextDup (Rest)};
      Ok = NewLabel.Name && VecPush (&Config->Labels, NewLabel);
      if (!Ok) {
        FreePool (NewLabel.Name);
      }
    } else if (TextEqNoCase (Keyword, "menu")) {
      BootEntryKeyValue (Rest, &SubKeyword, &Arg);
      if (TextEqNoCase (SubKeyword, "title")) {
        Ok = BootEntrySetText (&Config->MenuTitle, Arg);
      } else if (TextEqNoCase (SubKeyword, "label")) {
        Ok = !Label || BootEntrySetText (&Label->MenuLabel, Arg);
      } else if (TextEqNoCase (SubKeyword, "default")) {
        Ok = SetOpt (&Config->MenuDefault, Label ? Label->Name : NULL);
      } else if (TextEqNoCase (SubKeyword, "include") && Depth < 4) {
        Ok = ParseInclude (Config, Arg, Reader, Depth);
      }
    } else if (TextEqNoCase (Keyword, "include") && Depth < 4) {
      Ok = ParseInclude (Config, Rest, Reader, Depth);
    } else if (TextEqNoCase (Keyword, "default")) {
      Ok = First (&Config->DefaultLabel, Rest);
    } else if (TextEqNoCase (Keyword, "timeout")) {
      UINT64 Timeout = 0;
      Config->HasTimeout =
        TextParseU64 (Rest, 10, &Timeout) && Timeout <= MAX_UINT32;
      Config->TimeoutDs = Config->HasTimeout ? (UINT32)Timeout : 0;
    } else if (!Label) {
      continue;
    } else if (TextEqNoCase (Keyword, "kernel") ||
               TextEqNoCase (Keyword, "linux")) {
      Ok = First (&Label->Kernel, Rest);
    } else if (TextEqNoCase (Keyword, "initrd")) {
      Ok = Words (&Label->Initrds, Rest, IsInitrdSep);
    } else if (TextEqNoCase (Keyword, "append")) {
      Ok = BootEntrySetText (&Label->Append, Rest);
    } else if (TextEqNoCase (Keyword, "fdt") ||
               TextEqNoCase (Keyword, "devicetree")) {
      Ok = First (&Label->Fdt, Rest);
    } else if (TextEqNoCase (Keyword, "fdtdir") ||
               TextEqNoCase (Keyword, "devicetreedir")) {
      Ok = First (&Label->FdtDir, Rest);
    } else if (TextEqNoCase (Keyword, "fdtoverlays")) {
      Ok = Words (&Label->Overlays, Rest, IsAsciiSpace);
    }
    if (!Ok) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
ExtlinuxParse (
  IN OABL_TEXT Text,
  IN CHAR8 *(*Include OPTIONAL) (
    IN OABL_TEXT Path,
    OUT UINTN   *Len,
    IN VOID     *Context
  ),
  IN VOID             *Context,
  OUT EXTLINUX_CONFIG *Out
)
{
  READER Reader = {Include, Context};

  *Out = (EXTLINUX_CONFIG){0};
  if (ParseInto (Out, Text, &Reader, 0)) {
    return TRUE;
  }
  ExtlinuxConfigFree (Out);
  return FALSE;
}

STATIC VOID
LabelFree (IN OUT EXTLINUX_LABEL *Label)
{
  FreePool (Label->Name);
  FreePool (Label->MenuLabel);
  FreePool (Label->Kernel);
  BootEntryStringsFree (&Label->Initrds);
  FreePool (Label->Append);
  FreePool (Label->Fdt);
  FreePool (Label->FdtDir);
  BootEntryStringsFree (&Label->Overlays);
}

VOID
ExtlinuxConfigFree (IN OUT EXTLINUX_CONFIG *Config)
{
  FreePool (Config->MenuTitle);
  FreePool (Config->DefaultLabel);
  FreePool (Config->MenuDefault);
  for (UINTN Index = 0; Index < Config->Labels.Count; Index++) {
    LabelFree (&Config->Labels.Items[Index]);
  }
  VecFree (&Config->Labels);
  *Config = (EXTLINUX_CONFIG){0};
}

// Finds the first bootable label named Name.
STATIC BOOLEAN
ByName (
  IN CONST EXTLINUX_CONFIG *Config,
  IN CONST CHAR8           *Name OPTIONAL,
  OUT UINTN                *Out
)
{
  if (!Name) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Config->Labels.Count; Index++) {
    if (AsciiStrEq (Config->Labels.Items[Index].Name, Name) &&
        Config->Labels.Items[Index].Kernel) {
      *Out = Index;
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
ExtlinuxConfigDefaultIndex (
  IN CONST EXTLINUX_CONFIG *Config,
  OUT UINTN                *Out
)
{
  if (ByName (Config, Config->DefaultLabel, Out) ||
      ByName (Config, Config->MenuDefault, Out)) {
    return TRUE;
  }
  for (UINTN Index = 0; Index < Config->Labels.Count; Index++) {
    if (Config->Labels.Items[Index].Kernel) {
      *Out = Index;
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
ExtlinuxConfigEntries (
  IN CONST EXTLINUX_CONFIG *Config,
  IN CONST CHAR8           *Conf,
  OUT BOOT_ENTRY_LIST      *Out
)
{
  UINTN   Default    = 0;
  BOOLEAN HasDefault = ExtlinuxConfigDefaultIndex (Config, &Default);

  *Out = (BOOT_ENTRY_LIST){0};
  for (UINTN Position = 0; Position < Config->Labels.Count; Position++) {
    // The default first, then the rest in file order.
    UINTN                 Index  = !HasDefault           ? Position
                                   : Position == 0       ? Default
                                   : Position <= Default ? Position - 1
                                                         : Position;
    CONST EXTLINUX_LABEL *Label  = &Config->Labels.Items[Index];
    BOOT_ENTRY            Entry  = {.Source      = BOOT_ENTRY_SOURCE_EXTLINUX,
                                    .Action.Kind = BOOT_ENTRY_ACTION_LINUX};
    BOOT_ENTRY_ACTION    *Action = &Entry.Action;
    BOOLEAN               Ok;

    if (!Label->Kernel) {
      continue;
    }
    Entry.Id = SPrintAlloc ("extlinux:%s", Label->Name);
    Entry.Title =
      AsciiStrDup (Label->MenuLabel ? Label->MenuLabel : Label->Name);
    Entry.File      = AsciiStrDup (Conf);
    Action->Kernel  = BootEntryUefiPath (TextOf (Label->Kernel));
    Action->Cmdline = AsciiStrDup (Label->Append ? Label->Append : "");
    Ok = Entry.Id && Entry.Title && Entry.File && Action->Kernel &&
         Action->Cmdline &&
         BootEntryUefiPaths (&Label->Initrds, &Action->Initrds) &&
         BootEntryOptUefiPath (&Action->Devicetree, Label->Fdt) &&
         BootEntryUefiPaths (&Label->Overlays, &Action->Overlays) &&
         BootEntryOptUefiPath (&Action->FdtDir, Label->FdtDir);
    if (!Ok || !VecPush (Out, Entry)) {
      BootEntryFree (&Entry);
      BootEntryListFree (Out);
      return FALSE;
    }
  }
  return TRUE;
}
