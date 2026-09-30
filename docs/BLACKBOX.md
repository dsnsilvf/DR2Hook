# Caixa-preta do veículo

Ferramenta de pesquisa offline para responder, com uma sessão real, como o DiRT Rally 2.0 guarda o estado do veículo. O recorder só observa. O analyzer mede. Nome físico só aparece quando já era âncora confirmada.

Não escreve na memória do jogo. Não mexe em multiplayer, RaceNet, placar ou sessão competitiva. A telemetria UDP, quando ligada, é só recepção local do que o jogo já envia.

Depende de Python 3 e NumPy. No Linux / Proton a leitura usa `process_vm_readv`.

```bash
python3 -m tools.dr2rec --help
# ou
./scripts/dr2rec --help
```

## Captura

```bash
./scripts/dr2rec record \
    --process "DiRT Rally 2.0" \
    --interval 10ms \
    --rig 0x4B76BAB0 \
    --region physicsrig:0x0000:0x2600 \
    --udp capture.bin \
    --output session.dr2cap
```

`--interval` aceita `10`, `10ms`, `16ms`, `50ms` ou `0.05s`. Número puro é milissegundo. O relógio gravado é monotônico; a amostra não é um frame do jogo.

`--region physicsrig:INÍCIO:FIM` usa fim exclusivo. Pode repetir a opção. Sem `--region`, a captura fica só nas âncoras. O intervalo mínimo é 1 ms.

`--rig` fixa o endereço. Sem ele, cada amostra relê `dirtrally2.exe + 0x1681ce8` → `car + 0x30` → `container + 0x08`.

`--udp 20777` ou `--udp capture.bin` (porta padrão 20777) guarda o datagrama cru dentro do `.dr2cap`. O `.bin` é uma cópia paralela, com carimbo e tamanho antes de cada pacote. Não é obrigatório.

Ctrl+C encerra. A escrita vai numa thread separada. Se a fila encher, a amostra é contada como descarte em vez de travar o laço.

### Marcador

Uma linha no stdin vira marcador com esse rótulo. Linha vazia grava `EVENT_MARKER`. A tecla padrão é F8, se `/dev/input` for legível.

F8 no mod loader recarrega `dr2hook_core.dll`. Com a `dxgi.dll` carregada, use `--hotkey F9`.

O analyzer compara a janela antes e depois do marcador. Sem evidência física, a mudança de memória continua se chamando **memory transition**.

## Análise

```bash
./scripts/dr2rec analyze session.dr2cap
./scripts/dr2rec analyze session.dr2cap --exe /caminho/dirtrally2.exe
./scripts/dr2rec compare session_a.dr2cap session_b.dr2cap
./scripts/dr2rec inspect session.dr2cap --offset 0x1504
./scripts/dr2rec export session.dr2cap --output session.json
```

`analyze` escreve `session_report/`:

| Arquivo | Conteúdo |
| :--- | :--- |
| `report.md` / `report.html` | relatório |
| `changemap.txt` | `█` mudou, `─` ficou igual, por offset |
| `activity.tsv` | taxa, valores distintos, confiança |
| `analysis.json` | o mesmo resultado, para script |

O relatório traz resumo, verificação das âncoras, atividade, estruturas, eventos, correlação com lag, relações matemáticas, simetria, UDP, estados discretos, bits, ponteiros, data-flow, offsets interessantes, desconhecidos e o próximo experimento.

`compare` separa constante igual, constante diferente, dinâmico nas duas e dinâmico só numa sessão. É o jeito de separar setup, estado da sessão e diferença de carro: duas capturas, uma variável só.

`export` grava o hex das âncoras. O `.dr2cap` continua sendo a fonte. O JSON não substitui os bytes.

`--exe` procura o deslocamento imediato na seção `.text` e classifica o site como writer, reader ou desconhecido. A confiança disso fica em `POSSIBLE`. Não executa o jogo e não segue ponteiro.

## O que já é âncora

O analyzer não reabre o significado básico destes campos. A fonte é `SSOT.md` e `docs/reverse_engineering/` (suspensão, setup, rodas e pneus; o índice está em `docs/reverse_engineering/README.md`).

| Campo | Onde |
| :--- | :--- |
| Quatérnion do chassi | `+0x2E0` |
| Right, Up, Forward | `+0x2F0`, `+0x300`, `+0x310` |
| Curso, UDP = `float32(valor × 1000)` | `+0x1504 + i·0x420` |
| Ponto do eixo no chassi | `+0x1480 + i·0x420` |
| Escalar vertical de setup | `+0x14E8 + i·0x420` |
| Ponto com e sem o termo de suspensão | `+0x1660` e `+0x1670`, mais `i·0x420` |
| Up compartilhado da roda | `+0x1680 + i·0x420` |
| Estado do pneu e da roda | `+0x2cf0 + i`. `0` intacto, `1` furado, `2` só o aro, `3` roda solta |

A ordem é RL, RR, FL, FR. O passo é `0x420`. O `+0x000` do bloco da roda é esse Up em `rig + 0x1680`, não o primeiro byte do PhysicsRig.

Campos já documentados no SSOT, como marcha em `+0x1448` e velocidade em `+0x320`, entram no relatório como documentados. Não voltam como descoberta.

O byte por roda em `+0x2cf0` está em `docs/reverse_engineering/tyres.md`. Ele fica fora de `physicsrig:0x0000:0x2600`. Para a sessão incluí-lo:

```bash
--region physicsrig:0x0000:0x2600 --region physicsrig:0x2C00:0x2E00
```

`docs/reverse_engineering/tyres.md` fecha o significado observado desse byte: `0` intacto, `1` furado, `2` só o aro, `3` roda solta. O nome do campo não está no executável. O analyzer compara o byte; não reabre esses quatro valores.

## Formato `.dr2cap`

Arquivo little-endian, magic `DR2CAP`, versão 1.

1. Cabeçalho de 64 bytes: versão, quantidade de amostras, intervalo, ponteiros para o metadado e para os registros.
2. JSON: executável, pid, base, PhysicsRig, intervalo, regiões, âncoras, porta UDP, mapa de memória do início da captura.
3. Registros de amostra, marcador e fim.

Cada amostra guarda timestamp monotônico, sequência, ponteiro do rig, status, bytes crus das regiões, bytes crus das âncoras e o datagrama UDP cru, se houver. Nada é convertido para float na gravação. O analyzer é que experimenta `u8`, `i32`, `f32`, `f64`, ponteiro e bit.

Uma região é `physicsrig:início:fim`. O limite de uma região é 1 MiB. Ponteiro encontrado na análise não é seguido.

## Confiança

Só estes rótulos:

`CONFIRMED`, `STRONG EVIDENCE`, `PROBABLE`, `POSSIBLE`, `UNKNOWN`.

`CONFIRMED` fica nas âncoras que a sessão reproduz. Igualdade exata nova, em todas as amostras, chega no máximo a `STRONG EVIDENCE`. Correlação alta sem igualdade não vira identidade. Bit que liga durante uma memory transition não vira nome de flag.

## Teste

```bash
python3 -m unittest tools.dr2rec.tests.test_blackbox -v
```

O teste monta uma sessão sintética, confere o byte cru e exige que o relatório meça simetria, lag, igualdade com UDP e estado discreto sem inventar nome de peça.
