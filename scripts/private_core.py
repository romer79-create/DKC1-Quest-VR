"""Generate/build only on the owner's machine; no ROM copying or device access.

Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
LOCK=json.loads((ROOT/'upstream.lock.json').read_text(encoding='utf-8'))
def run(args,env=None):subprocess.run([str(x) for x in args],check=True,env=env)
def verify(upstream,rom):
 for folder,key in [(upstream,'dkc1recomp'),(upstream/'snesrecomp','snesrecomp')]:
  head=subprocess.check_output(['git','-C',str(folder),'rev-parse','HEAD'],text=True).strip()
  if head!=LOCK[key]['commit']:raise ValueError('Upstream revision differs from upstream.lock.json')
 if rom.is_relative_to(ROOT):raise ValueError('Keep the ROM outside this repository, including ignored folders')
 expected=LOCK['supported_rom']
 if rom.stat().st_size!=expected['bytes'] or hashlib.sha256(rom.read_bytes()).hexdigest()!=expected['sha256']:
  raise ValueError('Only the verified headerless USA v1.0 ROM is supported')
def prepare(upstream,work):
 config=work/'config';config.mkdir(parents=True,exist_ok=True)
 for source in (upstream/'recomp').rglob('*'):
  if source.is_file() and source.suffix in ['.cfg','.h','.toml']:
   destination=config/source.relative_to(upstream/'recomp');destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
 text=(upstream/'snesrecomp/runner/src/snes/ppu.c').read_text(encoding='utf-8')
 changes=[('#include "ppu.h"','#include "ppu.h"\n/* Passive host capture taps; original drawing behavior retained. */\nextern void sylar_capture_bg_line(Ppu *,unsigned,unsigned);\nextern void sylar_capture_final_line(Ppu *,unsigned);\nextern void sylar_relief_obj_pixel(Ppu *,unsigned,int,unsigned,uint16_t);'),
 ('  if (!sub)\n    PpuWriteOverlayRenderLine(ppu, source, y);','  if (!sub) {\n    PpuWriteOverlayRenderLine(ppu, source, y);\n    sylar_capture_bg_line(ppu,(unsigned)source,y);\n  }'),
 ('    } else {\n      ppu_draw_whole_line_legacy(ppu, line);\n    }\n  }\n}','    } else {\n      ppu_draw_whole_line_legacy(ppu, line);\n    }\n    sylar_capture_final_line(ppu,(unsigned)line);\n  }\n}'),
 ('                dst[0] = z + pixel;','                dst[0] = z + pixel;\n                sylar_relief_obj_pixel(ppu,(unsigned)line,col+x+px,(unsigned)slot,(uint16_t)(z+pixel));')]
 for old,new in changes:
  if text.count(old)!=1:raise ValueError('Pinned PPU anchor mismatch; stop instead of patching another revision')
  text=text.replace(old,new,1)
 (work/'ppu_capture.c').write_text(text,encoding='utf-8',newline='\n')
def objects(commands,work):
 result=[]
 for item in json.loads(commands.read_text(encoding='utf-8')):
  if Path(item['file']).name in ['headless_host.c','desktop_sram.c','ppu.c']:continue
  if '-fPIC' not in item['command'] or 'aarch64' not in item['command']:raise ValueError('Original Android PIC objects required')
  p=Path(item['directory'])/item['output']
  if not p.is_file():raise ValueError('Missing original object')
  result.append(p.as_posix())
 if len(result)!=2230:raise ValueError('Expected the original 2230 selected objects at the pinned generation')
 path=work/'objects.cmake';path.write_text('set(SYLAR_CORE_OBJECTS\n'+''.join(' "'+x+'"\n' for x in result)+')\n',encoding='utf-8');return path
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=['prepare','generate','build'])
 for name in ['upstream','rom','work']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--ndk',type=Path);p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');p.add_argument('--reuse-compile-commands',type=Path);p.add_argument('--jobs',type=int,default=4)
 a=p.parse_args();upstream=a.upstream.resolve();rom=a.rom.resolve();work=a.work.resolve()
 if work.is_relative_to(ROOT) and not work.is_relative_to(ROOT/'.local'):raise ValueError('Private build work must be external or under ignored .local/')
 if work==upstream or work==rom.parent:raise ValueError('Use a separate private work directory')
 verify(upstream,rom);work.mkdir(parents=True,exist_ok=True)
 if a.stage=='prepare':prepare(upstream,work)
 elif a.stage=='generate':
  if not (work/'config').exists():raise ValueError('Run prepare first')
  framework=upstream/'snesrecomp';target=work/'analyzer-target'
  run(['cargo','build','--locked','--release','--manifest-path',framework/'recompiler-rs/Cargo.toml','--target-dir',target,'--bin','snesrecomp-analyze'])
  env=os.environ.copy();env.update(SNESRECOMP_ROOT=str(framework),SNESRECOMP_NATIVE_ANALYZER=str(target/'release'/('snesrecomp-analyze.exe' if os.name=='nt' else 'snesrecomp-analyze')),DKC1_WIDESCREEN='0',DKC1_RENDER_WIDTH='256',PYTHONDONTWRITEBYTECODE='1');env.pop('SNESRECOMP_EMIT_AOT_DENY_GATE',None)
  run([sys.executable,framework/'tools/v2_sync_funcs_h.py','--cfg-dir',work/'config','--out',work/'config/funcs.h'],env)
  run([sys.executable,framework/'tools/v2_emit.py','--rom',rom,'--cfg-dir',work/'config','--out-dir',work/'generated','--no-host-root-scan','--no-hle','--cfg-roots','--analysis-backend','native','--max-insns','4096','--max-nodes','100000','--bank-shard-threshold-kib','1024','--bank-shard-pc-span','0x10'],env)
 else:
  if not a.ndk:raise ValueError('--ndk is required for build')
  defs={'CMAKE_MAKE_PROGRAM':a.ninja,'CMAKE_TOOLCHAIN_FILE':(a.ndk.resolve()/'build/cmake/android.toolchain.cmake').as_posix(),'ANDROID_ABI':'arm64-v8a','ANDROID_PLATFORM':'android-26','ANDROID_STL':'none','ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES':'ON','CMAKE_BUILD_TYPE':'Release','DKC1_SOURCE':upstream.as_posix(),'SYLAR_PRIVATE_CONFIG':(work/'config').as_posix(),'SYLAR_PRIVATE_GENERATED':(work/'generated').as_posix(),'SYLAR_PPU_COPY':(work/'ppu_capture.c').as_posix()}
  if a.reuse_compile_commands:defs['SYLAR_OBJECT_LIST']=objects(a.reuse_compile_commands.resolve(),work).as_posix()
  run([a.cmake,'-S',ROOT/'native-host','-B',work/'core','-G','Ninja']+['-D'+k+'='+str(v) for k,v in defs.items()]);run([a.cmake,'--build',work/'core','--parallel',str(a.jobs)])
  print('Private core built. Set SYLAR_CORE_LIBRARY to '+str(work/'core/libsylar_dkc1.so')+' for the Android frontend. Do not upload the output.')
if __name__=='__main__':main()
