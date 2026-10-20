#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include "world.h"
#include "vshader_shbin.h"

#define DISPLAY_TRANSFER_FLAGS                                                   \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |                              \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |                              \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define BG_VERTS 6

static place pl; /* the loaded place; mesh parts are drawn from it */
static int mesh_first[PLACE_MAX_MESHES]; /* where each place mesh starts in the vbo */

typedef struct {
    float pos[3];
    float clr[3];
    float tc[2];
} vertex;

static C3D_Tex gpu_textures[MAX_PLAYERS + PLACE_MAX_TEXTURES]; /* by texture slot */

/* Bit-interleave x and y (0..7) into a position in an 8x8 tile (Morton / Z order). */
static u32 morton8(u32 x, u32 y)
{
    u32 m = 0;
    for (int i = 0; i < 3; i++)
        m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
    return m;
}

/* Expand each 16-colour place texture to RGB565 in the PICA's tiled layout (8x8 tiles in
 * Morton order, rows stored bottom-up) and upload it, nearest-filtered. */
static void upload_textures(void)
{
    texture *all[MAX_PLAYERS + PLACE_MAX_TEXTURES]; /* the avatars', then the place's */
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        all[count++] = &avatar_textures[i];
    for (int i = 0; i < pl.texture_count; i++)
        all[count++] = &pl.textures[i];
    static u16 data[TEXTURE_MAX * TEXTURE_MAX];
    for (int i = 0; i < count; i++) {
        texture *t = all[i];
        t->slot = i;
        for (int y = 0; y < t->h; y++) {
            for (int x = 0; x < t->w; x++) {
                const uint8_t *c = t->palette[texel(t, x, y)];
                int fy = t->h - 1 - y;
                u32 tile = (fy / 8) * (t->w / 8) + x / 8;
                data[tile * 64 + morton8(x % 8, fy % 8)] = (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3;
            }
        }
        C3D_TexInit(&gpu_textures[i], t->w, t->h, GPU_RGB565);
        C3D_TexUpload(&gpu_textures[i], data);
        C3D_TexSetFilter(&gpu_textures[i], GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&gpu_textures[i], GPU_REPEAT, GPU_REPEAT);
    }
}

/* Textured: texture * triangle colour. Untextured: the colour as is. */
static void set_texture(const texture *t)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    if (t) {
        C3D_TexBind(0, &gpu_textures[t->slot]);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    } else {
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    }
}

static const int quad_tris[6] = {0, 1, 2, 0, 2, 3};

static vertex *add_mesh(vertex *out, const mesh *m)
{
    const float k = 1.0f / MESH_ONE;
    for (int t = 0; t < m->tris; t++) {
        for (int c = 0; c < 3; c++) {
            const int16_t *v = m->verts[t * 3 + c];
            const int16_t *uv = m->uvs ? m->uvs[t * 3 + c] : (const int16_t[2]){0, 0};
            *out++ = (vertex){{v[0] * k, v[1] * k, v[2] * k},
                              {m->colors[t][0] / 255.0f, m->colors[t][1] / 255.0f,
                               m->colors[t][2] / 255.0f},
                              {uv[0] * k, uv[1] * k}};
        }
    }
    return out;
}

/* Background quad (clip space, drawn with identity matrices), then the guy, the level
 * and the place's meshes, all as triangles. Call after world_init() so the level mesh
 * exists. */
static vertex *build_vbo(void)
{
    static const float bg[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    int tris = guy_mesh.tris + level_mesh.tris;
    for (int i = 0; i < pl.mesh_count; i++)
        tris += pl.meshes[i].tris;
    vertex *vbo = linearAlloc(sizeof(vertex) * (BG_VERTS + tris * 3));
    vertex *out = vbo;
    for (int i = 0; i < 6; i++)
        *out++ = (vertex){{bg[quad_tris[i]][0], bg[quad_tris[i]][1], -0.5f}, {0.1f, 0.1f, 0.15f}, {0, 0}};
    out = add_mesh(out, &guy_mesh);
    out = add_mesh(out, &level_mesh);
    for (int i = 0; i < pl.mesh_count; i++) {
        mesh_first[i] = out - vbo;
        out = add_mesh(out, &pl.meshes[i]);
    }
    return vbo;
}

/* Microseconds since boot, from the ARM11 tick counter (split to avoid overflow). */
static uint64_t now_us(void)
{
    uint64_t t = svcGetSystemTick();
    return t / SYSCLOCK_ARM11 * 1000000 + t % SYSCLOCK_ARM11 * 1000000 / SYSCLOCK_ARM11;
}

/* Circle pad moves, A jumps, B runs, D-pad orbits, L/R zoom in/out. */
static unsigned read_input(u32 keys)
{
    unsigned in = 0;
    if (keys & KEY_CPAD_LEFT)  in |= MOVE_LEFT;
    if (keys & KEY_CPAD_RIGHT) in |= MOVE_RIGHT;
    if (keys & KEY_CPAD_UP)    in |= MOVE_FWD;
    if (keys & KEY_CPAD_DOWN)  in |= MOVE_BACK;
    if (keys & KEY_DLEFT)      in |= CAM_LEFT;
    if (keys & KEY_DRIGHT)     in |= CAM_RIGHT;
    if (keys & KEY_DUP)        in |= CAM_UP;
    if (keys & KEY_DDOWN)      in |= CAM_DOWN;
    if (keys & KEY_L)          in |= CAM_IN;
    if (keys & KEY_R)          in |= CAM_OUT;
    if (keys & KEY_A)          in |= JUMP;
    if (keys & KEY_B)          in |= RUN;
    return in;
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    printf("hangout64\n\nCircle pad: move\nA:          jump\nB:          run\nD-pad:      orbit\nL/R:        zoom\nSTART:      exit\n");

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    /* The top screen is physically rotated: its framebuffer is 240 wide, 400 tall. */
    C3D_RenderTarget *top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    DVLB_s *dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
    shaderProgram_s prog;
    shaderProgramInit(&prog);
    shaderProgramSetVsh(&prog, &dvlb->DVLE[0]);
    C3D_BindProgram(&prog);
    int u_proj = shaderInstanceGetUniformLocation(prog.vertexShader, "projection");
    int u_mv = shaderInstanceGetUniformLocation(prog.vertexShader, "modelView");

    C3D_AttrInfo *attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attr, 2, GPU_FLOAT, 2);

    if (experience_load(&pl, builtin_experience, builtin_experience_size) != PLACE_OK) {
        printf("built-in experience: %s\n", experience_error);
        return 1;
    }
    world w = world_init(&pl, 1);
    camera cam = camera_init();
    vertex *vbo = build_vbo();
    C3D_BufInfo *buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, vbo, sizeof(vertex), 3, 0x210);

    upload_textures();
    C3D_CullFace(GPU_CULL_NONE);

    C3D_Mtx proj, identity;
    Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(53.0f), 4.0f / 3.0f, 0.2f, 100.0f, false);
    Mtx_Identity(&identity);

    ticker tk = {0};
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break;
        unsigned b = read_input(hidKeysHeld());
        for (int n = ticker_due(&tk, now_us()); n > 0; n--) {
            input in[MAX_PLAYERS] = {{b, cam.now.yaw}};
            w = world_update(w, in);
            cam = camera_update(cam, b, &w.players[0]);
        }

        /* view = translate(0, 0, -dist) rotX(pitch) rotY(-yaw) translate(-x, -y, -z) */
        const view *v = &cam.now;
        C3D_Mtx view;
        Mtx_Identity(&view);
        Mtx_Translate(&view, 0.0f, 0.0f, -v->dist / 1000.0f, true);
        Mtx_RotateX(&view, C3D_AngleFromDegrees(v->pitch / 10.0f), true);
        Mtx_RotateY(&view, C3D_AngleFromDegrees(-v->yaw / 10.0f), true);
        Mtx_Translate(&view, -v->x / 1000.0f, -v->y / 1000.0f, -v->z / 1000.0f, true);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C3D_RenderTargetClear(top, C3D_CLEAR_ALL, 0x000000FF, 0);
        C3D_FrameDrawOn(top);
        /* Pillarbox: the middle 320 of the 400 columns (y in the rotated framebuffer). */
        C3D_SetViewport(0, (400 - SCREEN_W) / 2, SCREEN_H, SCREEN_W);

        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &identity);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &identity);
        set_texture(NULL);
        C3D_DrawArrays(GPU_TRIANGLES, 0, BG_VERTS);

        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
        for (int i = 0; i < w.player_count; i++) {
            const player *p = &w.players[i];
            C3D_Mtx m;
            Mtx_Copy(&m, &view);
            Mtx_Translate(&m, p->x / 1000.0f, FLOOR_Y + (p->y + player_bob(p)) / 1000.0f, p->z / 1000.0f, true);
            Mtx_RotateY(&m, C3D_AngleFromDegrees(p->heading / 10.0f), true);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &m);
            set_texture(avatar_meshes[i].texture);
            C3D_DrawArrays(GPU_TRIANGLES, BG_VERTS, guy_mesh.tris * 3);
            set_texture(NULL);
        }
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &view);
        C3D_DrawArrays(GPU_TRIANGLES, BG_VERTS + guy_mesh.tris * 3, level_mesh.tris * 3);

        /* Mesh parts: the model's origin sits at the part's bottom centre. */
        for (int i = 0; i < pl.part_count; i++) {
            const part *q = &pl.parts[i];
            if (q->mesh < 0)
                continue;
            C3D_Mtx m;
            Mtx_Copy(&m, &view);
            Mtx_Translate(&m, q->x / 1000.0f, FLOOR_Y + (q->y - q->sy / 2) / 1000.0f, q->z / 1000.0f, true);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &m);
            set_texture(pl.meshes[q->mesh].texture);
            C3D_DrawArrays(GPU_TRIANGLES, mesh_first[q->mesh], pl.meshes[q->mesh].tris * 3);
            set_texture(NULL);
        }
        C3D_FrameEnd(0);
    }

    linearFree(vbo);
    shaderProgramFree(&prog);
    DVLB_Free(dvlb);
    C3D_RenderTargetDelete(top);
    C3D_Fini();
    gfxExit();
    return 0;
}
