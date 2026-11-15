#pragma once

#include <il2cpp/il2cpp_helper.h>

struct UserProfile {
public:
  __declspec(property(get = __get_LocaId)) long LocaId;
  __declspec(property(get = __get_Name)) Il2CppString* Name;
  __declspec(property(get = __get_UserId)) Il2CppString* UserId;
  __declspec(property(get = __get_Level)) int32_t Level;

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "UserProfile");
    return class_helper;
  }

public:
  long __get_LocaId()
  {
    static auto field = get_class_helper().GetField("_locaId").offset();
    return *(long*)((char*)this + field);
  }

  Il2CppString* __get_Name()
  {
    static auto field = get_class_helper().GetField("name_").offset();
    return *(Il2CppString**)((char*)this + field);
  }

  Il2CppString* __get_UserId()
  {
    static auto prop = get_class_helper().GetProperty("UserId");
    return prop.GetRaw<Il2CppString>(this);
  }

  int32_t __get_Level()
  {
    static auto prop = get_class_helper().GetProperty("Level");
    return *prop.Get<int32_t>(this);
  }
};
