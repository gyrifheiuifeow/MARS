#include "publication_paths.hpp"
#include "entry_support.hpp"
#include "exact_road_assertion.hpp"
#define main legacy_mesh_main
#include "../src_tree/submission_experiments_20260726/src/mesh_submission_runner.cpp"
#undef main
#define main legacy_qscan_main
#include "../src_tree/road_qscan_probe/road_range_qscan_probe.cpp"
#undef main
#include "../src_tree/baselines/mars_baselines/qg_build_contract.hpp"


#include "vector_cache_preparation.hpp"

using entry::put;using entry::get;using entry::vec;
static const filesystem::path WORKSPACE=publicationOutputRoot(filesystem::path(__FILE__).parent_path().parent_path());
static void saveGlobalMap(const GlobalIndexData& g,const filesystem::path& path){
 auto f=entry::fresh(path,true);put<uint64_t>(f,0x474d415030303031ULL);put<uint64_t>(f,g.dim);put<uint64_t>(f,g.entries.size());
 for(auto& e:g.entries){put<int32_t>(f,e.object_id);put<int32_t>(f,e.vertex_id);put<int32_t>(f,e.group_id);put<int32_t>(f,e.category_id);put<uint8_t>(f,e.is_category);}
 entry::nested(f,g.entry_objects);entry::finishFile(f);entry::account(path,0,"aspect-to-object IDs and postings");
}
static GlobalIndexData loadGlobalMap(const filesystem::path& path){
 ifstream f(path,ios::binary);entry::magic(f,0x474d415030303031ULL);GlobalIndexData g;g.dim=get<uint64_t>(f);auto n=get<uint64_t>(f);if(n>100000000||g.dim!=768)throw runtime_error("invalid global map");
 g.entries.reserve(n);for(uint64_t i=0;i<n;++i){AspectEntry e;e.object_id=get<int32_t>(f);e.vertex_id=get<int32_t>(f);e.group_id=get<int32_t>(f);e.category_id=get<int32_t>(f);e.is_category=get<uint8_t>(f);g.entries.push_back(e);}
 g.entry_objects=entry::nested<int>(f);if(g.entry_objects.size()!=n)throw runtime_error("mapping size mismatch");entry::eof(f);return g;
}
static void saveQScan(const RoadQScanIndex& q,const filesystem::path& path){
 auto f=entry::fresh(path,true);put<uint64_t>(f,0x515343414e303031ULL);
 for(auto* store:{q.poi.get(),q.cat.get()}){put<int32_t>(f,store->bits);put<int32_t>(f,store->dim);put<double>(f,store->delta);put<uint64_t>(f,store->rows.size());for(const auto& row:store->rows){vec(f,row.codes);put<float>(f,row.error);}}
 entry::finishFile(f);entry::account(path,0,"deterministic 5-bit levels stored as uint8 codes plus errors");
}
static RoadQScanIndex loadQScan(Dataset& ds,const filesystem::path& path){
 ifstream f(path,ios::binary);entry::magic(f,0x515343414e303031ULL);RoadQScanIndex q;
 q.poi=make_unique<QuantizedStore>(*ds.poi_reader,5);q.cat=make_unique<QuantizedStore>(*ds.cat_reader,5);
 for(auto* store:{q.poi.get(),q.cat.get()}){auto bits=get<int32_t>(f),dim=get<int32_t>(f);auto delta=get<double>(f);auto n=get<uint64_t>(f);if(bits!=5||dim!=store->dim||delta!=store->delta||n!=store->rows.size())throw runtime_error("quantization binding mismatch");for(size_t i=0;i<n;++i){store->rows[i].codes=vec<uint8_t>(f,100000);store->rows[i].error=get<float>(f);if(i&&store->rows[i].codes.size()!=size_t(dim))throw runtime_error("quantized row shape mismatch");}}
 entry::eof(f);return q;
}
static void saveMeshState(const MeshRoadRuntime& r,const filesystem::path& path){
 auto f=entry::fresh(path,true);put<uint64_t>(f,0x4d45534852543031ULL);put<double>(f,r.alpha);put<uint64_t>(f,r.coords.size());
 for(const auto& c:r.coords){put<double>(f,c.x);put<double>(f,c.y);put<uint8_t>(f,c.present);}
 put<int32_t>(f,r.tree.rootId());
 vector<int> ids(r.selected_nodes.begin(),r.selected_nodes.end());sort(ids.begin(),ids.end());vec(f,ids);put<uint64_t>(f,r.indexed_aspects);
 entry::finishFile(f);entry::account(path,uint64_t(r.coords.size())*(2*sizeof(double)+sizeof(uint8_t)),"Mesh runtime: road coordinates excluded; tree and selected-node maps retained");
}
static MeshRoadRuntime loadMeshState(const filesystem::path& path,const filesystem::path& nodes){
 ifstream f(path,ios::binary);entry::magic(f,0x4d45534852543031ULL);MeshRoadRuntime r;r.alpha=get<double>(f);auto nc=get<uint64_t>(f);if(nc>100000000)throw runtime_error("invalid road coords");r.coords.resize(nc);
 for(auto& c:r.coords){c.x=get<double>(f);c.y=get<double>(f);c.present=get<uint8_t>(f);}
 auto root=get<int32_t>(f);ifstream treeFile(nodes/"str.meta",ios::binary);entry::magic(treeFile,0x4d45534853545231ULL);if(get<double>(treeFile)!=r.alpha)throw runtime_error("STR alpha mismatch");auto nn=get<uint64_t>(treeFile);if(nn>10000000)throw runtime_error("invalid STR count");vector<meshroad::STRNode> tree;tree.reserve(nn);
 for(uint64_t i=0;i<nn;++i){meshroad::STRNode n;n.id=get<int32_t>(treeFile);n.parent=get<int32_t>(treeFile);n.leaf=get<uint8_t>(treeFile);n.box.min_x=get<double>(treeFile);n.box.min_y=get<double>(treeFile);n.box.max_x=get<double>(treeFile);n.box.max_y=get<double>(treeFile);n.children=vec<int>(treeFile);n.object_ids=vec<int>(treeFile);tree.push_back(std::move(n));}entry::eof(treeFile);
 r.tree.restoreFrozen(root,std::move(tree));auto ids=vec<int>(f);r.selected_nodes.insert(ids.begin(),ids.end());r.indexed_aspects=get<uint64_t>(f);entry::eof(f);r.frozen_placement=true;
 for(int node:ids){auto h=make_unique<meshroad::LocalHNSW>();h->load(nodes/ ("node_"+to_string(node)));r.local_indexes.emplace(node,std::move(h));}return r;
}
struct BaselineRuntime{
 GlobalIndexData global;unique_ptr<rabitqlib::hnsw::HierarchicalNSW> hnsw;unique_ptr<rabitqlib::symqg::QuantizedGraph<float>> qg;RoadQScanIndex qscan;MeshRoadRuntime mesh;
};
static Options taskOptions(Options base,const entry::Task& t,const string& method){
 base.tau=t.tau;base.k=t.k;base.gt_path=t.gt;base.query_tag="q1000_seed42_natural_medium";
 if(method=="HNSW"||method=="SymphonyQG"){base.ef_search=t.ef;base.candidate_budget=t.budget;base.max_candidate_budget=t.budget;if(t.budget<base.k)throw runtime_error("budget below k");}
 if(method=="Mesh-Road") {auto m=meshroad::meshSearchParametersForTask(base.mesh_ef,base.mesh_candidate_budget,base.mesh_max_candidates,"mesh_candidate_budget",to_string(t.budget),base.k);base.mesh_ef=m.ef;base.mesh_candidate_budget=m.candidate_budget;base.mesh_max_candidates=m.max_candidates;if(base.mesh_ef!=t.ef||base.mesh_candidate_budget!=t.budget||base.mesh_max_candidates!=t.budget)throw runtime_error("Mesh parameter mapping mismatch");}
 return base;
}
struct Returned {vector<int> ids;int budget=0,actual_ef=0;};
static Returned searchNew(const string& method,Dataset& ds,BaselineRuntime& r,const Options& o,const vector<float>& q,int source){
 if(method=="HNSW"){
  const char* restart_flag=getenv("STUDENT_HNSW_EMPTY_RESTART");
  r.hnsw->set_empty_result_restart(restart_flag && string(restart_flag)=="1");
  auto a=adaptiveFilteredGlobalCandidatesTimedCumulative(ds,r.global,q,source,o.tau,o.k,o.candidate_budget,o.max_candidate_budget,o.candidate_growth,[&](int budget,auto& feasible){auto rows=r.hnsw->search_filtered(q.data(),1,budget,max(o.ef_search,budget),1,feasible);vector<rabitqlib::PID> ids;for(auto& p:rows[0])ids.push_back(p.second);return ids;});
  return {std::move(a.ids),a.final_budget,max(o.ef_search,a.final_budget)};
 }
 if(method=="SymphonyQG"){
  auto a=adaptiveFilteredGlobalCandidatesTimedCumulative(ds,r.global,q,source,o.tau,o.k,o.candidate_budget,o.max_candidate_budget,o.candidate_growth,[&](int budget,auto& feasible){r.qg->set_ef(size_t(max(o.ef_search,budget)));vector<rabitqlib::PID> ids(budget,rabitqlib::PID(r.global.entries.size()));r.qg->search_filtered(q.data(),budget,ids.data(),feasible);return ids;});
  return {std::move(a.ids),a.final_budget,max(o.ef_search,a.final_budget)};
 }
 if(method=="Mesh-Road"){auto a=searchMeshRoad(ds,r.mesh,o,q,source);return {std::move(a.ids),a.final_budget,o.mesh_ef};}
 if(method=="RoadScan"){auto a=roadRangeQScanEstimateOnly(ds,r.qscan,q,source,o.tau,o.k);return {std::move(a.ids),0,0};}
 throw runtime_error("unknown method");
}
// Reference wrappers preserve the original runner's calls. Mesh uses its unedited original body.
static vector<int> searchReference(const string& method,Dataset& ds,BaselineRuntime& r,const Options& o,const vector<float>& q,int source){
 if(method=="Mesh-Road")return reference_searchMeshRoad(ds,r.mesh,o,q,source).ids;
 if(method=="RoadScan")return roadRangeQScanEstimateOnly(ds,r.qscan,q,source,o.tau,o.k).ids;
 if(method=="HNSW")return adaptiveFilteredGlobalCandidatesTimedCumulative(ds,r.global,q,source,o.tau,o.k,o.candidate_budget,o.max_candidate_budget,o.candidate_growth,[&](int budget,auto& feasible){auto rows=r.hnsw->search_filtered(q.data(),1,budget,max(o.ef_search,budget),1,feasible);vector<rabitqlib::PID> ids;for(auto& p:rows[0])ids.push_back(p.second);return ids;}).ids;
 return adaptiveFilteredGlobalCandidatesTimedCumulative(ds,r.global,q,source,o.tau,o.k,o.candidate_budget,o.max_candidate_budget,o.candidate_growth,[&](int budget,auto& feasible){r.qg->set_ef(size_t(max(o.ef_search,budget)));vector<rabitqlib::PID> ids(budget,rabitqlib::PID(r.global.entries.size()));r.qg->search_filtered(q.data(),budget,ids.data(),feasible);return ids;}).ids;
}
static void verify(const vector<int>& ids,Dataset& ds,const entry::Pick& q,const Options& o){
 meshroad::validatePrediction(ids,o.k,ds.objects.size(),[&](int id){double d=h2hQuery(q.vertex-1,ds.objects[id].vertex_id-1);return exact_assertion::feasible(o.region,q.vertex-1,ds.objects[id].vertex_id-1,o.tau,d);});
}
static string exactScores(const vector<int>& ids,const vector<float>& q,Dataset& ds){ostringstream f;f<<setprecision(17);for(size_t i=0;i<ids.size();++i){if(i)f<<';';f<<objectScore(q,ds.objects[ids[i]],ds);}return f.str();}
static void queries(const string& mode,const string& method,Dataset& ds,BaselineRuntime& r,Options base,const vector<entry::Task>& tasks,const vector<entry::Pick>& picks,const filesystem::path& output){
 if((mode=="build_accept"||mode=="accept")&&picks.size()>30)throw runtime_error("acceptance query limit exceeded");
 for(auto& t:tasks){Options o=taskOptions(base,t,method);loadTaskData(ds,o);auto f=entry::fresh(output/(t.id+".csv"));f<<setprecision(17);
  f<<"task,method,query_id,row_index,source_vertex,tau_m,k,ef_config,ef_actual,candidate_budget,max_candidate_budget,final_budget,limit_scope,core_query_ms,extra_validation_ms,evaluation_ms,GT_count,recall_denominator,Recall,ID_Recall,result_ids,exact_scores,reference_equal,score_mode,shared_score_reuse\n";
  for(auto& pick:picks){if(ds.query_meta.at(pick.row).query_id!=pick.id||ds.query_meta.at(pick.row).source_vertex_id!=pick.vertex||pick.tag!=o.query_tag)throw runtime_error("query manifest mismatch");const auto& q=ds.query_vecs[pick.row];double ms=0;bool equal=true;auto begin=entry::Clock::now();Returned a;
   if(mode=="build_accept"){a.ids=searchReference(method,ds,r,o,q,pick.vertex);a.budget=method=="RoadScan"?0:t.budget;a.actual_ef=method=="RoadScan"?0:(method=="Mesh-Road"?o.mesh_ef:max(o.ef_search,t.budget));}
   else {a=searchNew(method,ds,r,o,q,pick.vertex);auto end=entry::Clock::now();ms=chrono::duration<double,milli>(end-begin).count();}
   auto v0=entry::Clock::now();verify(a.ids,ds,pick,o);if(mode=="accept")equal=(a.ids==searchReference(method,ds,r,o,q,pick.vertex));auto v1=entry::Clock::now();if(!equal)throw runtime_error("legacy/new result mismatch");
   auto e0=entry::Clock::now();auto recall=recallAtScoreThreshold(a.ids,q,ds,pick.id,o.k),idr=recallAtIds(a.ids,ds,pick.id,o.k);auto scores=exactScores(a.ids,q,ds);auto e1=entry::Clock::now();int count=ds.gt.result_count.at(pick.id),denom=min(o.k,count);
   if(method!="RoadScan"&&a.budget>t.budget)throw runtime_error("unexpected candidate expansion");
   if(a.ids.size()>size_t(denom))throw runtime_error("returned more objects than GT permits");
   f<<t.id<<','<<method<<','<<pick.id<<','<<pick.row<<','<<pick.vertex<<','<<o.tau<<','<<o.k<<','<<(method=="RoadScan"?0:t.ef)<<','<<a.actual_ef<<','<<(method=="RoadScan"?0:t.budget)<<','<<(method=="RoadScan"?0:t.budget)<<','<<a.budget<<','<<(method=="Mesh-Road"?"per_local_index":method=="RoadScan"?"all_reachable_objects":"global_aspect_candidates")<<','<<ms<<','<<chrono::duration<double,milli>(v1-v0).count()<<','<<chrono::duration<double,milli>(e1-e0).count()<<','<<count<<','<<denom<<','<<recall<<','<<idr<<','<<entry::ids(a.ids)<<','<<scores<<','<<equal<<','<<(method=="RoadScan"?"estimate_only":"inherited_baseline")<<','<<(method=="RoadScan"?"OFF":"not_applicable")<<'\n';
  }entry::finishFile(f);
 }
}
int main(int argc,char** argv){try{
 if(argc!=10&&argc!=11)throw runtime_error("usage: entry mode method region instance output tasks queries seed [frozen_mesh_placement]");
 string mode=argv[1],method=argv[2],region=argv[3];filesystem::path instance=argv[4],output=argv[5];bool build=mode=="build"||mode=="build_accept";
 if(build)throw runtime_error("query-only runtime: unchanged Exp8 construction uses build_runtime");
 if(!build&&mode!="load"&&mode!="accept"&&mode!="cache_reference")throw runtime_error("bad mode");if(method!="HNSW"&&method!="SymphonyQG"&&method!="RoadScan"&&method!="Mesh-Road")throw runtime_error("bad method");
 entry::allowed(output,WORKSPACE);filesystem::create_directories(output);if(build){entry::allowed(instance,WORKSPACE);filesystem::create_directories(instance);}else if(!filesystem::is_directory(instance))throw runtime_error("read-only instance directory missing");
 if(build&&!filesystem::is_empty(instance))throw runtime_error("refuse nonempty instance directory (including failed artifacts)");if(!build&&!filesystem::exists(instance/"state_kind.txt"))throw runtime_error("unbound instance");
 auto tasks=entry::tasks(argv[6]);auto picks=entry::picks(argv[7]);if(tasks.empty())throw runtime_error("empty tasks");
 uint64_t seed=stoull(argv[8]);if(string(argv[9])!="reuse_off")throw runtime_error("RoadScan reuse must be explicitly OFF");
 Options base;base.region=region;base.query_tag="q1000_seed42_natural_medium";base.ann_seed=seed;base.mesh_seed=seed;base.quant_bits=5;base.qg_degree=32;base.qg_ef_build=200;base.hnsw_m=16;base.hnsw_ef_build=100;base.max_index_vectors=0;base.mesh_leaf_capacity=10000;base.mesh_memory_mb=region=="COL"?4000:8000;base.mesh_ef=(region=="NYC"||region=="BAY")?128:256;base.mesh_candidate_budget=base.mesh_max_candidates=base.mesh_ef;base.mesh_index_dir=(instance/"nodes").string();
 if(method=="Mesh-Road"){if(argc!=11)throw runtime_error("frozen Mesh placement required");base.mesh_frozen_placement_manifest=argv[10];}
 base=taskOptions(base,tasks.front(),method);entry::Cost cost;entry::currentCost=&cost;Dataset ds=loadDataset(base);buildH2HIndex(region);BaselineRuntime runtime;
 if(build){
  if(method=="HNSW"||method=="SymphonyQG"){
   entry::phase(1);runtime.global=buildGlobalIndexData(ds,0);entry::phase(2);
   if(method=="HNSW"){runtime.hnsw=make_unique<rabitqlib::hnsw::HierarchicalNSW>(runtime.global.entries.size(),runtime.global.dim,5,16,100,seed,rabitqlib::METRIC_IP);runtime.hnsw->construct(11,runtime.global.centroids.data(),runtime.global.entries.size(),runtime.global.matrix.data(),runtime.global.cluster_ids.data(),1,true);entry::phase(3);runtime.hnsw->save((instance/"index.bin").c_str());entry::account(instance/"index.bin",0,"HNSW links quantization centroids labels; raw input matrix not serialized");}
   else {rabitqlib::reset_random_seed(static_cast<std::mt19937::result_type>(seed));runtime.qg=make_unique<rabitqlib::symqg::QuantizedGraph<float>>(runtime.global.entries.size(),runtime.global.dim,reviewer_contract::requireSIMDSafeQGDegree(32),rabitqlib::METRIC_IP);auto builder=reviewer_contract::makeSingleThreadQGBuilder(*runtime.qg,200,runtime.global.matrix.data());builder.build();entry::phase(3);runtime.qg->save((instance/"index.bin").c_str());entry::account(instance/"index.bin",uint64_t(runtime.global.entries.size())*runtime.global.dim*sizeof(float),"Symphony graph quantization and links; raw vectors excluded");}
   saveGlobalMap(runtime.global,instance/"mapping.bin");
  }else if(method=="RoadScan"){entry::phase(1);runtime.qscan=buildRoadQScanIndex(ds,5);entry::phase(3);saveQScan(runtime.qscan,instance/"qscan_state.bin");}
  else {runtime.mesh=buildMeshRoadRuntime(ds,base);entry::phase(3);saveMeshState(runtime.mesh,instance/"runtime.bin");}
  auto tag=entry::fresh(instance/"state_kind.txt");tag<<method<<'\n'<<region<<'\n'<<seed<<'\n';entry::finishFile(tag);entry::account(instance/"state_kind.txt",0,"instance method region and seed label");entry::phase(5);entry::spaceReport(output/"space.csv");entry::costReport(cost,output/"build_stages.csv","build");
 }else{
  ifstream tag(instance/"state_kind.txt");string m,r;uint64_t saved;tag>>m>>r>>saved;if(m!=method||r!=region||saved!=seed)throw runtime_error("instance identity mismatch");entry::phase(4);
  if(method=="HNSW"||method=="SymphonyQG"){runtime.global=loadGlobalMap(instance/"mapping.bin");if(method=="HNSW"){runtime.hnsw=make_unique<rabitqlib::hnsw::HierarchicalNSW>();runtime.hnsw->load((instance/"index.bin").c_str());if(runtime.hnsw->dimension()!=runtime.global.dim||runtime.hnsw->max_elements()!=runtime.global.entries.size())throw runtime_error("HNSW mapping mismatch");}else{runtime.qg=make_unique<rabitqlib::symqg::QuantizedGraph<float>>();runtime.qg->load((instance/"index.bin").c_str());if(runtime.qg->num_vertices()!=runtime.global.entries.size()||runtime.qg->dimension()!=runtime.global.dim)throw runtime_error("QG mapping mismatch");}}
  else if(method=="RoadScan")runtime.qscan=loadQScan(ds,instance/"qscan_state.bin");else runtime.mesh=loadMeshState(instance/"runtime.bin",instance/"nodes"/region);
  entry::costReport(cost,output/"reload_stages.csv","reload");
  if(mode=="accept"&&getenv("ENTRY_REPEAT_SERIALIZATION_CHECK")){
   auto rt=output/"roundtrip";filesystem::create_directories(rt);
   if(method=="HNSW"){runtime.hnsw->save((rt/"index.bin").c_str());saveGlobalMap(runtime.global,rt/"mapping.bin");}
   else if(method=="SymphonyQG"){runtime.qg->save((rt/"index.bin").c_str());saveGlobalMap(runtime.global,rt/"mapping.bin");}
   else if(method=="RoadScan")saveQScan(runtime.qscan,rt/"qscan_state.bin");
   else {auto nd=rt/"nodes"/region;filesystem::create_directories(nd);saveTreeMetadata(runtime.mesh.tree,runtime.mesh.alpha,nd/"str.meta");for(auto& item:runtime.mesh.local_indexes)item.second->save(nd/("node_"+to_string(item.first)));saveMeshState(runtime.mesh,rt/"runtime.bin");}
  }
 }
 // Fixed raw-vector cache preparation is outside all query timers and outside Exp8 build paths.
 prepareVectorReaderCaches(ds,output,mode=="cache_reference");
 // No evaluation query can execute before the construction/reload peak snapshot above.
 if(mode!="build"&&mode!="cache_reference")queries(mode,method,ds,runtime,base,tasks,picks,output);
 recordVectorReaderCacheState(ds,output,"after_queries");
 delete ds.poi_reader;delete ds.cat_reader;cout<<"ENTRY_COMPLETE "<<mode<<' '<<method<<endl;return 0;
}catch(const exception& e){cerr<<"ENTRY_FAILED "<<e.what()<<endl;return 2;}}
