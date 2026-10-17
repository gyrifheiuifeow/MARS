from pathlib import Path
from array import array
import struct,hashlib,json,sys
P=Path(__file__).resolve().parent;O=P.parent/'student_mars_freq_a2_v1'
def scalar(f,k):return struct.unpack(k,f.read(struct.calcsize(k)))[0]
def vec(f,k):
 n=scalar(f,'<Q');a=array(k);a.frombytes(f.read(n*a.itemsize));assert len(a)==n;return a
def digest(f,n):
 h=hashlib.sha256()
 while n:
  b=f.read(min(n,8*1024*1024));assert b;h.update(b);n-=len(b)
 return h.hexdigest()
def check(reg,rep):
 old=O/f'instances/{reg}/rep1/frequency_state.bin';new=P/f'instances/{reg}/rep{rep}/frequency_state.bin';freq=[];node_uid=array('I');node_oid=array('I')
 with old.open('rb') as a,new.open('rb') as b:
  assert scalar(a,'<Q')==0x4d46524551413231 and scalar(b,'<Q')==0x4d46524551414731
  assert scalar(a,'<Q')==scalar(b,'<Q')==768;assert scalar(a,'<i')==2 and scalar(b,'<i')==64
  na=scalar(a,'<Q');nb=scalar(b,'<Q');assert na==nb;assert digest(a,na*4)==digest(b,nb*4)
  n=scalar(a,'<Q');assert n==scalar(b,'<Q')
  for uid in range(n):
   aa=vec(a,'I');bb=vec(b,'I');assert aa==bb;freq.append(len(bb));assert len(set(bb))==len(bb)
   if len(bb)<64:
    for oid in bb:node_uid.append(uid);node_oid.append(oid)
  objects=scalar(a,'<Q');assert objects==scalar(b,'<Q')
  for _ in range(objects):assert vec(a,'I')==vec(b,'I')
  assert vec(a,'Q')==vec(b,'Q');assert vec(a,'I')==vec(b,'I');ca=vec(a,'B');cb=vec(b,'B');ra=vec(a,'I');rb=vec(b,'I')
  assert list(cb)==[int(x>=64) for x in freq];assert list(rb)==[i for i,x in enumerate(freq) if x<64];assert not a.read(1) and not b.read(1)
 with (new.parent/'mars_aux.bin').open('rb') as f:
  assert scalar(f,'<Q')==0x4d41525341555832;assert scalar(f,'<i')==64;assert scalar(f,'<Q')==768
  assert vec(f,'i')==array('i',node_oid);assert vec(f,'I')==node_uid
 with (new.parent/'rest_graph.bin').open('rb') as f:assert struct.unpack('<QQQQ',f.read(32))==(len(node_uid),32,768,768)
 assert sum(cb)=={'NYC':1030,'BAY':910,'COL':774,'FLA':1284}[reg]
 result={'region':reg,'repetition':rep,'alpha':64,'distinct_vectors':n,'pivot_vectors':sum(cb),'pivot_postings':sum(x for x in freq if x>=64),'rest_distinct_vectors':len(rb),'rest_graph_nodes':len(node_uid),'all_vectors_owners_source_IDs_match_previously_independently_verified_input':True,'all_channels_and_rest_pairs_independently_checked':True,'graph_header_matches_association_count':True}
 (P/f'runs/{reg}_rep{rep}/PARTITION_PASS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
if __name__=='__main__':check(sys.argv[1],int(sys.argv[2]))
