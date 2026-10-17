#pragma once
#include <cstring>
struct FrequencyPartition {
 size_t dim=0;int alpha=0;
 std::vector<float> matrix;
 std::vector<std::vector<uint32_t>> owners,object_uids;
 std::vector<uint64_t> source_keys;
 std::vector<uint32_t> source_uids,rest_uids;
 std::vector<uint8_t> channels;
 const float* vectorAt(size_t uid)const{return matrix.data()+uid*dim;}
};
inline FrequencyPartition frequency_partition;
inline int frequency_requested_alpha=0;
static std::string exactVectorKey(const float* v,size_t d){std::string k(d*sizeof(float),'\0');for(size_t i=0;i<d;++i){float x=v[i];if(!std::isfinite(x))throw std::runtime_error("nonfinite normalized aspect");if(x==0)x=0.0f;std::memcpy(k.data()+i*sizeof(float),&x,sizeof(float));}return k;}
static FrequencyPartition buildFrequencyPartition(Dataset& ds,const GlobalIndexData& g,int alpha){
 if(alpha<1||alpha!=frequency_requested_alpha)throw std::runtime_error("explicit positive alpha not bound");
 FrequencyPartition f;f.dim=g.dim;f.alpha=alpha;f.object_uids.resize(ds.objects.size());
 std::unordered_map<std::string,uint32_t> dictionary;dictionary.reserve(g.entries.size());
 std::map<uint64_t,uint32_t> sources;
 auto identify=[&](size_t aspect,uint64_t source){
  if(aspect>=g.entries.size())throw std::runtime_error("missing source aspect");
  auto key=exactVectorKey(g.matrix.data()+aspect*g.dim,g.dim);auto it=dictionary.find(key);uint32_t uid;
  if(it==dictionary.end()){uid=f.owners.size();dictionary.emplace(key,uid);f.owners.emplace_back();size_t old=f.matrix.size();f.matrix.resize(old+g.dim);std::memcpy(f.matrix.data()+old,key.data(),key.size());}else uid=it->second;
  auto [s,inserted]=sources.emplace(source,uid);if(!inserted&&s->second!=uid)throw std::runtime_error("source ID changed vector");return uid;
 };
 for(const auto&o:ds.objects){
  if(o.object_id<0||size_t(o.object_id)>=f.object_uids.size())throw std::runtime_error("object map invalid");
  auto& ids=f.object_uids[o.object_id];ids.push_back(identify(g.poi_aspect_by_object.at(o.object_id),uint64_t(uint32_t(o.poi_id))));
  for(int cid:o.category_ids)ids.push_back(identify(g.cat_aspect_by_id.at(cid),(1ULL<<32)|uint32_t(cid)));
  std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
  for(auto uid:ids)f.owners[uid].push_back(o.object_id);
 }
 for(const auto&[key,uid]:sources){f.source_keys.push_back(key);f.source_uids.push_back(uid);}
 for(uint32_t uid=0;uid<f.owners.size();++uid){auto&o=f.owners[uid];if(o.empty()||!std::is_sorted(o.begin(),o.end())||std::adjacent_find(o.begin(),o.end())!=o.end())throw std::runtime_error("frequency association invariant");f.channels.push_back(o.size()>=size_t(alpha)?1:0);if(o.size()<size_t(alpha))f.rest_uids.push_back(uid);}
 return f;
}
static void saveFrequency(const FrequencyPartition& f,const std::filesystem::path& path){
 auto out=entry::fresh(path,true);entry::put<uint64_t>(out,0x4d46524551414731ULL);entry::put<uint64_t>(out,f.dim);entry::put<int32_t>(out,f.alpha);entry::vec(out,f.matrix);entry::nested(out,f.owners);entry::nested(out,f.object_uids);entry::vec(out,f.source_keys);entry::vec(out,f.source_uids);entry::vec(out,f.channels);entry::vec(out,f.rest_uids);entry::finishFile(out);
 entry::account(path,uint64_t(f.matrix.size())*sizeof(float),"frequency IDs counts channels and both object maps; only explicit normalized float payload copy excluded");
}
static FrequencyPartition loadFrequency(const std::filesystem::path& path,int expected_alpha){
 std::ifstream in(path,std::ios::binary);entry::magic(in,0x4d46524551414731ULL);FrequencyPartition f;f.dim=entry::get<uint64_t>(in);f.alpha=entry::get<int32_t>(in);if(expected_alpha<1||f.alpha!=expected_alpha||f.dim==0||f.dim>4096)throw std::runtime_error("wrong frequency index configuration");f.matrix=entry::vec<float>(in,2000000000);f.owners=entry::nested<uint32_t>(in);f.object_uids=entry::nested<uint32_t>(in);f.source_keys=entry::vec<uint64_t>(in);f.source_uids=entry::vec<uint32_t>(in);f.channels=entry::vec<uint8_t>(in);f.rest_uids=entry::vec<uint32_t>(in);entry::eof(in);
 if(f.matrix.size()!=f.owners.size()*f.dim||f.channels.size()!=f.owners.size()||f.source_keys.size()!=f.source_uids.size())throw std::runtime_error("frequency state dimension mismatch");
 std::vector<uint32_t> expected_rest;for(uint32_t uid=0;uid<f.owners.size();++uid){const auto&o=f.owners[uid];if(o.empty()||!std::is_sorted(o.begin(),o.end())||std::adjacent_find(o.begin(),o.end())!=o.end()||f.channels[uid]!=(o.size()>=size_t(f.alpha)))throw std::runtime_error("frequency partition corrupt");for(auto oid:o)if(oid>=f.object_uids.size()||!std::binary_search(f.object_uids[oid].begin(),f.object_uids[oid].end(),uid))throw std::runtime_error("frequency reverse association missing");if(o.size()<size_t(f.alpha))expected_rest.push_back(uid);}
 if(expected_rest!=f.rest_uids)throw std::runtime_error("rest UID map corrupt");return f;
}
static void exportCanonicalFrequency(const FrequencyPartition& f,const std::filesystem::path& path){
 std::vector<std::pair<std::string,uint32_t>> order;order.reserve(f.owners.size());for(uint32_t i=0;i<f.owners.size();++i)order.push_back({exactVectorKey(f.vectorAt(i),f.dim),i});std::sort(order.begin(),order.end());
 auto out=entry::fresh(path,true);entry::put<uint64_t>(out,0x4652454652454632ULL);entry::put<uint64_t>(out,f.dim);entry::put<uint64_t>(out,f.object_uids.size());entry::put<uint64_t>(out,f.owners.size());
 for(auto&[key,uid]:order){out.write(key.data(),key.size());entry::put<uint64_t>(out,f.owners[uid].size());for(auto oid:f.owners[uid])entry::put<uint32_t>(out,oid);}entry::finishFile(out);
}
static CatIndex buildFrequencyPivotIndex(Dataset& ds,MarsIndex& idx){
 const auto&f=frequency_partition;if(f.object_uids.size()!=ds.objects.size()||f.alpha!=frequency_requested_alpha)throw std::runtime_error("frequency state not bound");CatIndex ci;ci.dim=f.dim;
 for(uint32_t uid=0;uid<f.owners.size();++uid){if(!f.channels[uid])continue;ci.slot_cid.push_back(uid);ci.slot_vec.push_back(f.vectorAt(uid));ci.postings.emplace_back();auto&post=ci.postings.back();for(auto oid:f.owners[uid]){int v=ds.objects.at(oid).vertex_id;if(v<1||size_t(v)>=idx.hierarchy.vertex_key.size())throw std::runtime_error("object vertex out of hierarchy");int key=idx.hierarchy.vertex_key[v];if(key<0)throw std::runtime_error("object has no road key");post.push_back({key,int(oid)});}std::sort(post.begin(),post.end(),[](const auto&a,const auto&b){return a.dfs_key<b.dfs_key;});ci.total_posting_entries+=post.size();}
 return ci;
}
