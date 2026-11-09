#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace std;
struct Object {int name;vector<int> cats;};
static void normalize(vector<float>& v){double s=0;for(float x:v)s+=double(x)*double(x);double n=sqrt(s);if(n>0)for(float&x:v)x=float(double(x)/n);for(float&x:v){if(!isfinite(x))throw runtime_error("nonfinite vector");if(x==0)x=0;}}
template<class T>void writev(ofstream&f,T v){f.write((const char*)&v,sizeof(v));}
int main(int argc,char**argv){try{
 if(argc!=5)throw runtime_error("usage dataset_dir region stats|reference output_file");
 string dir=argv[1],region=argv[2],mode=argv[3];if(mode!="stats"&&mode!="reference")throw runtime_error("mode");
 ifstream check(argv[4]);if(check.good())throw runtime_error("refuse overwrite");
 ifstream mapfile(dir+"/poi_node_mapping_Ext3.txt");if(!mapfile)throw runtime_error("mapping missing");
 vector<Object> objs;set<int> usedname,usedcat;string line;
 while(getline(mapfile,line)){auto pos=line.find('<');if(pos==string::npos)continue;Object o;o.name=stoi(line.substr(0,pos));usedname.insert(o.name);istringstream ss(line.substr(pos+1));int c;while(ss>>c){o.cats.push_back(c);usedcat.insert(c);}objs.push_back(move(o));}
 // Build the reference from fvecs using full-coordinate keys and per-object sets.
 map<string,uint32_t> dict;vector<vector<uint32_t>> owners;
 vector<uint32_t> names,cats;
 auto read=[&](string file,const set<int>&used,vector<uint32_t>&ids){ifstream f(file,ios::binary);if(!f)throw runtime_error("fvecs missing");ids.push_back(UINT32_MAX);int d;uint32_t id=0;size_t count=0;while(f.read((char*)&d,4)){if(d!=768)throw runtime_error("dim");vector<float> v(d);f.read((char*)v.data(),4*d);if(!f)throw runtime_error("short read");++id;ids.push_back(UINT32_MAX);if(!used.count(id))continue;++count;normalize(v);string key((char*)v.data(),v.size()*4);auto it=dict.find(key);if(it==dict.end()){uint32_t uid=owners.size();it=dict.emplace(move(key),uid).first;owners.emplace_back();}ids[id]=it->second;}if(count!=used.size())throw runtime_error("missing referenced ID");};
 read(dir+"/poi.fvecs",usedname,names);read(dir+"/cat.fvecs",usedcat,cats);
 for(uint32_t oid=0;oid<objs.size();++oid){set<uint32_t> aspects;aspects.insert(names.at(objs[oid].name));for(int c:objs[oid].cats)aspects.insert(cats.at(c));if(aspects.count(UINT32_MAX))throw runtime_error("missing UID");for(auto x:aspects)owners.at(x).push_back(oid);}
 ofstream out(argv[4],mode=="reference"?ios::binary:ios::out);if(!out)throw runtime_error("output failed");
 if(mode=="reference"){
  writev<uint64_t>(out,0x4652454652454632ULL);writev<uint64_t>(out,768);writev<uint64_t>(out,objs.size());writev<uint64_t>(out,dict.size());
  for(auto&[key,uid]:dict){out.write(key.data(),key.size());writev<uint64_t>(out,owners[uid].size());for(uint32_t oid:owners[uid])writev<uint32_t>(out,oid);}
 }else{
  out<<setprecision(17)<<"region,alpha,objects,distinct_vectors,pivot_vectors,pivot_postings,rest_vectors,rest_associations,pivot_covered_objects,pivot_object_fraction,estimated_graph_bytes,estimated_mapping_and_vector_bytes,conservative_instance_bytes\n";
  for(uint64_t a:{2,4,8,16,32}){uint64_t piv=0,pp=0,rest=0,rp=0;vector<bool>covered(objs.size(),false);for(auto&o:owners){if(o.size()>=a){++piv;pp+=o.size();for(auto id:o)covered[id]=true;}else{++rest;rp+=o.size();}}uint64_t nc=count(covered.begin(),covered.end(),true);uint64_t graph=rp*7000ULL+16*1024*1024ULL;uint64_t mapping=dict.size()*(3072ULL+32)+8*(pp+rp)+objs.size()*32ULL;uint64_t reserved=graph+mapping+512ULL*1024*1024;
   out<<region<<','<<a<<','<<objs.size()<<','<<dict.size()<<','<<piv<<','<<pp<<','<<rest<<','<<rp<<','<<nc<<','<<double(nc)/objs.size()<<','<<graph<<','<<mapping<<','<<reserved<<'\n';}
 }
 out.flush();if(!out)throw runtime_error("output incomplete");cerr<<"REFERENCE_COMPLETE "<<region<<' '<<mode<<" distinct="<<dict.size()<<endl;
}catch(exception&e){cerr<<"FAILED "<<e.what()<<endl;return 2;}}
