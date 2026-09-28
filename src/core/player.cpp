#include "dr2hook/player.h"
#include "dr2hook/logger.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace dr2hook {

MemoryScanner *Player::s_scanner = nullptr;
uintptr_t Player::s_vehicleAddress = 0;

void Player::Configure(MemoryScanner *scanner, uintptr_t vehicleAddress) {
  s_scanner = scanner;
  s_vehicleAddress = vehicleAddress;
}

uintptr_t Player::GetVehicleAddress() { return s_vehicleAddress; }

bool Player::ResolveVehicleAddress(uintptr_t gameBase) {
  if (gameBase == 0) {
    return false;
  }

  IMemoryAccessor *accessor =
      (s_scanner != nullptr) ? s_scanner->GetAccessor() : nullptr;
  DirectMemoryAccessor directFallback;
  if (accessor == nullptr) {
    accessor = &directFallback;
  }

  if (!accessor->IsValidAddress(gameBase)) {
    return false;
  }

  uintptr_t mgrPtrAddress = gameBase + 0x168c100;
  uintptr_t descAddress = gameBase + 0x168c0e0;

  bool descriptorValid = false;
  if (accessor->IsValidAddress(descAddress)) {
    char descBuf[32] = {};
    if (accessor->Read(descAddress, descBuf, sizeof(descBuf) - 1)) {
      descBuf[sizeof(descBuf) - 1] = '\0';
      if (std::strstr(descBuf, "vehicle_manager") != nullptr) {
        descriptorValid = true;
      }
    }
  }

  if (!descriptorValid && s_scanner != nullptr) {
    uintptr_t found = s_scanner->FindPattern(
        gameBase, 0x2000000, "76 65 68 69 63 6C 65 5F 6D 61 6E 61 67 65 72");
    if (found != 0) {
      mgrPtrAddress = found + 0x20;
      descriptorValid = true;
    }
  }

  if (!accessor->IsValidAddress(mgrPtrAddress)) {
    return false;
  }

  uintptr_t vehicleManager = 0;
  if (!accessor->Read(mgrPtrAddress, &vehicleManager, sizeof(vehicleManager)) ||
      vehicleManager == 0 || !accessor->IsValidAddress(vehicleManager)) {
    return false;
  }

  uintptr_t carPtrAddress = vehicleManager + 0x30;
  if (!accessor->IsValidAddress(carPtrAddress)) {
    return false;
  }

  uintptr_t dynamicsCar = 0;
  if (!accessor->Read(carPtrAddress, &dynamicsCar, sizeof(dynamicsCar)) ||
      dynamicsCar == 0 || !accessor->IsValidAddress(dynamicsCar)) {
    return false;
  }

  uintptr_t vtable = 0;
  if (!accessor->Read(dynamicsCar, &vtable, sizeof(vtable)) ||
      (gameBase != 0 && vtable != gameBase + 0x12af930)) {
    return false;
  }

  uintptr_t implPtrAddress = dynamicsCar + 0x08;
  if (!accessor->IsValidAddress(implPtrAddress)) {
    return false;
  }

  uintptr_t dynamicsCarImpl = 0;
  if (!accessor->Read(implPtrAddress, &dynamicsCarImpl,
                      sizeof(dynamicsCarImpl)) ||
      dynamicsCarImpl == 0 || !accessor->IsValidAddress(dynamicsCarImpl)) {
    return false;
  }

  s_vehicleAddress = dynamicsCarImpl;
  char hexBuf[32] = {};
  std::snprintf(hexBuf, sizeof(hexBuf), "%llX",
                static_cast<unsigned long long>(dynamicsCarImpl));
  Logger::Info(
      std::string("Player::ResolveVehicleAddress: Veiculo ancorado em 0x") +
      hexBuf);
  return true;
}

bool Player::CaptureState(CarState &outState) {
  if (s_scanner == nullptr || s_vehicleAddress == 0) {
    Logger::Warn(
        "Player::CaptureState: scanner ou vehicleAddress nao configurado.");
    return false;
  }

  IMemoryAccessor *accessor = s_scanner->GetAccessor();
  if (accessor == nullptr || !accessor->IsValidAddress(s_vehicleAddress)) {
    Logger::Warn("Player::CaptureState: accessor nulo ou endereco invalido.");
    return false;
  }

  if (!accessor->Read(s_vehicleAddress, &outState, sizeof(CarState))) {
    Logger::Warn("Player::CaptureState: falha na leitura de CarState.");
    return false;
  }

  return true;
}

bool Player::ApplyState(const CarState &state) {
  if (s_scanner == nullptr || s_vehicleAddress == 0) {
    Logger::Warn(
        "Player::ApplyState: scanner ou vehicleAddress nao configurado.");
    return false;
  }

  IMemoryAccessor *accessor = s_scanner->GetAccessor();
  if (accessor == nullptr || !accessor->IsValidAddress(s_vehicleAddress)) {
    Logger::Warn("Player::ApplyState: accessor nulo ou endereco invalido.");
    return false;
  }

  CarState sanitizedState = state;
  sanitizedState.angularVelocity = Vector3{0.f, 0.f, 0.f};
  for (int i = 0; i < 4; ++i) {
    sanitizedState.wheels[i].suspensionCompression =
        SUSPENSION_STATIC_SAG_RATIO;
    sanitizedState.wheels[i].inContact = true;
  }

  if (!accessor->Write(s_vehicleAddress, &sanitizedState, sizeof(CarState))) {
    Logger::Warn(
        "Player::ApplyState: falha na escrita de CarState na memoria.");
    return false;
  }

  return true;
}

} // namespace dr2hook
