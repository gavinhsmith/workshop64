#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <string.h>
#include "world.h"

/* Power-of-two texture holding the frame (SCREEN_H high, up to SCREEN_W_MAX wide), so plain
 * GL 1.1 works everywhere. */
#define TEX_W SCREEN_W_MAX
#define TEX_H 256

static place pl; /* the loaded place; mesh parts are drawn from it */
static GLuint gpu_textures[MAX_PLAYERS + PLACE_MAX_TEXTURES]; /* by texture slot */

/* Expand each 16-colour place texture to RGB and upload it, nearest-filtered. */
static void upload_textures(void)
{
    texture *all[MAX_PLAYERS + PLACE_MAX_TEXTURES]; /* the avatars', then the place's */
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        all[count++] = &avatar_textures[i];
    for (int i = 0; i < pl.texture_count; i++)
        all[count++] = &pl.textures[i];
    static uint8_t rgb[TEXTURE_MAX * TEXTURE_MAX][3];
    glGenTextures(count, gpu_textures);
    for (int i = 0; i < count; i++) {
        texture *t = all[i];
        t->slot = i;
        for (int y = 0; y < t->h; y++)
            for (int x = 0; x < t->w; x++)
                memcpy(rgb[y * t->w + x], t->palette[texel(t, x, y)], 3);
        glBindTexture(GL_TEXTURE_2D, gpu_textures[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, t->w, t->h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

/* WASD moves, Space jumps, arrows orbit, Q/E zoom in/out. */
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
    if (k[SDL_SCANCODE_Q])     in |= CAM_IN;
    if (k[SDL_SCANCODE_E])     in |= CAM_OUT;
    if (k[SDL_SCANCODE_SPACE]) in |= JUMP;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) in |= RUN;
    return in;
}

/* Textured meshes: the triangle colour tints the texture (GL_MODULATE, the default). */
static void draw_mesh(const mesh *m)
{
    const float k = 1.0f / MESH_ONE;
    if (m->texture) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, gpu_textures[m->texture->slot]);
    }
    glBegin(GL_TRIANGLES);
    for (int t = 0; t < m->tris; t++) {
        glColor3ubv(m->colors[t]);
        for (int c = 0; c < 3; c++) {
            const int16_t *v = m->verts[t * 3 + c];
            if (m->uvs)
                glTexCoord2f(m->uvs[t * 3 + c][0] * k, m->uvs[t * 3 + c][1] * k);
            glVertex3f(v[0] * k, v[1] * k, v[2] * k);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/* Drawn sw x SCREEN_H in the corner of the back buffer, then scaled up by present(). */
static void draw_scene(const world *w, const view *v, int sw)
{
    glViewport(0, 0, sw, SCREEN_H);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    double r = 0.1 * sw / SCREEN_H; /* same vertical view everywhere; wider sees more */
    glFrustum(-r, r, -0.1, 0.1, 0.2, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -v->dist / 1000.0f);
    glRotatef(v->pitch / 10.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(-v->yaw / 10.0f, 0.0f, 1.0f, 0.0f);
    glTranslatef(-v->x / 1000.0f, -v->y / 1000.0f, -v->z / 1000.0f);
    draw_mesh(&level_mesh);

    /* Mesh parts: the model's origin sits at the part's bottom centre. */
    for (int i = 0; i < pl.part_count; i++) {
        const part *q = &pl.parts[i];
        if (q->mesh < 0)
            continue;
        glPushMatrix();
        glTranslatef(q->x / 1000.0f, FLOOR_Y + (q->y - q->sy / 2) / 1000.0f, q->z / 1000.0f);
        draw_mesh(&pl.meshes[q->mesh]);
        glPopMatrix();
    }

    for (int i = 0; i < w->player_count; i++) {
        const player *p = &w->players[i];
        glPushMatrix();
        glTranslatef(p->x / 1000.0f, FLOOR_Y + (p->y + player_bob(p)) / 1000.0f, p->z / 1000.0f);
        glRotatef(p->heading / 10.0f, 0.0f, 1.0f, 0.0f);
        draw_mesh(&avatar_meshes[i]);
        glPopMatrix();
    }
}

/* Grab the sw x SCREEN_H frame and draw it as big as fits (nearest-filtered), centred. It
 * matches the window's shape, so there are bars only past SCREEN_W_MAX wide or under 4:3. */
static void present(SDL_Window *win, GLuint tex, int sw)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, sw, SCREEN_H);

    int w, h;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    float scale = SDL_min((float)w / sw, (float)h / SCREEN_H);
    float x0 = (w - sw * scale) / 2.0f, y0 = (h - SCREEN_H * scale) / 2.0f;
    float x1 = x0 + sw * scale, y1 = y0 + SCREEN_H * scale;
    float u = (float)sw / TEX_W, v = (float)SCREEN_H / TEX_H;

    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x0, y0);
    glTexCoord2f(u, 0); glVertex2f(x1, y0);
    glTexCoord2f(u, v); glVertex2f(x1, y1);
    glTexCoord2f(0, v); glVertex2f(x0, y1);
    glEnd();

    SDL_GL_SwapWindow(win);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_Window *win = SDL_CreateWindow("hangout64", SCREEN_W * 3, SCREEN_H * 3,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win || !SDL_GL_CreateContext(win)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(win, SCREEN_W, SCREEN_H);
    SDL_GL_SetSwapInterval(1);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEX_W, TEX_H, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); /* sharp pixels */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (experience_load(&pl, builtin_experience, builtin_experience_size) != PLACE_OK) {
        SDL_Log("built-in experience: %s", experience_error);
        return 1;
    }
    world w = world_init(&pl, 1);
    upload_textures();
    camera cam = camera_init();
    ticker tk = {0};
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
                goto done;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11 && !e.key.repeat)
                SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
        }
        unsigned b = read_input();
        for (int n = ticker_due(&tk, SDL_GetTicksNS() / 1000); n > 0; n--) {
            input in[MAX_PLAYERS] = {{b, cam.now.yaw}};
            w = world_update(w, in);
            cam = camera_update(cam, b, &w.players[0]);
        }
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        int sw = screen_width(pw, ph);
        draw_scene(&w, &cam.now, sw);
        present(win, tex, sw);
    }
done:
    SDL_Quit();
    return 0;
}
