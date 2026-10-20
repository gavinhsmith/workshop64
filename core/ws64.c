/* Experience loader. Input may come from anywhere (eventually the network), so every length,
 * count, index and value is checked before use; on any error the place is freed and left
 * empty. Format: see tools/ws64.py; the logic files are Lua tables (docs/engine.md 11.6),
 * read in a Lua state with no libraries and capped memory and instructions. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua32.h"
#include "world.h"

#define MAX_ENTRIES 512
#define MAX_NAME 64
#define LUA_MEMORY (256 * 1024) /* for reading the logic files */
#define LUA_STEPS 100           /* thousands of Lua instructions for the same */
#define MAX_DEPTH 16            /* group nesting */
#define FLOOR_TRIS (FLOOR_TILES * FLOOR_TILES * 2)
#define BOX_TRIS 10

char experience_error[128];

enum { KIND_LOGIC = 1, KIND_TEXTURE, KIND_MODEL };

typedef struct {
    unsigned kind, variant, name_len;
    const char *name;
    const uint8_t *data;
    uint32_t size;
} entry;

static unsigned rd16(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(int err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(experience_error, sizeof experience_error, fmt, ap);
    va_end(ap);
    return err;
}

void place_free(place *pl)
{
    for (int i = 0; i < pl->mesh_count; i++) {
        free((void *)pl->meshes[i].verts);
        free((void *)pl->meshes[i].colors);
        free((void *)pl->meshes[i].uvs);
    }
    free(pl->meshes);
    for (int i = 0; i < pl->texture_count; i++)
        free((void *)pl->textures[i].pixels);
    free(pl->textures);
    free(pl->parts);
    memset(pl, 0, sizeof *pl);
}

/* The index: every entry's name and blob within the file. */
static int read_index(const uint8_t *data, size_t size, entry **out, int *count)
{
    if (size < 8 || memcmp(data, "WS64", 4) != 0)
        return fail(PLACE_BAD_MAGIC, "not a .ws64 file");
    if (rd16(data + 4) != WS64_VERSION)
        return fail(PLACE_BAD_VERSION, "unsupported .ws64 format version");
    unsigned n = rd16(data + 6);
    if (n > MAX_ENTRIES)
        return fail(PLACE_OVER_BUDGET, "too many entries");
    entry *e = *out = calloc(n ? n : 1, sizeof *e);
    if (!e)
        return PLACE_NO_MEMORY;
    size_t off = 8;
    for (unsigned i = 0; i < n; i++, e++) {
        if (size - off < 12)
            return fail(PLACE_TRUNCATED, "truncated index");
        const uint8_t *r = data + off;
        e->kind = r[0], e->variant = r[1], e->name_len = rd16(r + 2);
        uint32_t at = rd32(r + 4);
        e->size = rd32(r + 8);
        off += 12;
        if (e->name_len > MAX_NAME || e->name_len > size - off)
            return fail(PLACE_TRUNCATED, "bad entry name");
        e->name = (const char *)data + off;
        off += e->name_len;
        if (at > size || e->size > size - at)
            return fail(PLACE_TRUNCATED, "%.*s: truncated", (int)e->name_len, e->name);
        e->data = data + at;
    }
    *count = (int)n;
    return PLACE_OK;
}

static int load_texture(texture *t, const uint8_t *d, uint32_t len)
{
    if (len < 4)
        return PLACE_BAD_ENTRY;
    if (d[0] < 3 || d[0] > 5 || d[1] < 3 || d[1] > 5) /* TEXTURE_MIN..TEXTURE_MAX */
        return PLACE_BAD_VALUE;
    int w = 1 << d[0], h = 1 << d[1];
    uint32_t bytes = (uint32_t)(w * h / 2);
    if (len != 4 + 48 + bytes)
        return PLACE_BAD_ENTRY;
    uint8_t *pixels = malloc(bytes);
    t->pixels = pixels; /* set before checking, so place_free cleans up either way */
    if (!pixels)
        return PLACE_NO_MEMORY;
    t->w = w;
    t->h = h;
    memcpy(t->palette, d + 4, 48);
    memcpy(pixels, d + 52, bytes);
    return PLACE_OK;
}

#define MESH_HEADER 8
#define MESH_VERT 10 /* x, y, z, u, v */
#define MESH_TRI 9   /* a, b, c, r, g, b */

/* Indexed triangles in the file, expanded to the 3-corners-per-triangle mesh the
 * renderers draw. */
static int load_mesh(const place *pl, mesh *m, const uint8_t *d, uint32_t len)
{
    if (len < MESH_HEADER)
        return PLACE_BAD_ENTRY;
    uint32_t nv = rd16(d), nt = rd16(d + 2);
    int tex = (int16_t)rd16(d + 4);
    if (nv == 0 || nt == 0 || len != MESH_HEADER + nv * MESH_VERT + nt * MESH_TRI)
        return PLACE_BAD_ENTRY;
    if (nt > PLACE_MAX_TRIS)
        return PLACE_OVER_BUDGET;
    if (tex < -1 || tex >= pl->texture_count) /* textures are loaded before models */
        return PLACE_BAD_INDEX;
    m->texture = tex >= 0 ? &pl->textures[tex] : NULL; /* stable: allocated at full size */

    int16_t (*verts)[3] = malloc(nt * 3 * sizeof *verts);
    uint8_t (*colors)[3] = malloc(nt * sizeof *colors);
    int16_t (*uvs)[2] = m->texture ? malloc(nt * 3 * sizeof *uvs) : NULL;
    m->verts = verts; /* set before checking, so place_free cleans up either way */
    m->colors = colors;
    m->uvs = uvs;
    m->tris = (int)nt;
    if (!verts || !colors || (m->texture && !uvs))
        return PLACE_NO_MEMORY;

    const uint8_t *vd = d + MESH_HEADER, *td = vd + nv * MESH_VERT;
    for (uint32_t t = 0; t < nt; t++, td += MESH_TRI) {
        for (int c = 0; c < 3; c++) {
            uint32_t i = rd16(td + 2 * c);
            if (i >= nv)
                return PLACE_BAD_INDEX;
            const uint8_t *v = vd + i * MESH_VERT;
            for (int a = 0; a < 3; a++)
                verts[t * 3 + c][a] = (int16_t)rd16(v + 2 * a);
            if (uvs)
                for (int a = 0; a < 2; a++)
                    uvs[t * 3 + c][a] = (int16_t)rd16(v + 6 + 2 * a);
        }
        memcpy(colors[t], td + 6, 3);
    }
    long long far = 0; /* the corner furthest from the origin, for culling */
    for (uint32_t i = 0; i < nt * 3; i++) {
        long long d = (long long)verts[i][0] * verts[i][0] + (long long)verts[i][1] * verts[i][1] + (long long)verts[i][2] * verts[i][2];
        far = d > far ? d : far;
    }
    m->radius = (int)(isqrt(far) * 1000 / MESH_ONE) + 1;
    return PLACE_OK;
}

/* Canonical textures, then canonical models (which refer to textures by number). */
static int load_assets(place *pl, const entry *e, int count, const entry **models)
{
    for (int i = 0; i < count; i++) {
        if (e[i].kind != KIND_TEXTURE || e[i].variant != 0)
            continue;
        if (pl->texture_count == PLACE_MAX_TEXTURES)
            return fail(PLACE_OVER_BUDGET, "more than 16 textures");
        /* Full size up front, never moved: meshes point into it. */
        if (!pl->textures && !(pl->textures = calloc(PLACE_MAX_TEXTURES, sizeof *pl->textures)))
            return PLACE_NO_MEMORY;
        int err = load_texture(&pl->textures[pl->texture_count++], e[i].data, e[i].size);
        if (err)
            return fail(err, "textures/%.*s: bad texture", (int)e[i].name_len, e[i].name);
    }
    for (int i = 0; i < count; i++) {
        if (e[i].kind != KIND_MODEL || e[i].variant != 0)
            continue;
        for (int j = 0; j < pl->mesh_count; j++)
            if (models[j]->name_len == e[i].name_len && !memcmp(models[j]->name, e[i].name, e[i].name_len))
                return fail(PLACE_BAD_ENTRY, "models/%.*s: twice", (int)e[i].name_len, e[i].name);
        if (pl->mesh_count == PLACE_MAX_MESHES)
            return fail(PLACE_OVER_BUDGET, "more than 32 models");
        if (!pl->meshes && !(pl->meshes = calloc(PLACE_MAX_MESHES, sizeof *pl->meshes)))
            return PLACE_NO_MEMORY;
        models[pl->mesh_count] = &e[i];
        int err = load_mesh(pl, &pl->meshes[pl->mesh_count++], e[i].data, e[i].size);
        if (err)
            return fail(err, "models/%.*s: bad model", (int)e[i].name_len, e[i].name);
    }
    return PLACE_OK;
}

/* --- Logic files, read with Lua ------------------------------------------------------- */

typedef struct {
    place *pl;
    const entry *entries;
    int count;
    const entry *models[PLACE_MAX_MESHES];
    size_t memory;
    int steps;
    int err;         /* what to return if Lua stops with an error */
    char where[128]; /* the file and node being read, for messages */
} loader;

static void *capped_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    loader *ld = ud;
    if (!ptr)
        osize = 0; /* Lua passes the kind of object instead */
    if (nsize == 0) {
        free(ptr);
        ld->memory -= osize;
        return NULL;
    }
    if (nsize > osize && nsize - osize > LUA_MEMORY - ld->memory)
        return NULL;
    void *p = realloc(ptr, nsize);
    if (p)
        ld->memory = ld->memory - osize + nsize;
    return p;
}

static void count_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    loader *ld = *(loader **)lua_getextraspace(L);
    if (++ld->steps > LUA_STEPS)
        luaL_error(L, "takes too long to load");
}

/* Push t[k] without metamethods; returns its type. */
static int field(lua_State *L, int t, const char *k)
{
    lua_pushstring(L, k);
    return lua_rawget(L, t);
}

/* Every key of table t is a string from `allowed` or, if `list`, an index 1..#t. */
static void check_keys(lua_State *L, loader *ld, int t, const char *const *allowed, int list)
{
    lua_Unsigned len = lua_rawlen(L, t);
    lua_pushnil(L);
    while (lua_next(L, t)) {
        lua_pop(L, 1);
        int ok = 0;
        if (lua_type(L, -1) == LUA_TSTRING) {
            const char *k = lua_tostring(L, -1);
            for (const char *const *a = allowed; *a && !ok; a++)
                ok = strcmp(k, *a) == 0;
            if (!ok)
                luaL_error(L, "%s: unknown field '%s'", ld->where, k);
        } else if (list && lua_isinteger(L, -1)) {
            lua_Integer i = lua_tointeger(L, -1);
            ok = i >= 1 && (lua_Unsigned)i <= len;
        }
        if (!ok)
            luaL_error(L, "%s: unexpected key", ld->where);
    }
}

static const char *get_string(lua_State *L, loader *ld, int t, const char *k, int required, size_t max)
{
    int type = field(L, t, k);
    const char *s = NULL;
    size_t len = 0;
    if (type == LUA_TSTRING)
        s = lua_tolstring(L, -1, &len);
    else if (type != LUA_TNIL || required)
        luaL_error(L, "%s: '%s' should be a string", ld->where, k);
    if (len > max)
        luaL_error(L, "%s: '%s' is too long", ld->where, k);
    lua_pop(L, 1); /* still referenced from t, so s stays valid */
    return s;
}

static int get_integer(lua_State *L, loader *ld, int t, const char *k, int fallback, int lo, int hi)
{
    int type = field(L, t, k);
    lua_Integer v = fallback;
    if (type != LUA_TNIL) {
        if (!lua_isinteger(L, -1))
            luaL_error(L, "%s: '%s' should be a whole number", ld->where, k);
        v = lua_tointeger(L, -1);
    }
    if (v < lo || v > hi)
        luaL_error(L, "%s: '%s' should be %d to %d", ld->where, k, lo, hi);
    lua_pop(L, 1);
    return (int)v;
}

/* A length in units (decimals allowed) as thousandths, rounded to nearest. */
static int to_milli(lua_State *L, int i, int *out)
{
    const lua_Number max = PLACE_MAX_COORD / 1000;
    if (lua_isinteger(L, i)) {
        lua_Integer v = lua_tointeger(L, i);
        if (v < -max || v > max)
            return 0;
        *out = (int)v * 1000;
        return 1;
    }
    if (lua_type(L, i) != LUA_TNUMBER)
        return 0;
    lua_Number v = lua_tonumber(L, i);
    if (!(v >= -max && v <= max)) /* also rejects NaN */
        return 0;
    v *= 1000;
    *out = (int)(v < 0 ? v - (lua_Number)0.5 : v + (lua_Number)0.5);
    return 1;
}

/* t[k] = {a, b, c}: lengths (in thousandths) or, if `color`, 0..255. Returns 0 if absent. */
static int get_triple(lua_State *L, loader *ld, int t, const char *k, int out[3], int color)
{
    int type = field(L, t, k);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return 0;
    }
    if (type != LUA_TTABLE || lua_rawlen(L, -1) != 3)
        luaL_error(L, "%s: '%s' should be {%s}", ld->where, k, color ? "r, g, b" : "x, y, z");
    for (int i = 0; i < 3; i++) {
        lua_rawgeti(L, -1, i + 1);
        int ok;
        if (color) {
            ok = lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 0 && lua_tointeger(L, -1) <= 255;
            out[i] = ok ? (int)lua_tointeger(L, -1) : 0;
        } else {
            ok = to_milli(L, -1, &out[i]);
        }
        if (!ok)
            luaL_error(L, "%s: '%s' has a bad value", ld->where, k);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 1;
}

/* Run a logic file and leave the table it returns on the stack. */
static void run_file(lua_State *L, loader *ld, const char *path)
{
    const entry *e = NULL;
    for (int i = 0; i < ld->count && !e; i++)
        if (ld->entries[i].kind == KIND_LOGIC && ld->entries[i].name_len == strlen(path) &&
            !memcmp(ld->entries[i].name, path, ld->entries[i].name_len))
            e = &ld->entries[i];
    if (!e)
        luaL_error(L, "%s is missing", path);
    char chunk[MAX_NAME + 2];
    snprintf(chunk, sizeof chunk, "=%s", path);
    if (luaL_loadbufferx(L, (const char *)e->data, e->size, chunk, "t") != LUA_OK) /* text only: never bytecode */
        lua_error(L);
    lua_call(L, 0, 1);
    if (!lua_istable(L, -1))
        luaL_error(L, "%s should return a table", path);
    snprintf(ld->where, sizeof ld->where, "%s", path);
}

static const char *const part_keys[] = {"name", "pos", "size", "color", "model", NULL};
static const char *const group_keys[] = {"name", "pos", NULL};
static const char *const scene_keys[] = {"camera", NULL};

static void read_children(lua_State *L, loader *ld, int t, const int origin[3], int depth);

/* A scene node: a part if it has a size, otherwise a group of the nodes in its list. */
static void read_node(lua_State *L, loader *ld, int t, const int origin[3], int depth, int index)
{
    size_t len = strlen(ld->where);
    const char *name = get_string(L, ld, t, "name", 0, 32);
    if (name)
        snprintf(ld->where + len, sizeof ld->where - len, "/%s", name);
    else
        snprintf(ld->where + len, sizeof ld->where - len, "/%d", index);

    int is_part = field(L, t, "size") != LUA_TNIL;
    lua_pop(L, 1);
    check_keys(L, ld, t, is_part ? part_keys : group_keys, !is_part);
    int pos[3] = {0, 0, 0};
    if (!get_triple(L, ld, t, "pos", pos, 0) && is_part)
        luaL_error(L, "%s: needs 'pos'", ld->where);
    for (int a = 0; a < 3; a++) {
        pos[a] += origin[a];
        if (pos[a] < -PLACE_MAX_COORD || pos[a] > PLACE_MAX_COORD)
            luaL_error(L, "%s: outside the world", ld->where);
    }

    if (!is_part) {
        read_children(L, ld, t, pos, depth + 1);
    } else {
        place *pl = ld->pl;
        if (pl->part_count == PLACE_MAX_PARTS) {
            ld->err = PLACE_OVER_BUDGET;
            luaL_error(L, "%s: more than %d parts", ld->where, PLACE_MAX_PARTS);
        }
        part *p = &pl->parts[pl->part_count];
        int size[3], color[3] = {163, 162, 165};
        get_triple(L, ld, t, "size", size, 0);
        for (int a = 0; a < 3; a++)
            if (size[a] < 1)
                luaL_error(L, "%s: 'size' should be positive", ld->where);
        get_triple(L, ld, t, "color", color, 1);
        p->x = pos[0], p->y = pos[1], p->z = pos[2];
        p->sx = size[0], p->sy = size[1], p->sz = size[2];
        for (int a = 0; a < 3; a++)
            p->color[a] = (uint8_t)color[a];
        p->mesh = -1;
        const char *model = get_string(L, ld, t, "model", 0, MAX_NAME);
        for (int i = 0; model && i < pl->mesh_count && p->mesh < 0; i++)
            if (ld->models[i]->name_len == strlen(model) && !memcmp(ld->models[i]->name, model, strlen(model)))
                p->mesh = i;
        if (model && p->mesh < 0)
            luaL_error(L, "%s: no model '%s' in models/", ld->where, model);
        pl->part_count++;
    }
    ld->where[len] = '\0';
}

static void read_children(lua_State *L, loader *ld, int t, const int origin[3], int depth)
{
    if (depth > MAX_DEPTH)
        luaL_error(L, "%s: groups nested too deeply", ld->where);
    luaL_checkstack(L, 8, NULL);
    lua_Unsigned n = lua_rawlen(L, t);
    for (lua_Unsigned i = 1; i <= n; i++) {
        if (lua_rawgeti(L, t, (lua_Integer)i) != LUA_TTABLE)
            luaL_error(L, "%s: entry %d should be a table", ld->where, (int)i);
        read_node(L, ld, lua_gettop(L), origin, depth, (int)i);
        lua_pop(L, 1);
    }
}

static const char *const manifest_keys[] = {"id", "title", "version", "api", "players", "start", "icon", NULL};

/* experience.lua, then the start scene's parts. Runs protected: errors land in lua_pcall. */
static int read_logic(lua_State *L)
{
    loader *ld = lua_touserdata(L, 1);
    run_file(L, ld, "experience.lua");
    int m = lua_gettop(L);
    check_keys(L, ld, m, manifest_keys, 0);
    if (get_integer(L, ld, m, "api", 0, 1, 0x7fff) > ENGINE_API) {
        ld->err = PLACE_NEEDS_UPDATE;
        luaL_error(L, "needs a newer version of workshop64");
    }
    get_string(L, ld, m, "id", 1, 64);
    get_string(L, ld, m, "title", 1, 32);
    get_integer(L, ld, m, "version", 0, 1, 0x7fffffff);
    get_integer(L, ld, m, "players", 1, 1, MAX_PLAYERS);
    const char *start = get_string(L, ld, m, "start", 1, MAX_NAME - 11);
    const char *icon = get_string(L, ld, m, "icon", 0, MAX_NAME);
    int found = 0;
    for (int i = 0; icon && i < ld->count && !found; i++)
        found = ld->entries[i].kind == KIND_TEXTURE && ld->entries[i].name_len == strlen(icon) &&
                !memcmp(ld->entries[i].name, icon, strlen(icon));
    if (icon && !found)
        luaL_error(L, "experience.lua: no texture '%s' for 'icon'", icon);

    char scene[MAX_NAME + 1];
    snprintf(scene, sizeof scene, "scenes/%s.lua", start);
    run_file(L, ld, scene);
    int s = lua_gettop(L);
    check_keys(L, ld, s, scene_keys, 1); /* settings, then the list of nodes */
    ld->pl->has_camera = get_triple(L, ld, s, "camera", ld->pl->camera, 0);
    static const int origin[3] = {0, 0, 0};
    read_children(L, ld, s, origin, 0);
    return 0;
}

static int load_logic(loader *ld)
{
    lua_State *L = lua_newstate(capped_alloc, ld);
    if (!L)
        return PLACE_NO_MEMORY;
    *(loader **)lua_getextraspace(L) = ld;
    lua_sethook(L, count_hook, LUA_MASKCOUNT, 1000);
    lua_pushcfunction(L, read_logic);
    lua_pushlightuserdata(L, ld);
    ld->err = PLACE_BAD_DATA;
    int status = lua_pcall(L, 1, 0, 0);
    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        snprintf(experience_error, sizeof experience_error, "%s",
                 status == LUA_ERRMEM ? "logic files use too much memory" : msg ? msg : "error");
        if (status == LUA_ERRMEM)
            ld->err = PLACE_OVER_BUDGET;
    }
    lua_close(L);
    return status == LUA_OK ? PLACE_OK : ld->err;
}

/* The triangle budget for what gets drawn. */
static int check_place(const place *pl)
{
    long tris = FLOOR_TRIS;
    for (int i = 0; i < pl->part_count; i++)
        tris += pl->parts[i].mesh < 0 ? BOX_TRIS : pl->meshes[pl->parts[i].mesh].tris;
    if (tris > PLACE_MAX_TRIS)
        return fail(PLACE_OVER_BUDGET, "over the triangle budget");
    return PLACE_OK;
}

int experience_load(place *pl, const uint8_t *data, size_t size)
{
    memset(pl, 0, sizeof *pl);
    experience_error[0] = '\0';
    static loader ld; /* big; one load at a time */
    memset(&ld, 0, sizeof ld);
    entry *entries = NULL;
    int err = read_index(data, size, &entries, &ld.count);
    if (!err)
        err = load_assets(pl, entries, ld.count, ld.models);
    if (!err && !(pl->parts = calloc(PLACE_MAX_PARTS, sizeof *pl->parts)))
        err = PLACE_NO_MEMORY;
    if (!err) {
        ld.pl = pl;
        ld.entries = entries;
        err = load_logic(&ld);
    }
    if (!err)
        err = check_place(pl);
    free(entries);
    if (err != PLACE_OK) {
        if (!experience_error[0])
            snprintf(experience_error, sizeof experience_error, "%s", err == PLACE_NO_MEMORY ? "out of memory" : "bad file");
        place_free(pl);
    }
    return err;
}
