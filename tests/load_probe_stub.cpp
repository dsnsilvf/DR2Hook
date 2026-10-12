#include "dr2hook/load_probe.h"

// O race_events.cpp marca a linha do tempo da sonda; no test_auto_stage a sonda não existe (puxaria os pacotes de
// pista e os hooks do core inteiro).
namespace dr2hook {
void LoadProbeMark(const char *) {}
} // namespace dr2hook
