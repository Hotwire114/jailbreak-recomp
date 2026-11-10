"""Produce a strict-native copy of the framework's dirty-RAM execution unit.

The guard stops before any interpreted instruction changes guest state.
It does not replace or bypass native dispatch or any guest function.
"""
import argparse,hashlib,json,pathlib

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--framework',type=pathlib.Path,required=True)
parser.add_argument('--output',type=pathlib.Path,required=True)
args=parser.parse_args()
source=args.framework/'runtime/src/dirty_ram_interp.c'
text=source.read_text(encoding='utf-8')
anchor='static int exec_one_fetched(CPUState *cpu, uint32_t pc, uint32_t insn,\n                            uint32_t *next_pc_out) {'
if text.count(anchor)!=1:raise RuntimeError('Framework interpreter entry changed; review guard before building')
insertion='''
#ifdef RR_STRICT_NATIVE
    extern void rr_native_gap(CPUState *cpu, uint32_t pc, uint32_t insn);
    rr_native_gap(cpu, pc, insn);
#endif
'''
args.output.mkdir(parents=True,exist_ok=True)
target=args.output/'dirty_ram_interp_guarded.c'
dispatch_anchor='    /* B-2: statically-compiled overlay functions (generated/overlays_static.c).'
if text.count(dispatch_anchor)!=1:raise RuntimeError('Framework native dispatch entry changed; review integration')
dispatch_hook='''#ifdef RR_STRICT_NATIVE
    extern int rr_kernel_native_dispatch(CPUState *cpu, uint32_t addr);
    if (!forced && rr_kernel_native_dispatch(cpu, addr)) return 1;
#endif

'''
target.write_text(text.replace(anchor,anchor+insertion).replace(dispatch_anchor,dispatch_hook+dispatch_anchor),encoding='utf-8')
(args.output/'native_guard_provenance.json').write_text(json.dumps({'upstream_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'guarded_source_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'hook':'exec_one_fetched, before guest-state changes','policy':'Stop before the first dirty-RAM interpreted instruction when RR_STRICT_NATIVE is defined.'},indent=2)+'\n')
