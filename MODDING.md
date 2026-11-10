# Road Rash: Jailbreak mods

Mods are `.rrmod` packs. A pack can change:

- **Textures and UI art**: rider and bike skins, the HUD, fonts, menu art, the sky and the world. Replacements can be larger than the originals (HD).
- **Sounds**: engines, crashes, menu sounds, taunt lines; any sample the game plays.
- **Models**: the shape of bikes, riders, cars, pedestrians and hazards.
- **Frontend artwork** (the older format 1 packs): the menu movies and pictures in `/DATA/FE/*.STR`.

The rules:

- Mods apply only in **Enhanced** mode. Original always plays the stock game.
- **Five-O** keeps the game's own riders: rider and bike textures, sounds and models are left out of Five-O races (rider names still apply).
- Each mod has its own On / Off switch on the launcher's **Mods** page. Up to 8 mods can be installed. Changes apply the next time you press Play.
- Packs never contain the game. Authors start from their own disc (the dump below) and share only their own edits.
- Packs hold pictures, sounds and 3D models only. Code, scripts and executable patches are refused.

## Install and switch mods

1. Open the launcher and choose **Mods**.
2. **Install a mod** and pick the `.rrmod` file. New mods start switched **off**.
3. Select the mod's row (Enter or click) to switch it on or off. The help text says what the pack contains.
4. Press **Play**. The launcher checks every enabled pack before the game starts.

To remove a mod, press Del or click its X. Its files move to `mods\removed` in your installation, so nothing is lost. If two enabled packs replace the same texture, sound or model, the launcher asks you to switch one off.

## Make your own pack

### 1. Dump what the game uses

On the Mods page, switch **Dump for modding** on, then play normally: visit the menus and race the courses and bikes you want to change. Close the game window when done (that saves the last pictures). Everything goes into your installation's `mods` folder (**Open mods folder** opens it):

| Folder | What is in it |
| --- | --- |
| `texture-dump\dumps` | Every texture the game drew, as PNG. The file name is the texture's id; keep it. |
| `sound-dump` | Every sound sample the game played, as WAV, plus `index.csv` (id, rate, loop, length, menu or race). |
| `model-dump` | Every 3D model on your disc, as OBJ, one folder per game file, plus `index.csv`. Written once, when you switch dumping on. |

Texture packs pause while dumping is on, so you see the original game. Switch it off again when you have what you need.

### 2. Lay out your pack folder

Make a folder for your pack. Inside it, use these folders; the second level says what each file is (it decides what Five-O leaves out):

```
My pack\
  textures\rider\   textures\bike\   textures\hud\   textures\font\   textures\menu\   textures\world\
  sounds\rider\     sounds\bike\     sounds\crash\   sounds\menu\     sounds\world\
  models\rider\     models\bike\     models\world\
```

Only create the folders you use.

### 3. Textures and UI art

1. Find the picture you want in `texture-dump\dumps` (a picture viewer with thumbnails helps).
2. Copy it into your pack's `textures\<kind>` folder **without renaming it**.
3. Edit it. You can draw over it at the same size, or scale it up 2x, 4x or more for an HD version; keep the same shape (width-to-height ratio). Transparent pixels stay transparent in game.

Some textures appear several times with different endings: that is the same picture with another palette (another rider's colours, say). Replace each one you want to change.

### 4. Sounds

1. Listen to the files in `sound-dump`. `index.csv` says whether each one was heard in a menu or a race and whether it loops (engines loop).
2. Make your new sound as a WAV file (8, 16 or 24-bit, or 32-bit float, any rate, mono or stereo; up to 10 minutes).
3. Save it in your pack's `sounds\<kind>` folder **named after the sample it replaces**, for example `sounds\bike\6d2a0806.wav`.

The game keeps its own pitch, volume and timing: an engine sound still rises with the revs. Your WAV is converted to the PlayStation's sound format when the game starts. Looping sounds repeat your whole WAV, so make it loop cleanly.

### 5. Models

1. Find the model in `model-dump`. `index.csv` lists every model with its game file, model number and level of detail (LOD 0 is the full-detail one you see up close; 1-3 are the far-away versions). Bikes and riders are in the `BBLEVEL1/2/3_GEO` folders: models 100-120 are bikes, 150 and 159 riders.
2. Open the OBJ in a 3D editor. In Blender: File > Import > Wavefront (.obj), and leave the "split by object / group" options off so the vertex order is kept.
3. Change the shape: move, scale and rotate vertices, change UVs, delete faces or make new ones between existing vertices.
4. Export as OBJ (in Blender: keep UVs and materials; triangles or quads only) into your pack's `models\<kind>` folder with the **same file name** (it ends in the model's id).

Model limits (the game's budgets):

- Keep **every vertex**: same count, same order. Move them instead of adding or deleting them.
- Each part (`g part0`, `g part1` ...) may hold at most as many faces as the original; the OBJ's comments list them.
- Stay within 1.25x the original's size, or the game hides the model too early.
- Parts are stored in their own space: a bike's wheels and fork sit at the origin and are placed by the game; a rider's limbs too. Edit each part where it is.
- Replace each level of detail you want changed; the others stay original. Options > Display > Level of detail keeps LOD 0 on screen longer.

### 6. Build the pack

Run this in a Command Prompt from the launcher's folder, using the Python that came with the game's build tools (change the paths, id and name to yours):

```bat
"%LOCALAPPDATA%\RoadRashJailbreak\toolkits\rr-toolkit-0.6\work\tooling\toolchain\python\python.exe" tools\mod_manager.py --data-root "%LOCALAPPDATA%\RoadRashJailbreak" --operation pack --id "yourname.my-pack" --name "My pack" --version "1.0.0" --source "C:\Mods\My pack" --output "C:\Mods\my-pack.rrmod"
```

The helper checks every file against your own disc (sound rates come from the dump, models from the disc), then writes the `.rrmod`. It never overwrites an existing file. Install the result from the Mods page to try it. Errors appear in `mods.log`.

The id is lower case: letters, digits, dots and dashes, starting with a letter. Raise the version when you share an update.

## Limits and known issues (0.15)

- Packs cost no measurable frame rate in the races tested: with a test texture pack, 60 FPS races held 58.5 frames a second, the same as without it (`tools/texture_pack_speed.py` keeps the framework's texture tracking cheap). Very large packs and 16-rider pile-ups were not measured.
- After **Quick resume** or a savestate, texture replacements appear again only once the game reloads those textures (the next race or menu). Sounds and models apply at once.
- Five-O switching, two-player races and the menus' 3D bike previews (`*.MRO` models) were not checked in game.
- Very large texture packs: the framework decodes up to its memory budget and falls back to the original texture beyond it.

## For tool makers: the pack format

A `.rrmod` is a ZIP with `manifest.json` plus the files it lists:

```json
{
  "format": 2,
  "id": "yourname.my-pack",
  "version": "1.0.0",
  "name": "My pack",
  "game_executable_sha256": "52a3e28b2f97e49d8122eb4f510041e64dce046fd8fa9d4cdc49f9e64ff66a7b",
  "textures": [{"file": "textures/world/texupload-...png", "category": "world", "sha256": "..."}],
  "sounds": [{"file": "sounds/bike/6d2a0806.wav", "sample": "6d2a0806", "category": "bike",
              "rate": 22050, "loop": true, "volume": 100, "sha256": "..."}],
  "models": [{"file": "models/bike/60483c65.obj", "mesh": "60483c65", "category": "bike", "sha256": "..."}]
}
```

- Texture file names are the framework dumper's keys (DuckStation-style: upload hash, palette hash, rectangle); PNG only, up to 8192 x 8192.
- `sample` is FNV-1a (32-bit) of the sample's PS1 ADPCM bytes, from its start to the block with the end flag. `rate` is the rate it first played at (44100 x pitch / 4096); the game's pitch then plays the replacement at the original speed. `volume` (1-400 %, optional) scales the WAV.
- `mesh` is FNV-1a (32-bit) of the model LOD's vertex list and face lists (see below).
- Optional: `author`, `description`, and `assets` (frontend artwork as in format 1, which also needs `disc_sha256`).
- Limits: 4,096 files and 256 MiB expanded per pack; 8 installed packs.

The launcher's manager (`tools/mod_manager.py`) checks packs, keeps them in `mods\library`, and before each Enhanced Play writes `runtime\mods\assets` (merged textures, sounds, and models converted to `.rrmesh`) with `assets.ini`, which the game reads (`source/mod_assets.cpp`). Sounds use private SPU sample banks 1-12.

## For tool makers: the model format (what is known)

Models live in `/DATA/BBLEVEL*.GEO` (bikes and riders by difficulty level), `CAR*.GEO`, `PED01A.GEO`, `HAZARD*.GEO`, `CARSC.GEO` and the bike-shop `*.MRO` files. All values are little-endian. `tools/models.py` reads and writes it.

| Block | Layout |
| --- | --- |
| `LECT` | Texture block (palettes and pixels with their VRAM place); `size` at +4. Skipped by the model reader. |
| `RMD3` | Model header, 16 bytes: magic, size of the whole model, model id, then byte +12 = number of LODs. |
| `DOD3` | One LOD. +4 size (header and lists), +8 model id, +0x0C flags, +0x10 a size value (not a bounding sphere), +0x14 scale (0x1000), +0x18 u16 number of parts, +0x20 offset of the part tree, +0x24 offset of the vertex list, +0x28 / +0x2C / +0x30 offsets of three optional lists (0 = none). Offsets count from the DOD3 start. Vertex list: u32 count, then `count` x {s16 x, y, z, pad} (y points down). |
| `DPD3` | One part, right after its DOD3 (as many as the DOD3 says). +0x0D part index, +0x0E u16 vertex count, +0x10 u32 first vertex, +0x14 offset of the face list: u32 count, then 20-byte faces. |
| face | u8 u0, v0; u16 word A; u8 u1, v1; u16 word B; u8 u2, v2, u3, v3; u16 vertex index x 4. Word A bits 11-13 give the number of corners: 3 = triangle (0x1811, 0x9811), 4 = quad (0x2011, 0xA011, 0x2091, 0xA091). Quads are in plain cyclic order 0-1-2-3. A triangle stores index 0 and UV (0, 0) in the unused fourth slot (all 3,076 on the disc do). 43 quads (car models) repeat index 2 as index 3, a quad collapsed to a triangle. The exporter writes them as quads with that vertex twice. If an editor turns one into a triangle, its material (word A still saying 4) turns it back. Bit 7 (0x0080) picks the shared wheel texture; 0x8000 is likely semi-transparency. The rest of word A is the texture page; word B is 0 on 85 % of faces, otherwise a small number (up to about 0x24), probably a palette slot set at run time. |
| `BBD3` | Bounds, 32 bytes, after the LOD's parts. |

Facts confirmed on all 1,764 LODs on the disc: parts cover the vertex list in order without gaps; 96 % of faces use vertices of their own part, the rest join neighbouring parts (rider seams); every index is below the vertex count.

In RAM, the race loader (overlay code at 0x8005C0C4) keeps each block where it was read and turns the offsets into pointers. A 50-slot table at 0x800CE1B0 (16 bytes: id, LOD count, complete flag at +6, LOD array) points to each LOD's {DOD3, DPD3 array, BBD3}. Game objects hold the model's table slot (+0x60) and the current LOD's DOD3 (+0). The runtime swaps a replacement in place inside those blocks, so every pointer stays valid; it saves the original bytes and puts them back for Five-O.

What remains to be cracked:

- The part tree at DOD3 +0x20 (a byte-coded hierarchy) is still unread. How the game places parts is now known (it is not taken from that tree):
  - A table in the race overlay (`RASHCDG.BIN`, 0x800CC790) gives each part's parent and a joint vertex of the parent.
  - The `ANIMTBL*.PSX` animations give one rotation per bone.
  
  Exports are still per part, not assembled, and replacements must keep the vertex slots.
- The three optional lists (+0x28, +0x2C with 4-short records of vertex indices, +0x30): probably shadow, outline or lighting data. Keeping the vertex slots keeps them working.
- Words A and B of a face in full, and `LECT` to PNG, so exported OBJs can carry their textures.
- Larger replacements: the framework's mod memory (1 MiB at 0x9F000000) could hold bigger meshes once the table and object pointers are rewritten.

## Frontend artwork packs (format 1)

The older format replaces whole `/DATA/FE/*.STR` frontend pictures with same-size, valid BS-v2 MDEC streams (one to eight per pack, up to 32 MiB). They must target your exact disc image (`disc_sha256`). Create one with:

```bat
"%LOCALAPPDATA%\RoadRashJailbreak\toolkits\rr-toolkit-0.6\work\tooling\toolchain\python\python.exe" tools\mod_manager.py --data-root "%LOCALAPPDATA%\RoadRashJailbreak" --operation create --id "author.title-art" --name "My title artwork" --asset "/DATA/FE/RR_LOGO.STR" --replacement "C:\Mods\replacement.str" --output "C:\Mods\my-title-art.rrmod"
```

The installed disc image is never edited: enabled artwork becomes guarded `disc_user` overlays in the runtime's own mod system.
