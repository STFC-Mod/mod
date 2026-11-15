#pragma once

#include <il2cpp/il2cpp_helper.h>

#include "Vector2.h"

#pragma pack(push, 1)
struct vec3 {
  float x, y, z;
};
#pragma pack(pop)

struct NavigationPan {
public:
  // An inline value has no Il2CppObject header. Vector2's reflected accessors
  // expect a boxed value and would read/write the adjacent pixel drag delta.
  __declspec(property(get = __get__lastDelta)) vec2* _lastDelta;
  __declspec(property(get = __get__panningStarted)) bool _panningStarted;
  __declspec(property(get = __get__deltaScaler)) float _deltaScaler;
  __declspec(property(get = __get__keyboardDeltaScaler)) float _keyboardDeltaScaler;
  __declspec(property(get = __get__trackingPOI, put = __set__trackingPOI)) uintptr_t* _trackingPOI;

  __declspec(property(get = __get__farMagRadiusRatioSystemNormal,
                      put = __set__farMagRadiusRatioSystemNormal)) float _farMagRadiusRatioSystemNormal;
  __declspec(property(get = __get__farMagRadiusRatioSystemExtended,
                      put = __set__farMagRadiusRatioSystemExtended)) float _farMagRadiusRatioSystemExtended;
  __declspec(property(get = __get__nearMagRadiusRatioSystem)) float _nearMagRadiusRatioSystem;

  void MoveCamera(vec2 delta, bool isMomentum = false)
  {
    static auto MoveCamera = get_class_helper().GetMethod<void(NavigationPan*, vec2, bool)>("MoveCamera");
    MoveCamera(this, delta, isMomentum);
  }

  static bool BlockPan()
  {
    return get_class_helper().GetStaticField("BlockPan").Get<bool>();
  }

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "NavigationPan");
    return class_helper;
  }

public:
  uintptr_t* __get__trackingPOI()
  {
    static auto field = get_class_helper().GetField("_trackingPOI");
    return *(uintptr_t**)((ptrdiff_t)this + field.offset());
  }

  void __set__trackingPOI(uintptr_t* value)
  {
    static auto field = get_class_helper().GetField("_trackingPOI");
    *(uintptr_t**)((ptrdiff_t)this + field.offset()) = value;
  }

  vec2* __get__lastDelta()
  {
    static auto field = get_class_helper().GetField("_lastDelta");
    return (vec2*)((ptrdiff_t)this + field.offset());
  }

  bool __get__panningStarted()
  {
    static auto field = get_class_helper().GetField("_panningStarted");
    return *(bool*)((ptrdiff_t)this + field.offset());
  }

  float __get__keyboardDeltaScaler()
  {
    static auto field = get_class_helper().GetField("_keyboardDeltaScaler");
    return *(float*)((ptrdiff_t)this + field.offset());
  }

  float __get__deltaScaler()
  {
    static auto field = get_class_helper().GetField("_deltaScaler");
    return *(float*)((ptrdiff_t)this + field.offset());
  }

  float __get__farMagRadiusRatioSystemNormal()
  {
    static auto field = get_class_helper().GetField("_farMagRadiusRatioSystemNormal").offset();
    return *(float*)((ptrdiff_t)this + field);
  }

  void __set__farMagRadiusRatioSystemNormal(float v)
  {
    static auto field                  = get_class_helper().GetField("_farMagRadiusRatioSystemNormal").offset();
    *(float*)((ptrdiff_t)this + field) = v;
  }

  float __get__farMagRadiusRatioSystemExtended()
  {
    static auto field = get_class_helper().GetField("_farMagRadiusRatioSystemExtended").offset();
    return *(float*)((ptrdiff_t)this + field);
  }

  void __set__farMagRadiusRatioSystemExtended(float v)
  {
    static auto field                  = get_class_helper().GetField("_farMagRadiusRatioSystemExtended").offset();
    *(float*)((ptrdiff_t)this + field) = v;
  }

  float __get__nearMagRadiusRatioSystem()
  {
    static auto field = get_class_helper().GetField("_nearMagRadiusRatioSystemNormal");
    return *(float*)((ptrdiff_t)this + field.offset());
  }

  // void __set__lastDelta(int depth) {
  //     static auto field = get_class_helper().GetField("_depth");
  //     *(int*)((ptrdiff_t)this + field.offset()) = depth;
  // }
};
