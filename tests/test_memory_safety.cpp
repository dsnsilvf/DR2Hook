#include "dr2hook/logger.h"
#include "dr2hook/memory.h"
#include "dr2hook/player.h"
#include "dr2hook/safety.h"
#include "dr2hook/savestate.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int g_testsRun = 0;
static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(condition, msg)                                            \
  do {                                                                         \
    ++g_testsRun;                                                              \
    if (condition) {                                                           \
      ++g_testsPassed;                                                         \
    } else {                                                                   \
      ++g_testsFailed;                                                         \
      std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " ("            \
                << __func__ << "): " << (msg) << " -> Assertion '"             \
                << #condition << "' failed." << std::endl;                     \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_FLOAT_NEAR(val, expected, eps, msg)                        \
  TEST_ASSERT(std::fabs((val) - (expected)) <= (eps), msg)

// ---------------------------------------------------------------------------
// 1. Validação de leitura/escrita do MockMemoryAccessor
// ---------------------------------------------------------------------------
void TestMockMemoryAccessor() {
  std::cout << "[RUN] TestMockMemoryAccessor..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  constexpr uintptr_t testAddr = 0x140000000;
  const std::vector<uint8_t> testData = {0x11, 0x22, 0x33, 0x44, 0x55};

  // Endereço não mapeado deve falhar
  TEST_ASSERT(!mock.IsValidAddress(testAddr), "Endereço inicial não mapeado");
  std::vector<uint8_t> readBuf(testData.size(), 0);
  TEST_ASSERT(!mock.Read(testAddr, readBuf.data(), readBuf.size()),
              "Read em memória não mapeada deve retornar false");
  TEST_ASSERT(!mock.Write(testAddr, testData.data(), testData.size()),
              "Write em memória não mapeada deve retornar false");

  // Endereço nulo ou parâmetros inválidos
  TEST_ASSERT(!mock.IsValidAddress(0), "Endereço 0 é inválido");
  TEST_ASSERT(!mock.Read(0, readBuf.data(), readBuf.size()),
              "Read no endereço 0 deve retornar false");
  TEST_ASSERT(!mock.Write(0, testData.data(), testData.size()),
              "Write no endereço 0 deve retornar false");
  TEST_ASSERT(!mock.Read(testAddr, nullptr, 10),
              "Read com buffer nulo deve retornar false");
  TEST_ASSERT(!mock.Write(testAddr, nullptr, 10),
              "Write com buffer nulo deve retornar false");
  TEST_ASSERT(!mock.Read(testAddr, readBuf.data(), 0),
              "Read com tamanho zero deve retornar false");
  TEST_ASSERT(!mock.Write(testAddr, testData.data(), 0),
              "Write com tamanho zero deve retornar false");

  // Mapear memória com SetMemory e ler de volta
  mock.SetMemory(testAddr, testData);
  TEST_ASSERT(mock.IsValidAddress(testAddr), "Endereço agora é válido");
  TEST_ASSERT(mock.IsValidAddress(testAddr + testData.size() - 1),
              "Fim da faixa mapeada é válida");
  TEST_ASSERT(!mock.IsValidAddress(testAddr + testData.size()),
              "Bytes além da faixa mapeada não são válidos");

  bool readOk = mock.Read(testAddr, readBuf.data(), readBuf.size());
  TEST_ASSERT(readOk, "Read deve ter sucesso na memória mapeada");
  TEST_ASSERT(readBuf == testData, "Dados lidos devem coincidir com gravados");

  // Escrever novos dados sobre a região mapeada
  const std::vector<uint8_t> updatedData = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
  bool writeOk = mock.Write(testAddr, updatedData.data(), updatedData.size());
  TEST_ASSERT(writeOk, "Write deve suceder em memória já mapeada");
  mock.Read(testAddr, readBuf.data(), readBuf.size());
  TEST_ASSERT(readBuf == updatedData, "Dados sobrescritos devem ser lidos");

  // Invalidar endereço
  mock.InvalidateAddress(testAddr + 2);
  TEST_ASSERT(!mock.IsValidAddress(testAddr + 2),
              "Endereço invalidado não deve ser válido");
  TEST_ASSERT(!mock.Read(testAddr, readBuf.data(), readBuf.size()),
              "Read sobre endereço com buraco deve falhar");

  // Construtor com tamanho inicial
  dr2hook::MockMemoryAccessor mockSized(256, 0x2000);
  TEST_ASSERT(mockSized.IsValidAddress(0x2000), "Base pré-alocada válida");
  TEST_ASSERT(mockSized.IsValidAddress(0x20FF), "Topo pré-alocado válido");
  TEST_ASSERT(!mockSized.IsValidAddress(0x2100), "Fora da faixa inválido");
  uint8_t zeroByte = 0xFF;
  mockSized.Read(0x2000, &zeroByte, 1);
  TEST_ASSERT(zeroByte == 0, "Memória pré-alocada deve ser inicializada em 0");
}

// ---------------------------------------------------------------------------
// 2. Validação do FindPattern com wildcards ? e ??
// ---------------------------------------------------------------------------
void TestFindPattern() {
  std::cout << "[RUN] TestFindPattern..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t baseAddr = 0x140100000;
  // Bloco de bytes simulando código de máquina com bytes conhecidos:
  // 0x00: 0x90, 0x90, 0x90, 0x90
  // 0x04: 0x48, 0x8B, 0x05, 0xEF, 0xBE, 0xAD, 0xDE, 0x48, 0x85, 0xC0
  // 0x0E: 0xCC, 0xCC, 0xCC
  std::vector<uint8_t> codeBytes = {
      0x90, 0x90, 0x90, 0x90, 0x48, 0x8B, 0x05, 0xEF, 0xBE,
      0xAD, 0xDE, 0x48, 0x85, 0xC0, 0xCC, 0xCC, 0xCC,
  };
  mock.SetMemory(baseAddr, codeBytes);

  // 1. Padrão exato
  uintptr_t found = scanner.FindPattern(baseAddr, codeBytes.size(),
                                        "48 8B 05 EF BE AD DE 48 85 C0");
  TEST_ASSERT(found == baseAddr + 4, "Busca de padrão exato");

  // 2. Padrão com wildcards de byte único '?'
  found = scanner.FindPattern(baseAddr, codeBytes.size(),
                              "48 8B 05 ? ? ? ? 48 85 C0");
  TEST_ASSERT(found == baseAddr + 4, "Busca com wildcard '?'");

  // 3. Padrão com wildcards duplos '? ?' e '??'
  found = scanner.FindPattern(baseAddr, codeBytes.size(),
                              "48 8B 05 ?? ?? ?? ?? 48 85 C0");
  TEST_ASSERT(found == baseAddr + 4, "Busca com wildcard duplo ??");

  // 4. Mistura de '?' e '??' com múltiplos espaços
  found = scanner.FindPattern(baseAddr, codeBytes.size(),
                              "48  8B  ?  ??  BE  AD  ?  48  85  C0");
  TEST_ASSERT(found == baseAddr + 4,
              "Busca com wildcards mistos e espaçamento");

  // 5. Padrão não existente deve retornar 0
  found = scanner.FindPattern(baseAddr, codeBytes.size(), "FF FF FF FF");
  TEST_ASSERT(found == 0, "Padrão inexistente retorna 0");

  // 6. SearchSize menor que o padrão
  found = scanner.FindPattern(baseAddr, 2, "48 8B 05");
  TEST_ASSERT(found == 0, "SearchSize menor que o padrão retorna 0");

  // 7. Padrão vazio
  found = scanner.FindPattern(baseAddr, codeBytes.size(), "");
  TEST_ASSERT(found == 0, "Padrão vazio retorna 0");

  // 8. Endereço inicial inválido
  found = scanner.FindPattern(0, codeBytes.size(), "48 8B 05");
  TEST_ASSERT(found == 0, "Endereço inicial 0 retorna 0");

  // 9. Scanner sem accessor
  dr2hook::MemoryScanner emptyScanner(nullptr);
  found = emptyScanner.FindPattern(baseAddr, codeBytes.size(), "48 8B");
  TEST_ASSERT(found == 0, "Scanner sem accessor retorna 0");
}

// ---------------------------------------------------------------------------
// 3. Validação do ResolvePointerChain em cadeias válidas e quebradas/nulas
// ---------------------------------------------------------------------------
void TestResolvePointerChain() {
  std::cout << "[RUN] TestResolvePointerChain..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t baseAddr = 0x140200000;
  constexpr uintptr_t ptr1 = 0x140300000;
  constexpr uintptr_t ptr2 = 0x140400000;
  constexpr uintptr_t targetObj = 0x140500000;

  // Montagem da cadeia de ponteiros:
  // baseAddr -> aponta para ptr1
  // ptr1 + 0x28 -> aponta para ptr2
  // ptr2 + 0x10 -> aponta para targetObj
  // offset final: 0x40 -> endereço esperado = targetObj + 0x40
  mock.SetValue(baseAddr, ptr1);
  mock.SetValue(ptr1 + 0x28, ptr2);
  mock.SetValue(ptr2 + 0x10, targetObj);

  // Mapear o endereço final para ser válido
  mock.SetValue(targetObj + 0x40, uint64_t{0x12345678});

  // 1. Resolução com 3 offsets (cadeia válida)
  std::vector<uintptr_t> offsets = {0x28, 0x10, 0x40};
  uintptr_t resolved = scanner.ResolvePointerChain(baseAddr, offsets);
  TEST_ASSERT(resolved == targetObj + 0x40,
              "ResolvePointerChain com múltiplos níveis deve resolver");

  // 2. Resolução com 1 offset
  mock.SetValue(targetObj, uint64_t{0x999});
  std::vector<uintptr_t> singleOffset = {0x00};
  uintptr_t singleResolved =
      scanner.ResolvePointerChain(ptr2 + 0x10, singleOffset);
  TEST_ASSERT(singleResolved == targetObj,
              "ResolvePointerChain com 1 nível deve resolver");

  // 3. Resolução com cadeia vazia (retorna baseAddress se válido)
  uintptr_t emptyChain = scanner.ResolvePointerChain(baseAddr, {});
  TEST_ASSERT(emptyChain == baseAddr,
              "ResolvePointerChain com offsets vazios retorna baseAddress");

  // 4. Cadeia quebrada: ponteiro nulo intermediário
  mock.SetValue(ptr1 + 0x28, uintptr_t{0});
  uintptr_t brokenNull = scanner.ResolvePointerChain(baseAddr, offsets);
  TEST_ASSERT(brokenNull == 0,
              "ResolvePointerChain com ponteiro nulo deve retornar 0");

  // 5. Cadeia quebrada: memória não mapeada / falha de leitura
  mock.InvalidateAddress(ptr1 + 0x28);
  uintptr_t brokenUnmapped = scanner.ResolvePointerChain(baseAddr, offsets);
  TEST_ASSERT(brokenUnmapped == 0,
              "ResolvePointerChain com leitura falha deve retornar 0");

  // 6. Base address nulo
  uintptr_t nullBase = scanner.ResolvePointerChain(0, offsets);
  TEST_ASSERT(nullBase == 0,
              "ResolvePointerChain com base nula deve retornar 0");

  // 7. Scanner com accessor nulo
  dr2hook::MemoryScanner nullScanner(nullptr);
  uintptr_t nullAcc = nullScanner.ResolvePointerChain(baseAddr, offsets);
  TEST_ASSERT(nullAcc == 0,
              "ResolvePointerChain com accessor nulo deve retornar 0");
}

// ---------------------------------------------------------------------------
// 4. Validação da reavaliação dinâmica e fail-closed do SafetyGuard
// ---------------------------------------------------------------------------
void TestSafetyGuard() {
  std::cout << "[RUN] TestSafetyGuard..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t sessionModeAddr = 0x140600000;
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::Unknown));

  dr2hook::SafetyGuard::Configure(&scanner, sessionModeAddr);

  // 1. Unknown -> CanWriteState == false (Fail-Closed)
  TEST_ASSERT(dr2hook::SafetyGuard::EvaluateCurrentMode() ==
                  dr2hook::GameSessionMode::Unknown,
              "Modo inicial Unknown");
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser false em Unknown");

  // 2. Modos Homologados para Treino / Offline
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::DirtFish));
  TEST_ASSERT(dr2hook::SafetyGuard::EvaluateCurrentMode() ==
                  dr2hook::GameSessionMode::DirtFish,
              "Modo DirtFish detectado");
  TEST_ASSERT(dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser true em DirtFish");

  mock.SetValue(
      sessionModeAddr,
      static_cast<uint32_t>(dr2hook::GameSessionMode::TimeTrialOffline));
  TEST_ASSERT(dr2hook::SafetyGuard::EvaluateCurrentMode() ==
                  dr2hook::GameSessionMode::TimeTrialOffline,
              "Modo TimeTrialOffline detectado");
  TEST_ASSERT(dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser true em TimeTrialOffline");

  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::CustomOffline));
  TEST_ASSERT(dr2hook::SafetyGuard::EvaluateCurrentMode() ==
                  dr2hook::GameSessionMode::CustomOffline,
              "Modo CustomOffline detectado");
  TEST_ASSERT(dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser true em CustomOffline");

  // 3. Modos Competitivos / Online RaceNet -> Hard-Lock (CanWriteState ==
  // false)
  mock.SetValue(sessionModeAddr, static_cast<uint32_t>(
                                     dr2hook::GameSessionMode::DailyChallenge));
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser false em DailyChallenge");

  mock.SetValue(
      sessionModeAddr,
      static_cast<uint32_t>(dr2hook::GameSessionMode::WeeklyChallenge));
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser false em WeeklyChallenge");

  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::ClubOnline));
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser false em ClubOnline");

  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::CareerOnline));
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "CanWriteState deve ser false em CareerOnline");

  // 4. Reavaliação Dinâmica em Tempo Real (Sem cache!)
  // Jogador entra em DirtFish
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::DirtFish));
  TEST_ASSERT(dr2hook::SafetyGuard::CanWriteState(),
              "Offline -> escrita liberada");
  // Jogador inicia desafio Daily online de repente:
  mock.SetValue(sessionModeAddr, static_cast<uint32_t>(
                                     dr2hook::GameSessionMode::DailyChallenge));
  TEST_ASSERT(
      !dr2hook::SafetyGuard::CanWriteState(),
      "Transição imediata para DailyChallenge bloqueia escrita sem cache");

  // 5. Fail-Closed em Falha de Leitura ou Desconexão
  mock.InvalidateAddress(sessionModeAddr);
  TEST_ASSERT(dr2hook::SafetyGuard::EvaluateCurrentMode() ==
                  dr2hook::GameSessionMode::Unknown,
              "Falha de leitura resulta em Unknown");
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "Falha de leitura resulta em CanWriteState false");

  // 6. Endereço não configurado (0)
  dr2hook::SafetyGuard::Configure(&scanner, 0);
  TEST_ASSERT(dr2hook::SafetyGuard::EvaluateCurrentMode() ==
                  dr2hook::GameSessionMode::Unknown,
              "Endereço 0 resulta em Unknown");
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "Endereço 0 resulta em CanWriteState false");

  // 7. Scanner nulo
  dr2hook::SafetyGuard::Configure(nullptr, sessionModeAddr);
  TEST_ASSERT(dr2hook::SafetyGuard::EvaluateCurrentMode() ==
                  dr2hook::GameSessionMode::Unknown,
              "Scanner nulo resulta em Unknown");
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "Scanner nulo resulta em CanWriteState false");
}

// ---------------------------------------------------------------------------
// 5. Validação da sanitização de física do Player::ApplyState
// ---------------------------------------------------------------------------
void TestPlayerStateSanitization() {
  std::cout << "[RUN] TestPlayerStateSanitization..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t vehicleAddr = 0x140700000;

  dr2hook::CarState initialState{};
  initialState.position = {123.4f, 567.8f, -90.1f};
  initialState.linearVelocity = {45.0f, -2.0f, 15.0f};
  // Velocidade angular residual agressiva (ex.: carro em drift ou capotamento)
  initialState.angularVelocity = {2.5f, -1.8f, 3.2f};
  initialState.rotationMatrix.m[0][0] = 1.0f;
  initialState.rotationMatrix.m[1][1] = 1.0f;
  initialState.rotationMatrix.m[2][2] = 1.0f;

  // Suspensões em estados arbitrários/comprimidas/airborne
  initialState.wheels[0] = {0.05f, 50.0f, false};
  initialState.wheels[1] = {0.80f, 30.0f, true};
  initialState.wheels[2] = {0.00f, 0.0f, false};
  initialState.wheels[3] = {0.95f, -10.0f, true};

  mock.SetValue(vehicleAddr, initialState);
  dr2hook::Player::Configure(&scanner, vehicleAddr);

  // 1. Capturar estado
  dr2hook::CarState capturedState{};
  bool capOk = dr2hook::Player::CaptureState(capturedState);
  TEST_ASSERT(capOk, "CaptureState deve retornar true");
  TEST_ASSERT_FLOAT_NEAR(capturedState.position.x, 123.4f, 0.001f,
                         "Posição X capturada");
  TEST_ASSERT_FLOAT_NEAR(capturedState.angularVelocity.x, 2.5f, 0.001f,
                         "AngularVelocity X original capturada");

  // 2. Aplicar estado normalmente (parado / sem momentum)
  bool applyOk = dr2hook::Player::ApplyState(capturedState, dr2hook::RestoreMode::Normal);
  TEST_ASSERT(applyOk, "ApplyState Normal deve retornar true");

  // 3. Ler o estado gravado na memória para verificar a sanitização normal
  dr2hook::CarState appliedMemoryState{};
  mock.Read(vehicleAddr, &appliedMemoryState, sizeof(dr2hook::CarState));

  // Posição deve ser preservada
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.position.x, 123.4f, 0.001f,
                         "Posição X preservada");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.position.y, 567.8f, 0.001f,
                         "Posição Y preservada");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.position.z, -90.1f, 0.001f,
                         "Posição Z preservada");

  // No modo Normal, velocidade linear e angular DEVEM SER ZERADAS (parado no checkpoint)
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.linearVelocity.x, 0.0f, 0.0001f,
                         "Velocidade linear X zerada no modo Normal");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.linearVelocity.y, 0.0f, 0.0001f,
                         "Velocidade linear Y zerada no modo Normal");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.linearVelocity.z, 0.0f, 0.0001f,
                         "Velocidade linear Z zerada no modo Normal");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.angularVelocity.x, 0.0f, 0.0001f,
                         "AngularVelocity X zerada no modo Normal");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.angularVelocity.y, 0.0f, 0.0001f,
                         "AngularVelocity Y zerada no modo Normal");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.angularVelocity.z, 0.0f, 0.0001f,
                         "AngularVelocity Z zerada no modo Normal");

  // 4. Aplicar estado com momentum (RestoreMode::WithMomentum)
  bool applyMomentumOk = dr2hook::Player::ApplyState(capturedState, dr2hook::RestoreMode::WithMomentum);
  TEST_ASSERT(applyMomentumOk, "ApplyState WithMomentum deve retornar true");
  mock.Read(vehicleAddr, &appliedMemoryState, sizeof(dr2hook::CarState));

  // Velocidade linear e angular DEVEM SER PRESERVADAS no modo WithMomentum
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.linearVelocity.x, 45.0f, 0.001f,
                         "Velocidade linear X preservada com momentum");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.linearVelocity.y, -2.0f, 0.001f,
                         "Velocidade linear Y preservada com momentum");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.linearVelocity.z, 15.0f, 0.001f,
                         "Velocidade linear Z preservada com momentum");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.angularVelocity.x, 2.5f, 0.001f,
                         "Velocidade angular X preservada com momentum");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.angularVelocity.y, -1.8f, 0.001f,
                         "Velocidade angular Y preservada com momentum");
  TEST_ASSERT_FLOAT_NEAR(appliedMemoryState.angularVelocity.z, 3.2f, 0.001f,
                         "Velocidade angular Z preservada com momentum");

  // Todas as 4 rodas DEVEM ESTAR normalizadas para repouso estático (0.35f) e
  // em contato
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_FLOAT_NEAR(
        appliedMemoryState.wheels[i].suspensionCompression,
        dr2hook::SUSPENSION_STATIC_SAG_RATIO, 0.0001f,
        ("Roda " + std::to_string(i) + " compressão normalizada para sag 0.35f")
            .c_str());
    TEST_ASSERT(appliedMemoryState.wheels[i].inContact,
                ("Roda " + std::to_string(i) + " inContact true").c_str());
  }

  // Falha de escrita se endereço não configurado
  dr2hook::Player::Configure(&scanner, 0);
  TEST_ASSERT(!dr2hook::Player::ApplyState(capturedState),
              "ApplyState com endereço 0 falha");
  TEST_ASSERT(!dr2hook::Player::CaptureState(capturedState),
              "CaptureState com endereço 0 falha");
}

// ---------------------------------------------------------------------------
// 6. Validação da integração do SavestateManager (F5 e F6 com gate)
// ---------------------------------------------------------------------------
void TestSavestateManagerIntegration() {
  std::cout << "[RUN] TestSavestateManagerIntegration..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t vehicleAddr = 0x140800000;
  constexpr uintptr_t sessionModeAddr = 0x140900000;

  dr2hook::CarState initialCarState{};
  initialCarState.position = {10.0f, 20.0f, 30.0f};
  initialCarState.angularVelocity = {1.0f, 1.0f, 1.0f};
  mock.SetValue(vehicleAddr, initialCarState);

  mock.SetValue(
      sessionModeAddr,
      static_cast<uint32_t>(dr2hook::GameSessionMode::TimeTrialOffline));

  dr2hook::Player::Configure(&scanner, vehicleAddr);
  dr2hook::SafetyGuard::Configure(&scanner, sessionModeAddr);

  TEST_ASSERT(dr2hook::SavestateManager::Initialize(),
              "SavestateManager inicializa");
  TEST_ASSERT(!dr2hook::SavestateManager::HasSavedState(),
              "Inicialmente sem estado gravado");

  // Key up não deve fazer nada
  dr2hook::SavestateManager::OnKeyAction(VK_F5, false);
  TEST_ASSERT(!dr2hook::SavestateManager::HasSavedState(),
              "Key up F5 não aciona gravação");

  // Pressionar F5 (Gravar estado)
  dr2hook::SavestateManager::OnKeyAction(VK_F5, true);
  TEST_ASSERT(dr2hook::SavestateManager::HasSavedState(),
              "F5 down grava savestate com sucesso");
  TEST_ASSERT_FLOAT_NEAR(dr2hook::SavestateManager::GetSavedState().position.x,
                         10.0f, 0.001f,
                         "Estado gravado possui coordenadas originais");

  // Simular deslocamento do carro na pista (carro andou até 100, 200, 300)
  dr2hook::CarState movedCarState = initialCarState;
  movedCarState.position = {100.0f, 200.0f, 300.0f};
  mock.SetValue(vehicleAddr, movedCarState);

  // 1. Tentar restaurar em modo restrito / online (RaceNet CareerOnline)
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::CareerOnline));
  TEST_ASSERT(!dr2hook::SafetyGuard::CanWriteState(),
              "SafetyGuard bloqueia escrita em modo CareerOnline");

  dr2hook::SavestateManager::OnKeyAction(VK_F6, true);

  // Memória do veículo NÃO PODE TER SIDO MODIFICADA
  dr2hook::CarState currentVehicle{};
  mock.Read(vehicleAddr, &currentVehicle, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      currentVehicle.position.x, 100.0f, 0.001f,
      "F6 em modo restrito é bloqueado: memória intocada (posição x = 100)");

  // 2. Retornar para modo homologado (DirtFish) e restaurar
  mock.SetValue(sessionModeAddr,
                static_cast<uint32_t>(dr2hook::GameSessionMode::DirtFish));
  TEST_ASSERT(dr2hook::SafetyGuard::CanWriteState(),
              "SafetyGuard libera escrita em DirtFish");

  // Restauração Normal via F6
  dr2hook::SavestateManager::SetRestoreMode(dr2hook::RestoreMode::Normal);
  TEST_ASSERT(dr2hook::SavestateManager::GetRestoreMode() == dr2hook::RestoreMode::Normal,
              "RestoreMode configurado como Normal");

  dr2hook::SavestateManager::OnKeyAction(VK_F6, true);

  // Memória do veículo deve ser restaurada para a posição original gravada (10,
  // 20, 30) e com velocidades zeradas no modo Normal
  mock.Read(vehicleAddr, &currentVehicle, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      currentVehicle.position.x, 10.0f, 0.001f,
      "F6 em modo permitido restaura posição salva (posição x = 10)");
  TEST_ASSERT_FLOAT_NEAR(currentVehicle.linearVelocity.x, 0.0f, 0.0001f,
                         "Física restaurada normal possui velocidade linear zerada");
  TEST_ASSERT_FLOAT_NEAR(currentVehicle.angularVelocity.x, 0.0f, 0.0001f,
                         "Física restaurada normal possui velocidade angular zerada");

  // 3. Modificar novamente e testar restauração com momentum via F7
  movedCarState.position = {500.0f, 600.0f, 700.0f};
  mock.SetValue(vehicleAddr, movedCarState);

  dr2hook::SavestateManager::OnKeyAction(0x76, true); // VK_F7
  mock.Read(vehicleAddr, &currentVehicle, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      currentVehicle.position.x, 10.0f, 0.001f,
      "F7 restaura posição salva (posição x = 10)");
  TEST_ASSERT_FLOAT_NEAR(currentVehicle.angularVelocity.x, 1.0f, 0.0001f,
                         "F7 com momentum preserva velocidade angular original (1.0)");

  // 4. Testar chaveamento de modo do F6 para WithMomentum
  movedCarState.position = {800.0f, 900.0f, 1000.0f};
  mock.SetValue(vehicleAddr, movedCarState);
  dr2hook::SavestateManager::SetRestoreMode(dr2hook::RestoreMode::WithMomentum);
  TEST_ASSERT(dr2hook::SavestateManager::GetRestoreMode() == dr2hook::RestoreMode::WithMomentum,
              "RestoreMode configurado como WithMomentum");

  dr2hook::SavestateManager::OnKeyAction(VK_F6, true);
  mock.Read(vehicleAddr, &currentVehicle, sizeof(dr2hook::CarState));
  TEST_ASSERT_FLOAT_NEAR(
      currentVehicle.position.x, 10.0f, 0.001f,
      "F6 com modo WithMomentum ativo restaura posição salva (posição x = 10)");
  TEST_ASSERT_FLOAT_NEAR(currentVehicle.angularVelocity.x, 1.0f, 0.0001f,
                         "F6 com modo WithMomentum ativo preserva velocidade angular (1.0)");

  // Finalização
  dr2hook::SavestateManager::Shutdown();
  TEST_ASSERT(!dr2hook::SavestateManager::HasSavedState(),
              "Shutdown limpa savestate");
}

// ---------------------------------------------------------------------------
// 7. Validação da resolução dinâmica do veículo (ResolveVehicleAddress)
// ---------------------------------------------------------------------------
void TestResolveVehicleAddress() {
  std::cout << "[RUN] TestResolveVehicleAddress..." << std::endl;

  dr2hook::MockMemoryAccessor mock;
  dr2hook::MemoryScanner scanner(&mock);

  constexpr uintptr_t gameBase = 0x140000000;
  constexpr uintptr_t primaryCarPtrAddr = gameBase + 0x1681ce8;
  constexpr uintptr_t carAddr = 0x4a9d4b80;
  constexpr uintptr_t containerAddr = 0x4aa0f100;
  constexpr uintptr_t physicsRigAddr = 0x4aa1bab0;

  dr2hook::Player::Configure(&scanner, 0);

  // 1. gameBase 0 deve falhar
  TEST_ASSERT(!dr2hook::Player::ResolveVehicleAddress(0),
              "gameBase 0 deve retornar false");

  // 2. gameBase sem memória mapeada deve falhar
  TEST_ASSERT(!dr2hook::Player::ResolveVehicleAddress(gameBase),
              "gameBase não mapeado deve retornar false");

  // 3. Mapear cadeia completa:
  // gameBase -> primaryCarPtrAddr -> carAddr -> carAddr + 0x30 -> containerAddr -> containerAddr + 0x08 -> physicsRigAddr
  mock.SetValue(gameBase, uint16_t(0x5A4D)); // 'MZ'
  mock.SetValue(primaryCarPtrAddr, carAddr);
  mock.SetValue(carAddr, uint64_t(0x14127cc00)); // Vtable do carro
  mock.SetValue(carAddr + 0x30, containerAddr);
  mock.SetValue(containerAddr, uint64_t(0x141400c30)); // Vtable do container
  mock.SetValue(containerAddr + 0x08, physicsRigAddr);
  mock.SetValue(physicsRigAddr, containerAddr);

  TEST_ASSERT(dr2hook::Player::ResolveVehicleAddress(gameBase),
              "ResolveVehicleAddress deve ter sucesso com cadeia primária");
  TEST_ASSERT(dr2hook::Player::GetVehicleAddress() == physicsRigAddr,
              "Endereço do veículo ancorado deve ser o physicsRig");

  // 4. Testar fallback para container estático (0x201b7c0)
  dr2hook::Player::Configure(&scanner, 0);
  mock.SetValue(primaryCarPtrAddr, uintptr_t(0)); // Anular primário
  constexpr uintptr_t staticContainerAddr = gameBase + 0x201b7c0;
  mock.SetValue(staticContainerAddr, containerAddr);

  TEST_ASSERT(dr2hook::Player::ResolveVehicleAddress(gameBase),
              "ResolveVehicleAddress deve ter sucesso com fallback de container");
  TEST_ASSERT(dr2hook::Player::GetVehicleAddress() == physicsRigAddr,
              "Endereço do veículo ancorado deve ser o physicsRig via fallback");
}

// ---------------------------------------------------------------------------
// Main Runner
// ---------------------------------------------------------------------------
int main() {
  std::cout << "====================================================="
            << std::endl;
  std::cout << "DR2Hook - Suíte de Testes Unitários de Memória e Segurança"
            << std::endl;
  std::cout << "====================================================="
            << std::endl;

  TestMockMemoryAccessor();
  TestFindPattern();
  TestResolvePointerChain();
  TestSafetyGuard();
  TestPlayerStateSanitization();
  TestSavestateManagerIntegration();
  TestResolveVehicleAddress();

  std::cout << "====================================================="
            << std::endl;
  std::cout << "Resultados dos Testes:" << std::endl;
  std::cout << "  Total de asserções executadas: " << g_testsRun << std::endl;
  std::cout << "  Passaram: " << g_testsPassed << std::endl;
  std::cout << "  Falharam: " << g_testsFailed << std::endl;
  std::cout << "====================================================="
            << std::endl;

  return (g_testsFailed == 0) ? 0 : 1;
}
