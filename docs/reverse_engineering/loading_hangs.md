# Travamentos da carga como sonda do carregamento

Quando uma mudança nossa deixa a tela de carga esperando para sempre, o travamento mostra **o que a carga espera**. Vale tratar como instrumento, não só como bug. Registrado em 2026-10-04, durante o teste de limite de fantasmas (`ghosts.md` §6.3).

## Caso 1: 3º carro fantasma (2026-10-04)

- Mudança: antes de `SpawnStageVehicles` (`0x14046b320`), zerar `entrada+0xb4` da 3ª entrada de fantasma para o passe 2 criar `car 4`.
- Efeito: o spawn retorna normal, o IO continua uns segundos e a carga **nunca termina**. Sem exceção. Em 1 de 3 tentativas o processo morreu em silêncio ~5 s depois.
- A thread principal segue no laço de frames (`0x1403c3a10` → `Sleep` em `0x14109f440`): quem espera é a **máquina de estados da tela de carga**, não uma thread presa.
- Durante a espera os 5 controladores de fantasma ainda não têm veículo (`+0x00 == 0`) e os slots estão em estado 0. Os controladores só ganham veículo depois da carga.
- Conclusão parcial: a carga tem uma condição de "todos os carros prontos" que o veículo extra não cumpre. Ver `investigations/grok/ghost-limit-prompt-2.md`.

## Como reproduzir a sonda

1. Hook de log no ponto de interesse (aqui `SpawnStageVehicles`), registrando o estado antes e depois (`GhostLab[limite]`, `LogStageEntries` em `src/core/ghost_lab.cpp`).
2. **Vigia de threads** (`DumpThreads`): 25 s depois, suspende cada thread, lê `RIP` e varre a pilha (até 0x6000 bytes) atrás de endereços de retorno no exe, conferindo o byte `e8`/`ff 15` antes. Grava `GhostLab[espera]` no log. Só roda com `dr2hook_ghost_cars.txt` presente.
3. Achar a thread principal: a que passa por `0x1403c3a10`/`0x1405fa5c8` (laço de frames). As demais com `rip=6ffffff3ea94/f094/f574` são esperas do ntdll de threads de trabalho.
4. Captador de exceção (`CrashLogger`, VEH): registra violações de acesso com `RIP`, registradores e retornos. Se não aparecer nada, o travamento é espera, não crash.
5. O jogo continua vivo e legível por `/proc/<pid>/mem`: dá para inspecionar a memória durante a espera.

## Pegadinhas

- Pelo `/proc/<pid>/task/*/syscall` só se vê a pilha do Wine (a thread `pid` é a de inicialização, em `select`). A pilha do Windows tem de ser lida de dentro do processo.
- O Reiniciar **não** recria os veículos: hooks de spawn só disparam em carregamento completo da especial.
- Travamento de carga só sai reiniciando o jogo (`scripts/dev/restart_game.sh`).
- O pool de entradas da sessão é reaproveitado entre cargas (mesmos endereços).

## Ideia

O mesmo vigia serve para qualquer carga que pare: ligar por arquivo de teste, 25 s, ler `GhostLab[espera]` e seguir os retornos da thread principal até a função que testa a condição.
