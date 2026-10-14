# Guia de Instalação e Configuração do DR2Hook v0.1.0

Este guia detalha os procedimentos para instalação, validação, uso e desinstalação do **DR2Hook v0.1.0** no *DiRT Rally 2.0* para plataformas Windows e Linux (incluindo Steam Deck).

---

## 1. Requisitos do Sistema

- **Jogo Base:** *DiRT Rally 2.0* (Steam, executável x64 `dirtrally2.exe`).
- **Renderizador:** DirectX 11.
- **Sistema Operacional Suportado:**
  - Windows 10 ou 11 (64-bit).
  - Linux (qualquer distribuição com Steam e Proton 7.0+ ou Proton Experimental / GE-Proton).
  - Steam Deck (SteamOS 3.4+).

---

## 2. Instalação no Windows 10 / 11 (Steam)

O DR2Hook opera como uma biblioteca proxy manual (`dxgi.dll`). Sua instalação segue o modelo de "zero configuração", bastando extrair os arquivos na pasta raiz do jogo.

### Passo 1: Localizar a pasta do DiRT Rally 2.0
1. Abra o aplicativo **Steam** e acesse a sua **Biblioteca**.
2. Clique com o botão direito sobre o título **DiRT Rally 2.0**.
3. Selecione **Gerenciar** > **Explorar arquivos locais**.
4. O Windows Explorer abrirá na pasta principal do jogo. Certifique-se de visualizar o arquivo `dirtrally2.exe`.

### Passo 2: Extrair o pacote de release
1. Baixe o pacote oficial `DR2Hook-v0.1.0.zip`.
2. Extraia o conteúdo diretamente na pasta onde se encontra `dirtrally2.exe`.
3. A estrutura de diretórios deve ficar configurada exatamente da seguinte forma:

```
[Pasta de Instalação do DiRT Rally 2.0]/
├── dirtrally2.exe
├── dxgi.dll                    <-- Proxy DLL do DR2Hook (residente)
├── dr2hook_core.dll            <-- Overlay, telemetria e mods (recarregável com F8)
├── mods/
│   ├── debug_mode/             <-- Número de fantasmas e teclas de pesquisa (F9/F11)
│   │   ├── mod.json
│   │   └── main.lua
│   └── practice_mode/          <-- Mod oficial de treino
│       ├── mod.json
│       └── main.lua
```

### Passo 3: Iniciar e verificar o funcionamento
1. Inicie o jogo normalmente através da sua biblioteca Steam.
2. Ao carregar a tela principal, o DR2Hook iniciará de forma assíncrona desacoplada em segundo plano sem impactar o tempo de carregamento.
3. **Isolamento de Rede Automático:** Por design e segurança (Fair Play), o mod loader ativa o isolamento de rede Winsock para impedir conexões aos servidores do RaceNet. O jogo operará estritamente offline enquanto a DLL estiver carregada.
4. Entre em uma sessão de treino livre (ex: **DirtFish** ou **Tomada de Tempo / Time Trial**).
5. **Verifique os atalhos de teclado e interface:**
   - **`Insert`**: Abre ou fecha a interface in-game Dear ImGui (`DR2Hook v0.1.0`), com abas dedicadas:
     - **Diagnostics:** Telemetria em tempo real (RPM real do motor, marcha ativa, velocidade km/h, posição XYZ, velocidade angular, suspensão).
     - **Mods:** Lista e status dos mods Lua carregados.
     - **Practice Mode:** Painel de controle de checkpoints e escolha de modo de restauração (**Normal** vs. **With Momentum**).
   - **`F5`**: Salva o checkpoint atual (posição espacial, orientação, velocidade linear e velocidade angular).
   - **`F6`**: Restaura o checkpoint salvo instantaneamente no modo **Normal** (parado, suspensão assentada).
   - **`F7`**: Restaura o checkpoint com **With Momentum** (velocidade linear e angular do instante da gravação).
   - **`F8`**: Recarrega `dr2hook_core.dll` do disco sem fechar o jogo. A `dxgi.dll` permanece carregada. O checkpoint que só existia em memória é descartado e os scripts Lua reiniciam (`onInit` roda de novo). O mesmo comando está no botão **Reload Native Core (F8)** da aba Mods. O botão **Reload Scripts (Hot-Reload)** dessa aba só reinicia o Lua.
6. **Outros atalhos do core:**
   - **`F9`**: câmera livre na especial (mouse olha, WASD move, Espaço/Q sobem e descem, Ctrl congela o movimento do teclado, `+`/`-` mudam a velocidade, Shift multiplica por 4).
   - **`F11`**: insta crash (destrói o carro, só offline), para estudar o fluxo de dano terminal.
   - F9 e F11 podem ser desligadas em **Pausa > DR2 Hook > Mods > Debug Mode**; ali também fica quantos carros fantasma aparecem (*Ghost cars on screen*).
7. **Arquivos opcionais ao lado de `dirtrally2.exe`** (ferramentas de pesquisa; sem eles nada muda):
   - `dr2hook_ghost_cars.txt`: um número (máximo efetivo **15**) com quantos carros fantasma o jogo cria na próxima carga completa da especial. Com o arquivo presente, **F7** soma uma cópia de teste e **F6** pausa e retoma os fantasmas, em vez das ações do checkpoint. Ver [ghosts.md](../reverse_engineering/ghosts.md) §6.
   - `dr2hook_cmd.txt`: comandos remotos executados pelo core e apagados em seguida. Ver [remote_commands.md](remote_commands.md).
8. **Verificação de Log:**
   - Na pasta do jogo, verifique a criação do arquivo `dr2hook.log`. Ele registrará os hooks em `Present`, `WndProc`, Winsock (`ws2_32.dll`), a inicialização do motor Lua e o carregamento do mod `practice_mode`.

---

## 3. Instalação no Linux e Steam Deck (Steam Proton)

No ecossistema Linux/SteamOS via Wine/Proton, o carregamento de DLLs proxy locais requer a declaração explícita de override para priorizar a biblioteca local antes da versão embutida (*builtin*) do Wine.

### Passo 1: Extração dos arquivos
1. No **Steam Deck**, acerte para o **Modo Desktop** (ou execute via terminal em distribuições Linux convencionais).
2. Abra a pasta do jogo via Steam (**Gerenciar** > **Explorar arquivos locais**).
3. Extraia o arquivo `DR2Hook-v0.1.0.zip` para a pasta do jogo ao lado de `dirtrally2.exe`. Os dois binários precisam estar lá: `dxgi.dll` e `dr2hook_core.dll`, ambos em minúsculas.

### Passo 2: Configurar o parâmetro de inicialização (DLL Override)
1. No cliente Steam (Modo Desktop ou Modo Jogo), clique com o botão direito em **DiRT Rally 2.0** e selecione **Propriedades...**
2. Na aba **Geral**, localize o campo **Opções de Inicialização** (*Launch Options*).
3. Adicione o seguinte comando:

```bash
WINEDLLOVERRIDES="dxgi=n,b" %command%
```

> **Significado técnico:**
> `WINEDLLOVERRIDES="dxgi=n,b"` instrui o carregador do Wine a buscar primeiro a DLL nativa (`n` - native) presente na pasta do executável, e recorrer à versão embutida (`b` - builtin) caso a primeira não esteja disponível.

4. Feche a janela de propriedades e execute o jogo.

---

## 4. Desinstalação do DR2Hook

Para desinstalar completamente o mod loader sem reinstalar ou verificar arquivos do jogo:

1. Acesse a pasta raiz do DiRT Rally 2.0.
2. Exclua `dxgi.dll`, `dr2hook_core.dll` e qualquer `dr2hook_core.*.dll` deixada por um reload.
3. (Opcional) Exclua a pasta `mods/` e o arquivo `dr2hook.log`.
4. No Linux / Steam Deck: remova a linha `WINEDLLOVERRIDES="dxgi=n,b" %command%` das opções de inicialização na Steam.
5. Ao iniciar novamente, o DiRT Rally 2.0 carregará o `dxgi.dll` oficial do sistema operacional sem qualquer interferência e a conectividade com servidores do jogo será restabelecida.

---

## 5. Desenvolvimento com hot-reload

A `dxgi.dll` fica mapeada o tempo todo. O que muda com frequência (telemetria, overlay, savestate, Lua) está em `dr2hook_core.dll`.

1. Compile `dr2hook_core.dll` e substitua o arquivo de mesmo nome na pasta do jogo. Não substitua `dr2hook_core.N.dll`: essa cópia é a que o processo mantém carregada.
2. No jogo, pressione **F8**.

O host copia `dr2hook_core.dll` para um nome novo (`dr2hook_core.1.dll`, `dr2hook_core.2.dll`, …), encerra o módulo anterior e carrega a cópia. Por isso o arquivo que você acabou de compilar não fica travado pelo processo. Trocar a própria `dxgi.dll` ainda exige fechar o jogo.

---

## 6. Resolução de Problemas Comuns (Troubleshooting)

### O jogo fecha imediatamente ou não inicia
- **Causa provável:** Bloqueio de antivírus ou dependência ausente do Visual C++ Redistributable.
- **Solução:**
  1. Verifique se o Windows Defender ou software antivírus moveu `dxgi.dll` ou `dr2hook_core.dll` para quarentena. Caso positivo, crie uma exceção para os dois arquivos na pasta do jogo.
  2. Verifique se o arquivo `dr2hook.log` foi criado. Caso o arquivo de log esteja em branco ou não exista, o jogo pode estar travando antes de carregar dependências do DirectX. Instale a versão mais recente do *Visual C++ Redistributable x64 (2015-2022)*.

### O jogo inicia, mas o menu (`Insert`) ou atalhos (`F5`/`F6`) não funcionam
- **Causa provável:** Interceptação ou conflito de hooks com outros softwares de captura/overlay.
- **Solução:**
  1. Aplicativos como *RivaTuner Statistics Server (RTSS)*, *MSI Afterburner*, *GeForce Experience* ou *Discord Overlay* podem concorrer pelos hooks do DirectX 11. Configure o RTSS para nível de injeção *Low* ou adicione uma exceção para `dirtrally2.exe`.
  2. Confirme que `dr2hook_core.dll` está ao lado de `dxgi.dll`. Sem ela o proxy sobe, mas o menu não.
  3. Abra `dr2hook.log` e confirme se há mensagens como:
     - `dr2hook_core.dll carregado.`
     - `Hooks principais inicializados com sucesso.`
     - `Mod carregado com sucesso: Practice Mode (dr2.practice_mode) v1.0.0`

### "Savestate bloqueado em modos competitivos/oficiais!"
- **Causa:** Política de Fair Play (Anti-Cheat) ativada.
- **Explicação:** O DR2Hook bloqueia escritas de memória em eventos oficiais da RaceNet e mantém o jogo isolado da rede. Isso é um comportamento deliberado e inegociável para preservar tabelas de líderes oficiais.
- **Solução:** Para utilizar treinos de savestate livremente, utilize o modo **DirtFish**, **Tomada de Tempo (Time Trial)** ou **Campeonatos Customizados Offline**.

### Problemas específicos no Steam Deck / Proton
- Se o jogo iniciar sem o DR2Hook carregar no Proton:
  1. Certifique-se de que a variável `WINEDLLOVERRIDES="dxgi=n,b" %command%` foi digitada exatamente como exibido, incluindo as aspas e o `%command%` final.
  2. Confirme se os nomes na pasta do jogo estão exatamente em minúsculas: `dxgi.dll` e `dr2hook_core.dll`. Sistemas de arquivos Linux são sensíveis a maiúsculas/minúsculas.
