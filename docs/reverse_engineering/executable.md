# Executable

## Overview

Alvo do mapeamento: `dirtrally2.exe` (Steam / Codemasters). A engine gráfica e física citada no material é EGO Engine 4.x / Codemasters neon architecture.

## Known Structures

O layout de sessão e o `DynamicsCarImpl` não estão nas seções PE. Estão no heap, alcançados pelos ponteiros em `.data`. A cadeia está em [PhysicsRig](physics_rig.md). Os descritores RTTI ficam em `.rdata` e a hierarquia lida está no mesmo documento.

## Memory Layout

- **Arquitetura:** x86-64 (AMD64), little-endian.
- **Base de imagem PE padrão:** `0x140000000`.
- **Compilador original:** Microsoft Visual C++ (MSVC x64), com otimizações de link-time code generation (`/LTCG`) e vetorização SIMD (AVX / SSE2).

| Seção | Endereço virtual relativo (RVA) | Permissões | Conteúdo e propósito |
| :--- | :--- | :--- | :--- |
| `.text` | `+0x00001000` | `RX` (leitura / execução) | Código de máquina do motor, rotinas de física e sub-rotinas de tick. |
| `.rdata` | `+0x00A00000` | `R` (somente leitura) | Tabelas de métodos virtuais (`vtables`), descritores RTTI MSVC, constantes de ponto flutuante e strings estáticas de depuração. |
| `.data` | `+0x01600000` | `RW` (leitura / escrita) | Tabela global de subsistemas da engine, ponteiros singletons e estados mutáveis de sessão. |
| `_RDATA` | Variável | `R` (somente leitura) | Metadados suplementares da CRT e tabelas de inicialização de exceção (`pdata` / `xdata`). |

Endereços de instrução no restante da documentação são endereços virtuais com essa base, no formato `0x140......`.

## Functions

Não há, neste material, um catálogo de funções do executável fora dos sítios citados em cada subsistema.

## Confirmed

A base `0x140000000`, as quatro seções acima e o uso de MSVC x64 com `/LTCG` e SIMD estão declarados como o alvo analisado.

## Unknown

O RVA exato de `_RDATA` está marcado como variável. Não há, neste material, tamanho de imagem, timestamp do PE nem hash do binário.

## Related Systems

- [PhysicsRig](physics_rig.md) — RTTI em `.rdata` e ponteiros de sessão em `.data`
- [Network Guard](network_guard.md) — hooks em `ws2_32.dll`, não no `.text` do jogo
