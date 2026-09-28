# Registros de Decisões de Arquitetura (ADR): Safety Gate e Savestate Nativo

## Status
- **ADR-003:** Aceito / Homologado (2026-09-12)
- **ADR-004:** Aceito / Homologado (2026-09-12)

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
