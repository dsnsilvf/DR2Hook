# Registros de Decisões de Arquitetura (ADR): DR2 ModLoader v0.1.0

## Status
- **ADR-003:** Aceito / Homologado (2026-09-12) — Princípio Fail-Closed e Reavaliação Contínua no Safety Gate
- **ADR-004:** Aceito / Homologado (2026-09-12) — Normalização para Repouso Estático da Suspensão e Atenuação Inercial Deliberada
- **ADR-005:** Aceito / Homologado (2026-09-28) — Isolamento de Rede Obrigatório via Winsock Hooks (Air-Gap Anti-Cheat)
- **ADR-006:** Aceito / Homologado (2026-09-28) — Restauração de Checkpoint em Modo Duplo (Normal vs. Com Momentum)

---

## ADR-003: Princípio Fail-Closed e Reavaliação Contínua no Safety Gate

### Contexto
O DiRT Rally 2.0 possui modos de jogo competitivos online (Daily Challenges, Weekly Challenges, Clubs, Carreira Online / RaceNet) integrados a rankings globais com premiações no jogo e prestígio comunitário. O objetivo central do DR2Hook é fornecer uma ferramenta de treino, depuração e extensão comunitária estritamente ética ("Fair Play"), sem violar a integridade competitiva do jogo.

Se um usuário tentar acionar rotinas de escrita em memória (como o savestate via `F6` ou futuras APIs de teleporte / reset de danos) durante um evento oficial online, a modificação de memória deve ser bloqueada de forma inegociável.

Além disso, problemas comuns em modding incluem:
1. Atualizações do jogo (*patches*) que alteram endereços base ou quebram cadeias de ponteiros (*pointer chains*).
2. Timeouts ou leituras corrompidas de assinaturas de bytes (*AOB / Pattern Scanning*).
3. Transições dinâmicas de sessão onde o jogador passa de uma pista de treino livre (*DirtFish*) diretamente para um evento oficial online sem reiniciar o jogo.

### Decisão
1. **Implementação Nativa no Core (C++):** A verificação de segurança reside no subsistema de mais baixo nível (`SafetyGuard`), garantindo que scripts Lua (Fase 4) ou atalhos nativos não possam contornar as restrições.
2. **Princípio Fail-Closed Estrito:** Qualquer incerteza no ambiente — leitura de memória falha, endereço não mapeado, ponteiro nulo, timeout ou valor desconhecido de modo de jogo (`GameSessionMode::Unknown`) — resulta obrigatoriamente em bloqueio de escrita (`CanWriteState() == false`). Não são permitidos valores padrão permissivos nem exceções não tratadas.
3. **Reavaliação Contínua sem Cache:** `SafetyGuard::CanWriteState()` reavalia o endereço de sessão a cada solicitação de escrita. Não existe cache de sessão; transições entre estados de jogo são detectadas imediatamente na tentativa de escrita.
4. **Modos Homologados:** Somente modos comprovadamente desconectados e de treino livre (`DirtFish`, `TimeTrialOffline`, `CustomOffline`) recebem autorização de escrita (`CanWriteState() == true`).

### Consequências
- **Positivas:**
  - Impossibilidade de exploração do savestate em tabelas de classificação globais (anti-cheat inegociável).
  - Resiliência estrutural a patches do jogo: se uma atualização do jogo quebrar os offsets, o mod entra em modo seguro (bloqueio de escrita) em vez de corromper memória ou causar falhas catastróficas (*crashes*).
- **Trade-offs:**
  - Custo de leitura de memória a cada requisição de restore (insignificante: 1 leitura de 4 bytes por acionamento de tecla).

---

## ADR-004: Normalização para Repouso Estático da Suspensão (Sag) e Atenuação Inercial Deliberada

### Contexto
Ao restaurar o estado cinemático do veículo (`CarState`) via teleporte ou checkpoint (`F6`), restaurar cegamente os valores brutos capturados pelo frame anterior pode causar instabilidades extremas no solucionador de física da EGO Engine:
1. **Colapso de Contato e Ejeção da Pista:** Se o veículo for restaurado com extensão total de suspensão (`suspensionCompression = 0.0f` / airborne) ou suspensão totalmente comprimida (`1.0f`), o impacto da carroceria com o solo no primeiro frame causa penetração de malha geométrica, força de reação de mola hiperbólica e ejeção violenta do carro (*bounce bug*).
2. **Divergência Inercial:** Se o savestate foi registrado com alta velocidade angular residual (ex.: meio de um capotamento ou derrapagem violenta), reaplicar essa rotação instantaneamente em uma nova orientação ou pequena variação no terreno frequentemente desestabiliza o solucionador de restrições rígidas (*constraint solver*), induzindo oscilações infinitas ou física corrompida.

### Decisão
1. **Sag Estático de Suspensão:** A rotina de restauração (`Player::ApplyState`) normaliza sistematicamente a compressão de suspensão de todas as 4 rodas para o ponto de equilíbrio de repouso sob gravidade e peso próprio:
   $$\text{SUSPENSION\_STATIC\_SAG\_RATIO} = 0.35f$$
   Simultaneamente, os estados de contato das rodas com a superfície são ativados (`inContact = true`).
2. **Amortecimento Inercial Deliberado:** A velocidade angular residual do chassi é conscientemente zerada no instante da restauração:
   $$\text{angularVelocity} = \{0.0f, 0.0f, 0.0f\}$$
   A velocidade linear e a matriz de rotação do veículo são preservadas para manter a direção e velocidade para a frente.

### Consequências
- **Positivas:**
  - Aterrissagem e estabilização imediata e suave no primeiro frame pós-restore.
  - Eliminação de solavancos, espasmos e ejeções da pista ao usar o savestate em qualquer terreno.
  - Alta repetibilidade para treinos consistentes de trechos e curvas.
- **Trade-offs:**
  - Sacrifício intencional da continuidade exata de derrapagens angulares extremas registradas no meio de uma curva: o carro retoma a linha com a velocidade vetorial linear, porém com rotação angular estabilizada. Esta decisão foi formalmente aceita como o compromisso ideal entre realismo e estabilidade determinística da engine.

---

## ADR-005: Isolamento de Rede Obrigatório via Winsock Hooks (Air-Gap Anti-Cheat)

### Contexto
Mesmo com o `SafetyGuard` bloqueando escritas de memória em modos competitivos identificados por ponteiro de sessão, confiar exclusivamente em heurísticas de memória de sessão para impedir trapaças online introduz um ponto único de falha: se um patch do jogo alterar a estrutura da sessão ou um jogador conseguir forçar a transição de flags, funções de modificação poderiam teoricamente ser acionadas em tabelas de classificação online (RaceNet).

Para eliminar categoricamente qualquer risco de uso do DR2 ModLoader para trapaças em eventos oficiais, implementou-se uma política de isolamento físico de rede (*air-gap* forçado).

### Decisão
1. **Detours no Winsock (`ws2_32.dll`):** O `NetworkGuard` instala hooks determinísticos em funções de transporte e resolução de nomes do Windows Sockets:
   - `getaddrinfo`: Intercepta consultas DNS e bloqueia resoluções de domínios da Codemasters/EA/RaceNet (ex.: `*.codemasters.com`, `*.dirtgame.com`, `racenet.com`), retornando `WSAHOST_NOT_FOUND` / `EAI_NONAME`.
   - `connect`: Intercepta tentativas de conexão TCP e recusa imediatamente qualquer conexão destinada a servidores remotos do jogo com `WSAECONNREFUSED` (`10061`).
   - `sendto`: Intercepta tráfego UDP de telemetria ou rede para servidores remotos, descartando pacotes não autorizados com `WSAEACCES` (`10013`).
2. **Mandatório e Irreversível em Runtime:** O isolamento de rede é ativado na inicialização do mod loader e não pode ser desativado pelo usuário via interface ou scripts durante a execução do processo. A única forma de jogar online novamente é fechar o jogo e remover a DLL `dxgi.dll`.

### Consequências
- **Positivas:**
  - Impossibilidade matemática de envio de tempos ilegítimos ou estados adulterados para os servidores de ranking mundial da Codemasters/EA.
  - Segurança irrestrita para o usuário treinar no DirtFish e Time Trial sem risco de banimentos na conta Steam/RaceNet.
- **Trade-offs:**
  - Recursos online legítimos do jogo ficam desabilitados enquanto a DLL estiver carregada, o que é o comportamento desejado por especificação.

---

## ADR-006: Restauração de Checkpoint em Modo Duplo (Normal vs. Com Momentum)

### Contexto
Durante o desenvolvimento do mod de treino (Practice Mode), identificou-se que diferentes cenários de pilotagem exigem abordagens distintas para o retorno ao checkpoint:
1. **Treino de largada ou posicionamento estacionário:** O piloto deseja apenas posicionar o carro em um ponto fixo da especial sem carregar a velocidade anterior.
2. **Treino de curvas de alta velocidade e saltos:** O piloto precisa repetir uma tomada de curva de 120 km/h com o veículo já embalado na inércia, rotação angular e vetor de velocidade exatos em que a gravação foi feita.

### Decisão
O método `Player::ApplyState` e a API Lua `Player.setState(state, mode)` foram estendidos para suportar dois modos operacionais:
1. **Modo Normal (`RestoreMode::Normal` / `"normal"`):**
   - Restaura as coordenadas espaciais $X, Y, Z$ (`PhysicsRig + 0x2d0`), quatérnion de rotação (`+0x2e0`), matriz de orientação (`+0x2f0 - +0x310`) e âncora visual (`container + 0xcd0`).
   - Zera as velocidades lineares (`+0x320`) e angulares (`+0x330`).
   - Aplica suspensão estática padronizada (sag 0.35) nas 4 rodas para garantir que o carro fique estável e em repouso.
2. **Modo Com Momentum (`RestoreMode::WithMomentum` / `"momentum"`):**
   - Restaura as coordenadas, quatérnion e matriz de orientação.
   - Restaura o vetor completo de **velocidade linear** $\vec{v} = (v_x, v_y, v_z)$.
   - Restaura o vetor de **velocidade angular** $\vec{\omega} = (\omega_x, \omega_y, \omega_z)$.
   - Preserva o amortecimento e curso das suspensões do instante gravado, permitindo continuidade imediata da dinâmica veicular.

### Consequências
- **Positivas:**
  - Flexibilidade máxima para o piloto: treino de curvas rápidas sem perda de dinâmica e treino de largadas/linhas paradas.
  - Controle integrado via interface gráfica ImGui (aba Practice Mode) e via script Lua.

