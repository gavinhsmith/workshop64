#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "world.h"

static place pl; /* the loaded place; mesh parts are drawn from it */
static int gpu_textures[1 + PLACE_MAX_TEXTURES]; /* by texture slot */

/* The DS's 16-colour texture format matches the place format (4 bits, low nibble = left
 * pixel), so pixels go up as they are; only the palette is converted to RGB15. */
static void upload_textures(void)
{
    texture *all[1 + PLACE_MAX_TEXTURES]; /* the avatar's, then the place's */
    int count = 0;
    all[count++] = &avatar_texture;
    for (int i = 0; i < pl.texture_count; i++)
        all[count++] = &pl.textures[i];
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankE(VRAM_E_TEX_PALETTE);
    glGenTextures(count, gpu_textures);
    for (int i = 0; i < count; i++) {
        texture *t = all[i];
        t->slot = i;
        u16 palette[16];
        for (int c = 0; c < 16; c++)
            palette[c] = RGB15(t->palette[c][0] >> 3, t->palette[c][1] >> 3, t->palette[c][2] >> 3);
        glBindTexture(0, gpu_textures[i]);
        /* Sizes are TEXTURE_SIZE_8/16/32 = log2(pixels) - 3. */
        glTexImage2D(0, 0, GL_RGB16, __builtin_ctz(t->w) - 3, __builtin_ctz(t->h) - 3, 0,
                     TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T, t->pixels);
        glColorTableEXT(0, 0, 16, 0, 0, palette);
    }
    glBindTexture(0, 0);
}

/* Microseconds since cpuStartTiming(). Its bus-clock count is 32 bits and wraps every two
 * minutes, so it's accumulated into 64 bits; called every frame, it never misses a wrap. */
static uint64_t now_us(void)
{
    static uint64_t total;
    static u32 last;
    u32 t = cpuGetTiming();
    total += t - last;
    last = t;
    return total / BUS_CLOCK * 1000000 + total % BUS_CLOCK * 1000000 / BUS_CLOCK;
}

/* D-pad moves, A jumps, B runs, R + D-pad orbits, X/Y zoom in/out, L switches the camera. */
static unsigned read_input(u32 keys)
{
    unsigned in = 0;
    int orbit = (keys & KEY_R) != 0;
    if (keys & KEY_LEFT)  in |= orbit ? CAM_LEFT : MOVE_LEFT;
    if (keys & KEY_RIGHT) in |= orbit ? CAM_RIGHT : MOVE_RIGHT;
    if (keys & KEY_UP)    in |= orbit ? CAM_UP : MOVE_FWD;
    if (keys & KEY_DOWN)  in |= orbit ? CAM_DOWN : MOVE_BACK;
    if (keys & KEY_A)     in |= JUMP;
    if (keys & KEY_B)     in |= RUN;
    if (keys & KEY_X)     in |= CAM_IN;
    if (keys & KEY_Y)     in |= CAM_OUT;
    if (keys & KEY_L)     in |= CAM_MODE;
    return in;
}

/* Tenths of a degree -> DS angle units (wrapped first so it can't overflow). */
static int angle(int deci)
{
    return (deci % 3600 + 3600) % 3600 * DEGREES_IN_CIRCLE / 3600;
}

/* Thousandths of a unit -> f32. */
static int f32m(int thousandths)
{
    return thousandths * 4096 / 1000;
}

/* v16 is 4.12 and only reaches +/-8, the floor needs +/-10: send half-size
 * vertices (8.8 -> 4.12 is *16, halved is *8) and scale the matrix by 2. */
static void draw_mesh(const mesh *m)
{
    glPushMatrix();
    glScalef32(inttof32(2), inttof32(2), inttof32(2));
    /* Textured: the triangle colour tints the texture (the default modulate mode).
     * Texture coordinates are 12.4 texels: 8.8 fractions of the texture * size / 16. */
    const texture *tex = m->texture;
    glBindTexture(0, tex ? gpu_textures[tex->slot] : 0);
    glBegin(GL_TRIANGLES);
    for (int t = 0; t < m->tris; t++) {
        glColor3b(m->colors[t][0], m->colors[t][1], m->colors[t][2]);
        for (int c = 0; c < 3; c++) {
            const int16_t *v = m->verts[t * 3 + c];
            if (tex)
                glTexCoord2t16(m->uvs[t * 3 + c][0] * tex->w / 16, m->uvs[t * 3 + c][1] * tex->h / 16);
            glVertex3v16(v[0] * 8, v[1] * 8, v[2] * 8);
        }
    }
    glEnd();
    glBindTexture(0, 0);
    glPopMatrix(1);
}

/* The chat lives on the bottom screen: SELECT brings up libnds's touch keyboard over its
 * lower half; Enter says the line, B (or the keyboard's top-left key) cancels. */
static chat_log chat;
static char draft[CHAT_MAX + 1];
static int typing;

/* Reprint the bottom screen: the controls and the chat, or while typing (the keyboard covers
 * the lower half) the last few lines and the draft. */
static void show_chat(void)
{
    consoleClear();
    if (!typing)
        printf("workshop64\n\nD-pad:     move\nA:         jump\nB:         run\nR + D-pad: orbit\nX/Y:       zoom\nL:         camera\nSELECT:    chat\nSTART:     exit\n\n");
    for (int i = typing && chat.count > 3 ? chat.count - 3 : 0; i < chat.count; i++)
        printf("%s\n", chat.lines[i]);
    if (typing)
        printf("> %s_", draft);
}

static void chat_update(u32 down)
{
    if (!typing) {
        if (down & KEY_SELECT) {
            typing = 1;
            draft[0] = '\0';
            keyboardShow();
            show_chat();
        }
        return;
    }
    int c = keyboardUpdate();
    size_t n = strlen(draft);
    if (c == DVK_ENTER || c == DVK_FOLD || (down & KEY_B)) {
        if (c == DVK_ENTER && n)
            chat_say(&chat, "TestPlayer", draft, 0);
        typing = 0;
        keyboardHide();
    } else if (c == DVK_BACKSPACE && n) {
        draft[n - 1] = '\0';
    } else if (c >= ' ' && c <= '~' && n < CHAT_MAX) {
        draft[n] = (char)c, draft[n + 1] = '\0';
    } else {
        return; /* nothing changed */
    }
    show_chat();
}

/* The DS renders at its native 256x192 (still 4:3, so same camera). */
int main(void)
{
    videoSetMode(MODE_0_3D);
    consoleDemoInit(); /* text console on the bottom screen */
    keyboardDemoInit();
    keyboardHide();
    show_chat();
    glInit();
    glClearColor(3, 3, 5, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(53, 256.0 / 192.0, 0.2, 100);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK); /* triangles seen from behind (meshes are counter-clockwise from outside) are never drawn */

    if (experience_load(&pl, builtin_experience, builtin_experience_size) != PLACE_OK) {
        printf("built-in experience: %s\n", experience_error);
        return 1;
    }
    world w = world_init(&pl, 1);
    glEnable(GL_TEXTURE_2D);
    upload_textures();
    camera cam = camera_init();
    ticker tk = {0};
    cpuStartTiming(0);
    for (;;) {
        scanKeys();
        if (keysDown() & KEY_START)
            break;
        chat_update(keysDown());

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const view *v = &cam.now;
        glTranslatef32(0, 0, -f32m(v->dist));
        glRotateXi(angle(v->pitch));
        glRotateYi(angle(-v->yaw));
        glTranslatef32(-f32m(v->x), -f32m(v->y), -f32m(v->z));
        draw_mesh(&level_mesh);

        /* Mesh parts: the model's origin sits at the part's bottom centre. */
        for (int i = 0; i < pl.part_count; i++) {
            const part *q = &pl.parts[i];
            if (q->mesh < 0 || !part_visible(&cam.now, 256, 192, q, &pl.meshes[q->mesh]))
                continue; /* a box (in the level mesh), or off screen */
            glPushMatrix();
            glTranslatef32(f32m(q->x), inttof32(FLOOR_Y) + f32m(q->y - q->sy / 2), f32m(q->z));
            draw_mesh(&pl.meshes[q->mesh]);
            glPopMatrix(1);
        }

        for (int i = 0; i < w.player_count; i++) {
            if (camera_hides(&cam, i) || !player_visible(&cam.now, 256, 192, &w.players[i]))
                continue;
            const player *p = &w.players[i];
            glPushMatrix();
            pose pz = player_pose(p);
            glTranslatef32(f32m(p->x), inttof32(FLOOR_Y) + f32m(p->y + pz.lift), f32m(p->z));
            glRotateYi(angle(p->heading));
            for (int k = 0; k < AVATAR_PARTS; k++) { /* each part turns about its joint */
                int j[3], turn;
                avatar_place(&pz, k, j, &turn);
                glPushMatrix();
                glTranslatef32(f32m(j[0]), f32m(j[1]), f32m(j[2]));
                glRotateXi(angle(turn));
                draw_mesh(&avatar_parts[i][k]);
                glPopMatrix(1);
            }
            glPopMatrix(1);
        }

        glFlush(0);
        swiWaitForVBlank();
        unsigned b = typing ? 0 : read_input(keysHeld()); /* typing doesn't move you */
        for (int n = ticker_due(&tk, now_us()); n > 0; n--) {
            input in[MAX_PLAYERS] = {{b, cam.now.yaw, 0, 0}};
            w = world_update(w, in);
            cam = camera_update(cam, in[0], &w);
        }
    }
    return 0;
}
