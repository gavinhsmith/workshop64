/* Browser port: SDL3 for the window and keys, WebGL (GLES 2) to draw. The scene is drawn
 * SCREEN_H high, as wide as the page's shape (see screen_width), into an offscreen buffer,
 * then scaled up nearest-neighbour to fill the page. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <GLES2/gl2.h>
#include <emscripten.h>
#include <string.h>
#include "world.h"

static place pl;
static GLuint gpu_textures[1 + PLACE_MAX_TEXTURES]; /* by texture slot */
static GLint u_mvp, u_textured;

/* A mesh in a vertex buffer: one vertex per corner, the triangle's colour copied to each. */
typedef struct {
    int16_t x, y, z, u, v;
    uint8_t r, g, b, pad;
} vertex;

typedef struct {
    GLuint vbo;
    int count;
    const texture *tex;
} gpu_mesh;

static gpu_mesh gpu_level, gpu_place[PLACE_MAX_MESHES], gpu_avatars[MAX_PLAYERS][AVATAR_PARTS];
static GLuint frame_fbo, frame_tex, frame_depth, quad_vbo;
static int frame_w; /* the frame's current width */

static gpu_mesh upload_mesh(const mesh *m)
{
    static vertex buf[PLACE_MAX_TRIS * 3]; /* the level, a place model or an avatar */
    gpu_mesh g = {0, m->tris * 3, m->texture};
    SDL_assert(g.count <= (int)SDL_arraysize(buf));
    for (int i = 0; i < g.count; i++) {
        const int16_t *p = m->verts[i], *uv = m->uvs ? m->uvs[i] : (const int16_t[2]){0, 0};
        const uint8_t *c = m->colors[i / 3];
        buf[i] = (vertex){p[0], p[1], p[2], uv[0], uv[1], c[0], c[1], c[2], 0};
    }
    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, g.count * sizeof *buf, buf, GL_STATIC_DRAW);
    return g;
}

/* Expand each 16-colour texture to RGB and upload it, nearest-filtered. */
static void upload_textures(void)
{
    texture *all[1 + PLACE_MAX_TEXTURES]; /* the avatar's, then the place's */
    int count = 0;
    all[count++] = &avatar_texture;
    for (int i = 0; i < pl.texture_count; i++)
        all[count++] = &pl.textures[i];
    static uint8_t rgb[TEXTURE_MAX * TEXTURE_MAX][3];
    glGenTextures(count, gpu_textures);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < count; i++) {
        texture *t = all[i];
        t->slot = i;
        for (int y = 0; y < t->h; y++)
            for (int x = 0; x < t->w; x++)
                memcpy(rgb[y * t->w + x], t->palette[texel(t, x, y)], 3);
        glBindTexture(GL_TEXTURE_2D, gpu_textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, t->w, t->h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

/* The triangle colour tints the texture, like fixed-function GL_MODULATE on the PC port. */
static const char *vs_src =
    "attribute vec3 a_pos; attribute vec2 a_uv; attribute vec3 a_col;\n"
    "uniform mat4 u_mvp; varying vec2 v_uv; varying vec3 v_col;\n"
    "void main() { gl_Position = u_mvp * vec4(a_pos / 256.0, 1.0); v_uv = a_uv / 256.0; v_col = a_col; }\n";
static const char *fs_src =
    "precision mediump float;\n"
    "uniform sampler2D u_tex; uniform float u_textured; varying vec2 v_uv; varying vec3 v_col;\n"
    "void main() { gl_FragColor = vec4(v_col, 1.0) * mix(vec4(1.0), texture2D(u_tex, v_uv), u_textured); }\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        SDL_Log("shader: %s", log);
    }
    return s;
}

static void setup_shader(void)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs_src));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs_src));
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glBindAttribLocation(p, 2, "a_col");
    glLinkProgram(p);
    glUseProgram(p);
    u_mvp = glGetUniformLocation(p, "u_mvp");
    u_textured = glGetUniformLocation(p, "u_textured");
    for (int i = 0; i < 3; i++)
        glEnableVertexAttribArray(i);
}

/* 4x4 matrices, column-major as GL expects; each op multiplies on the right, like glRotatef. */
static void mat_mul(float *m, const float *op)
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = m[row] * op[c * 4] + m[4 + row] * op[c * 4 + 1] +
                             m[8 + row] * op[c * 4 + 2] + m[12 + row] * op[c * 4 + 3];
    memcpy(m, r, sizeof r);
}

static void translate(float *m, float x, float y, float z)
{
    mat_mul(m, (const float[16]){1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1});
}

static void rotate_x(float *m, float tenths)
{
    float a = tenths * (SDL_PI_F / 1800), c = SDL_cosf(a), s = SDL_sinf(a);
    mat_mul(m, (const float[16]){1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1});
}

static void rotate_y(float *m, float tenths)
{
    float a = tenths * (SDL_PI_F / 1800), c = SDL_cosf(a), s = SDL_sinf(a);
    mat_mul(m, (const float[16]){c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1});
}

static void draw_mesh(const gpu_mesh *g, const float *mvp)
{
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    glUniform1f(u_textured, g->tex ? 1.0f : 0.0f);
    if (g->tex)
        glBindTexture(GL_TEXTURE_2D, gpu_textures[g->tex->slot]);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, x));
    glVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, u));
    glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vertex), (void *)offsetof(vertex, r));
    glDrawArrays(GL_TRIANGLES, 0, g->count);
}

/* Same transforms as the PC port's draw_scene (see view in world.h). */
static void draw_scene(const world *w, const camera *c)
{
    const view *v = &c->now;
    glViewport(0, 0, frame_w, SCREEN_H);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* glFrustum(-r, r, -t, t, n, f): same vertical view everywhere; wider sees more. */
    const float n = 0.2f, f = 100.0f, t = 0.1f, r = t * frame_w / SCREEN_H;
    float vp[16] = {n / r, 0, 0, 0, 0, n / t, 0, 0, 0, 0, -(f + n) / (f - n), -1, 0, 0, -2 * f * n / (f - n), 0};
    translate(vp, 0, 0, -v->dist / 1000.0f);
    rotate_x(vp, v->pitch);
    rotate_y(vp, -v->yaw);
    translate(vp, -v->x / 1000.0f, -v->y / 1000.0f, -v->z / 1000.0f);
    draw_mesh(&gpu_level, vp);

    for (int i = 0; i < pl.part_count; i++) {
        const part *q = &pl.parts[i];
        if (q->mesh < 0)
            continue;
        float m[16];
        memcpy(m, vp, sizeof m);
        translate(m, q->x / 1000.0f, FLOOR_Y + (q->y - q->sy / 2) / 1000.0f, q->z / 1000.0f);
        draw_mesh(&gpu_place[q->mesh], m);
    }

    for (int i = 0; i < w->player_count; i++) {
        if (camera_hides(c, i))
            continue;
        const player *p = &w->players[i];
        pose pz = player_pose(p);
        float m[16];
        memcpy(m, vp, sizeof m);
        translate(m, p->x / 1000.0f, FLOOR_Y + (p->y + pz.lift) / 1000.0f, p->z / 1000.0f);
        rotate_y(m, p->heading);
        for (int k = 0; k < AVATAR_PARTS; k++) { /* each part turns about its joint */
            int j[3], turn;
            avatar_place(&pz, k, j, &turn);
            float pm[16];
            memcpy(pm, m, sizeof pm);
            translate(pm, j[0] / 1000.0f, j[1] / 1000.0f, j[2] / 1000.0f);
            rotate_x(pm, turn);
            draw_mesh(&gpu_avatars[i][k], pm);
        }
    }
}

/* Give the frame this width (it's SCREEN_H high); called when the page changes shape. */
static void size_frame(int w)
{
    if (w == frame_w)
        return;
    frame_w = w;
    glBindTexture(GL_TEXTURE_2D, frame_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, SCREEN_H, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, frame_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, SCREEN_H);
}

/* The frame the scene is drawn into, and a quad covering the screen to show it. */
static void setup_frame(void)
{
    glGenTextures(1, &frame_tex);
    glBindTexture(GL_TEXTURE_2D, frame_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); /* needed for odd sizes in WebGL 1 */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &frame_depth);
    size_frame(SCREEN_W);
    glGenFramebuffers(1, &frame_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, frame_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, frame_depth);

    static const vertex quad[6] = {
        {-256, -256, 0, 0, 0, 255, 255, 255}, {256, -256, 0, 256, 0, 255, 255, 255}, {256, 256, 0, 256, 256, 255, 255, 255},
        {-256, -256, 0, 0, 0, 255, 255, 255}, {256, 256, 0, 256, 256, 255, 255, 255}, {-256, 256, 0, 0, 256, 255, 255, 255},
    };
    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
}

/* Draw the frame as big as fits, centred. It matches the page's shape, so there are bars only
 * past SCREEN_W_MAX wide or under 4:3. */
static void present(SDL_Window *win)
{
    int w, h;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    float scale = SDL_min((float)w / frame_w, (float)h / SCREEN_H);
    int vw = (int)(frame_w * scale), vh = (int)(SCREEN_H * scale);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
    glDisable(GL_DEPTH_TEST);
    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, identity);
    glUniform1f(u_textured, 1.0f);
    glBindTexture(GL_TEXTURE_2D, frame_tex);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, x));
    glVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, u));
    glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vertex), (void *)offsetof(vertex, r));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0); /* drawing into the frame while it's bound is an error */
    glBindFramebuffer(GL_FRAMEBUFFER, frame_fbo);
}

/* WASD moves, Space jumps, Shift runs, arrows orbit, Q switches the camera, Z/X zoom in/out. */
static unsigned read_input(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    unsigned in = 0;
    if (k[SDL_SCANCODE_A])     in |= MOVE_LEFT;
    if (k[SDL_SCANCODE_D])     in |= MOVE_RIGHT;
    if (k[SDL_SCANCODE_W])     in |= MOVE_FWD;
    if (k[SDL_SCANCODE_S])     in |= MOVE_BACK;
    if (k[SDL_SCANCODE_LEFT])  in |= CAM_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) in |= CAM_RIGHT;
    if (k[SDL_SCANCODE_UP])    in |= CAM_UP;
    if (k[SDL_SCANCODE_DOWN])  in |= CAM_DOWN;
    if (k[SDL_SCANCODE_Z])     in |= CAM_IN;
    if (k[SDL_SCANCODE_X])     in |= CAM_OUT;
    if (k[SDL_SCANCODE_Q])     in |= CAM_MODE;
    if (k[SDL_SCANCODE_SPACE]) in |= JUMP;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) in |= RUN;
    return in;
}

static SDL_Window *win;
static world w;
static camera cam;
static ticker tk;
static float mouse_x, mouse_y; /* movement not yet used by a tick */

/* The browser owns the loop and calls this once per display frame. */
static void frame(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        /* Clicking captures the mouse for the camera (pointer lock); the browser's Esc lets it go. */
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            SDL_SetWindowRelativeMouseMode(win, true);
        if (e.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(win))
            mouse_x += e.motion.xrel, mouse_y += e.motion.yrel;
    }
    int n = ticker_due(&tk, SDL_GetTicksNS() / 1000);
    unsigned b = read_input();
    int look_x = 0, look_y = 0; /* the mouse moved since the last tick, spread over the ticks due */
    if (n) {
        look_from_motion((int)(mouse_x / n), (int)(mouse_y / n), &look_x, &look_y);
        mouse_x = mouse_y = 0;
    }
    for (; n > 0; n--) {
        input in[MAX_PLAYERS] = {{b, cam.now.yaw, look_x, look_y}};
        w = world_update(w, in);
        cam = camera_update(cam, in[0], &w);
    }
    int pw, ph;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    size_frame(screen_width(pw, ph));
    draw_scene(&w, &cam);
    present(win);
    SDL_GL_SwapWindow(win);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    /* Resizable: SDL then keeps the canvas's pixel size matching the page (see shell.html). */
    win = SDL_CreateWindow("hangout64", SCREEN_W * 3, SCREEN_H * 3,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win || !SDL_GL_CreateContext(win)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }

    if (experience_load(&pl, builtin_experience, builtin_experience_size) != PLACE_OK) {
        SDL_Log("built-in experience: %s", experience_error);
        return 1;
    }
    w = world_init(&pl, 1);
    cam = camera_init();
    setup_shader();
    setup_frame();
    upload_textures();
    gpu_level = upload_mesh(&level_mesh);
    for (int i = 0; i < pl.mesh_count; i++)
        gpu_place[i] = upload_mesh(&pl.meshes[i]);
    for (int i = 0; i < MAX_PLAYERS; i++)
        for (int k = 0; k < AVATAR_PARTS; k++)
            gpu_avatars[i][k] = upload_mesh(&avatar_parts[i][k]);
    glEnable(GL_DEPTH_TEST);

    emscripten_set_main_loop(frame, 0, true); /* never returns */
    return 0;
}
