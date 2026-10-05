# Prompt para o Grok: por que o som do carro some depois do Reiniciar (dano terminal)

Cole tudo abaixo. Anexe `docs/reverse_engineering/terminal_damage.md` e, se puder, `dirtrally2.exe` (Ghidra/IDA) ou os dumps.

---

Você é um engenheiro de engenharia reversa (x64, Windows, Wwise). Preciso da causa do SOM DO CARRO SUMIR depois do Reiniciar, no dirtrally2.exe (DiRT Rally 2.0, imagem base 0x140000000, símbolos MSVC do Wwise AK::SoundEngine presentes no export/strings do exe).

## Sintoma
1. Em uma especial, o carro sofre dano terminal (insta crash forçado por mim; motivo em `componente+0x556 = 7`, setter `0x140744460(comp,4,0)`).
2. Esc abre o menu de pausa (patch meu no fluxo), escolho Reiniciar.
3. A especial recomeça, o carro anda, mas **o som do carro (motor) não toca** e o **volante fica preso** (force feedback). O resto do som (UI, ambiente) não sei se continua.
4. Sem crash prévio, Reiniciar normal funciona e o som volta.

## O que já foi verificado (não repita)
- No instante em que o `StateRace` volta ao topo após o Reiniciar: controlador ok, `+0x74=0 +0x75=0 +0x77/+0x78=0`, `+0x556=7`, **gate `host+0x2350 = 1`** (host = ObjectManager, vtable viva `0x14126f588`), **`ffb+0xe1 = 0`**. Ou seja gate e FFB já estão normais: as hipóteses "gate em 0" e "FFB em 1" estão descartadas nesse instante.
- `StateEndRace` zera o gate (`0x1403eab50`); `0x1404069b0` o religa; `0x140c2fcb0/0x140c2fb40` restauram FFB. O OnEnter da cutscene `0x14026f130` desliga FFB (`+0xef disable_ffb`); a saída `0x140277e50` só restaura se `CutsceneManager+0x555 == 0` (índice `0x14169406c`) e não achei quem deixa esse byte em 1.
- Não há mute de bus, snapshot nem `StopEvent` em EndRace, freeze, decisão ou cutscenes. O único evento de áudio do dano com nome é `play_terminal_damage` (`0x14125eda8`), só no OnEnter `0x14030f050` de `StateBeginTerminalDamage` (caminho de `terminal_damage_replay`, que NÃO roda aqui).
- Byte global `0x1415b4bd9 = 1` gravado em `0x14026f1e4` (efeito desconhecido). `0x14017a160` com modo 4 mexe no dispositivo de áudio (efeito desconhecido).
- Strings relevantes no exe: `engine_rpm`, `engine_rpm_normalised`, `audio/wwiseConfig.xml`, `AK::BankManager`, `AK::EventManager`, `puncture_tyre`, `blow_tyre`, `lose_wheel`.

## O que quero de você (nesta ordem)
1. **Quem cria/liga o som do motor** de um veículo: ache a função que faz `PostEvent` do motor (play_engine / equivalente), o game object Wwise do carro (`RegisterGameObj`) e o RTPC `engine_rpm` (`SetRTPCValue`). Dê RVAs e o objeto C++ dono (componente de áudio do veículo).
2. **Quem para/desregistra esse som**: procure `StopAll`, `StopPlayingID`, `ExecuteActionOnEvent`, `UnregisterGameObj`, `SetGameObjectAuxSendValues`, `SetState/SetSwitch`, `SetBusConfig/mute`, e quem os chama no caminho do dano terminal (setter `0x140744460` modo 4, handlers `TerminalDamageMessage`/`UnrecoverableCar`/`ResetCar`, `StateEndRace`, cutscenes de fim).
3. **Quem deveria recriar/religar** no Reiniciar: o Reiniciar recria o carro ou só reposiciona? Se só reposiciona, qual flag/estado do componente de áudio fica "parado" e quem o rearma na especial nova (compare com o caminho de Reiniciar sem crash).
4. Explique o `+0x556` (motivo) e o byte `CutsceneManager+0x555` em relação ao áudio, e o efeito de `0x1415b4bd9`.
5. Proponha um **reparo mínimo chamável de dentro do processo** (uma ou duas funções do jogo, com argumentos) para religar som do motor e direção após o Reiniciar, e **um log/leitura barata** para validar antes (ex.: estado do game object, playing IDs, valor do RTPC).

## Formato
- Separe **FATO** (visto no código, com RVA e trecho de disassembly) de **HIPÓTESE**.
- Dê RVAs absolutos (0x14....) e nome provável da função.
- Se faltar dado, diga exatamente qual leitura/dump ao vivo eu devo fazer (endereço + tamanho) em vez de chutar.
