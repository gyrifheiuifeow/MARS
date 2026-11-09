#!/usr/bin/env python3
"""Verify and extract artifact archives."""
from pathlib import Path
import argparse,hashlib,json,shutil,tarfile,urllib.request,urllib.parse,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def obtain(record,target,assets_dir=None,base_url=None):
 name=record['name']
 if Path(name).name!=name or '/' in name or '\\' in name:raise ValueError('Asset name must be a filename')
 if target.exists() and target.stat().st_size==record['bytes'] and sha(target)==record['sha256']:return
 partial=target.with_name(target.name+'.partial')
 if assets_dir is not None:
  with (assets_dir/name).open('rb') as src,partial.open('wb') as dst:shutil.copyfileobj(src,dst,8*1024*1024)
 else:
  url=base_url.rstrip('/')+'/'+urllib.parse.quote(name,safe='')
  with urllib.request.urlopen(url,timeout=180) as src,partial.open('wb') as dst:shutil.copyfileobj(src,dst,8*1024*1024)
 if partial.stat().st_size!=record['bytes'] or sha(partial)!=record['sha256']:raise RuntimeError('Asset checksum mismatch: '+name)
 partial.replace(target)
def extract_archive(archive,dest):
 dest=dest.resolve();dest.mkdir(parents=True,exist_ok=True)
 with tarfile.open(archive) as t:
  for member in t:
   path=(dest/member.name).resolve()
   if not path.is_relative_to(dest) or not (member.isfile() or member.isdir()):raise RuntimeError('Unsafe archive entry: '+member.name)
   if member.isfile() and path.exists():
    digest=hashlib.sha256()
    with t.extractfile(member) as src:
     for chunk in iter(lambda:src.read(8*1024*1024),b''):digest.update(chunk)
    if path.stat().st_size!=member.size or sha(path)!=digest.hexdigest():raise RuntimeError('Refusing to overwrite different existing file: '+str(path))
    continue
   t.extract(member,dest,filter='data')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--kind',choices=['data','records','local','all'],default='data')
 p.add_argument('--output',type=Path,required=True)
 p.add_argument('--manifest',type=Path,default=ROOT/'provenance/release_assets.json')
 p.add_argument('--download-only',action='store_true')
 p.add_argument('--archive',help='Archive name, including all split parts')
 source=p.add_mutually_exclusive_group(required=True)
 source.add_argument('--assets-dir',type=Path,help='Directory containing the supplied archive files')
 source.add_argument('--base-url',help='URL of the directory containing the archive files')
 a=p.parse_args()
 if sys.version_info<(3,12):p.error('Python 3.12+ is required for safe tar extraction.')
 if a.base_url and urllib.parse.urlsplit(a.base_url).scheme not in ('http','https'):p.error('The mirror must use HTTP or HTTPS.')
 manifest=json.loads(a.manifest.read_text(encoding='utf-8'))
 wanted=[r for r in manifest if a.kind=='all' or r['kind']==a.kind]
 if a.archive:
  wanted=[r for r in wanted if r.get('archive')==a.archive]
  if not wanted:p.error('No matching archive in this kind: '+a.archive)
 out=a.output.resolve();cache=out/'.downloads';cache.mkdir(parents=True,exist_ok=True)
 for r in wanted:
  print('Checking',r['name'],flush=True);obtain(r,cache/r['name'],a.assets_dir,a.base_url)
 if a.download_only:return
 groups={}
 for r in wanted:
  if r.get('archive'):groups.setdefault(r['archive'],[]).append(r)
 for name,parts in groups.items():
  if Path(name).name!=name or '/' in name or '\\' in name:raise ValueError('Unsafe archive filename')
  parts.sort(key=lambda r:r['name']);archive=cache/name
  marker=cache/(name+'.extracted.json');identity=[r['sha256'] for r in parts]
  if marker.exists() and json.loads(marker.read_text())==identity:continue
  if len(parts)>1:
   with archive.open('wb') as dst:
    for r in parts:
     with (cache/r['name']).open('rb') as src:shutil.copyfileobj(src,dst,8*1024*1024)
  dest=(out/parts[0].get('extract_root','')).resolve()
  if not dest.is_relative_to(out):raise ValueError('Extraction root escapes output directory')
  print('Extracting',name,flush=True);extract_archive(archive,dest);marker.write_text(json.dumps(identity))
 print('Verified artifacts restored to',out)
if __name__=='__main__':main()
