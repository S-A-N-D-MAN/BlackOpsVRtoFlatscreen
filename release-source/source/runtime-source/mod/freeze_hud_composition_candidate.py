"""Freeze the reviewed, tested HUD prototype without deployment or a launch."""
from pathlib import Path
import datetime, hashlib, json, math, shutil, struct, subprocess, zipfile

mod = Path(__file__).resolve().parent
root = mod.parent
destination = mod / 'artifacts/experimental-hud-composition-v13'
if destination.exists():
    raise SystemExit(f'Refusing to overwrite {destination}')
build_log = mod / 'hud-composition-integration-build.log'
if '100% tests passed out of 39' not in build_log.read_text(encoding='utf-8-sig'):
    raise SystemExit('39 passing tests absent')
gpu_log = root / 'phase17/hud-staging-gpu-root-01.log'
if 'PASS checks=1036 failures=0 frames=32 exact_pixel_comparisons=1511424 retail_calls=0 xr_calls=0' not in gpu_log.read_text(encoding='utf-8-sig'):
    raise SystemExit('Parent GPU evidence absent')

def record(path, base):
    data = path.read_bytes()
    return {'path': path.relative_to(base).as_posix(), 'size': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}

def symbol_identity(dll, pdb):
    """Compare PE CodeView RSDS with the actual MSF PDB information stream."""
    image = dll.read_bytes()
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    if image[pe:pe+4] != b'PE\0\0':
        raise ValueError('PE signature')
    machine, sections = struct.unpack_from('<HH', image, pe+4)
    opt_size = struct.unpack_from('<H', image, pe+20)[0]
    opt = pe+24
    if machine != 0x14c or struct.unpack_from('<H', image, opt)[0] != 0x10b:
        raise ValueError('Expected PE32 x86')
    def rva_offset(rva):
        for index in range(sections):
            section = opt+opt_size+index*40
            virtual_size, virtual_address, raw_size, raw = struct.unpack_from('<IIII', image, section+8)
            if virtual_address <= rva < virtual_address+max(virtual_size, raw_size):
                return raw+rva-virtual_address
        raise ValueError('Unmapped PE debug RVA')
    debug_rva, debug_size = struct.unpack_from('<II', image, opt+96+6*8)
    debug = rva_offset(debug_rva)
    codeview = []
    for index in range(debug_size//28):
        kind, size, address, raw = struct.unpack_from('<IIII', image, debug+index*28+12)
        if kind == 2 and size >= 24 and image[raw:raw+4] == b'RSDS':
            codeview.append((image[raw+4:raw+20], struct.unpack_from('<I', image, raw+20)[0]))
    if len(codeview) != 1:
        raise ValueError('Expected one CodeView identity')
    data = pdb.read_bytes()
    if not data.startswith(b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0'):
        raise ValueError('Expected MSF 7 PDB')
    block, unused, block_count, directory_size, reserved, block_map = struct.unpack_from('<6I', data, 32)
    directory_blocks = struct.unpack_from('<'+'I'*math.ceil(directory_size/block), data, block_map*block)
    directory = b''.join(data[item*block:(item+1)*block] for item in directory_blocks)[:directory_size]
    stream_count = struct.unpack_from('<I', directory)[0]
    sizes = struct.unpack_from('<'+'I'*stream_count, directory, 4)
    cursor = 4+4*stream_count
    info = None
    for index, size in enumerate(sizes):
        count = 0 if size == 0xffffffff else math.ceil(size/block)
        blocks = struct.unpack_from('<'+'I'*count, directory, cursor)
        cursor += count*4
        if index == 1:
            info = b''.join(data[item*block:(item+1)*block] for item in blocks)[:size]
    if info is None or len(info) < 28:
        raise ValueError('PDB information stream missing')
    identity = (info[12:28], struct.unpack_from('<I', info, 8)[0])
    if codeview[0] != identity:
        raise ValueError('DLL/PDB GUID or age mismatch')
    return {'guid_bytes_le': identity[0].hex(), 'age': identity[1], 'matched': True}

symbol_match = symbol_identity(mod/'build/Release/d3d9.dll', mod/'build/Release/d3d9.pdb')
excluded = {'mod/src/input/command_rig_ledger.hpp', 'mod/src/input/command_rig_ledger.cpp',
            'mod/tests/command_rig_ledger_tests.cpp'}
sources = []
for directory in ['mod/src', 'mod/tests', 'mod/vendor', 'prototypes/vr-core/vendor',
                  'prototypes/vr-core/harness', 'prototypes/vr-core/include']:
    sources.extend(p for p in (root/directory).rglob('*') if p.is_file() and p.relative_to(root).as_posix() not in excluded)
for relative in ['mod/CMakeLists.txt', 'mod/build.ps1', 'mod/LICENSE', 'mod/PROVENANCE.md',
                 'mod/freeze_hud_composition_candidate.py', 'prototypes/vr-core/CMakeLists.txt']:
    sources.append(root/relative)
sources = sorted(set(sources), key=lambda p: p.relative_to(root).as_posix())
records = [record(p, root) for p in sources]
destination.mkdir(parents=True)
for name in ['d3d9.dll', 'd3d9.pdb', 'bovr-launch.exe', 'bovr-hud-staging-probe.exe',
             'bovr-hud-eye-command-tests.exe', 'bovr-hud-publication-tests.exe',
             'bovr-hud-pass-probe-tests.exe', 'bovr-hud-view-binding-tests.exe']:
    shutil.copy2(mod/'build/Release'/name, destination/name)
for path in [build_log, mod/'build/Testing/Temporary/LastTest.log', gpu_log, Path(__file__),
             root/'phase17/HUD-COMPOSITION-PROTOTYPE-PLAN.md', root/'phase17/HUD-COMPOSITION-IMPLEMENTATION.md',
             root/'phase20/DEVICE-REPLACEMENT-IMPLEMENTATION.md']:
    shutil.copy2(path, destination/path.name)
(destination/'source-lock.json').write_text(json.dumps({'root': 'BlackOpsVR', 'source_count': len(sources),
    'excluded_unintegrated_parent_sources': sorted(excluded), 'sources': records}, indent=2)+'\n', encoding='utf-8')
with zipfile.ZipFile(destination/'source-snapshot.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for source in sources:
        archive.write(source, source.relative_to(root).as_posix())
for source, expected in zip(sources, records):
    if record(source, root) != expected:
        raise SystemExit(f'Source changed during freeze: {source}')
dumpbin = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x86\dumpbin.exe')
imports = subprocess.run([str(dumpbin), '/imports', str(destination/'d3d9.dll')], check=True, capture_output=True, text=True).stdout
(destination/'imports.txt').write_text(imports, encoding='utf-8')
if any(line.strip().lower() == 'd3d9.dll' for line in imports.splitlines()):
    raise SystemExit('Unexpected proxy self import')
(destination/'README.md').write_text('''# Experimental native HUD composition V13

Default off. Set BOVR_HUD_COMPOSITION_PROBE=1 only with the existing identity-guarded XR adapter and supported non-MSAA fixture. The separate UI_PRODUCER_PROBE flag is unnecessary and remains associated with an unresolved startup exit.

39/39 CPU/ABI tests pass. Root's hidden D3D9Ex harness passed 1,036 checks across 32 frames and 1,511,424 RGB pixel comparisons, with no game/OpenXR calls. Matching DLL/PDB identity, launcher, source, tests and evidence are preserved. Read HUD-COMPOSITION-IMPLEMENTATION.md for the exact conservative command scope and pointer/storage ownership contract. Retail HUD visibility/readability and physical headset acceptance remain pending.

This candidate also contains V12 device recovery and root's V11c haptic diagnostics. V12's live vid_restart exited 0x8000DEAD before recovery traces; its cause and live restart acceptance remain unresolved. The parent-owned unintegrated command-rig ledger is excluded. No artifact was deployed and this freeze launched nothing. Earlier artifacts are unchanged.
''', encoding='utf-8')
result = {'recorded_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'artifact': destination.name,
          'dll_sha256': record(destination/'d3d9.dll', destination)['sha256'], 'symbols': symbol_match,
          'cpu_tests': 39, 'cpu_status': 'passed', 'source_files': len(sources),
          'parent_gpu_probe': {'passed': True, 'checks': 1036, 'frames': 32, 'rgb_comparisons': 1511424,
                               'retail_calls': 0, 'xr_calls': 0},
          'retail_hud_accepted': False, 'physical_headset_accepted': False,
          'live_device_recreation_accepted': False, 'ui_producer_startup_issue_resolved': False,
          'artifacts': [record(p, destination) for p in sorted(destination.iterdir()) if p.is_file()]}
(destination/'validation.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps({key: result[key] for key in ['artifact', 'dll_sha256', 'symbols', 'cpu_tests', 'source_files']}))
