#!/usr/bin/env python3
"""Fail on hardware imports, device aliases or module parameters in the object."""
import pathlib
import re
import subprocess
import sys

obj = pathlib.Path(sys.argv[1]).resolve()
symbols = subprocess.check_output(['nm', '-u', str(obj)], text=True)
forbidden = re.compile(
    r'^(?:__?spi_|spi_|(?:devm_)?gpiod?_|(?:__)?pm_runtime_|acpi_|'
    r'(?:devm_)?request_(?:threaded_)?irq$|pci_|regmap_|'
    r'(?:devm_)?ioremap|iounmap|ioread|iowrite|read[bwlq]$|write[bwlq]$|'
    r'(?:devm_)?request_firmware|register_.*driver|driver_register$)')
bad = [line for line in symbols.splitlines()
       if line.split() and forbidden.search(line.split()[-1])]
all_symbols = subprocess.check_output(['nm', str(obj)], text=True)
if '__param_' in all_symbols:
    bad.append('module parameter found')
sections = subprocess.check_output(['readelf', '-p', '.modinfo', str(obj)], text=True)
if re.search(r'\balias=', sections):
    bad.append('module/device alias found')
if bad:
    raise SystemExit('OFFLINE AUDIT FAILED:\n' + '\n'.join(bad))
print('PASS: no forbidden hardware imports, device aliases or module parameters.')
print('Scope: symbol audit of this object; not a proof about arbitrary future changes.')
print(symbols.strip())
