"""Read-only quit-registration evidence from the pinned retail SP image."""
import hashlib
import json
import struct
from pathlib import Path
import capstone
import pefile

root = Path(__file__).resolve().parents[3]
exe = Path(json.loads((root / 'phase1/installation-lock.json').read_text())['root']) / 'BlackOps.exe'
data = exe.read_bytes()
assert hashlib.sha256(data).hexdigest() == 'db00603ddea3ee201c5870c7bb27e27fc569528839e92a3f78689c256dd73ff3'
pe = pefile.PE(data=data)
base = pe.OPTIONAL_HEADER.ImageBase
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
text_section = next(s for s in pe.sections if s.Name.startswith(b'.text'))
code = text_section.get_data()
text_base = base + text_section.VirtualAddress
report = []
for needle in (b'quit\0', b'quitting...\n\0'):
    offset = data.find(needle)
    while offset >= 0:
        va = base + pe.get_rva_from_offset(offset)
        report.append(f'string {needle!r} at {va:#x}')
        encoded = struct.pack('<I', va)
        position = code.find(encoded)
        while position >= 0:
            address = text_base + position
            report.append(f'candidate operand reference {address:#x}')
            start = address - 24
            report += [f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}'
                       for i in decoder.disasm(pe.get_data(start-base, 85), start)]
            position = code.find(encoded, position+1)
        offset = data.find(needle, offset+len(needle))
for name, address, length in [('aligned_quit_registration', 0x82d041, 0x29), ('Com_Quit_f', 0x5df2b0, 0x160),
                              ('Cmd_AddCommandInternal', 0x661400, 0x4c), ('Cbuf_AddText', 0x49b930, 0xb0)]:
    report.append(f'\n{name} {address:#x}')
    report += [f'{i.address:#x} {i.bytes.hex():24} {i.mnemonic} {i.op_str}'
               for i in decoder.disasm(pe.get_data(address-base, length), address)]
output = root / 'phase3/quit-gate-mapping.txt'
output.write_text('\n'.join(report)+'\n')
print(output)
