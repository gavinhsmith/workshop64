#include "world.h"
#include <stdlib.h>

static const place *level; /* the current place, set by world_init() */

#define FLOOR_TRIS (FLOOR_TILES * FLOOR_TILES * 2)
static int16_t level_verts[PLACE_MAX_TRIS * 3][3]; /* experience_load() keeps us within the budget */
static uint8_t level_colors[PLACE_MAX_TRIS][3];
mesh level_mesh = {.verts = level_verts, .colors = level_colors};

/* Rates are per tick (TICK_HZ, 30 a second). */
#define MOVE_SPEED 120 /* thousandths of a unit per tick: 3.6 units a second */
#define RUN_SPEED 220
#define DIAGONAL 2896 /* 1/sqrt(2), scaled by 4096 */
#define ORBIT_SPEED 70 /* tenths of a degree per tick: 210 degrees a second */
#define ZOOM_SPEED 200
#define JUMP_SPEED 200 /* peaks about 1.15 units up after 0.4 s */
#define GRAVITY 16
#define STEP_HEIGHT 400 /* ledges up to this high are walked up, not jumped */
#define EASE 4      /* the camera closes 1/EASE of the gap each tick */
#define TURN_EASE 2 /* players turn faster than the camera follows */
#define BEHIND_DELAY 22 /* ticks of walking one way before the camera swings behind */
#define BEHIND_SPEED 10 /* tenths of a degree per tick: 30 degrees a second */
#define TICK_CATCHUP 4  /* most ticks run at once to catch up; time beyond that is dropped */

/* Whether triangle a, b, c turns counter-clockwise seen from outside, `inside` being a point
 * inside the shape: the platforms cull triangles seen from behind. */
static int faces_out(const int *a, const int *b, const int *c, const int *inside)
{
    long long u[3], v[3], m[3];
    for (int i = 0; i < 3; i++) {
        u[i] = b[i] - a[i];
        v[i] = c[i] - a[i];
        m[i] = (a[i] + b[i] + c[i]) / 3 - inside[i];
    }
    return (u[1] * v[2] - u[2] * v[1]) * m[0] + (u[2] * v[0] - u[0] * v[2]) * m[1] + (u[0] * v[1] - u[1] * v[0]) * m[2] > 0;
}

/* Append a quad (corners in order round its edge) as two triangles facing away from
 * `inside`. Coordinates in thousandths. */
static int add_quad(int t, const int c[4][3], const int inside[3], const uint8_t color[3], int shade)
{
    static const int forward[6] = {0, 1, 2, 0, 2, 3}, backward[6] = {0, 2, 1, 0, 3, 2};
    const int *corner = faces_out(c[0], c[1], c[2], inside) ? forward : backward;
    for (int k = 0; k < 6; k++)
        for (int a = 0; a < 3; a++)
            level_verts[t * 3 + k][a] = c[corner[k]][a] * MESH_ONE / 1000;
    for (int i = 0; i < 2; i++)
        for (int a = 0; a < 3; a++)
            level_colors[t + i][a] = color[a] * shade / 100;
    return t + 2;
}

static void build_level_mesh(void)
{
    const int fy = FLOOR_Y * 1000;
    int t = 0;
    for (int i = 0; i < FLOOR_TILES; i++) {
        for (int j = 0; j < FLOOR_TILES; j++) {
            int x0 = (2 * i - FLOOR_TILES) * 1000, z0 = (2 * j - FLOOR_TILES) * 1000;
            const int c[4][3] = {{x0, fy, z0}, {x0, fy, z0 + 2000}, {x0 + 2000, fy, z0 + 2000}, {x0 + 2000, fy, z0}};
            uint8_t shade = (i + j) % 2 ? 60 : 90;
            const uint8_t color[3] = {shade, shade, shade + 10};
            const int below[3] = {x0 + 1000, fy - 1000, z0 + 1000}; /* seen from above only */
            t = add_quad(t, c, below, color, 100);
        }
    }
    /* Box parts: flat-shaded by face direction so the edges read without lighting. Mesh
     * parts are drawn by the platforms with their own model. */
    for (int i = 0; i < level->part_count; i++) {
        const part *p = &level->parts[i];
        if (p->mesh >= 0)
            continue;
        int x0 = p->x - p->sx / 2, x1 = p->x + p->sx / 2;
        int y0 = fy + p->y - p->sy / 2, y1 = fy + p->y + p->sy / 2;
        int z0 = p->z - p->sz / 2, z1 = p->z + p->sz / 2;
        const int faces[5][4][3] = {
            {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}, /* top */
            {{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}, /* +X */
            {{x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}}, /* -X */
            {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, /* +Z */
            {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, /* -Z */
        };
        static const int shade[5] = {100, 80, 80, 65, 65};
        const int centre[3] = {p->x, fy + p->y, p->z};
        for (int f = 0; f < 5; f++)
            t = add_quad(t, faces[f], centre, p->color, shade[f]);
    }
    level_mesh.tris = t;
}

world world_init(const place *pl, int players)
{
    static const uint8_t coats[MAX_PLAYERS][3] = {
        {196, 40, 28}, {13, 105, 172}, {75, 151, 75}, {245, 205, 48}, /* red, blue, green, yellow */
    };
    level = pl;
    build_level_mesh();
    world w = {0};
    w.player_count = players;
    for (int i = 0; i < players; i++) {
        w.players[i].x = i * 1500; /* side by side, standing on the floor */
        w.players[i].on_ground = 1;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        for (int a = 0; a < 3; a++)
            w.players[i].coat[a] = coats[i][a];
        avatar_build(i, coats[i]);
    }
    return w;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Bhaskara I's approximation: within 0.2% of sin. */
int isin(int deg)
{
    deg %= 360;
    if (deg < 0)
        deg += 360;
    int sign = 1;
    if (deg >= 180) {
        deg -= 180;
        sign = -1;
    }
    int p = deg * (180 - deg);
    return sign * 4 * 4096 * p / (40500 - p);
}

int icos(int deg)
{
    return isin(deg + 90);
}

/* Move cur 1/rate of the way to target, at least 1 so it always arrives. */
static int ease(int cur, int target, int rate)
{
    int d = target - cur, step = d / rate;
    if (step == 0 && d != 0)
        step = d > 0 ? 1 : -1;
    return cur + step;
}

static int wrap3600(int a)
{
    return (a % 3600 + 3600) % 3600;
}

/* The part the player's box overlaps, or NULL. Touching (e.g. standing on top) isn't
 * overlapping. */
static const part *hit_part(const player *p)
{
    for (int i = 0; i < level->part_count; i++) {
        const part *q = &level->parts[i];
        if (p->x + PLAYER_RADIUS > q->x - q->sx / 2 && p->x - PLAYER_RADIUS < q->x + q->sx / 2 &&
            p->z + PLAYER_RADIUS > q->z - q->sz / 2 && p->z - PLAYER_RADIUS < q->z + q->sz / 2 &&
            p->y + PLAYER_HEIGHT > q->y - q->sy / 2 && p->y < q->y + q->sy / 2)
            return q;
    }
    return 0;
}

/* Move *pos (p->x or p->z) by d. Walking into a part either steps up onto it (if
 * grounded and it's low enough, with room above) or stops flush against its face. */
static void move_axis(player *p, int *pos, int d, int along_x)
{
    if (d == 0)
        return;
    int old = *pos;
    *pos = clamp(*pos + d, -PLAYER_LIMIT, PLAYER_LIMIT);
    const part *q = hit_part(p);
    if (!q)
        return;

    int top = q->y + q->sy / 2, old_y = p->y;
    if (p->on_ground && top - p->y <= STEP_HEIGHT) {
        p->y = top;
        if (!hit_part(p))
            return;
        p->y = old_y;
    }

    int lo = along_x ? q->x - q->sx / 2 : q->z - q->sz / 2;
    int hi = along_x ? q->x + q->sx / 2 : q->z + q->sz / 2;
    *pos = d > 0 ? lo - PLAYER_RADIUS : hi + PLAYER_RADIUS;
    /* Another part in the way, or flush would mean moving backwards: stay put. */
    if (hit_part(p) || (d > 0 ? *pos < old : *pos > old))
        *pos = old;
}

static player update_player(player p, input in)
{
    /* Strafe relative to the camera: forward is away from it, along the ground. */
    int right = !!(in.buttons & MOVE_RIGHT) - !!(in.buttons & MOVE_LEFT);
    int fwd = !!(in.buttons & MOVE_FWD) - !!(in.buttons & MOVE_BACK);
    int s = isin(in.yaw / 10), c = icos(in.yaw / 10);
    int speed = in.buttons & RUN ? RUN_SPEED : MOVE_SPEED;
    int step = right && fwd ? speed * DIAGONAL / 4096 : speed; /* same speed diagonally */

    /* One axis at a time, so walls block one direction and you slide along the other. */
    int old_x = p.x, old_z = p.z;
    move_axis(&p, &p.x, (right * c - fwd * s) * step / 4096, 1);
    move_axis(&p, &p.z, (-right * s - fwd * c) * step / 4096, 0);

    /* Jump from the floor or a part; gravity brings you back. */
    if ((in.buttons & JUMP) && p.on_ground)
        p.vy = JUMP_SPEED;
    /* Gravity first, so standing still still pushes into the ground each tick and
     * on_ground stays set. */
    p.vy -= GRAVITY;
    int dy = p.vy;
    p.y += dy;
    p.on_ground = 0;
    const part *q = hit_part(&p);
    if (q && dy <= 0) { /* landed on top */
        p.y = q->y + q->sy / 2;
        p.vy = 0;
        p.on_ground = 1;
    } else if (q) { /* bumped your head */
        p.y = q->y - q->sy / 2 - PLAYER_HEIGHT;
        p.vy = 0;
    }
    if (p.y <= 0) {
        p.y = p.vy = 0;
        p.on_ground = 1;
    }

    avatar_step(&p, p.x != old_x || p.z != old_z, !!(in.buttons & RUN));

    /* Face the way you're moving: the camera's yaw plus the stick direction
     * (forward 0, left +90, right -90, back 180; diagonals in between). */
    if (right || fwd) {
        static const int offset[3][3] = {
            /* fwd: -1     0     1 */
            {1350, 900, 450},    /* right = -1 */
            {1800, 0, 0},        /* right =  0 */
            {-1350, -900, -450}, /* right =  1 */
        };
        p.target_heading = wrap3600(in.yaw + offset[right + 1][fwd + 1]);
    }
    /* Turn the short way round. */
    int turn = wrap3600(p.target_heading - p.heading + 1800) - 1800;
    p.heading = wrap3600(p.heading + ease(0, turn, TURN_EASE));
    return p;
}

int ticker_due(ticker *t, uint64_t now_us)
{
    /* Time is kept in units of 1/TICK_HZ microseconds, so ticks stay exact over time. */
    uint64_t now = now_us * TICK_HZ;
    if (!t->started) {
        t->started = 1;
        t->next = now;
    }
    int due = 0;
    while (now >= t->next && due < TICK_CATCHUP) {
        t->next += 1000000;
        due++;
    }
    if (now >= t->next) /* still behind after a stall (or a pause): drop the rest */
        t->next = now + 1000000;
    return due;
}

world world_update(world w, const input inputs[MAX_PLAYERS])
{
    for (int i = 0; i < w.player_count; i++)
        w.players[i] = update_player(w.players[i], inputs[i]);
    return w;
}

camera camera_init(void)
{
    view v = {0, CAM_FOCUS, 0, 0, 150, CAM_DIST};
    return (camera){.now = v, .target = v, .mode = CAM_THIRD, .zoom = CAM_DIST};
}

long long isqrt(long long v)
{
    long long r = 0;
    for (long long bit = 1LL << 62; bit; bit >>= 2) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
    }
    return r;
}

int iatan2(int y, int x)
{
    if (!x && !y)
        return 0;
    long long ax = llabs(x), ay = llabs(y);
    long long t = ax > ay ? ay * 4096 / ax : ax * 4096 / ay; /* tan of the angle to the nearer axis */
    /* atan(t) = 45t + t(1 - t)(14.02 + 3.80t) degrees on [0, 1], within about 0.1 degree */
    int a = (int)(450 * t / 4096 + t * (4096 - t) / 4096 * (1402 + 380 * t / 4096) / 40960);
    if (ay > ax)
        a = 900 - a;
    if (x < 0)
        a = 1800 - a;
    return y < 0 ? -a : a;
}

void look_from_motion(int dx, int dy, int *look_x, int *look_y)
{
    dx = clamp(dx, -10000, 10000), dy = clamp(dy, -10000, 10000); /* no overflow; capped below anyway */
    *look_x = dx * LOOK_FULL / MOUSE_FULL;
    *look_y = dy * LOOK_FULL / MOUSE_FULL;
}

int camera_hides(const camera *c, int player)
{
    /* Only once it's close: while easing in from third person you still see them. */
    return c->mode == CAM_FIRST && c->subject.kind == SUBJECT_PLAYER && c->subject.index == player &&
           c->now.dist < 800;
}

camera camera_update(camera c, input in, const world *w)
{
    unsigned buttons = in.buttons;
    if ((buttons & CAM_MODE) && !c.mode_held) {
        int fixed = level && level->has_camera;
        c.mode = c.mode == CAM_THIRD ? CAM_FIRST : c.mode == CAM_FIRST && fixed ? CAM_FIXED : CAM_THIRD;
        c.target.pitch = c.mode == CAM_FIRST ? 0 : 150; /* level eyes; looking down on them */
        if (c.mode == CAM_FIXED) {
            c.anchor[0] = level->camera[0];
            c.anchor[1] = FLOOR_Y * 1000 + level->camera[1]; /* views aren't offset by FLOOR_Y */
            c.anchor[2] = level->camera[2];
        }
    }
    c.mode_held = !!(buttons & CAM_MODE);

    /* Where the subject's feet are; a player also says whether they're grounded. */
    const player *follow = NULL;
    int x = c.subject.x, y = c.subject.y, z = c.subject.z;
    if (c.subject.kind == SUBJECT_PLAYER && c.subject.index >= 0 && c.subject.index < w->player_count) {
        follow = &w->players[c.subject.index];
        x = follow->x, y = follow->y, z = follow->z;
    } else if (c.subject.kind == SUBJECT_PART && level && c.subject.index >= 0 && c.subject.index < level->part_count) {
        const part *q = &level->parts[c.subject.index];
        x = q->x, y = q->y + q->sy / 2, z = q->z;
    }

    /* In third person the orbit buttons move the camera around the subject (right moves it
     * right, so the view turns left; up raises it, looking down more); in first person they
     * turn the view itself, so both are the other way round. */
    int right = c.mode == CAM_FIRST ? -ORBIT_SPEED : ORBIT_SPEED, up = right;
    /* The orbit buttons push the look stick all the way; the stick's direction counts in
     * full, its length only up to LOOK_FULL (a round gate: no faster on a diagonal). */
    int lx = in.look_x + (!!(buttons & CAM_RIGHT) - !!(buttons & CAM_LEFT)) * LOOK_FULL;
    int ly = in.look_y + (!!(buttons & CAM_DOWN) - !!(buttons & CAM_UP)) * LOOK_FULL;
    lx = clamp(lx, -4 * LOOK_FULL, 4 * LOOK_FULL), ly = clamp(ly, -4 * LOOK_FULL, 4 * LOOK_FULL);
    int len = (int)isqrt(lx * lx + ly * ly);
    if (len > LOOK_FULL) {
        lx = lx * LOOK_FULL / len;
        ly = ly * LOOK_FULL / len;
    }
    c.target.yaw += right * lx / LOOK_FULL;
    c.target.pitch -= up * ly / LOOK_FULL;

    if (c.mode == CAM_FIRST) {
        c.target.x = x;
        c.target.y = y + CAM_EYE;
        c.target.z = z;
        c.target.dist = 0;
        c.target.pitch = clamp(c.target.pitch, -CAM_LOOK_MAX, CAM_LOOK_MAX);
        c.ground = y;
        c.walked = 0;
    } else {
        /* Follow the ground a player stands on fully, but only half of a jump, so they stay
         * in frame and you still see the height. Dropping below it (walking off) counts too. */
        if (!follow || follow->on_ground || y < c.ground)
            c.ground = y;
        c.target.x = x;
        c.target.y = c.ground + (y - c.ground) / 2 + CAM_FOCUS;
        c.target.z = z;
    }
    if (c.mode == CAM_THIRD) {
        if (buttons & CAM_IN)  c.zoom -= ZOOM_SPEED;
        if (buttons & CAM_OUT) c.zoom += ZOOM_SPEED;
        c.zoom = clamp(c.zoom, CAM_DIST_MIN, CAM_DIST_MAX);
        c.target.dist = c.zoom;
        c.target.pitch = clamp(c.target.pitch, CAM_PITCH_MIN, CAM_PITCH_MAX);

        /* A player walking one way (moving, and facing within a few degrees of last tick,
         * which allows for the camera's own turning) for a while swings the camera behind
         * them, unless they're walking toward it or you're orbiting. */
        if (follow) {
            int moved = x != c.last_x || z != c.last_z;
            int steady = abs(wrap3600(follow->target_heading - c.last_heading + 1800) - 1800) < 50;
            c.walked = moved && steady && !lx ? c.walked + 1 : 0;
            c.last_heading = follow->target_heading;
            int behind = wrap3600(follow->target_heading - c.target.yaw + 1800) - 1800;
            if (c.walked > BEHIND_DELAY && abs(behind) < 1350)
                c.target.yaw += clamp(behind, -BEHIND_SPEED, BEHIND_SPEED);
        }
    }
    c.last_x = x;
    c.last_z = z;

    c.now.x = ease(c.now.x, c.target.x, EASE);
    c.now.y = ease(c.now.y, c.target.y, EASE);
    c.now.z = ease(c.now.z, c.target.z, EASE);
    c.now.yaw = ease(c.now.yaw, c.target.yaw, EASE);
    c.now.pitch = ease(c.now.pitch, c.target.pitch, EASE);
    c.now.dist = ease(c.now.dist, c.target.dist, EASE);
    if (c.mode == CAM_FIRST) { /* the eyes don't lag behind the head; the distance eases in */
        c.now.x = c.target.x;
        c.now.y = c.target.y;
        c.now.z = c.target.z;
    }
    if (c.mode == CAM_FIXED) {
        /* Stand exactly at the anchor, aimed at the (eased) look point: work the view's angles
         * and distance out from the two, keeping yaw continuous so easing out of this mode
         * doesn't spin the long way round. */
        int dx = c.anchor[0] - c.now.x, dy = c.anchor[1] - c.now.y, dz = c.anchor[2] - c.now.z;
        int across = (int)isqrt((long long)dx * dx + (long long)dz * dz);
        c.now.yaw += wrap3600(iatan2(dx, dz) - c.now.yaw + 1800) - 1800;
        c.now.pitch = iatan2(dy, across);
        c.now.dist = (int)isqrt((long long)across * across + (long long)dy * dy);
        c.target.yaw = c.now.yaw;
        c.walked = 0;
    }
    return c;
}
