#include <libdragon.h>
#include <GL/gl.h>
#include <GL/gl_integration.h>
#include "world.h"

#define STICK_DEADZONE 32

static place pl; /* the loaded place; mesh parts are drawn from it */
static GLuint gpu_textures[1 + PLACE_MAX_TEXTURES]; /* by texture slot */

/* Expand each 16-colour place texture to RGBA16 (32x32 = 2 KB, fits TMEM) and upload it,
 * nearest-filtered. */
static void upload_textures(void)
{
    texture *all[1 + PLACE_MAX_TEXTURES]; /* the avatar's, then the place's */
    int count = 0;
    all[count++] = &avatar_texture;
    for (int i = 0; i < pl.texture_count; i++)
        all[count++] = &pl.textures[i];
    glGenTextures(count, gpu_textures);
    for (int i = 0; i < count; i++) {
        texture *t = all[i];
        t->slot = i;
        surface_t *s = malloc(sizeof *s);
        *s = surface_alloc(FMT_RGBA16, t->w, t->h); /* uncached, so the RDP sees our writes */
        uint16_t *px = s->buffer;
        for (int y = 0; y < t->h; y++) {
            for (int x = 0; x < t->w; x++) {
                const uint8_t *c = t->palette[texel(t, x, y)];
                px[y * (s->stride / 2) + x] = (c[0] >> 3) << 11 | (c[1] >> 3) << 6 | (c[2] >> 3) << 1 | 1;
            }
        }
        glBindTexture(GL_TEXTURE_2D, gpu_textures[i]);
        glSurfaceTexImageN64(GL_TEXTURE_2D, 0, s, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

/* Textured meshes: the triangle colour tints the texture (GL_MODULATE, the default). */
static void bind_texture(const mesh *m)
{
    if (m->texture) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gpu_textures[m->texture->slot]);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
}

/* Stick moves, A jumps, B runs, D-pad orbits, C-up/C-down zoom in/out, R switches the camera.
 * Z opens the chat. */
static unsigned read_input(void)
{
    joypad_poll();
    joypad_inputs_t in = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t b = joypad_get_buttons_held(JOYPAD_PORT_1);
    unsigned bits = 0;
    if (in.stick_x < -STICK_DEADZONE) bits |= MOVE_LEFT;
    if (in.stick_x > STICK_DEADZONE)  bits |= MOVE_RIGHT;
    if (in.stick_y > STICK_DEADZONE)  bits |= MOVE_FWD;
    if (in.stick_y < -STICK_DEADZONE) bits |= MOVE_BACK;
    if (b.d_left)  bits |= CAM_LEFT;
    if (b.d_right) bits |= CAM_RIGHT;
    if (b.d_up)    bits |= CAM_UP;
    if (b.d_down)  bits |= CAM_DOWN;
    if (b.c_up)    bits |= CAM_IN;
    if (b.c_down)  bits |= CAM_OUT;
    if (b.r)       bits |= CAM_MODE;
    if (b.a)       bits |= JUMP;
    if (b.b)       bits |= RUN;
    return bits;
}

/* The on-screen keyboard: D-pad moves, A types, B deletes (cancels when empty), Start sends. */
static unsigned read_keyboard(void)
{
    joypad_buttons_t b = joypad_get_buttons_held(JOYPAD_PORT_1);
    return (b.d_up ? KB_UP : 0) | (b.d_down ? KB_DOWN : 0) | (b.d_left ? KB_LEFT : 0) | (b.d_right ? KB_RIGHT : 0) |
           (b.a ? KB_TYPE : 0) | (b.b ? KB_BACK : 0) | (b.start ? KB_DONE : 0);
}

/* Chat rectangles, filled by the RDP straight into the frame. */
static void chat_rect(int x, int y, int w, int h, const uint8_t rgb[3], void *ctx)
{
    (void)ctx;
    rdpq_set_fill_color(RGBA32(rgb[0], rgb[1], rgb[2], 255));
    rdpq_fill_rectangle(x, y, x + w, y + h);
}

static void draw_mesh(const mesh *m)
{
    const float k = 1.0f / MESH_ONE;
    glBegin(GL_TRIANGLES);
    for (int t = 0; t < m->tris; t++) {
        glColor3ub(m->colors[t][0], m->colors[t][1], m->colors[t][2]);
        for (int c = 0; c < 3; c++) {
            const int16_t *v = m->verts[t * 3 + c];
            if (m->uvs)
                glTexCoord2f(m->uvs[t * 3 + c][0] * k, m->uvs[t * 3 + c][1] * k);
            glVertex3f(v[0] * k, v[1] * k, v[2] * k);
        }
    }
    glEnd();
}

/* Record a mesh once as a display list (an RSP command block) so drawing it each frame
 * costs the CPU one call instead of a GL call per vertex. */
static GLuint compile_mesh(const mesh *m)
{
    bind_texture(m); /* before recording, so the texture coordinates are recorded too */
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    draw_mesh(m);
    glEndList();
    glDisable(GL_TEXTURE_2D);
    return list;
}

int main(void)
{
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    gl_init();
    joypad_init();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1333f, 0.1333f, -0.1f, 0.1f, 0.2f, 100.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE); /* triangles seen from behind (meshes are counter-clockwise from outside) are never drawn */
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    assertf(experience_load(&pl, builtin_experience, builtin_experience_size) == PLACE_OK, "built-in experience: %s", experience_error);
    world w = world_init(&pl, 1);
    upload_textures();
    camera cam = camera_init();
    ticker tk = {0};
    chat_log chat = {0};
    keyboard kb;
    int typing = 0, ticks = 0; /* Z opens the chat keyboard */
    GLuint level_list = compile_mesh(&level_mesh), avatar_lists[MAX_PLAYERS][AVATAR_PARTS];
    for (int i = 0; i < MAX_PLAYERS; i++)
        for (int k = 0; k < AVATAR_PARTS; k++)
            avatar_lists[i][k] = compile_mesh(&avatar_parts[i][k]);
    GLuint mesh_lists[PLACE_MAX_MESHES];
    for (int i = 0; i < pl.mesh_count; i++)
        mesh_lists[i] = compile_mesh(&pl.meshes[i]);
    for (;;) {
        rdpq_attach(display_get(), display_get_zbuf());
        gl_context_begin();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const view *v = &cam.now;
        glTranslatef(0.0f, 0.0f, -v->dist / 1000.0f);
        glRotatef(v->pitch / 10.0f, 1.0f, 0.0f, 0.0f);
        glRotatef(-v->yaw / 10.0f, 0.0f, 1.0f, 0.0f);
        glTranslatef(-v->x / 1000.0f, -v->y / 1000.0f, -v->z / 1000.0f);
        glCallList(level_list);

        /* Mesh parts: the model's origin sits at the part's bottom centre. */
        for (int i = 0; i < pl.part_count; i++) {
            const part *q = &pl.parts[i];
            if (q->mesh < 0 || !part_visible(&cam.now, SCREEN_W, SCREEN_H, q, &pl.meshes[q->mesh]))
                continue; /* a box (in the level mesh), or off screen */
            glPushMatrix();
            glTranslatef(q->x / 1000.0f, FLOOR_Y + (q->y - q->sy / 2) / 1000.0f, q->z / 1000.0f);
            bind_texture(&pl.meshes[q->mesh]);
            glCallList(mesh_lists[q->mesh]);
            glDisable(GL_TEXTURE_2D);
            glPopMatrix();
        }

        for (int i = 0; i < w.player_count; i++) {
            if (camera_hides(&cam, i) || !player_visible(&cam.now, SCREEN_W, SCREEN_H, &w.players[i]))
                continue;
            const player *p = &w.players[i];
            glPushMatrix();
            pose pz = player_pose(p);
            glTranslatef(p->x / 1000.0f, FLOOR_Y + (p->y + pz.lift) / 1000.0f, p->z / 1000.0f);
            glRotatef(p->heading / 10.0f, 0.0f, 1.0f, 0.0f);
            for (int k = 0; k < AVATAR_PARTS; k++) { /* each part turns about its joint */
                int j[3], turn;
                avatar_place(&pz, k, j, &turn);
                bind_texture(&avatar_parts[i][k]); /* only the face is textured */
                glPushMatrix();
                glTranslatef(j[0] / 1000.0f, j[1] / 1000.0f, j[2] / 1000.0f);
                glRotatef(turn / 10.0f, 1.0f, 0.0f, 0.0f);
                glCallList(avatar_lists[i][k]);
                glPopMatrix();
            }
            glDisable(GL_TEXTURE_2D);
            glPopMatrix();
        }

        gl_context_end();
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
        chat_draw(&chat, typing ? kb.text : NULL, typing ? &kb : NULL, ticks, SCREEN_W, SCREEN_H, chat_rect, NULL);
        rdpq_detach_show();
        unsigned b = read_input();
        if (typing) {
            int r = keyboard_update(&kb, read_keyboard());
            if (r == KB_SEND && kb.text[0])
                chat_say(&chat, "TestPlayer", kb.text, ticks);
            typing = r == KB_TYPING;
            b = 0; /* typing doesn't move you */
        } else if (joypad_get_buttons_pressed(JOYPAD_PORT_1).z) {
            typing = 1;
            kb = keyboard_open();
        }
        int n = ticker_due(&tk, get_ticks_us());
        ticks += n;
        for (; n > 0; n--) {
            input in[MAX_PLAYERS] = {{b, cam.now.yaw, 0, 0}};
            w = world_update(w, in);
            cam = camera_update(cam, in[0], &w);
        }
    }
}
