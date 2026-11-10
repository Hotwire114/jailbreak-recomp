"""Owner-local, data-only mods. No game bytes are shipped.

The input is a .rrmod ZIP with manifest.json and the pack's own files:
  format 1: same-size BS-v2 MDEC frontend artwork (/DATA/FE/*.STR), applied as
            guarded disc_user overlays in the native backend's mod system;
  format 2: any of that artwork plus textures (PNG named by the texture dump),
            sounds (WAV replacing a dumped SPU sample) and models (OBJ
            replacing a dumped DOD3 mesh). The runtime (source/mod_assets.cpp)
            reads runtime/mods/assets/assets.ini, written here from the enabled
            packs. Packs apply in Enhanced only; rider and bike items never
            apply in Five-O.
No executable patches, DLLs, Python plugins or scripts are accepted.
"""
import argparse, configparser, csv, hashlib, json, os, pathlib, re, shutil, struct
import subprocess, sys, tempfile, uuid, zipfile
from disc import Disc

EXE_HASH='52a3e28b2f97e49d8122eb4f510041e64dce046fd8fa9d4cdc49f9e64ff66a7b'
MAX_BYTES=32*1024*1024           # format 1 artwork
MAX_PACK=256*1024*1024           # format 2, expanded
MAX_FILES=4096
MAX_MODS=8
PACKAGE=pathlib.Path(__file__).resolve().parent.parent
# Texture file names are the framework dumper's replacement keys.
TEXTURE_NAME=re.compile(r'tex(?:upload|page)-(?:ST)?(?:P4|P8|C16)-[0-9A-F]{16}(?:-[0-9A-F]{16})?-\d{1,4}x\d{1,3}-\d{1,4}-\d{1,4}-\d{1,4}x\d{1,4}(?:-P\d{1,3}-\d{1,3})?\.png')
TEXTURE_KINDS=['rider','bike','hud','font','menu','world']
SOUND_KINDS=['rider','bike','crash','menu','world']
MODEL_KINDS=['rider','bike','world']
RIDER_KINDS={'rider','bike'}     # never in Five-O (owner's rule: Five-O keeps the game's riders)
class ModError(ValueError):pass
def sha(b):return hashlib.sha256(b).hexdigest()
def file_hash(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def require(condition,message):
    if not condition:raise ModError(message)
def atom(path,data):
    temporary=path.with_name(path.name+'.writing');temporary.write_bytes(data);os.replace(temporary,path)
def safe_name(name):
    p=pathlib.PurePosixPath(name)
    require(isinstance(name,str) and name and '\\' not in name and ':' not in name and not p.is_absolute() and all(n not in ['.','..'] for n in name.split('/')),'Unsafe mod archive path')
    require(all(n and not n.endswith(('.', ' ')) and not re.match(r'(?i)^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\.|$)',n) for n in p.parts),'Unsupported Windows filename')
    return p
def private_path(store,path):
    require(path.resolve().is_relative_to(store),'Mod path escaped the installation')
    current=path
    while current!=store and current.is_relative_to(store):
        require(not current.is_symlink() and not current.is_junction(),'Linked mod paths are unsupported')
        current=current.parent
    return path
def stream_layout(data):
    require(24<=len(data)<=MAX_BYTES,'Unsupported artwork size')
    pos=0;layout=[];pixels=0
    while pos<len(data):
        if not data[pos]:
            require(not any(data[pos:]),'Artwork has an invalid trailing record');break
        require(pos+24<=len(data) and data[pos:pos+4]==b'MDEC','Only BS-v2 frontend MDEC artwork is supported')
        length,w,h=struct.unpack_from('>IHH',data,pos+4)
        qscale,version=struct.unpack_from('<HH',data,pos+20)
        require(24<=length<=len(data)-pos and w and h and w<=512 and h<=512 and w%16==0 and h%16==0 and 1<=qscale<=63 and version==2,'Invalid artwork frame header')
        pixels+=w*h;layout.append((length,w,h));pos+=length
        require(len(layout)<=256 and pixels<=16777216,'Artwork decode budget exceeded')
    require(layout,'Artwork has no frames');return layout
def png_check(b,what):
    require(len(b)>=33 and b[:8]==b'\x89PNG\r\n\x1a\n' and b[12:16]==b'IHDR','Texture is not a PNG image: '+what)
    w,h,depth,colour=struct.unpack_from('>IIBB',b,16)
    require(1<=w<=8192 and 1<=h<=8192 and depth in (1,2,4,8,16) and colour in (0,2,3,4,6),'Unsupported PNG size or format: '+what)
    return w,h
def wav_check(b,what):
    require(len(b)>=44 and b[:4]==b'RIFF' and b[8:12]==b'WAVE','Sound is not a WAV file: '+what)
    pos=12;fmt=None;data=None
    while pos+8<=len(b):
        tag=b[pos:pos+4];size=struct.unpack_from('<I',b,pos+4)[0]
        if tag==b'fmt ' and size>=16:fmt=struct.unpack_from('<HHIIHH',b,pos+8);fmt=(struct.unpack_from('<H',b,pos+32)[0] if fmt[0]==0xFFFE and size>=26 else fmt[0],)+fmt[1:]
        elif tag==b'data':data=min(size,len(b)-pos-8)
        pos+=8+size+(size&1)
    require(fmt and data,'WAV has no fmt or data chunk: '+what)
    tag,channels,rate,_,_,bits=fmt
    require((tag==1 and bits in (8,16,24,32)) or (tag==3 and bits==32),'Use 8/16/24-bit PCM or 32-bit float WAV: '+what)
    require(1<=channels<=8 and 4000<=rate<=192000,'Unsupported WAV channels or rate: '+what)
    require(data/(channels*bits//8)/rate<=600,'Sounds are limited to 10 minutes: '+what)
def identity(store):
    image=private_path(store,store/'game-disc.bin');require(image.is_file(),'Install the original game first')
    receipt=store/'local-build.json';require(receipt.is_file(),'Prepare the local game build first')
    receipt=json.loads(receipt.read_text());actual=file_hash(image)
    require(receipt.get('installed_disc_sha256')==actual and receipt.get('game_executable_sha256')==EXE_HASH,'Installed game identity differs from its local build')
    disc=Disc(image);inventory={e['path']:e for e in disc.inventory()}
    exe=inventory.get('/SLUS_010.53');require(exe and sha(disc.read(exe))==EXE_HASH,'Unsupported game executable')
    return disc,inventory,actual
def hex_hash(v):return isinstance(v,str) and re.fullmatch('[0-9a-f]{64}',v)
def check_artwork(m,disc,inventory,image_hash):
    paths=set();payloads=set()
    require(isinstance(m['assets'],list) and len(m['assets'])<=8,'Mods hold at most eight artwork assets')
    if m['assets']:require(m.get('disc_sha256')==image_hash,'Artwork in this mod is for a different disc image')
    for a in m['assets']:
        require(isinstance(a,dict) and set(a)=={'disc_path','file','size','sha256','expected_sha256'},'Unsupported asset fields; code patches are not accepted')
        require(isinstance(a['disc_path'],str) and re.fullmatch(r'/DATA/FE/[A-Z0-9_]+\.STR',a['disc_path']),'Only frontend .STR artwork replacements are supported')
        e=inventory.get(a['disc_path']);require(e and e['size']==a['size'] and type(a['size']) is int and 24<=a['size']<=MAX_BYTES,'Replacement must match the original file size')
        require(a['disc_path'] not in paths and a['file'] not in payloads,'Duplicate asset path');paths.add(a['disc_path']);payloads.add(a['file']);safe_name(a['file'])
        require(hex_hash(a['sha256']) and hex_hash(a['expected_sha256']),'Invalid asset hash')
        original=disc.read(e);require(sha(original)==a['expected_sha256'],'Original artwork guard does not match');stream_layout(original)
    require(sum(a['size'] for a in m['assets'])<=MAX_BYTES,'Expanded artwork exceeds 32 MiB')
def check_items(m):
    """Format 2 textures, sounds and models: names, kinds and bounds."""
    seen=set();textures=set();samples=set();meshes=set()
    for t in m['textures']:
        require(isinstance(t,dict) and set(t)=={'file','category','sha256'} and t['category'] in TEXTURE_KINDS and hex_hash(t['sha256']),'Invalid texture entry')
        p=safe_name(t['file']);require(len(p.parts)==3 and p.parts[0]=='textures' and p.parts[1]==t['category'] and TEXTURE_NAME.fullmatch(p.name),'Textures go in textures/<kind>/ with their dumped file name: '+t['file'])
        require(p.name not in textures,'The same texture is replaced twice: '+p.name);textures.add(p.name);seen.add(t['file'].casefold())
    for s in m['sounds']:
        require(isinstance(s,dict) and {'file','sample','category','rate','loop','sha256'}<=set(s)<={'file','sample','category','rate','loop','volume','sha256'},'Invalid sound entry')
        require(s['category'] in SOUND_KINDS and isinstance(s['sample'],str) and re.fullmatch('[0-9a-f]{8}',s['sample']) and type(s['rate']) is int and 1000<=s['rate']<=176400 and type(s['loop']) is bool and hex_hash(s['sha256']),'Invalid sound entry')
        require(type(s.get('volume',100)) is int and 1<=s.get('volume',100)<=400,'Sound volume is a percentage from 1 to 400')
        p=safe_name(s['file']);require(len(p.parts)==3 and p.parts[0]=='sounds' and p.parts[1]==s['category'] and p.suffix.lower()=='.wav','Sounds go in sounds/<kind>/ as WAV: '+s['file'])
        require(s['sample'] not in samples,'The same sound is replaced twice: '+s['sample']);samples.add(s['sample']);seen.add(s['file'].casefold())
    for d in m['models']:
        require(isinstance(d,dict) and set(d)=={'file','mesh','category','sha256'} and d['category'] in MODEL_KINDS and hex_hash(d['sha256']),'Invalid model entry')
        require(isinstance(d['mesh'],str) and re.fullmatch('[0-9a-f]{8}',d['mesh']),'Invalid model mesh id')
        p=safe_name(d['file']);require(len(p.parts)==3 and p.parts[0]=='models' and p.parts[1]==d['category'] and p.suffix.lower()=='.obj','Models go in models/<kind>/ as OBJ: '+d['file'])
        require(d['mesh'] not in meshes,'The same model is replaced twice: '+d['mesh']);meshes.add(d['mesh']);seen.add(d['file'].casefold())
    require(len(seen)==len(m['textures'])+len(m['sounds'])+len(m['models']),'Duplicate file in mod')
def manifest_check(m,disc,inventory,image_hash):
    require(isinstance(m,dict) and m.get('format') in (1,2),'Unsupported mod format')
    if m['format']==1:
        require(set(m)=={'format','id','version','name','game_executable_sha256','disc_sha256','assets'},'Unsupported mod manifest fields')
        require(isinstance(m['assets'],list) and m['assets'],'Mods need one to eight artwork assets')
        m={**m,'textures':[],'sounds':[],'models':[]}
    else:
        require({'format','id','version','name','game_executable_sha256'}<=set(m)<={'format','id','version','name','author','description','game_executable_sha256','disc_sha256','assets','textures','sounds','models'},'Unsupported mod manifest fields')
        m={'assets':[],'textures':[],'sounds':[],'models':[],**m}
        require(all(isinstance(m[k],list) for k in ['assets','textures','sounds','models']),'Invalid mod contents')
        require(m['assets'] or m['textures'] or m['sounds'] or m['models'],'This mod replaces nothing')
        require(len(m['textures'])+len(m['sounds'])+len(m['models'])+len(m['assets'])<=MAX_FILES,'Mod has too many files')
        for k in ['author','description']:require(isinstance(m.get(k,''),str) and len(m.get(k,''))<=400,'Invalid '+k)
    require(m['game_executable_sha256']==EXE_HASH,'Mod is for a different game')
    require(isinstance(m['id'],str) and re.fullmatch(r'[a-z][a-z0-9.-]{1,47}',m['id']),'Invalid mod identifier')
    require(isinstance(m['version'],str) and re.fullmatch(r'[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}',m['version']),'Invalid mod version')
    require(isinstance(m['name'],str) and 1<=len(m['name'])<=60 and not any(ord(c)<32 or c in '[]=;' for c in m['name']),'Invalid mod display name')
    check_artwork(m,disc,inventory,image_hash);check_items(m)
    return m
def declared_files(m):return [a['file'] for a in m['assets']]+[x['file'] for k in ['textures','sounds','models'] for x in m[k]]
def payload_check(m,folder,store,disc,inventory):
    for a in m['assets']:
        path=private_path(store,folder/a['file']);require(path.is_file() and path.stat().st_size==a['size'],'Missing or changed mod artwork')
        b=path.read_bytes();require(sha(b)==a['sha256'],'Mod artwork hash mismatch')
        require(stream_layout(b)==stream_layout(disc.read(inventory[a['disc_path']])),'Frame sizes and dimensions must match the original artwork')
        result=subprocess.run([str(PACKAGE/'RoadRashJailbreak.exe'),'--validate-mdec-file',str(path)],timeout=30)
        require(result.returncode==0,'Artwork contains an invalid MDEC coefficient stream')
    total=0
    for kind,check in [('textures',png_check),('sounds',wav_check),('models',obj_check)]:
        for x in m[kind]:
            path=private_path(store,folder/x['file']);require(path.is_file(),'Missing mod file: '+x['file'])
            b=path.read_bytes();total+=len(b);require(sha(b)==x['sha256'],'Mod file changed since it was installed: '+x['file'])
            check(b,x['file'])
    require(total<=MAX_PACK,'Mod exceeds 256 MiB')
def obj_check(b,what):
    """A replacement mesh: OBJ text with v / vt / f lines. The original's budgets are
    checked where the disc is read (models.check, in build_pack and sync)."""
    import models
    try:models.parse_obj(b.decode('utf-8','replace'),what)
    except models.ModelError as e:raise ModError(str(e))
def model_replacement(index,x,b):
    """The runtime's replacement mesh for one pack model, checked against the owner's disc."""
    import models
    lod=index.get(int(x['mesh'],16));require(lod,f'Model {x["mesh"]} is not on this disc; export it first (Mods > Dump for modding)')
    try:return models.check(lod,b.decode('utf-8','replace'),x['file'])
    except models.ModelError as e:raise ModError(str(e))
def load_library(store,disc,inventory,image_hash):
    library=store/'mods/library';library.mkdir(parents=True,exist_ok=True);packages={}
    for path in sorted(library.glob('*/*/manifest.json')):
        private_path(store,path);m=manifest_check(json.loads(path.read_text()),disc,inventory,image_hash)
        require(path.parent.name==m['version'] and path.parent.parent.name==m['id'],'Mod directory identity mismatch')
        require(m['id'] not in packages,'Multiple installed versions; remove the old version first')
        packages[m['id']]=(m,path.parent)
    require(len(packages)<=MAX_MODS,f'Up to {MAX_MODS} mods can be installed');return packages
def selected_state(store):
    path=store/'mods/selections.json'
    if not path.exists():return {}
    state=json.loads(path.read_text());require(isinstance(state,dict) and all(type(v) is bool for v in state.values()),'Invalid mod selection state');return state
def dump_enabled(store):
    path=store/'mods/dump.json'
    return path.exists() and json.loads(path.read_text()).get('on') is True
def native_manifest(m,inventory):
    q=lambda v:json.dumps(v,ensure_ascii=False)
    text=f'format_version = 2\nid = {q("rr.asset."+m["id"])}\nversion = {q(m["version"])}\nname = {q(m["name"])}\nresolver = "declarative"\n[[target]]\ngame_id = "SLUS-01053"\nexe_sha256 = {q(EXE_HASH)}\ndisc_sha256 = {q(m["disc_sha256"])}\n[[feature]]\nid = "artwork"\nname = {q(m["name"])}\ndefault_enabled = false\n'
    for a in m['assets']:
        text+=f'[[overlay]]\nfeature = "artwork"\ntarget = "disc_user"\noffset = {inventory[a["disc_path"]]["extent_lba"]*2048}\nfile = {q(a["file"])}\nsha256 = {q(a["sha256"])}\nexpected_sha256 = {q(a["expected_sha256"])}\n'
    return text
def place(source,dest):
    """Hard link (instant, no extra disk) or copy a validated pack file."""
    dest.parent.mkdir(parents=True,exist_ok=True)
    try:os.link(source,dest)
    except OSError:shutil.copy2(source,dest)
def build_assets(store,stage,active,dump,disc):
    """runtime/mods/assets: merged textures, sounds and models, plus assets.ini
    (paths relative to the runtime folder) for source/mod_assets.cpp."""
    ini=configparser.ConfigParser(interpolation=None);ini.optionxform=str
    rider_textures=any(t['category'] in RIDER_KINDS for m,f in active for t in m['textures'])
    textures=[(t,f) for m,f in active for t in m['textures']]
    if textures:
        for t,folder in textures:
            name=pathlib.PurePosixPath(t['file']).name
            place(folder/t['file'],stage/'textures/all/replacements'/name)
            if rider_textures and t['category'] not in RIDER_KINDS:place(folder/t['file'],stage/'textures/safe/replacements'/name)
        if rider_textures:(stage/'textures/safe/replacements').mkdir(parents=True,exist_ok=True)
        ini['textures']={'all':r'mods\assets\textures\all','safe':r'mods\assets\textures\safe' if rider_textures else ''}
    sounds=[(m,s,f) for m,f in active for s in m['sounds']]
    ini['sounds']={'count':str(len(sounds))}
    for i,(m,s,folder) in enumerate(sounds):
        rel=pathlib.PurePosixPath(m['id'])/s['file'];place(folder/s['file'],stage/'sounds'/rel)
        ini[f'sound{i}']={'sample':s['sample'],'file':'mods\\assets\\sounds\\'+str(rel).replace('/','\\'),'rate':str(s['rate']),'loop':str(int(s['loop'])),
                          'rider':str(int(s['category'] in RIDER_KINDS)),'volume':str(s.get('volume',100))}
    models=[(m,d,f) for m,f in active for d in m['models']]
    ini['models']={'count':str(len(models))}
    if models:
        import models as model_format
        index=model_format.disc_models(disc)
        for i,(m,d,folder) in enumerate(models):
            out=stage/'models'/m['id']/(d['mesh']+'.rrmesh');out.parent.mkdir(parents=True,exist_ok=True)
            out.write_bytes(model_replacement(index,d,(folder/d['file']).read_bytes()))
            ini[f'model{i}']={'mesh':d['mesh'],'file':f'mods\\assets\\models\\{m["id"]}\\{d["mesh"]}.rrmesh','rider':str(int(d['category'] in RIDER_KINDS))}
    ini['dump']={'textures':r'..\mods\texture-dump' if dump else '','sounds':r'..\mods\sound-dump' if dump else '','models':r'..\mods\model-dump' if dump else ''}
    if dump:
        for d in ['texture-dump','sound-dump']:(store/'mods'/d).mkdir(parents=True,exist_ok=True)
        if not (store/'mods/model-dump/index.csv').is_file():   # models come straight from the disc, once
            import models as model_format
            model_format.export(disc,store/'mods/model-dump')
    from io import StringIO
    out=StringIO();ini.write(out);(stage/'assets.ini').write_bytes(out.getvalue().encode('utf-16'))   # UTF-16 with BOM: GetPrivateProfileStringW reads any path
    return len(textures),len(sounds),len(models)
def swap(store,mods,name,stage):
    """Replace runtime/mods/<name> with the staged folder, keeping the old one until it succeeded."""
    target=private_path(store,mods/name);previous=mods/(name+'-old-'+uuid.uuid4().hex)
    if target.exists():target.rename(previous)
    try:stage.rename(target)
    except BaseException:
        if target.exists():shutil.rmtree(private_path(store,target))
        if previous.exists():previous.rename(target)
        raise
    if previous.exists():shutil.rmtree(private_path(store,previous))
def sync(store,packages,state,disc,inventory):
    mods=private_path(store,store/'runtime/mods');mods.mkdir(parents=True,exist_ok=True)
    installed=private_path(store,mods/'installed')
    if installed.exists():
        require(all(p.name.startswith('rr.asset.') and not p.is_symlink() and not p.is_junction() for p in installed.iterdir()),'Unmanaged runtime mod found; keep it elsewhere before using this manager')
    active=[v for k,v in packages.items() if state.get(k,False)];claimed=set();keys={}
    for m,folder in active:
        payload_check(m,folder,store,disc,inventory)
        for a in m['assets']:
            require(a['disc_path'] not in claimed,'Two enabled mods replace the same artwork; switch one off');claimed.add(a['disc_path'])
        for kind,key in [('textures',lambda x:pathlib.PurePosixPath(x['file']).name),('sounds',lambda x:x['sample']),('models',lambda x:x['mesh'])]:
            for x in m[kind]:
                other=keys.setdefault((kind,key(x)),m)
                require(other is m,f'"{other["name"]}" and "{m["name"]}" replace the same {kind[:-1]}; switch one off')
    dump=dump_enabled(store)
    # Publish only validated active packages. Disabled payloads cannot become
    # runtime operations. Original images and all saves remain read-only here.
    stage=mods/('managed-next-'+uuid.uuid4().hex);stage.mkdir()
    assets=mods/('assets-next-'+uuid.uuid4().hex);assets.mkdir()
    text='format_version = 2\n'
    try:
        for m,folder in active:
            if not m['assets']:continue
            dest=stage/('rr.asset.'+m['id'])/m['version'];dest.mkdir(parents=True)
            for a in m['assets']:
                out=dest/a['file'];out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(folder/a['file'],out)
            (dest/'manifest.toml').write_text(native_manifest(m,inventory),encoding='utf-8')
            text+=f'[[package]]\nid = "rr.asset.{m["id"]}"\nversion = "{m["version"]}"\n[[feature]]\npackage_id = "rr.asset.{m["id"]}"\nid = "artwork"\nenabled = true\n'
        counts=build_assets(store,assets,active,dump,disc)
        # Preserve selections of unrelated bundled features.
        old_state=mods/'state.toml'
        if old_state.exists():
            import tomllib
            old=tomllib.loads(old_state.read_text(encoding='utf-8-sig'))
            for kind in ['package','feature']:
                for item in old.get(kind,[]):
                    if str(item.get('package_id',item.get('id',''))).startswith('rr.asset.'):continue
                    text+='[['+kind+']]\n'
                    for key,value in item.items():
                        require(re.fullmatch('[a-z_]+',key),'Unsupported existing state key')
                        if isinstance(value,dict):text+=key+' = { '+', '.join(json.dumps(k)+' = '+json.dumps(v) for k,v in value.items())+' }\n'
                        elif isinstance(value,bool):text+=key+' = '+str(value).lower()+'\n'
                        else:text+=key+' = '+json.dumps(value)+'\n'
        swap(store,mods,'installed',stage);swap(store,mods,'assets',assets);atom(old_state,text.encode('utf-8'))
    finally:
        for d in [stage,assets]:
            if d.exists():shutil.rmtree(private_path(store,d))
    atom(store/'mods/selections.json',(json.dumps(state,indent=2)+'\n').encode())
    catalog=configparser.ConfigParser(interpolation=None);catalog['catalog']={'count':str(len(packages)),'dump':str(int(dump))}
    for i,(key,(m,folder)) in enumerate(packages.items()):catalog['mod'+str(i)]={'id':key,'name':m['name'],'enabled':str(int(state.get(key,False))),'contents':contents(m)}
    from io import StringIO
    out=StringIO();catalog.write(out);atom(store/'mods/catalog.ini',out.getvalue().encode('utf-16'))
    atom(store/'mods/active-plan.json',(json.dumps({'schema':2,'active':[m['id'] for m,folder in active],'assets':sorted(claimed),'textures':counts[0],'sounds':counts[1],'models':counts[2],'dump':dump,'game_code_patches':False},indent=2)+'\n').encode())
    return len(active)
def contents(m):
    parts=[(len(m['assets']),'artwork'),(len(m['textures']),'textures'),(len(m['sounds']),'sounds'),(len(m['models']),'models')]
    return ', '.join(f'{n} {what}' for n,what in parts if n)
def install(archive,store,disc,inventory,image_hash,packages):
    require(archive.is_file() and archive.stat().st_size<=MAX_PACK+MAX_BYTES,'Mod archive is missing or too large')
    staging=store/'mods'/('import-'+uuid.uuid4().hex);staging.mkdir()
    try:
        with zipfile.ZipFile(archive) as z:
            entries=z.infolist();names=set();total=0
            require(1<=len(entries)<=MAX_FILES+1,'Mod archive has too many files')
            for e in entries:
                safe_name(e.filename);require(not e.is_dir() and not e.flag_bits&1 and e.compress_type in [zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED],'Unsupported archive entry')
                require((e.external_attr>>16)&0o170000 in [0,0o100000],'Archive links/devices are unsupported')
                require(e.filename.casefold() not in names,'Duplicate archive filename');names.add(e.filename.casefold());total+=e.file_size
            require(total<=MAX_PACK+MAX_BYTES and 'manifest.json' in names,'Missing manifest or oversized expanded archive')
            require(z.getinfo('manifest.json').file_size<=4*1024*1024,'Oversized manifest')
            m=manifest_check(json.loads(z.read('manifest.json')),disc,inventory,image_hash)
            require(m['id'] not in packages and len(packages)<MAX_MODS,f'Mod already installed, or the {MAX_MODS}-mod limit was reached')
            require(names=={'manifest.json',*(f.casefold() for f in declared_files(m))},'Archive contains files outside its declared contents')
            for e in entries:
                out=staging/e.filename;out.parent.mkdir(parents=True,exist_ok=True)
                with z.open(e) as src,out.open('wb') as dst:shutil.copyfileobj(src,dst)
            payload_check(m,staging,store,disc,inventory)
        dest=private_path(store,store/'mods/library'/m['id']/m['version']);dest.parent.mkdir(parents=True,exist_ok=True);require(not dest.exists(),'Mod version already exists');staging.rename(dest)
        packages[m['id']]=(m,dest);return m['id']
    finally:
        if staging.exists():shutil.rmtree(private_path(store,staging))
def sound_index(store,source):
    """Rates and loops of dumped samples: the source folder's own index.csv, then the dump's."""
    rows={}
    for path in [store/'mods/sound-dump/index.csv',source/'sounds/index.csv']:
        if path.is_file():
            with path.open(newline='') as f:
                for r in csv.DictReader(f):rows[r['sample'].lower()]=r
    return rows
def build_pack(a,store,image_hash,disc):
    """A format 2 .rrmod from a folder laid out like the dumps:
    textures/<kind>/<dumped name>.png, sounds/<kind>/<sample>.wav, models/<kind>/<mesh>.obj."""
    source=a.source.resolve();require(source.is_dir(),'The source folder does not exist');index=None
    m={'format':2,'id':a.id,'version':a.version,'name':a.name,'game_executable_sha256':EXE_HASH,'textures':[],'sounds':[],'models':[]}
    files={};rates=sound_index(store,source)
    for kind in TEXTURE_KINDS:
        for p in sorted((source/'textures'/kind).glob('*.png')) if (source/'textures'/kind).is_dir() else []:
            b=p.read_bytes();png_check(b,p.name);rel=f'textures/{kind}/{p.name}';files[rel]=b;m['textures'].append({'file':rel,'category':kind,'sha256':sha(b)})
    for kind in SOUND_KINDS:
        for p in sorted((source/'sounds'/kind).glob('*.wav')) if (source/'sounds'/kind).is_dir() else []:
            sample=p.stem.lower();require(re.fullmatch('[0-9a-f]{8}',sample),f'Name each sound after the dumped sample it replaces (8 hex digits): {p.name}')
            row=rates.get(sample);require(row,f'Sound {sample} is not in the sound dump index; dump it first (Mods > Dump for modding)')
            b=p.read_bytes();wav_check(b,p.name);rel=f'sounds/{kind}/{sample}.wav';files[rel]=b
            m['sounds'].append({'file':rel,'sample':sample,'category':kind,'rate':int(row['rate']),'loop':row['loop']=='1','sha256':sha(b)})
    for kind in MODEL_KINDS:
        for p in sorted((source/'models'/kind).glob('*.obj')) if (source/'models'/kind).is_dir() else []:
            mesh=p.stem.lower()[-8:];require(re.fullmatch('[0-9a-f]{8}',mesh),f'Keep the exported name, which ends in the mesh id (8 hex digits): {p.name}')
            if index is None:
                import models as model_format
                index=model_format.disc_models(disc)
            b=p.read_bytes();obj_check(b,p.name);rel=f'models/{kind}/{mesh}.obj';files[rel]=b
            x={'file':rel,'mesh':mesh,'category':kind,'sha256':sha(b)};model_replacement(index,x,b);m['models'].append(x)
    require(files,'The source folder has no textures/, sounds/ or models/ files in the expected layout')
    m=manifest_check(m,None,{},image_hash)
    require(not a.output.exists(),'Output already exists');a.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(a.output,'x',zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json',json.dumps({k:v for k,v in m.items() if v!=[] or k in ('textures','sounds','models')},indent=2)+'\n')
        for rel,b in files.items():z.writestr(rel,b)
    return f'Mod pack created: {len(m["textures"])} textures, {len(m["sounds"])} sounds, {len(m["models"])} models. Install it from the Mods page.'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--data-root',type=pathlib.Path,required=True);p.add_argument('--operation',choices=['install','toggle','remove','sync','create','pack','dump'],required=True)
    p.add_argument('--archive',type=pathlib.Path);p.add_argument('--id');p.add_argument('--asset');p.add_argument('--replacement',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);p.add_argument('--name');p.add_argument('--version',default='1.0.0')
    p.add_argument('--source',type=pathlib.Path);a=p.parse_args()
    store=a.data_root.resolve();(store/'mods').mkdir(parents=True,exist_ok=True);disc=None
    try:
        private_path(store,store/'mods');disc,inventory,image_hash=identity(store);packages=load_library(store,disc,inventory,image_hash);state=selected_state(store)
        if a.operation=='create':
            require(a.id and a.name and a.asset and a.replacement and a.output,'Creating a mod requires id, name, asset, replacement and output')
            e=inventory.get(a.asset);require(e is not None,'Unknown disc asset');require(a.replacement.is_file() and a.replacement.stat().st_size<=MAX_BYTES,'Replacement artwork is missing or too large');b=a.replacement.read_bytes()
            m={'format':1,'id':a.id,'version':a.version,'name':a.name,'game_executable_sha256':EXE_HASH,'disc_sha256':image_hash,'assets':[{'disc_path':a.asset,'file':'artwork/replacement.str','size':len(b),'sha256':sha(b),'expected_sha256':sha(disc.read(e))}]}
            manifest_check(m,disc,inventory,image_hash)
            with tempfile.TemporaryDirectory(dir=store/'mods') as temp:
                folder=pathlib.Path(temp);(folder/'artwork').mkdir();(folder/'artwork/replacement.str').write_bytes(b);payload_check({**m,'textures':[],'sounds':[],'models':[]},folder,store,disc,inventory)
            require(not a.output.exists(),'Output already exists');a.output.parent.mkdir(parents=True,exist_ok=True)
            with zipfile.ZipFile(a.output,'x',zipfile.ZIP_DEFLATED) as z:z.writestr('manifest.json',json.dumps(m,indent=2)+'\n');z.writestr('artwork/replacement.str',b)
            message='Artwork mod created. Install it from the Mods page.'
        elif a.operation=='pack':
            require(a.id and a.name and a.source and a.output,'Packing a mod requires id, name, source and output')
            message=build_pack(a,store,image_hash,disc)
        else:
            rollback=None
            if a.operation=='install':state[install(a.archive,store,disc,inventory,image_hash,packages)]=False
            if a.operation=='dump':atom(store/'mods/dump.json',(json.dumps({'on':not dump_enabled(store)})+'\n').encode())
            if a.operation in ['toggle','remove']:
                require(a.id in packages,'Mod is not installed')
                if a.operation=='toggle':state[a.id]=not state.get(a.id,False)
                else:
                    m,folder=packages.pop(a.id);removed=store/'mods/removed';removed.mkdir(exist_ok=True);dest=removed/(a.id+'-'+uuid.uuid4().hex);folder.rename(dest);rollback=(dest,folder);state.pop(a.id,None)
            try:count=sync(store,packages,state,disc,inventory)
            except BaseException:
                if rollback:rollback[0].rename(rollback[1])
                raise
            message=f'MODS READY - {count} ENABLED{" - DUMPING ON" if dump_enabled(store) else ""}. CHANGES APPLY ON NEXT LAUNCH.'
        (store/'mods/status.txt').write_text(message,encoding='utf-8');print(message);return 0
    except Exception as error:
        message='MOD NOT APPLIED: '+str(error);(store/'mods/status.txt').write_text(message[:230],encoding='utf-8');print(message);return 1
    finally:
        if disc:disc.close()
if __name__=='__main__':sys.exit(main())
