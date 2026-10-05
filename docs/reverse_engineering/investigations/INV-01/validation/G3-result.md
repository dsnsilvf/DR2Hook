# INV-01: Gate G3 Result (Runtime Self-Test — READ-ONLY)

- **Data / Hora:** 2026-10-01, 07:47-07:48 BRT (UTC-3)
- **Máquina:** CachyOS Linux (Kernel x86_64), Proton Experimental
- **Build do Alvo:** `dirtrally2.exe` SHA-256 `c119f509...5d73442` (PE timestamp `0x605cbae3`, base `0x140000000`)
- **DLLs DR2Hook:** `dxgi.dll` (`dd79f234...`), `dr2hook_core.dll` (`4f3f0a22...`)
- **Configuração:** `dr2hook_physics_harness.ini` com `self_test=1` (modo somente-leitura forçado, writes=0)
- **Veredito:** **PASS**

---

## 1. Critérios de Avaliação e Resultados

| Critério | Especificação | Resultado Real | Status |
| :--- | :--- | :--- | :--- |
| **G3.1 Prólogos de Hooks** | 9 sites comparados contra disco | 0 mismatches, 9/9 bateram exatamente | **PASS** |
| **G3.2 Instalação de Hooks** | 4 obrigatórios + 5 auxiliares | 9/9 com `installed=1` | **PASS** |
| **G3.3 Integridade da Física (ABI)** | Sem quebra de `xmm1` ($dt$), sem teleporte, sem NaN | 0 NaN, 0 Inf, física estável | **PASS** |
| **G3.4 Duração da Amostragem** | $\ge 600$ ticks in-stage | **600** in-stage ticks (9.285 ticks totais na sessão) | **PASS** |
| **G3.5 Reentrância** | 0 sobreposições de execução de boundary | **reentrancy = 0** | **PASS** |
| **G3.6 Escritas Reais em Memória** | Bloqueadas em modo self-test | **writes = 0** | **PASS** |
| **G3.7 Sham-Writes Sintéticos** | Validação da fila em boundaries | **2.985** sham-writes registrados | **PASS** |
| **G3.8 Confounding Keys** | F5/F6/F7 desligados durante coleta | **0** teclas pressionadas em 168.312 linhas | **PASS** |
| **G3.9 Encerramento do Jogo** | Fechamento limpo sem crash | Exit code 0, overlay finalizado com sucesso | **PASS** |

---

## 2. Inventário de Hooks e Amostragens de Boundaries

Extraído diretamente de `dr2hook_physics_harness_self_test.log`:

```ini
phase=relatorio final
writes=0
sham_write=1
in_stage_ticks=600
pass_tick_threshold=600
reentrancy=0
sham_writes=2985
result=PASS
hook,B2 (tick_start),1
hook,M2 (0x1407395fa) / H6 (0x14073e314),1
hook,commit,1
hook,post_physics_task,1
hook,physics_step,1
hook,pretick,1
hook,end_step,1
hook,commit_log_73a070,1
hook,commit_log_73b620,1
boundary,B2,602
boundary,M1,599
boundary,M2,599
boundary,M3,728
boundary,B3,728
boundary,H1,41
boundary,H2,41
boundary,H3_ENTRY,42
boundary,H3_RETURN,41
boundary,H4_ENTRY,602
boundary,H4_RETURN,602
boundary,H5_ENTRY,123
boundary,H5_RETURN,123
boundary,H6,0
boundary,LOG_COMMIT_74D190,728
boundary,LOG_COMMIT_73A070,601
boundary,LOG_COMMIT_73B620,0
```

---

## 3. CSV de Telemetria (`dr2hook_physics_tick_harness_20261001_074736.csv`)

- **Tamanho:** 75.984.424 bytes
- **Linhas coletadas:** 168.313 (1 header + 168.312 amostras)
- **Threads registradas:**
  - Thread 368 (física primária): 133.844 amostras
  - Threads auxiliares (500, 504, 508, 512, 516, 520): ~5.000 a 6.000 amostras cada
- **Tick máximo:** 9.285 (~154,7 segundos de execução)

---

## 4. Conclusão

O harness de instrumentação `physics_tick_harness` está **aprovado e homologado para a Fase de Experimentos de Escrita** (escrita em L1 vs API nativa do jogo).
O arquivo temporário de ativação `dr2hook_physics_harness.ini` foi removido após o teste.
