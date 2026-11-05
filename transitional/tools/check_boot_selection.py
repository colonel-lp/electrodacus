#!/usr/bin/env python3
"""Check the actual ESP32 ELF: Arduino Update must call the guarded selector."""
import argparse
import re
import subprocess
from pathlib import Path


def check(elf, objdump):
    assembly = subprocess.check_output([str(objdump), '-d', '-C', str(elf)], text=True)
    functions = {}
    matches = list(re.finditer(r'^[0-9a-f]+ <(.+)>:$', assembly, re.M))
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(assembly)
        functions[match[1]] = assembly[match.end():end]
    update = functions.get('UpdateClass::_verifyEnd()', '')
    wrapper = functions.get('__wrap_esp_ota_set_boot_partition', '')
    if '<__wrap_esp_ota_set_boot_partition>' not in update:
        raise ValueError('Arduino Update does not call the guarded boot selector in this ELF')
    if '<esp_ota_set_boot_partition>' in update:
        raise ValueError('Arduino Update bypasses boot validation')
    if 'ella::validateBootSelection(' not in wrapper or '<esp_ota_set_boot_partition>' not in wrapper:
        raise ValueError('Boot selector wrapper is incomplete in this ELF')
    print('Verified ESP32 ELF: Arduino Update uses validation before SDK boot selection')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--objdump', type=Path)
    args = parser.parse_args()
    candidates = list((Path.home() / '.platformio/packages').glob('toolchain-*/bin/xtensa-esp32-elf-objdump'))
    objdump = args.objdump or (candidates[0] if candidates else None)
    if objdump is None:
        parser.error('Supply --objdump from the pinned ESP32 toolchain')
    check(args.elf, objdump)
