"""Compile byte-proven BIOS and SDK kernel installation images at their RAM PCs.

Source bytes come from pinned original inputs. Optional snapshots only verify
the installation; they never supply code to the compiler. No runtime patches
are applied. Generated bodies retain timing and control-transfer helpers.
"""
import argparse, hashlib, json, pathlib, re, struct, subprocess

PINNED_BIOS = 'fabe498fbf224e4721f12f31b6f5fe0659205e341dc4e5c5f91b9bd1a1011c57'
PINNED_EXE = '52a3e28b2f97e49d8122eb4f510041e64dce046fd8fa9d4cdc49f9e64ff66a7b'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--framework', type=pathlib.Path, required=True)
parser.add_argument('--recompiler', type=pathlib.Path, required=True)
parser.add_argument('--bios', type=pathlib.Path, required=True)
parser.add_argument('--game-exe', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--snapshot', type=pathlib.Path)
args = parser.parse_args()
args.output = args.output.resolve()
args.framework = args.framework.resolve()
bios = args.bios.read_bytes()
if hashlib.sha256(bios).hexdigest() != PINNED_BIOS:
    raise ValueError('Unsupported OpenBIOS revision')
stub = bios[0x76f4:0x7704]
if struct.unpack('<4I', stub) != (0x3c020000, 0x24423554, 0x0040f809, 0):
    raise ValueError('Firmware installation source changed')
exe = args.game_exe.read_bytes()
if hashlib.sha256(exe).hexdigest() != PINNED_EXE:
    raise ValueError('Unsupported original game executable')
def original(pc, size):
    offset = 2048+pc-0x80010000
    return exe[offset:offset+size]
images = [('bios', 0x281c, stub, [0x281c],
           'OpenBIOS RAM 0x430C..0x4320 copies ROM 0xBFC076F4'),
          ('redirect', 0x357c, original(0x800450c8, 16), [0x357c],
           '_patch_card copies [0x800450C8,0x800450D8) to kernel handler+0x28'),
          ('sdk', 0xa000df80, original(0x80045058, 112), [0xa000df80, 0xa000dfac],
           '_copy_memcard_patch copies [0x80045058,0x800450C8) to 0x0000DF80')]
snapshot = args.snapshot.read_bytes() if args.snapshot else None
args.output.mkdir(parents=True, exist_ok=True)
declarations, rows, receipts = [], [], []
for name, base, body, seeds, producer in images:
    phys = base & 0x1fffffff
    if snapshot is not None and snapshot[phys:phys+len(body)] != body:
        raise ValueError(f'{name}: live installation differs from its original producer')
    folder = args.output/name
    folder.mkdir(exist_ok=True)
    header = bytearray(2048)
    header[:8] = b'PS-X EXE'
    struct.pack_into('<I', header, 0x10, base)
    struct.pack_into('<II', header, 0x18, base, len(body))
    (folder/f'{name}.exe').write_bytes(header+body)
    (folder/'seeds.txt').write_text(''.join(f'0x{s:08X}\n' for s in seeds))
    config = f'''[game]
name="Road Rash native kernel {name}"
id="RRKERNEL"
exe="{(folder/f'{name}.exe').as_posix()}"
load_address="0x{base:08X}"
entry_pc="0x{base:08X}"
text_size="0x{len(body):X}"
[recompiler]
bios_config="{(args.framework/'bios/OpenBIOS.toml').as_posix()}"
seeds="{(folder/'seeds.txt').as_posix()}"
strict=true
out_dir="{(folder/'generated').as_posix()}"
'''
    (folder/'kernel.toml').write_text(config)
    result = subprocess.run([str(args.recompiler.resolve()), '--config', str(folder/'kernel.toml'),
                             '--project-root', str(args.framework)], capture_output=True, text=True)
    (folder/'compile.log').write_text(result.stdout+'\n'+result.stderr)
    if result.returncode:
        raise RuntimeError(f'{name}: recompiler failed ({result.returncode}); see compile.log')
    units = sorted((folder/'generated').glob(f'{name}.exe_full_*.c'))
    if len(units) != 1:
        raise ValueError(f'{name}: unexpected code generation')
    code = units[0].read_text()
    symbols = sorted(set(re.findall(r'\bfunc_[0-9A-Fa-f]+(?:_cont_[0-9A-Fa-f]+)?\b', code)))
    defines = ''.join(f'#define {s} rr_kernel_{s}\n' for s in symbols)
    (args.output/f'rr_kernel_code_{name}.c').write_text(defines+
        f'#include "{name}/generated/{name}.exe_full_00.c"\n')
    declarations += [f'extern void rr_kernel_{s}(CPUState *cpu);\n' for s in symbols]
    dispatch = (folder/'generated'/f'{name}.exe_dispatch.c').read_text()
    entries = re.findall(r'\{(0x[0-9A-F]+)u, (0x[0-9A-F]+)u, [0-9]+u, [0-9]+u, (func_[A-F0-9]+)\}', dispatch)
    if not entries or any(s not in symbols for _, _, s in entries):
        raise ValueError(f'{name}: native entry table changed')
    words = ', '.join(f'0x{w:08X}u' for w in struct.unpack('<'+'I'*(len(body)//4), body))
    for pc, continuation, symbol in entries:
        if (int(pc, 16) & 0xe0000000) != (base & 0xe0000000):
            raise ValueError('Unproven segment alias emitted')
        rows.append(f'    {{{pc}u, 0x{base:08X}u, {len(body)//4}u, {{{words}}}, rr_kernel_{symbol}}},\n')
    receipts.append({'name': name, 'runtime_base': f'0x{base:08X}', 'size': len(body),
                     'producer': producer, 'source_sha256': hashlib.sha256(body).hexdigest(),
                     'generated_sha256': hashlib.sha256(units[0].read_bytes()).hexdigest(),
                     'entries': [pc for pc, _, _ in entries]})
(args.output/'rr_kernel_entries.h').write_text('/* Generated from pinned original sources. */\n'+
    ''.join(declarations)+'static const RRKernelEntry rr_kernel_entries[] = {\n'+''.join(rows)+'};\n')
receipt = {'bios_sha256': PINNED_BIOS, 'exe_sha256': PINNED_EXE,
           'snapshot_verified': bool(snapshot), 'images': receipts,
           'guard': 'Exact full-PC and all live image words on every dispatch',
           'entry_count': len(rows), 'interpreted_generation_instructions': 0}
(args.output/'provenance.json').write_text(json.dumps(receipt, indent=2)+'\n')
print(f'Prepared {len(images)} byte-proven kernel images and {len(rows)} native entries')
