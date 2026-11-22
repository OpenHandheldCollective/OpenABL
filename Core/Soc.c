/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Soc - the Qualcomm SoCs OpenABL knows.
*/

#include "Core/Soc.h"

// Sorted by id. Linux names SoCs by their part number; the SoC string comes
// after the board's in a root compatible list, as in "ayntec,odin2mini",
// "qcom,qcs8550", "qcom,sm8550".
STATIC CONST SOC_INFO mSocs[] = {
  {321, "Snapdragon 845", {"qcom,sdm845"}},
  {356, "Snapdragon 865", {"qcom,sm8250"}},
  {444, "Snapdragon 662", {"qcom,sm6115"}},
  {467, "Dragonwing QCS6125", {"qcom,sm6115"}},
  {531, "Snapdragon 8+ Gen 1", {NULL}},
  {601, "Snapdragon G3x Gen 2", {"qcom,sm8550"}},
  {603, "Dragonwing QCS8550", {"qcom,sm8550"}},
  {634, "Snapdragon G2 Gen 1", {"qcom,sm6450"}},
  {639, "Snapdragon 8 Elite", {"qcom,sm8750"}},
  {654, "Snapdragon G1 Gen 2", {"qcom,sm4450"}},
  {682, "Snapdragon G3 Gen 3", {"qcom,sm8650"}},
  {700, "Snapdragon G2 Gen 2", {"qcom,sm7675"}},
  {706, "Dragonwing Q8750", {"qcom,sm8750"}},
  {732, "Dragonwing Q7790", {NULL}},
};

CONST SOC_INFO *
SocById (IN UINT32 Id)
{
  for (UINTN Index = 0; Index < sizeof (mSocs) / sizeof (*mSocs); Index++) {
    if (mSocs[Index].Id == Id) {
      return &mSocs[Index];
    }
  }
  return NULL;
}

BOOLEAN
SocHasCompatibles (IN CONST SOC_INFO *Soc)
{
  return Soc && Soc->Compatibles[0];
}

BOOLEAN
SocMatches (
  IN CONST SOC_INFO *Soc,
  IN OABL_BYTES      List
)
{
  for (UINTN At = 0; At < List.Len;) {
    UINTN End = At;

    while (End < List.Len && List.Ptr[End]) {
      End++;
    }
    if (End == List.Len) {
      return FALSE;
    }
    for (UINTN Index = 0;
         Index < SOC_COMPATIBLES_MAX && Soc->Compatibles[Index]; Index++) {
      if (TextEqStr (
            (OABL_TEXT){(CONST CHAR8 *)List.Ptr + At, End - At},
            Soc->Compatibles[Index]
          )) {
        return TRUE;
      }
    }
    At = End + 1;
  }
  return FALSE;
}
