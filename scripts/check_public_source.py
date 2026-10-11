"""Fail before sharing ROMs, generated game data, binary artifacts or secrets.

This is an additional publication guard, not a legal or perfect secret scanner.
Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
"""
import argparse,json,re,subprocess,sys
from pathlib import Path,PurePosixPath
# Preserve a short Windows checkout/junction path for deeply nested notices.
ROOT=Path(__file__).absolute().parent.parent
TEXT_SUFFIXES={'.c','.h','.py','.md','.txt','.json','.xml','.gradle','.properties','.lockfile','.map','.yml','.yaml'}
NAMES={'LICENSE','.gitignore','.gitattributes'}
PRIVATE={'private','generated','sandbox','source','input','logs','captures','screenshots','artifacts','.local','build','.gradle','.cxx','reference'}
PATTERNS=[re.compile(rb'gh[pousr]_[A-Za-z0-9]{25,}'),re.compile(rb'github_pat_[A-Za-z0-9_]{20,}'),re.compile(rb'AKIA[0-9A-Z]{16}'),re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),re.compile(rb'(?i)[A-Z]:[\\/]Users[\\/]'),re.compile(rb'\b2G0YC[0-9A-Z]{8}\b')]
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args])
def check(name,data,mode=None):
 p=PurePosixPath(name)
 if any(x.lower() in PRIVATE for x in p.parts):raise ValueError('Private/build directory: '+name)
 if p.suffix.lower() not in TEXT_SUFFIXES and p.name not in NAMES:raise ValueError('Unapproved text-source extension: '+name)
 if len(data)>2_000_000:raise ValueError('Unexpectedly large source: '+name)
 if b'\0' in data:raise ValueError('Binary file: '+name)
 data.decode('utf-8-sig')
 if mode and mode not in ['100644','100755']:raise ValueError('Symlink/submodule/unsupported entry: '+name)
 if any(x.search(data) for x in PATTERNS):raise ValueError('Credential or private identifier detected in '+name)
 if re.search(rb'(?i)(?:const\s+)?(?:unsigned\s+char|uint8_t)\s+\w+\s*\[\s*\d{4,}\s*\]\s*=\s*\{',data):raise ValueError('Large embedded byte array needs review: '+name)
def tree(ref):
 count=0
 for raw in git('ls-tree','-rz',ref).split(b'\0'):
  if not raw:continue
  metadata,name=raw.split(b'\t',1);mode,kind,oid=metadata.decode().split();name=name.decode()
  if kind!='blob':raise ValueError('Only text blobs may be published: '+name)
  check(name,git('cat-file','blob',oid),mode);count+=1
 return count
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tracked',action='store_true');p.add_argument('--history',action='store_true');a=p.parse_args();count=0
 if a.tracked:
  for name in git('ls-files','-z').decode().split('\0'):
   if name:check(name,(ROOT/name).read_bytes());count+=1
 else:
  for path in ROOT.rglob('*'):
   if path.is_file() and '.git' not in path.relative_to(ROOT).parts:
    check(path.relative_to(ROOT).as_posix(),path.read_bytes());count+=1
 revisions=[]
 if a.history:
  revisions=git('rev-list','--all').decode().splitlines()
  for ref in revisions:tree(ref)
 if not count:raise ValueError('Empty source tree')
 print(json.dumps({'source_only_check_passed':True,'text_files':count,'history_commits_checked':len(revisions),'rom_or_binary_assets_allowed':False}))
if __name__=='__main__':
 try:main()
 except (ValueError,UnicodeError,subprocess.CalledProcessError) as e:print(str(e),file=sys.stderr);raise SystemExit(1)
