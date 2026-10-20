"""Pack an experience folder (docs/engine.md section 11.6) into a .wb64 file.

    python3 tools/wb64.py experiences/course course.wb64
    python3 tools/wb64.py experiences/course build/gen/builtin_experience.c builtin_experience

Logic files (experience.lua, scenes/, scripts/, ui/) are copied byte for byte; the engine
checks them when it loads them (core/wb64.c), so run the result in the PC build or
`make test` to check those. Models and textures are converted to their canonical forms here.

Format (all little-endian):
    "WB64"  u16 format version (1)  u16 entry count
    entries: u8 kind  u8 variant  u16 name length  u32 offset  u32 size  name
    blobs, at their offsets from the start of the file

    kind 1  logic: Lua source; the name is its path in the folder ("scenes/course.lua")
    kind 2  texture: canonical form; the name is its path in textures/ without the extension
            u8 log2 width, log2 height (3..5: 8..32 px)  u8 pad[2]
            palette 16 * (u8 r, g, b)  pixels w*h/2 bytes (4-bit indices, rows top to bottom,
            low nibble = left pixel)
    kind 3  model: canonical form; named like textures, from models/
            u16 verts  u16 tris  s16 texture (-1 none, else the n-th texture entry)  u16 pad
            verts * (s16 x, y, z, u, v)  tris * (u16 a, b, c, u8 r, g, b)
    variant 0 is the canonical form; per-platform variants come later. Readers skip entries
    whose kind or variant they don't know.

Model positions and texture coordinates are 8.8 fixed point (256 = 1 unit / the whole
texture). Models are OBJ files at their real size in units, Z-up with the front facing -Y;
colours come from the .mtl's Kd, the texture from its map_Kd (a file in textures/).
"""
import os
import struct
import sys

from objload import load_mtl, load_obj
from texload import load_texture

VERSION = 1
LOGIC, TEXTURE, MODEL = 1, 2, 3
LOGIC_DIRS = ('scenes', 'scripts', 'ui')


def fail(msg):
    sys.exit(f'wb64: {msg}')


def files(root, sub, ext):
    """Paths under root/sub with extension ext, relative to root/sub, '/'-separated, sorted."""
    out = []
    for d, _, names in os.walk(os.path.join(root, sub)):
        for n in names:
            if n.endswith(ext):
                out.append(os.path.relpath(os.path.join(d, n), os.path.join(root, sub)).replace(os.sep, '/'))
    return sorted(out)


def pack_texture(path):
    w, h, palette, pixels, su, sv, _ = load_texture(path)
    data = struct.pack('<BBxx', w.bit_length() - 1, h.bit_length() - 1)
    return data + b''.join(struct.pack('BBB', *c) for c in palette) + pixels, (su, sv)


def pack_model(path, texture_ids, uv_scale):
    materials = load_mtl(path)
    maps = {m for _, m in materials.values() if m}
    if len(maps) > 1:
        fail(f'{path}: uses {len(maps)} textures; a model can use one')
    texture = -1
    if maps:
        name = os.path.splitext(maps.pop())[0].replace('\\', '/')
        if name not in texture_ids:
            fail(f'{path}: its .mtl uses texture "{name}", which is not in textures/')
        texture = texture_ids[name]
    try:
        verts, uvs, tris, colors = load_obj(path, None, {k: rgb for k, (rgb, _) in materials.items()})
    except ValueError as e:
        fail(e)
    if texture >= 0 and uvs is None:
        fail(f'{path}: has a texture but no texture coordinates')
    if texture >= 0:  # the texture may have been padded to a power of two
        su, sv = uv_scale[texture]
        uvs = [(round(u * su), round(v * sv)) for u, v in uvs]
    uvs = uvs or [(0, 0)] * len(verts)
    if len(verts) > 0xffff or len(tris) > 0xffff:
        fail(f'{path}: too many vertices or triangles')
    data = struct.pack('<HHhxx', len(verts), len(tris), texture)
    data += b''.join(struct.pack('<hhhhh', *v, *uv) for v, uv in zip(verts, uvs))
    return data + b''.join(struct.pack('<HHHBBB', *t, *c) for t, c in zip(tris, colors))


def pack(root):
    allowed = {'experience.lua', 'models', 'textures', *LOGIC_DIRS}
    for name in os.listdir(root):
        if name not in allowed:
            fail(f'{root}: unexpected "{name}" (expected {", ".join(sorted(allowed))})')
    if not os.path.isfile(os.path.join(root, 'experience.lua')):
        fail(f'{root}: no experience.lua')

    entries = []  # (kind, name, data)
    for path in ['experience.lua'] + [f'{d}/{p}' for d in LOGIC_DIRS for p in files(root, d, '.lua')]:
        entries.append((LOGIC, path, open(os.path.join(root, path), 'rb').read()))

    texture_ids, uv_scale = {}, []
    for path in files(root, 'textures', '.png'):
        data, scale = pack_texture(os.path.join(root, 'textures', path))
        texture_ids[path[:-4]] = len(texture_ids)
        uv_scale.append(scale)
        entries.append((TEXTURE, path[:-4], data))
    for path in files(root, 'models', '.obj'):
        entries.append((MODEL, path[:-4], pack_model(os.path.join(root, 'models', path), texture_ids, uv_scale)))

    header_size = 8 + sum(12 + len(name.encode()) for _, name, _ in entries)
    index, blobs = b'', b''
    for kind, name, data in entries:
        n = name.encode()
        index += struct.pack('<BBHII', kind, 0, len(n), header_size + len(blobs), len(data)) + n
        blobs += data
    return b'WB64' + struct.pack('<HH', VERSION, len(entries)) + index + blobs


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = pack(src)
    if not dst.endswith('.c'):
        open(dst, 'wb').write(data)
        return
    name = sys.argv[3]
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'/* Generated by tools/wb64.py from {src.replace(os.sep, "/")}; do not edit. */\n')
        f.write('#include "world.h"\n\n')
        f.write(f'const uint8_t {name}[] = {{\n')
        for i in range(0, len(data), 16):
            f.write('    ' + ' '.join(f'0x{b:02x},' for b in data[i:i + 16]) + '\n')
        f.write('};\n')
        f.write(f'const unsigned {name}_size = sizeof {name};\n')


main()
