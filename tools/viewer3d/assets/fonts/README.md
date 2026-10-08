# Fontes do editor

O viewer3d embute estas fontes no executável: o CMake lê os `.ttf` e gera `embedded_fonts.cpp` no build. Assim nada depende das fontes do sistema.

| Arquivo | Origem | Licença |
| --- | --- | --- |
| `Inter-Regular.ttf`, `Inter-SemiBold.ttf` | Inter 4.1, `Inter-4.1.zip` das [releases](https://github.com/rsms/inter/releases/tag/v4.1) (`extras/ttf/`) | SIL Open Font License 1.1 ([`LICENSE-Inter.txt`](LICENSE-Inter.txt)) |
| `Lucide.ttf` | Lucide 1.53.0, pacote npm [`lucide-static`](https://www.npmjs.com/package/lucide-static) (`font/lucide.ttf`) | ISC, com parte dos ícones em MIT (Feather) ([`LICENSE-Lucide.txt`](LICENSE-Lucide.txt)) |

As três são recortes feitos pelo [`subset_fonts.py`](subset_fonts.py) (fontTools):

- **Inter**: só o texto dos painéis (Latim-1, aspas, travessões, setas e o sinal de menos);
- **Lucide**: só os ícones que o editor usa.

O script também gera `src/app/icons.hpp`, com as macros `ICON_*` e as faixas que o ImGui lê. Para acrescentar um ícone ([catálogo](https://lucide.dev/icons/)), ponha o nome dele em `ICONS` no script e rode-o de novo com as fontes originais extraídas:

```bash
python3 tools/viewer3d/assets/fonts/subset_fonts.py --inter <pasta do Inter-4.1.zip> --lucide <pasta package/ do lucide-static>
```
