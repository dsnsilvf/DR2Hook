#include "dr2hook/player.h"
#include "dr2hook/logger.h"

namespace dr2hook {

MemoryScanner *Player::s_scanner = nullptr;
uintptr_t Player::s_vehicleAddress = 0;

void Player::Configure(MemoryScanner *scanner, uintptr_t vehicleAddress) {
  s_scanner = scanner;
  s_vehicleAddress = vehicleAddress;
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
