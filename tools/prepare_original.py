"""Prepare an isolated, owner-local Original runtime without changing mods.

The present backend is the locally compiled REFERENCE build. Original has
its own executable directory because the engine reads mods/settings there,
not from its working directory. Only runtime essentials are copied locally;
no game executable, BIOS, game data or snapshots are in this distribution.
"""
import argparse, json, os, pathlib, shutil, sys, uuid
from mod_manager import atom, file_hash, identity, private_path, require

def original(store):
    disc, inventory, image_hash = identity(store)
    try:
        receipt = json.loads((store/'local-build.json').read_text())
        source = private_path(store, store/'runtime')
        binary = private_path(store, source/'RoadRashNativeResearch.exe')
        require(binary.is_file() and file_hash(binary)==receipt['runtime_sha256'],
                'The local game build differs from its receipt; rebuild it first')
        sidecar = binary.with_suffix('.execution.json')
        # Future enhanced implementation families must retain a separately
        # compiled reference. Never label an HLE executable as Original.
        if sidecar.exists():
            contract=json.loads(private_path(store,sidecar).read_text())
            require(contract.get('profile')=='REFERENCE',
                    'Original requires the reference game build')
            if 'binary_sha256' in contract:
                require(contract['binary_sha256']==receipt['runtime_sha256'],
                        'The reference execution contract differs from its binary')
        target = private_path(store, store/'runtime-original')
        target.mkdir(exist_ok=True)
        mods = private_path(store, target/'mods')
        if mods.exists():
            require(not any(mods.rglob('manifest.toml')),
                    'Original runtime contains mod packages; keep them in the Enhanced mod library')
        files=[binary]
        files += [private_path(store,p) for p in source.glob('*.dll')]
        if sidecar.exists():files.append(sidecar)
        for name in ['openbios.bin','OpenBIOS.LICENSE']:
            p=private_path(store,source/'bios'/name)
            require(p.is_file(),'The local OpenBIOS files are missing; rebuild the game')
            files.append(p)
        for src in files:
            dst=private_path(store,target/src.relative_to(source))
            dst.parent.mkdir(parents=True,exist_ok=True)
            if dst.exists() and file_hash(dst)==file_hash(src):continue
            temp=private_path(store,dst.with_name(dst.name+'.'+uuid.uuid4().hex+'.writing'))
            try:
                shutil.copy2(src,temp)
                require(file_hash(temp)==file_hash(src),'Local runtime copy failed verification')
                os.replace(temp,dst)
            finally:
                if temp.exists():temp.unlink()
        # Original owns this separate settings file; Enhanced settings, mod
        # selections and source runtime remain untouched, including on errors.
        atom(private_path(store,target/'settings.toml'),b'# Original mode uses game-original.toml.\n')
        atom(private_path(store,target/'rider-options.ini'),b'[hud]\nlarger_text=0\n[riders]\nrename=0\n')
        mods.mkdir(exist_ok=True)
        atom(private_path(store,mods/'state.toml'),b'format_version = 2\n')
        atom(store/'original-launch.json',(json.dumps({
            'schema':1,'mode':'original','installed_disc_sha256':image_hash,
            'runtime_sha256':file_hash(target/binary.name),
            'runtime_directory':'runtime-original','mods_enabled':False,
            'original_instruction_timing':True,'enhanced_selection_changed':False,
            'locally_staged_only':True
        },indent=2)+'\n').encode())
    finally:disc.close()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data-root',type=pathlib.Path,required=True)
    store=p.parse_args().data_root.resolve()
    try:
        original(store)
        message='ORIGINAL MODE READY - ENHANCED SETTINGS AND MOD CHOICES KEPT.'
        code=0
    except Exception as error:
        message='ORIGINAL MODE NOT READY: '+str(error);code=1
    (store/'launch-status.txt').write_text(message[:230],encoding='utf-8')
    print(message);return code

if __name__=='__main__':sys.exit(main())
