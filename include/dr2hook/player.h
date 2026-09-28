#pragma once

#include "dr2hook/memory.h"
#include <cstdint>

namespace dr2hook {

struct Vector3 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
};

struct Vector4 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float w = 1.f;
};

struct Matrix3x3 {
  float m[3][3] = {};
};

struct WheelState {
  float suspensionCompression = 0.35f;
  float angularVelocity = 0.f;
  bool inContact = true;
};

struct CarState {
  Vector3 position;
  Vector3 linearVelocity;
  Vector3 angularVelocity;
  Matrix3x3 rotationMatrix;
  WheelState wheels[4];
  Vector4 quaternion{0.f, 0.f, 0.f, 1.f};
};

enum class RestoreMode {
  Normal = 0,       // Teleporta parado: velocidades linear e angular zeradas
  WithMomentum = 1  // Restaura momentum integral: velocidade linear e angular preservadas
};

inline constexpr float SUSPENSION_STATIC_SAG_RATIO = 0.35f;

class Player {
public:
  static void Configure(MemoryScanner *scanner, uintptr_t vehicleAddress);
  static bool CaptureState(CarState &outState);
  static bool ApplyState(const CarState &state,
                         RestoreMode mode = RestoreMode::Normal);
  static bool ResolveVehicleAddress(uintptr_t gameBase);
  static uintptr_t GetVehicleAddress();

private:
  static MemoryScanner *s_scanner;
  static uintptr_t s_vehicleAddress;
};

} // namespace dr2hook

using dr2hook::CarState;
using dr2hook::Matrix3x3;
using dr2hook::Player;
using dr2hook::RestoreMode;
using dr2hook::SUSPENSION_STATIC_SAG_RATIO;
using dr2hook::Vector3;
using dr2hook::Vector4;
using dr2hook::WheelState;
