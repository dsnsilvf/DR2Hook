# Comandos remotos

Canal por arquivo para controlar o jogo sem ninguém no teclado (a IA escreve um arquivo, o jogo executa). Código: `src/core/remote_commands.cpp`, no `dr2hook_core.dll` (recarrega com F8).

## Protocolo

1. Escrever linhas de comando em `dr2hook_cmd.txt`, na pasta do jogo (a do `dirtrally2.exe`). Uma por linha; linhas vazias e as que começam com `#` são ignoradas.
2. A cada ~150 ms o core lê o arquivo e o **apaga**, executa as linhas em ordem e grava `dr2hook_cmd.out`: `# lote N`, e para cada comando `> comando` seguido do resultado.
3. Cada comando também vai para o `dr2hook.log` com o prefixo `[remoto]`.

Para o próximo lote, esperar o `dr2hook_cmd.txt` sumir e ler o `.out`.

## Comandos

| Comando | O que faz |
| :--- | :--- |
| `help` | Lista os comandos. |
| `status` | Resumo em linguagem simples: o que está no topo (corrida, menu de pausa, cutscene, opções…) e se a escrita está liberada (offline). |
| `stack` | Pilha de estados do fluxo (base → topo), com a vtable de cada um. Diz em que tela o jogo está. |
| `pause` | Pausa a corrida: liga o byte `+0x58` do `StateRace` do topo (o mesmo que a ação `driving.pause` faz), e o slot `+0x70` devolve `pause`. Só com a corrida no topo. Validado no jogo. |
| `unpause` | Atalho de `link continue` no menu de pausa. Volta à corrida (passando pela contagem). Validado. |
| `link <nome>` | Posta a transição `<nome>` (`options`, `back`, `continue`, `restart_race`, `quit`…) no estado do topo, se for uma tela. O runner abre o link no próximo tick, como se o item tivesse sido escolhido. Recusa em evento online e quando o topo não é uma tela. |
| `key <tecla>` | Posta `WM_KEYDOWN`/`WM_KEYUP` na janela: `f8`, `f9`, `f11`, `esc`, `insert`, letras, `0x77`. Alcança os atalhos do core, o F8 e o `onKeyDown` dos mods. **Não move os menus nativos** (o jogo lê Raw Input); para isso use `link`. |
| `crash` | Insta crash (mesmo do F11). Só em treino offline. |
| `mods` | Lista os mods Lua e as opções (`[índice] id = valor`). |
| `opt <mod> <opção> [valor]` | Mexe numa opção de mod, por índice ou id. `valor`: índice do combo (toggle: `0` Off, `1` On) ou o texto do valor. Sem valor: botão chama, toggle e choice avançam. |
| `toast <texto>` | Mostra um aviso no overlay. |
| `log <texto>` | Escreve uma linha no log (marca um ponto do teste). |

## Limites e dúvidas

- `link` validado no jogo em 2026-10-04: com a pausa aberta, `link options` abriu `options_ingame` (topo `st=1256e98`) e `link back` voltou.
- Navegação por cursor (mover o foco, escolher o item destacado) não existe: só transições pelo nome do link. O nome certo sai do `flow.xml` do nó do topo; `stack` mostra o `id` do nó.
- O canal não executa Lua nem lê memória arbitrária.
- `key f8` recarrega o core pelo canal (validado): dá para instalar um core novo e recarregar sem ninguém no teclado.
- Fluxo do dano terminal só pelo canal, validado em 2026-10-04: `unpause`, `crash`, `key esc` (abre o menu de pausa), `link restart_race` (reinicia a especial).
- Depois de um Reiniciar o jogo espera a largada. Com *Race start* em *On throttle* ou *Normal* é preciso uma pessoa no volante; **antes de ficar afk, mande `opt dr2.practice_mode race_start Automatic`** (a opção é salva em `settings.ini`). Mudar a opção com a largada já esperando não a libera.
- Print da janela: python-xlib (ver a nota `remote-game-control` da memória); funciona com o jogo em tela cheia.
