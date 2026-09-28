-- ========================================================================
-- DR2 ModLoader: Practice Mode (Practice / Savestate Mod)
-- ========================================================================

local savedState = nil

function onInit()
    print("[Practice Mode] Loaded! Press F5 to save checkpoint, F6 to restore, F7 for momentum.")
end

function onStageStart(stage)
    -- Clear saved checkpoint when a new stage begins
    savedState = nil
    print("[Practice Mode] New stage started: " .. stage.name)
end

function onKeyDown(keyCode)
    -- F5 (0x74): Save car position and momentum
    if keyCode == 0x74 then
        if Safety.isRestrictedMode() then
            UI.notify("Savestate blocked in competitive/official modes!", 3.0)
            return
        end

        savedState = Player.getState()
        if savedState ~= nil then
            UI.notify("Checkpoint saved!", 2.0)
            print(string.format("[Practice Mode] Checkpoint saved at: (%.2f, %.2f, %.2f)", 
                savedState.position.x, savedState.position.y, savedState.position.z))
        else
            UI.notify("Save failed: vehicle unavailable!", 2.5)
            print("[Practice Mode] Failed to capture vehicle state.")
        end

    -- F6 (0x75): Restore position normally (stationary)
    elseif keyCode == 0x75 then
        if Safety.isRestrictedMode() then
            UI.notify("Restore blocked in competitive modes!", 3.0)
            return
        end

        if savedState ~= nil then
            local ok = Player.setState(savedState, "normal")
            if ok then
                UI.notify("Returning to checkpoint (Normal)...", 1.5)
                print("[Practice Mode] Checkpoint restored normally.")
            else
                UI.notify("Failed to restore checkpoint in memory!", 2.5)
                print("[Practice Mode] Failed to apply vehicle state.")
            end
        else
            UI.notify("No checkpoint saved yet! Press F5 first.", 2.5)
            print("[Practice Mode] No saved checkpoint available.")
        end

    -- F7 (0x76): Restore with full momentum
    elseif keyCode == 0x76 then
        if Safety.isRestrictedMode() then
            UI.notify("Restore blocked in competitive modes!", 3.0)
            return
        end

        if savedState ~= nil then
            local ok = Player.setState(savedState, "momentum")
            if ok then
                UI.notify("Returning to checkpoint (With Momentum)...", 1.5)
                print("[Practice Mode] Checkpoint restored with momentum.")
            else
                UI.notify("Failed to restore checkpoint in memory!", 2.5)
                print("[Practice Mode] Failed to apply vehicle state with momentum.")
            end
        else
            UI.notify("No checkpoint saved yet! Press F5 first.", 2.5)
            print("[Practice Mode] No saved checkpoint available.")
        end
    end
end
