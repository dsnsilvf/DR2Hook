-- ========================================================================
-- DR2 Hook: Debug Mode
-- ========================================================================
-- Opcoes de pesquisa que saíram do Practice Mode: quantos carros fantasma o
-- jogo desenha, o fantasma solido e as teclas F9 (camera livre) e F11 (insta crash).

-- "Off" = so o fantasma do jogo; 2..15 = o original mais copias (15 fantasmas +
-- jogador = 16 carros, o maximo estavel; ghosts.md §6.8).
local ghostCars = { "Off" }
for n = 2, 15 do
    ghostCars[#ghostCars + 1] = tostring(n)
end
local ghostSpacing = { "1 s", "2 s", "5 s", "10 s" }

local function seconds(label)
    return tonumber((label or "0"):match("([+-]?%d+)")) or 0
end

local function selectedGhostCars()
    local _, value = Menu.get("ghost_cars")
    return tonumber(value) or 0
end

-- O numero de carros vale na proxima carga da especial; as copias que cabem nos
-- carros que o jogo ja criou entram agora (e "Off" tira as copias na hora).
local function applyGhostCars()
    local cars = selectedGhostCars()
    local _, spacing = Menu.get("ghost_spacing")
    Ghost.setCars(cars)
    Ghost.clone(cars >= 2 and cars - 1 or 0, seconds(spacing))
end

-- Native menu: Pause > DR2 Hook > Mods > Debug Mode
Menu.choice("ghost_cars", "Ghost cars on screen", ghostCars, 1,
    function() applyGhostCars() end)
Menu.choice("ghost_spacing", "Ghost spacing", ghostSpacing, 1,
    function() applyGhostCars() end)
Menu.toggle("solid_ghost", "Solid ghost car", false,
    function(enabled) Ghost.setOpaque(enabled) end)
Menu.toggle("free_camera_key", "Free camera key (F9)", true,
    function(enabled) Debug.setHotkey("free_camera", enabled) end)
Menu.toggle("insta_crash_key", "Insta crash key (F11)", true,
    function(enabled) Debug.setHotkey("insta_crash", enabled) end)

Menu.describe("ghost_cars",
    "How many ghost cars to draw, all copies of the loaded ghost, each one further behind.\n\n"
    .. "*Off:* only the game's own ghost.\n"
    .. "*2 to 15:* the ghost plus copies. 15 ghosts and your car are the most the game can draw.\n\n"
    .. "Needs a ghost loaded. A new count takes effect the next time the stage is loaded "
    .. "from the menu; until then only the copies that fit are shown.")
Menu.describe("ghost_spacing", "Time between each ghost car.")
Menu.describe("solid_ghost",
    "Draws the ghost as a normal, solid car with shadows. "
    .. "Takes effect when the stage is loaded or restarted.")
Menu.describe("free_camera_key",
    "Lets *F9* turn the free camera on. When off, F9 goes to the game, "
    .. "but it can still turn a free camera that is already on off.")
Menu.describe("insta_crash_key",
    "Lets *F11* destroy the car at once (terminal-damage research, offline only). "
    .. "When off, F11 goes to the game.")

function onInit()
    Ghost.setOpaque(Menu.get("solid_ghost"))
    Debug.setHotkey("free_camera", Menu.get("free_camera_key"))
    Debug.setHotkey("insta_crash", Menu.get("insta_crash_key"))
    applyGhostCars()
    local _, cars = Menu.get("ghost_cars")
    print("[Debug Mode] Loaded. Ghost cars on screen: " .. cars .. ".")
end

function onStageStart(stage)
    applyGhostCars() -- o jogo recria os slots a cada especial
end
