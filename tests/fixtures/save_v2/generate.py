#!/usr/bin/env python3
"""Reproducible synthetic EAGLS v2.0 save examples; no game or player data.

Optional maintainer utility, Python 3.10+ standard library. Neither CMake nor the
tool invokes it. Implements its own zero-run LZSS encoder and CRT XOR oracle.
The files demonstrate the container, not a playable state.
"""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parent
MAGIC = b"[E]nhanced [A]dventure [G]ame [L]anguage [S]ystem E.A.G.L.S"
POOL = b"1qaz2wsx3edc4rfv5tgb6yhn7ujm8ik,9ol.0p;/-@:^[]"

def compress(raw):
    ring = bytearray(4096)
    r = 4078
    out = bytearray()
    p = 0
    while p < len(raw):
        flag_pos = len(out)
        out.append(0)
        for bit in range(8):
            if p >= len(raw): break
            length = min(18, len(raw) - p)
            while length >= 3 and any(raw[p:p+length]): length -= 1
            offset = ring.find(bytes(length)) if length >= 3 else -1
            if offset >= 0:
                out.extend([offset & 255, ((offset >> 8) << 4) | (length - 3)])
                for _ in range(length):
                    ring[r] = 0; r = (r+1) & 4095
                p += length
            else:
                out[flag_pos] |= 1 << bit
                c = raw[p]; p += 1
                out.append(c); ring[r] = c; r = (r+1) & 4095
    return bytes(out)

def crypt(data, seed):
    state = (seed if seed < 128 else seed-256) & 0xffffffff
    result = bytearray(data)
    for i in range(len(result)):
        state = (state*214013+2531011) & 0xffffffff
        result[i] ^= POOL[((state >> 16) & 32767) % len(POOL)]
    return bytes(result)

def write_example(kind, blocks, seed):
    folder = ROOT/kind
    archive = folder/'decrypted'
    archive.mkdir(parents=True, exist_ok=True)
    header = MAGIC.ljust(128, b'\0')
    tail = b'SYNTHETIC_STALE_TAIL_NOT_A_SEED\xff'
    streams = [compress(b) for b in blocks]
    encrypted = header + b''.join(struct.pack('<I',len(s)) for s in streams)
    encrypted += crypt(b''.join(streams),seed) + bytes([seed]) + tail
    (folder/'example.sav').write_bytes(encrypted)
    for i,(block,stream) in enumerate(zip(blocks,streams)):
        (archive/f'block{i}.bin').write_bytes(block)
        (archive/f'block{i}.lzss').write_bytes(stream)
    for name,data in [('header.bin',header),('seed.bin',bytes([seed])),('trailer.bin',tail)]:
        (archive/name).write_bytes(data)
    metadata = dict(format='eagls-v2-save-v1',profile='eagls-v2.0-pe1.5.0.0',kind=kind)
    (archive/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8',newline='\n')

def main():
    global_state = bytearray(0x10b008)
    global_marker = b'SYNTHETIC_GLOBAL_NO_GAME_CONTENT!'
    global_state[:len(global_marker)] = global_marker
    struct.pack_into('<I',global_state,0xc01fc,15)
    numbered = bytearray(0x157400)
    marker = b'SYNTHETIC_NUMBERED_NO_GAME_CONTENT'
    numbered[:len(marker)] = marker
    sizes = [18,18,23,18,12,28,14,13,8,18,17,18,18,23,18,12,28,14,13,8,18,17]
    for i,size in enumerate(sizes):
        offset = 0x79808 + i*376
        song = 11 <= i <= 20
        struct.pack_into('<iii',numbered,offset+8,800 if song else 500,0,size)
        face = ('ＭＳ 明朝' if song else 'ＭＳ ゴシック').encode('cp932')
        numbered[offset+48:offset+48+len(face)] = face
    second = bytearray(0x157400)
    second_marker = b'SYNTHETIC_SECOND_BLOCK_RNG_CHECK!'
    second[:len(second_marker)] = second_marker
    write_example('global',[bytes(global_state)],83)
    write_example('numbered',[bytes(numbered),bytes(second)],195)
    sums={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
          for kind in ['global','numbered'] for p in sorted((ROOT/kind).rglob('*')) if p.is_file()}
    (ROOT/'SHA256SUMS.json').write_text(json.dumps(sums,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__': main()
