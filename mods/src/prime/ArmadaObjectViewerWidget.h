#pragma once

#include "FleetPlayerData.h"
#include "GenericButtonContext.h"
#include "ObjectViewerBaseWidget.h"

struct ArmadaObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ArmadaObjectViewerWidget">;

  void SetCourseToArmada()
  { Call<"SetCourseToArmada">(); }
  void ValidateThenJoinArmada()
  { Call<"ValidateThenJoinArmada">(); }
  void JoinArmada()
  { Call<"JoinArmada">(); }
  bool IsAllowedToJoinArmada(FleetPlayerData* fleet)
  { return Call<"IsAllowedToJoinArmada", bool>(fleet); }

  GenericButtonContext* joinContext()
  { return Field<"_joinContext", GenericButtonContext*>(); }
};
