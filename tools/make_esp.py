"""Generate PlantAnything.esp: an ESL-flagged plugin holding placeholder FLOR records.

The SKSE plugin fills each placeholder in at startup (mesh, name, produce, sound, bounds)
for items that have no plant that grows properly in fertile soil. The records only need to
exist so that plants spawned from them keep a stable base FormID in the save.

usage: python make_esp.py <output path> [count]
"""
import struct
import sys

FIRST_ID = 0x800  # ESL range 0x800-0xFFF works on SE 1.5.97 and AE
MAX_COUNT = 0x800


def sub(tag: bytes, data: bytes) -> bytes:
    assert len(tag) == 4 and len(data) < 0x10000
    return tag + struct.pack("<H", len(data)) + data


def zstr(s: str) -> bytes:
    return s.encode("ascii") + b"\0"


def record(tag: bytes, form_id: int, flags: int, data: bytes) -> bytes:
    # type, dataSize, flags, formID, timestamp, version control, internal version (44 = SSE), unknown
    return tag + struct.pack("<IIIHHHH", len(data), flags, form_id, 0, 0, 44, 0) + data


def group(label: bytes, contents: bytes) -> bytes:
    return b"GRUP" + struct.pack("<I", 24 + len(contents)) + label + struct.pack("<iHHI", 0, 0, 0, 0) + contents


def build(count: int) -> bytes:
    assert 0 < count <= MAX_COUNT
    flora = []
    for i in range(count):
        local = FIRST_ID + i
        data = b"".join([
            sub(b"EDID", zstr(f"PlantAnythingSoilPlant{i:04d}")),
            sub(b"OBND", b"\0" * 12),
            sub(b"FULL", zstr("Plant")),
            sub(b"PFPC", bytes([100, 100, 100, 100])),  # produce in every season
        ])
        flora.append(record(b"FLOR", 0x01000000 | local, 0, data))  # master count 1 -> own index 01
    flor_group = group(b"FLOR", b"".join(flora))

    hedr = struct.pack("<fiI", 1.7, count + 1, FIRST_ID + count)  # version, records+groups, next object id
    tes4 = b"".join([
        sub(b"HEDR", hedr),
        sub(b"CNAM", zstr("PlantAnything")),
        sub(b"SNAM", zstr("Placeholder plants for Plant Anything (filled in by PlantAnything.dll).")),
        sub(b"MAST", zstr("Skyrim.esm")),
        sub(b"DATA", b"\0" * 8),
    ])
    return record(b"TES4", 0, 0x200, tes4) + flor_group  # 0x200 = light (ESL) flag


if __name__ == "__main__":
    out = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 1024
    with open(out, "wb") as f:
        f.write(build(count))
    print(f"wrote {out} with {count} placeholder flora")
