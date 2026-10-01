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
inline constexpr uintptr_t kRvaPostPhysicsTask = 0xDBCA20;
inline constexpr uintptr_t kRvaPhysicsStep = 0xDBC500;
inline constexpr uintptr_t kRvaPreTick = 0x749A30;
inline constexpr uintptr_t kRvaEndStep = 0x7511E0;
inline constexpr uintptr_t kRvaIntegratorM2ReturnSite = 0x7395FA;
inline constexpr uintptr_t kRvaIntegratorReturnSite = 0x73E314;
inline constexpr uintptr_t kRvaCommitLogAuxA = 0x73A070;
inline constexpr uintptr_t kRvaCommitLogAuxB = 0x73B620;

inline constexpr uintptr_t kVaIntegrator = kImageBase + kRvaIntegrator;       // 0x140746150
inline constexpr uintptr_t kVaTickStart = kImageBase + kRvaTickStart;         // 0x14074b8f0
inline constexpr uintptr_t kVaCommit = kImageBase + kRvaCommit;               // 0x14074d190
inline constexpr uintptr_t kVaPerTickCaller = kImageBase + kRvaPerTickCaller; // 0x1407511e0
inline constexpr uintptr_t kVaSetPose = kImageBase + kRvaSetPose;             // 0x140746770
inline constexpr uintptr_t kVaPostPhysicsTask =
    kImageBase + kRvaPostPhysicsTask; // 0x140dbca20
inline constexpr uintptr_t kVaPhysicsStep = kImageBase + kRvaPhysicsStep;   // 0x140dbc500
inline constexpr uintptr_t kVaPreTick = kImageBase + kRvaPreTick;             // 0x140749a30
inline constexpr uintptr_t kVaEndStep = kImageBase + kRvaEndStep;             // 0x1407511e0
inline constexpr uintptr_t kVaIntegratorM2ReturnSite =
    kImageBase + kRvaIntegratorM2ReturnSite; // 0x1407395fa
inline constexpr uintptr_t kVaIntegratorReturnSite =
    kImageBase + kRvaIntegratorReturnSite; // 0x14073e314
inline constexpr uintptr_t kVaCommitLogAuxA =
    kImageBase + kRvaCommitLogAuxA; // 0x14073a070
inline constexpr uintptr_t kVaCommitLogAuxB =
    kImageBase + kRvaCommitLogAuxB; // 0x14073b620

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

// Spec correction 4 — colunas CSV extra
inline constexpr uint32_t kContainerExtraVec4Offset = 0xC930;
inline constexpr uint32_t kRigExtraVec4Offset = 0x290;

} // namespace dr2hook::physics_harness
