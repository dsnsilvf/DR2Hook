# ImGuizmo (cópia para o viewer3d)

- Origem: <https://github.com/CedricGuillemet/ImGuizmo>, master depois da tag 1.10 (commit `18cef5e031d8c6973d80284c67f60549fafd78c1`, 2026-08-08). Esse commit já inclui a correção do tremor no raio do mouse (#427).
- Licença: MIT (`LICENSE`).
- Arquivos copiados sem mudança: `src/ImGuizmo.cpp` e `src/ImGuizmo.h`. O resto do repositório (sequenciador, curvas, editor de grafos) não é usado.
- É compilado junto com o ImGui, no alvo `imgui` do `CMakeLists.txt`.

O editor usa o `Manipulate` para mover e girar a seleção. Ele cuida do arraste preso ao eixo pelo raio do mouse, dos quadrados de plano, do modo mundo/objeto e do texto com o quanto andou. A ligação com a pista fica em `src/app/ui.cpp`, na seção `---- Gizmo`, e cobre o histórico, o encaixe na grade e o grudar no chão.

Para atualizar, copie os dois arquivos de um commit novo e atualize esta nota.
