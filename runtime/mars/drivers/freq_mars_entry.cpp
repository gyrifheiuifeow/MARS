#include "publication_paths.hpp"
#include "entry_support.hpp"
#define STUDENT_EMBEDDED_MAIN 1
#include "../src_tree/submission_experiments_20260726/src/mars_cat_runner.cpp"
#include "exact_road_assertion.hpp"
using entry::put;using entry::get;using entry::vec;
static const std::filesystem::path WORKSPACE=publicationOutputRoot(std::filesystem::path(__FILE__).parent_path().parent_path());
struct Task {string id,variant,manifest,workload;int tau,k,ef,alpha;};
struct Pick {string group,tag;int id,row,vertex;};
static vector<string> split(const string& line,char delimiter){stringstream s(line);vector<string> f;string v;while(getline(s,v,delimiter))f.push_back(v);return f;}
static vector<Task> loadTasks(const string& file){ifstream f(file);if(!f)throw runtime_error("task file missing");string line;getline(f,line);vector<Task> rows;while(getline(f,line)){auto a=split(line,'\t');if(a.size()!=8)throw runtime_error("bad task row");rows.push_back({a[0],a[3],a[5],a[6],stoi(a[1]),stoi(a[2]),stoi(a[4]),stoi(a[7])});}return rows;}
static vector<Pick> loadPicks(const string& file){ifstream f(file);if(!f)throw runtime_error("query list missing");string line;getline(f,line);vector<Pick> rows;while(getline(f,line)){auto a=split(line,',');if(a.size()<5)throw runtime_error("bad query list");rows.push_back({a[0],a[1],stoi(a[2]),stoi(a[3]),stoi(a[4])});}return rows;}
static string gtPath(string region,string tag,int tau,int k){string leaf=tag+"_tau"+to_string(tau)+"_k"+to_string(k)+".tsv";string p=marsRoot()+"/data/"+region+"/groundtruth/"+leaf;if(!ifstream(p).good())p=marsRoot()+"/results_tau_all_2000_10000_20260707_000802/groundtruth/"+region+"/"+leaf;if(!ifstream(p).good())throw runtime_error("existing GT missing: "+p);return p;}
static Options options(string region,string tag,int tau,int k,int ef){Options o;o.region=region;o.query_tag=tag;o.tau=tau;o.k=k;o.gt_path=gtPath(region,tag,tau,k);o.ann_seed=100;o.qg_degree=32;o.qg_ef_build=200;o.max_index_vectors=0;o.param_name="ef";o.param_value=to_string(ef);return o;}
static uint64_t idsHash(const vector<int>& ids){uint64_t h=1469598103934665603ULL;for(int id:ids){h^=uint64_t(id);h*=1099511628211ULL;}return h;}
static string idsText(const vector<int>& ids){ostringstream s;for(size_t i=0;i<ids.size();++i){if(i)s<<';';s<<ids[i];}return s.str();}
static string scoresText(const vector<int>& ids,const vector<float>& q,Dataset& ds){ostringstream s;s<<setprecision(17);for(size_t i=0;i<ids.size();++i){if(i)s<<';';s<<objectScore(q,ds.objects[ids[i]],ds);}return s.str();}
static uint64_t verifyResult(const vector<int>& ids,int source,double tau,int k,Dataset& ds){if(ids.size()>size_t(k))throw runtime_error("result exceeds k");unordered_set<int> seen;uint64_t calls=0;for(int id:ids){if(id<0||id>=int(ds.objects.size())||!seen.insert(id).second)throw runtime_error("invalid/duplicate result ID");double d=h2hQuery(source-1,ds.objects[id].vertex_id-1);++calls;if(!exact_assertion::feasible(getenv("EXACT_ROAD_REGION"),source-1,ds.objects[id].vertex_id-1,tau,d))throw runtime_error("result infeasible");}return calls;}
static void checkGraphHeader(string graph,size_t objects,size_t dim){ifstream f(graph,ios::binary);size_t h[4];f.read(reinterpret_cast<char*>(h),sizeof(h));if(!f||h[0]!=objects||h[1]!=32||h[2]!=dim||h[3]<dim)throw runtime_error("graph header does not match dataset/mapping/config");}
static void saveAux(const MarsIndex& idx,const CatIndex& ci,const vector<int>& names,const vector<uint32_t>& uids,const filesystem::path& path){
 auto f=entry::fresh(path,true);put<uint64_t>(f,0x4d41525341555832ULL);put<int32_t>(f,frequency_partition.alpha);put<uint64_t>(f,ci.dim);vec(f,names);vec(f,uids);const auto& h=idx.hierarchy;vec(f,h.scales);put<uint64_t>(f,h.coords.size());for(auto& c:h.coords){put<double>(f,c.x);put<double>(f,c.y);put<uint8_t>(f,c.present);}
 put<uint64_t>(f,h.levels.size());for(const auto& level:h.levels){put<uint64_t>(f,level.size());for(const auto& b:level){put<int32_t>(f,b.id);put<int32_t>(f,b.level);put<int32_t>(f,b.parent);put<int32_t>(f,b.representative_vertex);vec(f,b.children);vec(f,b.vertices);put<uint64_t>(f,b.workload);for(double v:{b.road_radius,b.min_x,b.max_x,b.min_y,b.max_y})put<double>(f,v);put<int32_t>(f,b.key_l);put<int32_t>(f,b.key_h);}}
 entry::nested(f,h.vertex_to_block);entry::nested(f,h.boundary_distance);vec(f,h.vertex_key);vec(f,h.key_vertex);vec(f,ci.slot_cid);put<uint64_t>(f,ci.postings.size());for(auto& p:ci.postings){put<uint64_t>(f,p.size());for(auto& e:p){put<int32_t>(f,e.dfs_key);put<int32_t>(f,e.object_id);}}
 entry::finishFile(f);entry::account(path,uint64_t(h.coords.size())*(2*sizeof(double)+sizeof(uint8_t)),"MARS road hierarchy pivot postings and mappings; shared road coordinates excluded");
}
static void loadAux(Dataset& ds,MarsIndex& idx,CatIndex& ci,NameGraph& ng,const filesystem::path& path){
 ifstream f(path,ios::binary);entry::magic(f,0x4d41525341555832ULL);if(get<int32_t>(f)!=frequency_requested_alpha)throw runtime_error("auxiliary alpha mismatch");ci.dim=get<uint64_t>(f);if(ci.dim!=ds.query_vecs.front().size())throw runtime_error("MARS dimension mismatch");ng.pid2oid=vec<int>(f);ng.pid2uid=vec<uint32_t>(f);ng.num=ng.pid2oid.size();ng.dim=ci.dim;
 auto& h=idx.hierarchy;h.scales=vec<double>(f);auto nc=get<uint64_t>(f);if(nc>100000000)throw runtime_error("bad coordinate count");h.coords.resize(nc);for(auto& c:h.coords){c.x=get<double>(f);c.y=get<double>(f);c.present=get<uint8_t>(f);}
 auto nl=get<uint64_t>(f);if(nl>100)throw runtime_error("bad hierarchy levels");h.levels.resize(nl);for(auto& level:h.levels){auto nb=get<uint64_t>(f);if(nb>100000000)throw runtime_error("bad block count");level.resize(nb);for(auto& b:level){b.id=get<int32_t>(f);b.level=get<int32_t>(f);b.parent=get<int32_t>(f);b.representative_vertex=get<int32_t>(f);b.children=vec<int>(f);b.vertices=vec<int>(f);b.workload=get<uint64_t>(f);b.road_radius=get<double>(f);b.min_x=get<double>(f);b.max_x=get<double>(f);b.min_y=get<double>(f);b.max_y=get<double>(f);b.key_l=get<int32_t>(f);b.key_h=get<int32_t>(f);}}
 h.vertex_to_block=entry::nested<int>(f);h.boundary_distance=entry::nested<float>(f);h.vertex_key=vec<int>(f);h.key_vertex=vec<int>(f);ci.slot_cid=vec<int>(f);auto np=get<uint64_t>(f);if(np!=ci.slot_cid.size())throw runtime_error("pivot map mismatch");ci.postings.resize(np);
 for(auto& p:ci.postings){auto n=get<uint64_t>(f);if(n>100000000)throw runtime_error("bad posting size");p.resize(n);for(auto& e:p){e.dfs_key=get<int32_t>(f);e.object_id=get<int32_t>(f);}ci.total_posting_entries+=n;}entry::eof(f);
 if(ci.dim!=frequency_partition.dim||ng.num!=restAssociationCount())throw runtime_error("frequency auxiliary dimension/count mismatch");
 for(int uid:ci.slot_cid){if(uid<0||size_t(uid)>=frequency_partition.owners.size()||!frequency_partition.channels[uid])throw runtime_error("pivot UID mismatch");ci.slot_vec.push_back(frequency_partition.vectorAt(uid));}
 validateRestMapping(ng);

}

#include "freq_acceptance.hpp"
int main(int argc,char**argv){try{
#if STUDENT_FREQ_ACCEPTANCE
 if(argc==3&&string(argv[1])=="unit"){runToyAcceptance(argv[2]);cout<<"TOY_ACCEPTANCE_PASS"<<endl;return 0;}
#endif
 if(argc!=7)throw runtime_error("usage mode region instance output tasks alpha");
 string alpha_text=argv[6];if(alpha_text.empty()||!all_of(alpha_text.begin(),alpha_text.end(),[](char c){return c>='0'&&c<='9';}))throw runtime_error("invalid explicit alpha");
 frequency_requested_alpha=stoi(alpha_text);if(frequency_requested_alpha<1)throw runtime_error("alpha must be positive");
 if(const char* e=getenv("STUDENT_FREQ_ALPHA")){if(string(e)!=alpha_text)throw runtime_error("environment alpha disagrees with explicit alpha");}

 string mode=argv[1],region=argv[2];filesystem::path instance=argv[3],outdir=argv[4];bool build=mode=="build",partition=mode=="partition",accept=mode=="accept",diag=mode=="diag";
 if(!build&&!partition&&!accept&&mode!="perf"&&!diag)throw runtime_error("bad mode");
 if((diag!=bool(STUDENT_DIAGNOSTICS))&&!(build||partition||accept))throw runtime_error("wrong timing/diagnostics executable");
 if(accept&&!STUDENT_FREQ_ACCEPTANCE)throw runtime_error("acceptance executable required");
 entry::allowed(outdir,WORKSPACE);filesystem::create_directories(outdir);
 if(build){entry::allowed(instance,WORKSPACE/"instances");filesystem::create_directories(instance);if(!filesystem::is_empty(instance))throw runtime_error("refuse overwrite index instance");}
 setenv("EXACT_ROAD_REGION",region.c_str(),1);setenv("EXACT_ROAD_ASSERTION_LOG",(outdir/"extra_exact_road_checks.tsv").c_str(),1);
 setenv("MARS_CAT_PIVOT","1",1);setenv("MARS_CAT_UNIQUE","1",1);setenv("MARS_CAT_UNIQUE_EF","1600",1);
 Options base=options(region,"q1000_seed42_natural_medium",6000,10,1600);entry::Cost cost;entry::currentCost=&cost;
 Dataset ds=loadDataset(base);MarsIndex idx;CatIndex ci;NameGraph ng;
 if(!partition)buildH2HIndex(region);
 entry::phase(1);idx.global=buildGlobalIndexData(ds,0); // preserve the existing full-data cache preparation order
 if(build||partition){
  frequency_partition=buildFrequencyPartition(ds,idx.global,frequency_requested_alpha);
  if(partition){saveFrequency(frequency_partition,outdir/"frequency_state.bin");exportCanonicalFrequency(frequency_partition,outdir/"canonical.bin");cout<<"PARTITION_COMPLETE "<<region<<endl;delete ds.poi_reader;delete ds.cat_reader;return 0;}
  entry::phase(2);idx.hierarchy=buildRoadBlockHierarchy(ds);ci=buildCatIndex(ds,idx);
  setenv("STUDENT_NAME_GRAPH_FILE",(instance/"rest_graph.bin").c_str(),1);setenv("FREQ_BUILD_ONLY","1",1);buildNameGraph(ds,idx,base,ng);
  if(ng.num!=restAssociationCount()||(ng.num>0&&ng.num<=32))throw runtime_error("real rest graph count unsupported or mismatched");
  entry::phase(3);if(ng.num)entry::account(instance/"rest_graph.bin",uint64_t(ng.num)*ng.dim*sizeof(float),"rest graph with explicit float payload copy excluded; graph quantization and topology retained");
  saveFrequency(frequency_partition,instance/"frequency_state.bin");saveAux(idx,ci,ng.pid2oid,ng.pid2uid,instance/"mars_aux.bin");
  auto metadata=entry::fresh(instance/"identity.json");metadata<<"{\"region\":\""<<region<<"\",\"alpha\":"<<frequency_partition.alpha<<",\"objects\":"<<ds.objects.size()<<",\"distinct_vectors\":"<<frequency_partition.owners.size()<<",\"pivot_vectors\":"<<ci.slot_cid.size()<<",\"pivot_postings\":"<<ci.total_posting_entries<<",\"rest_distinct_vectors\":"<<frequency_partition.rest_uids.size()<<",\"rest_associations\":"<<ng.num<<",\"old_name_graph_reuse\":false,\"seed_policy\":\"inherited random sources; ann_seed label 100 is not a fully controlled construction seed\"}\n";entry::finishFile(metadata);entry::account(instance/"identity.json",0,"frequency/index binding metadata");
  entry::phase(5);entry::spaceReport(outdir/"space.csv");entry::costReport(cost,outdir/"build_stages.csv","build");
  cout<<"BUILD_COMPLETE_RSS_CAPTURED_BEFORE_RELOAD_OR_VALIDATION "<<region<<endl;
  delete ds.poi_reader;delete ds.cat_reader;return 0;
 }
 entry::phase(4);frequency_partition=loadFrequency(instance/"frequency_state.bin",frequency_requested_alpha);if(frequency_partition.dim!=idx.global.dim||frequency_partition.object_uids.size()!=ds.objects.size())throw runtime_error("bound frequency state incompatible with data");
 loadAux(ds,idx,ci,ng,instance/"mars_aux.bin");if(ng.num){checkGraphHeader((instance/"rest_graph.bin").string(),ng.num,ng.dim);ng.qg.load((instance/"rest_graph.bin").c_str());ng.ready=true;}
 if(ng.num&&(ng.qg.num_vertices()!=ng.num||ng.qg.dimension()!=ng.dim))throw runtime_error("graph/UID/object mapping mismatch");
 entry::costReport(cost,outdir/"reload_stages.csv","reload");entry::currentCost=nullptr;
 auto tasks=loadTasks(argv[5]);if(tasks.empty())throw runtime_error("empty task list");for(const auto&t:tasks)if(t.alpha!=frequency_requested_alpha)throw runtime_error("task/index alpha mismatch");string currentTag=base.query_tag,currentGT=base.gt_path;
 ReferenceRoad referenceRoad;ReferencePivots referencePivots;
 if(accept){
  referenceRoad.load(region);referencePivots.load((getenv("MARS_REFERENCE_ROOT") ? filesystem::path(getenv("MARS_REFERENCE_ROOT")) : WORKSPACE/"reference")/(region+"_independent.bin"),frequency_requested_alpha);
  saveFrequency(frequency_partition,outdir/"roundtrip_frequency_state.bin");
  saveAux(idx,ci,ng.pid2oid,ng.pid2uid,outdir/"roundtrip_mars_aux.bin");
  if(ng.num)ng.qg.save((outdir/"roundtrip_rest_graph.bin").c_str());
 }
 filesystem::create_directories(outdir/"raw");
 for(const auto&task:tasks){
  auto picks=loadPicks(task.manifest);if(picks.empty())throw runtime_error("empty query list");
  setenv("MARS_CAT_POINTWISE_ROAD",task.variant=="pointwise"?"1":"0",1);setenv("MARS_CAT_UNIQUE",task.variant=="pivot"?"0":"1",1);setenv("MARS_CAT_UNIQUE_EF",to_string(task.ef).c_str(),1);
  auto raw=entry::fresh(outdir/"raw"/(task.id+".csv"));raw<<setprecision(17);
  raw<<"alpha,config_id,region,tau_m,k,variant,rest_ef,workload,query_tag,query_id,row_index,source_vertex,core_query_ms,extra_assertion_ms,extra_assertion_h2h,extra_evaluation_ms,returned_objects,GT_result_count,recall_denominator,score_recall,id_recall,result_hash,result_ids,exact_scores";
  if(diag)raw<<",init_ms,compile_ms,pivot_ms,rest_ms,result_materialization_ms,cleanup_return_ms,phase_sum_ms,phase_error_ms,h2h_init,h2h_compile,h2h_pivot,h2h_rest,h2h_result,h2h_cleanup,h2h_core_total,visited_blocks,euclidean_pruned_blocks,bound_h2h_requests,posting_direct,posting_interval,posting_fringe,posting_scan_total,binary_comparisons,pivot_dot_ms,pivot_sort_ms,pivot_posting_ms,pivot_subphase_overhead_ms";
  if(accept)raw<<",reference_pivot_equal,processed_merge_equal,road_variant_equal,roundtrip_results_equal,processed_scores";
  raw<<'\n';cout<<"CONFIG_START "<<task.id<<" queries="<<picks.size()<<endl;
  for(const auto&pick:picks){
   Options opt=options(region,pick.tag,task.tau,task.k,task.ef);
   if(currentTag!=pick.tag||currentGT!=opt.gt_path){loadTaskData(ds,opt);currentTag=pick.tag;currentGT=opt.gt_path;}
   if(pick.row<0||pick.row>=int(ds.query_vecs.size())||ds.query_meta[pick.row].query_id!=pick.id||ds.query_meta[pick.row].source_vertex_id!=pick.vertex)throw runtime_error("query manifest mismatch");
   const auto&q=ds.query_vecs[pick.row];auto t0=chrono::steady_clock::now();studentBeginCore(t0);
   auto result=marsCatSearch(ds,idx,ci,ng,opt,q,pick.vertex,CatScoreMode::ChannelScore);
   auto t1=chrono::steady_clock::now();studentEndCore(t1);double ms=chrono::duration<double,milli>(t1-t0).count();auto detail=student_diagnostics;
   auto v0=chrono::steady_clock::now();uint64_t assertion=verifyResult(result.ids,pick.vertex,task.tau,task.k,ds);auto v1=chrono::steady_clock::now();
   auto e0=chrono::steady_clock::now();double recall=recallAtScoreThreshold(result.ids,q,ds,pick.id,task.k),idr=recallAtIds(result.ids,ds,pick.id,task.k);string scores=scoresText(result.ids,q,ds);auto e1=chrono::steady_clock::now();
   int count=ds.gt.result_count.count(pick.id)?ds.gt.result_count.at(pick.id):0,denom=min(task.k,count);if(result.ids.size()>size_t(denom))throw runtime_error("result exceeds exact GT feasible count");
   string processed;
#if STUDENT_FREQ_ACCEPTANCE
   if(accept){
    checkProcessedMerge(result.ids,task.k);auto saved_scores=freq_returned_scores;ostringstream ss;ss<<setprecision(17);for(size_t i=0;i<saved_scores.size();++i){if(i)ss<<';';ss<<saved_scores[i];}processed=ss.str();
    auto feasible=referenceRoad.reachable(pick.vertex,task.tau);for(int id:result.ids)if(!feasible.at(ds.objects[id].vertex_id-1))throw runtime_error("independent integer road mismatch");
    if(task.variant=="pivot"&&referencePivots.exact(q,ds,feasible,task.k)!=result.ids)throw runtime_error("pivot-only differs from independent exact enumeration");
    if(task.variant!="pivot"){
     setenv("MARS_CAT_POINTWISE_ROAD",task.variant=="pointwise"?"0":"1",1);auto other=marsCatSearch(ds,idx,ci,ng,opt,q,pick.vertex,CatScoreMode::ChannelScore);checkProcessedMerge(other.ids,task.k);if(other.ids!=result.ids||freq_returned_scores!=saved_scores||recallAtScoreThreshold(other.ids,q,ds,pick.id,task.k)!=recall)throw runtime_error("full/direct ID score Recall mismatch");setenv("MARS_CAT_POINTWISE_ROAD",task.variant=="pointwise"?"1":"0",1);
    }
    // Reload into the existing graph to avoid holding two indexes during replay.
    if(ng.num&&pick.id==picks.front().id){ng.qg.load((outdir/"roundtrip_rest_graph.bin").c_str());ng.qg.set_ef(task.ef);}
    auto replay=marsCatSearch(ds,idx,ci,ng,opt,q,pick.vertex,CatScoreMode::ChannelScore);checkProcessedMerge(replay.ids,task.k);if(replay.ids!=result.ids||freq_returned_scores!=saved_scores)throw runtime_error("save/load query changed");
   }
#endif
   raw<<frequency_requested_alpha<<','<<task.id<<','<<region<<','<<task.tau<<','<<task.k<<','<<task.variant<<','<<task.ef<<','<<task.workload<<','<<pick.tag<<','<<pick.id<<','<<pick.row<<','<<pick.vertex<<','<<ms<<','<<chrono::duration<double,milli>(v1-v0).count()<<','<<assertion<<','<<chrono::duration<double,milli>(e1-e0).count()<<','<<result.ids.size()<<','<<count<<','<<denom<<','<<recall<<','<<idr<<','<<idsHash(result.ids)<<','<<idsText(result.ids)<<','<<scores;
   if(diag){double sum=0;uint64_t calls=0;for(int n=0;n<6;++n){sum+=detail.phase_ms[n];calls+=detail.h2h[n];raw<<','<<detail.phase_ms[n];}raw<<','<<sum<<','<<(sum-ms);if(abs(sum-ms)>1e-8||detail.h2h[1]!=detail.bound_h2h_requested||detail.bound_visits!=uint64_t(result.stats.visited_blocks)||detail.h2h[2]+detail.h2h[3]!=uint64_t(result.stats.feasible_pointwise_h2h_calls))throw runtime_error("diagnostic accounting mismatch");for(int n=0;n<6;++n)raw<<','<<detail.h2h[n];raw<<','<<calls<<','<<result.stats.visited_blocks<<','<<detail.euclidean_pruned<<','<<detail.bound_h2h_requested<<','<<detail.post_direct<<','<<detail.post_interval<<','<<detail.post_fringe<<','<<(detail.post_direct+detail.post_interval+detail.post_fringe)<<','<<detail.binary_comparisons;double parts=0;for(double v:detail.pivot_parts_ms){raw<<','<<v;parts+=v;}raw<<','<<(detail.phase_ms[2]-parts);}
   if(accept)raw<<",1,1,1,1,"<<processed;
   raw<<'\n';
  }
  entry::finishFile(raw);cout<<"CONFIG_DONE "<<task.id<<endl;
 }
 delete ds.poi_reader;delete ds.cat_reader;cout<<"BATCH_COMPLETE "<<region<<' '<<mode<<endl;return 0;
}catch(const exception&e){cerr<<"FAILED "<<e.what()<<endl;return 2;}}
