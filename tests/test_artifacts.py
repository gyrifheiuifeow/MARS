import hashlib,importlib.util,io,tarfile,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('artifacts',ROOT/'scripts/fetch_artifacts.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ArtifactTests(unittest.TestCase):
 def test_local_asset_hash_and_extract(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);source=root/'source';cache=root/'cache';source.mkdir();cache.mkdir()
   with tarfile.open(source/'sample.tar.gz','w:gz') as t:
    body=b'sample data\n';info=tarfile.TarInfo('data/example.txt');info.size=len(body);t.addfile(info,io.BytesIO(body))
   f=source/'sample.tar.gz';record={'name':f.name,'bytes':f.stat().st_size,'sha256':m.sha(f)}
   m.obtain(record,cache/f.name,assets_dir=source);m.extract_archive(cache/f.name,root/'out')
   self.assertEqual((root/'out/data/example.txt').read_bytes(),body)
   m.extract_archive(cache/f.name,root/'out')
   (root/'out/data/example.txt').write_bytes(b'changed')
   with self.assertRaises(RuntimeError):m.extract_archive(cache/f.name,root/'out')
 def test_wrong_hash_is_not_promoted(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);source=root/'source';source.mkdir();(source/'sample').write_bytes(b'data')
   with self.assertRaises(RuntimeError):m.obtain({'name':'sample','bytes':4,'sha256':'0'*64},root/'sample',assets_dir=source)
   self.assertFalse((root/'sample').exists())
 def test_archive_cannot_escape_output(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)
   with tarfile.open(root/'bad.tar','w') as t:
    info=tarfile.TarInfo('../outside');info.size=1;t.addfile(info,io.BytesIO(b'x'))
   with self.assertRaises(RuntimeError):m.extract_archive(root/'bad.tar',root/'out')
   self.assertFalse((root/'outside').exists())
if __name__=='__main__':unittest.main()
