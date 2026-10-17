#pragma once
#include <fstream>
#include <sstream>
#include <queue>
#include <vector>
#include <limits>
#include <cmath>
#include <iomanip>
#include <cstdlib>
#include <stdexcept>
// Called ONLY by post-query assertions. No normal search code calls this helper.
// Original integer arcs are parsed independently; neither G nor H2H labels are reused.
namespace exact_assertion {
inline bool feasible(const std::string& region,int source0,int target0,double tau,double h2h){
 if(std::isfinite(h2h)&&h2h>=0&&h2h<=tau)return true;
 const char* manifest=std::getenv("EXACT_ROAD_UNITS_MANIFEST");
 const char* logfile=std::getenv("EXACT_ROAD_ASSERTION_LOG");
 if(!manifest||!logfile)throw std::runtime_error("missing bound exact-assertion inputs");
 std::ifstream mf(manifest);std::string line,path,sha,reg;long long scale=0;bool found=false;
 std::getline(mf,line);
 while(std::getline(mf,line)){std::istringstream row(line);long long s;std::string p,h;if(row>>reg>>s>>p>>h&&reg==region){scale=s;path=p;sha=h;found=true;break;}}
 // Formal radii are integer metres. Reject unsupported thresholds, never round or add epsilon.
 if(!found||scale<=0||!std::isfinite(tau)||tau<0||std::floor(tau)!=tau||tau>1e12)throw std::runtime_error("unverified road units or unsupported exact threshold");
 long long bound=static_cast<long long>(tau)*scale;
 using Edge=std::pair<int,long long>;std::vector<std::vector<Edge>> adj;
 std::ifstream in(path);if(!in)throw std::runtime_error("original integer graph unavailable");
 long long expected=-1,arcs=0;int n=0;
 while(std::getline(in,line)){
  if(line.empty()||line[0]=='c')continue;std::istringstream row(line);char type;row>>type;
  if(type=='p'){std::string kind;row>>kind>>n>>expected;if(n<=0||expected<0)throw std::runtime_error("bad original graph header");adj.resize(n);}
  else if(type=='a'){int u,v;long long w;if(!(row>>u>>v>>w)||u<1||v<1||u>n||v>n||w<=0)throw std::runtime_error("bad original integer arc");adj[u-1].push_back({v-1,w});++arcs;}
 }
 if(arcs!=expected||source0<0||target0<0||source0>=n||target0>=n)throw std::runtime_error("original graph/vertex binding invalid");
 const long long inf=std::numeric_limits<long long>::max();std::vector<long long> dist(n,inf);
 using Item=std::pair<long long,int>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> pq;
 dist[source0]=0;pq.push({0,source0});long long answer=-1;
 while(!pq.empty()){auto [d,u]=pq.top();pq.pop();if(d!=dist[u])continue;if(d>bound)break;if(u==target0){answer=d;break;}
  for(auto [v,w]:adj[u]){if(w>bound-d)continue;auto nd=d+w;if(nd<dist[v]){dist[v]=nd;pq.push({nd,v});}}
 }
 bool pass=answer>=0&&answer<=bound;
 std::ofstream log(logfile,std::ios::app);if(!log)throw std::runtime_error("exact assertion log unavailable");
 log<<std::setprecision(25)<<region<<'\t'<<source0+1<<'\t'<<target0+1<<'\t'<<tau<<'\t'<<h2h<<'\t'<<scale<<'\t'<<bound<<'\t'<<answer<<'\t'<<(pass?"PASS":"REJECT_NO_PATH_WITHIN_BOUND")<<'\t'<<sha<<'\n';
 log.flush();if(!log)throw std::runtime_error("exact assertion log write failed");return pass;
}
}
