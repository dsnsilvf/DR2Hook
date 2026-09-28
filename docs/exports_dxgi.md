# Documentação Técnica: Exports Mapeados da dxgi.dll (Proxy Layer)

## 1. Visão Geral

O **DR2Hook** intercepta a inicialização do executável do jogo (*DiRT Rally 2.0* — `dirtrally2.exe`, x64) utilizando a técnica de **Proxy DLL** (ADR-001).
A biblioteca `dxgi.dll` é depositada no diretório raiz do jogo, ao lado do executável. Quando o sistema operacional carrega as dependências dinâmicas do jogo, ele prioriza a DLL presente no diretório local da aplicação em relação àquela localizada no diretório de sistema (`C:\Windows\System32\dxgi.dll`).

Para garantir que o subsistema gráfico DirectX 11 opere normalmente e sem falhas, nossa proxy DLL exporta as funções essenciais da API DXGI e as redireciona dinamicamente para a biblioteca oficial do Windows.

---

## 2. Decisão Arquitetural: Stubs Manuais C++ vs. Arquivo `.def` (Direct Forwarding)

### Por que NÃO usar `.def` de Direct Forwarding?
Em implementações triviais de proxy DLL, desenvolvedores frequentemente utilizam arquivos de definição de módulo (`.def`) contendo diretivas de repasse direto, como:
```def
EXPORTS
    CreateDXGIFactory=C:\Windows\System32\dxgi.CreateDXGIFactory
    CreateDXGIFactory1=C:\Windows\System32\dxgi.CreateDXGIFactory1
    ...
```
Embora simples, essa abordagem apresenta limitações críticas e inaceitáveis para a arquitetura do DR2Hook:
1. **Ignoramento do Código da DLL:** O loader do Windows (*ntdll Ldr*) resolve repasses estáticos via `.def` diretamente para a DLL de destino sem passar por nenhum código intermediário nosso.
2. **Impossibilidade de Diagnóstico e Logging:** Não é possível registrar quando, como ou com quais parâmetros as funções gráficas foram solicitadas pela engine (EGO Engine).
3. **Inviabilidade de Hooks Futuros:** A Fase 2 do projeto requer interceptação de chamadas de criação de swapchain (`IDXGIFactory::CreateSwapChain`) e ancoragem de hooks (`IDXGISwapChain::Present`). O repasse cego via `.def` bloqueia a capacidade de inspecionar ou embrulhar interfaces DirectX retornadas.

### A Solução Adotada: Stubs Manuais C++ com `GetProcAddress`
- Cada função da interface pública da `dxgi.dll` é implementada explicitamente em C++ com `DR2HOOK_API` (`extern "C" __declspec(dllexport)`).
- Os stubs consultam ponteiros de função dinamicamente obtidos via `GetProcAddress` a partir da instância genuína de `dxgi.dll` (carregada explicitamente de `System32`).
- As invocações são registradas em nível `DEBUG` no arquivo `dr2hook.log`.
- Se o ponteiro genuíno não estiver disponível (ex: falha de carregamento ou chamada prematura), o stub retorna `DXGI_ERROR_UNSUPPORTED` e registra o incidente em nível `ERROR`, garantindo tolerância a falhas sem causar *access violation* / crash fatal.

---

## 3. Prevenção de Deadlock de Loader Lock e Ciclo de Vida

O carregador dinâmico do Windows serializa chamadas a `DllMain` através de uma seção crítica global conhecida como **Loader Lock**.
Invocação de I/O em disco, chamadas a `LoadLibrary` ou sincronização complexa dentro de `DLL_PROCESS_ATTACH` em `DllMain` geram alto risco de deadlock mútuo.

### Ciclo de Inicialização Desacoplada
1. **Entrada em `DllMain` (`DLL_PROCESS_ATTACH`):**
   - Invoca `DisableThreadLibraryCalls(hModule)` para suprimir notificações supérfluas de threads.
   - Cria imediatamente uma thread trabalhadora dedicada via `CreateThread(nullptr, 0, DR2Hook_InitThread, hModule, 0, nullptr)`.
   - Retorna imediatamente `TRUE`, liberando o Loader Lock sem nenhum bloqueio.
2. **Execução de `DR2Hook_InitThread`:**
   - Fora do Loader Lock, inicializa o subsistema de log thread-safe em `dr2hook.log`.
   - Consulta o caminho oficial do sistema via `GetSystemDirectoryA` (ex: `C:\Windows\System32`).
   - Carrega com segurança `C:\Windows\System32\dxgi.dll` via `LoadLibraryA`.
   - Resolve os endereços dos 5 exports essenciais com `GetProcAddress`.
3. **Finalização (`DLL_PROCESS_DETACH`):**
   - Invoca `ShutdownProxy()` para liberar a biblioteca do sistema via `FreeLibrary`.
   - Encerra o sistema de log via `Logger::Shutdown()`.

---

## 4. Tabela de Exports Essenciais Mapeados

| Export / Símbolo | Assinatura C++ | Versão DXGI | Comportamento no Proxy | Fallback / Tratamento de Erro |
| :--- | :--- | :---: | :--- | :--- |
| `CreateDXGIFactory` | `HRESULT WINAPI (REFIID riid, void** ppFactory)` | DXGI 1.0 | Repassa para ponteiro genuíno de `System32\dxgi.dll` com log DEBUG. | Retorna `DXGI_ERROR_UNSUPPORTED` e loga ERROR se ponteiro nulo. |
| `CreateDXGIFactory1` | `HRESULT WINAPI (REFIID riid, void** ppFactory)` | DXGI 1.1 | Repassa para ponteiro genuíno de `System32\dxgi.dll` com log DEBUG. | Retorna `DXGI_ERROR_UNSUPPORTED` e loga ERROR se ponteiro nulo. |
| `CreateDXGIFactory2` | `HRESULT WINAPI (UINT Flags, REFIID riid, void** ppFactory)` | DXGI 1.3 | Repassa para ponteiro genuíno de `System32\dxgi.dll` com log DEBUG. | Retorna `DXGI_ERROR_UNSUPPORTED` e loga ERROR se ponteiro nulo. |
| `DXGIGetDebugInterface1` | `HRESULT WINAPI (UINT Flags, REFIID riid, void** pDebug)` | DXGI 1.3 | Repassa para ponteiro genuíno de `System32\dxgi.dll` com log DEBUG. | Retorna `DXGI_ERROR_UNSUPPORTED` e loga ERROR se ponteiro nulo. |
| `DXGIDeclareAdapterRemovalSupport` | `HRESULT WINAPI ()` | DXGI 1.6 | Repassa para ponteiro genuíno de `System32\dxgi.dll` com log DEBUG. | Retorna `DXGI_ERROR_UNSUPPORTED` e loga ERROR se ponteiro nulo. |

---

## 5. Garantias de Compatibilidade de ABI

- No Windows x64, a convenção de chamada é unificada (`__fastcall` subjacente). A especificação `WINAPI` (`__stdcall`) é compatível e as funções exportadas com `extern "C" __declspec(dllexport)` não sofrem name mangling (decoração de nomes) em compiladores padrão MSVC e Clang, garantindo compatibilidade binária idêntica à `dxgi.dll` de fábrica da Microsoft.
