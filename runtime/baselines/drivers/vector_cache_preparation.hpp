#pragma once
// Prepare and record vector-reader caches before query timing.
static void recordVectorReaderCacheState(Dataset& ds,const filesystem::path& output,const string& label) {
 auto names=ds.poi_reader->cachedVectorIds(),categories=ds.cat_reader->cachedVectorIds();
 auto fingerprint=[](const vector<int>& ids){uint64_t h=1469598103934665603ULL;for(int id:ids){h^=uint32_t(id);h*=1099511628211ULL;}return h;};
 auto nf=entry::fresh(output/(label+"_name_ids.txt"));for(int id:names)nf<<id<<'\n';entry::finishFile(nf);
 auto cf=entry::fresh(output/(label+"_category_ids.txt"));for(int id:categories)cf<<id<<'\n';entry::finishFile(cf);
 auto f=entry::fresh(output/(label+"_cache_state.json"));
 f<<"{\"name_count\":"<<names.size()<<",\"category_count\":"<<categories.size()<<",\"name_capacity\":"<<ds.poi_reader->cacheCapacity()<<",\"category_capacity\":"<<ds.cat_reader->cacheCapacity()<<",\"name_bucket_count\":"<<ds.poi_reader->cacheBucketCount()<<",\"category_bucket_count\":"<<ds.cat_reader->cacheBucketCount()<<",\"name_ids_fnv64\":\""<<fingerprint(names)<<"\",\"category_ids_fnv64\":\""<<fingerprint(categories)<<"\"}\n";entry::finishFile(f);
}
static void prepareVectorReaderCaches(Dataset& ds,const filesystem::path& output,bool original_mars_preparation_reference) {
 if(ds.poi_reader->cacheCapacity()!=200000||ds.cat_reader->cacheCapacity()!=200000)throw runtime_error("reader cache capacity differs from frozen rule");
 recordVectorReaderCacheState(ds,output,"before_preparation");
 auto start=entry::Clock::now();size_t name_calls=ds.objects.size(),category_calls=0;
 if(original_mars_preparation_reference) {
  // Replay vector and mapping preparation to obtain the reference cache state.
  auto data=buildGlobalIndexData(ds,0);category_calls=data.cat_aspect_by_id.size();
 } else {
  ds.poi_reader->resetReadCacheForPreparation();ds.cat_reader->resetReadCacheForPreparation();
  // Cache category centres, object names in data order, then sorted category IDs.
  for(int cid:ds.category_map.group_cat_id){ds.cat_reader->readNorm1Based(cid);++category_calls;}
  set<int> category_ids;
  for(const auto& object:ds.objects){ds.poi_reader->readNorm1Based(object.poi_id);for(int cid:object.category_ids)category_ids.insert(cid);}
  for(int cid:category_ids){ds.cat_reader->readNorm1Based(cid);++category_calls;}
 }
 auto end=entry::Clock::now();
 auto f=entry::fresh(output/"vector_cache_preparation.json");
 f<<setprecision(17)<<"{\"rule\":\"dataset order of name IDs; category centres then ascending used category IDs; original first-in cache capacity retained\",\"reference_original_buildGlobalIndexData\":"<<(original_mars_preparation_reference?"true":"false")<<",\"preparation_ms\":"<<chrono::duration<double,milli>(end-start).count()<<",\"name_readNorm_calls\":"<<name_calls<<",\"category_readNorm_calls\":"<<category_calls<<",\"uses_evaluation_queries_or_GT_for_selection\":false,\"query_timer_includes_preparation\":false,\"Exp8_build_path_modified\":false,\"RoadScan_score_reuse\":\"OFF\"}\n";entry::finishFile(f);
 recordVectorReaderCacheState(ds,output,"prepared");
}
