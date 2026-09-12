-- ========================================================================
-- DR2Hook: Practice Mode (Mod de Treino / Savestate)
-- ========================================================================

local savedState = nil

function onInit()
    print("[Practice Mode] Carregado! Pressione F5 para salvar o checkpoint e F6 para restaurar.")
end

function onStageStart(stage)
    -- Ao iniciar uma nova especial, limpa o checkpoint salvo anteriormente
    savedState = nil
    print("[Practice Mode] Nova especial iniciada: " .. stage.name)
end

function onKeyDown(keyCode)
    -- F5 (0x74): Salvar posição e inércia do carro
    if keyCode == 0x74 then
        if Safety.isRestrictedMode() then
            UI.notify("Savestate bloqueado em modos competitivos/oficiais!", 3.0)
            return
        end

        savedState = Player.getState()
        UI.notify("Checkpoint salvo!", 2.0)
        print(string.format("[Practice Mode] Checkpoint gravado em: (%.2f, %.2f, %.2f)", 
            savedState.position.x, savedState.position.y, savedState.position.z))

    -- F6 (0x75): Restaurar posição e velocidade
    elseif keyCode == 0x75 then
        if Safety.isRestrictedMode() then
            UI.notify("Restauração bloqueada em modos competitivos!", 3.0)
            return
        end

        if savedState ~= nil then
            Player.setState(savedState)
            UI.notify("Retornando ao checkpoint...", 1.5)
        else
            UI.notify("Nenhum checkpoint salvo ainda! Pressione F5 primeiro.", 2.5)
        end
    end
end
