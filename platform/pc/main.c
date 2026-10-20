#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <string.h>
#include "world.h"

/* Power-of-two texture holding the frame (SCREEN_H high, up to SCREEN_W_MAX wide), so plain
 * GL 1.1 works everywhere. */
#define TEX_W SCREEN_W_MAX
#define TEX_H 256

static place pl; /* the loaded place; mesh parts are drawn from it */
static GLuint gpu_textures[1 + PLACE_MAX_TEXTURES]; /* by texture slot */

/* Expand each 16-colour place texture to RGB and upload it, nearest-filtered. */
static void upload_textures(void)
{
    texture *all[1 + PLACE_MAX_TEXTURES]; /* the avatar's, then the place's */
    int count = 0;
    all[count++] = &avatar_texture;
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
static void draw_scene(const world *w, const camera *cam, int sw)
{
    const view *v = &cam->now;
    glViewport(0, 0, sw, SCREEN_H);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE); /* triangles seen from behind (meshes are counter-clockwise from outside) are never drawn */
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
        if (q->mesh < 0 || !part_visible(v, sw, SCREEN_H, q, &pl.meshes[q->mesh]))
            continue; /* a box (in the level mesh), or off screen */
        glPushMatrix();
        glTranslatef(q->x / 1000.0f, FLOOR_Y + (q->y - q->sy / 2) / 1000.0f, q->z / 1000.0f);
        draw_mesh(&pl.meshes[q->mesh]);
        glPopMatrix();
    }

    for (int i = 0; i < w->player_count; i++) {
        if (camera_hides(cam, i) || !player_visible(v, sw, SCREEN_H, &w->players[i]))
            continue;
        const player *p = &w->players[i];
        glPushMatrix();
        pose pz = player_pose(p);
        glTranslatef(p->x / 1000.0f, FLOOR_Y + (p->y + pz.lift) / 1000.0f, p->z / 1000.0f);
        glRotatef(p->heading / 10.0f, 0.0f, 1.0f, 0.0f);
        for (int k = 0; k < AVATAR_PARTS; k++) { /* each part turns about its joint */
            int j[3], turn;
            avatar_place(&pz, k, j, &turn);
            glPushMatrix();
            glTranslatef(j[0] / 1000.0f, j[1] / 1000.0f, j[2] / 1000.0f);
            glRotatef(turn / 10.0f, 1.0f, 0.0f, 0.0f);
            draw_mesh(&avatar_parts[i][k]);
            glPopMatrix();
        }
        glPopMatrix();
    }
}

/* Grab the sw x SCREEN_H frame and draw it as big as fits (nearest-filtered), centred. It
 * matches the window's shape, so there are bars only past SCREEN_W_MAX wide or under 4:3. */
static void present(SDL_Window *win, GLuint tex, int sw)
{
    glDisable(GL_CULL_FACE);
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

/* --- Chat: / opens a line to type in; Enter says it, Esc cancels. Just echoed locally. ---- */

static chat_log chat;
static char draft[CHAT_MAX + 1];
static int typing, skip_slash, ticks; /* skip_slash: the / that opened it may arrive as text */

static void chat_rect(int x, int y, int w, int h, const uint8_t rgb[3], void *ctx)
{
    (void)ctx;
    glColor3ubv(rgb);
    glVertex2i(x, y);
    glVertex2i(x + w, y);
    glVertex2i(x + w, y + h);
    glVertex2i(x, y + h);
}

/* The chat over the frame (see chat_draw). */
static void draw_chat(int sw)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, sw, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); /* y runs down here, which turns quads over */
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    chat_draw(&chat, typing ? draft : NULL, NULL, ticks, sw, SCREEN_H, chat_rect, NULL);
    glEnd();
}

static void chat_close(SDL_Window *win)
{
    typing = 0;
    SDL_StopTextInput(win);
}

/* Typing keys while the chat line is open; returns whether the event was the chat's. */
static int chat_event(SDL_Window *win, const SDL_Event *e)
{
    if (!typing) {
        if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_SLASH && !e->key.repeat) {
            typing = skip_slash = 1;
            draft[0] = '\0';
            SDL_StartTextInput(win);
            return 1;
        }
        return 0;
    }
    if (e->type == SDL_EVENT_TEXT_INPUT) {
        const char *t = e->text.text;
        if (skip_slash && *t == '/')
            t++;
        skip_slash = 0;
        for (size_t n = strlen(draft); *t && n < CHAT_MAX; t++)
            if (*t >= ' ' && *t <= '~')
                draft[n++] = *t, draft[n] = '\0';
        return 1;
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        skip_slash = 0;
        if (e->key.key == SDLK_BACKSPACE && draft[0])
            draft[strlen(draft) - 1] = '\0';
        if (e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) {
            if (draft[0])
                chat_say(&chat, "TestPlayer", draft, ticks);
            chat_close(win);
        }
        if (e->key.key == SDLK_ESCAPE)
            chat_close(win);
        return 1;
    }
    return 0;
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
    SDL_Window *win = SDL_CreateWindow("workshop64", SCREEN_W * 3, SCREEN_H * 3,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win || !SDL_GL_CreateContext(win)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(win, SCREEN_W, SCREEN_H);
    SDL_GL_SetSwapInterval(1);
    extern const int app_icon_w, app_icon_h; /* assets/icon, built in by tools/icon2c.py */
    extern const unsigned char app_icon[];
    SDL_Surface *icon = SDL_CreateSurfaceFrom(app_icon_w, app_icon_h, SDL_PIXELFORMAT_RGBA32, (void *)app_icon, app_icon_w * 4);
    if (icon) {
        SDL_SetWindowIcon(win, icon);
        SDL_DestroySurface(icon);
    }

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
    float mouse_x = 0, mouse_y = 0; /* movement not yet used by a tick */
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                goto done;
            if (chat_event(win, &e))
                continue;
            /* Clicking captures the mouse for the camera; Esc lets it go, and quits if it's free. */
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                SDL_SetWindowRelativeMouseMode(win, true);
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                if (!SDL_GetWindowRelativeMouseMode(win))
                    goto done;
                SDL_SetWindowRelativeMouseMode(win, false);
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(win))
                mouse_x += e.motion.xrel, mouse_y += e.motion.yrel;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11 && !e.key.repeat)
                SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
        }
        int n = ticker_due(&tk, SDL_GetTicksNS() / 1000);
        unsigned b = typing ? 0 : read_input(); /* typing doesn't move you */
        int look_x = 0, look_y = 0; /* the mouse moved since the last tick, spread over the ticks due */
        if (n) {
            if (!typing)
                look_from_motion((int)(mouse_x / n), (int)(mouse_y / n), &look_x, &look_y);
            mouse_x = mouse_y = 0;
        }
        ticks += n;
        for (; n > 0; n--) {
            input in[MAX_PLAYERS] = {{b, cam.now.yaw, look_x, look_y}};
            w = world_update(w, in);
            cam = camera_update(cam, in[0], &w);
        }
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        int sw = screen_width(pw, ph);
        draw_scene(&w, &cam, sw);
        draw_chat(sw);
        present(win, tex, sw);
    }
done:
    SDL_Quit();
    return 0;
}
