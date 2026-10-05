#pragma once

namespace dr2hook {

// Aumenta a alocação do objeto VEHICLE_SYSTEM (0x16c0 -> 0x1900 bytes) para o
// core poder pôr arrays por carro de 24 posições no fim dele (ghost_lab.cpp,
// PatchVehicleSystemSlots). O objeto nasce no construtor da aplicação, antes
// do core carregar: instalar no DLL_PROCESS_ATTACH. Não loga.
bool InstallVehicleSystemSizePatch();
// Depois do Logger::Init: relata o resultado.
void LogVehicleSystemSizePatch();

} // namespace dr2hook
