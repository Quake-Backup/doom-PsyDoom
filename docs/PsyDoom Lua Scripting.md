# PsyDoom Lua Scripting
Table of contents:
- [Overview](#Overview)
    - [Capabilities](#Capabilities)
    - [Limitations](#Limitations)
- [Setting up a basic Lua script](#Setting-up-a-basic-Lua-script)
- [More complicated scripting examples](#More-complicated-scripting-examples)
    - [Custom floor mover with texture change on finish](#Custom-floor-mover-with-texture-change-on-finish)
    - [Spawn an Imp's fireball from one marker towards another](#Spawn-an-Imps-fireball-from-one-marker-towards-another)
- [Available script execution hooks](#Available-script-execution-hooks)
    - [Scripted linedef special types](#Scripted-linedef-special-types)
    - [Scripted sector special types](#Scripted-sector-special-types)
- [Available Lua APIs](#Available-Lua-APIs)
- [PsyDoom Lua API](#PsyDoom-Lua-API)
    - [Types](#Types)
        - [sector_t](#sector_t)
        - [line_t](#line_t)
        - [side_t](#side_t)
        - [mobj_t](#mobj_t)
        - [player_t](#player_t)
        - [CustomFloorDef](#CustomFloorDef)
        - [CustomPlatDef](#CustomPlatDef)
        - [CustomCeilingDef](#CustomCeilingDef)
        - [CustomDoorDef](#CustomDoorDef)
    - [Functions](#Functions)
        - [Miscellaneous](#Miscellaneous)
        - [Delayed and repeating action scheduling](#Delayed-and-repeating-action-scheduling)
        - [Sectors](#Sectors)
        - [Lines](#Lines)
        - [Sides](#Sides)
        - [Things](#Things)
        - [Players](#Players)
        - [Script execution context](#Script-execution-context)
        - [Moving floors, ceilings and platforms](#Moving-floors-ceilings-and-platforms)
        - [Sounds](#Sounds)
    - [Constants (Avoid modifying these!)](#Constants-Avoid-modifying-these)
        - [Return values for `T_MoveFloor` and `T_MoveCeiling`](#Return-values-for-T_MoveFloor-and-T_MoveCeiling)
        - [Power types](#Power-types)
        - [Card types](#Card-types)
        - [Weapon types](#Weapon-types)
        - [Ammo types](#Ammo-types)
        - [Sector flags](#Sector-flags)
        - [Line flags](#Line-flags)
        - [Thing flags](#Thing-flags)
        - [Thing types (built-in)](#Thing-types-built-in)
        - [Sound ids (built-in)](#Sound-ids-built-in)

# Overview
As an extended modding feature, PsyDoom features a limited Lua scripting interface (currently Lua 5.4) to allow for more control over line and sector specials. This feature is deliberately limited in scope and mostly provides additional flexibility for events and interactive objects in the map. It was inspired somewhat by the Doom 64 engine's 'Macro' scripting system.

## Capabilities
- The ability to trigger scripted actions for switches, line triggers etc.
- More control over timing and the exact behavior of ceilings, platforms and floors.
- The ability to spawn custom sector specials (e.g custom line scroll)

## Limitations
- Writing to global variables is **completely disallowed**. This restriction is made to keep level data easily serializable. If you need a global data store however, you can work around this limitation by encoding data in sector fields (e.g. floor or ceiling height). Note: due to Lua limitations you may find it still possible to accidentally write to 'constants'. This should be avoided as it will break both scripts and serialization.
- Just one script is allowed per map.
- Scripts cannot add new enemies or object types or change enemy behavior.
- Only a limited subset of the game's API and data structures are exposed to scripting.

# Setting up a basic Lua script
1. A `SCRIPTS` lump must first be added to the `MAP[XX].WAD` file. In order to work well with PSX Doom Builder this should be placed *before* the map number marker; failure to do so may result in the map format not being recognized. For example, if placing a script in `MAP01.WAD` then the lump order should look like this:
```
SCRIPTS
MAP01
THINGS
LINEDEFS
...
```
2. A script file consists of a series of actions defined using the function `SetAction`. When PsyDoom first loads the script, it executes the entire file and all calls to `SetAction`. This builds a list of scripted actions that the engine can trigger via line and sector specials. *Note: once this initial execution phase is over `SetAction` becomes unavailable and no other actions can be registered.*
3. The `SetAction` function takes 2 parameters, the first being the number of the action. This number is referenced by line and sector specials (via their 'tag' fields) and is used to identify which action to execute. The 2nd parameter is a Lua function containing the logic to execute for the action. Here is a very basic example defining action `1` which prints "Hello World" to the console (if PsyDoom is launched via a terminal):
```
SetAction(1, function()
    print("Hello world!")
end)
```
4. In order to trigger the action in-game, use a PsyDoom specific 'scripted' line or sector special. For a line special the line tag identifies which action number to execute. For a sector special the sector tag identifies which action to execute. For example, assigning action `300` to a line (`W1 Do Script Action (Player only)`) and setting the line tag to `1` will cause script action `1` to be executed one time only, when the player walks over the line.
# More complicated scripting examples
## Custom floor mover with texture change on finish
```
-- When this action is triggered, move the floor for each sector with tag '2' to height -100.
-- When each floor is done moving, action '101' will be called for that floor.
SetAction(100, function()
    ForEachSectorWithTag(2, function(sector)
        local floor = CustomFloorDef.new()      -- Custom floor def is initialized with default settings
        floor.destheight = -100                 -- This field MUST be specified
        floor.speed = 1                         -- Make it slow
        floor.dofinishscript = true             -- Call action 101 when done
        floor.finishscript_actionnum = 101
        floor.stopsound = sfx_swtchn            -- Play a switch activate sound when done
        EV_DoCustomFloor(sector, floor)         -- Try and get the floor moving
    end)
end)

-- Changes the texture of the floor that just finished moving to water
SetAction(101, function()
    local sector = GetTriggeringSector()
    sector.floorpic = R_FlatNumForName("WATER01")   -- Note: this texture must already be loaded for the map
end)
```
## Spawn an Imp's fireball from one marker towards another
```
-- Find the MARKER1 and MARKER2 things in the first sector found with tag 1 and 2 respectively.
-- Then spawn an Imp's fireball from the 1st marker in the direction towards the 2nd.
SetAction(100, function()
    local srcSector = FindSectorWithTag(1)
    local dstSector = FindSectorWithTag(2)
    local srcMarker = srcSector:FindMobjWithType(MT_MARKER1)
    local dstMarker = dstSector:FindMobjWithType(MT_MARKER2)
    P_SpawnMissile(srcMarker, dstMarker, MT_TROOPSHOT)
end)
```

# Available script execution hooks
## Scripted linedef special types
Below is a list of PsyDoom specific line special numbers which trigger scripted actions. The prefix `W` means walk over the line, `S` means use as a switch, `G` means shoot and `D` means use as a door. The suffix `1` means once, and `R` means repeatable. For each of these line specials, the line tag indicates which scripted action number to execute.
- 300: W1 Do Script Action (Player only)
- 301: W1 Do Script Action (Monsters only)
- 302: W1 Do Script Action (Player + Monsters)
- 310: WR Do Script Action (Player only)
- 311: WR Do Script Action (Monsters only)
- 312: WR Do Script Action (Player + Monsters)
- 320: S1 Do Script Action (Player only)
- 330: SR Do Script Action (Player only)
- 340: G1 Do Script Action (Player only)
- 341: G1 Do Script Action (Monsters only)
- 342: G1 Do Script Action (Player + Monsters)
- 350: GR Do Script Action (Player only)
- 351: GR Do Script Action (Monsters only)
- 352: GR Do Script Action (Player + Monsters)
- 360: D1 Scripted Door (Player only)
- 361: D1 Scripted Door (Monsters only)
- 362: D1 Scripted Door (Player + Monsters)
- 370: DR Scripted Door (Player only)
- 371: DR Scripted Door (Monsters only)
- 372: DR Scripted Door (Player + Monsters)
- 380: Scripted Spawn Line Special
    - Executes once during map loading for the affected lines. It allows for custom effects to be spawned.

## Scripted sector special types
Below is a list of PsyDoom specific sector special numbers which trigger scripted actions. For each of these sector specials, the sector tag indicates which scripted action number to execute.
- 300: Scripted Sector Special Spawn
    - Executes once on startup for a sector. It allows for custom effects to be spawned or scheduled.
- 301: Scripted Player In Special Sector Action
    - Executes every game tick (15 times per second) that the player is in the sector.

# Available Lua APIs
1. PsyDoom provides a small subset of the `base` Lua library for scripts. Available functions are:
```
ipairs      select
tonumber    tostring
print       error
type
```
2. A small (portable) subset of the `math` library is also provided. Available functions are:
```
math.abs    math.tointeger
math.ceil   math.floor
math.min    math.max
math.fmod
```

# PsyDoom Lua API
## Types
Below is a list of all types exposed to Lua scripts. Note that all types are passed by reference in scripts, so changes to the object will always affect the underlying engine object.
### sector_t
```lua
-- Represents a sector in the game; only a useful subset of the datastructure is exposed
sector_t {
    int32  index    -- Index of the sector in the list of sectors for the level (ZERO BASED!)(readonly)

    --------------------------------------------------------------------------------------------------------------------
    -- IMPORTANT: if moving a floor or ceiling correctly is desired, use 'T_MoveCeiling' or 'T_MoveFloor'.
    -- Setting these values directly will NOT affect things in the sector.
    --------------------------------------------------------------------------------------------------------------------
    float   floorheight             -- Floor height (floating-point/real format)
    int32   floorheight_fixed       -- Floor height (16.16 integer fixed point format)
    float   ceilingheight           -- Ceiling height (floating-point/real format)
    int32   ceilingheight_fixed     -- Ceiling height (16.16 integer fixed point format)
    
    int32   floorpic        -- Floor flat number
    int32   ceilingpic      -- Ceiling flat number
    uint8   colorid         -- Sector light color index
    uint8   lightlevel      -- Sector light level (0-255)
    int32   special         -- Sector special number (0 if none)
    int32   tag             -- Sector tag
    uint32  flags           -- Sector bit flags
    uint8   ceil_colorid    -- Sector ceiling light color (PsyDoom extension, if '0' use normal sector light color)
    int32   numlines        -- Number of lines in the sector (readonly)
    bool    hasthinker      -- If 'true' then there is an effect (floor mover etc.) operating exclusively on the sector (readonly)
    
    -- Floor and ceiling texture x and y offsets: can be used to scroll textures
    float   floor_tex_offset_x
    float   floor_tex_offset_y
    float   ceil_tex_offset_x
    float   ceil_tex_offset_y

    GetLine(int32 index) -> line_t      -- Get a specific line in the sector or 'nil' if the index is out of range
    ForEachLine(function f)             -- Call function 'f' for each line in the sector, passing in a 'line_t' as a parameter
    ForEachMobj(function f)             -- Call function 'f' for each thing in the sector, passing in a 'mobj_t' as a parameter

    -- Return the first thing found with the specified type, or 'nil' if none found
    FindMobjWithType(uint32 type) -> mobj_t
    
    -- Iterates through all the 2 sided lines in the sector, calling the function 'f' with a single parameter of type 'sector_t'.
    -- The parameter contains the sector on the opposite side of the line to the current sector.
    ForEachSurroundingSector(function f)
    
    -- Get the minimum or maximum floor, ceiling or light level in surrounding sectors; if there are no surrounding sectors return 'defaultValue'
    GetLowestSurroundingFloor(float defaultValue) -> float
    GetLowestSurroundingCeiling(float defaultValue) -> float
    GetLowestSurroundingLightLevel(int32 defaultValue) -> int32
    GetHighestSurroundingFloor(float defaultValue) -> float
    GetHighestSurroundingCeiling(float defaultValue) -> float
    GetHighestSurroundingLightLevel(int32 defaultValue) -> int32
    
    -- Get the next floor, ceiling or light level in surrounding sectors which is lower or higher than the specified value.
    -- If no such value exists, return 'defaultValue'.
    GetNextLowestSurroundingFloor(float lowerThanValue, float defaultValue) -> float
    GetNextLowestSurroundingCeiling(float lowerThanValue, float defaultValue) -> float
    GetNextLowestSurroundingLightLevel(int32 lowerThanValue, int32 defaultValue) -> int32
    GetNextHighestSurroundingFloor(float higherThanValue, float defaultValue) -> float
    GetNextHighestSurroundingCeiling(float higherThanValue, float defaultValue) -> float
    GetNextHighestSurroundingLightLevel(int32 higherThanValue, int32 defaultValue) -> int32
}
```
### line_t
```lua
-- Represents a line in level (one or two sided)
line_t {
    int32       index           -- Index of the line in the list of lines for the level (ZERO BASED!)(readonly)
    float       v1x             -- Line point 1: x (readonly)
    float       v1y             -- Line point 1: y (readonly)
    float       v2x             -- Line point 2: x (readonly)
    float       v2y             -- Line point 2: y (readonly)
    float       angle           -- Line angle (degrees)(readonly)
    uint32      flags           -- Line bit flags
    int32       special         -- The current line special
    int32       tag             -- Tag for the line
    side_t      frontside       -- Front side for the line (readonly)
    side_t      backside        -- Back side for the line (nil if none)(readonly)
    sector_t    frontsector     -- Front sector for the line (readonly)
    sector_t    backsector      -- Back sector for the  line (nil if none)(readonly)
}
```
### side_t
```lua
-- Represents one side of a line
side_t {
    int32       index                   -- Index of the side in the list of sides for the level (ZERO BASED!)(readonly)
    float       textureoffset           -- Horizontal texture offset (floating-point/real format)
    int32       textureoffset_fixed     -- Horizontal texture offset (16.16 integer fixed point format)
    float       rowoffset               -- Vertical texture offset (floating-point/real format)
    int32       rowoffset_fixed         -- Vertical texture offset (16.16 integer fixed point format)
    int32       toptexture              -- Top texture number
    int32       bottomtexture           -- Bottom texture number
    int32       midtexture              -- Middle texture number
    sector_t    sector                  -- Which sector the side belongs to (readonly)
}
```
### mobj_t
```lua
-- Represents a thing/actor in the level
mobj_t {
    float       x           -- Position: x (readonly)
    float       y           -- Position: y (readonly)
    float       z           -- Position: z (readonly)
    float       angle       -- Angle in degrees
    float       momx        -- Velocity: x
    float       momy        -- Velocity: y
    float       momz        -- Velocity: z
    uint32      type        -- Thing type (readonly)
    int32       doomednum   -- Thing Doom Editor Number; may be '-1' for non-editor placed things (readonly)
    int32       tag         -- Script defined thing tag (can be used to tag and identify things)
    uint32      flags       -- Flags (readonly)
    uint32      radius      -- Radius (readonly)
    uint32      height      -- Height (readonly)
    uint32      health      -- Health (readonly)
    sector_t    sector      -- Which sector the thing is currently in (readonly)
    mobj_t      target      -- Target for enemies, firing object for missiles
    mobj_t      tracer      -- For homing missiles the target being tracked
    player_t    player      -- The player associated with this thing (nil if none)(readonly)
}
```
### player_t
```lua
player_t {
    int32   index               -- Index of the player in the list of players (ZERO BASED!)(readonly)
    mobj_t  mo                  -- The player's thing/map-object (readonly)
    int32   health              -- Player health (as displayed)(readonly)
    int32   armorpoints         -- Armor points (readonly)
    int32   armortype           -- Type of armor: 0 = no armor, 1 = regular armor, 2 = mega armor (readonly)
    bool    backpack            -- 'true' if the player picked up the backpack (readonly)
    int32   frags               -- Frag count for a deathmatch game (readonly)
    int32   killcount           -- Intermission stats: monster kill count (readonly)
    int32   itemcount           -- Intermission stats: number of items picked up (readonly)
    int32   secretcount         -- Intermission stats: number of secrets found (readonly)
    uint32  readyweapon         -- The currently equipped weapon (readonly)
    uint32  pendingweapon       -- The weapon to equip next or 'wp_nochange' if no weapon change is pending (readonly)
    
    GetPowerTicsLeft(int32 powerType) -> int32      -- Get how many remaining game tics the specified power will be active for
    HasCard(int32 cardType) -> bool                 -- Returns 'true' if the player has the specified card type
    IsWeaponOwned(int32 weaponType) -> bool         -- Returns 'true' if the specified weapon is owned
    GetAmmo(int32 ammoType) -> int32                -- Return the amount of the specified ammo type the player has
    GetMaxAmmo(int32 ammoType) -> int32             -- Return the maximum amount of the specified ammo type the player can pickup
}
```
### CustomFloorDef
```lua
-- Settings for a custom floor.
-- These all default to reasonable values, however 'destheight' should ALWAYS be specified.
CustomFloorDef {
    new()                               -- Create a new custom floor definition with default values

    bool    crush                       -- Is the floor crushing?
    bool    dofinishscript              -- Call the finish script action when completed moving?
    float   destheight                  -- Destination height for the floor
    float   speed                       -- Speed that the floor moves at
    uint32  startsound                  -- Sound to make when starting ('0' if none)
    uint32  movesound                   -- Sound to make when moving ('0' if none)
    uint32  movesoundfreq               -- How many tics between instances of the move sound playing
    uint32  stopsound                   -- Sound to make when stopping ('0' if none)
    int32   finishscript_actionnum      -- If enabled, a script action to execute when the floor has come to a complete stop/finished
    int32   finishscript_userdata       -- Userdata to pass to the 'finish' script action
}
```
### CustomPlatDef
```lua
-- Settings for a custom platform.
-- These all default to reasonable values, however 'minheight', 'maxheight', 'startstate' and 'finishstate' should ALWAYS be specified.
CustomPlatDef {
    new()                               -- Create a new custom platform definition with default values

    bool    crush                       -- Is the platform crushing?
    bool    dofinishscript              -- Call the finish script action when completed moving?
    int32   startstate                  -- -1 = down, 0 = wait, 1 = up
    int32   finishstate                 -- -1 = down, 0 = wait, 1 = up (stop the platform after this state is done)    
    float   minheight                   -- Minimum platform floor height
    float   maxheight                   -- Maximum platform floor height
    float   speed                       -- Speed that the platform moves at
    int32   waittime                    -- How many game tics the platform waits for when in the 'waiting' state
    uint32  startsound                  -- Sound to make when starting ('0' if none)
    uint32  movesound                   -- Sound to make when moving ('0' if none)
    uint32  movesoundfreq               -- How many tics between instances of the move sound playing
    uint32  stopsound                   -- Sound to make when stopping ('0' if none)
    int32   finishscript_actionnum      -- If enabled, a script action to execute when the platform has come to a complete stop/finished
    int32   finishscript_userdata       -- Userdata to pass to the 'finish' script action
}
```
### CustomCeilingDef
```lua
-- Settings for a custom ceiling.
-- These all default to reasonable values, however 'minheight' and 'maxheight' should ALWAYS be specified.
CustomCeilingDef {
    new()                               -- Create a new custom ceiling definition with default values

    bool    crush                       -- Is the ceiling crushing?
    bool    dofinishscript              -- Call the finish script action when completed moving?
    float   minheight                   -- Minimum ceiling height the crusher reaches
    float   maxheight                   -- Maximum ceiling height the crusher reaches
    int32   startdir                    -- 1 = up, 0 = paused, -1 = down
    float   normalspeed                 -- Speed normally when moving
    float   crushspeed                  -- Speed when crushing something (usually 1/8 of normal speed)
    int32   numdirchanges               -- How many times the crusher can change direction before stopping (if '-1' then no limit)
    uint32  startsound                  -- Sound to make when starting ('0' if none)
    uint32  movesound                   -- Sound to make when moving ('0' if none)
    uint32  movesoundfreq               -- How many tics between instances of the move sound playing
    uint32  changedirsound              -- Sound to make when changing direction ('0' if none)
    uint32  stopsound                   -- Sound to make when stopping ('0' if none)
    int32   finishscript_actionnum      -- If enabled, a script action to execute when the ceiling has come to a complete stop/finished
    int32   finishscript_userdata       -- Userdata to pass to the 'finish' script action
};
```
### CustomDoorDef
```lua
-- Settings for a custom door.
-- These all default to reasonable values, however 'minheight' and 'maxheight' should ALWAYS be specified.
CustomDoorDef {
    new()                               -- Create a new custom door definition with default values

    bool    open                        -- Does the door open ('true') or close ('false') initially?
    bool    doreturn                    -- If 'true' then return the door to it's prior open/closed state before the door is done
    bool    dofinishscript              -- Call the finish script action when the door is done?
    bool    blockable                   -- If 'true' then the door goes back up if closing and something is underneath it
    float   minheight                   -- Minimum (closed) ceiling height for the door
    float   maxheight                   -- Maximum (open) ceiling height for the door
    float   speed                       -- Speed of the door
    int32   waittime                    -- How long the door waits (in tics) before trying to return to it's previous state (if returning at all)
    uint32  opensound                   -- Sound to make when opening ('sfx_None' if none)
    uint32  closesound                  -- Sound to make when closing ('sfx_None' if none)
    int32   finishscript_actionnum      -- If enabled, a script action to execute when the door has finished up completely
    int32   finishscript_userdata       -- Userdata to pass to the 'finish' script action
}
```
## Functions
### Miscellaneous
```lua
FixedToFloat(fixed v) -> float              -- Convert an integer 16.16 fixed point number to a floating point number.
FloatToFixed(float v) -> int32              -- Convert a floating point number to a 16.16 fixed point number.
P_Random() -> int32                         -- Return a random number from 0-255
R_TextureNumForName(string name) -> int32   -- Lookup the texture number for a particular texture name (returns -1 if not found)
R_FlatNumForName(string name) -> int32      -- Lookup the flat number for a particular texture name (returns -1 if not found)
G_ExitLevel()                               -- Exit to the next map
G_SecretExitLevel(int32t nextMap)           -- Exit to the specified map
StatusMessage(string msg)                   -- Show a status bar message, similar to the ones for item pickups
KeyFlash_Red(player_t player)               -- Do a 'red key needed' status bar flash for the specified player
KeyFlash_Blue(player_t player)              -- Do a 'blue key needed' status bar flash for the specified player
KeyFlash_Yellow(player_t player)            -- Do a 'yellow key needed' status bar flash for the specified player

ApproxLength(float dx, float dy) -> float                           -- Using Doom's portable 'approximate' length estimation, return the length of the specified vector
ApproxDistance(float x1, float y1, float x2, float y2) -> float     -- Using Doom's portable 'approximate' length estimation, return the distance between the two points
AngleToPoint(float x1, float y1, float x2, float y2) -> float       -- Using Doom's lookup tables, compute the angle from one point to another (from p1 to p2)

-- Using Doom's finite lookup tables, return the sine or cosine of an angle given in degrees
FineSine(float angleDegrees)
FineCosine(float angleDegrees)

-- Tells if the current game is single player, coop or deathmatch
IsSinglePlayerGame() -> bool
IsCoopGame() -> bool
IsDeathmatchGame() -> bool

-- PsyDoom: show a status bar style message (31 characters max) near the center of the screen for the specified number of tics.
-- Optionally play the specified sound when showing the message; specify '0' if no sound is desired.
-- This feature can be used for status bar style messages which are not interrupted by item pickups.
AlertMessage(string msg, uint32 numTics, uint32 soundId)

-- Changes the camera to view from a certain position at a secified angle in degrees.
-- The camera goes into effect for all players. While it is active players will be invulernable, monsters will not see players and will walk away.
-- The status bar will also be hidden and player inputs ignored while the camera is active.
-- The camera can be used to show events like remote doors opening.
DoCamera(float x, float y, float z, float angle, uint32 numTics)

-- Stops the current camera, returning control to all players
StopCamera()
```
### Delayed and repeating action scheduling
```lua
-- Schedule the specified action number to occur at least 1 tic in the future, plus the number of delay tics.
-- The tag specified is used to identify the delayed action, and userdata can be used for any purposes.
ScheduleAction(int32 actionNum, int32 delayTics, int32 tag, int32 userdata)

-- Schedule the specified action number to occur at least 1 tic in the future, plus the number of initial delay tics.
-- The action executes once plus the number of repeats specified. If repeats is <= '-1' then the action will repeat forever.
-- The tag specified is used to identify the delayed action, and userdata can be used for any purposes.
ScheduleRepeatingAction(
    int32 actionNum,
    int32 initialDelayTics,
    int32 numRepeats,
    int32 repeatDelay,
    int32 tag,
    int32 userdata
)

StopAllScheduledActions()                   -- Stops all actions that are scheduled
StopScheduledActionsWithTag(int32 tag)      -- Stops all scheduled actions which have the specified tag

-- Pause or unpause all scheduled actions.
-- Note: if unpausing then the actions will not execute until the next frame at the very least.
PauseAllScheduledActions(bool bPause) 

-- Pause or unpause scheduled actions with the specified tag.
-- Note: if unpausing then the actions will not execute until the next frame at the very least.
PauseScheduledActionsWithTag(int32 tag, bool bPause)

-- Returns the number actions scheduled with the specified tag.
-- This includes any actions that are paused but NOT actions that have been stopped.
GetNumScheduledActionsWithTag(int32 tag) -> int32
```
### Sectors
```lua
GetNumSectors() -> int32                            -- Returns the number of sectors in the level.
GetSector(int32 index) -> sector_t                  -- Gets a specific sector in the level via a ZERO BASED index. If the index is out of range 'nil' is returned.
FindSectorWithTag(int32 tag) -> sector_t            -- Returns the first sector with the specified tag or 'nil' if not found.
ForEachSector(function f)                           -- Iterates over all sectors in the game. The function is called for each sector, passing in the 'sector_t' as a parameter.
ForEachSectorWithTag(int32 tag, function f)         -- For each sector that has the given tag, call function 'f' passing in the 'sector_t' as a parameter.
SectorAtPosition(float x, float y) -> sector_t      -- Returns the sector at the specified position, or the closest one to it (should always return something)
```
### Lines
```lua
GetNumLines() -> int32                      -- Returns the number of lines in the level.
GetLine(int32 index) -> line_t              -- Gets a specific line in the level via a ZERO BASED index. If the index is out of range 'nil' is returned.
FindLineWithTag(int32 tag) -> line_t        -- Returns the first line with the specified tag or 'nil' if not found.
ForEachLine(function f)                     -- Iterates over all lines in the game. The function is called for each line, passing in the 'line_t' as a parameter.
ForEachLineWithTag(int32 tag, function f)   -- For each line that has the given tag, call function 'f' passing in the 'line_t' as a parameter

-- Tells what side of the specified line the point is on.
-- Returns '0' if on the front side, or otherwise '1' if on the back side.
P_PointOnLineSide(float x, float y, line_t line) -> int32

-- Try to trigger the line special for crossing the specified line (if it has a walk-over special) using the specified thing.
-- If the thing is not allowed to trigger the line or there is no special, then this call does nothing.
-- If the thing is not specified then it defaults to the first player in the game.
P_CrossSpecialLine(line_t line, mobj_t crossingMobj)

-- Similar to 'P_CrossSpecialLine' except it triggers line specials that must be shot.
-- Again, if the thing is not specified then it will default to the first player.
P_ShootSpecialLine(line_t line, mobj_t shootingMobj)

-- Similar to 'P_CrossSpecialLine' except it triggers line specials that must be activated via a switch and also doors.
-- Again, if the thing is not specified then it will default to the first player.
P_UseSpecialLine(line_t line, mobj_t usingMobj)

-- Flips the switch texture for the given line to the opposite switch texture.
-- If the switch is usable again, schedule it to switch back after a while or otherwise mark it unusable.
P_ChangeSwitchTexture(line_t line, bool bUseAgain)
```
### Sides
```lua
GetNumSides() -> int32              -- Returns the number of sides in the level.
GetSide(int32 index) -> side_t      -- Gets a specific side in the level by a ZERO BASED index. If the index is out of range 'nil' is returned.
ForEachSide(function f)             -- Iterates over all sides in the game. The function is called for each side, passing in the 'side_t' as a parameter.
```
### Things
```lua
-- Iterates over all things in the game. The function is called for each thing, passing in the 'mobj_t' as a parameter.
ForEachMobj(function f)

-- Iterate over all things approximately in the rectangular area enclosing the two specified points.
-- The specified function is invoked with a 'mobj_t' parameter for each thing found.
ForEachMobjInArea(float x1, float y1, float x2, float y2, function f)

-- Returns the thing type for the specified DoomEd number, or -1 if no matching thing type is found.
-- The thing type is used for spawning and thing identification at runtime.
FindMobjTypeForDoomEdNum(int32 doomEdNum) -> int32

-- Spawn a thing at the specified position of the specified type; on successful spawn the thing is returned, otherwise 'nil'
P_SpawnMobj(float x, float y, float z, uint32 mobjType) -> mobj_t

-- Spawn a missile of the specified type from 'src' (firer) to 'dst'; on successful spawn the thing is returned, otherwise 'nil'.
-- IMPORTANT: the 'src' and 'dst' things MUST be specified for this work properly.
P_SpawnMissile(mobj_t& src, mobj_t& dst, uint32 mobjType) -> mobj_t

-- Spawn a missile of a specified type which explodes immediately.
-- Useful for creating explosions from rockets, for example, at a specified position.
P_SpawnMissileExplosion(float x, float y, float z, uint32 mobjType)

-- Damages the given target by the specified integer amount. The inflictor is optional and affects force calculations.
-- The source is optional and affects blame for the attack.
P_DamageMobj(mobj_t target, mobj_t inflictor, mobj_t source, int32 baseDamageAmt)

-- Schedules the thing to be deleted and removed from the game once the script finishes executing.
-- The thing is immediately put into the S_NULL state also to deactivate it.
P_RemoveMobj(mobj_t target)

-- Teleport the specified thing to the specified point, optionally preserving momentum and telefragging things at the destination.
-- Fog and sound are optional, pass '0' to have neither. If the teleport is not possible then 'false' is returned.
EV_TeleportTo(
    mobj_t mobj,
    float dstX,
    float dstY,
    float dstAngle,     -- In degrees
    bool bTelefrag,
    bool bPreserveMomentum,
    uint32 fogMobjType,         -- Optional, '0' for none
    uint32 fogSoundId           -- Optional, '0' for none
) -> bool

-- Do a noise alert to awaken monsters using the specified noise maker.
-- The noise maker must be a player, if it's a monster then this call will have no effect.
P_NoiseAlert(mobj_t noiseMaker)

-- Check to see if there is a line of sight between the two things and returns 'true' if that is the case
P_CheckSight(mobj_t m1, mobj_t m2)

-- Test if a position can be moved to for the specified thing and return 'true' if the move is allowed
P_CheckPosition(mobj_t mobj, float x, float y) -> bool
```
### Players
```lua
GetNumPlayers() -> int32                -- Return the number of players in the game
GetCurPlayer() -> player_t              -- Return this user's player object
GetPlayer(int32 index) -> player_t      -- Return a player specified by a ZERO BASED index. Returns 'nil' if the index is invalid.
```
### Script execution context
```lua
GetTriggeringLine() -> line_t           -- The line that triggered the current script action (nil if none)
GetTriggeringSector() -> sector_t       -- The sector that triggered the current script action (nil if none)
GetTriggeringMobj() -> mobj_t           -- The thing that triggered the current script action (nil if none)
GetCurActionTag() -> int32              -- Get the tag associated with the current executing script action (Will only be defined for delayed actions, otherwise '0')
GetCurActionUserdata() -> int32         -- Get the userdata associated with the current executing script action (Will only be defined for delayed actions, otherwise '0')
SetLineActionAllowed(bool allowed)      -- If called with 'false' indicates that the script action is not allowed. Can prevent switches from changing state.
```
## Moving floors, ceilings and platforms
```lua
--------------------------------------------------------------------------------------------------------------------------------------------
-- Manually move a floor or ceiling towards a destination height, possibly altering the height of things inside the sector or crushing them.
-- The speed is always specified as a positive quantity. The result is one of the T_MOVEPLANE_XXX constants.
-- These functions must be called continually in order to reach the destination height.
--------------------------------------------------------------------------------------------------------------------------------------------
T_MoveFloor(sector_t sector, float speed, float destHeight, bool bCrush) -> uint32
T_MoveCeiling(sector_t sector, float speed, float destHeight, bool bCrush) -> uint32

--------------------------------------------------------------------------------------------------------------------------------------------
-- Do a custom floor mover, platform/elevator, ceiling/crusher or on the specified sector using the specified settings.
-- Returns 'true' if the special was started, or 'false' if some other thinker (ceiling etc.) is already operating on the sector.
--------------------------------------------------------------------------------------------------------------------------------------------
EV_DoCustomFloor(sector_t sector, CustomFloorDef) -> bool
EV_DoCustomPlat(sector_t sector, CustomPlatDef platDef) -> bool
EV_DoCustomCeiling(sector_t sector, CustomCeilingDef ceilDef) -> bool
EV_DoCustomDoor(sector_t sector, CustomDoorDef ceilDef) -> bool

--------------------------------------------------------------------------------------------------------------------------------------------
-- Pause a platform/elevator for a specific sector, or sectors with the specified tag; 'true' is returned if any sectors were affected
--------------------------------------------------------------------------------------------------------------------------------------------
EV_StopPlatForSector(sector sector) -> bool
EV_StopPlatForTag(int32 tag) -> bool

--------------------------------------------------------------------------------------------------------------------------------------------
-- Unpause a platform/elevator for a specific sector, or sectors with the specified tag; 'true' is returned if any sectors were affected
--------------------------------------------------------------------------------------------------------------------------------------------
P_ActivateInStasisPlatForSector(sector_t sector) -> bool
P_ActivateInStasisPlatForTag(int32 tag) -> bool

--------------------------------------------------------------------------------------------------------------------------------------------
-- Pause a ceiling crusher for a specific sector, or sectors with the specified tag; 'true' is returned if any sectors were affected
--------------------------------------------------------------------------------------------------------------------------------------------
EV_CeilingCrushStopForSector(const sector_t sector) -> bool
EV_CeilingCrushStopForTag(const int32 tag) -> bool

--------------------------------------------------------------------------------------------------------------------------------------------
-- Unpause a ceiling crusher for a specific sector, or sectors with the specified tag; 'true' is returned if any sectors were affected
--------------------------------------------------------------------------------------------------------------------------------------------
P_ActivateInStasisCeilingForSector(sector_t sector) -> bool
P_ActivateInStasisCeilingsForTag(int32 tag) -> bool

```
### Sounds
```lua
S_PlaySoundAtMobj(mobj_t origin, uint32 soundId)            -- Play a sound at the thing's position (note: no attenutation if thing not specified)
S_PlaySoundAtSector(sector_t origin, uint32 soundId)        -- Play a sound at the sector center (note: no attenutation if sector not specified)
S_PlaySoundAtPosition(float x, float y, uint32 soundId)     -- Play a sound at the specified world position
```
## Constants (Avoid modifying these!)
*Note: unless specified otherwise, all these values are of type `uint32`*

### Return values for `T_MoveFloor` and `T_MoveCeiling`
```lua
T_MOVEPLANE_OK          -- Movement for the floor/ceiling was fully OK
T_MOVEPLANE_CRUSHED     -- The floor/ceiling is crushing things and may not have moved because of this
T_MOVEPLANE_PASTDEST    -- Floor/ceiling has reached its destination or very close to it (sometimes stops just before if crushing things)
```
### Power types
```lua
pw_invulnerability      -- Invulnerability powerup
pw_strength             -- Berserk powerup
pw_invisibility         -- Partial invisibility powerup
pw_ironfeet             -- Radiation suit powerup
pw_allmap               -- Computer area map powerup
pw_infrared             -- Light amplification visor (fullbright) powerup
```
### Card types
```lua
it_redcard
it_bluecard
it_yellowcard
it_redskull
it_blueskull
it_yellowskull
```
## Weapon types
```lua
wp_fist
wp_pistol
wp_shotgun
wp_supershotgun
wp_chaingun
wp_missile          -- Rocket launcher
wp_plasma
wp_bfg
wp_chainsaw
wp_nochange         -- Used to represent no weapon change for the 'pendingweapon'
```
### Ammo types
```lua
am_clip         -- Bullets
am_shell
am_cell
am_misl         -- Rockets
```
### Sector flags
```lua
SF_NO_REVERB    -- Disables reverb on a sector
SF_GHOSTPLAT    -- Render the sector at the lowest floor height surrounding it, creating an 'invisible platform' effect

------------------------------------------------------------------------------------------------------------------------
-- These flags allow the sector height to be expanded or contracted for shading purposes.
-- They offer a little control over the gradient with dual colored lighting.
--
-- The adjustments are in multiples of the sector height (ceil - floor). Floors are normally adjusted downwards
-- and ceilings are adjusted upwards (gradient expand mode), unless the gradient 'contract' flag is being used.
--
-- Adjustment amounts (gradient expand):
--  +1  +0.5x sector shading height
--  +2  +1.0x sector shading height
--  +3  +2.0x sector shading height
--
-- Adjustment amounts (gradient contract):
--  +1  -0.25x sector shading height
--  +2  -0.5x  sector shading height
--  +3  -0.75x sector shading height
------------------------------------------------------------------------------------------------------------------------
SF_GRAD_CONTRACT
SF_GRAD_FLOOR_PLUS_1
SF_GRAD_FLOOR_PLUS_2
SF_GRAD_CEIL_PLUS_1
SF_GRAD_CEIL_PLUS_2
```
### Line flags
```lua
ML_BLOCKING             -- The line blocks all movement
ML_BLOCKMONSTERS        -- The line blocks monsters
ML_TWOSIDED             -- Unset for single sided lines
ML_DONTPEGTOP           -- If unset then upper texture is anchored to the ceiling rather than bottom edge
ML_DONTPEGBOTTOM        -- If unset then lower texture is anchored to the floor rather than top edge
ML_SECRET               -- Don't show as two sided in the automap, because it's a secret
ML_SOUNDBLOCK           -- Stops sound propagation
ML_DONTDRAW             -- Hide on the automap
ML_MAPPED               -- Set when the line is to be shown on the automap
ML_MIDMASKED            -- PSX DOOM: Middle texture has translucent or alpha blended pixels
ML_MIDTRANSLUCENT       -- PSX DOOM: Middle texture drawn with alpha blending
ML_BLOCKPRJECTILE       -- PSX DOOM: Line stops projectiles
ML_MID_FIXED_HEIGHT     -- PSX FINAL DOOM: forces the middle part of a wall to have a fixed height equal to the texture height (used for fences that don't repeat vertically)
ML_VOID                 -- PsyDoom specific: flag a line as 'see through' for occlusion culling and prohibit sky walls from being rendered
ML_ADD_SKY_WALL_HINT    -- PsyDoom specific: hints that a 'sky wall' should be added for 2 sided lines with a sky ceiling or floors
```
### Thing flags
```lua
MF_SPECIAL              -- Thing is an item which can be picked up
MF_SOLID                -- Thing is collidable and blocks movement
MF_SHOOTABLE            -- Thing is targetable (can be hit) and can take damage
MF_NOSECTOR             -- Thing is not present in sector thing lists
MF_NOBLOCKMAP           -- Thing is not present in the blockmap 
MF_AMBUSH               -- Monster is deaf and does not react to sound
MF_JUSTHIT              -- Monster has just been hit and should try to attack or fight back immediately
MF_JUSTATTACKED         -- Monster has just attacked and should wait for a bit before attacking again
MF_SPAWNCEILING         -- When spawning on level start, spawn touching the ceiling rather than the floor
MF_NOGRAVITY            -- Don't apply gravity every game tick to this monster (can float in the air)
MF_DROPOFF              -- The thing is allowed to jump/fall off high ledges
MF_PICKUP               -- Flag used when moving a thing: pickup items that it comes into contact with
MF_NOCLIP               -- Cheat that disables collision on the player
MF_SLIDE                -- Not used in PSX Doom: Linux Doom says 'Player: keep info about sliding along walls'
MF_FLOAT                -- The thing can fly/float up and down (used by Cacodemons etc.)
MF_TELEPORT             -- A flag set temporarily when a thing is the process of teleporting
MF_MISSILE              -- Set by flying projectiles (Rocket, Imp Fireball): don't damage the same species and explode when blocked.
MF_DROPPED              -- The pickup was dropped by an enemy that drops ammo; was not spawned in the level originally
MF_SHADOW               -- Not used in PSX: on PC this causes demons to be drawn fuzzy/semi-translucent
MF_NOBLOOD              -- Don't bleed when shot; used by barrels
MF_CORPSE               -- Thing is dead: don't stop sliding when positioned hanging over a step
MF_INFLOAT              -- The thing is floating up or down so it can move to a certain position
MF_COUNTKILL            -- Killing this thing counts towards the kill stats at level end
MF_COUNTITEM            -- Picking up this item counts towards the item stats at level end
MF_SKULLFLY             -- The thing is a lost soul in flight: special logic is needed for this thing type
MF_NOTDMATCH            -- The thing is not spawned in deathmatch mode (used for keycards)
MF_SEETARGET            -- A flag set on monsters when the monster can see or has a line of sight towards it's intended target
MF_BLEND_ON             -- PSX DOOM: PSX DOOM: if set then blending is enabled for the object (alpha, additive or subtractive)
MF_BLEND_MODE_BIT1      -- PSX DOOM: 1 of 2 bits determining blend mode if blending is enabled
MF_BLEND_MODE_BIT2      -- PSX DOOM: 1 of 2 bits determining blend mode if blending is enabled
```
### Thing types (built-in)
```lua
MT_PLAYER               -- Player
MT_POSSESSED            -- Former Human
MT_SHOTGUY              -- Former Sergeant
MT_UNDEAD               -- Revenant
MT_TRACER               -- Revenant homing missile
MT_SMOKE                -- Smoke from Revenant missile
MT_FATSO                -- Mancubus
MT_FATSHOT              -- Mancubus fireball
MT_CHAINGUY             -- Chaingunner
MT_TROOP                -- Imp
MT_SERGEANT             -- Demon
MT_HEAD                 -- Cacodemon
MT_BRUISER              -- Baron of Hell
MT_KNIGHT               -- Hell Knight
MT_SKULL                -- Lost Soul
MT_SPIDER               -- Spider Mastermind
MT_BABY                 -- Arachnotron
MT_CYBORG               -- Cyberdemon
MT_PAIN                 -- Pain Elemental
MT_BARREL               -- Barrel
MT_TROOPSHOT            -- Imp fireball
MT_HEADSHOT             -- Cacodemon fireball
MT_BRUISERSHOT          -- Baron/Knight fireball
MT_ROCKET               -- Rocket (in flight)
MT_PLASMA               -- Plasma fireball
MT_BFG                  -- BFG main blast
MT_ARACHPLAZ            -- Arachnotron plasma ball
MT_PUFF                 -- Smoke puff
MT_BLOOD                -- Blood puff
MT_TFOG                 -- Teleport fog
MT_IFOG                 -- Item respawn fog
MT_TELEPORTMAN          -- Teleport destination marker
MT_EXTRABFG             -- Smaller BFG explosion on enemies
MT_MISC0                -- Green armor
MT_MISC1                -- Blue armor
MT_MISC2                -- Health bonus
MT_MISC3                -- Armor bonus
MT_MISC4                -- Blue keycard
MT_MISC5                -- Red keycard
MT_MISC6                -- Yellow keycard
MT_MISC7                -- Yellow skull key
MT_MISC8                -- Red skull key
MT_MISC9                -- Blue skull key
MT_MISC10               -- Stimpack
MT_MISC11               -- Medikit
MT_MISC12               -- Soulsphere
MT_INV                  -- Invulnerability
MT_MISC13               -- Berserk
MT_INS                  -- Invisibility
MT_MISC14               -- Radiation Suit
MT_MISC15               -- Computer Map
MT_MISC16               -- Light Amplification Goggles
MT_MEGA                 -- Megasphere
MT_CLIP                 -- Ammo clip
MT_MISC17               -- Box of Ammo
MT_MISC18               -- Rocket
MT_MISC19               -- Box of Rockets
MT_MISC20               -- Cell Charge
MT_MISC21               -- Cell Charge Pack
MT_MISC22               -- Shotgun shells
MT_MISC23               -- Box of Shells
MT_MISC24               -- Backpack
MT_MISC25               -- BFG9000
MT_CHAINGUN             -- Chaingun
MT_MISC26               -- Chainsaw
MT_MISC27               -- Rocket Launcher
MT_MISC28               -- Plasma Gun
MT_SHOTGUN              -- Shotgun
MT_SUPERSHOTGUN         -- Super Shotgun
MT_MISC29               -- Tall techno floor lamp
MT_MISC30               -- Short techno floor lamp
MT_MISC31               -- Floor lamp
MT_MISC32               -- Tall green pillar
MT_MISC33               -- Short green pillar
MT_MISC34               -- Tall red pillar
MT_MISC35               -- Short red pillar
MT_MISC36               -- Short red pillar (skull)
MT_MISC37               -- Short green pillar (beating heart)
MT_MISC38               -- Evil Eye
MT_MISC39               -- Floating skull rock
MT_MISC40               -- Gray tree
MT_MISC41               -- Tall blue firestick
MT_MISC42               -- Tall green firestick
MT_MISC43               -- Tall red firestick
MT_MISC44               -- Short blue firestick
MT_MISC45               -- Short green firestick
MT_MISC46               -- Short red firestick
MT_MISC47               -- Stalagmite
MT_MISC48               -- Tall techno pillar
MT_MISC49               -- Candle
MT_MISC50               -- Candelabra
MT_MISC51               -- Hanging victim, twitching (blocking)
MT_MISC52               -- Hanging victim, arms out (blocking)
MT_MISC53               -- Hanging victim, 1-legged (blocking)
MT_MISC54               -- Hanging pair of legs (blocking)
MT_MISC56               -- Hanging victim, arms out
MT_MISC57               -- Hanging pair of legs
MT_MISC58               -- Hanging victim, 1-legged
MT_MISC55               -- Hanging leg (blocking)
MT_MISC59               -- Hanging leg
MT_MISC60               -- Hook Chain
MT_MISC_BLOODHOOK       -- Chain Hook With Blood
MT_MISC_HANG_LAMP       -- UNUSED: points to hanging lamp sprite, but DoomEd number is Chaingunner... 
MT_MISC61               -- Dead cacodemon
MT_MISC63               -- Dead former human
MT_MISC64               -- Dead demon
MT_MISC66               -- Dead imp
MT_MISC67               -- Dead former sergeant
MT_MISC68               -- Bloody mess 1
MT_MISC69               -- Bloody mess 2
MT_MISC70               -- 5 skulls shish kebob
MT_MISC73               -- Pile of skulls and candles
MT_MISC71               -- Pool of blood and bones
MT_MISC72               -- Skull on a pole
MT_MISC74               -- Impaled human
MT_MISC75               -- Twitching impaled human
MT_MISC76               -- Large brown tree
MT_MISC77               -- Burning barrel
MT_MISC78               -- Hanging victim, guts removed
MT_MISC79               -- Hanging victim, guts and brain removed
MT_MISC80               -- Hanging torso, looking down
MT_MISC81               -- Hanging torso, open skull
MT_MISC82               -- Hanging torso, looking up
MT_MISC83               -- Hanging torso, brain removed
MT_MISC84               -- Pool of blood and guts
MT_MISC85               -- Pool of blood
MT_MISC86               -- Pool of brains
MT_MISC87               -- Unused hanging lamp sprite
MT_VILE                 -- Arch-vile
MT_FIRE                 -- Arch-vile's flame
MT_WOLFSS               -- Wolfenstein SS officer
MT_KEEN                 -- Commander Keen easter egg
MT_BOSSBRAIN            -- Icon Of Sin head on a stick
MT_BOSSSPIT             -- Icon Of Sin box shooter
MT_BOSSTARGET           -- Icon Of Sin box target
MT_SPAWNSHOT            -- Icon Of Sin box
MT_SPAWNFIRE            -- Icon Of Sin spawn fire
MT_MARKER1              -- PsyDoom generic marker (type 1)
MT_MARKER2              -- PsyDoom generic marker (type 2)
MT_MARKER3              -- PsyDoom generic marker (type 3)
MT_MARKER4              -- PsyDoom generic marker (type 4)
MT_MARKER5              -- PsyDoom generic marker (type 5)
MT_MARKER6              -- PsyDoom generic marker (type 6)
MT_MARKER7              -- PsyDoom generic marker (type 7)
MT_MARKER8              -- PsyDoom generic marker (type 8)
MT_MARKER9              -- PsyDoom generic marker (type 9)
MT_MARKER10             -- PsyDoom generic marker (type 10)
MT_MARKER11             -- PsyDoom generic marker (type 11)
MT_MARKER12             -- PsyDoom generic marker (type 12)
MT_MARKER13             -- PsyDoom generic marker (type 13)
MT_MARKER14             -- PsyDoom generic marker (type 14)
MT_MARKER15             -- PsyDoom generic marker (type 15)
MT_MARKER16             -- PsyDoom generic marker (type 16)
```
### Sound ids (built-in)
```lua
sfx_sgcock              -- Weapon pickup sound
sfx_punch               -- Punch hit
sfx_itmbk               -- Deathmatch item respawn
sfx_firsht2             -- Demon/Baron/Cacodemon etc. fireball sound
sfx_barexp              -- Barrel/rocket explode
sfx_firxpl              -- Demon fireball hit
sfx_pistol              -- Pistol fire
sfx_shotgn              -- Shotgun fire
sfx_plasma              -- Plasma rifle fire
sfx_bfg                 -- BFG start firing
sfx_sawup               -- Chainsaw being started up
sfx_sawidl              -- Chainsaw idle loop
sfx_sawful              -- Chainsaw saw
sfx_sawhit              -- Chainsaw hit
sfx_rlaunc              -- Rocket fire sound
sfx_rxplod              -- BFG explosion sound
sfx_pstart              -- Elevator start
sfx_pstop               -- Elevator/mover stop (also menu up/down sound)
sfx_doropn              -- Regular/slow door open
sfx_dorcls              -- Regular/slow door close
sfx_stnmov              -- Floor/crusher move sound
sfx_swtchn              -- Switch activate
sfx_swtchx              -- Exit switch activate
sfx_itemup              -- Bonus pickup
sfx_wpnup               -- Weapon pickup sound
sfx_oof                 -- Ooof sound after falling hard, or when trying to use unusable wall
sfx_telept              -- Teleport sound
sfx_noway               -- Ooof sound after falling hard, or when trying to use unusable wall
sfx_dshtgn              -- Super shotgun fire
sfx_dbopn               -- SSG open barrel
sfx_dbload              -- SSG load shells
sfx_dbcls               -- SSG close barrel
sfx_plpain              -- Player pain sound
sfx_pldeth              -- Player death sound
sfx_slop                -- Gib/squelch sound
sfx_posit1              -- Former human sight: 1
sfx_posit2              -- Former human sight: 2
sfx_posit3              -- Former human sight: 3 (unused)
sfx_podth1              -- Former human death: 1
sfx_podth2              -- Former human death: 2
sfx_podth3              -- Former human death: 3 (unused)
sfx_posact              -- Former human idle
sfx_popain              -- Former human pain
sfx_dmpain              -- Demon pain
sfx_dmact               -- Demon idle/growl
sfx_claw                -- Imp/Baron etc. melee claw
sfx_bgsit1              -- Imp sight: 1
sfx_bgsit2              -- Imp sight: 2
sfx_bgdth1              -- Imp death: 1
sfx_bgdth2              -- Imp death: 2
sfx_bgact               -- Imp idle
sfx_sgtsit              -- Demon sight
sfx_sgtatk              -- Demon attack
sfx_sgtdth              -- Demon death
sfx_brssit              -- Baron sight
sfx_brsdth              -- Baron death
sfx_cacsit              -- Cacodemon sight
sfx_cacdth              -- Cacodemon death
sfx_sklatk              -- Lost Soul attack
sfx_skldth              -- (Unused) Intended for Lost Soul death?
sfx_kntsit              -- Knight sight
sfx_kntdth              -- Knight death
sfx_pesit               -- Pain Elemental sight
sfx_pepain              -- Pain Elemental pain
sfx_pedth               -- Pain Elemental death
sfx_bspsit              -- Arachnotron sight
sfx_bspdth              -- Arachnotron death
sfx_bspact              -- Arachnotron idle
sfx_bspwlk              -- Arachnotron hoof
sfx_manatk              -- Mancubus attack
sfx_mansit              -- Mancubus sight
sfx_mnpain              -- Mancubus pain
sfx_mandth              -- Mancubus death
sfx_firsht              -- Demon/Baron/Cacodemon etc. fireball sound
sfx_skesit              -- Revenant sight
sfx_skedth              -- Revenant death
sfx_skeact              -- Revenant idle
sfx_skeatk              -- Revenant missile fire
sfx_skeswg              -- Revenant throw punch
sfx_skepch              -- Revenant punch land
sfx_cybsit              -- Cyberdemon sight
sfx_cybdth              -- Cyberdemon death
sfx_hoof                -- Cyberdemon hoof up
sfx_metal               -- Cyberdemon thud down (metal)
sfx_spisit              -- Spider Mastermind sight
sfx_spidth              -- Spider Mastermind death
sfx_bdopn               -- Fast/blaze door open
sfx_bdcls               -- Fast/blaze door close
sfx_getpow              -- Powerup pickup
sfx_vilsit              -- Arch-vile sight
sfx_vipain              -- Arch-vile pain
sfx_vildth              -- Arch-vile death
sfx_vilact              -- Arch-vile idle
sfx_vilatk              -- Arch-vile attack
sfx_flamst              -- Arch-vile flames (start)
sfx_flame               -- Arch-vile flames burn
sfx_sssit               -- Wolfenstein-SS sight
sfx_ssdth               -- Wolfenstein-SS death
sfx_keenpn              -- Commander Keen pain
sfx_keendt              -- Commander Keen death
sfx_bossit              -- Icon of Sin sight
sfx_bospit              -- Icon of Sin cube spit
sfx_bospn               -- Icon of Sin pain
sfx_bosdth              -- Icon of Sin death
sfx_boscub              -- Icon of Sin spawn cube fly
```
