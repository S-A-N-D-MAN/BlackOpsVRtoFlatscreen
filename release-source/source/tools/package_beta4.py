# SPDX-License-Identifier: GPL-3.0-only
"""Create a fresh public beta.4 from the accepted Foliage-Test2 and version-only launcher.

Explicit input receipts are read-only. No game launch, production install or
upload occurs. Private fixtures/evidence, historical private tools and symbols
are excluded; complete matching native/launcher source and licenses are kept.
"""
from pathlib import Path
import argparse, datetime, hashlib, importlib.util, json, re, shutil, sys, zipfile
sys.dont_write_bytecode = True
VERSION = '0.1.0-beta.4'
DLL = '6fd7edc61eca82276ff632eca6f21cc3bcca9db54af269ed5ebbe24f2f4b41d1'
BASE_MANIFEST = 'c4bb17022d7bc157dbf2b9f3ab0681038ef175430be89189b65a87e32e9e8d8b'
ENV = '40ec02c785c02b31f46c989209a1d17bb24e0a531cfcc9f1ab5a83612d775743'
CONFIG = '2c132a2a1a36002fed07341d4a279b6a815e745f67f42c7d7d7f10d3e646d03e'
PAUSE_FLAGS = {'BOVR_PRODUCER_PACING_WAIT': '1', 'BOVR_PRODUCER_PACING_DIAGNOSTIC': '1'}
DOCS = ('README.md','INSTALL.txt','REQUIREMENTS.txt','CONTROLS.txt','KNOWN-ISSUES.txt',
        'REPORTING-ISSUES.txt','FEEDBACK-TEMPLATE.txt','BUILD-SOURCE.md','CHANGELOG.md')

def need(ok, text):
    if not ok: raise RuntimeError(text)

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def encode(value): return (json.dumps(value,indent=2)+'\n').encode('utf-8')
def write(path,value): Path(path).write_bytes(encode(value))
def rows(root,prefix=''):
    return [{'path':prefix+p.relative_to(root).as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)}
            for p in sorted(root.rglob('*')) if p.is_file()]
def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
    need(sha(src)==sha(dst),'Copy changed: '+str(dst))

def run(a):
    for p in (a.work,a.base,a.native_result,a.launcher_result,a.live_proof):
        need(p.is_absolute() and p.exists(),'Absolute existing inputs required')
    release=a.work/'source/release';native=a.work/'source/runtime-source'
    spec=importlib.util.spec_from_file_location('public_release_helpers',release/'package_release.py')
    h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
    for p in (a.base,release,native,a.work/'published-launcher'):h.assert_tree_no_reparse(p,'Public packaging input')
    need(sha(a.base/'PAYLOAD-SHA256.txt')==BASE_MANIFEST,'Accepted Foliage-Test2 manifest changed')
    recorded={}
    for line in (a.base/'PAYLOAD-SHA256.txt').read_text(encoding='ascii').splitlines():
        digest,name=line.split('  ',1)
        need(name and ':' not in name and '\\' not in name and all(x not in ('','.','..')for x in name.split('/')) and name not in recorded,'Unsafe base manifest')
        recorded[name]=digest
    actual={r['path']:r['sha256']for r in rows(a.base)}
    need(set(actual)==set(recorded)|{'PAYLOAD-SHA256.txt'} and all(actual[k]==v for k,v in recorded.items()),'Accepted Foliage-Test2 inventory changed')
    need(sha(a.base/'payload/d3d9.dll')==DLL,'Native runtime differs from accepted Foliage-Test2')
    native_rows=rows(native,'runtime-source/')
    need(len(native_rows)==1058 and native_rows==read(a.base/'source/RUNTIME-SOURCE-INVENTORY.json'),'Exact complete accepted native source required')
    for p in native.rglob('*'):
        if p.is_file():h.runtime_source_name(native,p)
    nr=read(a.native_result)
    need(nr.get('status')=='BUILD_AND_TEST_PASS' and nr.get('passed')==nr.get('total')==178 and nr.get('dll_sha256')==DLL,'Exact passing178 native receipt required')
    for key,digest in (('tests_xml','tests_xml_sha256'),('ctest_log','ctest_log_sha256'),('test_plan','test_plan_sha256'),('build_receipt','build_receipt_sha256')):
        need(sha(nr[key])==nr[digest],'Native receipt input changed: '+key)
    nb=read(nr['build_receipt']);freeze=read(nb['source_freeze'])
    need(sha(nb['source_freeze'])==nb['source_freeze_sha256'] and freeze['files']==native_rows and nb['dll_sha256']==DLL,'Native source/build identity differs')
    live=read(a.live_proof)
    need(live.get('status')=='PASS' and live.get('root_accepted')is True and live.get('dll_sha256')==DLL
         and live.get('native_result_sha256')==sha(a.native_result) and live.get('physical_headset_accepted')is False,'Root accepted exact Foliage-Test2 evidence required')
    need(live.get('config_default_sha256')==CONFIG and live.get('native_tests')==178,'Accepted foliage profile/test identity differs')
    need(set(live['case_hashes'])==set(live['cases']) and live['cases'],'Complete accepted case hashes required')
    for path,digest in live['case_hashes'].items():need(sha(path)==digest,'Accepted live evidence changed')
    tests=read(a.launcher_result)
    need(tests.get('checks')==139 and tests.get('failures')==0 and tests.get('gameLaunched')is False and tests.get('retailWrites')==0,'Fresh139 launcher checks required')
    skipped=[r for r in tests['results']if r.get('skipped')]
    need(len(skipped)==1 and skipped[0].get('name')=='multiplayer executable identity' and skipped[0].get('reason')=='BlackOpsMP.exe not installed','Unexpected launcher fixture accounting')
    need(tests['testedArtifacts']=={'version':VERSION,'nativeDllSha256':DLL,'launcherCoreSha256':sha(a.work/'published-launcher/BlackOpsVR.Core.dll'),'runtimeEnvSha256':ENV,'configDefaultSha256':CONFIG},'Fresh launcher artifact binding differs')
    for name,digest in (('runtime-env.json',ENV),('config-default.cfg',CONFIG)):
        need(sha(release/'payload'/name)==digest==sha(a.base/'payload'/name),'Payload setting changed: '+name)
    env=read(release/'payload/runtime-env.json')
    need('BOVR_BORROWED_NORMAL_GATE'not in env and all(env.get(k)==v for k,v in PAUSE_FLAGS.items()),'Accepted pacing/gate flags differ')
    need(env.get('BOVR_STEREO_LOD_FOV_COMPENSATION')=='1' and env.get('BOVR_STEREO_LOD_PROBE')==env.get('BOVR_FOLIAGE_LOD_PROBE')=='0','Accepted foliage correction/diagnostic flags differ')
    for k,v in env.items():
        need(isinstance(v,str) and not k.startswith('SIMXR_') and k not in ('XR_RUNTIME_JSON','LOCALAPPDATA','BOVR_TRACE_FILE') and not re.search(r'(?i)([a-z]:[\\/]|/users/|/home/)',v),'Machine/test override in public environment')
    # Only the two public version literals may change launcher implementation.
    code=[]
    for directory in h.RELEASE_LAUNCHER_DIRS:
        for p in sorted((release/directory).rglob('*')):
            if not p.is_file()or {'bin','obj'}&set(p.relative_to(release).parts):continue
            h.release_source_name(p);rel=p.relative_to(release).as_posix();old=(a.base/'source/release'/rel).read_bytes();current=p.read_bytes()
            if rel in ('launcher/BlackOpsVR.Launcher.csproj','launcher/Core/BuildIdentity.cs'):
                need(old.count(b'0.1.0-beta.3')==1 and current==old.replace(b'0.1.0-beta.3',VERSION.encode()),'Version-only launcher source change exceeded')
            else:need(current==old,'Launcher behavior/test source changed: '+rel)
            code.append(p)
    published=[]
    for p in sorted((a.work/'published-launcher').rglob('*')):
        if not p.is_file()or p.suffix.casefold()=='.pdb':continue
        rel=p.relative_to(a.work/'published-launcher');need(p.suffix.casefold()in('.dll','.exe','.json')and h.safe_components(rel),'Unexpected publish file')
        published.append(p)
    publish_names={p.relative_to(a.work/'published-launcher').as_posix()for p in published}
    need({'BlackOpsVR.exe','BlackOpsVR.dll','BlackOpsVR.Core.dll','coreclr.dll','hostfxr.dll','hostpolicy.dll'}<=publish_names,'Incomplete self-contained launcher')
    for name in ('BlackOpsVR.exe','BlackOpsVR.dll','BlackOpsVR.Core.dll'):
        h.assert_binary_privacy(a.work/'published-launcher'/name,name,reject_absolute_pdb=name!='BlackOpsVR.exe')
    h.assert_binary_privacy(a.base/'payload/d3d9.dll','Accepted runtime',reject_absolute_pdb=True)
    need(VERSION.encode('utf-16-le')in(a.work/'published-launcher/BlackOpsVR.Core.dll').read_bytes(),'Public Core version absent')
    out=a.work/'output'/('BlackOpsVR-v'+VERSION+'-Patreon-Release');need(not out.exists(),'Fresh public output required')
    out.mkdir(parents=True);folder=out/'BlackOpsVR';folder.mkdir()
    for p in published:copy(p,folder/p.relative_to(a.work/'published-launcher'))
    for name in DOCS:copy(release/name,folder/name)
    for name in ('d3d9.dll','runtime-env.json','config-default.cfg'):copy(a.base/'payload'/name,folder/'payload'/name)
    payload={name:sha(folder/'payload'/name)for name in ('d3d9.dll','runtime-env.json','config-default.cfg')};write(folder/'payload/manifest.json',payload)
    shutil.copytree(native,folder/'source/runtime-source')
    for p in code:copy(p,folder/'source/release'/p.relative_to(release))
    for name in DOCS+('package_release.py',):copy(release/name,folder/'source/release'/name)
    for name in ('runtime-env.json','config-default.cfg'):copy(release/'payload'/name,folder/'source/release/payload'/name)
    write(folder/'source/release/payload/manifest.json',payload)
    copy(Path(__file__).resolve(),folder/'source/tools/package_beta4.py')
    shutil.copytree(a.base/'licenses',folder/'licenses');copy(a.base/'LICENSE.txt',folder/'LICENSE.txt')
    copy(a.base/'THIRD-PARTY-NOTICES.txt',folder/'THIRD-PARTY-NOTICES.txt')
    (folder/'THIRD-PARTY-NOTICES.txt').write_text((folder/'THIRD-PARTY-NOTICES.txt').read_text().replace('0.1.0-beta.3',VERSION),encoding='utf-8')
    write(folder/'source/RUNTIME-SOURCE-INVENTORY.json',native_rows)
    release_rows=[r for r in rows(folder/'source')if not r['path'].startswith('runtime-source/')and not r['path'].endswith('SOURCE-INVENTORY.json')]
    write(folder/'source/RELEASE-SOURCE-INVENTORY.json',release_rows)
    publish_rows=[{'path':p.relative_to(a.work/'published-launcher').as_posix(),'bytes':p.stat().st_size,'sha256':sha(p)}for p in published]
    write(folder/'DOTNET-RUNTIME-INVENTORY.json',publish_rows)
    info={'version':VERSION,'public_release':True,'private_test':False,'native_dll_sha256':DLL,'native_source_files':1058,
          'native_source_inventory_sha256':sha(folder/'source/RUNTIME-SOURCE-INVENTORY.json'),'release_source_inventory_sha256':sha(folder/'source/RELEASE-SOURCE-INVENTORY.json'),
          'launcher_core_sha256':sha(folder/'BlackOpsVR.Core.dll'),'payload_manifest_sha256':sha(folder/'payload/manifest.json'),'runtime_env_sha256':ENV,'config_default_sha256':CONFIG,
          'native_tests_passed':178,'native_test_attempts':nr['total_actual_attempts'],'native_retry_note':'All 178 native tests passed first attempt; no retry used','launcher_checks':139,'launcher_failures':0,'launcher_unavailable_fixture_count':1,
          'launcher_unavailable_fixture':'BlackOpsMP.exe not installed; competitive multiplayer unsupported','launcher_change':'Version literals only; behavior unchanged; exact accepted foliage payload/defaults retained',
          'enabled_scheduling_flags':PAUSE_FLAGS,'normal_profile_gate_enabled':False,'dotnet_runtime':'8.0.31','deployment':'ordinary self-contained win-x64 folder','native_x86':True,
          'native_result_sha256':sha(a.native_result),'launcher_result_sha256':sha(a.launcher_result),'accepted_live_proof_sha256':sha(a.live_proof),'base_private_manifest_sha256':BASE_MANIFEST,
          'simulator_projection_fps':live['simulator_projection_fps'],'simulator_interval_ms':live['measured_interval_ms'],'simulator_successful_projection_frames':live['successful_projection_frames'],
          'recorded_stereo_pairs':live['recorded_stereo_pairs'],'enabled_foliage_correction':True,'foliage_diagnostics_enabled':False,'mip_filter_mode':1,
          'live_cases':['River checkpoint: 36 recorded stereo pairs with per-eye application and restoration','Targeted High-resolution simulator timing: 1809 successful projections / 30.031 seconds; no empty, reused or failed-end frames'],
          'limits':['Newest foliage changes have targeted simulator validation; physical-headset acceptance and a new full campaign playthrough were not performed','FPS covers one short fixed High-resolution scene, not headset refresh or every scene','Transport CPU-stage diagnostics were disabled; no stage-cost or controlled beta.3-to-beta.4 FPS improvement claim','Some graphical and animation issues can remain; runtime-specific startup and duplicate bullet-mark reports remain open'],
          'physical_headset_tested':False,'physical_headset_accepted':False,'all_campaign_performance_verified':False,'included_game_files':False,'included_private_evidence':False}
    write(folder/'BUILD-INFO.json',info)
    (folder/'START-HERE.txt').write_text('BLACK OPS VR '+VERSION+' - PATREON BETA\n\nExtract the ENTIRE ZIP into a NEW folder. Keep files together.\nSave and close Black Ops, then open this release\'s BlackOpsVR.exe.\nSelect the same Steam game folder to retain separate VR progress/settings.\nChoose Install / Update, connect headset/controllers, then Launch VR.\nChoose No if offered optimal/reset graphics.\n\nRead README.md, INSTALL.txt, CONTROLS.txt and KNOWN-ISSUES.txt.\nMatching source and third-party licenses are included.\n',encoding='utf-8')
    (folder/'SOURCE.txt').write_text('Complete native source: source/runtime-source.\nVersion-only rebuilt launcher and checks: source/release.\nPublic packaging tool: source/tools/package_beta4.py.\nSee BUILD-SOURCE.md, LICENSE.txt, THIRD-PARTY-NOTICES.txt and licenses/.\n',encoding='utf-8')
    (folder/'VALIDATION.txt').write_text('BLACK OPS VR '+VERSION+'\n\n178 native tests passed first attempt; no retry used.\n139 fresh beta.4 launcher checks, zero failures; one unavailable MP identity fixture.\nExact accepted native runtime and complete matching source retained.\nRiver-checkpoint review:36 recorded stereo pairs.\nTargeted High-resolution simulator check:60.2378 successful projection FPS over30.031seconds,1809frames,no empty/reused/failed-end frames.\nOptional transport CPU-stage costs unavailable because that diagnostic was disabled.\nNewest foliage changes are simulator-validated; no fresh physical-headset acceptance, full campaign playthrough, all-weapon or all-scene FPS guarantee.\nBUILD-INFO.json binds artifacts and validation scope.\n',encoding='utf-8')
    # Source-level privacy check excludes historical private tools entirely.
    forbidden={'players','save','saves','logs','evidence','validation-beta3','bin','obj','__pycache__','.git'}
    personal=re.compile(r'(?i)[a-z]:[\\/]users[\\/][^\\/\s\"\']+')
    for p in folder.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(folder);need(not forbidden&{x.casefold()for x in rel.parts}and p.suffix.casefold()not in('.pdb','.svg','.log','.dmp','.7z','.zip'),'Private/generated artifact in public output: '+rel.as_posix())
        need(p.name.casefold()not in('blackops.exe','blackopsmp.exe','binkw32.dll','steam_api.dll','settings.json'),'Game/private file in public output')
        if p.suffix.casefold()in('.md','.txt','.json','.py','.ps1','.cs','.csproj','.xaml','.hpp','.cpp','.inc'):
            need(personal.search(p.read_text(encoding='utf-8-sig'))is None,'Personal machine path in public file: '+rel.as_posix())
    file_rows=rows(folder);(folder/'PAYLOAD-SHA256.txt').write_text(''.join(r['sha256']+'  '+r['path']+'\n'for r in file_rows),encoding='ascii')
    file_rows=rows(folder)
    need(rows(folder/'source/runtime-source','runtime-source/')==native_rows,'Packaged native source changed')
    need(sha(folder/'payload/d3d9.dll')==DLL and sha(folder/'payload/runtime-env.json')==ENV and sha(folder/'payload/config-default.cfg')==CONFIG,'Packaged accepted payload changed')
    archive=out/('BlackOpsVR-v'+VERSION+'-Patreon-Release.zip')
    h.zip_entries(archive,[(folder/r['path'],'BlackOpsVR/'+r['path'])for r in file_rows])
    with zipfile.ZipFile(archive)as z:
        expected={'BlackOpsVR/'+r['path']:r for r in file_rows};need(set(z.namelist())==set(expected),'ZIP entry inventory differs')
        for name,row in expected.items():need(hashlib.sha256(z.read(name)).hexdigest()==row['sha256'],'ZIP entry hash differs: '+name)
    result={'status':'PUBLIC_BETA4_CREATED_VERIFIED_NOT_UPLOADED','version':VERSION,'package':str(folder),'zip':str(archive),'zip_sha256':sha(archive),'zip_bytes':archive.stat().st_size,'files':len(file_rows),
            'package_manifest_sha256':sha(folder/'PAYLOAD-SHA256.txt'),'payload_manifest_sha256':sha(folder/'payload/manifest.json'),'build':info,'source_and_payload_verified':True,'privacy_verified':True,'zip_all_entry_hashes_verified':True,'production_installed':False,'game_launched':False,'uploaded':False}
    write(out/'RELEASE-MANIFEST.json',result)
    (out/('BlackOpsVR-v'+VERSION+'-SHA256.txt')).write_text(result['zip_sha256']+'  '+archive.name+'\n',encoding='ascii')
    print(json.dumps(result,indent=2));return 0

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('work','base','native-result','launcher-result','live-proof'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    try:return run(a)
    except (OSError,RuntimeError,ValueError,KeyError,UnicodeError)as e:
        print(json.dumps({'status':'REFUSED','reason':str(e),'automatic_cleanup':False,'installed':False,'uploaded':False}));return 1
if __name__=='__main__':sys.exit(main())
