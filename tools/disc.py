"""Strict, read-only PS1 ISO9660 analysis. No game bytes in public reports."""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
import pathlib
import re
import struct


class DiscError(ValueError):
    pass


def u32(b, p):
    return struct.unpack_from('<I', b, p)[0]


def both32(b, p):
    little, big = u32(b, p), struct.unpack_from('>I', b, p + 4)[0]
    if little != big:
        raise DiscError('ISO9660 endian copies disagree')
    return little


def exe_header(data):
    if len(data) < 2048 or data[:8] != b'PS-X EXE':
        raise DiscError('Not a complete PS-X EXE header')
    h = dict(zip(['entry_pc','initial_gp','load_address','payload_size','data_address','data_size','bss_address','bss_size','stack_base','stack_offset'], struct.unpack_from('<10I', data, 0x10)))
    start = h['load_address'] & 0x1fffffff
    if h['payload_size'] > 0x200000 or start + h['payload_size'] > 0x200000:
        raise DiscError('Executable payload is outside retail PS1 RAM')
    if len(data) < 2048 + h['payload_size']:
        raise DiscError('Truncated executable payload')
    entry = h['entry_pc'] & 0x1fffffff
    if entry % 4 or not start <= entry < start + h['payload_size']:
        raise DiscError('Entry point is not inside the aligned executable payload')
    h['sha256'] = hashlib.sha256(data).hexdigest()
    h['entry_file_offset'] = 2048 + entry - start
    return h


class Disc:
    def __init__(self, path):
        self.path = pathlib.Path(path).resolve()
        self.cue = None
        self.base = 0
        if self.path.suffix.lower() == '.cue':
            self.cue = self.path.read_text(encoding='utf-8-sig')
            files = re.findall(r'^\s*FILE\s+"([^"]+)"\s+BINARY\s*$', self.cue, re.M | re.I)
            tracks = re.findall(r'^\s*TRACK\s+(\d+)\s+(\S+)', self.cue, re.M | re.I)
            indexes = re.findall(r'^\s*INDEX\s+01\s+(\d+):(\d+):(\d+)', self.cue, re.M | re.I)
            if len(files) != 1 or len(tracks) != 1 or len(indexes) != 1:
                raise DiscError('This inspector currently supports one FILE and one data TRACK; multi-track is rejected')
            if tracks[0][1].upper() not in ('MODE2/2352', 'MODE1/2352', 'MODE1/2048'):
                raise DiscError('Unsupported CUE data track format')
            relative = pathlib.PureWindowsPath(files[0])
            if relative.is_absolute() or '..' in relative.parts:
                raise DiscError('CUE path must remain in its own directory')
            self.path = (self.path.parent / pathlib.Path(*relative.parts)).resolve()
            m, s, f = map(int, indexes[0])
            if s >= 60 or f >= 75:
                raise DiscError('Invalid CUE timestamp')
            self.base = (m * 60 + s) * 75 + f
            self.sector_size = int(tracks[0][1].split('/')[1])
        else:
            self.sector_size = 0
        self.fp = self.path.open('rb')
        size = self.path.stat().st_size
        try:
            self._load_volume(size)
        except BaseException:
            self.fp.close()
            raise

    def _load_volume(self, size):
        if not self.sector_size:
            for candidate in (2048, 2352):
                if size % candidate:
                    continue
                for offset in ([0] if candidate == 2048 else [16,24]):
                    self.fp.seek(16 * candidate + offset)
                    if self.fp.read(7) == b'\x01CD001\x01':
                        self.sector_size = candidate
                        break
                if self.sector_size:
                    break
        if not self.sector_size or size % self.sector_size:
            self.fp.close()
            raise DiscError('No supported aligned ISO9660 data track found')
        self.sectors = size // self.sector_size - self.base
        pvd = None
        for lba in range(16, min(80, self.sectors)):
            descriptor = self.sector(lba)
            if descriptor[1:6] != b'CD001' or descriptor[6] != 1:
                raise DiscError('Invalid ISO9660 descriptor')
            if descriptor[0] == 1:
                pvd = descriptor
            if descriptor[0] == 255:
                break
        if pvd is None:
            raise DiscError('No primary volume descriptor')
        if struct.unpack_from('<H', pvd, 128)[0] != 2048 or struct.unpack_from('>H', pvd, 130)[0] != 2048:
            raise DiscError('Unsupported logical block size')
        self.volume = {'system_id': pvd[8:40].decode('ascii',errors='replace').strip(), 'volume_id': pvd[40:72].decode('ascii',errors='replace').strip(), 'logical_blocks': both32(pvd,80), 'sector_size': self.sector_size, 'data_track_start_sector': self.base}
        if self.volume['logical_blocks'] > self.sectors:
            raise DiscError('ISO volume extends beyond data track')
        self.root = self.record(pvd[156:156+pvd[156]])

    def close(self):
        self.fp.close()

    def sector(self,lba):
        if lba < 0 or lba >= self.sectors:
            raise DiscError('Sector out of bounds')
        self.fp.seek((self.base+lba)*self.sector_size)
        raw = self.fp.read(self.sector_size)
        if len(raw) != self.sector_size:
            raise DiscError('Truncated sector')
        if self.sector_size == 2048:
            return raw
        if raw[:12] != b'\x00' + b'\xff'*10 + b'\x00':
            raise DiscError('Invalid raw CD sync')
        if raw[15] == 1:
            return raw[16:2064]
        if raw[15] == 2:
            if raw[16:20] != raw[20:24]:
                raise DiscError('XA subheader copies disagree')
            if raw[18] & 0x20:
                raise DiscError('XA Form 2 sector: requires streaming audio/video decoder, not a 2048-byte file read')
            return raw[24:2072]
        raise DiscError('Unsupported raw CD sector mode')

    @staticmethod
    def record(r):
        if len(r) < 34 or r[0] != len(r) or 33+r[32] > len(r):
            raise DiscError('Invalid directory record')
        if r[1] or r[26] or r[27]:
            raise DiscError('Extended attributes/interleaved ISO files are unsupported')
        name=r[33:33+r[32]]
        return {'name': name.decode('ascii',errors='replace'), 'extent_lba':both32(r,2), 'size':both32(r,10), 'flags':r[25], 'directory':bool(r[25]&2), 'xa_attributes_hex': r[33+r[32]+(1 if not r[32]%2 else 0):].hex()}

    def read(self, entry, limit=None):
        if entry['flags'] & 0x80:
            raise DiscError('Multi-extent ISO file is unsupported')
        n=entry['size'] if limit is None else min(entry['size'],limit)
        if entry['extent_lba'] + (entry['size']+2047)//2048 > self.volume['logical_blocks']:
            raise DiscError('File extent extends beyond ISO volume')
        return b''.join(self.sector(entry['extent_lba']+i) for i in range((n+2047)//2048))[:n]

    def inventory(self):
        result=[]
        seen=set()
        def walk(directory,parent,depth):
            if depth>32 or directory['size']>16*1024*1024:
                raise DiscError('Unreasonable directory dimensions')
            key=(directory['extent_lba'],directory['size'])
            if key in seen:
                raise DiscError('Repeated/cyclic directory extent')
            seen.add(key)
            data=self.read(directory)
            pos=0
            while pos<len(data):
                length=data[pos]
                if not length:
                    pos=(pos//2048+1)*2048
                    continue
                if pos%2048+length>2048 or pos+length>len(data):
                    raise DiscError('Directory record crosses sector or directory boundary')
                item=self.record(data[pos:pos+length]);pos+=length
                if item['name'] in ('\x00','\x01'):
                    continue
                name=re.sub(r';\d+$','',item['name'])
                if not name or name in ('.','..') or any(c in name for c in '/\\:'):
                    raise DiscError('Unsafe ISO filename')
                item['path']=parent+'/'+name
                result.append(item)
                if len(result)>100000:
                    raise DiscError('Too many directory entries')
                if item['directory']:
                    walk(item,item['path'],depth+1)
        walk(self.root,'',0)
        return result


def analyze(path, private_dir=None):
    disc=Disc(path)
    try:
        entries=disc.inventory()
        by_path={e['path'].upper():e for e in entries}
        files=[e for e in entries if not e['directory']]
        system=by_path.get('/SYSTEM.CNF')
        boot_text=disc.read(system).decode('ascii',errors='replace') if system else None
        boot=None
        if boot_text:
            match=re.search(r'^\s*BOOT\s*=\s*cdrom:\\?([^\r\n;]+)(?:;\d+)?',boot_text,re.M|re.I)
            if match:
                boot='/'+match[1].strip().replace('\\','/').lstrip('/')
        executables=[]
        for item in files:
            try:
                prefix=disc.read(item,4096)
                if prefix[:8]==b'PS-X EXE':
                    data=disc.read(item)
                    info=exe_header(data)
                    info['path']=item['path']
                    executables.append(info)
                    item['format']='PS-X EXE'
                    if private_dir:
                        target=pathlib.Path(private_dir)/'executables'/item['path'].lstrip('/')
                        target.parent.mkdir(parents=True,exist_ok=True)
                        target.write_bytes(data)
                elif prefix[:4] in (b'VAGp',b'VAGi',b'pBAV',b'pQES',b'SHPP'):
                    item['format']=prefix[:4].decode('ascii')
                elif prefix[:4]==b'\x10\x00\x00\x00' and len(prefix)>=8 and u32(prefix,4)<=15:
                    item['format']='TIM signature candidate (unvalidated)'
                else:
                    item['format']='unidentified'
            except DiscError as exc:
                item['format']='unreadable via Form1 accessor'
                item['scan_note']=str(exc)
        main=next((e for e in executables if e['path'].upper()==(boot or '').upper()),None)
        return {'schema_version':1,'source':str(pathlib.Path(path).resolve()),'volume':disc.volume,'system_cnf':boot_text,'boot_path':boot,'boot_executable':main,'file_count':len(files),'directory_count':len(entries)-len(files),'extension_counts':dict(collections.Counter(pathlib.PurePosixPath(e['path']).suffix.upper() or '(none)' for e in files)),'executable_headers':executables,'files':files,'limitations':['Container formats and headerless overlays are not identified by extension or signatures alone.','No game code is executed by disc inspection.','XA Form2 streams are reported and not treated as Form1 files.','Reports establish technical identity and cannot prove lawful ownership.']}
    finally:
        disc.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disc',type=pathlib.Path)
    parser.add_argument('--report',type=pathlib.Path,required=True)
    parser.add_argument('--private-dir',type=pathlib.Path)
    args=parser.parse_args()
    try:
        report=analyze(args.disc,args.private_dir)
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({k:report[k] for k in ('volume','boot_path','boot_executable','file_count','directory_count','extension_counts','executable_headers')},indent=2))
    except (DiscError,OSError) as exc:
        parser.exit(2,str(exc)+'\n')
