#pragma once
struct ReferenceRoad {
 vector<vector<pair<int,long long>>> adj;long long scale=0;
 void load(string region){ifstream m(getenv("EXACT_ROAD_UNITS_MANIFEST"));string line,reg,path,hash;getline(m,line);while(getline(m,line)){istringstream s(line);long long unit;s>>reg>>unit>>path>>hash;if(reg==region){scale=unit;break;}}if(scale<=0)throw runtime_error("missing verified integer units");ifstream f(path);if(!f)throw runtime_error("reference road missing");long long expected=0,count=0;while(getline(f,line)){istringstream s(line);char c=0;s>>c;if(c=='p'){string kind;int n;s>>kind>>n>>expected;adj.resize(n);}else if(c=='a'){int a,b;long long w;s>>a>>b>>w;adj.at(a-1).push_back({b-1,w});++count;}}if(count!=expected)throw runtime_error("reference road record count");}
 vector<char> reachable(int source,int tau)const{long long cap=static_cast<long long>(tau)*scale;vector<long long>d(adj.size(),numeric_limits<long long>::max());vector<char>ok(adj.size(),false);using Pair=pair<long long,int>;priority_queue<Pair,vector<Pair>,greater<Pair>>pq;d.at(source-1)=0;pq.push({0,source-1});while(!pq.empty()){auto[dist,u]=pq.top();pq.pop();if(dist!=d[u])continue;if(dist>cap)break;ok[u]=true;for(auto[v,w]:adj[u]){if(w>cap-dist)continue;auto nd=dist+w;if(nd<d[v]){d[v]=nd;pq.push({nd,v});}}}return ok;}
};
struct ReferencePivots {
 size_t dim=0;vector<vector<float>> vectors;vector<vector<uint32_t>> owners;
 void load(const filesystem::path& p,int alpha){ifstream f(p,ios::binary);entry::magic(f,0x4652454652454632ULL);dim=entry::get<uint64_t>(f);auto objects=entry::get<uint64_t>(f),n=entry::get<uint64_t>(f);(void)objects;for(uint64_t i=0;i<n;++i){vector<float> v(dim);f.read((char*)v.data(),dim*4);auto count=entry::get<uint64_t>(f);vector<uint32_t>ids(count);f.read((char*)ids.data(),count*4);if(!f)throw runtime_error("reference truncated");if(count>=size_t(alpha)){vectors.push_back(move(v));owners.push_back(move(ids));}}entry::eof(f);}
 vector<int> exact(const vector<float>&q,Dataset&ds,const vector<char>&reachable,int k)const{vector<double> scores(ds.objects.size(),-1e100);for(size_t i=0;i<vectors.size();++i){double s=0;for(size_t d=0;d<dim;++d)s+=double(q[d])*double(vectors[i][d]);for(auto id:owners[i])if(reachable.at(ds.objects[id].vertex_id-1))scores[id]=max(scores[id],s);}vector<pair<double,int>>rank;for(size_t i=0;i<scores.size();++i)if(scores[i]>-1e99)rank.push_back({scores[i],int(i)});sort(rank.begin(),rank.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});vector<int>ids;for(size_t i=0;i<rank.size()&&i<size_t(k);++i)ids.push_back(rank[i].second);return ids;}
};
#if STUDENT_FREQ_ACCEPTANCE
static void checkProcessedMerge(const vector<int>& ids,int k){map<int,double> score;for(auto&u:freq_observed_updates){auto it=score.find(u.oid);if(it==score.end())score[u.oid]=u.score;else it->second=max(it->second,u.score);}vector<pair<double,int>> rank;for(auto&[id,s]:score)rank.push_back({s,id});sort(rank.begin(),rank.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});if(ids.size()!=min(size_t(k),rank.size())||freq_returned_scores.size()!=ids.size())throw runtime_error("processed-score result cardinality mismatch");for(size_t i=0;i<ids.size();++i)if(ids[i]!=rank[i].second||freq_returned_scores[i]!=rank[i].first)throw runtime_error("processed-score max aggregation mismatch");}
static void runAlpha64Boundary(const filesystem::path& out){
 frequency_requested_alpha=64;Dataset ds;ds.road_n=2;ds.objects_by_vertex.resize(3);ds.query_vecs={{0,1}};MarsIndex idx;idx.global.dim=2;
 vector<vector<float>> names;map<int,vector<float>> cats={{10,{1,0}},{11,{0,1}},{12,{-1,0}}};map<vector<float>,set<uint32_t>> truth;
 for(int i=0;i<65;++i){vector<float> v=i<64?vector<float>{1,0}:vector<float>{-1,0};names.push_back(v);ObjectRecord o;o.object_id=i;o.poi_id=i+1;o.vertex_id=i<5?1:2;o.category_ids={12};if(i<63)o.category_ids.push_back(11);if(i==0){o.category_ids.push_back(10);o.category_ids.push_back(10);}ds.objects.push_back(o);ds.objects_by_vertex[o.vertex_id].push_back(i);idx.global.poi_aspect_by_object.push_back(idx.global.entries.size());idx.global.entries.push_back({i,o.vertex_id,0,0,false});idx.global.matrix.insert(idx.global.matrix.end(),v.begin(),v.end());truth[v].insert(i);for(int cid:o.category_ids)truth[cats[cid]].insert(i);}
 for(auto&[cid,v]:cats){idx.global.cat_aspect_by_id[cid]=idx.global.entries.size();idx.global.entries.push_back({-1,-1,0,cid,true});idx.global.matrix.insert(idx.global.matrix.end(),v.begin(),v.end());}
 if(truth.at(vector<float>{1,0}).size()!=64||truth.at(vector<float>{0,1}).size()!=63||truth.at(vector<float>{-1,0}).size()!=65)throw runtime_error("independent boundary fixture invalid");
 frequency_partition=buildFrequencyPartition(ds,idx.global,64);if(frequency_partition.owners.size()!=3)throw runtime_error("alpha64 merge boundary");
 for(size_t u=0;u<3;++u){vector<float>v(frequency_partition.vectorAt(u),frequency_partition.vectorAt(u)+2);set<uint32_t>got(frequency_partition.owners[u].begin(),frequency_partition.owners[u].end());if(got!=truth.at(v)||frequency_partition.channels[u]!=(got.size()>=64))throw runtime_error("alpha64 boundary frequency or membership wrong");}
 auto&h=idx.hierarchy;h.coords.resize(3);h.vertex_key={-1,0,1};h.key_vertex={1,2};CatIndex ci=buildCatIndex(ds,idx);NameGraph ng;ng.dim=2;populateRestMapping(ng);validateRestMapping(ng);ng.ready=true;
 if(ci.slot_cid.size()!=2||ng.num!=63||frequency_partition.rest_uids.size()!=1)throw runtime_error("alpha64 full shared rest association count");
 for(size_t p=0;p<63;++p)if(ng.pid2oid[p]!=int(p)||ng.pid2uid[p]!=frequency_partition.rest_uids[0])throw runtime_error("alpha64 rest mapping lost owner");
 freq_fixture_rest_enumeration=true;Options opt;opt.k=10;opt.tau=5000;opt.param_name="ef";opt.param_value="1600";vector<float>q={0,1};setenv("MARS_CAT_PIVOT","1",1);int checks=0;
 auto check=[&](){for(bool point:{false,true})for(bool rest:{false,true}){setenv("MARS_CAT_POINTWISE_ROAD",point?"1":"0",1);setenv("MARS_CAT_UNIQUE",rest?"1":"0",1);auto r=marsCatSearch(ds,idx,ci,ng,opt,q,1,CatScoreMode::ChannelScore);checkProcessedMerge(r.ids,10);if(r.ids!=vector<int>{0,1,2,3,4})throw runtime_error("alpha64 reachable topk mismatch");for(double v:freq_returned_scores)if(v!=(rest?1.0:0.0))throw runtime_error("alpha64 shared rest score lost");++checks;}};
 check();auto dir=out/"boundary64";filesystem::create_directories(dir);saveFrequency(frequency_partition,dir/"frequency.bin");saveAux(idx,ci,ng.pid2oid,ng.pid2uid,dir/"aux.bin");
 frequency_partition=loadFrequency(dir/"frequency.bin",64);ci=CatIndex{};ng.pid2oid.clear();ng.pid2uid.clear();loadAux(ds,idx,ci,ng,dir/"aux.bin");ng.ready=true;check();freq_fixture_rest_enumeration=false;
 auto f=entry::fresh(dir/"BOUNDARY64_PASS.json");f<<"{\"PASS\":true,\"alpha\":64,\"frequencies\":[63,64,65],\"pivot_vectors\":2,\"shared_rest_distinct_vectors\":1,\"rest_nodes\":63,\"query_checks\":"<<checks<<",\"roundtrip\":true,\"independent_object_frequency_and_scores\":true}\n";entry::finishFile(f);
}

static void runToyAcceptance(const filesystem::path& out){
 filesystem::create_directories(out);size_t checks=0;
 for(int alpha:{1,2,3,4,32,64}){
  frequency_requested_alpha=alpha;Dataset ds;ds.road_n=2;ds.objects_by_vertex.resize(3);ds.query_vecs={{0,1}};MarsIndex idx;idx.global.dim=2;
  vector<vector<float>> names={{1,0},{1,0},{1,0},{0,1},{-1,0},{0,-1}};
  map<int,vector<float>> cats={{10,{1,0}},{11,{0,1}},{12,{-1,0}},{13,{0,-1}},{14,{.6f,.8f}},{15,{.8f,.6f}}};
  vector<vector<int>> assigned={{10,10,13,14},{11,14,15},{12,15},{13},{},{}};
  map<vector<float>,set<uint32_t>> truth;
  for(int i=0;i<6;++i){ObjectRecord o;o.object_id=i;o.vertex_id=(i==3||i==4)?2:1;o.poi_id=i+1;o.category_ids=assigned[i];ds.objects.push_back(o);ds.objects_by_vertex[o.vertex_id].push_back(i);idx.global.poi_aspect_by_object.push_back(idx.global.entries.size());idx.global.entries.push_back({i,o.vertex_id,0,0,false});idx.global.matrix.insert(idx.global.matrix.end(),names[i].begin(),names[i].end());truth[names[i]].insert(i);for(int c:assigned[i])truth[cats[c]].insert(i);}
  for(auto&[cid,v]:cats){idx.global.cat_aspect_by_id[cid]=idx.global.entries.size();idx.global.entries.push_back({-1,-1,0,cid,true});idx.global.matrix.insert(idx.global.matrix.end(),v.begin(),v.end());}
  frequency_partition=buildFrequencyPartition(ds,idx.global,alpha);
  if(truth.size()!=frequency_partition.owners.size())throw runtime_error("toy vector merge mismatch");
  set<pair<uint32_t,uint32_t>> expectedRest;
  for(uint32_t uid=0;uid<frequency_partition.owners.size();++uid){vector<float>v(frequency_partition.vectorAt(uid),frequency_partition.vectorAt(uid)+2);set<uint32_t>own(frequency_partition.owners[uid].begin(),frequency_partition.owners[uid].end());if(own!=truth.at(v)||frequency_partition.channels[uid]!=(own.size()>=size_t(alpha)))throw runtime_error("toy independent frequency/partition mismatch");if(own.size()<size_t(alpha))for(auto oid:own)expectedRest.insert({uid,oid});}
  auto&h=idx.hierarchy;h.coords.resize(3);h.vertex_key={-1,0,1};h.key_vertex={1,2};CatIndex ci=buildCatIndex(ds,idx);NameGraph ng;ng.dim=2;ng.ready=true;populateRestMapping(ng);validateRestMapping(ng);
  set<pair<uint32_t,uint32_t>>got;for(size_t pid=0;pid<ng.num;++pid)got.insert({ng.pid2uid[pid],uint32_t(ng.pid2oid[pid])});if(got!=expectedRest||got.size()!=ng.num)throw runtime_error("toy missing/duplicate rest associations");
  // A fixed visited set bypasses ANN only in this synthetic fixture. Real graph search remains approximate.
  freq_fixture_rest_enumeration=true;Options o;o.k=10;o.tau=5000;o.param_name="ef";o.param_value="1600";vector<float>q={0,1};setenv("MARS_CAT_PIVOT","1",1);
  auto queryChecks=[&](){for(bool rest:{false,true})for(bool point:{false,true}){
   setenv("MARS_CAT_UNIQUE",rest?"1":"0",1);setenv("MARS_CAT_POINTWISE_ROAD",point?"1":"0",1);
   auto r=marsCatSearch(ds,idx,ci,ng,o,q,1,CatScoreMode::ChannelScore);checkProcessedMerge(r.ids,o.k);
   vector<pair<double,int>>rank;for(int oid=0;oid<6;++oid){if(ds.objects[oid].vertex_id!=1)continue;double best=-1e100;for(auto&[v,own]:truth)if(own.count(oid)&&(rest||own.size()>=size_t(alpha))){double s=double(v[0])*q[0]+double(v[1])*q[1];if(own.size()<size_t(alpha))s=double(float(s));best=max(best,s);}if(best>-1e99)rank.push_back({best,oid});}
   sort(rank.begin(),rank.end(),[](auto&a,auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});vector<int>ids;for(auto x:rank)ids.push_back(x.second);if(r.ids!=ids)throw runtime_error("toy reference max aggregation or reachability mismatch");++checks;
  }};
  queryChecks();auto dir=out/("alpha"+to_string(alpha));filesystem::create_directories(dir);
  saveFrequency(frequency_partition,dir/"frequency.bin");saveAux(idx,ci,ng.pid2oid,ng.pid2uid,dir/"aux.bin");
  bool rejected=false;try{auto wrong=loadFrequency(dir/"frequency.bin",alpha+1);(void)wrong;}catch(const exception&){rejected=true;}if(!rejected)throw runtime_error("mismatched alpha silently loaded");
  auto loaded=loadFrequency(dir/"frequency.bin",alpha);if(loaded.matrix!=frequency_partition.matrix||loaded.owners!=frequency_partition.owners||loaded.object_uids!=frequency_partition.object_uids||loaded.channels!=frequency_partition.channels||loaded.rest_uids!=frequency_partition.rest_uids||loaded.source_uids!=frequency_partition.source_uids)throw runtime_error("toy frequency roundtrip mismatch");
  frequency_partition=move(loaded);ng.pid2oid.clear();ng.pid2uid.clear();ci=CatIndex{};loadAux(ds,idx,ci,ng,dir/"aux.bin");ng.ready=true;queryChecks();
 }
 runAlpha64Boundary(out);
 freq_fixture_rest_enumeration=false;auto report=entry::fresh(out/"TOY_PASS.json");report<<"{\"PASS\":true,\"alphas\":[1,2,3,4,32,64],\"query_checks\":"<<checks<<",\"cases\":[\"frequency_equals_alpha\",\"shared_rest_below_alpha\",\"all_UID_object_pairs\",\"shared_rest_mixed_reachability\",\"multiple_rest_max_object_score\",\"equal_name_category_content\",\"duplicate_aspect_same_object\",\"empty_pivot\",\"fewer_than_k\",\"frequency_and_aux_roundtrip_query\",\"wrong_alpha_rejected\"],\"ANN\":\"synthetic fixed visited-set enumeration; no real graph constructed\"}\n";entry::finishFile(report);
}

#endif
