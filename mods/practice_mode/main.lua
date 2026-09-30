-- ========================================================================
-- DR2 Hook: Practice Mode (Practice / Savestate Mod)
-- ========================================================================

local savedState = nil
local restoreModes = { "Normal", "Momentum" }

local function notify(text, duration)
    if Menu.get("notifications") then
        UI.notify(text, duration)
    end
end

local function saveCheckpoint()
    if Safety.isRestrictedMode() then
        notify("Savestate blocked in competitive/official modes!", 3.0)
        return
    end

    savedState = Player.getState()
    if savedState ~= nil then
        notify("Checkpoint saved!", 2.0)
        print(string.format("[Practice Mode] Checkpoint saved at: (%.2f, %.2f, %.2f)",
            savedState.position.x, savedState.position.y, savedState.position.z))
    else
        notify("Save failed: vehicle unavailable!", 2.5)
        print("[Practice Mode] Failed to capture vehicle state.")
    end
end

-- mode: "normal" (stationary) or "momentum"
local function restoreCheckpoint(mode)
    if Safety.isRestrictedMode() then
        notify("Restore blocked in competitive modes!", 3.0)
        return
    end

    if savedState == nil then
        notify("No checkpoint saved yet! Press F5 first.", 2.5)
        print("[Practice Mode] No saved checkpoint available.")
        return
    end

    local label = mode == "momentum" and "With Momentum" or "Normal"
    if Player.setState(savedState, mode) then
        notify("Returning to checkpoint (" .. label .. ")...", 1.5)
        print("[Practice Mode] Checkpoint restored (" .. mode .. ").")
    else
        notify("Failed to restore checkpoint in memory!", 2.5)
        print("[Practice Mode] Failed to apply vehicle state (" .. mode .. ").")
    end
end

local function selectedRestoreMode()
    local _, value = Menu.get("restore_mode")
    return value == "Momentum" and "momentum" or "normal"
end

local function comingSoon(feature)
    return function(enabled)
        notify(feature .. (enabled and " enabled" or " disabled") .. " (coming soon)", 2.5)
    end
end

-- Native menu: Pause > DR2 Hook > Mods > Practice Mode
Menu.button("save_checkpoint", "Save checkpoint", saveCheckpoint)
Menu.button("restore_checkpoint", "Restore checkpoint", function()
    restoreCheckpoint(selectedRestoreMode())
end)
Menu.choice("restore_mode", "Restore mode", restoreModes, 1)
Menu.toggle("clear_on_stage_start", "Clear checkpoint on new stage", true)
Menu.toggle("indestructible_tyres", "Indestructible tyres", false,
    comingSoon("Indestructible tyres"))
Menu.toggle("indestructible_car", "Indestructible car", false,
    comingSoon("Indestructible car"))
Menu.toggle("notifications", "Notifications", true)

function onInit()
    print("[Practice Mode] Loaded! Press F5 to save checkpoint, F6 to restore, F7 for momentum.")
end

function onStageStart(stage)
    if Menu.get("clear_on_stage_start") then
        savedState = nil
    end
    print("[Practice Mode] New stage started: " .. stage.name)
end

function onKeyDown(keyCode)
    if keyCode == 0x74 then     -- F5: save car position and momentum
        saveCheckpoint()
    elseif keyCode == 0x75 then -- F6: restore stationary
        restoreCheckpoint("normal")
    elseif keyCode == 0x76 then -- F7: restore with full momentum
        restoreCheckpoint("momentum")
    end
end
