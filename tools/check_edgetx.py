"""Offline checks on an EdgeTX build for the Tango 2: vector table at
0x08020000, sane SP and reset vector, size, and nothing in the RAM the radio
images own (0x20015000+). usage: check_edgetx.py <firmware.bin> <firmware.elf>"""
import struct, sys
from elftools.elf.elffile import ELFFile
binf, elff = sys.argv[1], sys.argv[2]
d = open(binf, 'rb').read()
sp, rv = struct.unpack_from('<II', d, 0)
vec = struct.unpack_from('<98I', d, 0)
ok = True
def check(cond, msg):
    global ok
    print(('ok   ' if cond else 'FAIL ') + msg)
    ok = ok and cond
check(len(d) <= 0xA0000, 'size %d bytes (%.1f%% of 640 KB)' % (len(d), 100.0 * len(d) / 0xA0000))
check(sp in (0x10010000,) or 0x20000000 < sp <= 0x20015000, 'initial SP 0x%08X' % sp)
check(rv & 1 and 0x08020000 < rv < 0x080C0000, 'reset vector 0x%08X' % rv)
check(all(v == 0 or 0x08020000 <= v < 0x080C0000 for v in vec[1:]), 'all vectors inside 0x08020000-0x080BFFFF')
with open(elff, 'rb') as f:
    e = ELFFile(f)
    for s in e.iter_sections():
        a, n = s['sh_addr'], s['sh_size']
        if not (s['sh_flags'] & 2) or n == 0:   # SHF_ALLOC
            continue
        if 0x20000000 <= a < 0x20020000:
            check(a + n <= 0x20015000, 'RAM section %-14s 0x%08X-0x%08X below 0x20015000' % (s.name, a, a + n))
        if 0x08000000 <= a < 0x08100000:
            check(0x08020000 <= a and a + n <= 0x080C0000, 'flash section %-12s 0x%08X-0x%08X' % (s.name, a, a + n))
    syms = {x.name: x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
    check(syms.get('_heap_end', 0) <= 0x20015000, 'heap end 0x%08X' % syms.get('_heap_end', 0))
sys.exit(0 if ok else 1)
