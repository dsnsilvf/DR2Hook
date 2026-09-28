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

  // Candidatos estáticos na .data do DiRT Rally 2.0 (EGO Engine) para o carro ativo
  const uintptr_t carStaticAddrs[] = {
      gameBase + 0x1681ce8, // Ponteiro primário da sessão ativa do jogador
      gameBase + 0x15a4b00, // Tabela de veículos (Carro 1)
      gameBase + 0x15a9760  // Instância ativa em corrida
  };

  uintptr_t carPtr = 0;
  for (uintptr_t staticAddr : carStaticAddrs) {
    if (accessor->IsValidAddress(staticAddr)) {
      uintptr_t candidate = 0;
      if (accessor->Read(staticAddr, &candidate, sizeof(candidate)) &&
          candidate != 0 && accessor->IsValidAddress(candidate)) {
        carPtr = candidate;
        break;
      }
    }
  }

  uintptr_t container = 0;
  if (carPtr != 0 && accessor->IsValidAddress(carPtr)) {
    // car + 0x30 -> ponteiro para o container do veículo (0x4aa0f100)
    uintptr_t containerPtrAddr = carPtr + 0x30;
    if (accessor->IsValidAddress(containerPtrAddr)) {
      accessor->Read(containerPtrAddr, &container, sizeof(container));
    }
  }

  // Fallbacks para container global se o ponteiro da sessão não tiver +0x30
  if (container == 0 || !accessor->IsValidAddress(container)) {
    const uintptr_t containerStaticAddrs[] = {
        gameBase + 0x201b7c0,
        gameBase + 0x20203a8
    };
    for (uintptr_t cAddr : containerStaticAddrs) {
      if (accessor->IsValidAddress(cAddr)) {
        uintptr_t candidate = 0;
        if (accessor->Read(cAddr, &candidate, sizeof(candidate)) &&
            candidate != 0 && accessor->IsValidAddress(candidate)) {
          container = candidate;
          break;
        }
      }
    }
  }

  if (container == 0 || !accessor->IsValidAddress(container)) {
    return false;
  }

  // container + 0x08 -> ponteiro para o Physics Rig (DynamicsCarImpl)
  uintptr_t physicsRig = 0;
  uintptr_t rigPtrAddr = container + 0x08;
  if (!accessor->IsValidAddress(rigPtrAddr) ||
      !accessor->Read(rigPtrAddr, &physicsRig, sizeof(physicsRig)) ||
      physicsRig == 0 || !accessor->IsValidAddress(physicsRig)) {
    return false;
  }

  s_vehicleAddress = physicsRig;
  char hexBuf[32] = {};
  std::snprintf(hexBuf, sizeof(hexBuf), "%llX",
                static_cast<unsigned long long>(physicsRig));
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

  // 1. Verificacao de layout real da EGO Engine (Physics Rig com offsets +0x2d0)
  if (accessor->IsValidAddress(s_vehicleAddress + 0x2d0) &&
      accessor->IsValidAddress(s_vehicleAddress + 0x330)) {
    if (!accessor->Read(s_vehicleAddress + 0x2d0, &outState.position,
                        sizeof(Vector3))) {
      return false;
    }

    // Leitura do quaternion de orientacao
    accessor->Read(s_vehicleAddress + 0x2e0, &outState.quaternion,
                   sizeof(Vector4));

    Vector3 row0{}, row1{}, row2{};
    if (accessor->Read(s_vehicleAddress + 0x2f0, &row0, sizeof(Vector3)) &&
        accessor->Read(s_vehicleAddress + 0x300, &row1, sizeof(Vector3)) &&
        accessor->Read(s_vehicleAddress + 0x310, &row2, sizeof(Vector3))) {
      outState.rotationMatrix.m[0][0] = row0.x;
      outState.rotationMatrix.m[0][1] = row0.y;
      outState.rotationMatrix.m[0][2] = row0.z;
      outState.rotationMatrix.m[1][0] = row1.x;
      outState.rotationMatrix.m[1][1] = row1.y;
      outState.rotationMatrix.m[1][2] = row1.z;
      outState.rotationMatrix.m[2][0] = row2.x;
      outState.rotationMatrix.m[2][1] = row2.y;
      outState.rotationMatrix.m[2][2] = row2.z;
    }

    accessor->Read(s_vehicleAddress + 0x320, &outState.linearVelocity,
                   sizeof(Vector3));
    accessor->Read(s_vehicleAddress + 0x330, &outState.angularVelocity,
                   sizeof(Vector3));

    for (int i = 0; i < 4; ++i) {
      outState.wheels[i].suspensionCompression = SUSPENSION_STATIC_SAG_RATIO;
      outState.wheels[i].angularVelocity = 0.f;
      outState.wheels[i].inContact = true;
    }
    return true;
  }

  // Fallback para teste unitario com mock direto compacto
  if (!accessor->Read(s_vehicleAddress, &outState, sizeof(CarState))) {
    Logger::Warn("Player::CaptureState: falha na leitura de CarState.");
    return false;
  }

  return true;
}

bool Player::ApplyState(const CarState &state, RestoreMode mode) {
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

  // 1. Verificacao de layout real da EGO Engine (Physics Rig com offsets +0x2d0)
  if (accessor->IsValidAddress(s_vehicleAddress + 0x2d0) &&
      accessor->IsValidAddress(s_vehicleAddress + 0x330)) {
    // 1. Escrever posicao física (+0x2d0)
    accessor->Write(s_vehicleAddress + 0x2d0, &state.position, sizeof(Vector3));

    // 2. Escrever quaternion se for válido
    float quatNormSq = state.quaternion.x * state.quaternion.x +
                       state.quaternion.y * state.quaternion.y +
                       state.quaternion.z * state.quaternion.z +
                       state.quaternion.w * state.quaternion.w;
    if (quatNormSq > 0.1f) {
      accessor->Write(s_vehicleAddress + 0x2e0, &state.quaternion,
                      sizeof(Vector4));
    }

    // 3. Escrever orientacao/matriz 3x3 (+0x2f0, +0x300, +0x310)
    Vector3 row0{state.rotationMatrix.m[0][0], state.rotationMatrix.m[0][1],
                 state.rotationMatrix.m[0][2]};
    Vector3 row1{state.rotationMatrix.m[1][0], state.rotationMatrix.m[1][1],
                 state.rotationMatrix.m[1][2]};
    Vector3 row2{state.rotationMatrix.m[2][0], state.rotationMatrix.m[2][1],
                 state.rotationMatrix.m[2][2]};
    accessor->Write(s_vehicleAddress + 0x2f0, &row0, sizeof(Vector3));
    accessor->Write(s_vehicleAddress + 0x300, &row1, sizeof(Vector3));
    accessor->Write(s_vehicleAddress + 0x310, &row2, sizeof(Vector3));

    // 4. Velocidade linear e angular de acordo com o modo
    if (mode == RestoreMode::WithMomentum) {
      accessor->Write(s_vehicleAddress + 0x320, &state.linearVelocity,
                      sizeof(Vector3));
      accessor->Write(s_vehicleAddress + 0x330, &state.angularVelocity,
                      sizeof(Vector3));
    } else {
      Vector3 zeroVel{0.f, 0.f, 0.f};
      accessor->Write(s_vehicleAddress + 0x320, &zeroVel, sizeof(Vector3));
      accessor->Write(s_vehicleAddress + 0x330, &zeroVel, sizeof(Vector3));
    }

    // 5. Sincronizacao de transform visual no container (+0xcd0)
    uintptr_t container = 0;
    if (accessor->Read(s_vehicleAddress, &container, sizeof(container)) &&
        container != 0 && accessor->IsValidAddress(container + 0xcd0)) {
      Vector3 gfxPos = state.position;
      gfxPos.y -= 0.44f;
      accessor->Write(container + 0xcd0, &gfxPos, sizeof(Vector3));
    }
    return true;
  }

  // Fallback para teste unitario com mock direto compacto
  CarState sanitizedState = state;
  if (mode == RestoreMode::WithMomentum) {
    sanitizedState.linearVelocity = state.linearVelocity;
    sanitizedState.angularVelocity = state.angularVelocity;
  } else {
    sanitizedState.linearVelocity = Vector3{0.f, 0.f, 0.f};
    sanitizedState.angularVelocity = Vector3{0.f, 0.f, 0.f};
  }
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

