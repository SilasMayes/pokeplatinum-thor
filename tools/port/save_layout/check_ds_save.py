#!/usr/bin/env python3
"""check_ds_save.py SAVE.bin|.sav [...]

Checks a Platinum backup image against the DS's save layout as PKHeX reads it (SAV4Pt: general block 0xCF2C bytes
at 0, box block 0x121E4 bytes right after, footers in the last 0x14 bytes of each, CRC-16/CCITT over the rest),
independently of the game's code, and prints what PKHeX would see. Also says whether a copy is in the port's old
layout (general 0xCF40 / boxes 0x121EC), which save_migrate_v1.c converts.
"""
import struct, sys

GEN4_LETTERS = {0x0121 + i: str(i) for i in range(10)}
GEN4_LETTERS.update({0x012B + i: chr(ord('A') + i) for i in range(26)})
GEN4_LETTERS.update({0x0145 + i: chr(ord('a') + i) for i in range(26)})

def crc16_ccitt(data):
    top = bot = 0xFF
    for b in data:
        x = b ^ top
        x ^= x >> 4
        top = (bot ^ (x >> 3) ^ (x << 4)) & 0xFF
        bot = (x ^ (x << 5)) & 0xFF
    return (top << 8) | bot

def block(img, ofs, size, block_id):
    if ofs + size > len(img):
        return None
    data = img[ofs:ofs + size]
    save_counter, block_counter, fsize, sig, bid, crc = struct.unpack_from('<IIIIBxH', data, size - 0x14)
    ok = fsize == size and sig == 0x20060623 and bid == block_id and crc == crc16_ccitt(data[:size - 0x14])
    return ok, save_counter, block_counter

def name(raw):
    out = ''
    for (c,) in struct.iter_unpack('<H', raw):
        if c == 0xFFFF:
            break
        out += GEN4_LETTERS.get(c, '?')
    return out

for path in sys.argv[1:]:
    img = open(path, 'rb').read()
    img += b'\0' * (0x80000 - len(img))
    print(path)
    for copy, base in (('primary', 0), ('backup', 0x40000)):
        g = block(img, base, 0xCF2C, 0)
        b = block(img, base + 0xCF2C, 0x121E4, 1)
        og = block(img, base, 0xCF40, 0)
        ob = block(img, base + 0xCF40, 0x121EC, 1)
        line = f'  {copy}: DS layout general {"OK" if g[0] else "-"} boxes {"OK" if b[0] else "-"}'
        line += f' | old port layout general {"OK" if og[0] else "-"} boxes {"OK" if ob[0] else "-"}'
        print(line)
        if g[0]:
            gen = img[base:base + 0xCF2C]
            tid, sid = struct.unpack_from('<HH', gen, 0x68 + 0x10)
            party_count = struct.unpack_from('<I', gen, 0x9C)[0]
            print(f'    save counter {g[1]}, trainer {name(gen[0x68:0x68 + 16])!r} ID {tid} SID {sid}, party {party_count},'
                  f' daycare at 0x1654, pokedex at 0x1328 (PKHeX offsets)')
        if b[0]:
            box = img[base + 0xCF2C:base + 0xCF2C + 0x121E4]
            current = struct.unpack_from('<I', box, 0)[0]
            filled = sum(1 for i in range(18 * 30) if struct.unpack_from('<I', box, 4 + i * 136)[0] != 0)
            print(f'    boxes: current box {current}, {filled} slots used, unlocked wallpapers {box[0x121C6]:#x}')
