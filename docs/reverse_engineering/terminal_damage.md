# Dano terminal (carro destruído)

Material: relatórios do Grok em `captures/grok/terminal_damage/` (`REPORT.md` e `r2`–`r5`, cada um com o seu `PROMPT`) e testes ao vivo de 2026-10-03 e 2026-10-04 com `src/core/terminal_damage.cpp`. Endereços são VAs com ImageBase `0x140000000`.

Resumo do estado (2026-10-04):

| Parte | Estado |
| :--- | :--- |
| Insta crash (F11) | CONFIRMADO no jogo |
| Esc abre o menu de pausa durante o dano | CONFIRMADO no jogo |
| Reiniciar aparece e reinicia a especial | CONFIRMADO no jogo (o rótulo do item saiu vazio; ver abaixo) |
| Som e direção depois do Reiniciar | Quebrados. Gate e FFB já nascem normais nesse instante. O candidato que sobra é o byte `veículo+0x368` (ver "Som do motor") |

## Insta crash (F11)

CONFIRMADO no jogo. O motivo do dano fica em `componente+0x556` (`7` = carro inteiro, `4` = impacto). O componente é `*(*(A+8)+0x2BD0)`, com `A = *(*(controlador+0x28)+0x30)` e controlador de vtable `0x141270ac8`.

| Jeito | Resultado ao vivo |
| :--- | :--- |
| Gravar `+0x556 = 4` direto | O carro para e o jogador perde o controle, mas `TerminalDamageMessage` não é postada. O fluxo não entra nos estados terminais, a especial não reinicia e a pausa continua funcionando. |
| Setter `0x140744460(componente, 4, 0)` com `+0x555 != 0` | Comportamento completo do jogo: keyframes `terminal_notifier`, `slowdown`, `post_race_end`, `terminal_reason`, `fade_to_black`. A pausa fica bloqueada. |

`terminal_damage_force` é float de catálogo e `DEV_CRASH` é canal de log; nenhum dispara o dano.

Tecla: **F11** (F12 é a captura de tela da Steam; F11 era a colisão do fantasma, que ficou sem tecla). Só funciona onde `SafetyGuard::CanWriteState()` deixa (treino offline). Mensagens do toast: `Car destroyed.`, `Car already destroyed.`, `Crash blocked: online event.`, `Crash unavailable: not in a stage.`, `Crash failed: unexpected game state.`

### Achar o controlador

O primeiro desenho dependia do hook no tick `0x1403f7b60`, e o F11 só agia se o tick tivesse rodado nos últimos 500 ms. Depois do dano o tick para, e um segundo F11 dava "not in a stage" (`NoController`) em vez de "já destruído". A causa está na seção "Por que o tick para" abaixo. Hoje `FindPlayerController()` anda a lista do host e escolhe o controlador cujo dono tem vtable `0x14127cc00` e `+0xbc == 0`; o hook só vale se o tick for recente. Cadeia (REPORT4):

```
root = *(uint64*)0x141695150
host = *(uint64*)(*(uint64*)(root+8) + *(int32*)0x141694064 * 8)   ; *host == 0x14126f588 ao vivo (0x14126f440 é a base gravada pelo ctor)
controladores = [host+0x30, host+0x38)
```

O índice `0x141694064` é de `game::component::ObjectManager` (gravado em `0x140056e30`). Mesma chamada para o `neRacing::VehicleSystem` com o índice `0x141693ffc` (gravado em `0x140057350`).

Teste ainda pendente do segundo F11 (esperado: "Car already destroyed.").

## Por que a pausa some

CONFIRMADO ao vivo (sonda da pilha do runner, estados resolvidos por `[registry+0x10]`). Correção do primeiro relatório: a vtable `0x141251500` é **`StateRace`**, não `StatePause` (gravada no construtor `0x140249bc0`, `lea` em `0x140249c05`). O topo da pilha na corrida é `StateRace`, nó `28191942` no contra-relógio, e é ele que publica `TPauseAccess` e liga `+0x59`.

Depois do setter, `StateRace` sai da pilha e o topo passa por estados terminais:

`StateTerminalDamageCinematicDecision` (`0x14125ec40`) → `StateCutscene` (`0x14124b810`, `terminal_damage_start`) → `StateTerminalDamageFreeze` (`0x141254ad0`) → `StateCutscene` de novo (`terminal_damage_end`).

Nenhum deles produz o nome de transição `pause`. O Esc não é um `if` no executável; é uma cadeia:

1. A ação de input se chama `driving.pause` (string `0x141272228`). `0x140d97470(contexto, &ação)` devolve 1 se o nó da ação em `contexto+0x1f8` tem `+0x98` e `+0xa0` diferentes de zero. O mesmo `al` serve para teclado, Start e volante.
2. Só dois estados transformam isso no nome `pause`: `StateRace` (Update `0x1402a98e0`, portão `0x140288ac0`, e o slot `+0x70` = `0x1402815b0` devolve o nome) e `StateOsdCountDown` (Update `0x1402a8fd0`, slot `+0x70` = `0x140280880`).
3. O runner `0x140c7bd30` percorre a pilha inteira, do primeiro ponteiro ao último, chama o slot `vtable+0x70` de cada estado e, se o retorno não é nulo, procura o link com esse nome (`FindLink`) no nó. O laço está em `0x140c7be30`; o `call [r8+0x70]` em `0x140c7be67`.
4. Se algum nó da pilha tem um link com esse nome, o runner empilha o alvo. O menu de pausa da corrida é o nó `60321034` (`StatePauseScreen`, `screen_name=pause_menu`, `state` `314506569`), com `continue` e `back` para `139240567` (`StateOsdCountDownCheck`) e `restart_race` para `190668332` (`StateGameModeRestart`).

O portão do `StateRace` está em `0x140288ac0`: lê `this+0x59` e corta com `this+0x5b != 0` e com o timer `this+0x68`. `StateCutscene` devolve `dont_wait`, um nome enfileirado ou um ponteiro global (slot `+0x70` = `0x14027e660`), e o Update dela consulta `driving.reset_vehicle`, não `driving.pause`.

Os eventos `pause`/`unpause` postados por `0x140b0a4e0` não são o Esc: são publicações de quem já entrou ou saiu da pausa (`hud_pause_on`, `unpause_music`, troca de modo). `pause_multiplayer` só aparece em `0x140272856`.

## O patch de `flow.bin` e o gancho do Esc

Duas peças, as duas obrigatórias. CONFIRMADO no jogo em 2026-10-04: o log mostra o Esc visto pelo core, o gancho devolvendo `pause`, o runner empilhando `st=1250b50 id=314506569` (o menu de pausa) 11 ms depois do pedido, e o menu foi desenhado.

**1. Link `pause` nos nós terminais** (`src/core/ui_patch.cpp`, `PatchTerminalPause`). Para cada nó cujo `state` é `1461673396` (cutscene de início), `2859882945` (freeze) ou `1591963405` (cutscene de fim) e que ainda não tem `pause`, o patch acrescenta `<link id="pause" target=X/>`. `X` é o menu de pausa original da mesma árvore (`state` `314506569` com link `restart_race`) de maior prefixo de caminho em comum com o nó. Por que não um nó novo:

- A proposta da rodada 2 era um nó novo (`223478001`) filho da cutscene de fim. Foi testada em 2026-10-03: o runner empilhou o menu filho mas ele não foi desenhado e o jogo travou. Apontar para o menu que a corrida já usa funciona.
- O alvo não pode ser `StateRace`, `StatePause` (`143411667`) nem o `231066979` do rali: esses nós não são descendentes do overlay `104998759`, e entrar neles troca a cadeia debaixo do `StateIngame` e tira o freeze da pilha.

**2. Gancho no slot `+0x70` do `StateCutscene`** (`0x14027e660`, `DetourPoll`). O link existe, mas ninguém pede `pause`: `StateRace` saiu da pilha. O core vê o `WM_KEYDOWN` de Esc (`Core_OnWndProc` chama `TerminalDamage::RequestPause`), confere que o topo é a cutscene e marca o pedido. No quadro seguinte o slot `+0x70` devolve o ponteiro da string `pause` (`0x14124e000`) em `*out`, e o runner segue o link. O pedido vale 400 ms.

Limites conhecidos:

- Só a cutscene tem o gancho. O freeze quase nunca fica no topo; se ficar, o Esc não faz nada.
- Com o menu aberto o Esc não pede mais nada (o topo deixou de ser a cutscene); fechar fica por conta do jogo.
- **Continuar** vai a `StateOsdCountDownCheck`, que retoma a corrida. Com o carro destruído isso pode dar estado estranho; o teste foi só do Reiniciar.
- Os testes de link do repositório seguem quebrados pelo MinHook, como antes.

### Mapa do fluxo (contra-relógio; ids por contexto na rodada 2)

Caminho de pais: `260121992 StateGlobal` → `240281618 StateSystemEventMonitor` → `214006731 StateIngame`. Contextos e o `StateIngame` de cada um: rali e escola `244682244`, rallycross `201185096`, contra-relógio `214006731`, test drive `170247708`, rali PvP `246317749`, rallycross PvP `58832654`. A pilha completa só existe no contra-relógio e no test drive.

Do `StateRace` (`28191942`): `terminal_damage_replay` → `266119585` `StateVrTransition` → `157578996` `StateBeginTerminalDamage` → `5024886` (decisão). A pilha nova é a cadeia de pais do nó de destino, por isso `PostRace` → `EndRace` → `ResultConfirm` → `ShouldUpdateComponents` → `ControlBlackBars` → overlay `104998759` aparecem mesmo quando o link não aponta para eles.

| Classe | `state` | Nó (contra-relógio) | Observação |
| :--- | :--- | :--- | :--- |
| `StateRace` | `1181589206` | `28191942` | sai da pilha |
| `StatePostRace` | `2367058625` | `179245059` | vtable `0x141251018` |
| `StateEndRace` | `2896601810` | `27150459` | vtable `0x14124f7a8`; zera o gate (abaixo) |
| `StateResultConfirm` | `3320772179` | `96904568` | vtable `0x1412502e8` |
| `StateShouldUpdateComponents` | `2162892711` | `209347693` | vtable `0x141251108` |
| `StateControlBlackBars` | `2009172136` | `114943552` | vtable `0x141261f18` |
| `StateScreenFECore` `terminal_damage_overlay` | `885508184` | `104998759` | pai comum dos estados do dano |
| `StateTerminalDamageCinematicDecision` | `165474100` | `5024886` | `cinematic`/`non_cinematic` → cutscene de início; `multiplayer` → `249747281` |
| `StateCutscene` `terminal_damage_start` | `1461673396` | `56176763` | `disable_ffb=true`, `restore_ffb=false` |
| `StateTerminalDamageStateDecision` | `29328204` | `201182365` | escolhe a saída do freeze |
| `StateTerminalDamageFreeze` | `2859882945` | `154097811` | sem link próprio |
| `StateCutscene` `terminal_damage_end` | `1591963405` | `159383294` | `disable_ffb=true`; `restore_ffb` no default `1` |

Saída do freeze (decisão `201182365`): `part_failure` → trackside `131870526` e depois a cutscene de fim; `default` → cutscene de fim; `skip` → `69169280` (pula a cutscene de fim); `restart` → `199778848`, cujo `next` volta à **mesma** cutscene de fim e não reinicia a especial. A cutscene de fim leva a `66945099` `StateTimeTrialTypeDecision` e aos resultados do contra-relógio. Nada nessa cadeia volta à área de serviço: o `return_to_service` mora no menu de pausa `60321034` (`75453266`).

Nós que o dano não usa: `StateRestartRequired` (`2682915060`) é reinício de pré-corrida; `StateTerminalDamageNonCinematicEnd` (`542166620`) existe no `states.bin` mas tem zero nós no `flow.xml`. `StateCutscene` tem fábrica `0x14028b430`, ctor `0x140249590`, OnEnter `0x14026f130`, Update `0x1402a7650`; a chave `next_cutscene` está no asset, não no `flow.xml`.

## O Reiniciar no menu de pausa do dano

O menu abriu, mas sem **Reiniciar**: a condição `ui.pause_menu.restart_available` é avaliada com `escondido=1` depois do crash. Duas peças em `src/core/pause_menu.cpp`:

- O predicado de visibilidade (`DetourPredicate`) devolve `false` (visível) para essa condição quando `g_restartVisible` está ligado. `IsRestartCondition` reconhece o caminho pela string em `condição+0xa0`.
- O helper que grava `restart_available` e `restart_label` (`0x1402a4bd0`, hook `DetourRestartHelper`) roda com `dl = 0` quando o pedido está ligado: só o caminho completo monta o texto do rótulo.

O pedido chega por um export novo da `dxgi.dll`, **`Dr2Host_SetRestartVisible(int)`**, que o core chama por `GetProcAddress`. Quem liga e desliga (`terminal_damage.cpp`):

- Liga: no F11 (já restrito a offline) e no Esc durante a cutscene (vale também para um crash de verdade, sem F11).
- Desliga: quando a cutscene, o freeze e o menu de pausa saem da pilha depois de terem aparecido (`g_seenTerminal`), ao recarregar o core (F8) e ao desligar o mod.

Resultado ao vivo: o item aparece e o **Reiniciar** reinicia a especial. O rótulo apareceu vazio no teste de 2026-10-04 01:41, antes do hook no helper `0x1402a4bd0`; se o texto voltou depois não ficou registrado. Reiniciar usa o evento nativo `restart_race` do menu, o mod não implementa reinício.

`PauseMenu::TraceCondition` loga cada condição `ui.pause_menu.*` uma vez por valor. É diagnóstico temporário e deve sair do código.

## Por que o tick para

Respostas do REPORT4 (CONFIRMADO no exe; o que é hipótese está marcado).

O tick `0x1403f7b60` é o update do `TerminalDamageController` (vtable `0x141270ac8`; o slot `+0x68` é o thunk `0x14040d310`, que faz `jmp 0x1403f7b60`). Chamadores:

| O quê | VA | Quando |
| :--- | :--- | :--- |
| `TerminalDamageMessage` (callback gravado em `0x1403f32db`) | `0x1403ed080`; `call` em `0x1403ed0b3`, depois `movb $1, 0x77(%rbx)` | uma vez, quando a mensagem chega |
| Laço de componentes | `0x14040b190`, slot `+0x150` da vtable do host `0x14126f440`; `call [vtable+0x68]` em `0x14040b2e6` | cada quadro em que o laço chega no componente |

O laço pula o componente nulo, `+0x1a == 0`, ou `+0x19 != 0` com `+0x10 == 3`. O tick **não** morre porque o controlador é destruído nem por `+0x77`: o controlador continua na lista. O que corta é o byte **`host+0x2350`** (o "gate"): com ele em 0 e `[host+0x2360]` passado a `0x140adb4e0` devolvendo 0, o laço dá `ret` em `0x14040b252..0x14040b259` sem chamar o `+0x68` de nenhum componente.

- **Quem zera o gate:** `StateEndRace`. O OnEnter `0x14026f9e0` faz `jmp 0x140295360`, que chama `0x1403eab50` (`movw $0x100, host+0x2350`: `+0x2350 = 0`, `+0x2351 = 1`) em `0x140295476`, a menos que o byte `+0x3296` do objeto devolvido por `0x140559f10` seja diferente de zero (HYPOTHESIS: flag de sessão; vale 0 no treino offline). Outro chamador: `0x1406843fc`.
- **Quem religa:** só `0x1404069b0` (`movw $1, host+0x2350`), cujo único `call` é `0x140531a58`, na subida dos sistemas da especial. E `StateShouldUpdateComponents` (Update `0x1402aaab0`), mas só enquanto está na pilha: se algum veículo passa de `1.0` em `[corpo+8]+0x2508` (HYPOTHESIS: velocidade) ele grava `host+0x2350 = 1`; se todos ficam em 1 ou menos, grava 0. Por isso o tick volta a rodar numa janela curta logo depois do dano, enquanto o carro ainda anda.
- `TerminalDamageManager` não está nesse caminho (`0x14052f6f1` só registra um updatable).
- Os três handlers do controlador (`ResetCar`, `TerminalDamageMessage`, `UnrecoverableCar`) não chamam os slots `+0x20`/`+0x28` (que ligam/desligam `+0x1a`) nem gravam `+0x1a`.

## O que fica preso depois do Reiniciar

Sintoma (2026-10-04 12:43): o Reiniciar reinicia a especial, mas o som do carro some e o volante fica preso. Reparos já aplicados (zerar `+0x74/+0x75/+0x77/+0x78` do controlador e regravar `+0x556 = 7`) não resolveram nem o som nem a direção. REPORT5 aponta dois efeitos que o Reiniciar não desfaz:

**1. O gate (`host+0x2350`).** `EndRace` o zera; o Reiniciar normal não chama `0x1404069b0` porque num Reiniciar sem crash o `EndRace` nunca rodou. Com o gate em 0, o laço dos componentes não chama o `+0x68` de nenhum componente. HYPOTHESIS: som do motor e direção são componentes desse host. Esperado: `host+0x2350 == 0` depois do Reiniciar com crash, `1` depois do Reiniciar sem crash. Não há mute de bus, snapshot nem `StopEvent` em `EndRace`, no freeze, na decisão ou nas cutscenes. O único evento de áudio do dano com nome é `play_terminal_damage` (`0x14125eda8`), só no OnEnter `0x14030f050` de `StateBeginTerminalDamage`, que é o caminho de `terminal_damage_replay` e não roda aqui.

**2. O force feedback (`ffb+0xe1`).** O `ffb` é `*(VehicleSystem+0x2f0)`. No OnEnter da cutscene (`0x14026f130`), com `estado+0xef` (`disable_ffb`) ligado e o `+0x556` do componente de cutscene ainda 0, ela chama `0x140c2fcb0(ffb, 1)` e `0x140c2fb40(ffb)` (`0x14026f57e`, `0x14026f58a`): grava `ffb+0xe1 = 1` e o float `0.15` em `objeto+0xd8` de até 8 ponteiros em `ffb+0x300`. A subida da especial (`0x140531b74`) chama a mesma função com `edx = 0`, que é o estado de corrida. A saída da cutscene (slot `+0x58` = `0x140277e50`) só restaura se `disable_ffb` e `restore_ffb` estão ligados e `CutsceneManager+0x555 == 0`. A cutscene de início tem `restore_ffb=false`, então não restaura. A de fim herda o default `1` do construtor (`movw $1, estado+0xf0`), mas só restaura se o portão `CutsceneManager+0x555` permitir. Um segundo portão no enter (`r12+0x556 != 0` pula o desligamento) faz o enter da cutscene de fim não repetir o que o de início fez. O índice de `game::CutsceneManager` é `0x14169406c`; o de `UpdateTask` é `0x14169402c`.

Ainda não achado: quem deixa `CutsceneManager+0x555 = 1` no caminho do dano (o `0x1404d3f50` grava o `dl` ali; `0x14026d3e1` e `0x1402a732f` passam 0); qual função está no slot `+0x60` do comando `0xA`; o nome físico do float em `ffb+0x300[]+0xd8`. O som do motor, o byte `0x1415b4bd9` e o `0x14017a160` estão na seção "Som do motor".

### Leitura ao vivo depois do Reiniciar (2026-10-04, pelo canal remoto)

A vtable do `host` ao vivo é `0x14126f588`, não `0x14126f440` (essa é a base que o ctor `0x1403d7ab0` grava). A primeira tentativa do reparo descartou o `host` por isso (`controlador=nao`, `host=-1`). Corrigido: `IsHostVtable` aceita as duas.

Com a correção, no instante em que o `StateRace` volta ao topo depois de F11 → Esc → Reiniciar: `controlador=ok +0x74=0 +0x75=0 motivo=7 host+0x2350=1 ffb+0xe1=0`. **Ou seja, o gate já estava em 1 e o FFB em 0**: as hipóteses 1 e 2 do REPORT5 não explicam o sintoma, ao menos nesse instante. O que essa leitura não cobre é `veículo+0x368`, abaixo.

## Som do motor

Leitura estática do `dirtrally2.exe` (2026-10-04), depois da leitura ao vivo acima. O som do motor não é um mute de bus.

**Quem liga.** O componente de áudio do carro (o objeto da lista `[manager+0xf8, manager+0x100)`) posta o motor em `0x140b003b0`. O único chamador é o update `0x140b1c510`, e só se `áudio+0x314 == 9`, `áudio+0x250 != 0`, `áudio+0x28 != 0` e `áudio+0x40 != 0`. O nome é `play_engine_player_` (`0x1413b2070`) ou `play_engine_ai_` (`0x1413b2088`), conforme `áudio+0x363`. O wrapper `0x140ebb6f0` resolve o nome no objeto global `0x1415bc188` (vfunc `+0xf0`) e chama `0x140ebb5d0`; o id volta em `áudio+0x2118`. O mesmo bloco faz o escape com `play_exhaust_player_` / `play_exhaust_ai_` em `áudio+0x211c`. O game object passado no post é `áudio+0x20f0`.

O RTPC `engine_rpm` (`0x1413b1f88`) é resolvido em `0x140b0eb00` (vfunc `+0x88` do mesmo global) e guardado em `áudio+0x21a0` (bundle `engine_bundle`) e `áudio+0x21d8` (bundle `exhaust_bundle`). O update grava o valor com `0x140ebc2e0` em `0x140b1c68d`. `engine_rpm_normalised` é `0x1413b2230`, resolvido em `0x140b1dc3e`.

**Quem para.** O tick do manager (`0x140b1db50`, chamado de `0x140b16bc6`) percorre essa lista. Para cada carro:

```
veículo = *(áudio+0x38)
se veículo+0x368 != 0:
    0x140b155e0(áudio, dl=1)     ; para o motor todo quadro
senão:
    0x140b167c0(...)             ; arma áudio+0x250 a partir de veículo+0xa7
    0x140b1c510(áudio, ...)      ; o update que posta o motor
```

`0x140b155e0` com `dl != 0`, se o dword em `áudio+0x2118` não é zero (`0x1409bf520` é só `*id != 0`), chama `0x140eb9ef0(áudio+0x20f0)`, tira o id da tabela global `0x1415b44c0` (`0x140a0d780` → `0x140a03c20`) e zera o id (`0x140ebb2f0` faz `*id = 0`). O escape em `+0x211c` segue o mesmo par. Com `dl == 0` essa função não para os ids.

Quem liga o byte: `0x1409913e0(veículo, dl)` (`RetireVehicle`). Se `veículo+0x30` e `*(veículo+0x30)+0x840` existem, grava `veículo+0x35c = 1`, `veículo+0x368 = 1` e o slot `*(0x14159dad8)+0x194a8 + (veículo+0x6c)*4 = 1`, e chama `0x140991b20`. Quem desliga: `0x140999990(veículo)` (`RestoreVehicle`), só se `+0x368` já é 1: devolve `+0x35c` a partir de `+0x360`, grava `+0x368 = 0` e o mesmo slot global em 0.

No caminho de estados, o OnEnter `0x140274b60` (vtable `0x14124f550`, slot `+0x18`; fábrica `0x140291d10`, aloca `0x38`) pega o veículo 0 com `0x1409713e0(VehicleSystem, 0)` e chama `0x1409913e0(veículo, 0)`. O mesmo par está em `0x14029e800`. Outro update, `0x1402a536a`, percorre uma lista e aposenta todo veículo que ainda tem `+0x368 == 0`. O OnEnter do Reiniciar (`0x1402708a0`) não chama `0x140999990`. A subida da especial chama, em `0x140531a38`, mas só quando `0x1405083c0` devolve ponteiro e `retorno+0x63` difere de `sil`; senão chama o aposentar. `host+0x2350 == 1` depois do Reiniciar não prova que essa subida rodou: `StateShouldUpdateComponents` também liga o gate quando algum carro passa de `1.0`.

O desmonte completo do manager é `0x140af57d0`: chama `0x140b0eec0` em cada carro, que zera `áudio+0x314` e para o motor. Chamadores: o dtor `0x140aec140` e `0x140b0ef90`. Não aparece no OnEnter do `EndRace` nem no setter `0x140744460`.

**O que não é o mute.**

- `componente+0x556` é o motivo do dano (`4` impacto, `7` carro inteiro). Regravar `7` não reposta o evento. O `+0x556` da cutscene é outro objeto: trava de uma vez do desligamento do FFB (`0x14026f67b` grava 1).
- `CutsceneManager+0x555` só corta a restauração do FFB na saída da cutscene (`0x140277ec5`). Não há `lea` de `play_engine_*` nessa função.
- `0x14017a160` é o trocador de modo do `game::Osd` (índice `0x1416940b0`, nome `0x14123b4f0`). Grava o modo em `osd+0x331` e o anterior em `+0x332`. `EndRace` (`0x1402954dd`) e o tick do dano (`0x1403f7c9b`, e só quando o byte devolvido por `0x140732130` não é 0..4 nem 6) passam modo `5`. Modo `4` aparece em `0x14035db89` e `0x14061dd97`, e só ele liga `osd+0xb3`. Não chama post nem stop de Wwise.
- O byte global `0x1415b4bd9` é gravado `1` no enter da cutscene (`0x14026f1e4`) e em `0x14027370a`, e volta a `0` em `0x14052e58d` (região da subida da especial). O compare em `0x140a5a9df` pula um bloco de contas de listener quando o byte é 0. Não chama `0x140b155e0`.

**Reparo a testar**, no mesmo ponto em que o `StateRace` volta, antes de concluir que o som não volta:

```
vs      = tabela[*(int32*)0x141693ffc]     ; neRacing::VehicleSystem, a mesma cadeia do FFB
veículo = 0x1409713e0(vs, 0)
```

Ler `veículo+0x368`, `veículo+0xa7` e `veículo+0x6c`. Se `+0x368 == 1`, chamar `0x140999990(veículo)` e reler o byte (esperado `0`). No quadro seguinte o tick deixa de chamar `0x140b155e0` e o update posta de novo, desde que `áudio+0x314` ainda seja `9` e `áudio+0x250` volte a não-zero. `+0x250` só é religado, dentro de `0x140b167c0`, se `veículo+0xa7 != 0`. Se depois do `0x140999990` o som continuar mudo, a próxima leitura é essa: `+0xa7`, e no componente de áudio cujo `+0x38` é esse veículo, `+0x314`, `+0x250` e o dword `+0x2118` (0 = sem evento postado). Não chamar `0x140b003b0` à mão antes dessa leitura: sem `+0x314 == 9` o post não é o caminho que a corrida usa.

### Reparo atual (`RepairAfterRestart`)

Quando a sequência termina (cutscene, freeze e menu de pausa saíram da pilha) e o `StateRace` volta ao topo (espera até 30 s), o core, só se `CanWriteState()` deixar:

1. Loga `apos reiniciar: controlador, +0x74, +0x75, motivo, host+0x2350, ffb+0xe1`.
2. Zera `+0x74/+0x75/+0x77/+0x78` e põe `+0x556 = 7` (o `0x1403ffd00(controlador)` do REPORT5 faz o mesmo no jogo e não foi chamado).
3. Se `host+0x2350 == 0`, chama `0x1404069b0(host)`.
4. Se `ffb+0xe1 == 1`, chama `0x140c2fcb0(ffb, 0)` e `0x140c2fb40(ffb)`.

Como ler o teste: F8, F11, Esc, Reiniciar. Se só um dos dois voltar, o log diz qual passo funcionou. Hipóteses do REPORT5, em ordem: (1) gate em 0; (2) FFB em 1 (prende o volante e não explica o som); (3) o comando `0xA` não recria o carro, e o `+0x556` regravado não rearma o áudio (cai se o controlador antigo perder a vtable `0x141270ac8`, ou se o ponteiro novo aparecer na lista `host+0x30 .. host+0x38`). Leituras úteis antes e depois do Reiniciar: ponteiros de `host`, `ffb` e controlador, `CutsceneManager+0x555`, e se `*controlador_antigo` ainda é `0x141270ac8`. Tabela esperada em REPORT5 §G.

## Sair sem esperar (não testado)

`StateRecoverToServiceAreaCheck` (OnEnter `0x140273fa0`) posta `terminal_damage_recover_to_event_end` ou `terminal_damage_recover_to_service_area`, conforme o byte `+0x1e3a0` do objeto devolvido por `0x14055a4a0`. O runner só aceita link que exista no nó do topo.

## Pendências

- No Reiniciar depois do F11, ler `veículo+0x368` (e `+0xa7`) antes de qualquer escrita. Se for 1, chamar `0x140999990(veículo)` e ver se o motor volta. O volante preso é a mesma função de aposentar; a leitura separa os dois.
- Confirmar o rótulo do item Reiniciar com o hook do helper `0x1402a4bd0`.
- Testar o segundo F11 com o carro já destruído.
- Dar o gancho de Esc também ao freeze.
- Tirar `TraceCondition` do `pause_menu.cpp` e o log de "helper do Reiniciar" e "Reiniciar liberado".
- Teste automatizado do patch do `flow` (`PatchTerminalPause`).
- Nada disto está commitado; a árvore mistura câmera livre, GhostLab e dano terminal e precisa de commits separados.
