# Passagem de bastão — INV-01 (fonte de verdade do estado do carro)

Atualizado em 2026-10-01 07:30 (UTC-3), pelo RE Orchestrator. Este arquivo é o ponto de entrada. Onde ele divergir dos arquivos em `kb/`, vale este (o KB foi atualizado pela última vez em 2026-09-30 12:18 e não incorpora as decisões posteriores listadas abaixo).

## Regras de trabalho
- Classifique toda afirmação como CONFIRMED / PROBABLE / HYPOTHESIS / UNKNOWN / REFUTED. Concordância entre agentes não é evidência independente.
- Prioridade de evidência: escritores > leitores > fluxo de dados > callers/callees > constantes > comportamento em runtime > experimentos controlados > nomes/símbolos/strings.
- Prefira tentar refutar a confirmar. Não reinvestigue o que está CONFIRMED sem evidência nova.
- Escritas em memória do jogo: só em modo offline (estágio local, DR2Hook bloqueando servidores), checagem de sockets + log do NetworkGuard antes de cada lote, escrita mínima e reversível, valores finitos, deltas pequenos (ex.: +0,05 m).

## Ambiente
- Build do jogo: `dirtrally2.exe` SHA-256 c119f509…3442, PE timestamp 0x605cbae3, base 0x140000000 (sem relocação). Caminho: `/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/`. Roda via Proton.
- `.text` em memória = disco, exceto os 5 detours do DR2Hook (F-011, CONFIRMED).
- Repo: `dsnsilvf/DR2ModLoader`, branch `main` (decisão do usuário: todo o progresso consolidado na `main`).

## CONFIRMED (um carro, um local, uma sessão)
- F-012 Bloco de estado de origem no rig: `+0x2b0` v, `+0x2c0` ω, `+0x2d0` pos, `+0x2e0` quat. Integrado por `0x140746150` (Euler semi-implícito, dt = float32(1/60)), reproduzido bit a bit em 8735 ticks. Assinatura: rcx = S, xmm1 = dt.
- F-013 `+0x320/+0x330` e `+0x170/+0x180` são cópias com atraso de 1 tick de v/ω.
- `+0x200/+0x210` são cópia lag-0 de v/ω escrita pelo Commit (estático + duas fontes de runtime).
- F-014 Tick fixo de 60 Hz; índice do ring em `+0xdc` avança 1 por tick.
- Prólogos em disco (12 bytes), lidos de forma independente por dois agentes:
  - tick_start 0x14074b8f0: 488bc4488958184889702055
  - integrator 0x140746150: 488bc44889581055488da838
  - commit 0x14074d190: 488bc4488958184889702055
  - post_physics_task 0x140dbca20: 488bc4574881ecb000000033
  - physics_step 0x140dbc500: 488bc44889501041554883ec
  - pretick 0x140749a30: 488bc4488958104889781855
  - end_step 0x1407511e0: 40534883ec50488b05b3b2e6 (bytes 6-11 RIP-relativos)
  - 0x14073a070: 4c8bdc55535741554157498d
  - 0x14073b620: 488bc45657415641574881ec

## Ordem do tick (estático, PROBABLE onde não indicado)
- step `0x140dbc500` → PreTick `0x140749a30` (puxa estado do proxy, sobrescreve inércia) → gate `0x140dbc430` → update → PostTick → EndStep `0x1407511e0` (chama Commit `0x14074d190` todo tick).
- Tarefa `0x140dbca20` (PostPhysicsTask, por container) empurra o estado para o proxy em `[[con+0x840]+8]`.
- Pontos de escrita: **L1 = retorno de `0x140dbca20`** (o verdadeiro "entre ticks"). L0 = entrada de `0x140dbca20`. **B2 (entrada de `0x14074b8f0`) NÃO é L1**: ali o tick já rodou F.1/F.2, PreTick, gate e timers, então B2 = fim de L2.
- Integrador também é chamado na look-ahead (retorno `0x14073e314`, H6) e na integração real (retorno `0x1407395fa`, M2).

## API nativa de estado (PROBABLE quanto à semântica)
Reset `0x14074a110`, SetTransform `0x14074ad80`, SetLinVel `0x14074a910`, SetAngVel `0x14074a890`, Commit `0x14074d190`, GetSpawnState `0x14074b070`, SetFullState `0x14074a5e0`.

## PROBABLE / HYPOTHESIS
- Escrita em L1 sobrevive (PROBABLE, não testada). Receita de restauração: escrever o bloco de origem (PROBABLE).
- `rig+0x290` = lerp(`+0x170`, `+0x200`, alpha) por tick (PROBABLE; bate em 1732 ticks dentro do arredondamento; EMA desfavorecida). Teste I6 verifica bit a bit.
- `con+0xc930` = (`+0x200` − `+0x170`)/dt, aceleração calculada após o Commit (PROBABLE). Discriminador: escrita de v em L6/L7 deve gerar pico; em L1/L2 não. Leitor desconhecido.
- `+0x1338` avança 2·dt por tick, via dois escritores: `0x14074ddb1` e `0x14074bb28`.
- `0x14073b620` (chamado de `0x1409cdbb7`) é provável segundo escritor de `+0x1690`, possível explicação para H-025 falhar.

## REFUTED
- H-002 `+0x320` como velocidade. H-011 `con+0xcd0` como âncora visual.
- `0x14073e266/e3ed` como snapshot nativo (é integração de teste da look-ahead).
- Leitura anti-tamper da cauda do Commit. `+0x1338` avançando dt (é 2·dt).
- "L1 = B2".

## UNKNOWN / em aberto
- Consumidor do evento reset_vehicle / handler de reset: não encontrado.
- F.1 `0x140dadc00`, F.2 `0x140dbe300`, F.4-F.6 e `0x140db2220` não foram checados quanto a escritas no estado do carro, então a sobrevivência em L1/L6/L7 não está confirmada.
- "Pull desligado" se apoia em uma amostra (R7 verifica).
- Sinal real de modo offline/online (INV-02 proposto).

## Estado do harness de instrumentação
- Código em `src/core/physics_tick_harness.cpp` + `physics_harness_detour_x64.S`, dentro do `dr2hook_core.dll`. Desligado por padrão; ativa com `DR2HOOK_PHYSICS_HARNESS=1` ou `dr2hook_physics_harness.ini`. Docs: `docs/physics_tick_harness.md`.
- PR #1 (mergeado) tinha bytes de prólogo errados e bug de ABI (xmm1/dt destruído). Corrigido no PR #3, mergeado em `main` como `6b03139` após PASS do Validation Specialist (objdump, .pdata/.xdata, teste no Wine com callbacks que destroem registradores voláteis e fazem syscalls).
- PR #4 (draft, `b1ee5a7`): ajustes não bloqueantes do thunk (remove restore errado de r13, epílogo `add rsp,0xC0`, renomeia frame_loop → post_physics_task). **Não mergear antes do G3 e de revisão igual à do PR #3**, porque mexe no thunk.
- Pendências conhecidas: o sham-write só registra em log (não prova nada); ~19 VirtualQuery por linha de CSV (medir custo no G3); descarregar com F8 pode liberar o thunk com threads dentro, então encerre o G3 saindo do jogo.

## Próximo passo: gate G3 (autoteste, sem escrita) — AINDA NÃO EXECUTADO
Em 2026-10-01 o checkout local do usuário ainda estava em `a35baa9` (antes das correções).
1. `git pull --ff-only origin main` (deve chegar a `6b03139` ou depois).
2. Build MinGW em `build-g3` (manter `build-release` como fallback). Rodar `scripts/verify_physics_harness_dll.sh` no build do usuário (GCC 16).
3. Usuário sai do jogo. Backup de `dxgi.dll` e `dr2hook_core.dll` com hashes; instalar via tmp+mv.
4. ini contendo só `self_test=1`.
5. Estágio offline por ~20 s (≥ 600 ticks em estágio). Rechecar G1/G2 (offline e build).
6. Coletar `dr2hook.log`, `dr2hook_physics_harness_self_test.log` e o CSV. Remover o ini. Sair do jogo. Verificar que os 9 sites voltaram a bater com o disco.
- PASS: 4 hooks obrigatórios `installed=1`, sem `prologue mismatch`, ≥ 600 ticks, nenhuma linha `write tick=`, sem NaN/teleporte/crash.
- Abortar (sair do jogo) em: NaN/teleporte/explosão, crash, prologue mismatch em hook obrigatório, ou qualquer linha `write tick=`.
- Rollback: restaurar as duas DLLs.

## Depois do G3
- Experimentos de escrita de `validation/INV-01-experiment-plan.md` (rev 2, CSV de 80 colunas): escrita bruta em L1 vs API nativa, depois L0/L2/L4/L6/L7, teste de inércia E-B6, I6 (lerp do 0x290).
- Gates de escrita: G3 PASS; PROBABLE-offline aceito pelo orquestrador (atestado do usuário + 27 sockets UDP não conectados, sem hook em sendto); F5-F7 do Practice Mode registrados por tick (o usuário confirmou que não apertou durante as capturas).

## Outras recomendações em aberto (análise inicial do projeto)
- P0 segurança: SafetyGuard permissivo (`core_module.cpp:101`), `ApplyState` escrevendo via `[rig+0]`/`+0xcd0`, NetworkGuard que falha aberto.
- Docs prometem coisas que não existem (suspensão "settled", `getVehicleTelemetry`, `onStageStart`).
- Sem verificação de versão do jogo (adicionar checagem de hash/PE + assinaturas AOB). Sandbox Lua fraco.

## Arquivos nesta pasta
- `kb/`: base de conhecimento graduada (confirmed-facts, hypotheses, open-questions, glossary, history).
- `analyst/`: relatório estático e adendo (o adendo contém o erro "L1 = B2", já refutado).
- `captures/`: relatório de runtime rev 2, info do build, hashes do .text, scripts de captura.
- `validation/`: plano de experimentos rev 2, template CSV, gates, fase 1a, preparação do G3, revisões do PR #3.
- `orchestrator/`: inventário v1, briefings e spec do harness.
As capturas brutas (~560 MB) ficaram fora do repo; estão no computador do assistente em `/workspace/captures/INV-01/raw/`.
