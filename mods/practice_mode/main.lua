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

-- Largada no reinicio. Os nomes do menu dizem o que acontece; Race.* usa os
-- ids internos. Reaplicada a cada especial, quando o modo da sessao ja e
-- conhecido; `quiet` evita avisos fora da especial.
local startModes = { "Normal", "No countdown", "Automatic", "On throttle" }
local startModeIds = {
    ["Normal"] = "normal",
    ["No countdown"] = "no_countdown",
    ["Automatic"] = "automatic",
    ["On throttle"] = "on_throttle",
}

local function applyStartMode(label, quiet)
    local mode = startModeIds[label] or "normal"
    if mode ~= "normal" and Safety.isRestrictedMode() then
        if not quiet then
            notify("Race start options blocked in competitive modes!", 3.0)
        end
        mode = "normal"
    end
    if not Race.setStartMode(mode) and mode ~= "normal" and not quiet then
        notify("Race start options unavailable (hook not loaded).", 3.0)
    end
end

local function selectedStartMode()
    local _, value = Menu.get("race_start")
    return value
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
Menu.choice("race_start", "Race start", startModes, 1,
    function(_, value) applyStartMode(value, true) end)
Menu.toggle("indestructible_tyres", "Indestructible tyres", false,
    comingSoon("Indestructible tyres"))
Menu.toggle("indestructible_car", "Indestructible car", false,
    comingSoon("Indestructible car"))
Menu.toggle("notifications", "Notifications", true)

-- Texto do painel da direita para a linha em foco.
Menu.describe("save_checkpoint",
    "Saves the car's position, rotation and speed right now. Shortcut: F5.")
Menu.describe("restore_checkpoint",
    "Puts the car back at the saved checkpoint, using the restore mode below. "
    .. "Shortcuts: F6 (normal) and F7 (with momentum).")
Menu.describe("restore_mode",
    "Normal: the car comes back stopped, with the suspension settled.\n"
    .. "Momentum: it keeps the speed and spin it had when saved.")
Menu.describe("clear_on_stage_start",
    "Forgets the checkpoint when a new stage starts. Restarting the same stage keeps it.")
Menu.describe("race_start",
    "{v}How every start works, restarts included.\n\n"
    .. "{s:_22_din_bold}Normal: {s:_22_roboto_cnd}hold the handbrake and wait for the 5 lights.\n"
    .. "{s:_22_din_bold}No countdown: {s:_22_roboto_cnd}hold the handbrake and go at once.\n"
    .. "{s:_22_din_bold}Automatic: {s:_22_roboto_cnd}go as soon as the car is on the line.\n"
    .. "{s:_22_din_bold}On throttle: {s:_22_roboto_cnd}go when you press the throttle.")
Menu.describe("indestructible_tyres", "Coming soon: tyres that never wear or puncture.")
Menu.describe("indestructible_car", "Coming soon: no damage to the car.")
Menu.describe("notifications", "Shows on-screen messages for checkpoints and race start options.")

function onInit()
    applyStartMode(selectedStartMode(), true)
    print("[Practice Mode] Loaded! Press F5 to save checkpoint, F6 to restore, F7 for momentum.")
end

function onStageLoad(stage)
    applyStartMode(selectedStartMode(), false)
end

function onStageStart(stage)
    if stage.restart then
        return
    end
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
