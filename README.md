# workshop64
Multi-platform demo for a simple 3D homebrew title for Nintendo 64 and beyond.

Currently a little cartoon figure walking around a checkered floor with a few parts to climb, rendered 240 lines high everywhere (DS: native 256x192) and scaled up; wider screens show a wider view (3DS, Wii in 16:9, PC and web at any window shape).

This is the prototype for **workshop64**, a small Roblox-like platform; see the
[engine specification](docs/engine.md) for where it's going.

| Folder | Platform | Library | Output |
|---|---|---|---|
| `core/` | shared game code (no platform headers) | — | — |
| `platform/n64/` | Nintendo 64 | libdragon (preview, OpenGL) | `build/n64/workshop64.z64` |
| `platform/ogc/` | GameCube + Wii | libogc (GX) | `build/ogc/workshop64_gamecube.dol`, `_wii.dol` |
| `platform/nds/` | DS | libnds | `build/nds/workshop64.nds` |
| `platform/3ds/` | 3DS | libctru + citro3d | `build/3ds/workshop64.3dsx` |
| `platform/pc/` | Windows / Linux / macOS | SDL3 + OpenGL | `build/pc/workshop64` |
| `platform/web/` | browsers | Emscripten, SDL3 + WebGL | `build/web/` (html + js + wasm) |

`tools/` holds the scripts the build runs; `tools/authoring/` holds ones you run by hand.

## Controls

The figure walks around a checkered floor and turns to face where it's going. Movement is
relative to the camera (forward = into the screen). The camera has three modes, cycled with
the camera button:

- **Third person** (the start): behind the figure, easing after it so it stays in frame.
  Walk one way for a moment and the camera swings round behind it (except when it's walking
  toward the camera). Orbit and zoom move the camera around it.
- **First person**: from its eyes; orbit looks around. Switching eases the camera in or out.
- **Fixed**: high in a corner of the course, standing still and turning to keep the figure in
  view, like an old Resident Evil camera. A scene sets where with `camera = {x, y, z}`; scenes
  without one skip this mode.

| Platform | Move | Jump | Run | Orbit / look | Zoom in / out | Camera | Exit |
|---|---|---|---|---|---|---|---|
| PC, web | WASD | Space | Shift | Arrows or mouse | Z / X | Q | Esc (PC) |
| N64 | Stick | A | B | D-pad | C-up / C-down | R | — |
| GameCube | Stick | A | B | C-stick / D-pad | L / R | Z | START |
| Wii Remote | D-pad | A | 1 | B + D-pad | − / + | 2 | HOME |
| DS | D-pad | A | B | R + D-pad | X / Y | L | START |
| 3DS | Circle pad | A | B | D-pad | L / R | X | START |

On the Wii a GameCube pad works too, with the GameCube layout.

Chat (for now it just echoes as `TestPlayer: <msg>`; nobody else sees it). The game ignores
input while it's open.

| Platform | Open | Keyboard | Send | Cancel |
|---|---|---|---|---|
| PC, web | `/` | your keyboard | Enter | Esc |
| 3DS | Y | the system keyboard | OK | Cancel |
| DS | SELECT | touch keyboard on the bottom screen | Enter | B |
| N64 | Z | on screen: D-pad moves, A types, B deletes | Start | B when empty |
| GameCube | Y | on screen, as N64 | START | B when empty |
| Wii Remote | B + 1 | on screen, as N64 | + | B when empty |

On DS and 3DS the chat is shown on the bottom screen.

On PC and web, click to capture the mouse for the camera; Esc lets it go (on PC, Esc again
quits). The mouse works like the GameCube's C-stick: moving it a little turns the camera a
little, and moving it fast turns it at full stick speed but never faster, so it's no faster
than a controller.

On PC, F11 toggles fullscreen. The window can be any size: the picture is drawn 240 lines high
and as wide as the window's shape (4:3 or wider), then scaled to fill it. The web build does the
same with the browser window.

## Building

Every build needs Python 3 (standard library only): the built-in experience is generated
into `build/gen` from `experiences/course/`. Lua is a submodule:
clone with `--recursive` or run `git submodule update --init`. On Windows, `make pc` and
`make test` use MSYS2's UCRT64 gcc and SDL3 (with `C:\msys64\ucrt64\bin` on PATH); from Git
Bash, put that folder before Git's own `/mingw64/bin`, or gcc picks up the wrong DLLs and fails
without a message.

```sh
make test   # core unit tests (host compiler)
make pc     # needs CMake + SDL3
make n64    # consoles build in Docker, same images as CI
make ogc
make nds
make 3ds
make web    # also in Docker
make        # everything
make n64-reset  # rebuild the N64 image (platform/n64/Dockerfile) with the latest toolchain + libdragon
```

Finished files go to `build/<platform>/` (see the table above).

## Running

- N64: ares, or a flashcart (SummerCart64, EverDrive)
- GameCube/Wii: Dolphin; on hardware via Swiss (GC) or the Homebrew Channel (copy
  `build/ogc/apps/` to the SD card) / `wiiload` (Wii)
- DS: melonDS 1.0+, or a flashcart. DeSmuME 0.9.13 shows white screens for anything built with libnds 2 / calico.
- 3DS: Azahar, or the Homebrew Launcher / `3dslink`
- Web: serve `build/web/` over HTTP (browsers won't load wasm from `file://`), e.g.
  `python3 -m http.server -d build/web`, then open http://localhost:8000

## Experiences

An experience is a folder of plain files (see [the spec, section 11.6](docs/engine.md#116-development-format)),
packed into one `.ws64` file that is loaded and validated at runtime by `core/ws64.c`, so it
can be delivered over the network later. The built-in experience is `experiences/course/`,
packed into C by the build:

```
experiences/course/
  experience.lua          manifest: id, title, version, API version, start scene
  scenes/course.lua       the scene: a tree of parts and groups, as a Lua table
  models/crate.obj, .mtl  models at real size; colour and texture from the .mtl
  textures/crate.png
```

To pack one by hand:

```sh
python3 tools/ws64.py experiences/course course.ws64
```

The packer converts models and textures and copies the Lua files as they are; the engine
checks those when it loads them (any mistake names the file and part), so run the result in
the PC build or `make test`. The format is described at the top of `tools/ws64.py`. Budgets
(parts, models, textures, triangles) are in `core/world.h` and are sized for the weakest
console. Textures are PNGs with at most 16 colours, up to 32 px per side (32x32 fits the N64's
texture memory), padded to a power of two if needed; they are drawn nearest-filtered and
tinted by the model's colour.

The avatar is part of the engine, built in C (`core/avatar.c`): a chunky low-poly cartoon
figure (a square head on a dress-like body, arms and legs of two equal, nearly cube-shaped
sections each, with round gloves and big shoes; 146 triangles, so four fit the DS beside a
full scene) whose arms and legs swing at the shoulders and hips and bend at the elbows and
knees. The face is a 16x16 texture drawn as ASCII art in the source. Each
player's dress and sleeves wear their coat colour. Its animations (idle,
walk, run, jump, fall) are poses computed each tick from what the player is doing; when the
animation changes, the pose eases from the old one over 0.2 s.

The icon is `icon.svg`. `tools/authoring/icons.py` (needs `rsvg-convert`) renders it into
`assets/icon/` for each build: the PC window and Windows `.exe`, the DS and 3DS menus, the
Wii's Homebrew Channel and the web page's tab. Run it after changing the SVG and commit the
results. The N64 and GameCube formats have no icon.

Models are OBJ files exported Z-up with the front facing -Y (as from OwlCAD), with faces wound
counter-clockwise seen from outside: every build culls faces seen from behind.

## CI

Every push and PR runs tests and builds all platforms (artifacts on the run). Pushing a `v*` tag publishes a GitHub release with a zip per platform.
