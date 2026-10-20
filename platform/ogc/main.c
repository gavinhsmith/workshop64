#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif
#include "world.h"

/* gu.h maps guMtx* to libogc's paired-single (ps_) routines on GEKKO. Those corrupted the
 * Wii build's model matrices (the avatar and mesh parts exploded; which build broke depended
 * on unrelated code, i.e. on register allocation), so use the plain C versions: a handful
 * of matrices a frame doesn't need the speed. */
#undef guMtxConcat
#define guMtxConcat c_guMtxConcat
#undef guMtxTrans
#define guMtxTrans c_guMtxTrans
#undef guMtxTransApply
#define guMtxTransApply c_guMtxTransApply
#undef guMtxIdentity
#define guMtxIdentity c_guMtxIdentity
#undef guMtxRotRad
#define guMtxRotRad c_guMtxRotRad

#define FIFO_SIZE (256 * 1024)

static place pl; /* the loaded place; mesh parts are drawn from it */
static GXTexObj gpu_textures[1 + PLACE_MAX_TEXTURES]; /* by texture slot */

/* Expand each 16-colour place texture to RGB565 in GX's tiled layout (4x4-pixel tiles,
 * 32 bytes each, row-major) and set it up nearest-filtered. */
static void upload_textures(void)
{
    texture *all[1 + PLACE_MAX_TEXTURES]; /* the avatar's, then the place's */
    int count = 0;
    all[count++] = &avatar_texture;
    for (int i = 0; i < pl.texture_count; i++)
        all[count++] = &pl.textures[i];
    for (int i = 0; i < count; i++) {
        texture *t = all[i];
        t->slot = i;
        uint16_t *buf = memalign(32, t->w * t->h * 2);
        for (int y = 0; y < t->h; y++) {
            for (int x = 0; x < t->w; x++) {
                const uint8_t *c = t->palette[texel(t, x, y)];
                int tile = (y / 4) * (t->w / 4) + x / 4;
                buf[tile * 16 + (y % 4) * 4 + x % 4] = (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3;
            }
        }
        DCFlushRange(buf, t->w * t->h * 2);
        GX_InitTexObj(&gpu_textures[i], buf, t->w, t->h, GX_TF_RGB565, GX_REPEAT, GX_REPEAT, GX_FALSE);
        GX_InitTexObjLOD(&gpu_textures[i], GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    }
}

static GXRModeObj mode;
static void *xfb[2];
static void *frame_tex;      /* the frame, as an RGBA8 texture */
static int scene_w = SCREEN_W; /* the frame is scene_w x SCREEN_H: wider when the Wii is set to 16:9 */

static const GXColor BG = {25, 25, 38, 255};
static const GXColor BLACK = {0, 0, 0, 255};

/* Clear the scene area to the background (and far Z). A texture copy is the
 * only way GX clears part of the EFB; the copied pixels are overwritten next frame.
 * Copy clears only touch Z while Z updates are enabled. */
static void clear_scene_area(void)
{
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetCopyClear(BG, GX_MAX_Z24);
    GX_SetTexCopySrc(0, 0, scene_w, SCREEN_H);
    GX_SetTexCopyDst(scene_w, SCREEN_H, GX_TF_RGBA8, GX_FALSE);
    GX_CopyTex(frame_tex, GX_TRUE);
    GX_SetCopyClear(BLACK, GX_MAX_Z24);
}

static void video_init(void)
{
    VIDEO_Init();
    mode = *VIDEO_GetPreferredMode(NULL);
    VIDEO_Configure(&mode);

    for (int i = 0; i < 2; i++) {
        xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(&mode));
        VIDEO_ClearFrameBuffer(&mode, xfb[i], COLOR_BLACK);
    }
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode.viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    void *fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    u32 xfb_height = GX_SetDispCopyYScale(GX_GetYScaleFactor(mode.efbHeight, mode.xfbHeight));
    GX_SetDispCopySrc(0, 0, mode.fbWidth, mode.efbHeight);
    GX_SetDispCopyDst(mode.fbWidth, xfb_height);
    /* No vertical deflicker filter: it would blur the doubled lines. */
    GX_SetCopyFilter(mode.aa, mode.sample_pattern, GX_FALSE, mode.vfilter);
    GX_SetFieldMode(mode.field_rendering, mode.viHeight == 2 * mode.xfbHeight ? GX_ENABLE : GX_DISABLE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCullMode(GX_CULL_NONE);

    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 8); /* 8.8 = MESH_ONE */
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGB, GX_RGB8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT2, GX_VA_POS, GX_POS_XYZ, GX_S16, 8);
    GX_SetVtxAttrFmt(GX_VTXFMT2, GX_VA_CLR0, GX_CLR_RGB, GX_RGB8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT2, GX_VA_TEX0, GX_TEX_ST, GX_S16, 8); /* 8.8, like MESH_ONE */
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);

#ifdef HW_RVL
    /* Wii set to 16:9: the same 240 lines, with more columns for the wider view: 428 (240 x 16/9,
     * to a multiple of 4 for texture copies). They are scaled to the 640-wide frame, which the
     * TV stretches to widescreen. */
    if (CONF_GetAspectRatio() == CONF_ASPECT_16_9)
        scene_w = 428;
#endif
    frame_tex = memalign(32, GX_GetTexBufferSize(scene_w, SCREEN_H, GX_TF_RGBA8, GX_FALSE, 0));
    clear_scene_area();
}

/* Textured meshes use VTXFMT2 (with texture coordinates) and a TEV stage that multiplies
 * the texture by the triangle colour; untextured ones pass the colour straight through. */
static void draw_mesh(const mesh *m, Mtx mv)
{
    int textured = m->texture != NULL;
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    if (textured) {
        GX_LoadTexObj(&gpu_textures[m->texture->slot], GX_TEXMAP0);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetNumTexGens(1);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    }
    GX_Begin(GX_TRIANGLES, textured ? GX_VTXFMT2 : GX_VTXFMT0, m->tris * 3);
    for (int t = 0; t < m->tris; t++) {
        for (int c = 0; c < 3; c++) {
            const int16_t *v = m->verts[t * 3 + c];
            GX_Position3s16(v[0], v[1], v[2]);
            GX_Color3u8(m->colors[t][0], m->colors[t][1], m->colors[t][2]);
            if (textured)
                GX_TexCoord2s16(m->uvs[t * 3 + c][0], m->uvs[t * 3 + c][1]);
        }
    }
    GX_End();
    if (textured) {
        GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
        GX_SetNumTexGens(0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    }
}

/* Pass 1: the 3D scene into the top-left scene_w x 240 of the EFB. */
static void draw_scene(const world *w, const camera *cam)
{
    const view *v = &cam->now;
    /* triangles seen from behind (meshes are counter-clockwise from outside) are never drawn. GX takes clockwise as the front, so that's GX_CULL_FRONT. */
    GX_SetCullMode(GX_CULL_FRONT);
    GX_SetViewport(0, 0, scene_w, SCREEN_H, 0, 1);
    GX_SetScissor(0, 0, scene_w, SCREEN_H);
    Mtx44 proj;
    guPerspective(proj, 53.0f, (f32)scene_w / SCREEN_H, 0.2f, 100.0f);
    GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetNumChans(1);
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    /* view = translate(0, 0, -dist) rotX(pitch) rotY(-yaw) translate(-x, -y, -z) */
    Mtx rx, ry, t, rot, view, model, mv;
    guMtxRotDeg(rx, 'x', v->pitch / 10.0f);
    guMtxRotDeg(ry, 'y', -v->yaw / 10.0f);
    guMtxConcat(rx, ry, rot);
    guMtxTrans(t, -v->x / 1000.0f, -v->y / 1000.0f, -v->z / 1000.0f);
    guMtxConcat(rot, t, view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -v->dist / 1000.0f);
    draw_mesh(&level_mesh, view);

    /* Mesh parts: the model's origin sits at the part's bottom centre. */
    for (int i = 0; i < pl.part_count; i++) {
        const part *q = &pl.parts[i];
        if (q->mesh < 0 || !part_visible(v, scene_w, SCREEN_H, q, &pl.meshes[q->mesh]))
            continue; /* a box (in the level mesh), or off screen */
        guMtxTrans(t, q->x / 1000.0f, FLOOR_Y + (q->y - q->sy / 2) / 1000.0f, q->z / 1000.0f);
        guMtxConcat(view, t, mv);
        draw_mesh(&pl.meshes[q->mesh], mv);
    }

    /* player = translate(x, FLOOR_Y + y + lift, z) rotY(heading); each part then
     * translate(joint) rotX(swing) */
    for (int i = 0; i < w->player_count; i++) {
        if (camera_hides(cam, i) || !player_visible(v, scene_w, SCREEN_H, &w->players[i]))
            continue;
        const player *p = &w->players[i];
        pose pz = player_pose(p);
        guMtxRotDeg(ry, 'y', p->heading / 10.0f);
        guMtxTransApply(ry, model, p->x / 1000.0f, FLOOR_Y + (p->y + pz.lift) / 1000.0f, p->z / 1000.0f);
        for (int k = 0; k < AVATAR_PARTS; k++) {
            int j[3], turn;
            avatar_place(&pz, k, j, &turn);
            Mtx joint, part;
            guMtxRotDeg(rx, 'x', turn / 10.0f);
            guMtxTransApply(rx, joint, j[0] / 1000.0f, j[1] / 1000.0f, j[2] / 1000.0f);
            guMtxConcat(model, joint, part);
            guMtxConcat(view, part, mv);
            draw_mesh(&avatar_parts[i][k], mv);
        }
    }
}

/* Pass 2: copy the frame to a texture and draw it over the whole EFB with
 * nearest filtering (the VI and copy scalers can only filter). */
static void draw_upscaled(void)
{
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetTexCopySrc(0, 0, scene_w, SCREEN_H);
    GX_SetTexCopyDst(scene_w, SCREEN_H, GX_TF_RGBA8, GX_FALSE);
    GX_CopyTex(frame_tex, GX_TRUE);
    GX_PixModeSync();
    GX_InvalidateTexAll();

    GXTexObj tex;
    GX_InitTexObj(&tex, frame_tex, scene_w, SCREEN_H, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&tex, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&tex, GX_TEXMAP0);

    GX_SetViewport(0, 0, mode.fbWidth, mode.efbHeight, 0, 1);
    GX_SetScissor(0, 0, mode.fbWidth, mode.efbHeight);
    Mtx44 ortho;
    guOrtho(ortho, 0, mode.efbHeight, 0, mode.fbWidth, 0, 1);
    GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
    Mtx id;
    guMtxIdentity(id);
    GX_LoadPosMtxImm(id, GX_PNMTX0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetNumChans(0);
    GX_SetNumTexGens(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

    f32 x0 = 0, x1 = mode.fbWidth, y1 = mode.efbHeight;
    GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
    GX_Position3f32(x0, 0, -0.5f); GX_TexCoord2f32(0, 0);
    GX_Position3f32(x1, 0, -0.5f); GX_TexCoord2f32(1, 0);
    GX_Position3f32(x1, y1, -0.5f); GX_TexCoord2f32(1, 1);
    GX_Position3f32(x0, y1, -0.5f); GX_TexCoord2f32(0, 1);
    GX_End();
}

#ifdef HW_RVL
/* Power button on the console or a Wii Remote. Dolphin's Stop presses it too and
 * waits for the program to shut down. */
static volatile int power_pressed;
static void on_power(void) { power_pressed = 1; }
static void on_wiimote_power(s32 chan) { (void)chan; power_pressed = 1; }
#endif

#define STICK_DEADZONE 32
#define CSTICK_FULL 80 /* C-stick reading pushed all the way (it rarely reaches its gate's 127) */

/* A C-stick axis as part of a look stick: nothing inside the dead zone, then up to LOOK_FULL. */
static int cstick(int v)
{
    int a = v < 0 ? -v : v;
    if (a <= STICK_DEADZONE)
        return 0;
    a = (a - STICK_DEADZONE) * LOOK_FULL / (CSTICK_FULL - STICK_DEADZONE);
    a = a > LOOK_FULL ? LOOK_FULL : a;
    return v < 0 ? -a : a;
}

/* GC pad: stick moves, A jumps, B runs, C-stick or D-pad orbits, L/R zoom in/out, Z switches
 * the camera. Wii Remote: D-pad moves, A jumps, 1 runs, B + D-pad orbits, -/+ zoom, 2 switches
 * the camera. START / HOME sets *quit. */
static unsigned read_input(int *quit, int *look_x, int *look_y)
{
    PAD_ScanPads();
    u16 b = PAD_ButtonsHeld(0);
    s8 x = PAD_StickX(0), y = PAD_StickY(0), cx = PAD_SubStickX(0), cy = PAD_SubStickY(0);
    unsigned in = 0;
    if (x < -STICK_DEADZONE) in |= MOVE_LEFT;
    if (x > STICK_DEADZONE)  in |= MOVE_RIGHT;
    if (y > STICK_DEADZONE)  in |= MOVE_FWD;
    if (y < -STICK_DEADZONE) in |= MOVE_BACK;
    if (b & PAD_BUTTON_LEFT)  in |= CAM_LEFT;
    if (b & PAD_BUTTON_RIGHT) in |= CAM_RIGHT;
    if (b & PAD_BUTTON_UP)    in |= CAM_UP;
    if (b & PAD_BUTTON_DOWN)  in |= CAM_DOWN;
    *look_x = cstick(cx);
    *look_y = -cstick(cy); /* up on the stick is up */
    if (b & PAD_TRIGGER_L) in |= CAM_IN;
    if (b & PAD_TRIGGER_R) in |= CAM_OUT;
    if (b & PAD_TRIGGER_Z) in |= CAM_MODE;
    if (b & PAD_BUTTON_A)  in |= JUMP;
    if (b & PAD_BUTTON_B)  in |= RUN;
    *quit = (PAD_ButtonsDown(0) & PAD_BUTTON_START) != 0;
#ifdef HW_RVL
    WPAD_ScanPads();
    u32 w = WPAD_ButtonsHeld(0);
    int orbit = (w & WPAD_BUTTON_B) != 0;
    if (w & WPAD_BUTTON_LEFT)  in |= orbit ? CAM_LEFT : MOVE_LEFT;
    if (w & WPAD_BUTTON_RIGHT) in |= orbit ? CAM_RIGHT : MOVE_RIGHT;
    if (w & WPAD_BUTTON_UP)    in |= orbit ? CAM_UP : MOVE_FWD;
    if (w & WPAD_BUTTON_DOWN)  in |= orbit ? CAM_DOWN : MOVE_BACK;
    if (w & WPAD_BUTTON_MINUS) in |= CAM_IN;
    if (w & WPAD_BUTTON_PLUS)  in |= CAM_OUT;
    if (w & WPAD_BUTTON_2)     in |= CAM_MODE;
    if (w & WPAD_BUTTON_A)     in |= JUMP;
    if (w & WPAD_BUTTON_1)     in |= RUN;
    if ((WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) || power_pressed)
        *quit = 1;
#endif
    return in;
}

int main(void)
{
    video_init();
    PAD_Init();
#ifdef HW_RVL
    WPAD_Init();
    SYS_SetPowerCallback(on_power);
    WPAD_SetPowerButtonCallback(on_wiimote_power);
#endif

    int fb = 0, quit = 0;
    if (experience_load(&pl, builtin_experience, builtin_experience_size) != PLACE_OK)
        exit(1);
    world w = world_init(&pl, 1);
    upload_textures();
    camera cam = camera_init();
    ticker tk = {0};
    while (!quit) {
        draw_scene(&w, &cam);
        draw_upscaled();

        GX_DrawDone();
        fb ^= 1;
        GX_SetColorUpdate(GX_TRUE);
        GX_CopyDisp(xfb[fb], GX_TRUE); /* clears the whole EFB to black */
        clear_scene_area();
        VIDEO_SetNextFramebuffer(xfb[fb]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        int look_x, look_y;
        unsigned b = read_input(&quit, &look_x, &look_y);
        for (int n = ticker_due(&tk, ticks_to_microsecs(gettime())); n > 0; n--) {
            input in[MAX_PLAYERS] = {{b, cam.now.yaw, look_x, look_y}};
            w = world_update(w, in);
            cam = camera_update(cam, in[0], &w);
        }
    }
#ifdef HW_RVL
    if (power_pressed)
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
#endif
    exit(0);
}
