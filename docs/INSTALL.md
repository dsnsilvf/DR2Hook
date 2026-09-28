# Guia de Instalação e Configuração do DR2 ModLoader v0.1.0

Este guia detalha os procedimentos para instalação, validação, uso e desinstalação do **DR2 ModLoader v0.1.0** no *DiRT Rally 2.0* para plataformas Windows e Linux (incluindo Steam Deck).

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

O DR2 ModLoader opera como uma biblioteca proxy manual (`dxgi.dll`). Sua instalação segue o modelo de "zero configuração", bastando extrair os arquivos na pasta raiz do jogo.

### Passo 1: Localizar a pasta do DiRT Rally 2.0
1. Abra o aplicativo **Steam** e acesse a sua **Biblioteca**.
2. Clique com o botão direito sobre o título **DiRT Rally 2.0**.
3. Selecione **Gerenciar** > **Explorar arquivos locais**.
4. O Windows Explorer abrirá na pasta principal do jogo. Certifique-se de visualizar o arquivo `dirtrally2.exe`.

### Passo 2: Extrair o pacote de release
1. Baixe o pacote oficial `DR2ModLoader-v0.1.0.zip`.
2. Extraia o conteúdo diretamente na pasta onde se encontra `dirtrally2.exe`.
3. A estrutura de diretórios deve ficar configurada exatamente da seguinte forma:

```
[Pasta de Instalação do DiRT Rally 2.0]/
├── dirtrally2.exe
├── dxgi.dll                    <-- Proxy DLL do DR2 ModLoader
├── mods/
│   └── practice_mode/          <-- Mod oficial de treino
│       ├── mod.json
│       └── main.lua
```

### Passo 3: Iniciar e verificar o funcionamento
1. Inicie o jogo normalmente através da sua biblioteca Steam.
2. Ao carregar a tela principal, o DR2 ModLoader iniciará de forma assíncrona desacoplada em segundo plano sem impactar o tempo de carregamento.
3. **Isolamento de Rede Automático:** Por design e segurança (Fair Play), o mod loader ativa o isolamento de rede Winsock para impedir conexões aos servidores do RaceNet. O jogo operará estritamente offline enquanto a DLL estiver carregada.
4. Entre em uma sessão de treino livre (ex: **DirtFish** ou **Tomada de Tempo / Time Trial**).
5. **Verifique os atalhos de teclado e interface:**
   - **`Insert`**: Abre ou fecha a interface in-game Dear ImGui (`DR2 ModLoader v0.1.0`), com abas dedicadas:
     - **Diagnostics:** Telemetria em tempo real (RPM real do motor, marcha ativa, velocidade km/h, posição XYZ, velocidade angular, suspensão).
     - **Mods:** Lista e status dos mods Lua carregados.
     - **Practice Mode:** Painel de controle de checkpoints e escolha de modo de restauração (**Normal** vs. **With Momentum**).
   - **`F5`**: Salva o checkpoint atual (posição espacial, orientação, velocidade linear e velocidade angular).
   - **`F6`**: Restaura o checkpoint salvo instantaneamente conforme o modo selecionado (Normal ou Com Momentum).
6. **Verificação de Log:**
   - Na pasta do jogo, verifique a criação do arquivo `dr2hook.log`. Ele registrará os hooks em `Present`, `WndProc`, Winsock (`ws2_32.dll`), a inicialização do motor Lua e o carregamento do mod `practice_mode`.

---

## 3. Instalação no Linux e Steam Deck (Steam Proton)

No ecossistema Linux/SteamOS via Wine/Proton, o carregamento de DLLs proxy locais requer a declaração explícita de override para priorizar a biblioteca local antes da versão embutida (*builtin*) do Wine.

### Passo 1: Extração dos arquivos
1. No **Steam Deck**, acerte para o **Modo Desktop** (ou execute via terminal em distribuições Linux convencionais).
2. Abra a pasta do jogo via Steam (**Gerenciar** > **Explorar arquivos locais**).
3. Extraia o arquivo `DR2ModLoader-v0.1.0.zip` para a pasta do jogo ao lado de `dirtrally2.exe`.

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

## 4. Desinstalação do DR2 ModLoader

Para desinstalar completamente o mod loader sem reinstalar ou verificar arquivos do jogo:

1. Acesse a pasta raiz do DiRT Rally 2.0.
2. Exclua o arquivo `dxgi.dll`.
3. (Opcional) Exclua a pasta `mods/` e o arquivo `dr2hook.log`.
4. No Linux / Steam Deck: remova a linha `WINEDLLOVERRIDES="dxgi=n,b" %command%` das opções de inicialização na Steam.
5. Ao iniciar novamente, o DiRT Rally 2.0 carregará o `dxgi.dll` oficial do sistema operacional sem qualquer interferência e a conectividade com servidores do jogo será restabelecida.

---

## 5. Resolução de Problemas Comuns (Troubleshooting)

### O jogo fecha imediatamente ou não inicia
- **Causa provável:** Bloqueio de antivírus ou dependência ausente do Visual C++ Redistributable.
- **Solução:**
  1. Verifique se o Windows Defender ou software antivírus moveu `dxgi.dll` para quarentena. Caso positivo, crie uma exceção para o arquivo na pasta do jogo.
  2. Verifique se o arquivo `dr2hook.log` foi criado. Caso o arquivo de log esteja em branco ou não exista, o jogo pode estar travando antes de carregar dependências do DirectX. Instale a versão mais recente do *Visual C++ Redistributable x64 (2015-2022)*.

### O jogo inicia, mas o menu (`Insert`) ou atalhos (`F5`/`F6`) não funcionam
- **Causa provável:** Interceptação ou conflito de hooks com outros softwares de captura/overlay.
- **Solução:**
  1. Aplicativos como *RivaTuner Statistics Server (RTSS)*, *MSI Afterburner*, *GeForce Experience* ou *Discord Overlay* podem concorrer pelos hooks do DirectX 11. Configure o RTSS para nível de injeção *Low* ou adicione uma exceção para `dirtrally2.exe`.
  2. Abra `dr2hook.log` e confirme se há mensagens como:
     - `Hooks inicializados com sucesso.`
     - `ModManager: carregado mod 'dr2.practice_mode'`

### "Savestate bloqueado em modos competitivos/oficiais!"
- **Causa:** Política de Fair Play (Anti-Cheat) ativada.
- **Explicação:** O DR2 ModLoader bloqueia escritas de memória em eventos oficiais da RaceNet e mantém o jogo isolado da rede. Isso é um comportamento deliberado e inegociável para preservar tabelas de líderes oficiais.
- **Solução:** Para utilizar treinos de savestate livremente, utilize o modo **DirtFish**, **Tomada de Tempo (Time Trial)** ou **Campeonatos Customizados Offline**.

### Problemas específicos no Steam Deck / Proton
- Se o jogo iniciar sem o DR2 ModLoader carregar no Proton:
  1. Certifique-se de que a variável `WINEDLLOVERRIDES="dxgi=n,b" %command%` foi digitada exatamente como exibido, incluindo as aspas e o `%command%` final.
  2. Confirme se o nome do arquivo na pasta do jogo está exatamente em minúsculas: `dxgi.dll`. Sistemas de arquivos Linux são sensíveis a maiúsculas/minúsculas.
