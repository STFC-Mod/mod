#pragma once

#include "Camera.h"
#include "NavigationPan.h"
#include "Vector2.h"
#include "ZoomLevels.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class NodeDepth : int32_t {
  Galaxy       = 1,
  SolarSystem  = 2,
  PlanetSystem = 4,
  Starbase     = 8,
};

struct NavigationZoom : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationZoom">;

  void SetViewParameters(float radius, NodeDepth depth)
  { Call<"SetViewParameters">(radius, depth); }
  void ZoomCameraAtWorldPoint()
  { Call<"ZoomCameraAtWorldPoint">(); }

  float Distance()
  { return Get<"Distance", float>(); }
  void SetDistance(float value)
  { Set<"Distance">(value); }
  float NormalizedZoom()
  { return Get<"NormalizedZoom", float>(); }

  NodeDepth depth()
  { return Field<"_depth", NodeDepth>(); }
  void SetDepth(NodeDepth value)
  { SetField<"_depth">(value); }
  ZoomLevels zoomLevel()
  { return Field<"_zoomLevel", ZoomLevels>(); }
  Camera* sceneCamera()
  { return Field<"_sceneCamera", Camera*>(); }

  float storedZoomDistanceSystem()
  { return Field<"_storedZoomDistanceSystem", float>(); }
  void SetStoredZoomDistanceSystem(float value)
  { SetField<"_storedZoomDistanceSystem">(value); }
  float systemDefaultZoomRatio()
  { return Field<"_systemDefaultZoomRatio", float>(); }
  void SetSystemDefaultZoomRatio(float value)
  { SetField<"_systemDefaultZoomRatio">(value); }
  float actualDistance()
  { return Field<"_actualDistance", float>(); }
  void SetActualDistance(float value)
  { SetField<"_actualDistance">(value); }
  float zoomtotal()
  { return Field<"_zoomtotal", float>(); }
  void SetZoomtotal(float value)
  { SetField<"_zoomtotal">(value); }
  float minimum()
  { return Field<"_minimum", float>(); }
  void SetMinimum(float value)
  { SetField<"_minimum">(value); }
  float maximum()
  { return Field<"_maximum", float>(); }
  void SetMaximum(float value)
  { SetField<"_maximum">(value); }
  float zoomDelta()
  { return Field<"_zoomDelta", float>(); }
  void SetZoomDelta(float value)
  { SetField<"_zoomDelta">(value); }
  float viewRadius()
  { return Field<"_viewRadius", float>(); }
  void SetViewRadius(float value)
  { SetField<"_viewRadius">(value); }
  float lastZoomDelta()
  { return Field<"_lastZoomDelta", float>(); }
  void SetLastZoomDelta(float value)
  { SetField<"_lastZoomDelta">(value); }
  vec2 zoomLocation()
  { return Field<"_zoomLocation", vec2>(); }
  void SetZoomLocation(vec2 value)
  { SetField<"_zoomLocation">(value); }
  vec3 worldPoint()
  { return Field<"_worldPoint", vec3>(); }
  void SetWorldPoint(vec3 value)
  { SetField<"_worldPoint">(value); }
  float farRatioSystemNormal()
  { return Field<"_farRatioSystemNormal", float>(); }
  void SetFarRatioSystemNormal(float value)
  { SetField<"_farRatioSystemNormal">(value); }
  float farRatioSystemExtended()
  { return Field<"_farRatioSystemExtended", float>(); }
  void SetFarRatioSystemExtended(float value)
  { SetField<"_farRatioSystemExtended">(value); }
};
