> Relatório bruto do Gemini 3.8 Flash (agy), 2026-10-02. Chave, modo e contêiner conferidos por mim (ver ../../ghosts.md §1); o resto não foi revisado linha a linha.

# Relatório de Reconstrução da Cifra de Saves do DiRT Rally 2.0

## 1. Parâmetros da Cifra

- **Algoritmo**: AES-256
- **Modo de Operação**: ECB (Electronic Codebook)
- **IV**: Nenhum / Não aplicável (modo ECB opera bloco a bloco de 16 bytes de forma independente)
- **Chave AES (32 bytes / 256 bits, hex)**:
  `91d84b7138a2cc4dadc022db4ebd1edd6c3454746acb235b618b404170b86e71`

---

## 2. Rastreamento e Provas por Assembly

### Passo 1: Hash da string de semente (`0x140527157..0x140527177`)
- Em `0x140527157`: `mov edx, 3` e `lea rcx, "rp17"` (`0x141284668`).
- Em `0x140527163`: `call 0x14009f4d0`. A função é um hash recursivo FNV-1a (base `0x811c9dc5`, primo `0x1000193`). Com tamanho 3, consome os bytes de `"rp1"` e retorna `0x89d91d4a`.
- Em `0x140527168`: `xor eax, 0x37`. O valor `0x37` é o caractere ASCII `'7'`.
- Em `0x140527171`: `imul edx, eax, 0x1000193`. Isso completa exatamente o hash FNV-1a dos 4 caracteres `"rp17"`, resultando no inteiro de 32 bits:
  $$\text{seed} = \mathtt{0x7dc96bc7}$$

### Passo 2: Derivação da chave AES (`0x14080f360` e `0x140815510`)
- `0x14052716b`: `mov r8d, 2` passa o seletor de tamanho de chave (`0` = 128 bits, `1` = 192 bits, `2` = 256 bits).
- `0x14052717a`: `call 0x14080f360`. A função executa:
  1. `0x14080f385`: `call 0x140859b80(&prng_state, 0x7dc96bc7)`. Inicializa um gerador pseudoaleatório Mersenne Twister MT19937 com a fórmula LCG de Knuth: $mt[0] = \text{seed} \mid 1 = \mathtt{0x7dc96bc7}$, $mt[i] = (mt[i-1] \times 69069) \pmod{2^{32}}$, $mti = 624$.
  2. Loop de 64 iterações (`rdi = 0x40`) em `0x14080f3a0..0x14080f3c6`: chama `0x140866180`, que invoca `0x14086c730` (MT19937 twist + tempering padrão) e calcula `val = rand % 15`. Converte cada valor $0..14$ para caractere ASCII hexadecimal maiúsculo (`'0'`-`'9'`, `'A'`-`'E'`).
  3. A string hexadecimal de 64 caracteres resultante é:
     `"91D84B7138A2CC4DADC022DB4EBD1EDD6C3454746ACB235B618B404170B86E71"`
  4. Em `0x14080f400`: `call 0x140815510(cipher, 2, hex_str)`. Com `edx = 2`, define `[cipher + 0x10] = 0x100` (256 bits) e faz o parsing de cada par de caracteres hexadecimais para 32 bytes binários.
  5. Em `0x140815668`: define 14 rodadas (`0xe`) e chama a expansão de chave do AES-256 (`0x140805f10`).

### Passo 3: Modo de operação e IV
- Em `0x1407fbd40..0x1407fbdfc`: O campo `[obj + 0x30]` determina a direção da cifra:
  - `[obj + 0x30] == 0`: Decriptação. Seleciona `0x140806e00`/`0x140806a50` (AES-NI usando instruções `aesdec` / `aesdeclast`) ou rotinas de software `0x14080bae0`/`0x14080b900` (T-tables inversas Td).
  - `[obj + 0x30] == 1`: Encriptação. Seleciona `0x1408066a0`/`0x140806240` (AES-NI usando `aesenc` / `aesenclast`) ou rotinas de software `0x14080b520`/`0x14080b290` (T-tables Te).
- Em `0x1405271b9`: O valor `0x10` (16 bytes) é passado para `0x140cc3910` representando o tamanho de bloco do AES.
- Todos os arquivos decifram sem vetor de inicialização ou encadeamento (ECB puro). O primeiro bloco decifrado de todos os saves binários é idêntico (`04000000 18000000 02000000 00000000`), e os blocos subsequentes descomprimem com zlib sem corrupção de fluxo.

---

## 3. Estrutura dos Arquivos de Save

Os arquivos salvos possuem as seguintes camadas:
1. **Camada de Cifra**: AES-256-ECB sobre o arquivo completo (tamanho múltiplo de 16).
2. **Cabeçalho de Contêiner (24 bytes)**:
   - `0..3` (uint32): Versão do contêiner (`0x00000004`)
   - `4..7` (uint32): Tamanho do cabeçalho (`0x00000018` = 24 bytes)
   - `8..11` (uint32): Tipo de compressão (`0x00000002` = zlib / deflate)
   - `12..15` (uint32): Reservado (`0x00000000`)
   - `16..23` (uint64): Tamanho dos dados descomprimidos
3. **Payload Externo Descomprimido**: Serialização EGO com tag raiz `0x4ebbdd37` (`37 dd bb 4e`).
4. **Fantasma (Ghost) Interno**: Nos saves de fantasma (`.BWW` / `.KYK`), o contêiner armazena uma sub-stream zlib que, ao ser descomprimida, inicia com o magic `GHST` (`0x54534847`), correspondendo à verificação do desserializador em `0x1409d10e9`.
5. **Arquivos de Descrição (`#QKRHMYXE`)**: Contêm diretamente texto legível ASCII: `"Save System 2 Demo - Display Name"`.

---

## 4. Saída do Teste de Decifração (`decrypt_save.py`)

Amostras dos primeiros 64 bytes (hex) decifrados:

### Fantasma 1: `savegame@ghosts#ENDFX-0.BWW`
- **Contêiner decifrado (primeiros 64 bytes hex)**:
  `0400000018000000020000000000000096520000000000007801749b5370264ed0ee63dbb6b9b16ddbb6b31b3b6f6cdbc9c6b6ad8df1c6b66debfc4f7ddfc5b9`
- **Payload interno `GHST` descomprimido (primeiros 64 bytes hex)**:
  `4748535467760000077f5a0200000e9c040000048a040000049f000000045a02000008010000000462000000146e000004333336340204000000000104a3e5a9`
  *(Magic: `GHST`)*

### Fantasma 2: `savegame@ghosts#ENDFX-1.BWW`
- **Contêiner decifrado (primeiros 64 bytes hex)**:
  `040000001800000002000000000000009b000100000000007801749a43902e5ccca0dbb66ddbb66df3b6dfb6cd7bdbb66ddbb66ddbb6355fcdff6f66319b5405`
- **Payload interno `GHST` descomprimido (primeiros 64 bytes hex)**:
  `47485354bd6e0100077f800700000e040e000004810e000004f80100000480070000080100000004fd000000147e0000043238393702040000000001047b5091`
  *(Magic: `GHST`)*

### Perfil: `savegame@profile#NXDSMWW.BWW`
- **Contêiner decifrado (primeiros 64 bytes hex)**:
  `0400000018000000020000000000000000912800000000007801ecbd655c565bdbf50d2a282658d8ddd88a8add22b6a2d8dd89812d360676b7a26261778b1b03`
  *(Contêiner zlib válido com tamanho descomprimido de 2.658.560 bytes e tag EGO `0x4ebbdd37`)*

---

## 5. Separação: Verificado vs. Hipótese

### Verificado
1. **Cálculo da semente**: `0x14009f4d0` calcula o FNV-1a de `"rp1"` (`0x89d91d4a`), que com `^ 0x37` e `* 0x1000193` resulta deterministicamente em `0x7dc96bc7`.
2. **Gerador de chave**: MT19937 com semente `0x7dc96bc7 | 1` e LCG Knuth gera a sequência de 64 nibbles `rand % 15`, formando a string ASCII `"91D84B7138A2CC4DADC022DB4EBD1EDD6C3454746ACB235B618B404170B86E71"`.
3. **Chave AES**: Conversão direta dos pares hexadecimais para 32 bytes de chave AES-256 (`91d84b7138a2cc4dadc022db4ebd1edd6c3454746acb235b618b404170b86e71`), com 14 rodadas de expansão (`0x140805f10`).
4. **Modo e IV**: AES-256-ECB sem IV. Todos os arquivos em `saves/` decifram para dados estruturados válidos.
5. **Payload GHST**: Todos os 15 saves de ghost contêm internamente o stream descomprimido que inicia com `GHST` (`0x54534847`), compatível com `0x1409d10e9`.

### Hipótese
1. A geração de nibbles com `% 15` em vez de `% 16` (`lea r8d, [rdx + 0xf]` com `div edi` em `0x140866180`) provavelmente foi um bug de implementação do desenvolvedor (off-by-one em `rand_range(min, max)`), impedindo a geração do caractere `'F'` na chave gerada.
