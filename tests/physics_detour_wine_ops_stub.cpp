#include "dr2hook/physics_harness_detour_abi.h"

// Satisfy detour_entry symbols from physics_harness_detour_x64.S in the wine test link.
extern "C" {
dr2hook::physics_harness_abi::DetourOps g_ops_tick_start = {};
dr2hook::physics_harness_abi::DetourOps g_ops_integrator = {};
dr2hook::physics_harness_abi::DetourOps g_ops_commit = {};
dr2hook::physics_harness_abi::DetourOps g_ops_post_physics_task = {};
dr2hook::physics_harness_abi::DetourOps g_ops_physics_step = {};
dr2hook::physics_harness_abi::DetourOps g_ops_pretick = {};
dr2hook::physics_harness_abi::DetourOps g_ops_end_step = {};
dr2hook::physics_harness_abi::DetourOps g_ops_commit_aux_a = {};
dr2hook::physics_harness_abi::DetourOps g_ops_commit_aux_b = {};
}
