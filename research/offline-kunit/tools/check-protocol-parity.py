#!/usr/bin/env python3
"""Supplemental HOST regression, not KUnit: actual v0.7 pure core vs preserved v0.6.
Temporary type/string adapters only; no emulated mutex/workqueue or hardware.
"""
import ctypes as C
import json
from pathlib import Path
import random
import subprocess
import tempfile

v07 = Path(__file__).resolve().parent.parent
v06 = v07.parent / 'offline-mcu'

class Packet(C.Structure):
    _fields_ = [('kind', C.c_int), ('opcode', C.c_uint8),
                ('payload', C.c_void_p), ('size', C.c_size_t)]

checks = 0

def check(ok):
    global checks
    checks += 1
    if not ok:
        raise AssertionError(f'host protocol parity check {checks}')

with tempfile.TemporaryDirectory(prefix='gx-v07-parity-') as tmp:
    root = Path(tmp)
    (root / 'linux').mkdir()
    (root / 'linux/types.h').write_text(
        '#include <stddef.h>\n#include <stdint.h>\n'
        'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;\n')
    (root / 'linux/string.h').write_text('#include <string.h>\n')
    libs = []
    for version, source, include in [
            ('06', v06 / 'src/goodix_protocol.c', v06 / 'include'),
            ('07', v07 / 'gx_protocol_core.c', root)]:
        output = root / (version + '.so')
        subprocess.run(['gcc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=undefined',
                        '-fno-sanitize-recover=undefined', '-I', str(include),
                        str(source), '-o', str(output)], check=True)
        lib = C.CDLL(str(output))
        lib.gx_parse_packet.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Packet)]
        lib.gx_copy_version.argtypes = [C.POINTER(Packet), C.c_void_p, C.c_size_t]
        lib.gx_validate_a8_spi.argtypes = [C.c_void_p, C.c_size_t]
        lib.gx_build_a8_spi.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        libs.append(lib)

    fixture = json.loads((v06 / 'fixtures/windows-a8.json').read_text())
    ack = bytes.fromhex(fixture['ack_header'] + fixture['ack_body'])
    data = bytes.fromhex(fixture['version_header'] + fixture['version_body'])
    request = bytes.fromhex(fixture['request_spi'])

    def outcome(lib, blob):
        buf = C.create_string_buffer(blob)
        packet = Packet()
        ret = lib.gx_parse_packet(buf, len(blob), C.byref(packet))
        if ret:
            return (ret,)
        payload = C.string_at(packet.payload, packet.size)
        out = C.create_string_buffer(128)
        string_ret = lib.gx_copy_version(C.byref(packet), out, 128)
        return ret, packet.kind, packet.opcode, payload, string_ret, out.raw

    for lib in libs:
        check(outcome(lib, ack)[0] == 0)
        check(outcome(lib, data)[-1].split(b'\0')[0].decode() == fixture['firmware'])
        for cap in range(17):
            out = C.create_string_buffer(32)
            written = C.c_size_t(999)
            ret = lib.gx_build_a8_spi(out, cap, C.byref(written))
            check(ret == (0 if cap >= 15 else -8))
            check(written.value == (15 if cap >= 15 else 0))
            if ret == 0:
                check(out.raw[:15] == request)
        check(lib.gx_validate_a8_spi(C.create_string_buffer(request), len(request)) == 0)

    blobs = [ack, data]
    for frame in (ack, data):
        blobs += [frame[:n] for n in range(len(frame))]
        for i in range(len(frame)):
            for bit in range(8):
                b = bytearray(frame)
                b[i] ^= 1 << bit
                blobs.append(bytes(b))
    rng = random.Random(0x0706)
    for _ in range(5000):
        blobs.append(bytes(rng.randrange(256) for _ in range(rng.randrange(301))))
    for blob in blobs:
        check(outcome(libs[0], blob) == outcome(libs[1], blob))
    print(f'PASS: {checks} HOST protocol checks, {len(blobs)} differential packets; UBSan enabled.')
    print('NOT KUnit; no kernel concurrency/lifecycle execution or hardware validation.')
