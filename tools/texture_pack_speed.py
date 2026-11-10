"""duckstation_texture_pack.cpp recipe: texture packs without the race slowdown.

With a texture pack (or the texture dump) on, the renderer reports every drawn
primitive's screen rectangle to duck_texture_pack_invalidate, thousands of
times a frame. Each call rebuilt the whole upload list (a shared_ptr copy per
upload) and then ran retire_unused, which scans every upload and page once per
captured source. On the GL render thread that was ~58 % of a race frame, so
the game fell to ~49 VBlanks a second (PROGRESS-0.15-mods.md).

Behaviour is unchanged; only work that cannot change anything is skipped:

* invalidate returns early (still bumping the revision) when the rectangle
  touches no tracked upload, page, page palette, rect-hash or match-cache entry
  -- the rebuild would keep every entry, and with no residency removed there is
  nothing new for retire_unused to retire. A pending dump error keeps the old
  path, which retries finalising on every call;
* retire_unused collects the resident sources once (a set) instead of
  scanning the lists per source;
* begin_copy retires right after it drops the uploads a copy replaces, which
  its following invalidate used to do implicitly.
"""

def _sub(src, old, new):
    if src.count(old) != 1:
        raise RuntimeError(f'texture_pack_speed: expected one match for {old[:60]!r}, found {src.count(old)}')
    return src.replace(old, new)

def extend(src):
    src = _sub(src,
        "void retire_unused(DuckTexturePack& p) {\n"
        "    for (auto it = p.sources.begin(); it != p.sources.end();) {\n"
        "        if (has_residency(p,it->get())) { ++it; continue; }\n",
        "void retire_unused(DuckTexturePack& p) {\n"
        "    /* Road Rash (texture_pack_speed.py): resident sources once, not per source. */\n"
        "    std::unordered_set<const Source*> resident;\n"
        "    resident.reserve(p.uploads.size() + p.pages.size());\n"
        "    for (const auto& upload : p.uploads) resident.insert(upload.source.get());\n"
        "    for (const auto& page : p.pages) resident.insert(page.source.get());\n"
        "    for (auto it = p.sources.begin(); it != p.sources.end();) {\n"
        "        if (resident.count(it->get())) { ++it; continue; }\n")
    src = _sub(src,
        "                                    {px,0,w0,unsigned(h)-h0},{0,0,unsigned(w)-w0,unsigned(h)-h0}}};\n"
        "    ++p->revision;\n",
        "                                    {px,0,w0,unsigned(h)-h0},{0,0,unsigned(w)-w0,unsigned(h)-h0}}};\n"
        "    ++p->revision;\n"
        "    /* Road Rash (texture_pack_speed.py): draws report their screen rectangle\n"
        "     * here thousands of times a frame and almost never touch tracked data.\n"
        "     * Nothing intersecting = the rebuild below keeps everything and retires\n"
        "     * nothing; skip it unless a failed dump finalisation awaits a retry. */\n"
        "    if (p->lifecycle_error.empty()) {\n"
        "        bool touched = false;\n"
        "        for (const auto& r : regions) if (r.w && r.h && !touched) {\n"
        "            for (const auto& upload : p->uploads) if (intersects(upload.active,r)) { touched = true; break; }\n"
        "            for (const auto& page : p->pages) {\n"
        "                if (touched) break;\n"
        "                if (intersects(page.source->rect,r)) { touched = true; break; }\n"
        "                for (const auto& record : page.source->records)\n"
        "                    if (record.depth < 2 && intersects({record.clut_x,record.clut_y,record.palette_size,1},r)) { touched = true; break; }\n"
        "            }\n"
        "            if (!touched) for (const auto& v : p->rect_cache) if (intersects(v.rect,r)) { touched = true; break; }\n"
        "            if (!touched) for (const auto& c : p->match_cache) {\n"
        "                const auto& q = c.query;\n"
        "                const Rect page{q.page_x,q.page_y,256 / expansion(q.depth),256};\n"
        "                const Rect clut{q.clut_x,q.clut_y,std::min(q.depth == 0 ? 16u : 256u,1024u-q.clut_x),1};\n"
        "                if (intersects(page,r) || (q.depth < 2 && intersects(clut,r))) { touched = true; break; }\n"
        "            }\n"
        "        }\n"
        "        if (!touched) return;\n"
        "    }\n")
    src = _sub(src,
        "        duck_texture_pack_invalidate(p,dx,dy,w,h);\n"
        "        p->copy_destination = destination; p->copy_pending = true;\n",
        "        duck_texture_pack_invalidate(p,dx,dy,w,h);\n"
        "        retire_unused(*p);   /* uploads dropped above (texture_pack_speed.py) */\n"
        "        p->copy_destination = destination; p->copy_pending = true;\n")
    return '/* Owner-local texture-pack speed-up, generated from the pinned framework. */\n' + src

if __name__ == '__main__':
    import pathlib, sys
    pathlib.Path(sys.argv[2]).write_text(extend(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8')), encoding='utf-8')
