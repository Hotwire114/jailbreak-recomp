"""Create a private native build from game data installed by its owner.

The distributed launcher has no game bytes, translated functions or backend
binary. This script only produces those inside the user's local installation.
The already compiled developer cache may be adopted after input/build checks.
"""
import argparse, hashlib, json, os, pathlib, re, shutil, struct, subprocess, sys, threading, time
from disc import Disc, DiscError

EXE_HASH='52a3e28b2f97e49d8122eb4f510041e64dce046fd8fa9d4cdc49f9e64ff66a7b'
FRAMEWORK_REVISION='7b3b1ea2869721d9d1fe546778952db3820c077d'
BASELINE_RUNTIME_HASH='75d268024baa9acdf85c61d7102f0578dcbd4a244e0216f4ef233aeff5bb1841'
BASELINE_DISC_HASH='6fb1e22333f9243e0fb62c6c96975255a985cc75fac89772c528214575ca0806'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--data-root',type=pathlib.Path,required=True)
parser.add_argument('--workspace',type=pathlib.Path,required=True)
parser.add_argument('--force-compile',action='store_true')
# The online game's libraries (recomp-net, rbengine), unpacked and verified by setup_game.py from
# recipe/netplay.json. Without it only the offline game is built (online stays greyed out).
parser.add_argument('--netplay-libs',type=pathlib.Path)
args=parser.parse_args();store=args.data_root.resolve();workspace=args.workspace.resolve()
package=pathlib.Path(__file__).resolve().parent.parent
store.mkdir(parents=True,exist_ok=True)
log=(store/'build.log').open('w',encoding='utf-8',buffering=1)
def say(message):log.write(message+'\n');print(message,flush=True)
def status(message):
    say(message)
    (store/'build-status.txt').write_text(message,encoding='utf-8')
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def toml_path(path):return json.dumps(path.resolve().as_posix())
def run(command,cwd=None,env=None,out=None):
    say('Running '+pathlib.Path(str(command[0])).name)
    result=subprocess.run([str(x) for x in command],cwd=cwd,env=env,stdout=out or log,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError('Local build failed; see '+pathlib.Path((out or log).name).name+'.')
def build_jobs():
    """Parallel compile and link jobs: one per CPU thread (one left for the PC), with 0.75 GB of
    available memory each (eight jobs measured about 4 GB in all), never fewer than two. The
    game is the same either way."""
    import ctypes
    class Memory(ctypes.Structure):
        _fields_=[('length',ctypes.c_ulong),('load',ctypes.c_ulong)]+[(n,ctypes.c_ulonglong) for n in ('total','available','page_total','page_available','virtual_total','virtual_available','extended')]
    m=Memory();m.length=ctypes.sizeof(m)
    memory_jobs=int(m.available/(0.75*2**30)) if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)) else 2
    return max(2,min((os.cpu_count() or 2)-1,memory_jobs))
JOBS=int(os.environ.get('RR_BUILD_JOBS') or build_jobs())
# The offline and online games build at the same time, each with half the jobs: a few huge
# game functions take one job about 8 minutes whatever else runs, so one build alone leaves
# most jobs idle for its second half.
EACH=max(1,(JOBS+1)//2)
def build_target(toolbin,build,env,jobs,out=None):
    """Ninja resumes where it stopped, so a step that failed for an outside reason (antivirus
    holding a tool for a moment: "cmake.exe is not recognized") gets one more try."""
    command=[toolbin/'cmake.exe','--build',build,'--target','rr-game','-j',jobs]
    try:run(command,env=env,out=out)
    except RuntimeError:say('Build stopped; trying once more');time.sleep(5);run(command,env=env,out=out)
def required(path):
    if not path.exists():raise RuntimeError('Missing local build dependency: '+str(path)+'. See README.md.')
    return path
def pe_dependencies(file):
    """Read imported DLL names; executable data is never executed by this parser."""
    b=file.read_bytes()
    if len(b)<64 or b[:2]!=b'MZ':return []
    pe=struct.unpack_from('<I',b,60)[0]
    if b[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid local PE image')
    count=struct.unpack_from('<H',b,pe+6)[0];optional=struct.unpack_from('<H',b,pe+20)[0];opt=pe+24
    magic=struct.unpack_from('<H',b,opt)[0];directory=opt+(112 if magic==0x20b else 96)
    imports=struct.unpack_from('<I',b,directory+8)[0];sections=[]
    for n in range(count):
        p=opt+optional+40*n;vsize,rva,size,offset=struct.unpack_from('<4I',b,p+8);sections.append((rva,max(vsize,size),offset))
    def address(rva):
        for base,size,off in sections:
            if base<=rva<base+size:return off+rva-base
        raise ValueError('Invalid import RVA')
    if not imports:return []
    p=address(imports);names=[]
    while p+20<=len(b):
        row=struct.unpack_from('<5I',b,p);p+=20
        if not any(row):break
        pos=address(row[3]);end=b.find(b'\0',pos,pos+256)
        if end<0:raise ValueError('Invalid DLL name')
        names.append(b[pos:end].decode('ascii'))
    return names
def stage_runtime(build,compiler,framework,folder='runtime',out=None):
    runtime=store/folder;runtime.mkdir(exist_ok=True)
    exe=required(build/'RoadRashNativeResearch.exe');shutil.copy2(exe,runtime/exe.name)
    manifest=required(build/'RoadRashNativeResearch.execution.json')
    run([sys.executable,framework/'tools/release_stage.py','stage-execution','--binary',runtime/exe.name,'--manifest',manifest],out=out)
    for folder in ['bios','mods/bundled']:
        if (build/folder).exists():shutil.copytree(build/folder,runtime/folder,dirs_exist_ok=True)
    queue=[runtime/exe.name];seen=set()
    while queue:
        current=queue.pop()
        for name in pe_dependencies(current):
            key=name.lower()
            if key in seen:continue
            seen.add(key)
            source=next((p for p in [build/name,compiler/name] if p.is_file()),None)
            if source:
                shutil.copy2(source,runtime/name);queue.append(runtime/name)
    return runtime/exe.name
def compile_local(exe,image,image_hash,toolbin,framework,recompiler,bios):
    """Generate every game instruction from installed owner-supplied bytes."""
    status('Generating native game code from your installed files')
    private=store/'compile';stage=private/'stage2';stage.mkdir(parents=True,exist_ok=True)
    (stage/'bios').mkdir(exist_ok=True)
    shutil.copy2(framework/'bios/SCPH1001.toml',stage/'bios/SCPH1001.toml')
    shutil.copy2(package/'recipe/seeds.txt',private/'seeds.txt')
    config=f'''[game]
name="Road Rash Jailbreak Local Build"
id="SLUS-01053"
exe={toml_path(exe)}
disc={toml_path(image)}
load_address="0x80010000"
entry_pc="0x8004032C"
text_size="0x4B800"
stack_base="0x801FFFF0"
[recompiler]
bios_config={toml_path(framework/'bios/SCPH1001.toml')}
seeds={toml_path(private/'seeds.txt')}
strict=true
out_dir={toml_path(private/'framework-generated')}
[runtime]
bios_hle=false
openbios=true
[video]
renderer="opengl"
'''
    game=stage/'game.toml';game.write_text(config,encoding='utf-8')
    env=os.environ.copy();env['PATH']=str(toolbin)+os.pathsep+env.get('PATH','')
    # Reproducible builds: a fixed compile date (the 1.0 release, 18 Oct 2026) for
    # __DATE__/__TIME__ (the framework's crash-log build stamp), so every player's
    # build of the same disc is byte-identical.
    env['SOURCE_DATE_EPOCH']='1792281600'
    run([recompiler,'--config',game,'--project-root',framework],env=env)
    # Source views describe SDK transformations produced by native OpenBIOS.
    # All expected game words are read from the installed EXE at build time.
    original=exe.read_bytes();modified=bytearray(original)
    edits=[(0x8004502c,0),(0x80045030,0),(0x80045034,0x10000000),(0x80045038,0),(0x80045040,0),(0x800451a4,0),(0x800451ac,0x10000009),(0x800451b0,0)]
    manifest=f'''format_version=2
id="rr.native-openbios-source-view"
version="0.1.0"
name="Native OpenBIOS SDK source view"
author="Road Rash recomp investigation"
resolver="declarative"
[[target]]
game_id="SLUS-01053"
disc_sha256="{image_hash}"
exe_sha256="{EXE_HASH}"
[[feature]]
id="native-bios-transform"
name="Observed native BIOS transform"
default_enabled=true
'''
    for pc,word in edits:
        offset=2048+pc-0x80010000;expected=original[offset:offset+4];replacement=struct.pack('<I',word);modified[offset:offset+4]=replacement
        manifest+=f'[[patch]]\nfeature="native-bios-transform"\ntarget="main_exe"\naddress="0x{pc:08X}"\nexpected="{expected.hex()}"\nreplace="{replacement.hex()}"\n'
    (stage/'sdk-source-view.toml').write_text(manifest,encoding='utf-8')
    profile=json.loads((package/'recipe/overlays.json').read_text());profile['disc_hashes']['sha256']=image_hash
    profile['mod_packages']=[{'name':'openbios-sdk','manifest':'sdk-source-view.toml','manifest_sha256':hashlib.sha256(manifest.encode()).hexdigest(),'id':'rr.native-openbios-source-view','version':'0.1.0','features':{'native-bios-transform':{}},'plugins':[]}]
    extents=[]
    for pc,size,entries in [(0x80045014,68,[0x80045014,0x8004502c,0x80045038,0x8004503c,0x80045044]),(0x80045184,112,[0x80045184,0x80045194,0x800451a4,0x800451b4,0x800451c0,0x800451d4,0x800451d8,0x800451e0])]:
        off=2048+pc-0x80010000;body=modified[off:off+size]
        extents.append({'file':'SLUS_010.53','file_offset':'0x800','base':'0x80010000','address':hex(pc),'size':hex(size),'load_addr':hex(pc),'sha256':hashlib.sha256(body).hexdigest(),'entries':[hex(n) for n in entries],'allow_missing':True})
    profile['images'].append({'method':'fixed_address_extents','mod_package':'openbios-sdk','extents':extents});profile['expected_records']=5
    profile_path=stage/'overlays.json';profile_path.write_text(json.dumps(profile,indent=2)+'\n')
    temp=store/'build-temp';temp.mkdir(exist_ok=True)
    env['TMPDIR']=str(temp);env['TEMP']=str(temp);env['TMP']=str(temp)
    status('Preparing native race overlays from your installed files')
    run([sys.executable,framework/'tools/aot_overlay_pipeline.py','static','--profile',profile_path,'--game-toml',game,'--recompiler',recompiler,'--work-dir',stage/'aot-build','--gcc',toolbin/'clang.exe','--workers','2','--out-dir',stage/'overlays','--cmake',toolbin/'cmake.exe'],env=env)
    status('Preparing native boot routines and strict execution checks')
    run([sys.executable,package/'tools/prepare_kernel_native.py','--framework',framework,'--recompiler',recompiler,'--bios',bios,'--game-exe',exe,'--output',stage/'kernel-native'],env=env)
    run([sys.executable,package/'tools/prepare_native_guard.py','--framework',framework,'--output',stage/'guard'],env=env)
    source=store/'runtime-project';source.mkdir(exist_ok=True)
    shutil.copy2(package/'source/runtime-CMakeLists.txt',source/'CMakeLists.txt')
    for name in ['native_guard.c','native_kernel.c','rider_enhancements.cpp','rider_enhancements.h','rider_menu.cpp','pc_services.cpp','pc_services.h','discord_presence.cpp','discord_presence.h','custom_music.cpp','custom_music.h','native_ui.cpp','native_ui.h','taunt_voices.cpp','taunt_voices.h','mod_assets.cpp','mod_assets.h','quick_resume.cpp','quick_resume.h','menu_titles.cpp','controls_screen.cpp','controls_screen.h','netplay_session.cpp','netplay_session.h','game_editor.cpp','game_editor.h','hud_editor.cpp','gameplay_editor.cpp','demo_courses.cpp','demo_courses.h','editor_page.h','course_names.h','owner_assets.c','owner_assets.h','rider_model.h','showroom.cpp','showroom.h','rider_takeover.cpp','mouse_menus.cpp','mouse_menus.h','online_screen.cpp','online_screen.h','online_link.h','menu_canvas.h','disc_reader.h','mdec_image.h','mpeg_tables.h','hd_font_data.h','model_detail.h','achievements.h','gfx_options.h','cheat_codes.h','cheats_runtime.h','game_icon.h']:shutil.copy2(package/'source'/name,source/name)
    shutil.copy2(package/'tools/music_mixer.py',source/'music_mixer.py')
    shutil.copy2(package/'tools/spu_events.py',source/'spu_events.py')
    shutil.copy2(package/'tools/gpu_presentation.py',source/'gpu_presentation.py')
    shutil.copy2(package/'tools/gpu_geometry.py',source/'gpu_geometry.py')
    shutil.copy2(package/'tools/race_timing.py',source/'race_timing.py')
    shutil.copy2(package/'tools/netplay_prep.py',source/'netplay_prep.py')
    (source/'netplay-shim').mkdir(exist_ok=True);shutil.copy2(package/'source/netplay-shim/recomp_netplay_account.h',source/'netplay-shim/recomp_netplay_account.h')
    shutil.copy2(package/'tools/fast_timing.py',source/'fast_timing.py')
    shutil.copy2(package/'tools/interrupt_edges.py',source/'interrupt_edges.py')
    shutil.copy2(package/'tools/memory_fast.py',source/'memory_fast.py')
    shutil.copy2(package/'tools/debug_server_release.py',source/'debug_server_release.py')
    shutil.copy2(package/'tools/relpath_cc.py',source/'relpath_cc.py')
    shutil.copy2(package/'tools/relpath_ld.py',source/'relpath_ld.py')
    shutil.copy2(package/'tools/texture_pack_speed.py',source/'texture_pack_speed.py')
    shutil.copy2(package/'tools/dma_fast.py',source/'dma_fast.py')
    shutil.copy2(package/'tools/render_fast.py',source/'render_fast.py')
    build=store/'runtime-build'
    jobs=EACH if args.netplay_libs else JOBS
    status(f'Building your native game{" and its online version" if args.netplay_libs else ""} ({JOBS} jobs) - keep this window open')
    # ThinLTO is on by default (runtime-CMakeLists.txt); RR_BUILD_THINLTO=0 builds without it,
    # e.g. on a PC with little memory (the link needs about 2 GB).
    lto='OFF' if os.environ.get('RR_BUILD_THINLTO')=='0' else 'ON'
    # Speed profile (recipe/game.profdata, see runtime-CMakeLists.txt); RR_BUILD_PGO=gen
    # builds the instrumented trainer (tools/pgo_train_public.py), =off builds without.
    pgo_mode=os.environ.get('RR_BUILD_PGO','use');profile=package/'recipe'/'game.profdata'
    pgo_args=['-DRR_PGO_GENERATE='+('ON' if pgo_mode=='gen' else 'OFF'),'-DRR_PGO_USE='+(str(profile) if pgo_mode=='use' and profile.is_file() else '')]
    configure=[toolbin/'cmake.exe','-S',source,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER='+str(toolbin/'clang.exe'),'-DCMAKE_CXX_COMPILER='+str(toolbin/'clang++.exe'),'-DPSXRECOMP_ROOT='+str(framework),'-DRR_PRIVATE_ROOT='+str(private),'-DPSX_DEPS_OFFLINE=ON','-DPython3_EXECUTABLE='+sys.executable,'-DRR_THINLTO='+lto,'-DRR_LINK_JOBS='+str(jobs),*pgo_args]
    online=threading.Thread(target=build_online,args=(configure,toolbin,framework,env));online.start()
    try:
        run([*configure,'-B',build],env=env)
        build_target(toolbin,build,env,jobs)
        runtime=stage_runtime(build,toolbin,framework)
    finally:online.join()
    return runtime,'compiled from installed game data'
def online_fingerprint():
    """The same for every player with this release and game: online games only pair up when it matches."""
    h=hashlib.sha256()
    for name in [EXE_HASH,FRAMEWORK_REVISION]:h.update(name.encode()+b'\0')
    for folder in ['source','tools','recipe']:
        for p in sorted((package/folder).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:
                rel=p.relative_to(package).as_posix().encode();body=p.read_bytes()
                h.update(rel+b'\0'+len(body).to_bytes(8,'little')+body)
    return h.hexdigest()
def build_online(configure,toolbin,framework,env):
    """The online game (runtime-netplay): the same sources with recomp-net, eight seats and the
    AI-rider takeover. Built beside the offline game (compile_local), its tools' output in
    build-online.log. A failure here leaves the offline game as it is; online stays greyed out."""
    online=store/'runtime-netplay';ini=online/'RoadRashNativeResearch.netplay.ini'
    if ini.exists():ini.unlink()   # never a stale online game
    if not args.netplay_libs:say('Online game skipped: its libraries were not prepared.');return
    libs=args.netplay_libs.resolve()
    try:
        fingerprint=online_fingerprint();build=store/'runtime-netplay-build'
        with (store/'build-online.log').open('w',encoding='utf-8',buffering=1) as out:
            run([*configure,'-B',build,'-DRR_NETPLAY=ON','-DRECOMP_NET_ROOT='+str(required(libs/'recomp-net')),'-DRECOMP_RBENGINE_ROOT='+str(required(libs/'rbengine')),'-DRR_BUILD_FINGERPRINT='+fingerprint],env=env,out=out)
            build_target(toolbin,build,env,EACH,out)
            stage_runtime(build,toolbin,framework,'runtime-netplay',out)
        cache=(build/'CMakeCache.txt').read_text(errors='replace')
        on=lambda key:re.search('^'+key+r':BOOL=(ON|1|TRUE)\s*$',cache,re.M|re.I) is not None
        if not on('PSX_NETPLAY'):raise RuntimeError('the online build has no netplay')
        # The launcher's Online pages read this (launcher.c: online_ready, the lobby's build match).
        ini.write_text(f'[build]\nfingerprint={fingerprint}\nnetplay=1\nmode=delay\nplayers=8\nlobby={1 if on("RNET_ENABLE_ICE") else 0}\n',encoding='ascii')
        say('Online game ready, fingerprint '+fingerprint)
    except Exception as error:
        say('Online game not built ('+str(error)+'); the offline game is unaffected.')
try:
    image=required(store/'game-disc.bin');disc=Disc(image)
    inventory=disc.inventory();record=next((e for e in inventory if e['path']=='/SLUS_010.53'),None)
    if not record:raise DiscError('Installed disc does not contain the supported game')
    original=disc.read(record)
    if hashlib.sha256(original).hexdigest()!=EXE_HASH:raise DiscError('Unsupported game revision')
    image_hash=digest(image);inputs=store/'inputs';inputs.mkdir(exist_ok=True);exe=inputs/'SLUS_010.53';exe.write_bytes(original)
    shutil.copy2(package/'recipe/seeds.txt',inputs/'seeds.txt')
    stride=disc.sector_size;disc.close()
    cue=store/'installed-disc.cue';cue.write_text('FILE "game-disc.bin" BINARY\n  TRACK 01 '+('MODE2/2352' if stride==2352 else 'MODE1/2048')+'\n    INDEX 01 00:00:00\n')
    toolbin=required(workspace/'work/tooling/toolchain/bin');baseline=workspace/'work/build-stage2';baseline_exe=baseline/'RoadRashNativeResearch.exe'
    # Older developer caches lack the presentation adapter. Build it from the
    # user's installed files rather than silently adopting an older backend.
    framework=required(workspace/'work/upstream/psxrecomp');recompiler=required(workspace/'work/build-framework/psxrecomp-game.exe');bios=required(baseline/'bios/openbios.bin')
    runtime,origin=compile_local(exe,cue,image_hash,toolbin,framework,recompiler,bios)
    receipt={'schema':1,'created':time.strftime('%Y-%m-%d %H:%M:%S'),'game_executable_sha256':EXE_HASH,'installed_disc_sha256':image_hash,'runtime_sha256':digest(runtime),'framework_revision':FRAMEWORK_REVISION,'origin':origin,'original_disc_required_again':False,'distributed_game_data_or_backend':False,'presentation_adapter':'rr-pc-1.1','timing':'fast','precision_instrumentation':False,'pc_services_source_sha256':digest(package/'source/pc_services.cpp'),'discord_source_sha256':digest(package/'source/discord_presence.cpp'),'presentation_source_sha256':digest(package/'source/rider_enhancements.cpp'),'in_game_menu_source_sha256':digest(package/'source/rider_menu.cpp')}
    receipt['custom_music_source_sha256']=digest(package/'source/custom_music.cpp')
    receipt['hd_ui_source_sha256']=digest(package/'source/native_ui.cpp')
    receipt['voice_source_sha256']=digest(package/'source/taunt_voices.cpp')
    receipt['music_mixer_recipe_sha256']=digest(package/'tools/music_mixer.py')
    receipt['gpu_geometry_recipe_sha256']=digest(package/'tools/gpu_geometry.py')
    receipt['owner_asset_source_sha256']=digest(package/'source/owner_assets.c')
    receipt['gpu_presentation_recipe_sha256']=digest(package/'tools/gpu_presentation.py')
    receipt['race_timing_recipe_sha256']=digest(package/'tools/race_timing.py')
    receipt['fast_timing_recipe_sha256']=digest(package/'tools/fast_timing.py')
    receipt['interrupt_edges_recipe_sha256']=digest(package/'tools/interrupt_edges.py')
    receipt['memory_fast_recipe_sha256']=digest(package/'tools/memory_fast.py')
    receipt['texture_pack_speed_recipe_sha256']=digest(package/'tools/texture_pack_speed.py')
    receipt['dma_fast_recipe_sha256']=digest(package/'tools/dma_fast.py')
    receipt['render_fast_recipe_sha256']=digest(package/'tools/render_fast.py')
    receipt['spu_events_recipe_sha256']=digest(package/'tools/spu_events.py')
    receipt['native_guard_source_sha256']=digest(package/'source/native_guard.c')
    cache=store/'runtime-build'/'CMakeCache.txt';cache=cache.read_text(errors='replace') if cache.exists() else ''
    receipt['online_game']=(store/'runtime-netplay'/'RoadRashNativeResearch.netplay.ini').is_file()
    receipt['compiler_settings']={'march':'x86-64','lto':'thin' if 'RR_THINLTO:BOOL=ON' in cache and 'RR_THINLTO_LINKS:INTERNAL=1' in cache else 'off','pgo':'off'}
    (store/'local-build.json').write_text(json.dumps(receipt,indent=2)+'\n')
    status('Local game installation is ready. Return to the launcher and select Play.')
except Exception as error:
    say(str(error));sys.exit(1)
finally:log.close()
