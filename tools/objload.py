"""Load an OBJ (triangles or polygons, Z-up, front facing -Y) as an indexed mesh.

The model is turned Y-up with its front facing -Z, centred on X/Z, feet at y = 0 and
scaled to `height` units (or kept at its size in the file when `height` is None). Positions and texture coordinates are 8.8 fixed point (256 = 1
unit, or the full texture). Each triangle gets `rgb` shaded by how much it faces a fixed
light, so the shape reads without runtime lighting. `rgb` is one colour, or a dict of
colours by material name (see load_mtl). Faces in the group "hidden" are skipped
(see authoring/guy_uvs.py).
"""

LIGHT = (0.4, 0.8, 0.45)  # roughly from above, front-right


def load_mtl(path):
    """The materials of the OBJ at `path` (from its mtllib): {name: ((r, g, b), map_Kd or None)},
    colours 0-255 from Kd."""
    import os
    materials, base = {}, os.path.dirname(path)
    for line in open(path, encoding='utf-8'):
        parts = line.split()
        if parts and parts[0] == 'mtllib':
            name = None
            for m in open(os.path.join(base, ' '.join(parts[1:])), encoding='utf-8'):
                m = m.split()
                if m and m[0] == 'newmtl':
                    name = ' '.join(m[1:])
                    materials[name] = ((255, 255, 255), None)
                elif m and m[0] == 'Kd' and name:
                    materials[name] = (tuple(min(255, round(float(k) * 255)) for k in m[1:4]), materials[name][1])
                elif m and m[0] == 'map_Kd' and name:
                    materials[name] = (materials[name][0], ' '.join(m[1:]))
    return materials


def load_obj(path, height, rgb):
    """Returns (verts, uvs, tris, colors): fixed-point (x, y, z), fixed-point (u, v) per
    vertex or None if the model has no texture coordinates, (a, b, c) indices, (r, g, b)."""
    positions, texcoords, faces, face_materials = [], [], [], []
    hidden, material = False, None
    for line in open(path, encoding='utf-8'):
        parts = line.split()
        if parts and parts[0] == 'g':
            hidden = parts[1:] == ['hidden']
        if parts and parts[0] == 'usemtl':
            material = ' '.join(parts[1:])
        if parts and parts[0] == 'v':
            x, y, z = map(float, parts[1:4])
            positions.append((-x, z, y))  # Z-up, front -Y  ->  Y-up, front -Z (a proper rotation)
        elif parts and parts[0] == 'vt':
            u, v = map(float, parts[1:3])
            texcoords.append((u, 1 - v))  # OBJ's v runs up; texture rows run down
        elif parts and parts[0] == 'f' and not hidden:
            corners = []
            for p in parts[1:]:
                refs = p.split('/')
                vt = int(refs[1]) - 1 if len(refs) > 1 and refs[1] and texcoords else None
                corners.append((int(refs[0]) - 1, vt))
            fan = [(corners[0], corners[i], corners[i + 1]) for i in range(1, len(corners) - 1)]
            faces += fan
            face_materials += [material] * len(fan)

    # A vertex is a (position, texcoord) pair; without texcoords keep the file's numbering.
    if texcoords:
        index, verts, uvs, tris = {}, [], [], []
        for face in faces:
            tri = []
            for key in face:
                if key not in index:
                    index[key] = len(verts)
                    verts.append(positions[key[0]])
                    uvs.append(texcoords[key[1]] if key[1] is not None else (0, 0))
                tri.append(index[key])
            tris.append(tuple(tri))
    else:
        verts, uvs = positions, None
        tris = [tuple(c[0] for c in face) for face in faces]

    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    scale = height / (hi[1] - lo[1]) if height else 1
    centre = ((lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2)
    fixed = [tuple(round((v[i] - centre[i]) * scale * 256) for i in range(3)) for v in verts]
    fixed_uvs = [(round(u * 256), round(v * 256)) for u, v in uvs] if uvs else None
    if isinstance(rgb, dict):
        missing = {m for m in face_materials if m not in rgb}
        if missing:
            raise ValueError(f'{path}: faces use materials not in its .mtl: {", ".join(map(str, missing))}')
    colors = [tuple(min(255, round(c * _shade(verts, t))) for c in (rgb[m] if isinstance(rgb, dict) else rgb))
              for t, m in zip(tris, face_materials)]
    return fixed, fixed_uvs, tris, colors


def _shade(verts, t):
    a, b, c = (verts[i] for i in t)
    u = [b[i] - a[i] for i in range(3)]
    w = [c[i] - a[i] for i in range(3)]
    n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
    length = sum(k * k for k in n) ** 0.5 or 1
    light = sum(n[i] * LIGHT[i] for i in range(3)) / length / sum(k * k for k in LIGHT) ** 0.5
    return 0.55 + 0.45 * abs(light)  # abs: don't depend on the file's winding order
