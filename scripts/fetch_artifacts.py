#!/usr/bin/env python3
"""Download SHA-256 checked release assets and safely restore the recorded layout."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tarfile,urllib.request,urllib.parse,sys
ROOT=Path(__file__).resolve().parents[1]
REPO='gyrifheiuifeow/MARS__Efficient_Multi_Aspect_Road_Constrained_Vector_Search'
TAG='v2026.10.18-reproducibility'
class SafeRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,req,fp,code,msg,headers,newurl):
  redirected=super().redirect_request(req,fp,code,msg,headers,newurl)
  if redirected and urllib.parse.urlsplit(newurl).hostname!=urllib.parse.urlsplit(req.full_url).hostname:
   redirected.remove_header('Authorization')
  return redirected
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def main():
 urllib.request.install_opener(urllib.request.build_opener(SafeRedirect()))
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--kind',choices=['data','records','local','all'],default='data');p.add_argument('--output',type=Path,required=True);p.add_argument('--download-only',action='store_true');p.add_argument('--archive',help='Optional full archive name, e.g. dataset-NYC.tar.gz; all of its parts are selected');a=p.parse_args()
 if sys.version_info<(3,12):p.error('Python 3.12+ is required for safe tar extraction.')
 manifest=json.loads((ROOT/'provenance/release_assets.json').read_text())
 wanted=[r for r in manifest if a.kind=='all' or r['kind']==a.kind]
 if a.archive:
  wanted=[r for r in wanted if r.get('archive')==a.archive]
  if not wanted:p.error('No matching archive in this kind: '+a.archive)
 token=os.environ.get('GITHUB_TOKEN') or os.environ.get('GH_TOKEN')
 if not token:
  try:
   v=subprocess.run(['git','credential','fill'],input='protocol=https\nhost=github.com\n\n',text=True,capture_output=True,env={**os.environ,'GIT_TERMINAL_PROMPT':'0'},timeout=15)
   token=dict(line.split('=',1) for line in v.stdout.splitlines() if '=' in line).get('password')
  except (FileNotFoundError,subprocess.TimeoutExpired):pass
 headers={'Accept':'application/vnd.github+json','User-Agent':'MARS-artifact-downloader'}
 if token:headers['Authorization']='Bearer '+token
 with urllib.request.urlopen(urllib.request.Request(f'https://api.github.com/repos/{REPO}/releases/tags/{TAG}',headers=headers)) as f:release=json.load(f)
 remote={x['name']:x for x in release['assets']};out=a.output.resolve();cache=out/'.downloads';cache.mkdir(parents=True,exist_ok=True)
 for record in wanted:
  target=cache/record['name']
  if target.exists() and target.stat().st_size==record['bytes'] and sha(target)==record['sha256']:continue
  info=remote[record['name']];req=urllib.request.Request(info['url'],headers={**headers,'Accept':'application/octet-stream'})
  print('Downloading',record['name'],flush=True)
  partial=target.with_name(target.name+'.partial')
  with urllib.request.urlopen(req,timeout=180) as src,partial.open('wb') as dst:shutil.copyfileobj(src,dst,8*1024*1024)
  if partial.stat().st_size!=record['bytes'] or sha(partial)!=record['sha256']:raise RuntimeError('Asset checksum mismatch: '+record['name'])
  partial.replace(target)
 if a.download_only:return
 groups={}
 for r in wanted:
  if r.get('archive'):groups.setdefault(r['archive'],[]).append(r)
 for name,parts in groups.items():
  parts.sort(key=lambda r:r['name']);archive=cache/name
  marker=cache/(name+'.extracted.json');identity=[r['sha256'] for r in parts]
  if marker.exists() and json.loads(marker.read_text())==identity:continue
  if len(parts)>1:
   with archive.open('wb') as dst:
    for r in parts:
     with (cache/r['name']).open('rb') as src:shutil.copyfileobj(src,dst,8*1024*1024)
  dest=out/parts[0].get('extract_root','');dest.mkdir(parents=True,exist_ok=True)
  print('Extracting',name,flush=True)
  with tarfile.open(archive) as t:
   for member in t:
    path=(dest/member.name).resolve()
    if not path.is_relative_to(dest.resolve()) or not (member.isfile() or member.isdir()):raise RuntimeError('Unsafe archive entry: '+member.name)
    if member.isfile() and path.exists():
     digest=hashlib.sha256()
     with t.extractfile(member) as src:
      for chunk in iter(lambda:src.read(8*1024*1024),b''):digest.update(chunk)
     if path.stat().st_size!=member.size or sha(path)!=digest.hexdigest():raise RuntimeError('Refusing to overwrite different existing file: '+str(path))
     continue
    t.extract(member,dest,filter='data')
  marker.write_text(json.dumps(identity))
 print('Verified assets restored to',out)
if __name__=='__main__':main()
