/* The avatar: a chunky low-poly cartoon figure (a square head on a dress-like body, arms and
 * legs of two equal, nearly cube-shaped sections each, with round gloves and big shoes), and
 * its animations. Each part turns
 * about its joint (whole-part animation, docs/engine.md 9.1); the platforms draw the parts
 * with the pose from player_pose(). Under 160 triangles, so four fit the DS's budget beside
 * a full scene. All integer, like the rest of the core. */
#include <stdlib.h>
#include <string.h>
#include "world.h"

/* Neck, shoulders, elbows, hips and knees, in thousandths from the feet. Nothing is bigger
 * than the head. Arms angle out 35 degrees from the top of the body, legs 6 degrees from
 * under its hem; each section spans 190 between joints. */
const int avatar_joints[AVATAR_PARTS][3] = {
    [AVATAR_TORSO] = {0, 500, 0},
    [AVATAR_HEAD] = {0, 1000, 0},
    [AVATAR_FACE] = {0, 1000, 0},
    [AVATAR_ARM_L] = {130, 940, 0},
    [AVATAR_ARM_R] = {-130, 940, 0},
    [AVATAR_FOREARM_L] = {239, 784, 0},
    [AVATAR_FOREARM_R] = {-239, 784, 0},
    [AVATAR_LEG_L] = {130, 510, 0},
    [AVATAR_LEG_R] = {-130, 510, 0},
    [AVATAR_SHIN_L] = {150, 321, 0},
    [AVATAR_SHIN_R] = {-150, 321, 0},
};

const int avatar_parent[AVATAR_PARTS] = {
    [AVATAR_TORSO] = -1, [AVATAR_HEAD] = -1, [AVATAR_FACE] = AVATAR_HEAD,
    [AVATAR_ARM_L] = -1, [AVATAR_ARM_R] = -1, [AVATAR_FOREARM_L] = AVATAR_ARM_L, [AVATAR_FOREARM_R] = AVATAR_ARM_R,
    [AVATAR_LEG_L] = -1, [AVATAR_LEG_R] = -1, [AVATAR_SHIN_L] = AVATAR_LEG_L, [AVATAR_SHIN_R] = AVATAR_LEG_R,
};

/* The face, covering the front of the head: '.' skin, 'W' white, 'P' pupil, 'M' mouth. */
static const char *const face[16] = {
    "................",
    "................",
    "................",
    "................",
    "...WW......WW...",
    "..WWWW....WWWW..",
    "..WPPW....WPPW..",
    "..WPPW....WPPW..",
    "..WPPW....WPPW..",
    "...WW......WW...",
    "................",
    ".....M....M.....",
    "......MMMM......",
    "................",
    "................",
    "................",
};

enum { SKIN, COAT, PANTS, GLOVE, SHOE, FACE_LIT };
static const uint8_t material[][3] = {
    [SKIN] = {250, 200, 160}, [PANTS] = {45, 75, 175}, [GLOVE] = {240, 240, 240},
    [SHOE] = {120, 65, 35}, [FACE_LIT] = {255, 255, 255}, /* COAT: the player's colour */
};

#define PART_TRIS 24
static uint8_t face_pixels[16 * 16 / 2];
texture avatar_texture = {
    .w = 16, .h = 16, .pixels = face_pixels,
    .palette = {{250, 200, 160}, {255, 255, 255}, {30, 40, 110}, {120, 40, 40}}, /* skin, white, pupil, mouth */
};
static int16_t verts[AVATAR_PARTS][PART_TRIS * 3][3];
static uint8_t mats[AVATAR_PARTS][PART_TRIS], shades[AVATAR_PARTS][PART_TRIS];
static int tris[AVATAR_PARTS];
static int16_t face_uvs[PART_TRIS * 3][2];
static uint8_t colors[MAX_PLAYERS][AVATAR_PARTS][PART_TRIS][3];
mesh avatar_parts[MAX_PLAYERS][AVATAR_PARTS];

static long long isqrt(long long v)
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

/* sin and cos of t tenths of a degree, scaled by 4096. */
static int sin10(int t)
{
    t = (t % 3600 + 3600) % 3600;
    int d = t / 10;
    return isin(d) + (isin(d + 1) - isin(d)) * (t % 10) / 10;
}

static int cos10(int t)
{
    return sin10(t + 900);
}

/* One triangle (corners in thousandths from the feet) into part k, flat-shaded by how much
 * it faces a fixed light so the shape reads without lighting. */
static void add_tri(int k, const int *a, const int *b, const int *c, int mat)
{
    long long u[3], v[3];
    for (int i = 0; i < 3; i++) {
        u[i] = b[i] - a[i];
        v[i] = c[i] - a[i];
    }
    long long n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    static const long long light[3] = {400, 800, 450}; /* from above, front right; length ~1001 */
    long long len = isqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    long long dot = llabs(n[0] * light[0] + n[1] * light[1] + n[2] * light[2]);
    int t = tris[k]++;
    const int *corner[3] = {a, b, c};
    for (int i = 0; i < 3; i++)
        for (int ax = 0; ax < 3; ax++)
            verts[k][t * 3 + i][ax] = (int16_t)((corner[i][ax] - avatar_joints[k][ax]) * MESH_ONE / 1000);
    mats[k][t] = (uint8_t)mat;
    shades[k][t] = (uint8_t)(len ? 55 + 45 * dot / (len * 1001) : 100);
}

static void add_quad(int k, const int *a, const int *b, const int *c, const int *d, int mat)
{
    add_tri(k, a, b, c, mat);
    add_tri(k, a, c, d, mat);
}

/* A low-poly ellipsoid: `segs` around (the first starting `turn` tenths of a degree from
 * straight ahead, -Z), cut into bands at the latitudes in lat[0..bands] (degrees, from -90
 * or above up to 90; a band ending at a pole is a fan), band b in band_mat[b]. */
static void add_ellipsoid(int k, const int c[3], const int r[3], int segs, int turn, int bands,
                          const int *lat, const int *band_mat)
{
    for (int b = 0; b < bands; b++) {
        for (int s = 0; s < segs; s++) {
            int p[4][3];
            for (int i = 0; i < 4; i++) {
                int la = lat[b + (i >> 1)], lo = turn + (s + (i & 1)) * 3600 / segs;
                int ring = icos(la);
                p[i][0] = c[0] + r[0] * ring / 4096 * sin10(lo) / 4096;
                p[i][1] = c[1] + r[1] * isin(la) / 4096;
                p[i][2] = c[2] - r[2] * ring / 4096 * cos10(lo) / 4096;
            }
            if (lat[b] == -90)
                add_tri(k, p[0], p[3], p[2], band_mat[b]);
            else if (lat[b + 1] == 90)
                add_tri(k, p[0], p[1], p[2], band_mat[b]);
            else
                add_quad(k, p[0], p[1], p[3], p[2], band_mat[b]);
        }
    }
}

/* An open 8-sided tube tapering from an elliptical bottom (radii rb[0] across, rb[1] deep) to
 * an elliptical top, centres and radii in thousandths. */
static void add_frustum(int k, const int bot[3], const int rb[2], const int top[3], const int rt[2], int mat)
{
    for (int s = 0; s < 8; s++) {
        int a0 = 225 + s * 450, a1 = a0 + 450; /* flat faces front */
        int p[4][3] = {
            {bot[0] + rb[0] * sin10(a0) / 4096, bot[1], bot[2] - rb[1] * cos10(a0) / 4096},
            {bot[0] + rb[0] * sin10(a1) / 4096, bot[1], bot[2] - rb[1] * cos10(a1) / 4096},
            {top[0] + rt[0] * sin10(a1) / 4096, top[1], top[2] - rt[1] * cos10(a1) / 4096},
            {top[0] + rt[0] * sin10(a0) / 4096, top[1], top[2] - rt[1] * cos10(a0) / 4096},
        };
        add_quad(k, p[0], p[1], p[2], p[3], mat);
    }
}

/* A square prism (sides only: its ends are hidden) from a to b in the side-on plane, `half`
 * thick each way around the line between them. */
static void add_prism(int k, const int a[3], const int b[3], int half, int mat)
{
    int dx = b[0] - a[0], dy = b[1] - a[1];
    int len = (int)isqrt((long long)dx * dx + (long long)dy * dy);
    int ox = -dy * half / len, oy = dx * half / len; /* across the prism, in the side-on plane */
    int c[8][3];
    for (int i = 0; i < 8; i++) {
        const int *end = i & 4 ? b : a;
        int u = i & 1 ? 1 : -1, w = i & 2 ? 1 : -1;
        c[i][0] = end[0] + u * ox;
        c[i][1] = end[1] + u * oy;
        c[i][2] = end[2] + w * half;
    }
    add_quad(k, c[0], c[1], c[5], c[4], mat);
    add_quad(k, c[1], c[3], c[7], c[5], mat);
    add_quad(k, c[3], c[2], c[6], c[7], mat);
    add_quad(k, c[2], c[0], c[4], c[6], mat);
}

static void build_geometry(void)
{
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            static const char keys[] = ".WPM";
            int c = (int)(strchr(keys, face[y][x]) - keys);
            face_pixels[(y * 16 + x) / 2] |= (uint8_t)(x & 1 ? c << 4 : c);
        }
    }

    /* Head: a box; its front is the face part. */
    const int x0 = -270, x1 = 270, y0 = 1000, y1 = 1540, z0 = -250, z1 = 250;
    const int c[8][3] = {
        {x0, y0, z0}, {x1, y0, z0}, {x0, y1, z0}, {x1, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x0, y1, z1}, {x1, y1, z1},
    };
    add_quad(AVATAR_HEAD, c[2], c[6], c[7], c[3], SKIN); /* top */
    add_quad(AVATAR_HEAD, c[0], c[1], c[5], c[4], SKIN); /* bottom */
    add_quad(AVATAR_HEAD, c[7], c[6], c[4], c[5], SKIN); /* back */
    add_quad(AVATAR_HEAD, c[3], c[7], c[5], c[1], SKIN); /* +X */
    add_quad(AVATAR_HEAD, c[6], c[2], c[0], c[4], SKIN); /* -X */
    add_quad(AVATAR_FACE, c[3], c[2], c[0], c[1], FACE_LIT); /* front: top left as seen from the front first */
    static const int16_t face_uv[6][2] = {{0, 0}, {MESH_ONE, 0}, {MESH_ONE, MESH_ONE}, {0, 0}, {MESH_ONE, MESH_ONE}, {0, MESH_ONE}};
    memcpy(face_uvs, face_uv, sizeof face_uv);

    /* Body: a dress, wide at the hem and narrow at the neck (the head covers its top; the hem
     * is open, as nothing sees under it). */
    static const int hem[3] = {0, 500, 0}, hem_r[2] = {270, 200}, neck[3] = {0, 1020, 0}, neck_r[2] = {80, 55};
    add_frustum(AVATAR_TORSO, hem, hem_r, neck, neck_r, COAT);

    /* Limbs: two equal sections each, a little longer than they are thick, overlapping by
     * 20 at the elbow and knee so bending doesn't open a gap; the hand and shoe ride on the
     * lower section. */
    for (int side = 0; side < 2; side++) {
        int sx = side ? -1 : 1;
        int arm = side ? AVATAR_ARM_R : AVATAR_ARM_L, forearm = side ? AVATAR_FOREARM_R : AVATAR_FOREARM_L;
        int leg = side ? AVATAR_LEG_R : AVATAR_LEG_L, shin = side ? AVATAR_SHIN_R : AVATAR_SHIN_L;
        /* 10 more along the arm's 35-degree slope is (6, -8). */
        const int shoulder[3] = {130 * sx, 940, 0}, elbow_past[3] = {245 * sx, 776, 0};
        const int elbow_before[3] = {233 * sx, 792, 0}, wrist[3] = {348 * sx, 628, 0};
        add_prism(arm, shoulder, elbow_past, 80, COAT);
        add_prism(forearm, elbow_before, wrist, 80, COAT);
        const int glove_c[3] = {380 * sx, 580, 0}, glove_r[3] = {95, 95, 95};
        static const int glove_lat[3] = {-90, 0, 90}, glove_mat[2] = {GLOVE, GLOVE};
        add_ellipsoid(forearm, glove_c, glove_r, 6, 0, 2, glove_lat, glove_mat);

        /* 10 more along the legs' 6-degree slope is (1, -10). */
        const int hip[3] = {130 * sx, 510, 0}, knee_past[3] = {151 * sx, 311, 0};
        const int knee_before[3] = {149 * sx, 331, 0}, ankle[3] = {170 * sx, 132, 0};
        add_prism(leg, hip, knee_past, 85, PANTS);
        add_prism(shin, knee_before, ankle, 85, PANTS);
        const int shoe_c[3] = {175 * sx, 0, -60}, shoe_r[3] = {130, 170, 220}; /* open underneath: it sits on the floor */
        static const int shoe_lat[3] = {0, 45, 90}, shoe_mat[2] = {SHOE, SHOE};
        add_ellipsoid(shin, shoe_c, shoe_r, 5, 0, 2, shoe_lat, shoe_mat);
    }
}

void avatar_build(int i, const uint8_t coat[3])
{
    static int built;
    if (!built++)
        build_geometry();
    for (int k = 0; k < AVATAR_PARTS; k++) {
        for (int t = 0; t < tris[k]; t++) {
            const uint8_t *rgb = mats[k][t] == COAT ? coat : material[mats[k][t]];
            for (int a = 0; a < 3; a++)
                colors[i][k][t][a] = (uint8_t)(rgb[a] * shades[k][t] / 100);
        }
        int face = k == AVATAR_FACE;
        avatar_parts[i][k] = (mesh){verts[k], colors[i][k], tris[k], face ? face_uvs : NULL, face ? &avatar_texture : NULL};
    }
}

/* --- Animation ----------------------------------------------------------------------- */

#define BLEND_TICKS 6 /* a change of animation eases over 0.2 s */
#define FALL_DELAY 8  /* ticks off the ground before walking off an edge counts as falling: about half a unit */
#define RUN_STEP 10   /* ticks per running step; two make a cycle */
#define RUN_BOB 150   /* height of a running hop */

/* The pose of animation `anim`, `t` ticks in. Swings in tenths of a degree, positive
 * forward (an upper arm at 900 points straight ahead), lower limbs relative to the upper;
 * sin is scaled by 4096. */
static int pos(int v)
{
    return v > 0 ? v : 0;
}

static pose anim_pose(int anim, int t)
{
    pose p = {0};
    int s, c;
    switch (anim) {
    case ANIM_IDLE: /* a slow sway of the arms, elbows a little bent */
        s = isin(t * 6);
        p.swing[AVATAR_ARM_L] = 30 * s / 4096;
        p.swing[AVATAR_ARM_R] = -30 * s / 4096;
        p.swing[AVATAR_FOREARM_L] = p.swing[AVATAR_FOREARM_R] = 150;
        break;
    case ANIM_WALK: /* big swings; each knee bends as its leg comes through, each elbow bends
                     * more as its arm comes forward; a bounce each step */
        s = isin(t * 15), c = icos(t * 15);
        p.swing[AVATAR_LEG_L] = 450 * s / 4096;
        p.swing[AVATAR_LEG_R] = -450 * s / 4096;
        p.swing[AVATAR_SHIN_L] = -450 * pos(c) / 4096;
        p.swing[AVATAR_SHIN_R] = -450 * pos(-c) / 4096;
        p.swing[AVATAR_ARM_L] = -550 * s / 4096;
        p.swing[AVATAR_ARM_R] = 550 * s / 4096;
        p.swing[AVATAR_FOREARM_L] = 200 + 250 * pos(-s) / 4096;
        p.swing[AVATAR_FOREARM_R] = 200 + 250 * pos(s) / 4096;
        p.lift = 50 * abs(c) / 4096;
        break;
    case ANIM_RUN: /* wider swings, knees lifting high, elbows pumping sharply bent, and a
                    * hop each step */
        s = isin(t * 180 / RUN_STEP), c = icos(t * 180 / RUN_STEP);
        p.swing[AVATAR_LEG_L] = 650 * s / 4096;
        p.swing[AVATAR_LEG_R] = -650 * s / 4096;
        p.swing[AVATAR_SHIN_L] = -150 - 650 * pos(c) / 4096;
        p.swing[AVATAR_SHIN_R] = -150 - 650 * pos(-c) / 4096;
        p.swing[AVATAR_ARM_L] = -700 * s / 4096;
        p.swing[AVATAR_ARM_R] = 700 * s / 4096;
        p.swing[AVATAR_FOREARM_L] = p.swing[AVATAR_FOREARM_R] = 750; /* more and they'd reach the head */
        p.lift = RUN_BOB * abs(s) / 4096;
        break;
    case ANIM_JUMP: /* arms forward, forearms raised (no higher, or they'd reach the head), one knee tucked up */
        p.swing[AVATAR_ARM_L] = p.swing[AVATAR_ARM_R] = 700;
        p.swing[AVATAR_FOREARM_L] = p.swing[AVATAR_FOREARM_R] = 500;
        p.swing[AVATAR_LEG_L] = 450;
        p.swing[AVATAR_SHIN_L] = -800;
        p.swing[AVATAR_LEG_R] = -100;
        p.swing[AVATAR_SHIN_R] = -300;
        break;
    case ANIM_FALL: /* arms up and flailing, legs paddling with bent knees */
        s = isin(t * 40);
        p.swing[AVATAR_ARM_L] = 600 + 150 * s / 4096;
        p.swing[AVATAR_ARM_R] = 600 - 150 * s / 4096;
        p.swing[AVATAR_FOREARM_L] = p.swing[AVATAR_FOREARM_R] = 400;
        p.swing[AVATAR_LEG_L] = 200 * s / 4096;
        p.swing[AVATAR_LEG_R] = -200 * s / 4096;
        p.swing[AVATAR_SHIN_L] = p.swing[AVATAR_SHIN_R] = -400;
        break;
    }
    return p;
}

pose player_pose(const player *pl)
{
    pose p = anim_pose(pl->anim, pl->anim_time);
    if (pl->blend > 0) { /* ease from the pose the last animation was in */
        p.lift += (pl->from.lift - p.lift) * pl->blend / BLEND_TICKS;
        for (int k = 0; k < AVATAR_PARTS; k++)
            p.swing[k] += (pl->from.swing[k] - p.swing[k]) * pl->blend / BLEND_TICKS;
    }
    return p;
}

void avatar_place(const pose *p, int k, int at[3], int *angle)
{
    const int *j = avatar_joints[k];
    int parent = avatar_parent[k];
    if (parent < 0) {
        at[0] = j[0], at[1] = j[1], at[2] = j[2];
        *angle = p->swing[k];
        return;
    }
    /* Turn the joint about the parent's joint by the parent's swing (about X, like the
     * platforms' rotateX). */
    const int *pj = avatar_joints[parent];
    int a = p->swing[parent], s = sin10(a), c = cos10(a);
    int dy = j[1] - pj[1], dz = j[2] - pj[2];
    at[0] = j[0];
    at[1] = pj[1] + (dy * c - dz * s) / 4096;
    at[2] = pj[2] + (dy * s + dz * c) / 4096;
    *angle = a + p->swing[k];
}

void avatar_step(player *p, int moved, int running)
{
    p->air = p->on_ground ? 0 : p->air + 1;
    int anim = p->anim;
    if (p->on_ground)
        anim = !moved ? ANIM_IDLE : running ? ANIM_RUN : ANIM_WALK;
    else if (p->vy > 0 && p->anim == ANIM_JUMP)
        anim = ANIM_JUMP;
    else if (p->vy > 0 && p->air == 1)
        anim = ANIM_JUMP; /* just left the ground going up */
    else if (p->anim == ANIM_JUMP || p->air > FALL_DELAY)
        anim = ANIM_FALL; /* a brief step down a ledge isn't a fall */

    if (anim != p->anim) {
        p->from = player_pose(p);
        p->blend = BLEND_TICKS;
        p->anim = anim;
        p->anim_time = 0;
    } else {
        if (p->blend > 0)
            p->blend--;
        p->anim_time++;
    }
}
