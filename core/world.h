#ifndef WORLD_H
#define WORLD_H

#include <stddef.h>
#include <stdint.h>

/* Every platform renders SCREEN_H lines (DS: 192) and scales up. The width follows the
 * screen's shape at that height: SCREEN_W on a 4:3 screen, wider on wider ones (up to
 * SCREEN_W_MAX), never narrower. The vertical field of view is the same everywhere, so a
 * wider screen just sees more to the sides. */
#define SCREEN_W 320
#define SCREEN_H 240
#define SCREEN_W_MAX 1024

/* Rendering width for a w x h screen or window. */
static inline int screen_width(int w, int h)
{
    int sw = h > 0 ? SCREEN_H * w / h : SCREEN_W;
    return sw < SCREEN_W ? SCREEN_W : sw > SCREEN_W_MAX ? SCREEN_W_MAX : sw;
}

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
    int radius;                 /* thousandths: no corner is further from the origin (for culling) */
} mesh;

#define FLOOR_TILES 10 /* per side, 2 units each, centred on the origin */
#define FLOOR_Y -1     /* where the floor is drawn; game heights are measured from it */

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

/* A place: the content of one scene, loaded from an experience (.ws64) so it can come from
 * the network later. Format: see tools/ws64.py. Everything is validated on load. */
typedef struct {
    part *parts;
    int part_count;
    mesh *meshes;
    int mesh_count;
    texture *textures;
    int texture_count;
    int has_camera;
    int camera[3]; /* where the scene's fixed camera stands (y from the floor), if it has one */
} place;

enum {
    PLACE_OK, PLACE_BAD_MAGIC, PLACE_BAD_VERSION, PLACE_TRUNCATED, PLACE_BAD_ENTRY,
    PLACE_BAD_INDEX, PLACE_BAD_VALUE, PLACE_OVER_BUDGET, PLACE_NO_MEMORY,
    PLACE_BAD_DATA,     /* a logic file (Lua) failed to load or has bad fields */
    PLACE_NEEDS_UPDATE, /* needs a newer Lua API than ENGINE_API */
};
#define WS64_VERSION 1 /* .ws64 format version */
#define ENGINE_API 1   /* Lua API version this engine provides (docs/engine.md section 12) */
#define PLACE_MAX_PARTS 256
#define PLACE_MAX_MESHES 32
#define PLACE_MAX_TEXTURES 16
#define PLACE_MAX_TRIS 1400 /* floor + parts; leaves the DS's 2048 room for 4 avatars */
#define PLACE_MAX_COORD 50000 /* positions and sizes; keeps 8.8 mesh coords in range */

/* Load an experience (.ws64) from bytes into *pl: its models, textures and start scene.
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
    CAM_MODE = 1 << 12, /* switches the camera between third and first person when pressed */
};

/* One player's input for a tick: buttons, the yaw of the camera they're looking through (so
 * "forward" means into their screen), and an analog look stick (or a mouse, see
 * look_from_motion): -LOOK_FULL..LOOK_FULL each way, right and down positive. It adds to the
 * orbit buttons, which count as pushed all the way, and together they never turn the camera
 * faster than one stick pushed all the way. */
#define LOOK_FULL 1000
typedef struct {
    unsigned buttons;
    int yaw;
    int look_x, look_y;
} input;

#define PLAYER_RADIUS 350  /* collision box half-width */
#define PLAYER_HEIGHT 1600 /* the avatar is 1.54 tall */

/* The avatar's parts (ARM is the upper arm, LEG the thigh; a forearm hangs off its upper arm
 * at the elbow, a shin off its thigh at the knee, the face off the head), and a pose: how far
 * each part swings about its joint, on top of its parent's swing (tenths of a degree, positive
 * forward; an upper arm at 900 points straight ahead), and how high the whole avatar is lifted
 * off its feet (thousandths; only for drawing, collision uses y). */
enum {
    AVATAR_TORSO, AVATAR_HEAD, AVATAR_FACE, AVATAR_ARM_L, AVATAR_ARM_R, AVATAR_FOREARM_L, AVATAR_FOREARM_R,
    AVATAR_LEG_L, AVATAR_LEG_R, AVATAR_SHIN_L, AVATAR_SHIN_R, AVATAR_PARTS
};
typedef struct {
    int lift;
    int swing[AVATAR_PARTS];
} pose;

enum { ANIM_IDLE, ANIM_WALK, ANIM_RUN, ANIM_JUMP, ANIM_FALL };

typedef struct {
    int x, y, z;   /* feet */
    int vy;        /* vertical speed per tick */
    int on_ground;
    int heading;   /* 0..3599; 0 faces -Z, turns toward where it moves */
    int target_heading;
    int anim;      /* ANIM_*: what the avatar is doing */
    int anim_time; /* ticks since that started */
    int air;       /* ticks off the ground */
    int blend;     /* ticks left easing from `from` into the animation */
    pose from;     /* the pose when the animation last changed */
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

/* Avatars (core/avatar.c): a low-poly cartoon figure, at most 160 triangles in AVATAR_PARTS
 * parts (the face is its own part so only it is textured). Each part's mesh is around its
 * joint, so player i is drawn with pose = player_pose(p) as
 *   translate(x, FLOOR_Y + y + pose.lift, z) rotateY(heading), then for each part k:
 *   avatar_place(&pose, k, at, &angle); translate(at) rotateX(angle) avatar_parts[i][k]
 * The dress and sleeves wear player i's coat colour (triangle colours); the face part is textured
 * with avatar_texture. Built for all MAX_PLAYERS by world_init(); call
 * avatar_build() again if a coat changes. */
extern texture avatar_texture;
extern mesh avatar_parts[MAX_PLAYERS][AVATAR_PARTS];
extern const int avatar_joints[AVATAR_PARTS][3]; /* at rest; thousandths, from the feet */
extern const int avatar_parent[AVATAR_PARTS];    /* the part each hangs off, or -1 for the body */
void avatar_build(int i, const uint8_t coat[3]);

/* Where part k's joint is in pose p (its parent's swing moves it) and how far the part is
 * turned there (its swing plus its parent's: every joint turns about the same axis). */
void avatar_place(const pose *p, int k, int at[3], int *angle);

/* The pose to draw a player in: their animation, eased from the previous one for a moment
 * after it changes. */
pose player_pose(const player *p);

/* Pick and advance a player's animation after a tick's movement (used by world_update). */
void avatar_step(player *p, int moved, int running);

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

/* A camera is per viewer, not game state. It views a subject, by default the viewer's player,
 * in one of three modes:
 *   CAM_THIRD  from behind, easing after the subject so they stay in frame; after a moment of
 *              a player walking one way it swings round behind them (not when they walk
 *              toward it). The orbit and zoom buttons move it around them.
 *   CAM_FIRST  from the subject's eyes; the orbit buttons look around.
 *   CAM_FIXED  detached: it stands still at `anchor` and turns to keep the subject in view.
 * CAM_MODE cycles through them (easing across, except into CAM_FIXED, which cuts), offering
 * CAM_FIXED only where the scene has a fixed camera. The subject can also be another player,
 * a part (riding on its top) or a fixed point: set `subject`.
 * Platforms render with:
 *   view:   translate(0, 0, -dist) rotateX(pitch) rotateY(-yaw) translate(-x, -y, -z)
 *   level:  as is
 *   player: see avatar_parts above; skip any player camera_hides()                      */
typedef struct {
    int x, y, z; /* point it looks at (third person) or from (first person, dist 0) */
    int yaw;     /* increasing moves the camera right; not wrapped */
    int pitch;   /* positive looks down from above */
    int dist;
} view;

enum { CAM_THIRD, CAM_FIRST, CAM_FIXED };
enum { SUBJECT_PLAYER, SUBJECT_PART, SUBJECT_POINT };
typedef struct {
    int kind;    /* SUBJECT_* */
    int index;   /* which player or part */
    int x, y, z; /* a point: where its "feet" are, y from the floor (thousandths) */
} subject;

typedef struct {
    view now, target;
    int mode;   /* CAM_* */
    subject subject;
    int anchor[3]; /* where a CAM_FIXED camera stands, in view coordinates */
    int zoom;   /* third-person distance */
    int mode_held; /* CAM_MODE was held last tick: it switches on a press */
    int ground; /* height of the last thing a followed player stood on */
    int last_x, last_z, last_heading, walked; /* ticks they've walked one way */
} camera;

#define CAM_PITCH_MAX 800
#define CAM_PITCH_MIN -40 /* looking up a little; zoomed out, the camera stays above the floor */
#define CAM_LOOK_MAX 700  /* first person looks up or down this far */
#define CAM_FOCUS 300     /* views aren't offset by FLOOR_Y, so this looks 1.3 above the feet: the head */
#define CAM_EYE 250       /* and first person sees from 1.25 above the feet: the eyes */
#define CAM_DIST_MIN 2500
#define CAM_DIST_MAX 9000
#define CAM_DIST 5500     /* third-person distance to start with */

camera camera_init(void); /* third person, on player 0 */
camera camera_update(camera c, input in, const world *w);

/* Whether a platform should leave this player out: the camera is looking from their eyes. */
int camera_hides(const camera *c, int player);

/* Frustum culling: whether something might be on screen in view v, on a screen shaped
 * aspect_w:aspect_h. Only says no when it's surely off screen (behind the camera, past the
 * far plane or beyond an edge), so nothing pops at the edges. view_sees takes a sphere in
 * view coordinates (thousandths; y includes FLOOR_Y); the others are the spheres around a
 * player's avatar and around a model part's model. */
int view_sees(const view *v, int aspect_w, int aspect_h, int x, int y, int z, int r);
int player_visible(const view *v, int aspect_w, int aspect_h, const player *p);
int part_visible(const view *v, int aspect_w, int aspect_h, const part *q, const mesh *m);

/* A pointer's movement over one tick (pixels, right and down positive) as a look stick:
 * MOUSE_FULL pixels or more is pushed all the way that way, less is pushed part way. Like a
 * stick, a mouse never turns the camera faster than full speed. */
#define MOUSE_FULL 24
void look_from_motion(int dx, int dy, int *look_x, int *look_y);

/* sin/cos of whole degrees, scaled by 4096 (integer-only for the DS); the angle of (x, y)
 * from the x axis toward y, in tenths of a degree (-1800..1800); square root. */
int isin(int deg);
int icos(int deg);
int isin10(int tenths); /* the same for tenths of a degree */
int icos10(int tenths);
int iatan2(int y, int x);
long long isqrt(long long v);

/* Text in the engine font (assets/font.txt): printable ASCII (anything else draws as '?'),
 * FONT_W x FONT_H pixels, FONT_ADVANCE apart, lines FONT_LINE apart. text_draw calls `rect`
 * for each run of lit pixels, in screen pixels from x, y (the top left, y down). */
#define FONT_W 5
#define FONT_H 7
#define FONT_ADVANCE 6
#define FONT_LINE 9
int text_width(const char *s);
void text_draw(const char *s, int x, int y, void (*rect)(int x, int y, int w, int h, void *ctx), void *ctx);

/* A basic chat log (for now just local): the last CHAT_LINES lines, wrapped CHAT_COLS across
 * to fit the UI safe area, each with the tick it was said. */
#define CHAT_LINES 6
#define CHAT_COLS (256 / FONT_ADVANCE)
#define CHAT_MAX 64            /* characters in a message */
#define CHAT_SHOW (10 * TICK_HZ) /* how long a line stays up */
typedef struct {
    char lines[CHAT_LINES][CHAT_COLS + 1]; /* oldest first */
    int said[CHAT_LINES];
    int count;
} chat_log;

/* Add "who: msg" (printable ASCII only, the message cut to CHAT_MAX characters). */
void chat_say(chat_log *log, const char *who, const char *msg, int now);

/* The on-screen QWERTY keyboard, for consoles without a native one: KB_ROWS rows of
 * KB_COLS keys (the last row is Shift, Space, Del and OK), moved over with the d-pad. Feed it
 * the held KB_* buttons once a frame; it types into `text`. It says KB_SEND or KB_CANCEL
 * once those buttons are let go, so the press that closed it doesn't reach the game. */
#define KB_ROWS 5
#define KB_COLS 10
enum { KB_UP = 1, KB_DOWN = 2, KB_LEFT = 4, KB_RIGHT = 8, KB_TYPE = 16, KB_BACK = 32, KB_DONE = 64 };
enum { KB_TYPING, KB_SEND, KB_CANCEL };
typedef struct {
    char text[CHAT_MAX + 1];
    int row, col, shift;
    unsigned held;   /* last frame's buttons: each press counts once */
    int result;      /* KB_SEND or KB_CANCEL once closed, waiting for the buttons to be let go */
} keyboard;
keyboard keyboard_open(void);
int keyboard_update(keyboard *k, unsigned buttons);

/* Draw the chat over a screen_w x screen_h frame, in the bottom left of the UI safe area:
 * the log's recent lines, then, when draft isn't NULL, "> draft_" with the whole log; a
 * keyboard (if given) takes the bottom of the safe area and pushes the rest up. `rect` fills
 * screen pixels (y down) in a colour. */
void chat_draw(const chat_log *log, const char *draft, const keyboard *kb, int now, int screen_w, int screen_h,
               void (*rect)(int x, int y, int w, int h, const uint8_t rgb[3], void *ctx), void *ctx);

#endif
