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

## Reinício da especial sem rede

CONFIRMADO no jogo em 2026-10-02 (Finlândia, reinício pela pausa). Correção em `src/core/net_dialog.cpp` (core, recarrega com F8).

Sintomas antes da correção: ao reiniciar, às vezes aparecia "FALHA DE CONEXÃO / O servidor está indisponível / Código de erro: …" (só OK; o carregamento ficava parado até o OK) e, sempre, "CONTATANDO SERVIDOR / Aguarde enquanto sua entrada é processada". Do `restart_race` à largada iam ~5 s; depois, ~2,7 s.

### De onde vem

O fluxo do reinício passa por `StateEgoNetSignIn` (login na RaceNet) e por `StateNetWaitForNetwork`. O NetworkGuard recusa o DNS e o login falha.

| Endereço | Função | Comportamento |
| :--- | :--- | :--- |
| `0x14061d260` | `StateEgoNetSignIn` OnEnter (vtable `0x141295328`, slot 3) | Abre o diálogo código 6 = `net_egonet_signin_wait_msg` ("CONTATANDO SERVIDOR") via `0x14035c3c0`, se o byte online estiver ligado. Não olha `noerrors`. |
| `0x140623010` | `StateEgoNetSignIn` Update (slot 14) | Com o byte online desligado, sai direto pelo link `failed`. Ligado, tenta a rede; na falha, sem `noerrors` (byte `+0x3c`), monta o token e abre `net_egonet_error_generic_error_msg` (código 46) via `0x14035c790`. Estado do login em `+0x38` (4 = falhou). |
| `0x1406253a0` | `StateNetWaitForNetwork` Update (vtable `0x141296918`, slot 14) | Se `[mgr+0x3296]` estiver ligado, segura o fluxo até 5000 ms (`double` em `0x141292818`) antes de sair por `next` (hash global em `0x1415b9458`). |
| `0x141695200` → `+0x1b52` | Byte "online" | Lido pelo OnEnter e pelo Update do login para decidir se tenta a rede. |
| `0x140661900` | Código → id de mensagem | `switch` de 1 a 0x69 (tabela em `0x140661e1c`). 6 → `net_egonet_signin_wait_msg`, 0x2e → `net_egonet_error_generic_error_msg`. |
| `0x140138e10` | Monta o token `EGONET_ERROR` ("Código de erro") | Chamada antes de cada `net_egonet_error_generic_error_msg` (13 lugares) e também pelo `TaskDisplayDialog` OnEnter (`0x140382990`) para qualquer diálogo, então não identifica sozinha a origem. |

Fábricas: o construtor fica logo antes do nome do estado no bloco de registro (`0x1406282d0` → `StateEgoNetSignIn`, `0x140629fe0` → `StateNetWaitForNetwork`, `0x140385d60` → `TaskDisplayDialog`).

### Correção

- OnEnter e Update do login rodam com o byte online zerado e restaurado logo depois (`OfflineScope`). O login sai por `failed` sem aviso e sem popup, o mesmo destino do OK no popup.
- Depois do Update original do `StateNetWaitForNetwork`, o link vira `next`: sem espera.
- Tentativa descartada: ligar `noerrors` (+0x3c) só tirava o popup de erro; o "CONTATANDO SERVIDOR" vem do OnEnter, que ignora essa flag.

Os textos ficam em `language/language_bra.lng`; os diálogos, em `frontend/message_dialogs/network.xml` (`game_1.dat`, ver [ui_data.md](ui_data.md)).

## Related Systems

- [Telemetry](telemetry.md) — `sendto` no fim do tick; o guard não o intercepta
- [README](README.md) — o `F8` recarrega `dr2hook_core.dll` e não estes hooks
