#!/usr/bin/env python3
from __future__ import annotations
import argparse,datetime as dt,json,platform,shutil,subprocess
from pathlib import Path
def main(argv=None):
 ap=argparse.ArgumentParser();ap.add_argument('--type',default='aumu');ap.add_argument('--subtype',required=True);ap.add_argument('--manufacturer',required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args(argv);a.output.mkdir(parents=True,exist_ok=True);status='NOT_RUN';code=None;log='auval requires macOS and the actual installed component\n'
 if platform.system()=='Darwin' and shutil.which('auval'):
  p=subprocess.run(['auval','-v',a.type,a.subtype,a.manufacturer],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);code=p.returncode;log=p.stdout;status='PASS' if code==0 else 'FAIL'
 (a.output/'auval.log').write_text(log,encoding='utf-8');result={'status':status,'exitCode':code,'executedAt':dt.datetime.now(dt.timezone.utc).isoformat()};(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2));return 0 if status=='PASS' else 3
if __name__=='__main__':raise SystemExit(main())
