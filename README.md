# hangout64
Multi-platform demo for a simple 3D homebrew title for Nintendo 64 and beyond.

Currently a little guy walking around a checkered floor with a few parts to climb, rendered at 320x240 everywhere (DS: native 256x192) and scaled up.

This is the prototype for **workbench64**, a small Roblox-like platform; see the
[engine specification](docs/engine.md) for where it's going.

| Folder | Platform | Library | Output |
|---|---|---|---|
| `core/` | shared game code (no platform headers) | — | — |
| `platform/n64/` | Nintendo 64 | libdragon (preview, OpenGL) | `.z64` |
| `platform/ogc/` | GameCube + Wii | libogc (GX) | `.dol` ×2 |
| `platform/nds/` | DS | libnds | `.nds` |
| `platform/3ds/` | 3DS | libctru + citro3d | `.3dsx` |
| `platform/pc/` | Windows / Linux / macOS | SDL3 + OpenGL | executable |
| `platform/web/` | browsers | Emscripten, SDL3 + WebGL | `build/web/` (html + js + wasm) |

`tools/` holds the scripts the build runs; `tools/authoring/` holds ones you run by hand when
editing assets.

## Controls

The guy walks around a checkered floor and turns to face where he's going; the camera
eases after him. Movement is relative to the camera (forward = into the screen). Orbit and
zoom ease too. Walk one way for a moment and the camera slowly swings round behind him
(except when he's walking toward it).

| Platform | Move | Jump | Run | Orbit camera | Zoom in / out | Exit |
|---|---|---|---|---|---|---|
| PC, web | WASD | Space | Shift | Arrows | Q / E | Esc (PC) |
| N64 | Stick | A | B | D-pad | C-up / C-down | — |
| GameCube | Stick | A | B | C-stick / D-pad | L / R | START |
| Wii Remote | D-pad | A | 1 | B + D-pad | − / + | HOME |
| DS | D-pad | A | B | R + D-pad | X / Y | START |
| 3DS | Circle pad | A | B | D-pad | L / R | START |

On the Wii a GameCube pad works too, with the GameCube layout.

On PC, F11 toggles fullscreen. The window can be any size: the picture is scaled to fill it,
keeping 4:3.

## Building

Every build needs Python 3 (standard library only): the built-in models, textures and place
are generated into `build/gen` from `assets/`. On Windows, where `python3` may be the Store stub,
pass `PYTHON=python` to make.

```sh
make test   # core unit tests (host compiler)
make pc     # needs CMake + SDL3
make n64    # consoles build in Docker, same images as CI
make ogc
make nds
make 3ds
make web    # also in Docker
make        # everything
make n64-reset  # drop the cached N64 toolchain + libdragon volume (e.g. after an image update)
```

## Running

- N64: ares, or a flashcart (SummerCart64, EverDrive)
- GameCube/Wii: Dolphin; on hardware via Swiss (GC) or the Homebrew Channel / `wiiload` (Wii)
- DS: melonDS 1.0+, or a flashcart. DeSmuME 0.9.13 shows white screens for anything built with libnds 2 / calico.
- 3DS: Azahar, or the Homebrew Launcher / `3dslink`
- Web: serve `build/web/` over HTTP (browsers won't load wasm from `file://`), e.g.
  `python3 -m http.server -d build/web`, then open http://localhost:8000

## Places

A place is the content of one game: parts (boxes, optionally drawn with a custom model), the
models they use, and the models' textures. Places are data, loaded and validated at runtime by `core/place.c`,
so they can be delivered over the network later. The built-in place is
`assets/scene/course.json`, packed into C by the build. To pack one by hand:

```sh
python3 tools/placec.py assets/scene/course.json course.place   # binary
```

The format is described at the top of `tools/placec.py`. Budgets (parts, meshes, textures,
triangles) are in `core/world.h` and are sized for the weakest console. Textures are PNGs with
at most 16 colours, up to 32 px per side (32x32 fits the N64's texture memory), padded to a
power of two if needed; they are drawn nearest-filtered and tinted by the model's colour.

Engine assets (like the avatar) are compiled in instead, with `tools/obj2c.py` (also run by the build). The avatar's
texture is the atlas `assets/texture/guy_base.png` with `guy_coat.png` layered on top; the
coat's greys are tinted with each player's coat colour at runtime by swapping palette
entries. `tools/authoring/guy_uvs.py` maps the atlas onto the model, stretching each face's region to fit it
(cutting tapered faces along texel columns so stripes stay straight) and hiding the leg and
arm ends. Run it on a fresh export of `guy.obj` (it rewrites the file and refuses to run twice):

```sh
python3 tools/authoring/guy_uvs.py assets/model/guy.obj
```

Models are OBJ files exported Z-up with the front facing -Y (as from OwlCAD).

## CI

Every push and PR runs tests and builds all platforms (artifacts on the run). Pushing a `v*` tag publishes a GitHub release with a zip per platform.
