#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path

def sha(p):
 h=hashlib.sha256();
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--payload',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--status',required=True);a=ap.parse_args();base=a.payload.resolve();files=[]
 for p in sorted(base.rglob('*')):
  if p.is_file() and p.resolve()!=a.output.resolve():files.append({'path':p.relative_to(base).as_posix(),'size':p.stat().st_size,'sha256':sha(p)})
 a.output.write_text(json.dumps({'schemaVersion':1,'version':'0.13.0','status':a.status,'files':files},indent=2)+"\n");return 0
if __name__=='__main__':raise SystemExit(main())
