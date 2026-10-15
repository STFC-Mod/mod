#pragma once

#include "Hub.h"
#include "MonoSingleton.h"

#include <il2cpp/il2cpp.h>

struct CoordinateSearchContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Bookmarks", "CoordinateSearchContext">;

  void SetDefaultXCoordinate(Il2CppString* value)
  { Set<"DefaultXCoordinate">(value); }
  void SetDefaultYCoordinate(Il2CppString* value)
  { Set<"DefaultYCoordinate">(value); }
  void SetDefaultSystemInput(Il2CppString* value)
  { Set<"DefaultSystemInput">(value); }
};

struct BookmarksManager : MonoSingleton<BookmarksManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Bookmarks", "BookmarksManager">;

  void ViewBookmarks()
  { Call<"ViewBookmarks">(); }

  void ViewCoordinateSearch()
  {
    auto* context = il2cpp::New<CoordinateSearchContext>();
    // The binding already reported the missing class or constructor; plain bookmarks are the next best view.
    if (!context) {
      ViewBookmarks();
      return;
    }

    auto* zero = il2cpp_string_new("0");
    context->SetDefaultXCoordinate(zero);
    context->SetDefaultYCoordinate(zero);
    context->SetDefaultSystemInput(il2cpp_string_new(""));
    Hub::get_SectionManager()->TriggerSectionChange(SectionID::Bookmarks_Search_Coordinates, context, false, false,
                                                    true);
  }
};
