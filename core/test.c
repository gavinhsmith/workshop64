/* Host-side checks for core. Run with `make -C core test`. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"

static place pl;

/* Run `ticks` ticks with player 0 holding `buttons`, looking along `yaw`. */
static world run(world w, unsigned buttons, int yaw, int ticks)
{
    input in[MAX_PLAYERS] = {{buttons, yaw, 0, 0}};
    while (ticks--)
        w = world_update(w, in);
    return w;
}

static camera follow(camera c, unsigned buttons, const world *w, int ticks)
{
    while (ticks--)
        c = camera_update(c, (input){buttons, 0, 0, 0}, w);
    return c;
}

/* Little-endian writers, and a .ws64 builder for hand-made and broken experiences. */
static void wr16(uint8_t *p, unsigned v) { p[0] = v & 0xff; p[1] = v >> 8; }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v & 0xffff); wr16(p + 2, v >> 16); }

typedef struct {
    unsigned kind; /* 1 logic, 2 texture, 3 model */
    const char *name;
    const void *data;
    size_t size;
} test_entry;

static uint8_t *build(const test_entry *e, int n, size_t *size)
{
    size_t off = 8, total;
    for (int i = 0; i < n; i++)
        off += 12 + strlen(e[i].name);
    total = off;
    for (int i = 0; i < n; i++)
        total += e[i].size;
    uint8_t *b = malloc(total), *p = b + 8;
    memcpy(b, "WS64", 4);
    wr16(b + 4, WS64_VERSION);
    wr16(b + 6, n);
    for (int i = 0; i < n; i++) {
        size_t len = strlen(e[i].name);
        p[0] = e[i].kind, p[1] = 0;
        wr16(p + 2, len);
        wr32(p + 4, off);
        wr32(p + 8, e[i].size);
        memcpy(p + 12, e[i].name, len);
        memcpy(b + off, e[i].data, e[i].size);
        off += e[i].size;
        p += 12 + len;
    }
    *size = total;
    return b;
}

/* A blob of the built-in experience, by kind and name. */
static const uint8_t *builtin(unsigned kind, const char *name, size_t *size)
{
    const uint8_t *d = builtin_experience, *p = d + 8;
    for (unsigned i = 0, n = d[6] | d[7] << 8; i < n; i++) {
        unsigned len = p[2] | p[3] << 8;
        if (p[0] == kind && len == strlen(name) && !memcmp(p + 12, name, len)) {
            *size = p[8] | p[9] << 8 | p[10] << 16 | (uint32_t)p[11] << 24;
            return d + (p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24);
        }
        p += 12 + len;
    }
    assert(!"no such entry");
    return NULL;
}

static const char *good_manifest = "return {id = 'test', title = 'Test', version = 1, api = 1, start = 'course'}";

/* The built-in experience's assets with this manifest and scene. */
static uint8_t *experience(const char *manifest, const char *scene, size_t *size)
{
    test_entry e[5] = {
        {1, "experience.lua", manifest, strlen(manifest)},
        {1, "scenes/course.lua", scene, strlen(scene)},
        {2, "crate", NULL, 0}, {3, "crate", NULL, 0}, {3, "statue", NULL, 0},
    };
    for (int i = 2; i < 5; i++)
        e[i].data = builtin(e[i].kind, e[i].name, &e[i].size);
    return build(e, 5, size);
}

static int load(const uint8_t *d, size_t size)
{
    place p;
    int err = experience_load(&p, d, size);
    assert(err == PLACE_OK || (p.parts == NULL && p.meshes == NULL && p.part_count == 0 && experience_error[0]));
    place_free(&p);
    return err;
}

/* Load an experience with this manifest and scene; any error must mention `says`. */
static int try_logic(const char *manifest, const char *scene, const char *says)
{
    size_t size;
    uint8_t *b = experience(manifest, scene, &size);
    int err = load(b, size);
    free(b);
    if (says && !strstr(experience_error, says)) {
        printf("expected \"%s\" in: %s\n", says, experience_error);
        assert(0);
    }
    return err;
}

static int try_scene(const char *scene, const char *says)
{
    return try_logic(good_manifest, scene, says);
}

static void test_experience_loader(void)
{
    /* The built-in experience: 11 parts; models in name order, so the crate (textured with
     * texture 0, a 16x16 crate) is model 0 and the statue (untextured) model 1. */
    assert(experience_load(&pl, builtin_experience, builtin_experience_size) == PLACE_OK);
    assert(experience_error[0] == '\0');
    assert(pl.part_count == 11 && pl.mesh_count == 2 && pl.texture_count == 1);
    assert(pl.parts[1].y == 300 && pl.parts[1].sy == 600 && pl.parts[1].mesh == -1);
    assert(pl.parts[4].y == 1650); /* 1.65 units */
    assert(pl.has_camera && pl.camera[0] == 9000 && pl.camera[1] == 6000 && pl.camera[2] == 9000);
    assert(pl.parts[7].mesh == 1 && pl.parts[7].color[0] == 230 && pl.parts[8].mesh == 0);
    /* The crates are a group at x 3.1; its parts are placed relative to it. */
    assert(pl.parts[8].x == 2500 && pl.parts[9].x == 3700 && pl.parts[10].x == 3100 && pl.parts[10].y == 1500);
    const mesh *statue = &pl.meshes[1], *crate = &pl.meshes[0];
    assert(statue->tris > 400 && statue->texture == NULL && statue->uvs == NULL);
    assert(statue->colors[0][0] <= 230 && statue->colors[0][0] > 100); /* the .mtl's gold, shaded */
    int lo = 0, hi = 0;
    for (int i = 0; i < statue->tris * 3; i++) {
        lo = statue->verts[i][1] < lo ? statue->verts[i][1] : lo;
        hi = statue->verts[i][1] > hi ? statue->verts[i][1] : hi;
    }
    assert(lo == 0 && abs(hi - 24 * MESH_ONE / 10) <= 1); /* kept at its size in the file */
    assert(crate->tris == 12 && crate->texture == &pl.textures[0] && crate->uvs != NULL);
    for (int i = 0; i < crate->tris * 3; i++) /* each face maps the whole texture */
        for (int a = 0; a < 2; a++)
            assert(crate->uvs[i][a] == 0 || crate->uvs[i][a] == MESH_ONE);
    const texture *tex = &pl.textures[0];
    assert(tex->w == 16 && tex->h == 16);
    const uint8_t *corner = tex->palette[texel(tex, 0, 0)], *nail = tex->palette[texel(tex, 1, 1)];
    assert(corner[0] == 104 && corner[1] == 75 && corner[2] == 28);  /* frame */
    assert(nail[0] == 177 && nail[1] == 177 && nail[2] == 177);      /* a nail */

    /* Every truncation is rejected cleanly. */
    for (size_t n = 0; n < builtin_experience_size; n++)
        assert(load(builtin_experience, n) != PLACE_OK);

    uint8_t *b = malloc(builtin_experience_size);
    memcpy(b, builtin_experience, builtin_experience_size);
    b[0] = 'X';
    assert(load(b, builtin_experience_size) == PLACE_BAD_MAGIC);
    memcpy(b, builtin_experience, builtin_experience_size);
    wr16(b + 4, WS64_VERSION + 1);
    assert(load(b, builtin_experience_size) == PLACE_BAD_VERSION);

    /* Bad references and values in assets. */
    size_t size;
    uint8_t *model = b + (builtin(3, "statue", &size) - builtin_experience);
    unsigned verts = model[0] | model[1] << 8;
    memcpy(b, builtin_experience, builtin_experience_size);
    wr16(model + 8 + verts * 10, verts); /* first corner: past the last vertex */
    assert(load(b, builtin_experience_size) == PLACE_BAD_INDEX);
    memcpy(b, builtin_experience, builtin_experience_size);
    wr16(model + 4, 1); /* statue -> texture 1, which doesn't exist */
    assert(load(b, builtin_experience_size) == PLACE_BAD_INDEX && strstr(experience_error, "models/statue"));
    memcpy(b, builtin_experience, builtin_experience_size);
    b[builtin(2, "crate", &size) - builtin_experience] = 6; /* 64 px wide: over TEXTURE_MAX */
    assert(load(b, builtin_experience_size) == PLACE_BAD_VALUE);
    free(b);

    /* Entries of unknown kinds are skipped; an empty scene is fine; no manifest is an error. */
    test_entry extra[] = {
        {1, "experience.lua", good_manifest, strlen(good_manifest)},
        {1, "scenes/course.lua", "return {}", 9},
        {99, "future", "?", 1},
    };
    b = build(extra, 3, &size);
    assert(load(b, size) == PLACE_OK);
    free(b);
    b = build(extra + 1, 2, &size);
    assert(load(b, size) == PLACE_BAD_DATA && strstr(experience_error, "experience.lua is missing"));
    free(b);

    /* Scenes: fields, values, models, and groups. */
    assert(try_scene("return {{pos = {0, 1, 0}, size = {1, 1, 1}}, {name = 'empty group', pos = {1, 0, 0}}}", NULL) == PLACE_OK);
    assert(try_scene("return {{pos = {0, 1, 0}, size = {1, 1, 1}, rot = 45}}", "unknown field 'rot'") == PLACE_BAD_DATA);
    assert(try_scene("return {{name = 'g', {pos = {0, 0, 0}, size = {1, 1, 1}, model = 'nope'}}}",
                     "scenes/course.lua/g/1: no model 'nope'") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {0, 0, 0}, size = {1, 1, 1}, color = function() end}}", "'color'") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {0, 0, 0}, size = {1, 1, 1}, color = {256, 0, 0}}}", "'color'") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {0, 0, 0}, size = {1, 0.0004, 1}}}", "positive") == PLACE_BAD_DATA);
    assert(try_scene("return {{size = {1, 1, 1}}}", "needs 'pos'") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {60, 0, 0}, size = {1, 1, 1}}}", "'pos'") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {40, 0, 0}, {pos = {20, 0, 0}, size = {1, 1, 1}}}}", "outside the world") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {0, 0, 0}, size = {1, 1, 1}, {pos = {0, 0, 0}, size = {1, 1, 1}}}}", "unexpected key") == PLACE_BAD_DATA);
    assert(try_scene("return {sky = {0, 0, 0}}", "unknown field 'sky'") == PLACE_BAD_DATA);
    assert(try_scene("return {camera = {0, 1}}", "'camera'") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {0, 0, 0}, size = {1, 1, 1}}, 'x'}", "entry 2 should be a table") == PLACE_BAD_DATA);
    assert(try_scene("local t = {} t[1] = t return {t}", "nested too deeply") == PLACE_BAD_DATA);
    char deep[1000] = "return ";
    for (int i = 0; i < 300; i++)
        strcat(deep, "{");
    for (int i = 0; i < 300; i++)
        strcat(deep, "}");
    assert(try_scene(deep, "overflow") == PLACE_BAD_DATA); /* stops early: consoles have small stacks */

    /* Lua errors carry the file and line; the sandbox has nothing in it; text only. */
    assert(try_scene("return {\n{pos = }", "scenes/course.lua:2:") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {math.floor(1), 0, 0}, size = {1, 1, 1}}}", "math") == PLACE_BAD_DATA);
    assert(try_scene("return {{pos = {0, 0, 0}, size = {#os.getenv('PATH'), 1, 1}}}", "os") == PLACE_BAD_DATA);
    assert(try_scene("\x1bLua", "binary") == PLACE_BAD_DATA);
    assert(try_scene("while true do end", "takes too long") == PLACE_BAD_DATA);
    assert(try_scene("local s = 'x' while true do s = s .. s end", "too much memory") == PLACE_OVER_BUDGET);

    /* Lengths in units round to the nearest thousandth; groups nest and offset. */
    b = experience(good_manifest,
                   "return {{pos = {1, 0.5, -2.25}, {pos = {0.001, 1.65, 0}, size = {0.5, 1, 1}},"
                   " {pos = {-1, 0, 0}, {pos = {0, 0, 0.0004}, size = {1, 1, 1}}}}}", &size);
    place q;
    assert(experience_load(&q, b, size) == PLACE_OK && q.part_count == 2);
    assert(q.parts[0].x == 1001 && q.parts[0].y == 2150 && q.parts[0].z == -2250 && q.parts[0].sx == 500);
    assert(q.parts[1].x == 0 && q.parts[1].y == 500 && q.parts[1].z == -2250);
    assert(q.parts[0].color[0] == 163); /* default grey */
    place_free(&q);
    free(b);

    /* Budgets: 120 boxes plus the floor is 1400 triangles, the limit; parts max out at 256. */
    char *scene = malloc(64 * 300);
    for (int boxes = 120; boxes <= 121; boxes++) {
        strcpy(scene, "return {");
        for (int i = 0; i < boxes; i++)
            strcat(scene, "{pos = {0, 0, 0}, size = {1, 1, 1}},");
        strcat(scene, "}");
        assert(try_scene(scene, boxes == 120 ? NULL : "triangle budget") == (boxes == 120 ? PLACE_OK : PLACE_OVER_BUDGET));
    }
    strcpy(scene, "return {");
    for (int i = 0; i < 257; i++)
        strcat(scene, "{pos = {0, 0, 0}, size = {1, 1, 1}},");
    strcat(scene, "}");
    assert(try_scene(scene, "more than 256 parts") == PLACE_OVER_BUDGET);
    free(scene);

    /* The manifest. */
    const char *box = "return {{pos = {0, 1, 0}, size = {1, 1, 1}}}";
    assert(try_logic("return {id = 'x', title = 'X', version = 1, api = 1, start = 'course', icon = 'crate', players = 4}",
                     box, NULL) == PLACE_OK);
    assert(try_logic("return {id = 'x', title = 'X', version = 1, api = 2, start = 'course'}", box, "newer") == PLACE_NEEDS_UPDATE);
    assert(try_logic("return {id = 'x', title = 'X', version = 1, api = 1}", box, "'start'") == PLACE_BAD_DATA);
    assert(try_logic("return {id = 'x', title = 'X', version = 1, api = 1, start = 'nope'}", box,
                     "scenes/nope.lua is missing") == PLACE_BAD_DATA);
    assert(try_logic("return {id = 'x', title = 'X', version = 1, api = 1, start = 'course', icon = 'nope'}", box,
                     "icon") == PLACE_BAD_DATA);
    assert(try_logic("return {id = 'x', title = 'X', version = 1, api = 1, start = 'course', players = 5}", box,
                     "'players'") == PLACE_BAD_DATA);
    assert(try_logic("return {id = 'x', title = 'X', version = 1, api = 1, start = 'course', author = 'me'}", box,
                     "unknown field 'author'") == PLACE_BAD_DATA);
    assert(try_logic("return 5", box, "should return a table") == PLACE_BAD_DATA);
}

static int imax(int a, int b) { return a > b ? a : b; }

static void count_rect(int x, int y, int w, int h, void *ctx)
{
    int *r = ctx; /* runs, lit pixels, then the first run's x, y, w */
    if (!r[0]++)
        r[2] = x, r[3] = y, r[4] = w;
    r[1] += w * h;
}

/* Text: runs of lit pixels, glyphs FONT_ADVANCE apart, non-ASCII as '?'. Chat: "who: msg",
 * printable only, wrapped to CHAT_COLS, the oldest lines scrolling off. */
static void test_text(void)
{
    int r[5] = {0};
    text_draw("-", 10, 20, count_rect, r);
    assert(r[0] == 1 && r[1] == 5 && r[2] == 10 && r[3] == 23 && r[4] == 5); /* one row, middle */
    int a[5] = {0}, b[5] = {0}, q[5] = {0};
    text_draw("A", 0, 0, count_rect, a);
    text_draw(" A", 0, 0, count_rect, b);
    text_draw("", 0, 0, count_rect, q);
    int qq[5] = {0};
    text_draw("?", 0, 0, count_rect, qq);
    assert(a[1] == b[1] && b[2] == a[2] + FONT_ADVANCE && q[1] == qq[1] && text_width("abc") == 3 * FONT_ADVANCE);
    int all[5] = {0};
    for (char c = ' '; c < '~'; c++) {
        char str[2] = {c, 0};
        text_draw(str, 0, 0, count_rect, all);
    }
    assert(all[1] > 95 * 5); /* every glyph drawn something (the space aside) */

    chat_log log = {0};
    chat_say(&log, "TestPlayer", "hiÃ©!", 7);
    assert(log.count == 1 && strcmp(log.lines[0], "TestPlayer: hi!") == 0 && log.said[0] == 7);
    char long_msg[100];
    memset(long_msg, 'x', sizeof long_msg - 1);
    long_msg[99] = 0;
    chat_say(&log, "TestPlayer", long_msg, 8); /* cut to CHAT_MAX, then wrapped */
    assert(log.count == 3 && strlen(log.lines[1]) == CHAT_COLS);
    assert(strlen(log.lines[2]) == strlen("TestPlayer: ") + CHAT_MAX - CHAT_COLS);
    chat_say(&log, "TestPlayer", "Hello, world! wasd moves nothing 123 and more", 9); /* breaks at a space */
    assert(strcmp(log.lines[log.count - 2], "TestPlayer: Hello, world! wasd moves") == 0);
    assert(strcmp(log.lines[log.count - 1], "nothing 123 and more") == 0);
    for (int i = 0; i < 10; i++)
        chat_say(&log, "TestPlayer", "again", 9 + i);
    assert(log.count == CHAT_LINES && strcmp(log.lines[CHAT_LINES - 1], "TestPlayer: again") == 0 && log.said[CHAT_LINES - 1] == 18);
}

/* At most 160 triangles (four fit the DS's budget beside a full scene), from the feet to
 * within the player's height; only the face is textured; each player's shirt wears their coat. */
static void test_avatar(const world *w)
{
    assert(avatar_texture.w == 16 && avatar_texture.h == 16);
    assert(texel(&avatar_texture, 0, 0) == 0 && texel(&avatar_texture, 3, 4) == 1); /* skin, an eye */
    int lo = 99999, hi = -99999, total = 0;
    for (int k = 0; k < AVATAR_PARTS; k++) {
        const mesh *m = &avatar_parts[0][k];
        assert(m->tris > 0 && (m->texture != NULL) == (k == AVATAR_FACE) && (m->uvs != NULL) == (k == AVATAR_FACE));
        total += m->tris;
        for (int i = 0; i < m->tris * 3; i++) {
            int y = avatar_joints[k][1] + m->verts[i][1] * 1000 / MESH_ONE;
            lo = y < lo ? y : lo;
            hi = y > hi ? y : hi;
            if (m->uvs)
                assert(m->uvs[i][0] >= 0 && m->uvs[i][0] <= MESH_ONE && m->uvs[i][1] >= 0 && m->uvs[i][1] <= MESH_ONE);
        }
    }
    assert(total <= 160 && abs(lo) <= 4 && hi > PLAYER_HEIGHT - 100 && hi <= PLAYER_HEIGHT);
    assert(avatar_parts[0][AVATAR_FACE].tris == 2); /* the front of the head */

    /* A forearm hangs off its upper arm: at rest it's at its own joint; with the upper arm
     * swung straight ahead, the elbow comes forward to shoulder height and the forearm turns
     * with it. */
    pose rest = {0}, ahead = {0};
    int at[3], angle;
    const int *elbow = avatar_joints[AVATAR_FOREARM_L], *shoulder = avatar_joints[AVATAR_ARM_L];
    avatar_place(&rest, AVATAR_FOREARM_L, at, &angle);
    assert(at[0] == elbow[0] && at[1] == elbow[1] && at[2] == elbow[2] && angle == 0);
    ahead.swing[AVATAR_ARM_L] = 900;
    ahead.swing[AVATAR_FOREARM_L] = 300;
    avatar_place(&ahead, AVATAR_FOREARM_L, at, &angle);
    assert(at[0] == elbow[0] && abs(at[1] - shoulder[1]) <= 3 && abs(at[2] + (shoulder[1] - elbow[1])) <= 3);
    assert(angle == 1200);
    avatar_place(&ahead, AVATAR_ARM_L, at, &angle);
    assert(at[1] == shoulder[1] && angle == 900); /* the upper arm hangs off the body */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const mesh *torso = &avatar_parts[i][AVATAR_TORSO];
        const uint8_t *shirt = torso->colors[torso->tris - 1]; /* the top of the body */
        for (int a = 0; a < 3; a++)
            assert(shirt[a] <= w->players[i].coat[a] && shirt[a] * 100 >= w->players[i].coat[a] * 54);
        assert(avatar_parts[i][AVATAR_HEAD].colors[0][0] == avatar_parts[0][AVATAR_HEAD].colors[0][0]); /* skin */
        assert(avatar_parts[i][AVATAR_LEG_L].verts == avatar_parts[0][AVATAR_LEG_L].verts);
    }
}

/* Idle, walk, run, jump and fall, and smooth changes between them. */
static void test_animation(world w)
{
    assert(run(w, 0, 0, 10).players[0].anim == ANIM_IDLE);

    /* Walking and running swing the legs opposite each other, running further, with a hop. */
    int walk_max = 0, run_max = 0, hop = 0;
    world a = w, b = w;
    for (int i = 0; i < 30; i++) {
        a = run(a, MOVE_RIGHT, 0, 1);
        b = run(b, MOVE_RIGHT | RUN, 0, 1);
        pose pa = player_pose(&a.players[0]), pb = player_pose(&b.players[0]);
        assert(pa.swing[AVATAR_LEG_L] == -pa.swing[AVATAR_LEG_R] && pb.swing[AVATAR_ARM_L] == -pb.swing[AVATAR_ARM_R]);
        walk_max = imax(walk_max, pa.swing[AVATAR_LEG_L]);
        run_max = imax(run_max, pb.swing[AVATAR_LEG_L]);
        hop = imax(hop, pb.lift);
    }
    assert(a.players[0].anim == ANIM_WALK && b.players[0].anim == ANIM_RUN);
    assert(walk_max > 250 && run_max > 450 && hop > 120);

    /* Jumping raises the arms and tucks a knee; past the top of the jump it's a fall. */
    world j = run(w, JUMP, 0, 1);
    assert(j.players[0].anim == ANIM_JUMP);
    j = run(j, 0, 0, 8);
    pose up = player_pose(&j.players[0]);
    assert(j.players[0].anim == ANIM_JUMP && up.swing[AVATAR_ARM_L] > 600 && up.swing[AVATAR_SHIN_L] < -500); /* arms up, knee tucked */
    j = run(j, 0, 0, 10);
    assert(j.players[0].anim == ANIM_FALL);
    assert(run(j, 0, 0, 40).players[0].anim == ANIM_IDLE); /* landed */

    /* Dropping from a height is a fall; stepping down off the curb (0.3) isn't. */
    world drop = w;
    drop.players[0].y = 3000;
    drop.players[0].on_ground = 0;
    assert(run(drop, 0, 0, 12).players[0].anim == ANIM_FALL);
    world curb = run(w, MOVE_BACK, 0, 32); /* on top of the curb */
    for (int i = 0; i < 20; i++) {
        curb = run(curb, MOVE_BACK, 0, 1);
        assert(curb.players[0].anim == ANIM_WALK);
    }

    /* Changes ease in: idle, walk, run, a running jump, landing, stopping. No part jumps
     * more than 40 degrees in a tick, and the lift never jumps more than 0.06. */
    static const struct { unsigned buttons; int ticks; } script[] = {
        {0, 10}, {MOVE_FWD, 20}, {MOVE_FWD | RUN, 15}, {MOVE_FWD | RUN | JUMP, 1}, {MOVE_FWD | RUN, 25}, {0, 20},
    };
    world s = w;
    pose last = player_pose(&s.players[0]);
    int anims = 0, prev = s.players[0].anim;
    for (unsigned k = 0; k < sizeof script / sizeof script[0]; k++) {
        for (int t = 0; t < script[k].ticks; t++) {
            s = run(s, script[k].buttons, 0, 1);
            pose now = player_pose(&s.players[0]);
            for (int part = 0; part < AVATAR_PARTS; part++)
                assert(abs(now.swing[part] - last.swing[part]) <= 400);
            assert(abs(now.lift - last.lift) <= 60);
            anims += s.players[0].anim != prev;
            prev = s.players[0].anim;
            last = now;
        }
    }
    assert(anims >= 5); /* went through walk, run, jump, fall, back to running and idle */
}

int main(void)
{
    test_experience_loader();
    assert(isin(0) == 0 && isin(90) == 4096 && isin(270) == -4096 && icos(0) == 4096);
    assert(abs(isin(30) - 2048) < 20 && abs(isin(-30) + 2048) < 20);

    world w = world_init(&pl, 1);
    const player *p0 = &w.players[0];
    assert(w.player_count == 1 && p0->x == 0 && p0->z == 0);

    /* Movement is relative to the viewer's camera yaw: at 0, forward is -Z, right is +X. */
    world f = run(w, MOVE_FWD, 0, 10);
    assert(f.players[0].z == -1200 && f.players[0].x == 0);
    world r = run(w, MOVE_RIGHT, 0, 10);
    assert(r.players[0].x == 1200 && r.players[0].z == 0);
    world side = run(w, MOVE_FWD, 900, 10); /* camera orbited to the +X side */
    assert(side.players[0].x == -1200 && abs(side.players[0].z) <= 10);

    /* Diagonals move at the same speed (within rounding), not sqrt(2) faster. */
    world d = run(w, MOVE_FWD | MOVE_RIGHT, 0, 10);
    int dist2 = d.players[0].x * d.players[0].x + d.players[0].z * d.players[0].z;
    assert(d.players[0].x > 0 && d.players[0].z < 0 && dist2 > 1160 * 1160 && dist2 <= 1200 * 1200);

    /* Jump: up about 1.2 units, back down, no jumping again mid-air. */
    world j = run(w, JUMP, 0, 1);
    assert(j.players[0].y > 0 && !j.players[0].on_ground);
    int peak = 0;
    for (int i = 0; i < 100; i++) {
        j = run(j, 0, 0, 1);
        peak = j.players[0].y > peak ? j.players[0].y : peak;
    }
    assert(peak > 1100 && peak < 1300 && j.players[0].y == 0 && j.players[0].on_ground);
    world air = run(w, JUMP, 0, 10);
    assert(run(air, JUMP, 0, 1).players[0].vy < air.players[0].vy);

    /* Collision: the wall (x -4.5..-3.5) stops you flush against it; step 1 (z -5..-3,
     * 0.6 high) is too tall to walk up, so you stop flush; jumping gets you on top; you
     * land on tops. */
    world wall = run(w, MOVE_LEFT, 0, 200);
    assert(wall.players[0].x == -3500 + PLAYER_RADIUS);
    world step = run(w, MOVE_FWD, 0, 200);
    assert(step.players[0].z == -3000 + PLAYER_RADIUS && step.players[0].y == 0);

    /* The curb (z 3.5..4.5, 0.3 high) is low enough to walk up and off again. */
    world curb = run(w, MOVE_BACK, 0, 32); /* z = 3.84: on top of it */
    assert(curb.players[0].y == 300 && curb.players[0].on_ground);
    curb = run(curb, MOVE_BACK, 0, 20);   /* z = 6.24: walked off the far side */
    assert(curb.players[0].y == 0 && curb.players[0].z == 6240);
    world high = w; /* only walks up when grounded */
    high.players[0].z = 3100; /* front edge 3.45: one step reaches into the curb */
    high.players[0].y = 150;
    high.players[0].on_ground = 0;
    high.players[0].vy = 50;
    high = run(high, MOVE_BACK, 0, 1);
    assert(high.players[0].z == 3500 - PLAYER_RADIUS);
    world up = run(step, MOVE_FWD | JUMP, 0, 13);
    up = run(up, 0, 0, 30);
    assert(up.players[0].on_ground && up.players[0].y == 600);
    world drop = w;
    drop.players[0].z = -4000;
    drop.players[0].y = 3000;
    drop = run(drop, 0, 0, 100);
    assert(drop.players[0].y == 600 && drop.players[0].on_ground);
    for (int i = 0; i < 4; i++) { /* standing on a part: grounded every tick, not every other */
        drop = run(drop, 0, 0, 1);
        assert(drop.players[0].on_ground && drop.players[0].y == 600);
    }

    /* Players move independently from their own inputs. */
    world two = world_init(&pl, 2);
    input in[MAX_PLAYERS] = {{MOVE_RIGHT, 0, 0, 0}, {MOVE_FWD, 0, 0, 0}};
    for (int i = 0; i < 10; i++)
        two = world_update(two, in);
    assert(two.players[0].x == 1200 && two.players[0].z == 0);
    assert(two.players[1].x == 1500 && two.players[1].z == -1200);

    /* Players turn (the short way) to face where they move, and keep facing it. */
    assert(run(w, MOVE_RIGHT, 0, 100).players[0].heading == 2700);
    assert(run(w, MOVE_BACK, 0, 100).players[0].heading == 1800);
    assert(run(w, MOVE_LEFT | MOVE_FWD, 0, 100).players[0].heading == 450);
    assert(run(r, 0, 0, 100).players[0].heading == 2700);
    world turn = w;
    turn.players[0].heading = 100;
    turn.players[0].target_heading = 3500;
    assert(run(turn, 0, 0, 1).players[0].heading < 100);
    assert(run(turn, 0, 0, 100).players[0].heading == 3500);

    /* The camera follows with a lag, then settles exactly; orbit/zoom ease and clamp. */
    camera c = follow(camera_init(), 0, &f, 1);
    assert(c.now.z > f.players[0].z && c.now.z < 0);
    c = follow(c, 0, &f, 200);
    assert(c.now.z == f.players[0].z && c.now.x == f.players[0].x);
    camera o = follow(camera_init(), CAM_RIGHT, &w, 1);
    assert(o.target.yaw == 70 && o.now.yaw > 0 && o.now.yaw < 70);
    o = follow(o, CAM_UP | CAM_IN, &w, 300);
    assert(o.now.pitch == CAM_PITCH_MAX && o.now.dist == CAM_DIST_MIN);

    /* Running is faster than walking. */
    world walked = run(w, MOVE_RIGHT, 0, 30), ran = run(w, MOVE_RIGHT | RUN, 0, 30);
    assert(ran.players[0].x - p0->x > (walked.players[0].x - p0->x) * 3 / 2);
    test_animation(w);

    /* The ticker runs exactly TICK_HZ ticks a second whatever the polling rate, and drops
     * time after a stall instead of fast-forwarding through it. */
    ticker tk = {0};
    int ticks = 0;
    for (uint64_t us = 5000000; us < 7000000; us += 16667) /* 2 s polled at 60 Hz */
        ticks += ticker_due(&tk, us);
    assert(ticks >= 2 * TICK_HZ && ticks <= 2 * TICK_HZ + 1);
    ticks = 0;
    for (uint64_t us = 7000000; us < 9000000; us += 6944) /* 2 s polled at 144 Hz */
        ticks += ticker_due(&tk, us);
    assert(ticks >= 2 * TICK_HZ - 1 && ticks <= 2 * TICK_HZ + 1);
    assert(ticker_due(&tk, 60000000) == 4 && ticker_due(&tk, 60000000) == 0); /* a 51 s stall */

    /* Walking one way: nothing for a moment, then the camera slowly swings behind,
     * until it's there. Walking toward it never turns it. */
    world walk = w;
    camera s = camera_init();
    for (int i = 0; i < 300; i++) {
        input in[MAX_PLAYERS] = {{MOVE_RIGHT, s.now.yaw, 0, 0}};
        walk = world_update(walk, in);
        s = camera_update(s, (input){0, 0, 0, 0}, &walk);
        if (i == 20)
            assert(s.target.yaw == 0);
        if (i == 30)
            assert(s.target.yaw < 0 && s.target.yaw > -150);
    }
    assert(s.target.yaw < -450); /* strafing right circles, with the camera behind */
    world toward = run(w, MOVE_BACK, 0, 1);
    camera t = camera_init();
    for (int i = 0; i < 200; i++) {
        toward = run(toward, MOVE_BACK, 0, 1);
        t = camera_update(t, (input){0, 0, 0, 0}, &toward);
    }
    assert(t.target.yaw == 0);

    /* Standing on a part, the camera settles at its height; a jump from there only
     * raises the camera by half the jump. */
    camera onstep = follow(camera_init(), 0, &drop, 300);
    assert(onstep.now.y == 600 + CAM_FOCUS);
    world hop = run(drop, JUMP, 0, 10);
    onstep = camera_update(onstep, (input){0, 0, 0, 0}, &hop);
    assert(onstep.target.y == 600 + (hop.players[0].y - 600) / 2 + CAM_FOCUS);

    /* Camera modes. A press of CAM_MODE (once per press, not while held) switches to first
     * person: the distance eases to 0, the view sits at the eyes, and the player is hidden
     * only once the camera is close. Pressing again eases back out. */
    camera cm = follow(camera_init(), 0, &w, 50);
    assert(cm.mode == CAM_THIRD && !camera_hides(&cm, 0));
    cm = camera_update(cm, (input){CAM_MODE, 0, 0, 0}, &w);
    assert(cm.mode == CAM_FIRST && !camera_hides(&cm, 0)); /* still easing in */
    cm = follow(cm, CAM_MODE, &w, 10);
    assert(cm.mode == CAM_FIRST);
    cm = follow(cm, 0, &w, 60);
    assert(cm.now.dist == 0 && cm.now.y == w.players[0].y + CAM_EYE && camera_hides(&cm, 0) && !camera_hides(&cm, 1));
    cm = follow(cm, CAM_UP, &w, 100); /* up looks up; further up and down than third person */
    assert(cm.target.pitch == -CAM_LOOK_MAX);
    cm = follow(cm, CAM_DOWN, &w, 100);
    assert(cm.target.pitch == CAM_LOOK_MAX);
    camera turned = follow(follow(cm, CAM_RIGHT, &w, 13), 0, &w, 40); /* right turns the view right: about 90 degrees */
    world went = run(w, MOVE_FWD, turned.now.yaw, 10);
    assert(turned.target.yaw < cm.target.yaw && went.players[0].x > 1000 && abs(went.players[0].z) < 100);
    world ahead = run(w, MOVE_FWD, 0, 5);
    cm = camera_update(cm, (input){0, 0, 0, 0}, &ahead);
    assert(cm.now.z == ahead.players[0].z); /* the eyes don't lag */
    cm = camera_update(cm, (input){CAM_MODE, 0, 0, 0}, &ahead); /* on to the course's fixed camera */
    cm = camera_update(cm, (input){0, 0, 0, 0}, &ahead);
    cm = camera_update(cm, (input){CAM_MODE, 0, 0, 0}, &ahead);
    assert(cm.mode == CAM_THIRD);
    cm = follow(cm, 0, &ahead, 100);
    assert(cm.now.dist == CAM_DIST && !camera_hides(&cm, 0));

    /* Analog look: a stick or mouse pushed part way turns the camera part way; all the way, or
     * further, is full speed, the same as an orbit button, and never more, even on a diagonal
     * or with a button held too. */
    int lx, ly;
    look_from_motion(MOUSE_FULL / 2, 0, &lx, &ly);
    assert(lx == LOOK_FULL / 2 && ly == 0);
    camera half = camera_update(camera_init(), (input){0, 0, lx, ly}, &w);
    camera full = camera_update(camera_init(), (input){CAM_RIGHT, 0, 0, 0}, &w);
    look_from_motion(MOUSE_FULL * 50, 0, &lx, &ly);
    camera fling = camera_update(camera_init(), (input){0, 0, lx, ly}, &w);
    camera both = camera_update(camera_init(), (input){CAM_RIGHT, 0, LOOK_FULL, 0}, &w);
    assert(half.target.yaw == full.target.yaw / 2 && fling.target.yaw == full.target.yaw && both.target.yaw == full.target.yaw);
    look_from_motion(MOUSE_FULL * 10, MOUSE_FULL * 10, &lx, &ly);
    camera diag = camera_update(camera_init(), (input){0, 0, lx, ly}, &w);
    int dyaw = diag.target.yaw, dpitch = camera_init().target.pitch - diag.target.pitch;
    assert(dyaw > 0 && dpitch > 0 && dyaw * dyaw + dpitch * dpitch <= full.target.yaw * full.target.yaw + 2);

    /* Integer angles: straight up the x axis is 0, the y axis 90 degrees, behind 180. */
    assert(iatan2(0, 1000) == 0 && iatan2(1000, 0) == 900 && iatan2(0, -1000) == 1800 && iatan2(-1000, 0) == -900);
    assert(abs(iatan2(1000, 1000) - 450) <= 1 && abs(iatan2(500, 1000) - 266) <= 2 && abs(iatan2(-577, -1000) + 1500) <= 2);
    assert(isqrt(0) == 0 && isqrt(16) == 4 && isqrt(17) == 4 && isqrt(10000000000LL) == 100000);

    /* The fixed camera: CAM_MODE goes third, first, fixed (the course has one), third. It
     * stands at the scene's camera spot and turns to follow the player as they move. */
    camera fx = camera_init();
    fx = camera_update(fx, (input){CAM_MODE, 0, 0, 0}, &w);
    fx = camera_update(fx, (input){0, 0, 0, 0}, &w);
    fx = camera_update(fx, (input){CAM_MODE, 0, 0, 0}, &w);
    assert(fx.mode == CAM_FIXED);
    world roam = w;
    for (int i = 0; i < 60; i++) {
        roam = run(roam, i < 30 ? MOVE_LEFT : MOVE_FWD, fx.now.yaw, 1);
        fx = camera_update(fx, (input){0, 0, 0, 0}, &roam);
        /* Where the view puts the camera: the look point, back along its angles (rounded to
         * whole degrees here, so within half a degree at this distance). */
        int yaw = (fx.now.yaw + 5) / 10, pitch = (fx.now.pitch + 5) / 10;
        int across = fx.now.dist * icos(pitch) / 4096;
        int cx = fx.now.x + across * isin(yaw) / 4096;
        int cy = fx.now.y + fx.now.dist * isin(pitch) / 4096;
        int cz = fx.now.z + across * icos(yaw) / 4096;
        assert(abs(cx - 9000) < 200 && abs(cy - 5000) < 200 && abs(cz - 9000) < 200);
    }
    assert(fx.now.x == roam.players[0].x || abs(fx.now.x - roam.players[0].x) < 400); /* looking at them */
    fx = camera_update(fx, (input){CAM_MODE, 0, 0, 0}, &roam);
    assert(fx.mode == CAM_THIRD);

    /* Detached on a fixed point, or attached to a part (riding on its top), in either mode. */
    camera cd = camera_init();
    cd.subject = (subject){SUBJECT_POINT, 0, 5000, 2000, -3000};
    cd = follow(cd, 0, &w, 200);
    assert(cd.now.x == 5000 && cd.now.z == -3000 && cd.now.y == 2000 + CAM_FOCUS);
    cd.mode = CAM_FIRST;
    cd = follow(cd, 0, &w, 100);
    assert(cd.now.y == 2000 + CAM_EYE && cd.now.dist == 0 && !camera_hides(&cd, 0));
    camera ca = camera_init();
    ca.subject = (subject){SUBJECT_PART, 4, 0, 0, 0}; /* the ledge */
    ca = follow(ca, 0, &w, 200);
    assert(ca.now.x == pl.parts[4].x && ca.now.y == pl.parts[4].y + pl.parts[4].sy / 2 + CAM_FOCUS);

    test_avatar(&w);
    test_text();

    /* Level mesh: checkered floor at FLOOR_Y, then 10 triangles per box part (not the
     * 4 mesh parts), its top face first, drawn at the part's top. */
    int floor_tris = FLOOR_TILES * FLOOR_TILES * 2;
    assert(level_mesh.tris == floor_tris + 10 * (pl.part_count - 4));
    for (int i = 0; i < floor_tris * 3; i++)
        assert(level_mesh.verts[i][1] == FLOOR_Y * MESH_ONE);
    assert(level_mesh.colors[0][0] != level_mesh.colors[2][0]);
    const part *s1 = &pl.parts[1];
    assert(level_mesh.verts[(floor_tris + 10) * 3][1] == (FLOOR_Y * 1000 + s1->y + s1->sy / 2) * MESH_ONE / 1000);

    /* Mesh parts collide as their box: the statue (x -7.8..-6.2, z -7.4..-6.6). */
    world near = w;
    near.players[0].x = -7000;
    near.players[0].z = -5000;
    near = run(near, MOVE_FWD, 0, 100);
    assert(near.players[0].z == -6600 + PLAYER_RADIUS);

    place_free(&pl);
    puts("core: ok");
    return EXIT_SUCCESS;
}
