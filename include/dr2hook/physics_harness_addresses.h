#pragma once

#include <cstdint>

// Endereços VA/RVA do dirtrally2.exe (spec parte 2, image base 0x140000000).
namespace dr2hook::physics_harness {

inline constexpr uintptr_t kImageBase = 0x140000000ULL;

inline constexpr uintptr_t kRvaIntegrator = 0x746150;
inline constexpr uintptr_t kRvaTickStart = 0x74B8F0;
inline constexpr uintptr_t kRvaCommit = 0x74D190;
inline constexpr uintptr_t kRvaPerTickCaller = 0x7511E0;
inline constexpr uintptr_t kRvaSetPose = 0x746770;
inline constexpr uintptr_t kRvaFrameLoop = 0xDBCA20;

inline constexpr uintptr_t kVaIntegrator = kImageBase + kRvaIntegrator;       // 0x140746150
inline constexpr uintptr_t kVaTickStart = kImageBase + kRvaTickStart;         // 0x14074b8f0
inline constexpr uintptr_t kVaCommit = kImageBase + kRvaCommit;               // 0x14074d190
inline constexpr uintptr_t kVaPerTickCaller = kImageBase + kRvaPerTickCaller; // 0x1407511e0
inline constexpr uintptr_t kVaSetPose = kImageBase + kRvaSetPose;             // 0x140746770
inline constexpr uintptr_t kVaFrameLoop = kImageBase + kRvaFrameLoop;         // 0x140dbca20

// Spec parte 5 — API nativa experimental (DynamicsCarImpl* em RCX)
inline constexpr uintptr_t kRvaSetTransform = 0x74AD80;
inline constexpr uintptr_t kRvaSetLinVel = 0x74A910;
inline constexpr uintptr_t kRvaSetAngVel = 0x74A890;

inline constexpr uintptr_t kVaSetTransform = kImageBase + kRvaSetTransform; // 0x14074ad80
inline constexpr uintptr_t kVaSetLinVel = kImageBase + kRvaSetLinVel;         // 0x14074a910
inline constexpr uintptr_t kVaSetAngVel = kImageBase + kRvaSetAngVel;         // 0x14074a890

// Spec parte 3 — ponteiro global do carro ativo (.data)
inline constexpr uintptr_t kRvaActiveCarPointer = 0x1681CE8;
inline constexpr uintptr_t kVaActiveCarPointer = kImageBase + kRvaActiveCarPointer;

// Validação do rig (DynamicsCarImpl)
inline constexpr uint32_t kRigSelfPointerOffset = 0x12C0;
inline constexpr uint32_t kRigTypeTagOffset = 0x12D0;
inline constexpr uint32_t kRigExpectedTypeTag = 4;

} // namespace dr2hook::physics_harness
