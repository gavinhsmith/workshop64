#ifndef WORLD_H
#define WORLD_H

#include <stddef.h>
#include <stdint.h>

/* Every platform renders at this size (DS excepted) and scales up. */
#define SCREEN_W 320
#define SCREEN_H 240

/* 16 colours, 4 bits per pixel, rows top to bottom, low nibble = left pixel.
 * TEXTURE_MIN..TEXTURE_MAX pixels per side, powers of two: 32x32 at 16 bits is 2 KB, which
 * fits the N64's 4 KB of texture memory without palette support. */
#define TEXTURE_MIN 8
#define TEXTURE_MAX 32
typedef struct {
    int w, h;
    uint8_t palette[16][3];
    const uint8_t *pixels; /* w * h / 2 bytes */
    int slot;              /* the renderer's handle for it, assigned when it's uploaded */
} texture;

/* Palette index of pixel (x, y). */
static inline int texel(const texture *t, int x, int y)
{
    uint8_t b = t->pixels[(y * t->w + x) / 2];
    return x & 1 ? b >> 4 : b & 15;
}

/* Triangles with one colour each, optionally textured (the colour then tints the texture).
 * Coordinates are 8.8 fixed point: MESH_ONE = 1 unit, or the whole texture for uvs. */
#define MESH_ONE 256
typedef struct {
    const int16_t (*verts)[3];  /* tris * 3 corners */
    const uint8_t (*colors)[3]; /* one per triangle */
    int tris;
    const int16_t (*uvs)[2];    /* tris * 3 corners, or NULL when untextured */
    texture *texture;           /* NULL when untextured */
} mesh;

#define FLOOR_TILES 10 /* per side, 2 units each, centred on the origin */
#define FLOOR_Y -1     /* where the floor is drawn; game heights are measured from it */

extern const mesh guy_mesh; /* feet at y = 0, facing -Z; see tools/obj2c.py */
extern texture guy_texture;      /* its atlas, coat in grey: palette entries guy_texture_coat_first..15 */
extern const int guy_texture_coat_first;
extern mesh level_mesh;      /* floor + box parts of the current place, built by world_init() */

/* Lengths are in thousandths of a unit, angles in tenths of a degree. Heights (y) are
 * measured from the floor. Everything here is plain integers so it can be saved or sent
 * over a network as-is and runs identically on every platform. */

/* An axis-aligned box. With a mesh, it's drawn as that model (origin at the box's bottom
 * centre) and still collides as the box. */
typedef struct {
    int x, y, z;    /* centre */
    int sx, sy, sz; /* size */
    uint8_t color[3];
    int mesh;       /* index into the place's meshes, or -1 for a plain box */
} part;

/* A place: the content of one scene, loaded from an experience (.wb64) so it can come from
 * the network later. Format: see tools/wb64.py. Everything is validated on load. */
typedef struct {
    part *parts;
    int part_count;
    mesh *meshes;
    int mesh_count;
    texture *textures;
    int texture_count;
} place;

enum {
    PLACE_OK, PLACE_BAD_MAGIC, PLACE_BAD_VERSION, PLACE_TRUNCATED, PLACE_BAD_ENTRY,
    PLACE_BAD_INDEX, PLACE_BAD_VALUE, PLACE_OVER_BUDGET, PLACE_NO_MEMORY,
    PLACE_BAD_DATA,     /* a logic file (Lua) failed to load or has bad fields */
    PLACE_NEEDS_UPDATE, /* needs a newer Lua API than ENGINE_API */
};
#define WB64_VERSION 1 /* .wb64 format version */
#define ENGINE_API 1   /* Lua API version this engine provides (docs/engine.md section 12) */
#define PLACE_MAX_PARTS 256
#define PLACE_MAX_MESHES 32
#define PLACE_MAX_TEXTURES 16
#define PLACE_MAX_TRIS 1400 /* floor + parts; leaves the DS's 2048 room for 4 avatars */
#define PLACE_MAX_COORD 50000 /* positions and sizes; keeps 8.8 mesh coords in range */

/* Load an experience (.wb64) from bytes into *pl: its models, textures and start scene.
 * On failure returns a PLACE_* error, leaves *pl empty and says what was wrong in
 * experience_error. */
int experience_load(place *pl, const uint8_t *data, size_t size);
extern char experience_error[128];
void place_free(place *pl);

/* The built-in experience, packed from experiences/course. */
extern const uint8_t builtin_experience[];
extern const unsigned builtin_experience_size;

/* Held buttons for one tick. Movement is relative to the camera's view. */
enum {
    MOVE_LEFT = 1 << 0, MOVE_RIGHT = 1 << 1, MOVE_FWD = 1 << 2, MOVE_BACK = 1 << 3,
    CAM_LEFT = 1 << 4, CAM_RIGHT = 1 << 5, CAM_UP = 1 << 6, CAM_DOWN = 1 << 7,
    CAM_IN = 1 << 8, CAM_OUT = 1 << 9, JUMP = 1 << 10, RUN = 1 << 11,
};

/* One player's input for a tick: buttons plus the yaw of the camera they're looking
 * through, so "forward" means into their screen. */
typedef struct {
    unsigned buttons;
    int yaw;
} input;

#define PLAYER_RADIUS 350  /* collision box half-width */
#define PLAYER_HEIGHT 2400

typedef struct {
    int x, y, z;   /* feet */
    int vy;        /* vertical speed per tick */
    int on_ground;
    int heading;   /* 0..3599; 0 faces -Z, turns toward where it moves */
    int target_heading;
    int stride;    /* ticks into the running cycle while running on the ground, else 0 */
    uint8_t coat[3];
} player;

#define MAX_PLAYERS 4
typedef struct {
    player players[MAX_PLAYERS];
    int player_count;
} world;

#define PLAYER_LIMIT ((FLOOR_TILES - 1) * 1000) /* keep players on the floor */

/* Make `pl` the current place (used for collision and level_mesh, so it must outlive the
 * game) and return the starting state with `players` players, each with a default coat
 * colour and their avatar built. */
world world_init(const place *pl, int players);

/* Avatars: guy_mesh wearing player i's coat colour (the coat's palette entries swapped).
 * Draw player i with avatar_meshes[i]. Built for all MAX_PLAYERS by world_init(); call
 * avatar_build() again if a coat changes (then re-upload the texture). */
extern texture avatar_textures[MAX_PLAYERS];
extern mesh avatar_meshes[MAX_PLAYERS];
void avatar_build(int i, const uint8_t coat[3]);

/* How far to draw a player above their feet (thousandths): a little hop each running
 * step. Only for drawing; collision uses y. */
int player_bob(const player *p);

/* The game advances in fixed ticks, the same rate on every platform whatever the display
 * does. Each port asks a ticker how many ticks are due by its microsecond clock (any origin,
 * must not wrap) and runs that many world_update()s and camera_update()s. */
#define TICK_HZ 30
typedef struct {
    uint64_t next; /* when the next tick is due, in 1/TICK_HZ microseconds */
    int started;
} ticker;
int ticker_due(ticker *t, uint64_t now_us);

/* Advance one tick; inputs[i] drives players[i]. */
world world_update(world w, const input inputs[MAX_PLAYERS]);

/* A camera is per viewer, not game state. It follows one player and eases toward
 * the orbit/zoom target set by that viewer's buttons. After a moment of walking one way,
 * it slowly swings round behind them (not when they walk toward it). Platforms render with:
 *   view:   translate(0, 0, -dist) rotateX(pitch) rotateY(-yaw) translate(-x, -y, -z)
 *   level:  as is
 *   player: translate(x, FLOOR_Y + y + player_bob, z) rotateY(heading)                */
typedef struct {
    int x, y, z; /* point it looks at: their shoulders over the ground they stand on, plus half of a jump */
    int yaw;     /* increasing moves the camera right; not wrapped */
    int pitch;   /* positive looks down from above */
    int dist;
} view;

typedef struct {
    view now, target;
    int ground; /* height of the last thing the followed player stood on */
    int last_x, last_z, last_heading, walked; /* ticks they've walked one way */
} camera;

#define CAM_PITCH_MAX 800
#define CAM_PITCH_MIN -40 /* looking up a little; zoomed out, the camera stays a unit above the floor */
#define CAM_FOCUS 800     /* views aren't offset by FLOOR_Y, so this looks 1.8 above the feet: the shoulders */
#define CAM_DIST_MIN 3000
#define CAM_DIST_MAX 12000

camera camera_init(void);
camera camera_update(camera c, unsigned buttons, const player *follow);

/* sin/cos of whole degrees, scaled by 4096 (integer-only for the DS). */
int isin(int deg);
int icos(int deg);

#endif
