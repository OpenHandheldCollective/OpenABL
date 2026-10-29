/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - picks the current language and fills in templates.

  The strings themselves are in Ui/Lang/, one file per language.
  Scripts/mkfont.py reads the string literals there for the characters the
  CJK face needs, so every string the menu draws belongs in those files.
*/

#include "Ui/Lang.h"
#include "Base/Text.h"
#include "Core/Settings.h"

_Static_assert (
  LANG_COUNT == SETTINGS_LANGUAGES,
  "languages"
);
_Static_assert (
  sizeof (LANG_STRINGS) % sizeof (CONST CHAR8 *) == 0,
  "LANG_STRINGS holds only string pointers"
);

#define LANG_TABLE_ENTRY(Id, Symbol) [Id] = &gLang##Symbol,

STATIC CONST LANG_STRINGS *CONST mLangs[LANG_COUNT] = {
  LANG_LIST (LANG_TABLE_ENTRY)};

// The current language, with English where it has no string.
STATIC LANG_STRINGS mCurrent;

CONST LANG_STRINGS *gStrings = &gLangEn;

VOID
LangSet (IN UINT8 Lang)
{
  CONST LANG_STRINGS *Picked = mLangs[Lang < LANG_COUNT ? Lang : LANG_EN];
  CONST CHAR8 *CONST *From   = (CONST CHAR8 *CONST *)Picked;
  CONST CHAR8 *CONST *En     = (CONST CHAR8 *CONST *)&gLangEn;
  CONST CHAR8       **To     = (CONST CHAR8 **)&mCurrent;
  UINTN               Count  = sizeof (LANG_STRINGS) / sizeof (CONST CHAR8 *);

  for (UINTN Index = 0; Index < Count; Index++) {
    To[Index] = From[Index] ? From[Index] : En[Index];
  }
  gStrings = &mCurrent;
}

CONST CHAR8 *
LangName (IN UINT8 Lang)
{
  return mLangs[Lang < LANG_COUNT ? Lang : LANG_EN]->Name;
}

UINT8
LangFromCode (IN CONST CHAR8 *Code)
{
  for (UINT8 Lang = 0; Lang < LANG_COUNT; Lang++) {
    if (mLangs[Lang]->Code && AsciiStrEq (mLangs[Lang]->Code, Code)) {
      return Lang;
    }
  }
  return LANG_COUNT;
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
