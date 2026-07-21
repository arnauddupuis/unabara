#!/usr/bin/env python3
"""Synthesize a FIT dive file exercising decoder paths absent from the real
samples: tank pods, big-endian definitions, compressed timestamps, developer
fields, and a gas switch."""
import struct

CRC_TABLE = [0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
             0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400]

def crc16(data, crc=0):
    for b in data:
        tmp = CRC_TABLE[crc & 0xF]
        crc = (crc >> 4) & 0x0FFF
        crc = crc ^ tmp ^ CRC_TABLE[b & 0xF]
        tmp = CRC_TABLE[crc & 0xF]
        crc = (crc >> 4) & 0x0FFF
        crc = crc ^ tmp ^ CRC_TABLE[(b >> 4) & 0xF]
    return crc

BASE = {'enum': (0x00, 'B'), 'u8': (0x02, 'B'), 's8': (0x01, 'b'),
        'u16': (0x84, 'H'), 's16': (0x83, 'h'), 'u32': (0x86, 'I'),
        's32': (0x85, 'i'), 'u32z': (0x8C, 'I'), 'string': (0x07, 's')}

def definition(local, global_id, fields, big_endian=False, dev_fields=None):
    """fields: list of (field_num, base_key) or (field_num, base_key, size)"""
    header = 0x40 | local
    if dev_fields:
        header |= 0x20
    out = bytes([header, 0, 1 if big_endian else 0])
    out += struct.pack('>H' if big_endian else '<H', global_id)
    out += bytes([len(fields)])
    for f in fields:
        num, key = f[0], f[1]
        base, fmt = BASE[key]
        size = f[2] if len(f) > 2 else struct.calcsize(fmt)
        out += bytes([num, size, base])
    if dev_fields:
        out += bytes([len(dev_fields)])
        for num, size, idx in dev_fields:
            out += bytes([num, size, idx])
    return out

def data(local, fields, big_endian=False, dev_bytes=b''):
    out = bytes([local])
    endian = '>' if big_endian else '<'
    for key, value in fields:
        fmt = BASE[key][1]
        out += struct.pack(endian + fmt, value)
    return out + dev_bytes

def compressed(local, time_offset, fields, big_endian=False):
    out = bytes([0x80 | (local << 5) | time_offset])
    endian = '>' if big_endian else '<'
    for key, value in fields:
        out += struct.pack(endian + BASE[key][1], value)
    return out

START = 1100000000  # FIT epoch seconds
records = b''

# Sport: sub-sport 54 (multi-gas dive)
records += definition(0, 12, [(1, 'enum')])
records += data(0, [('enum', 54)])

# Two gases: message_index 254, o2 field 1, he field 0, status 2, type 3
records += definition(1, 259, [(254, 'u16'), (1, 'u8'), (0, 'u8'), (2, 'enum'), (3, 'enum')])
records += data(1, [('u16', 0), ('u8', 21), ('u8', 0), ('enum', 1), ('enum', 0)])
records += data(1, [('u16', 1), ('u8', 50), ('u8', 0), ('enum', 1), ('enum', 0)])

# Two tank pods: sensor id field 0 (u32z), type field 52 (28=pod), units 74 (2=bar), volume 77 (L*10)
records += definition(2, 147, [(0, 'u32z'), (52, 'enum'), (74, 'enum'), (77, 'u16')])
records += data(2, [('u32z', 1111), ('enum', 28), ('enum', 2), ('u16', 120)])
records += data(2, [('u32z', 2222), ('enum', 28), ('enum', 2), ('u16', 100)])

# RECORD definition: BIG-ENDIAN, with one developer field (3 bytes) to skip.
# timestamp 253, depth 92 (mm u32), temp 13 (s8), ndl 96 (u32 s), cns 97 (u8)
records += definition(3, 20, [(253, 'u32'), (92, 'u32'), (13, 's8'), (96, 'u32'), (97, 'u8')],
                      big_endian=True, dev_fields=[(0, 3, 0)])

# TANK_UPDATE definition (little-endian): timestamp, sensor, pressure bar*100
records += definition(4, 319, [(253, 'u32'), (0, 'u32z'), (1, 'u16')])
# EVENT definition: timestamp, event 0, data 3
records += definition(5, 21, [(253, 'u32'), (0, 'enum'), (3, 'u32')])

def record(ts, depth_mm, temp, ndl_s, cns):
    return data(3, [('u32', ts), ('u32', depth_mm), ('s8', temp), ('u32', ndl_s), ('u8', cns)],
                big_endian=True, dev_bytes=b'\xAA\xBB\xCC')

# Pre-dive-start gas switch event (should clamp to t=0, dive starts on gas 0 anyway)
records += data(5, [('u32', START - 2), ('enum', 57), ('u32', 0)])

# Dive samples: 0..9 s, descending, tank updates interleaved
records += record(START + 0, 1000, 19, 3000, 0)
records += data(4, [('u32', START + 1), ('u32z', 1111), ('u16', 20050)])  # 200.5 bar
records += data(4, [('u32', START + 1), ('u32z', 2222), ('u16', 18000)])  # 180.0 bar
records += record(START + 1, 5000, 19, 2900, 0)
# Compressed-timestamp records on local type 1 slot? Compressed local is 2 bits (0-3):
# reuse local 3 (RECORD) via compressed header (local 3 fits in 2 bits)
records += compressed(3, (START + 2) & 0x1F,
                      [('u32', START + 2), ('u32', 10000), ('s8', 18), ('u32', 2500), ('u8', 1)],
                      big_endian=True)[:1] + \
           struct.pack('>IIbIB', START + 2, 10000, 18, 2500, 1) + b'\xAA\xBB\xCC'
records += record(START + 3, 15000, 18, 2000, 1)
# Gas switch to gas index 1 (EAN50) at t=5
records += data(5, [('u32', START + 5), ('enum', 57), ('u32', 1)])
records += data(4, [('u32', START + 5), ('u32z', 1111), ('u16', 19000)])  # 190.0 bar
records += record(START + 5, 20000, 18, 1500, 2)
records += record(START + 7, 12000, 18, 1800, 2)
records += record(START + 9, 2000, 19, 3000, 3)

# TANK_SUMMARY: sensor, start, end (bar*100)
records += definition(6, 323, [(253, 'u32'), (0, 'u32z'), (1, 'u16'), (2, 'u16')])
records += data(6, [('u32', START + 10), ('u32z', 1111), ('u16', 20050), ('u16', 18500)])
records += data(6, [('u32', START + 10), ('u32z', 2222), ('u16', 18000), ('u16', 17500)])

# DIVE_SUMMARY: timestamp, dive number field 10, avg depth field 2 (mm)
records += definition(7, 268, [(253, 'u32'), (10, 'u32'), (2, 'u32')])
records += data(7, [('u32', START + 10), ('u32', 42), ('u32', 10500)])

# SESSION at the end (like real files): start_time 2, lat 3, lon 4
records += definition(8, 18, [(253, 'u32'), (2, 'u32'), (3, 's32'), (4, 's32')])
records += data(8, [('u32', START + 10), ('u32', START),
                    ('s32', int(46.5 / 180 * 2**31)), ('s32', int(6.5 / 180 * 2**31))])

# ACTIVITY: local_timestamp field 5 = timestamp + 3600 (UTC+1)
records += definition(9, 34, [(253, 'u32'), (5, 'u32')])
records += data(9, [('u32', START + 10), ('u32', START + 10 + 3600)])

header = struct.pack('<BBHI4s', 14, 0x20, 2195, len(records), b'.FIT')
header += struct.pack('<H', crc16(header))
payload = header + records
payload += struct.pack('<H', crc16(payload))

with open('synth_dive.fit', 'wb') as f:
    f.write(payload)
print(f"wrote synth_dive.fit ({len(payload)} bytes)")
