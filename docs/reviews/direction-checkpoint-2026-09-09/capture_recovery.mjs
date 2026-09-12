// Local-only preservation requested by the owner; no Git or source mutation.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const run=(args)=>execFileSync('git',args,{cwd:root,maxBuffer:64*1024*1024});
const sha=(bytes)=>crypto.createHash('sha256').update(bytes).digest('hex');
const base=path.join(root,'build/recovery-checkpoints');
fs.mkdirSync(base,{recursive:true});
const output=fs.mkdtempSync(path.join(base,'session-preservation-'));
const names=[...new Set(run(['ls-files','--cached','--others','--exclude-standard','-z']).toString().split('\0').filter(Boolean))].sort();
const identity=name=>{
  const file=path.join(root,name);
  let stat; try {stat=fs.lstatSync(file);} catch(error) {if(error.code==='ENOENT') return {kind:'missing'};throw error;}
  if(stat.isSymbolicLink()) return {kind:'symlink',target:fs.readlinkSync(file)};
  if(!stat.isFile()) return {kind:'not-regular',mode:stat.mode};
  return {kind:'file',bytes:stat.size,sha256:sha(fs.readFileSync(file))};
};
const entries=Object.fromEntries(names.map(name=>[name,identity(name)]));
const files=names.filter(name=>['file','symlink'].includes(entries[name].kind));
const patch=run(['diff','HEAD','--binary']);
const index=run(['diff','--cached','--binary']);
const archive=path.join(output,'source-tree.tar.gz');
execFileSync('/usr/bin/tar',['-czf',archive,'--null','-T','-'],{cwd:root,input:Buffer.from(files.join('\0')+'\0'),maxBuffer:1024*1024});
for(const name of names) if(JSON.stringify(identity(name))!==JSON.stringify(entries[name])) throw new Error('Worktree changed during preservation: '+name);
const manifest={capturedAt:new Date().toISOString(),head:run(['rev-parse','HEAD']).toString().trim(),
  branch:run(['branch','--show-current']).toString().trim(),
  scope:'Tracked and non-ignored untracked worktree files. Git metadata and ignored builds/dependencies excluded. No upload.',
  fileCount:files.length,entries,archiveSha256:sha(fs.readFileSync(archive)),patchSha256:sha(patch),indexPatchSha256:sha(index)};
fs.writeFileSync(path.join(output,'worktree-from-head.patch'),patch);
fs.writeFileSync(path.join(output,'index.patch'),index);
fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
execFileSync('/usr/bin/tar',['-tzf',archive],{maxBuffer:16*1024*1024});
console.log(JSON.stringify({output,fileCount:files.length,archiveSha256:manifest.archiveSha256,verifiedUnchanged:true,archiveReadable:true,uploaded:false}));
