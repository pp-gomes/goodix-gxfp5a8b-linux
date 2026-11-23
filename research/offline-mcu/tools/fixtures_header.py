"""Convert recorded bytes to C constants. No protocol implementation or I/O API."""
import json
from pathlib import Path
import sys

source, destination = map(Path, sys.argv[1:])
fixture = json.loads(source.read_text())
lines = ['/* Generated from fixtures/windows-a8.json; do not edit. */']
for key in ('request_spi', 'ack_header', 'ack_body', 'version_header',
            'version_body', 'cpu_select_tx', 'cpu_read_tx', 'cpu_rx'):
    data = bytes.fromhex(fixture[key])
    lines.append(f'static const uint8_t fx_{key}[] = {{' +
                 ', '.join(f'0x{x:02x}' for x in data) + '};')
lines.append('static const char fx_firmware[] = ' + json.dumps(fixture['firmware']) + ';')
destination.write_text('\n'.join(lines) + '\n')
