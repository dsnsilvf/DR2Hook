#pragma once

#include "dr2hook/physics_harness_addresses.h"
#include <cstdint>

namespace dr2hook::physics_native {

// Convenções inferidas (dirtrally2.exe x64 MSVC): DynamicsCarImpl* em RCX,
// floats nos registros XMM1+ conforme ordem dos parâmetros __fastcall.
using FnCommit = void(__fastcall *)(void *rig);
using FnSetLinVel = void(__fastcall *)(void *rig, float x, float y, float z);
using FnSetAngVel = void(__fastcall *)(void *rig, float x, float y, float z);
using FnSetTransform = void(__fastcall *)(void *rig, float px, float py, float pz,
                                           float qx, float qy, float qz, float qw);

struct NativeEntrypoints {
  FnSetTransform setTransform = nullptr;
  FnSetLinVel setLinVel = nullptr;
  FnSetAngVel setAngVel = nullptr;
  FnCommit commit = nullptr;
};

inline NativeEntrypoints ResolveEntrypoints(uintptr_t gameModuleBase) {
  NativeEntrypoints api{};
  if (gameModuleBase == 0) {
    return api;
  }
  api.setTransform = reinterpret_cast<FnSetTransform>(
      gameModuleBase + physics_harness::kRvaSetTransform);
  api.setLinVel = reinterpret_cast<FnSetLinVel>(gameModuleBase +
                                                physics_harness::kRvaSetLinVel);
  api.setAngVel = reinterpret_cast<FnSetAngVel>(gameModuleBase +
                                                physics_harness::kRvaSetAngVel);
  api.commit =
      reinterpret_cast<FnCommit>(gameModuleBase + physics_harness::kRvaCommit);
  return api;
}

struct CallParams {
  float px = 0.f;
  float py = 0.f;
  float pz = 0.f;
  float qx = 0.f;
  float qy = 0.f;
  float qz = 0.f;
  float qw = 1.f;
  float vx = 0.f;
  float vy = 0.f;
  float vz = 0.f;
  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
};

enum class CallKind : uint8_t {
  SetTransform = 0,
  SetLinVel = 1,
  SetAngVel = 2,
  Commit = 3,
};

inline void Invoke(CallKind kind, void *rig, const CallParams &params,
                   const NativeEntrypoints &api) {
  if (rig == nullptr) {
    return;
  }
  switch (kind) {
  case CallKind::SetTransform:
    if (api.setTransform != nullptr) {
      api.setTransform(rig, params.px, params.py, params.pz, params.qx, params.qy,
                       params.qz, params.qw);
    }
    break;
  case CallKind::SetLinVel:
    if (api.setLinVel != nullptr) {
      api.setLinVel(rig, params.vx, params.vy, params.vz);
    }
    break;
  case CallKind::SetAngVel:
    if (api.setAngVel != nullptr) {
      api.setAngVel(rig, params.ax, params.ay, params.az);
    }
    break;
  case CallKind::Commit:
    if (api.commit != nullptr) {
      api.commit(rig);
    }
    break;
  }
}

} // namespace dr2hook::physics_native
