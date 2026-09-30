#pragma once

#include <cstddef>
#include <cstdint>

// BUG 1 — primeiros bytes do dirtrally2.exe em disco (hex verificado independentemente).
namespace dr2hook::physics_harness_prologues {

inline constexpr size_t kPrologueLength = 12;

// end_step: bytes 6–11 iniciam mov rax,[rip+disp32]; verificação usa 12 B; MinHook deve roubar ≥13 B.
inline constexpr size_t kEndStepPrologueLength = 12;
inline constexpr size_t kEndStepMinimumStolenBytes = 13;

// tick_start @ RVA 0x74B8F0 — hex: 488bc4488958184889702055
inline constexpr uint8_t kTickStart[kPrologueLength] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x70, 0x20, 0x55,
};

// integrator @ RVA 0x746150 — hex: 488bc44889581055488da838
inline constexpr uint8_t kIntegrator[kPrologueLength] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x55, 0x48, 0x8D, 0xA8, 0x38,
};

// commit @ RVA 0x74D190 — hex: 488bc4488958184889702055
inline constexpr uint8_t kCommit[kPrologueLength] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x70, 0x20, 0x55,
};

// frame_loop @ RVA 0xDBCA20 — hex: 488bc4574881ecb000000033
inline constexpr uint8_t kFrameLoop[kPrologueLength] = {
    0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x33,
};

// physics_step @ RVA 0xDBC500 — hex: 488bc44889501041554883ec
inline constexpr uint8_t kPhysicsStep[kPrologueLength] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x41, 0x55, 0x48, 0x83, 0xEC,
};

// pretick @ RVA 0x749A30 — hex: 488bc4488958104889781855
inline constexpr uint8_t kPreTick[kPrologueLength] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55,
};

// end_step @ RVA 0x7511E0 — hex: 40534883ec50488b05b3b2e6 (+ 4.º byte disp no exe ao instalar)
inline constexpr uint8_t kEndStep[kEndStepPrologueLength] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x05, 0xB3, 0xB2, 0xE6,
};

// commit_log aux @ RVA 0x73A070 — hex: 4c8bdc55535741554157498d (mov r11,rsp)
inline constexpr uint8_t kCommitAuxA[kPrologueLength] = {
    0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x41, 0x55, 0x41, 0x57, 0x49, 0x8D,
};

// commit_log aux @ RVA 0x73B620 — hex: 488bc45657415641574881ec
inline constexpr uint8_t kCommitAuxB[kPrologueLength] = {
    0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC,
};

} // namespace dr2hook::physics_harness_prologues
