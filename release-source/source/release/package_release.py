"""Shared path, privacy, copy, source and license safeguards for Black Ops VR beta.4 packaging.

All paths are explicit. The output must be new; existing releases are never
overwritten. No game executable, saves, private logs or machine settings enter
the archive. Run the native and launcher checks and supply their exact outputs.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,re,shutil,zipfile

VERSION='0.1.0-beta.4'
EXPECTED_LAUNCHER_CHECKS=139
PRODUCT='BlackOpsVR-v'+VERSION
HERE=Path(__file__).resolve().parent

RUNTIME_SOURCE_EXTENSIONS={
    '.adoc','.bazel','.c','.cmake','.cpp','.def','.dict','.dox','.expected',
    '.expsym','.filters','.gradle','.h','.hpp','.html','.in','.inc','.inl',
    '.json','.license','.makefile','.map','.md','.nuspec','.pl','.pom',
    '.props','.ps1','.py','.rc','.sh','.targets','.txt','.vcxproj','.xml',
    '.yml',
}
RUNTIME_SOURCE_NAMES={
    '.clang-format','.clang-tidy','.editorconfig','.gitattributes',
    '.git-blame-ignore-revs','.gitignore','.java-version','.mailmap',
    'asan-wrapper','authors','copying','dep5','license','makefile','notice',
}
PRIVATE_PARTS={'bin','obj','players','__pycache__','.git','.hg','.svn'}
SENSITIVE_NAME=re.compile(
    r'(^|[._-])(credential|credentials|id_rsa|id_ed25519|private[_-]?key|secret|secrets)([._-]|$)',
    re.IGNORECASE,
)
RELEASE_DOCS=('INSTALL.txt','REQUIREMENTS.txt','CONTROLS.txt','KNOWN-ISSUES.txt','REPORTING-ISSUES.txt','FEEDBACK-TEMPLATE.txt','BUILD-SOURCE.md','CHANGELOG.md')
RELEASE_LAUNCHER_DIRS=('launcher','launcher-tests','launch-smoke-host')
RELEASE_LAUNCHER_EXTENSIONS={'.cs','.csproj','.xaml','.manifest'}
RELEASE_PAYLOAD_SOURCE=('runtime-env.json','config-default.cfg','manifest.json')
LICENSE_BASENAMES={'license','license.txt','copying','copying.txt','notice','notice.txt','third-party-notices.md','third-party-notices.txt'}
DOTNET_LICENSE_HASHES={
    ('microsoft.netcore.app.runtime.win-x64','8.0.31','LICENSE.TXT'):
        'd7a68596ab69b06f51ca278a6545148e4269a9381c26d597c13df5d88e08cf5b',
    ('microsoft.netcore.app.runtime.win-x64','8.0.31','THIRD-PARTY-NOTICES.TXT'):
        'b60b2912da28eaa6518593c9e2efb5334ee062d3c42e80d8fdfa806b3dc52977',
    ('microsoft.windowsdesktop.app.runtime.win-x64','8.0.31','LICENSE'):
        'a89886665765362eb77e0f8e26602c924520041d1711b2eedc136434fe4d01ab',
}
BINARY_PRIVATE_FRAGMENTS=(
    'c:\\users\\','c:/users/','pimax-yangyoung-r93-20260919',
    'blackopsvr\\phase20','blackopsvr/phase20',
)

def safe_components(relative_path):
    for component in relative_path.parts:
        lowered=component.lower()
        if lowered=='.env' or lowered.startswith('.env.') or SENSITIVE_NAME.search(lowered):
            return False
    return True

def is_distributable_license(path,relative_path):
    lowered=path.name.lower()
    in_licenses=any(part.lower()=='licenses' for part in relative_path.parts)
    return lowered in LICENSE_BASENAMES or path.suffix.lower()=='.license' or (
        in_licenses and path.suffix.lower() in {'.txt','.md','.license'}
    )

def checked_relative(root,path,label):
    relative_path=path.relative_to(root)
    relative=relative_path.as_posix()
    root_resolved=root.resolve(strict=True)
    root_stat=root.stat(follow_symlinks=False)
    need(not (getattr(root_stat,'st_file_attributes',0)&0x400),
         label+' root uses a reparse point: '+str(root))
    current=root
    for part in relative_path.parts:
        current=current/part
        stat=current.stat(follow_symlinks=False)
        need(not (getattr(stat,'st_file_attributes',0)&0x400),
             label+' path uses a reparse point: '+relative)
    need(path.resolve(strict=True).is_relative_to(root_resolved),
         label+' resolves outside its root: '+relative)
    return relative_path,relative

def assert_tree_no_reparse(root,label):
    need(root.is_dir(),label+' root is missing: '+str(root))
    root_stat=root.stat(follow_symlinks=False)
    need(not (getattr(root_stat,'st_file_attributes',0)&0x400),
         label+' root uses a reparse point: '+str(root))
    for entry in root.rglob('*'):
        stat=entry.stat(follow_symlinks=False)
        need(not (getattr(stat,'st_file_attributes',0)&0x400),
             label+' contains a reparse point: '+entry.relative_to(root).as_posix())

def runtime_source_name(root,path):
    relative_path,relative=checked_relative(root,path,'Runtime source')
    sdk_template=('prototypes/vr-core/vendor/waw-beta1/third_party/openxr-sdk/'
                  '.azure-pipelines/nuget/NugetTemplate/build/native/')
    source_template=relative in {sdk_template+'OpenXR.Loader.props',sdk_template+'OpenXR.Loader.targets'}
    parts={part.lower() for part in relative_path.parts}
    need(source_template or not (parts & (PRIVATE_PARTS|{'build'})),
         'Generated or private files in runtime source: '+relative)
    lowered=path.name.lower()
    need(safe_components(relative_path),
         'Potential credential or secret in runtime source: '+relative)
    need(path.suffix.lower() in RUNTIME_SOURCE_EXTENSIONS or lowered in RUNTIME_SOURCE_NAMES,
         'Runtime source type is not explicitly allowed: '+relative)
    return 'runtime-source/'+relative

def release_source_name(path):
    relative_path,relative=checked_relative(HERE,path,'Release source')
    lowered=path.name.lower()
    need(safe_components(relative_path),
         'Potential credential or secret in release source: '+relative)
    parts=relative_path.parts
    lower_parts=tuple(part.lower() for part in parts)
    fixed={name.lower() for name in RELEASE_DOCS+('package_release.py',)}
    payload={'payload/'+name.lower() for name in RELEASE_PAYLOAD_SOURCE}
    if relative.lower() in fixed or relative.lower() in payload:
        return 'release/'+relative
    need(parts and lower_parts[0] in RELEASE_LAUNCHER_DIRS,
         'Unexpected release source path: '+relative)
    need(not (set(lower_parts)&PRIVATE_PARTS) and 'build' not in lower_parts,
         'Generated or private file in release source: '+relative)
    need(path.suffix.lower() in RELEASE_LAUNCHER_EXTENSIONS,
         'Unexpected launcher source type: '+relative)
    return 'release/'+relative

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def assert_binary_privacy(path,label,reject_absolute_pdb=False):
    data=path.read_bytes()
    views=(data.decode('latin-1',errors='ignore').lower(),
           data.decode('utf-16-le',errors='ignore').lower())
    for marker in BINARY_PRIVATE_FRAGMENTS:
        need(all(marker not in view for view in views),
             label+' exposes a private build path: '+marker)
    absolute_pdb=re.compile(r'[a-z]:[\\/][^\x00\r\n]{0,512}\.pdb\b',re.IGNORECASE)
    if reject_absolute_pdb:
        need(all(absolute_pdb.search(view) is None for view in views),
             label+' exposes an absolute PDB path')

def need(ok,message):
    if not ok:raise RuntimeError(message)

def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
    need(sha(source)==sha(target),'Copy verification failed: '+target.name)

def encode(obj):return (json.dumps(obj,indent=2)+'\n').encode('utf-8')

def zip_entries(path,entries):
    names=[]
    with zipfile.ZipFile(path,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for source,name in sorted(entries,key=lambda v:v[1]):
            need(not source.is_symlink() and source.is_file(),'Invalid source entry: '+name)
            archive_parts=name.split('/')
            need(name and not name.startswith('/') and not name.endswith('/') and '\\' not in name
                 and ':' not in name and all(part not in {'','.','..'} for part in archive_parts),
                 'Invalid archive name: '+name)
            names.append(name);z.write(source,name)
    need(len(names)==len(set(n.lower() for n in names)),'Duplicate archive names')
    with zipfile.ZipFile(path) as z:
        need(z.testzip() is None,'ZIP CRC verification failed')
        need(all(((entry.external_attr>>16)&0o170000)!=0o120000 for entry in z.infolist()),
             'ZIP contains a symbolic-link entry')

def main():
    raise SystemExit('Use source/tools/package_beta4.py with absolute paths and the beta.4 review schema; this module supplies shared packaging safety helpers only.')

if __name__ == '__main__':
    main()
