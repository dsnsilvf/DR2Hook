# Network Guard

## Overview

Para o mod loader operar sem alcançar tabelas competitivas (RaceNet), o subsistema `NetworkGuard` atua na camada de transporte do sistema operacional, com hooks MinHook em `ws2_32.dll`.

Os detours ficam em `src/core/hooks.cpp`, dentro da `dxgi.dll`. Não acompanham o reload de `dr2hook_core.dll`.

## Functions

| Endereço / símbolo | Função | Relevância |
| :--- | :--- | :--- |
| `getaddrinfo`, `GetAddrInfoW` | Resolução de nomes | Só `localhost`, `127.0.0.1` e `::1` seguem para o sistema. Qualquer outro nome retorna `EAI_NONAME`. O nome recusado é escrito em `dr2hook.log`. |
| `connect` | `int WSAAPI HookedConnect(SOCKET s, const sockaddr *name, int namelen)` | Só `127.0.0.0/8` e o loopback IPv6 passam. O resto recebe `WSASetLastError(WSAECONNREFUSED)` (`10061`) e `SOCKET_ERROR`. O endereço de destino não é reescrito. |
| `sendto` | Datagrama UDP | Não é interceptado. |

## Data Flow

```text
nome ou sockaddr de destino
        │
        ▼
getaddrinfo / GetAddrInfoW / connect
        │
        ├── localhost, 127.0.0.1, ::1, 127.0.0.0/8, loopback IPv6
        │         └── segue para o sistema
        │
        └── qualquer outro destino
                  └── EAI_NONAME, ou WSAECONNREFUSED (10061) + SOCKET_ERROR
```

`sendto` fica fora desse filtro.

## Confirmed

O bloqueio de ranking descrito no material é o `connect` e a resolução de nomes, não um filtro de datagrama. A telemetria UDP local continua podendo sair pela loopback. O formato desse pacote está em [Telemetry](telemetry.md).

## Related Systems

- [Telemetry](telemetry.md) — `sendto` no fim do tick; o guard não o intercepta
- [README](README.md) — o `F8` recarrega `dr2hook_core.dll` e não estes hooks
