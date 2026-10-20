# workshop64 engine specification

workshop64 is a small, Roblox-like platform (not a recreation) for the Nintendo 64, GameCube,
Wii, DS, 3DS, PC and the web. People play **experiences**: worlds built from parts, controlled by Lua,
with their own models, textures, music and UI, played alone or together over the network.

This repository holds the prototype that informed this spec. The spec describes where
the engine is going; the [last section](#current-prototype-vs-this-spec) says what exists today.

Each item is marked:

- **Decided**: agreed; build to it.
- **Proposed**: a starting point to build and measure, open to change.
- **Open**: needs a decision. Collected in [Open questions](#open-questions).

---

## 1. Principles

1. **Every experience runs on every platform.** *Decided.* There are no minimum platforms, tiers or
   "not available on this system". One set of budgets applies everywhere, sized to the weakest
   hardware (§13). New platforms must run every existing experience.
2. **Lua and game data run on the host as-is.** *Decided.* Scripts ship as source and data in one
   portable format; consoles load them themselves. Only assets may be transcoded (§9).
3. **Each port optimizes for its hardware.** *Decided.* The core says what to draw and play; each
   platform decides how (display lists, native formats, batching). Ports spend spare headroom on
   smoothness, not on extra content.
4. **The look is fixed.** *Decided.* 240 lines high everywhere (DS 192), scaled up
   nearest-neighbour, 16-colour textures, static lighting. The width follows the screen's shape:
   320 on a 4:3 screen, wider on wider ones (3DS top screen 400, a 16:9 window about 427), with
   the same vertical view, so wider screens see more to the sides. Wider screens add columns, never
   fewer lines. The Wii follows its 4:3 / 16:9 setting (428 columns in 16:9).
5. **The engine is not a general game engine.** *Decided.* No standalone export: experiences run
   inside workshop64, never as their own ROM or executable.
6. **Untrusted content is validated on load.** *Decided.* Every `.ws64`, whatever its source, is
   bounds-checked before use, and scripts run in a sandbox.
7. **Versions are explicit.** *Decided.* Engine, Lua API and file format each carry a version (§12).

## 2. Terms

| Term | Meaning |
|---|---|
| Experience | A game made for workshop64, distributed as one `.ws64` file. |
| Session (lobby) | A running instance of an experience: its players, shared state, and the scenes loaded for it. |
| Scene | One world within an experience: parts, models and the assets they use. A session can hold any number. |
| Part | The building block of every world (§5). |
| Model | A mesh asset, with optional collision shapes and animated sub-objects (§9.1). |
| Asset | A model, texture, font, song or sound. Has a canonical form and optional variants. |
| Variant | A per-platform transcoding of an asset, made by tools from the canonical form. |
| Server script / client script | Lua that runs the world on the server / Lua that runs one player's UI, camera and effects (§7). |
| Host | The machine running a session's server: a dedicated server, a PC, or the local program offline. |

## 3. Architecture

*Proposed.*

```
           ┌──────────── core (portable C, integer) ────────────┐
input ───▶ │ simulation · collision · Lua runtime · UI layout   │ ───▶ draw list, UI list, audio events
           │ .ws64 loader · session/scene management · net code │
           └────────────────────────────────────────────────────┘
                                   ▲  ▼
           ┌──────────── platform layer (per port) ─────────────┐
           │ input · clock · renderer · asset upload · audio    │
           │ storage · network transport                        │
           └────────────────────────────────────────────────────┘
```

- The core never includes platform headers; ports never contain game logic.
- The core emits a **draw list** each frame (camera; mesh + transform + tint; UI quads; text). Each
  port turns it into native drawing, so a gameplay feature touches the core, not five renderers.
- The same core runs as a client, as a server, or both at once (offline play).

## 4. Simulation

- **Fixed tick.** *Decided:* 30 Hz on every platform, independent of display rate, PAL/NTSC or
  slowdown. Rendering interpolates between ticks where a port can afford it.
- **Integer maths.** *Decided* (carried over). Positions in thousandths of a unit, angles in tenths
  of a degree, sin/cos from integer tables. The DS has no FPU, and it keeps behaviour identical
  across MIPS, PowerPC, ARM and x86.
- **Plain-data state.** *Decided.* World and session state are flat structs with integer IDs, no
  pointers, so they can be copied, hashed, snapshotted and sent.
- **Authority.** With server-controlled networking (§10) the host's simulation is the truth.
  Clients run the same code to predict their own player, then correct to the host's state.
  Bit-identical results across platforms are therefore not required, but are still the goal: they
  make prediction cheap and replays testable.
- **Physics.** *Decided:* later. For now parts are anchored, or moved by scripts and animations;
  players ride whatever they stand on.

## 5. World

The world is made of parts only. *Decided.* There is no terrain system.

### Parts

| Property | Notes |
|---|---|
| Shape | Block and wedge. *Proposed:* cylinder and sphere later, drawn round but colliding as boxes. |
| Position, size | Integer thousandths. |
| Rotation | **45° steps about the vertical axis only.** *Decided.* Animations turn smoothly between steps (§9.1). |
| Colour, material | Material picks an engine texture (wood, brick, metal, …); colour tints it. |
| Model | Optional: draw with a model asset instead of the shape. |
| Collides | Whether players collide with it. |
| Anchored | Always true until physics exists. |
| Transparency | *Proposed:* limited: on/off, or a few levels, per scene budget. Alpha sorting is expensive on N64 and DS. |

### Collision

- Players are boxes; parts collide as boxes and wedges, or as a model's collision shapes (§9.1).
- Moving one axis at a time, stepping up small ledges and stopping flush with walls carry over
  from the prototype.
- **45° rotation** about the vertical axis means a turned box is no longer axis-aligned, but it
  is axis-aligned in a frame turned 45°. Collision adds a separating-axis test with two extra
  axes and a 1/√2 constant: still integer, still cheap. Turns on other axes are not supported,
  which keeps it that way. *Decided.*
- **Broadphase.** *Proposed:* a coarse grid per scene, so collision checks nearby parts only.

### Scene settings

Per scene: world bounds, kill height (fall below it and respawn), spawn points (a Spawn part
type), sky colour, fog colour and draw distance. Fog doubles as the rendering budget tool. The
prototype's fixed floor becomes an ordinary baseplate part.

## 6. Sessions and scenes

*Decided.*

- An experience runs inside a **session** (lobby). The session holds the players, shared
  state, saves and the set of scenes loaded for it.
- A session can have **any number of scenes**, so content can be split up freely. Players move
  between scenes; a scene may be shared by several players or used by one.
- **The engine owns memory.** Loading a scene loads only its assets; leaving it frees them when
  needed. Experiences never manage memory themselves.
- **Budgets apply per scene** (§13), so a large experience is several scenes, not one big one.
- When a script errors or exceeds its limits, the session stops that experience and returns the
  player to the hub with a message, rather than crashing. *Decided.*

## 7. Scripting

Lua controls the world. *Decided.*

- **Lua 5.4**, built with `LUA_32BITS` (32-bit integers and floats). *Proposed, measured (§16):*
  it fits easily in memory and is 10–20% faster on N64 than the default 64-bit build.
- Scripts ship as source in the `.ws64` and are compiled on the console when a scene loads, at
  about 5–8 ms per KB of source on N64 and DS (a 20 KB experience: 0.1–0.2 s). Lua bytecode is
  never loaded from files: it isn't portable and the VM doesn't verify it. Caching compiled
  scripts isn't needed at that speed.
- **Server scripts** run the world on the host; **client scripts** run one player's UI, camera,
  sound and effects. Client scripts can't change the world directly; they send requests the
  server scripts accept or reject.
- **Custom functions and types.** *Decided:* the engine provides its own Lua API rather than relying
  on the standard library. *Proposed:*
  - number and vector types that work in the engine's integer units;
  - a seeded random generator per session instead of `math.random`;
  - time as session ticks, with no `os.time`;
  - no `io`, `os`, `debug`, `load` of bytecode, or other ways out of the sandbox.
- **Limits.** *Proposed:* each script context has a memory cap and an instruction budget per tick,
  counted rather than timed: **8,000 Lua instructions per tick**, about 10 ms (30% of a tick) on
  DS, the slowest CPU (§16).
- **Lua decides; the engine loops.** *Proposed, from §16:* the consoles run roughly 600–1,500 Lua
  instructions per millisecond, so per-object, per-tick work belongs in engine functions written
  in C, with Lua setting them up and reacting to their results:
  - part velocity, tweens and animation playback, instead of moving parts in Lua every tick;
  - overlap, region and raycast queries, instead of Lua loops over parts;
  - timers and events fired by the engine;
  - text formatting for UI (`string.format` costs about 0.2 ms per call on N64 and DS).
- The API is versioned (§12) and only grows: old experiences keep working.

## 8. Input

The engine reads each controller into one **virtual controller**; a **Lua input controller** in the
experience decides what its buttons do. *Decided.*

- The virtual controller has a move direction (8 directions, plus analog strength where the
  hardware has it) and six buttons, `a`, `b`, `c`, `d`, `l`, `r`, named for the virtual controller,
  not the console. *Decided.* Each port maps its controller to them once.
- Experiences bind actions to those buttons in Lua (`input.bind("jump", "a")`, and read
  `input.held("jump")`, `input.pressed("jump")`), so Jump and Run are just the default controller's
  bindings, not engine features. Movement and camera have engine defaults an experience can
  replace.
- **Combinations** register as their own input. *Decided.* *Proposed* rules:

```lua
input.bind("dash", {"l", "a"})   -- l + a together is "dash"
input.bind("jump", "a")
```

  - Buttons pressed within a short window (about 2 ticks) count as pressed together.
  - The most specific binding wins: while `l` + `a` is held, `dash` fires and `jump` doesn't.
  - Releasing any button of a combination ends it.
  - A combination can include a direction (`{"l", "up"}`), which helps the Wii Remote's small
    button count.
- **Menu** is never available to experiences (§14.2).
- Touch, the Wii Remote's pointer, motion and second screens are never exposed to experiences;
  the engine may use them (UI focus, keyboard taps, bottom-screen help).

Today's mapping, which becomes the default controller:

| Platform | Move | Jump | Run | Orbit | Zoom | Camera mode | Menu |
|---|---|---|---|---|---|---|---|
| PC, web | WASD | Space | Shift | Arrows, mouse | Z / X | Q | Esc |
| N64 | Stick | A | B | D-pad | C-up / C-down | R | Start |
| GameCube | Stick | A | B | C-stick / D-pad | L / R | Z | Start |
| Wii Remote | D-pad | A | 1 | B + D-pad | − / + | 2 | Home |
| DS | D-pad | A | B | R + D-pad | X / Y | L | Start |
| 3DS | Circle pad | A | B | D-pad | L / R | X | Start |

The camera takes an analog look stick as well as the orbit buttons: pushed part way it turns
part way, and the buttons count as pushed all the way. The mouse on PC and web is read as that
stick, by how far it moves each tick, capped at all the way, so mouse players turn no faster
than anyone else. *Decided.*

The camera (engine-run for now; experiences will control it through the Lua API) has three
modes: third person, following behind and swinging round as the player walks; first person
from the eyes; and fixed, detached at a spot in the scene and turning to keep the player in
view. A scene gives its fixed camera's spot with `camera = {x, y, z}` (units, y from the floor).
The camera can also view something other than the player (another player, or riding on a part).
The camera button cycles the modes.

## 9. Assets

Every asset has a **canonical form** that every platform can load, and may carry **variants**
transcoded for particular platforms. *Decided.*

Rules for variants. *Proposed.*

1. A variant changes only how something looks or sounds, never the game. Collision never comes
   from rendered triangles; Lua sees asset IDs, never per-platform details.
2. Variants are generated from the canonical form by tools, not made by hand, and can be
   regenerated at any time. A missing or invalid variant falls back to the canonical form.
3. Budgets are checked against the canonical form.

| Asset | Source | Canonical form | Variants (examples) |
|---|---|---|---|
| Model | OBJ / glTF with UVs | 16-bit integer vertices, 8.8 UVs, per-triangle colour | N64 / GX display lists, DS v16 |
| Texture | PNG, ≤ 16 colours | 4-bit indexed + palette | N64 TMEM layout, GX tiles, 3DS Morton RGB565 |
| Font | engine-built | 1-bit glyph atlas | per-platform texture layout |
| Music | XM | XM patterns + IMA-ADPCM samples | samples as VADPCM (N64), DSP-ADPCM (GameCube, Wii, 3DS) |
| Sound | WAV | IMA-ADPCM, mono | as above |

### 9.1 Models

- **Whole-part animation only.** *Decided.* A model may contain named sub-objects; animation moves,
  rotates or scales whole sub-objects, either from keyframe tracks stored in the model or driven by
  Lua. No skeletons. Keyframe rotations are 45° steps (§5), and animation is drawn **smoothly**
  between them. *Decided.* *Proposed:* sub-objects without collision may rotate about any axis,
  since that only affects drawing; anything with collision turns about the vertical axis only. On the network the host owns animation time; clients render it.
- **Collision lives in the model file**, as namespaced objects. *Decided.* *Proposed* naming: objects
  called `collision` or `col_*` become collision shapes and are never drawn. Each must reduce to a
  box or wedge (the importer fits it, or rejects it with a clear error); up to about 8 per model.
  A model without collision objects collides as its part's box.
- **Collision shapes follow sub-object animation.** *Decided.* *Proposed:* position and scale follow
  smoothly, while a shape's rotation snaps to the nearest 45° step, so collision stays as cheap
  as for rotated parts.

### 9.2 Textures and lighting

- **16 colours per texture.** *Decided.* Shading and shadows suggest more colours on screen.
- **Lighting is static.** *Decided.* Either per-face shading or a simple baked light map, computed
  when the experience is built; consoles never calculate lighting.
- The DS draws only one texture per triangle, so it can't layer a light map. *Proposed:* the
  canonical form bakes light and shadow into vertex colours, splitting large faces where a shadow
  edge crosses them; platforms that can layer textures may use a light-map variant for sharper
  shadows.

### 9.3 Fonts

*Decided:* one fixed font embedded in the engine, used for all text, error messages and the hub.
Custom fonts may become an asset type later. *Proposed:* printable ASCII plus a few symbols
(arrows, ©) and per-platform button icons, so "Press Ⓐ" shows the right button everywhere.

### 9.4 Audio

*Decided:* music in a tracker format. *Proposed:* XM, about 8 channels. Players exist for N64
(libdragon), DS (maxmod) and PC (libxmp); GameCube/Wii and 3DS need a small XM mixer. Sound
effects are IMA-ADPCM in canonical form.

## 10. Networking

**Deferred.** Networking is out of scope for now; this records the idea so other parts of the
design don't rule it out.

- **Hosts:** dedicated servers, or a PC hosting a session. The backend will be **open source**, so
  anyone can host their own experience services. *Decided.* Consoles are clients. Offline play runs
  a host inside the same program.
- **Traffic:** clients send input each tick; the host sends state changes. Clients predict their
  own player and correct to the host.
- **Joining** a running session means downloading the assets the current scene needs (§11), then a
  state snapshot.
- **Chat** is filtered, rate-limited and logged on the host, never on consoles (§11.5).
- Later: the N64's link speed and latency, which set snapshot rates and download sizes.

## 11. Content and distribution

### 11.1 The `.ws64` file

*Decided:* one experience is one file, `<experience>.ws64`, holding everything for every platform.

```
experience.ws64
  header   magic "WS64", format version, experience id + version, required API version, content hash
  index    [entry: kind, asset id, variant (canonical | n64 | gx | ds | 3ds), scene mask, offset, size, flags, hash]
  logic    manifest, scenes (part trees, settings), UI layouts, Lua source   — one copy, run on the host
  assets   canonical blobs + optional variant blobs
  (signature slot, reserved)
```

*Proposed:*

- Little-endian, read byte by byte, so big-endian N64 and GameCube/Wii parse it like everyone else
  (as the prototype's place loader does).
- The index comes first, so a console reads the header, the logic and only the blobs it needs, and
  seeks past the rest. The scene mask says which scenes use each asset.
- Identical blobs are stored once.
- **Compression:** a per-entry flag, LZ4-class, fast enough to decompress on N64 and DS.
- **Signing:** a reserved slot, unused by local files, so catalogue files can be verified later
  without changing the format.
- The manifest lists the experience's modes (single player, multiplayer up to N), title and icon.

### 11.2 Sources

The loader doesn't care where a `.ws64` came from. *Decided.*

| Source | Use |
|---|---|
| Built-in | the hub and demos, compiled into the program |
| Local storage | SD card, Wii USB, a PC folder; mostly single-player experiences |
| Network | catalogue and multiplayer sessions |
| Sideload | `wiiload`, `3dslink`, a flashcart's USB port, a watched folder on PC: for creators testing on hardware |

### 11.3 Network delivery

*Proposed.*

- A console fetches the header, index and logic, then only the blobs for the scene it is entering,
  in its own variant or the canonical form. Large experiences download a scene at a time.
- Blobs are named by hash, so shared assets download once and updates only fetch what changed.
- Hashes are checked per blob; everything is validated on load regardless.
- **Background downloads.** *Decided:* may continue while playing if the budgets allow, at a reduced
  rate so gameplay keeps its bandwidth; full speed in the lobby or while loading.
- **Caching.** *Decided:* the program caches experiences. A finished download is stored as an
  ordinary `.ws64` (with only that platform's variants) and can be played offline.
  *Proposed:* least-recently-played eviction with a size cap per platform.
- **N64:** *Decided:* no cache for now; revisit when N64 networking is in focus.

### 11.4 Saves

*Proposed.* A small key-value store per experience through the Lua API, keyed by experience ID and
stored next to it, never inside the `.ws64`, so updates can't wipe progress. Saves carry the
experience version so an update can migrate them. Limits must fit the smallest storage: the N64
Controller Pak holds 32 KB total, so about 1–2 KB per experience there.

### 11.5 Text between players

*Decided:* free typing for everyone; sending chat requires an account. The host filters messages,
enforces rate and length limits, and supports per-player mute, block and reporting. Experiences
receive only filtered messages, and only if they ask for chat access. Accounts are deferred (§15).

### 11.6 Development format

*Decided:* creators work on an experience as a **folder of plain files**, and a tool packs the
folder into a `.ws64`. Manifest, scenes and UI are **Lua tables**; scripts are sorted into
folders by role; models carry their own settings in OBJ + MTL; scenes are trees of groups and parts.

```
course/                        tools/ws64.py course/  →  course.ws64
  experience.lua               manifest
  scenes/course.lua            one file per scene
  scripts/server/main.lua      session server script; other files here are server modules
  scripts/client/main.lua      each player's client script; other files here are client modules
  scripts/shared/*.lua         modules both sides can require
  ui/*.lua                     UI layouts (§14)
  models/crate.obj, crate.mtl
  textures/crate.png
  music/*.xm, sounds/*.wav
```

**Names are paths.** *Decided.* An asset or scene is named by its path within its folder,
without the extension: `models/crate.obj` is `"crate"`, `models/props/barrel.obj` is
`"props/barrel"`. Nothing is declared twice. The packer assigns integer IDs; Lua and the
network only ever see those IDs and names.

**Data files are Lua tables.** *Decided.* Manifest, scene and UI files ship in the `.ws64`
**unchanged**, and the console reads them with the Lua it already has, so there is no second
parser to secure. *Proposed* rules:

- Each file is one `return { … }` chunk, loaded as text only (never bytecode) into an empty
  environment under the instruction limit (§7).
- Allowed values: tables, strings, numbers and booleans. Functions are rejected, so data stays
  plain and a future build mode can rewrite these files.
- After loading, the C side checks every field, range, name and budget, as for any untrusted
  input (§1.6), then copies the result into flat structs and lets the Lua tables be freed.
- Lengths are in units and may have decimals (`1.5`); the loader rounds them to thousandths.
  Colours are `{r, g, b}`, 0–255. Rotation is in degrees, a multiple of 45, about the vertical
  axis (§5).

```lua
-- experience.lua
return {
    id = "workshop64.course",   -- stable forever; saves are keyed by it (§11.4)
    title = "Obstacle Course",
    version = 3,               -- this experience's own version, bumped on each release
    api = 1,                   -- Lua API version it needs (§12)
    players = 4,               -- 1 = single player
    start = "course",          -- scenes/course.lua
    icon = "icon",             -- textures/icon.png
}
```

```lua
-- scenes/course.lua
return {
    sky = {120, 170, 230}, fog = {120, 170, 230}, draw_distance = 60, kill_y = -20,
    camera = {9, 6, 9},        -- optional: where the fixed camera stands
    script = "course",         -- optional: scripts/server/course.lua runs while this scene is loaded

    {name = "baseplate", pos = {0, -0.5, 0}, size = {40, 1, 40}, color = {90, 150, 90}, material = "grass"},
    {name = "spawn", kind = "spawn", pos = {0, 0, 4}, size = {2, 0.2, 2}},
    {name = "stairs", pos = {0, 0, -6}, rot = 45,                    -- a group: no size
        {pos = {0, 0.3, 2},  size = {2, 0.6, 2}, color = {196, 40, 28}},
        {pos = {0, 0.6, 0},  size = {2, 1.2, 2}, color = {245, 205, 48}},
        {pos = {0, 0.9, -2}, size = {2, 1.8, 2}, color = {75, 151, 75}, shape = "wedge"},
    },
    {name = "crate", pos = {2.5, 0.5, -1}, size = {1, 1, 1}, model = "crate"},
}
```

**Scenes are trees.** *Decided.* A node with a `size` is a part; a node without one is a
**group**, and its list entries are its children. *Proposed:*

- A group's `pos` and `rot` apply to everything inside it. Scripts can find, move, show and
  hide a group as one.
- Part fields: `name`, `kind` (`"part"` or `"spawn"`), `shape` (`"block"` or `"wedge"`),
  `pos`, `size`, `rot`, `color`, `material`, `model`, `collides` (default true), `transparency`.
  Everything except `pos` and `size` is optional.
- Names are optional and don't have to be unique; scripts look nodes up by path
  (`"stairs/top"`) or by search.
- In memory the tree is a flat array of nodes, each with its parent's index (§4: plain data, no
  pointers), so it can be snapshotted and sent.
- Placing a child under a turned group rounds its world position to the nearest thousandth,
  using the integer sin/cos tables, so every platform gets the same result.

**Scripts are sorted by role.** *Decided.* Server scripts are in `scripts/server/`, client
scripts in `scripts/client/`, shared modules in `scripts/shared/`. *Proposed:*

- `server/main.lua` runs once per session; `client/main.lua` runs once per player. A scene's
  `script` adds a server script that runs while that scene is loaded.
- `require "util"` looks in the script's own role folder first, then in `shared/`. A client
  script can never load a server module, so server-only logic and secrets stay on the host
  once networking arrives.

**Models are OBJ + MTL.** *Decided.* A model is exported at its real size in units (Z-up, front
facing -Y, as today). The `.mtl` supplies each material's colour (`Kd`) and texture (`map_Kd`,
resolved by file name from `textures/`). *Proposed:* the model is centred on X/Z with its base
at the part's bottom, as today; named objects (`o`/`g`) are its sub-objects, and `collision` /
`col_*` objects are its collision shapes (§9.1).

**Packing.** *Decided:* `tools/ws64.py <folder> <file.ws64>` (Python standard library only)
copies the logic files byte for byte and converts assets to their canonical forms (and later
variants, §9), checking the assets as it goes. It doesn't read the Lua files: the engine's own
loader is the one place logic is checked, so there is no second implementation to drift. Run the
result in the PC build or the core tests to check it; each problem names the file and the node
(`scenes/course.lua/stairs/2: unknown field 'rot'`), with a line number for Lua errors. Every
console checks everything again on load.

**Running without packing.** *Proposed:* the PC build can run an experience folder directly and
reload it when a file changes, with strict mode (§13) on, so creators can iterate without packing.

## 12. Versioning

*Decided:* engine versioning is important.

- **Engine version:** the program. Players in one session must run the same engine version.
- **API version:** the Lua API. A `.ws64` declares the API version it needs; an older engine refuses
  it with "update required" instead of half-loading. The API only adds; nothing is removed or
  changed in place.
- **Format version:** the `.ws64` layout. Unknown index entry kinds are skipped, so new asset kinds
  don't break older readers.
- Updating the engine on N64 means a new ROM, so updates are rare: a stable API matters more
  than speed of change.

## 13. Budgets

One set, per scene, sized to the weakest platform. *Proposed* starting points, to be measured on
DS and N64 hardware before they're fixed:

| Budget (per scene) | Limited by | Starting point |
|---|---|---|
| Triangles on screen | DS hardware limit (2048/frame), N64 fill rate | ~1500, avatars included |
| Texture size | N64 texture memory (4 KB) | ≤ 64×64, 32×32 typical |
| Texture memory | DS texture VRAM | ~128 KB canonical |
| RAM for the experience | DS 4 MB, N64 4 MB (no Expansion Pak required), minus engine and Lua | ~1 MB, enforced by the Lua memory cap |
| Script work | DS CPU | 8,000 Lua instructions per tick (~10 ms on DS) |
| Parts | collision and draw cost | 256 (prototype value) |
| Collision shapes per model | collision cost | ~8 |
| Audio | DS channels, N64 software mixing | ~8 voices |
| UI quads and glyphs | fill rate | to measure |
| `.ws64` download per scene | N64 RAM and link speed | ~256 KB per platform |
| Save data | N64 Controller Pak | 1–2 KB per experience |

The cost of the avatar matters: avatars get at most 160 triangles each (today's has 146), so four
fit in what the DS's 2048 leaves beside a full 1400-triangle scene.

**Strict mode on PC.** *Proposed:* the PC build can enforce every budget live (memory caps,
instruction limits, triangle and texture counts, the 256×192 UI safe area) and report breaches
as N64 or DS would hit them, so testing on PC in strict mode means it works everywhere. CI can run
the same checks on every `.ws64`.

## 14. UI and text

### 14.1 Layout and elements

*Decided:* anchored layout, no scaling.

- UI positions itself by anchors (edges, centre) against the real screen: 240 high (192 on DS)
  and as wide as its shape (§1.4). Lua can read the size.
- Every UI must fit the **256×192 safe area**. Text is never scaled.
- *Proposed* elements: Frame (colour or 9-slice image), Image, Text (wrapping, alignment), Button,
  List (stacks children), Bar (progress/health). Each has anchors, position, size, visibility and
  draw order.
- UI is per player and run by client scripts.

### 14.2 Navigation

*Decided:* d-pad focus navigation is the universal method. Touch and the Wii pointer are turned
into the same focus and select events. Lua has control for custom experiences.

- Default: focus moves to the nearest focusable element in the pressed direction. A selects,
  B goes back.
- HUD elements can't take focus. Opening a menu captures input until it closes; open menus form a
  stack and B closes the top one.
- *Proposed* Lua API:

```lua
ui.focus(button)                        -- move focus explicitly
button.nav = {up = a, down = b}         -- explicit neighbours; nil keeps automatic
button.nav.left = false                 -- block a direction
menu.focus_scope = true                 -- focus can't leave this frame
menu.wrap = true                        -- leaving one edge enters the opposite one
button.focused:connect(fn)              -- gained / lost
button.activated:connect(fn)            -- A
menu.back:connect(function() return true end)  -- B; return true to handle it
menu.edge:connect(function(dir) end)    -- pressed past the edge: page, switch tabs
ui.capture(frame)                       -- raw directions and buttons while open (grids, minigames)
```

- **Start/Menu belongs to the engine** (resume, settings, leave). *Decided.* Experiences can't take
  it or block leaving; they may add their own pause screen inside the engine menu.

### 14.3 Text input

*Decided:* one simple engine keyboard, Xbox-style, on every platform. Native system keyboards are
not used, to keep one look; physical keyboards (PC, USB on Wii) type into the same keyboard
screen, and DS/3DS touch can tap its keys.

```
 ┌──────────────────────────────┐
 │ Name your pet:               │
 │ [Biscuit_          ]  7/16   │
 │                              │
 │  a b c d e f g h i j   ⌫     │
 │  k l m n o p q r s t   ␣     │
 │  u v w x y z - _ . '   ⇧     │
 │  1 2 3 4 5 6 7 8 9 0   ✓     │
 │                              │
 │ Ⓑ delete  Ⓨ space  Ⓛ/Ⓡ move  │
 └──────────────────────────────┘
```

*Proposed:*

- Experiences call `ui.prompt_text{title=…, max=16, charset="name"|"number"|"text"}` and get the
  result in a callback. The engine enforces the length and character set (disallowed keys are
  greyed out).
- Alphabetical grid, fits the safe area with the fixed font; Shift and a symbols page.

| Action | N64 | GameCube | Wii Remote | DS / 3DS | PC |
|---|---|---|---|---|---|
| Type key | A | A | A | A | any key |
| Backspace (cancel when empty) | B | B | B | B | Backspace |
| Space | C-down | Y | 1 | Y | Space |
| Shift / symbols | C-up | X | 2 | X | Shift |
| Move cursor | L / R | L / R | − / + | L / R | arrows |
| Confirm | Start | Start | ✓ key | Start | Enter |

- Chat adds recent-message recall and preset phrases on a shoulder button: typing on a controller
  is slow.

## 15. Deferred and rejected

| Topic | Status |
|---|---|
| Physics (unanchored parts) | Deferred. Parts are anchored or scripted for now. |
| Accounts | Deferred. Required for chat when they exist. |
| Building experiences | Manual for now: a hand-written experience folder (§11.6) packed by a tool. An in-engine build mode comes later. |
| Networking | Deferred. The server-controlled idea (§10) is kept in mind, not designed. |
| N64 caching and networking specifics | Deferred until N64 networking is in focus. |
| Terrain (heightmaps, voxels) | Rejected: the world is parts only. |
| Standalone export | Rejected: workshop64 isn't a general game engine. |
| Native system keyboards | Not used, to keep one look. |
| Skeletal animation, dynamic lighting | Out of scope. |

## 16. Measurements

### Lua 5.4 on N64 and DS

Measured with a one-off benchmark ROM (`bench/lua`, last present in commit fe0db4a) in ares v148
and melonDS 1.1, on 2026-10-13. Emulator timing is close to hardware but not exact; check out
that commit to rebuild the ROMs for hardware. Each workload runs for 30 ticks; "instr" counts Lua VM instructions only,
not work done inside C functions such as `string.format`.

| Per tick | N64, 64-bit | N64, 32-bit | DS, 64-bit | DS, 32-bit |
|---|---|---|---|---|
| 64 parts moved (1,476 instr) | 1.25 ms | 1.01 ms | 1.93 ms | 1.87 ms |
| 496 box-pair checks (7,770 instr) | 6.05 ms | 5.03 ms | 10.70 ms | 9.98 ms |
| 200 event callbacks (2,880 instr) | 2.21 ms | 1.82 ms | 3.53 ms | 3.12 ms |
| 10 formatted strings (150 instr) | 2.15 ms | 2.22 ms | 2.08 ms | 2.07 ms |
| 50 short-lived tables (453 instr) | 4.35 ms | 3.30 ms | 4.19 ms | 3.99 ms |
| All of the above (12,746 instr) | 16.8 ms (50%) | 14.4 ms (43%) | 22.8 ms (68%) | 21.5 ms (65%) |
| Lua instructions per ms | 760–1,300 | 890–1,550 | 560–820 | 590–920 |

| Memory and loading | N64, 64-bit | N64, 32-bit | DS, 64-bit | DS, 32-bit |
|---|---|---|---|---|
| New state with libraries | 10 KB, 5.3 ms | 8 KB, 4.2 ms | 10 KB, 4.9 ms | 8 KB, 5.0 ms |
| Compile 2.3 KB of source | 19.2 ms, +8 KB | 18.7 ms, +7 KB | 12.7 ms, +8 KB | 12.5 ms, +7 KB |
| After setup / peak | 29 / 59 KB | 23 / 49 KB | 29 / 59 KB | 23 / 49 KB |

For reference, the same benchmark on a desktop PC runs 200,000–400,000 instructions per ms; the
whole "all of the above" tick takes 0.05–0.06 ms.

What it means:

- **Lua fits.** Memory is tens of KB and compiling is fast enough to do on every load.
- **CPU is the limit.** A busy experience written all in Lua would use half to two-thirds of a
  30 Hz tick. Hence the 8,000-instruction budget and "Lua decides; the engine loops" (§7).
- **32-bit Lua** is faster on N64 and uses about 20% less memory; on DS it's about 5% faster.
- **Not tried yet**, per-port optimizations (§1.3): on DS, placing the VM's main loop
  (`luaV_execute`) in the ARM9's fast instruction memory (ITCM) and comparing Thumb with ARM code;
  tuning the garbage collector, which dominates the short-lived-table case.

## Open questions

1. **Lua on hardware:** confirm the §16 numbers on a real N64 and DS.

Settled since the first draft: `.ws64`; 30 Hz tick; smooth animation with collision that follows
it; rotation about the vertical axis only; the six-button virtual controller with combinations;
script errors return to the hub; no Expansion Pak (N64 budgets assume 4 MB); networking deferred;
Lua's cost measured (§16); the development format (§11.6).

## Current prototype vs this spec

What the prototype already has, and what changes:

| Area | Prototype today | Spec |
|---|---|---|
| Platforms, CI | All seven (with the web) build in Docker/CI, releases on tags | Keep |
| Maths | Integer core, `isin`/`icos` | Keep |
| World | Fixed 20×20 floor + up to 256 unrotated box parts | Parts only, baseplate as a part, wedges, 45° rotation, scene settings |
| Content | `experiences/course/` (§11.6) packed into a `.ws64` with logic, canonical textures and models; built in. Scenes take parts, groups and a fixed camera spot (no rotation, wedges or other scene settings yet); one scene per experience | Variants, sessions with several scenes, the rest of §11.6 |
| Assets | OBJ models, 16-colour PNG textures converted on load | Canonical forms + tool-made variants; collision in models |
| Rendering | Each `main.c` draws the level itself; back faces are culled everywhere, and avatars and models off screen are skipped (`view_sees`); the level is one mesh, drawn whole | Core draw list; ports render it their own way |
| Timing | Fixed 30 Hz tick on every platform (`ticker_due` in the core); drawn at display rate without interpolation | Keep; interpolate where a port can afford it |
| Avatar | 146-triangle low-poly cartoon figure (square head, dress body, two-section box limbs bending at elbows and knees), animated as whole parts (§9.1: idle, walk, run, jump, fall, eased between), coat-coloured dress | Count it against the budget; customisation as data |
| Scripting | Lua 5.4.9 (submodule, 32-bit numbers, no standard libraries) reads the logic files with memory and instruction caps; scripts don't run yet | §7 |
| Text, chat | The engine font (5x7, `assets/font.txt`, built into C); a local-only chat on PC and web (`/` to type, echoed as `TestPlayer: <msg>`) | §9.3, §11.5, §14.3 |
| UI, audio, networking, saves | None | §14, §9.4, §10, §11.4 |

Suggested order of work:

1. ~~Fixed tick everywhere~~ (done), and a headless core with replay and state-hash tests.
2. ~~Lua on N64 and DS: measure cost; settle the script budget and number types.~~ Done (§16).
3. The draw list and platform layer, starting with PC, then N64.
4. The experience folder (§11.6) and `.ws64` with sessions and scenes, replacing the place file.
5. Parts: baseplate, wedges, 45° rotation and collision.
6. UI, the fixed font and the keyboard.
7. Audio.
8. Networking and the open-source host, when it's back in scope.
