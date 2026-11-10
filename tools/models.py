"""Road Rash: Jailbreak 3D models (RMD3 / DOD3 / DPD3 / BBD3), read from the
owner's own disc. Exports every bike, rider, car, pedestrian and hazard mesh as
OBJ, and turns an edited OBJ into the replacement the runtime swaps in
(source/mod_assets.cpp). The format, as far as it is known, is in MODDING.md.

    python models.py export <installation> [<out-folder>]

A replacement keeps the mesh's vertex slots and parts: vertices may move and
faces and UVs may change, but the vertex count, the parts (o / g names) and
each part's face budget stay those of the original LOD (see check()).
No game data is stored in this file.
"""
import argparse, pathlib, re, struct, sys

MODEL_FILES = re.compile(r'/DATA/(?:BBLEVEL\w*|CAR\w+|HAZARD\d|PED01A|CARSC)\.GEO$|/DATA/\w+\.MRO$')
SCALE = 1000.0          # game units per OBJ unit (a bike is about 2 units long)
MAGIC = b'RRMS'         # replacement mesh for the runtime
class ModelError(ValueError): pass
def require(c, m):
    if not c: raise ModelError(m)
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def fnv(data, h=2166136261):
    for x in data: h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h

class Lod:
    """One level of detail: DOD3 (header, vertex list) with its DPD3 parts and BBD3."""
    def __init__(self, b, p, model, index, source):
        self.model, self.index, self.source = model, index, source
        size = u32(b, p + 4); self.flags = u32(b, p + 0x0C); self.radius = u32(b, p + 0x10)
        groups = u16(b, p + 0x18); vl = p + u32(b, p + 0x24); n = u32(b, vl)
        self.vertex_bytes = b[vl:vl + 4 + 8 * n]
        self.vertices = [struct.unpack_from('<4h', b, vl + 4 + 8 * i) for i in range(n)]
        self.parts = []; q = p + size
        for g in range(groups):
            require(b[q:q + 4] == b'DPD3', 'DPD3 expected')
            pl = q + u32(b, q + 0x14); count = u32(b, pl)
            polys = [b[pl + 4 + 20 * j:pl + 24 + 20 * j] for j in range(count)]
            self.parts.append(dict(first=u32(b, q + 0x10), count=u16(b, q + 0x0E), faces=count, poly_bytes=b[pl:pl + 4 + 20 * count], polys=polys))
            q += u32(b, q + 4)
        self.end = q
        # The id the runtime finds in RAM: these bytes are not changed by the game's loader.
        h = fnv(self.vertex_bytes)
        for part in self.parts: h = fnv(part['poly_bytes'], h)
        self.mesh = h

def parse(b, source):
    """Models of one GEO / MRO file: {model id: [Lod, ...]} in file order."""
    models = {}; p = 0; current = None
    while p + 12 <= len(b):
        tag, size = b[p:p + 4], u32(b, p + 4)
        if tag == b'RMD3': current = u32(b, p + 8); models.setdefault(current, []); p += 16; continue
        if tag == b'DOD3':
            lod = Lod(b, p, current, len(models[current]), source); models[current].append(lod); p = lod.end; continue
        if tag in (b'BBD3', b'LECT', b'DPD3'): p += size; continue
        break
    return models

def disc_models(disc):
    """Every model LOD on the disc, by mesh id (first file wins for shared meshes)."""
    found = {}
    for e in disc.inventory():
        if MODEL_FILES.search(e['path']):
            for mid, lods in parse(disc.read(e), e['path']).items():
                for lod in lods: found.setdefault(lod.mesh, lod)
    return found

def flags_name(poly): return f'tp{u16(poly, 2):04x}_c{u16(poly, 6):04x}'

# Faces: word A bits 11-13 give the corner count (3 = triangle, 4 = quad);
# quads are in plain cyclic order 0-1-2-3. The game's triangles store index 0
# and UV (0, 0) in the unused fourth slot. A few quads repeat index 2 as index 3
# (a collapsed quad, no area); they export as quads with that vertex twice, so
# their own fourth UV survives. An editor that turns one into a triangle keeps
# its material (word A still saying 4), which rebuilds the collapsed quad.
CORNERS = 0x3800
def face_corners(poly):
    return [0, 1, 2] if (u16(poly, 2) & CORNERS) >> 11 == 3 else [0, 1, 2, 3]

def obj_text(lod):
    """OBJ (y up, 1 unit = 1000 game units) with page-space UVs, one group per part."""
    out = [f'# Road Rash: Jailbreak model {lod.model}, LOD {lod.index} from {lod.source}',
           f'# mesh {lod.mesh:08x}: {len(lod.vertices)} vertices in {len(lod.parts)} parts, radius {lod.radius}',
           '# Keep every vertex (move them, do not add or delete) and keep the part groups.',
           '# Each part may hold at most the faces listed; faces are triangles or quads.',
           '# UVs are on the part\'s 256x256 texture page; usemtl keeps the game\'s page and palette words.']
    for g, part in enumerate(lod.parts):
        out.append(f'# part{g}: vertices {part["first"] + 1}-{part["first"] + part["count"]}, at most {part["faces"]} faces')
    out.append(f'o mesh_{lod.mesh:08x}')
    for x, y, z, pad in lod.vertices: out.append(f'v {x / SCALE:.4f} {-y / SCALE:.4f} {-z / SCALE:.4f}')
    faces = []; vt = 0
    for g, part in enumerate(lod.parts):
        faces.append(f'g part{g}'); mtl = None
        for poly in part['polys']:
            uv = [(poly[0], poly[1]), (poly[4], poly[5]), (poly[8], poly[9]), (poly[10], poly[11])]
            idx = struct.unpack_from('<4H', poly, 12)
            corners = face_corners(poly)
            if flags_name(poly) != mtl: mtl = flags_name(poly); faces.append(f'usemtl {mtl}')
            for c in corners: out.append(f'vt {(uv[c][0] + .5) / 256:.5f} {1 - (uv[c][1] + .5) / 256:.5f}')
            faces.append('f ' + ' '.join(f'{idx[c] + 1}/{vt + i + 1}' for i, c in enumerate(corners))); vt += len(corners)
    return '\n'.join(out + faces) + '\n'

def parse_obj(text, what):
    """Vertices, UVs and faces by group, without any game data (structure only)."""
    require(len(text) <= 16 * 1024 * 1024, 'Model file is too large: ' + what)
    v, vt, groups, group, mtl = [], [], {}, None, None
    for n, line in enumerate(text.splitlines(), 1):
        f = line.split()
        if not f or f[0].startswith('#'): continue
        try:
            if f[0] == 'v': v.append(tuple(float(x) for x in f[1:4]))
            elif f[0] == 'vt': vt.append(tuple(float(x) for x in f[1:3]))
            elif f[0] == 'g' and len(f) > 1: group = f[1]; groups.setdefault(group, [])
            elif f[0] == 'usemtl' and len(f) > 1: mtl = f[1]
            elif f[0] == 'f':
                require(len(f) in (4, 5), f'{what} line {n}: faces must be triangles or quads')
                groups.setdefault(group, [])
                corners = []
                for c in f[1:]:
                    parts = c.split('/'); vi = int(parts[0]); ti = int(parts[1]) if len(parts) > 1 and parts[1] else 0
                    vi = vi - 1 if vi > 0 else len(v) + vi; ti = (ti - 1 if ti > 0 else len(vt) + ti) if ti else None
                    require(0 <= vi < len(v) and (ti is None or 0 <= ti < len(vt)), f'{what} line {n}: face index out of range')
                    corners.append((vi, ti))
                groups[group].append((corners, mtl))
        except ValueError as e:
            raise ModelError(f'{what} line {n}: {e}')
    require(v and any(groups.values()), 'Model has no vertices or faces: ' + what)
    require(all(abs(c) < 1000 for p in v for c in p), 'Model coordinates out of range: ' + what)
    return v, vt, groups

def check(lod, text, what):
    """Validate an edited OBJ against the original LOD and build the runtime replacement."""
    v, vt, groups = parse_obj(text, what)
    require(len(v) == len(lod.vertices), f'{what}: keep all {len(lod.vertices)} vertices of the original (it has {len(v)}); move them instead of adding or deleting')
    # Size budget: within 1.25x the original's own extent from its origin (the
    # DOD3 radius field is not a bounding sphere; rider parts sit in bone space).
    extent = max((x * x + y * y + z * z) ** .5 for x, y, z, _ in lod.vertices) * 1.25 + 2
    verts = []
    for (x, y, z), stock in zip(v, lod.vertices):
        gx, gy, gz = round(x * SCALE), round(-y * SCALE), round(-z * SCALE)
        require(all(-32768 <= c <= 32767 for c in (gx, gy, gz)), f'{what}: a vertex is too far out')
        require((gx * gx + gy * gy + gz * gz) ** .5 <= extent, f'{what}: vertex ({x:.3f}, {y:.3f}, {z:.3f}) is more than 1.25x the original size ({extent / SCALE:.3f} from the origin)')
        verts.append(struct.pack('<4h', gx, gy, gz, stock[3]))
    # A face goes to its group's part (g partN, as exported) or, when an editor
    # dropped the groups, to the part holding most of its vertices.
    owner = [g for g, p in enumerate(lod.parts) for _ in range(p['count'])]
    by_part = [[] for _ in lod.parts]
    for name, faces in groups.items():
        m = re.fullmatch(r'part(\d+)', name or '')
        for corners, mtl in faces:
            if m and int(m[1]) < len(lod.parts): g = int(m[1])
            else: votes = [owner[i] for i, _ in corners]; g = max(votes, key=votes.count)
            by_part[g].append((corners, mtl))
    out = [MAGIC, struct.pack('<5I', 1, lod.mesh, len(verts), len(lod.parts), 0)]
    polys = []
    for g, faces in enumerate(by_part):
        stock = lod.parts[g]
        require(len(faces) <= stock['faces'], f'{what}: part{g} has {len(faces)} faces; the game has room for {stock["faces"]}')
        default = stock['polys'][0] if stock['polys'] else bytes(20)
        records = []
        for corners, mtl in faces:
            m = re.fullmatch(r'tp([0-9a-f]{4})_c([0-9a-f]{4})', mtl or '')
            tpage, clut = (int(m[1], 16), int(m[2], 16)) if m else (u16(default, 2), u16(default, 6))
            uv = [(min(255, max(0, int(vt[t][0] * 256))), min(255, max(0, int((1 - vt[t][1]) * 256)))) if t is not None else (0, 0) for _, t in corners]
            idx = [i for i, _ in corners]
            if len(corners) == 4: tpage = (tpage & ~CORNERS) | 4 << 11
            elif (tpage & CORNERS) >> 11 == 4: idx.append(idx[2]); uv.append(uv[2])     # a collapsed quad, as exported
            else: tpage = (tpage & ~CORNERS) | 3 << 11; idx.append(0); uv.append((0, 0))   # a triangle, as the game stores them
            records.append(struct.pack('<2BH2BH4B4H', *uv[0], tpage, *uv[1], clut, *uv[2], *uv[3], *idx))
        out.append(struct.pack('<I', len(records))); polys.append(b''.join(records))
    return b''.join(out) + b''.join(verts) + b''.join(polys)

def export(disc, folder):
    """All models as OBJ plus index.csv (mesh, file, model, lod, vertices, parts, faces)."""
    folder = pathlib.Path(folder); folder.mkdir(parents=True, exist_ok=True); rows = ['mesh,file,model,lod,vertices,parts,faces,radius']
    seen = set()
    for e in disc.inventory():
        if not MODEL_FILES.search(e['path']): continue
        name = e['path'].rsplit('/', 1)[1].replace('.', '_')
        for mid, lods in parse(disc.read(e), e['path']).items():
            for lod in lods:
                rows.append(f'{lod.mesh:08x},{e["path"]},{mid},{lod.index},{len(lod.vertices)},{len(lod.parts)},{sum(p["faces"] for p in lod.parts)},{lod.radius}')
                if lod.mesh in seen: continue
                seen.add(lod.mesh); sub = folder / name; sub.mkdir(exist_ok=True)
                (sub / f'model{mid}_lod{lod.index}_{lod.mesh:08x}.obj').write_text(obj_text(lod))
    (folder / 'index.csv').write_text('\n'.join(rows) + '\n')
    return len(seen)

if __name__ == '__main__':
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    from disc import Disc
    a = argparse.ArgumentParser(description=__doc__); a.add_argument('operation', choices=['export']); a.add_argument('installation', type=pathlib.Path); a.add_argument('out', type=pathlib.Path, nargs='?')
    a = a.parse_args(); d = Disc(a.installation / 'game-disc.bin')
    try: print(f'{export(d, a.out or a.installation / "mods/model-dump")} meshes exported')
    finally: d.close()
