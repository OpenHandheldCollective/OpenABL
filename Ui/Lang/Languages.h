/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Languages - every language the menu offers, in the order the Language
  screen lists them.

  Each entry is X (Id, Symbol): Id becomes a LANG_ID and Symbol names the
  LANG_STRINGS gLang<Symbol> that Ui/Lang/<Symbol>.c defines. The Language
  setting stores the Symbol as text ("ZhHans"), and the simulator's --lang
  takes it, so entries can be reordered freely. Never rename a Symbol once
  it has shipped: a menu that stored the old one would fall back to English.

  To add a language:

  1. Copy Ui/Lang/En.c to Ui/Lang/<Symbol>.c and rename its table to
     gLang<Symbol>.
  2. Set .Name to the language's name in itself (shown on the Language
     screen).
  3. Translate what you can. Strings left out show in English. Keep each
     string's {}, {0} and {1}, or its values go missing.
  4. Add its entry to LANG_LIST where it should appear on the Language
     screen.

  Nothing else changes: the build picks up the file, and the CJK face takes
  the characters it needs from every file in Ui/Lang/.

  Kept free of includes so Core/Settings.h can count the entries.
*/

#ifndef __UI_LANG_LANGUAGES_H__
#define __UI_LANG_LANGUAGES_H__

#define LANG_LIST(X)       \
  X (LANG_EN, En)          \
  X (LANG_FR, Fr)          \
  X (LANG_JA, Ja)          \
  X (LANG_RU, Ru)          \
  X (LANG_ZH_HANS, ZhHans) \
  X (LANG_KO, Ko)

#endif
