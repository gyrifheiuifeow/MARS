#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <type_traits>
#include <sys/resource.h>
namespace entry {
using Clock=std::chrono::steady_clock;
inline constexpr const char* phaseNames[]={"shared_load","method_preprocess","structure_build","save","reload","administration"};
struct Cost {
 double ms[6]{};int phase=0;Clock::time_point t=Clock::now(),start=t;bool active=true;
 void switchTo(int next){if(!active)return;auto n=Clock::now();ms[phase]+=std::chrono::duration<double,std::milli>(n-t).count();t=n;phase=next;}
 void stop(){switchTo(5);active=false;}
};
inline Cost* currentCost=nullptr;
inline void phase(int p){if(currentCost)currentCost->switchTo(p);}
inline uint64_t peakKiB(){rusage r{};if(getrusage(RUSAGE_SELF,&r))throw std::runtime_error("getrusage failed");return r.ru_maxrss;}
inline uint64_t rssKiB(){std::ifstream f("/proc/self/status");std::string s;while(std::getline(f,s))if(s.rfind("VmRSS:",0)==0){std::istringstream v(s.substr(6));uint64_t x;v>>x;return x;}throw std::runtime_error("VmRSS missing");}
inline std::ofstream fresh(const std::filesystem::path& p,bool binary=false){if(std::filesystem::exists(p))throw std::runtime_error("refuse overwrite: "+p.string());std::ofstream f(p,(binary?std::ios::binary:std::ios::out));if(!f)throw std::runtime_error("cannot write "+p.string());return f;}
inline void finishFile(std::ofstream& f){f.flush();if(!f.good())throw std::runtime_error("write failure");f.close();}
template<class T> void put(std::ostream& f,const T& v){static_assert(std::is_arithmetic_v<T>);f.write(reinterpret_cast<const char*>(&v),sizeof(v));}
template<class T> T get(std::istream& f){static_assert(std::is_arithmetic_v<T>);T v{};f.read(reinterpret_cast<char*>(&v),sizeof(v));if(!f)throw std::runtime_error("truncated state");return v;}
template<class T> void vec(std::ostream& f,const std::vector<T>& v){static_assert(std::is_arithmetic_v<T>);put<uint64_t>(f,v.size());if(!v.empty())f.write(reinterpret_cast<const char*>(v.data()),v.size()*sizeof(T));}
template<class T> std::vector<T> vec(std::istream& f,uint64_t max=100000000){auto n=get<uint64_t>(f);if(n>max)throw std::runtime_error("invalid state size");std::vector<T> v(n);if(n)f.read(reinterpret_cast<char*>(v.data()),n*sizeof(T));if(!f)throw std::runtime_error("truncated vector");return v;}
template<class T> void nested(std::ostream& f,const std::vector<std::vector<T>>& v){put<uint64_t>(f,v.size());for(const auto& r:v)vec(f,r);}
template<class T> std::vector<std::vector<T>> nested(std::istream& f){auto n=get<uint64_t>(f);if(n>100000000)throw std::runtime_error("invalid nested state");std::vector<std::vector<T>> a; a.reserve(n);for(uint64_t i=0;i<n;++i)a.push_back(vec<T>(f));return a;}
inline void magic(std::istream& f,uint64_t x){if(get<uint64_t>(f)!=x)throw std::runtime_error("state magic mismatch");}
inline void eof(std::ifstream& f){if(f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("trailing state bytes");}
struct SpaceRow{std::string file,role;uint64_t bytes,shared;};
inline std::vector<SpaceRow> space;
inline void account(const std::filesystem::path& file,uint64_t shared,const std::string& role){auto n=std::filesystem::file_size(file);if(n<shared)throw std::runtime_error("shared payload exceeds file");space.push_back({file.string(),role,n,shared});}
inline void spaceReport(const std::filesystem::path& out){auto f=fresh(out);f<<"file,role,serialized_bytes,embedded_shared_payload_bytes,additional_bytes\n";for(const auto& s:space)f<<s.file<<','<<s.role<<','<<s.bytes<<','<<s.shared<<','<<(s.bytes-s.shared)<<'\n';finishFile(f);}
inline void costReport(Cost& c,const std::filesystem::path& out,const std::string& kind){c.stop();auto peak=peakKiB(),rss=rssKiB();auto f=fresh(out);f<<std::setprecision(17)<<"phase,exclusive_ms\n";for(int i=0;i<6;++i)f<<phaseNames[i]<<','<<c.ms[i]<<'\n';finishFile(f);auto m=fresh(out.parent_path()/(kind+"_memory.json"));m<<"{\"process_peak_rss_kib_before_queries\":"<<peak<<",\"resident_rss_kib_before_queries\":"<<rss<<",\"includes_shared_input_load\":true,\"includes_serialization\":"<<(kind=="build"?"true":"false")<<",\"includes_validation_or_evaluation_queries\":false,\"scope\":\"process start through completed "<<kind<<"; captured before acceptance queries; no peak subtraction\"}\n";finishFile(m);}
inline std::vector<std::string> split(const std::string& s,char c){std::vector<std::string> a;std::istringstream f(s);std::string x;while(std::getline(f,x,c))a.push_back(x);return a;}
struct Task{std::string id,gt;int tau,k,budget,ef;};
inline std::vector<Task> tasks(const std::string& path){std::ifstream f(path);if(!f)throw std::runtime_error("tasks missing");std::string line;std::getline(f,line);std::vector<Task> a;while(std::getline(f,line)){auto x=split(line,'\t');if(x.size()!=7)throw std::runtime_error("bad task TSV");a.push_back({x[0],x[6],std::stoi(x[1]),std::stoi(x[2]),std::stoi(x[3]),std::stoi(x[4])});if(x[5]!="q1000_seed42_natural_medium")throw std::runtime_error("unexpected query tag");}return a;}
struct Pick{std::string tag;int id,row,vertex;};
inline std::vector<Pick> picks(const std::string& path){std::ifstream f(path);if(!f)throw std::runtime_error("query list missing");std::string line;std::getline(f,line);std::vector<Pick> a;while(std::getline(f,line)){auto x=split(line,',');if(x.size()<5)throw std::runtime_error("bad query CSV");a.push_back({x[1],std::stoi(x[2]),std::stoi(x[3]),std::stoi(x[4])});}return a;}
inline std::string ids(const std::vector<int>& v){std::ostringstream s;for(size_t i=0;i<v.size();++i){if(i)s<<';';s<<v[i];}return s.str();}
inline void allowed(const std::filesystem::path& p,const std::filesystem::path& workspace){auto a=std::filesystem::weakly_canonical(p).string(),b=std::filesystem::weakly_canonical(workspace).string()+"/";if(a.rfind(b,0)!=0)throw std::runtime_error("output outside new student version");}
}
