#include<bits/stdc++.h>
#include<malloc.h>
#include<unordered_map>
#include "h2h.h"
using namespace std;

static string marsRoot() {
    const char* env = getenv("MARS_ROOT");
    if (env && env[0]) return string(env);
    ifstream local_data("data/NYC/USA-road-d.NYC.gr");
    if (local_data.good()) return string(".");
    ifstream parent_data("../data/NYC/USA-road-d.NYC.gr");
    if (parent_data.good()) return string("..");
    return string("..");
}

static string pathJoin(const string& a, const string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

int N, M;//# of vertices and edges
vector<DD> coords;
long long hopsize, npathConcat;//# of path concatenations
double optw = DBL_MAX;//optimal answer
int treeheight = 0, treewidth = 0, treeavgheight = 0;
#define PI 3.14
#define RADIO_TERRESTRE 6370000.0
#define GRADOS_RADIANES PI / 180

double calculateDistance(int u, int v) {
    double latitud1 = coords[u].second, longitud1 = coords[u].first, latitud2 = coords[v].second, longitud2 = coords[v].first;
    double haversine;
    double temp;
    double distancia_puntos;

    latitud1  = latitud1  * GRADOS_RADIANES;
    longitud1 = longitud1 * GRADOS_RADIANES;
    latitud2  = latitud2  * GRADOS_RADIANES;
    longitud2 = longitud2 * GRADOS_RADIANES;

    haversine = (pow(sin((1.0 / 2) * (latitud2 - latitud1)), 2)) + ((cos(latitud1)) * (cos(latitud2)) * (pow(sin((1.0 / 2) * (longitud2 - longitud1)), 2)));
    temp = 2 * asin(min(1.0, sqrt(haversine)));
    distancia_puntos = RADIO_TERRESTRE * temp;

   return distancia_puntos-0.3;
}
struct node{
    int depth;
    int ran;//rank in the order
    int parent;
    vector<int> children;
    vector<int> X;
};
node T[MAX_V];

struct edge{
    int from, to;
    double weight;
    int id;
    edge(int a,int b,double w){
        from = a, to = b, weight = w;
    }
    bool operator <(const edge p) const{
        return id < p.id;
    }
};
vector<double> Lh2h[MAX_V];
int root = -1;
unordered_map<int, double> adj[MAX_V];//contains only edges to higher rank
unordered_map<int,int> adjo[MAX_V], adjT[MAX_V];//contains all the edges
int nsym;
int logn2[1 << 27];

vector<int> order;
bool flag[MAX_V];
bool cmp(const int &a, const int &b){
    return T[a].ran > T[b].ran;
}

vector<int> ordergen;
int del[MAX_V];//deleted neighbors
double update(int v){
//priorities for contracting vertices
    return 1000 * adjo[v].size() + del[v];
}
typedef pair<II, int> III;
void genorder(string filename, bool writeflag){
//first generating an order of contracting vertices
    priority_queue<II, vector<II>, greater<II> > degque;
    for (int i = 0; i < N; i++)
        degque.push(II(update(i), i));
    int iter = -1, totnewedge = 0;
    while(!degque.empty()){
        II ii = degque.top();
        degque.pop();
        int v = ii.second;
        if(flag[v])
            continue;
        double pri = update(v);
        if (pri > degque.top().first){//lazy update
            degque.push(II(pri,v));
            continue;
        }
        iter += 1;
        flag[v] = 1;
        ordergen.push_back(v);
        T[v].ran = iter;
        unordered_map<int, int>::iterator it;
        vector<int> nei;
        for (it = adjo[v].begin(); it !=adjo[v].end(); it++)
            if(!flag[it->first])
                nei.push_back(it->first);
        int lenX = nei.size();
        for (int j = 0; j < lenX; j++){
            int u = nei[j];
            for (int k = j + 1; k < lenX; k++){
                int w = nei[k];
                if(adjo[u].count(w)==0){
                    adjo[u][w] = 1;
                    adjo[w][u] = 1;
                    totnewedge += 1;
                }
            }
            //adjo[u].erase(v);
            del[u]++;
        }
    }
    if(writeflag){
        FILE *fp_order = fopen(filename.c_str(), "w");
        for (int i = 0; i < N;i++){
            fprintf(fp_order, "%d\n", T[i].ran);
        }
        fclose(fp_order);
    }
}

double maxlabelsize, avglabelsize;
int descnt[MAX_V];
vector<int> ancarray[MAX_V];//the indices (or depth) for X(v)'s nodes
queue<int> bfs, bfssave, bfsanc;
void treedec(){
    for (int i = 0; i < N; i++){
        int v = ordergen[i];
        if(i%100000==0)
            printf("%d\n", i);
        unordered_map<int, double>::iterator it;  
        for (it = adj[v].begin(); it !=adj[v].end(); it++)
            T[v].X.push_back(it->first);
        int lenX = T[v].X.size();
        for (int j = 0; j < lenX; j++){
            int u = T[v].X[j];
            //printf("u%d:", u+1);
            for (int k = j + 1; k < lenX; k++){
                int w = T[v].X[k];
                //printf("w%d ", w + 1);
                if(T[u].ran<T[w].ran){
                    if(adj[u].count(w)==0){
                        adj[u][w] = adj[v][w] + adj[v][u];
                    }
                    adj[u][w] = min(adj[u][w], adj[v][u] + adj[v][w]);
                    //printf("v%d u%d w%d:%f\n",v+1,u+1, w + 1,adj[u][w]);
                }
                else{
                    if(adj[w].count(u)==0){
                        adj[w][u] = adj[v][w] + adj[v][u];
                    }
                    adj[w][u] = min(adj[w][u], adj[v][u] + adj[v][w]);
                    //printf("v%d u%d w%d:%f\n",v+1,u+1, w + 1,adj[w][u]);
                }
                
            }
        }
    }
    //from bottom to top
    for (int i = 0; i < ordergen.size();i++){
        int v = ordergen[i];
        sort(T[v].X.begin(), T[v].X.end(), cmp);
        int lenx = T[v].X.size();
        if (lenx != 0)
            T[v].parent = T[v].X[lenx - 1];
        else
            T[v].parent = MAX_V;
        T[v].X.push_back(v);
        treewidth = max(treewidth, lenx + 1);
        if (T[v].parent == MAX_V){
            root = v;
            break;
        }
        T[T[v].parent].children.push_back(v);
    }
}
long long indexsize, totalwidth;
void generateLabel4v(int v){
    //generate labels for each X(v) and its ancestors 
    vector<int> anc;
    int u1 = v;
    while (T[u1].parent != MAX_V){
        anc.push_back(T[u1].parent);
        u1 = T[u1].parent;
    }
    int lenanc = anc.size();
    T[v].depth = lenanc;
    treeavgheight += (double)lenanc;
    treeheight = max(treeheight, lenanc + 1);
    for (int i = 0; i < lenanc;i++){
        int u = anc[anc.size() - 1 - i];
        int lenx = T[v].X.size();
        double mdis = DBL_MAX;
        for (int k = 0; k < lenx; k++){
            int w = T[v].X[k];
            if (w == u)
                ancarray[v].push_back(i);
            if (w == v){
                continue;
            }
            if(T[u].ran<T[w].ran){
                mdis = min(mdis, adj[v][w] + Lh2h[u][ancarray[v][k]]);
            }
            else{
                mdis = min(mdis, adj[v][w] + Lh2h[w][i]);
            }
        }
        Lh2h[v].push_back(mdis);
    }
    Lh2h[v].push_back(0);
    ancarray[v].push_back(lenanc);
    totalwidth += T[v].X.size();
}
void labeling(){
    //label in a top-down manner
    bfs.push(root);
    int iter = 0;
    while(!bfs.empty()){
        int v= bfs.front();
        bfs.pop();
        generateLabel4v(v);
        for (int i = 0; i < T[v].children.size();i++){
            bfs.push(T[v].children[i]);
        }
        if(iter%100000==0)
            printf("%d %d\n", iter, treeheight);
        iter += 1;
    }
}


vector < int > adjt[MAX_V];    // stores the tree
vector < int > euler;      // tracks the eulerwalk
vector < int > depthArr;   // depth for each node corresponding
                           // to eulerwalk
 
int FAI[2*MAX_V];     // stores first appearance index of every node
int level[2*MAX_V];   // stores depth for all nodes in the tree
int ptr;         // pointer to euler walk
int dp[2*MAX_V][30];  // sparse table
int logn[2*MAX_V];    // stores log values
int p2[30];      // stores power of 2
 
void buildSparseTable(int n)
{
    // initializing sparse table
    memset(dp,-1,sizeof(dp));
 
    // filling base case values
    for (int i=1; i<n; i++)
        dp[i-1][0] = (depthArr[i]>depthArr[i-1])?i-1:i;
 
    // dp to fill sparse table
    for (int l=1; l<28; l++)
      for (int i=0; i<n; i++)
        if (dp[i][l-1]!=-1 && dp[i+p2[l-1]][l-1]!=-1)
          dp[i][l] =
            (depthArr[dp[i][l-1]]>depthArr[dp[i+p2[l-1]][l-1]])?
             dp[i+p2[l-1]][l-1] : dp[i][l-1];
        else
             break;
}
 
int query(int l,int r)
{
    int d = r-l;
    int dx = logn[d];
    if (l==r) return l;
    if (depthArr[dp[l][dx]] > depthArr[dp[r-p2[dx]][dx]])
        return dp[r-p2[dx]][dx];
    else
        return dp[l][dx];
}
 
void preprocess()
{
    // memorizing powers of 2
    p2[0] = 1;
    for (int i=1; i<28; i++)
        p2[i] = p2[i-1]*2;
 
    // memorizing all log(n) values
    int val = 1,ptr=0;
    for (int i=1; i<2*MAX_V; i++)
    {
        logn[i] = ptr-1;
        if (val==i)
        {
            val*=2;
            logn[i] = ptr;
            ptr++;
        }
    }
}

void dfs(int cur,int prev,int dep)
{
    // marking FAI for cur node
    if (FAI[cur]==-1)
        FAI[cur] = ptr;
 
    level[cur] = dep;
 
    // pushing root to euler walk
    euler.push_back(cur);
 
    // incrementing euler walk pointer
    ptr++;
 
    for (auto x:adjt[cur])
    {
        if (x != prev)
        {
            dfs(x,cur,dep+1);
 
            // pushing cur again in backtrack
            // of euler walk
            euler.push_back(cur);
 
            // increment euler walk pointer
            ptr++;
        }
    }
}
 
// Create Level depthArray corresponding
// to the Euler walk Array
void makeArr()
{
    for (auto x : euler)
        depthArr.push_back(level[x]);
}
 
int LCA(int u,int v)
{
    // trivial case
    if (u==v)
       return u;
 
    if (FAI[u] > FAI[v])
       swap(u,v);
 
    // doing RMQ in the required range
    return euler[query(FAI[u], FAI[v])];
}
void lcamain()
{
    // constructing the described tree
    for (int i = 1; i <= N; i++){
        int u = i, v = T[i - 1].parent + 1;
        if (u == root + 1)
            continue;
        adjt[u].push_back(v);
        adjt[v].push_back(u);
    }
    
    // performing required precalculations
    preprocess();
    // doing the Euler walk
    ptr = 0;
    memset(FAI,-1,sizeof(FAI));
    dfs(root+1, 0, 0);

    // creating depthArray corresponding to euler[]
    makeArr();
 
    // building sparse table
    buildSparseTable(depthArr.size());
}

double h2hQuery(int s, int t){
    optw = DBL_MAX;
    if (s == t)
        return 0;
    int l = LCA(s + 1, t + 1) - 1;
    //printf("-%d %d: %d\n", s + 1, t + 1, l + 1);
    int ind;
    if (l == s){//X(s) is an ancestor of X(t)
        return Lh2h[t][T[s].depth];
    }
    else if (l == t){//X(t) is an ancestor of X(s)
        return Lh2h[s][T[t].depth];
    }
    else{//find the LCA and cs and ct
        int cs, ct;
        for (int i = 0; i < T[l].children.size();i++){
            int tmp = T[l].children[i] + 1;
            if(LCA(s+1,tmp)==tmp)
                cs = tmp-1;
            if(LCA(t+1,tmp)==tmp)
                ct = tmp-1;
        }
        l = (ancarray[cs].size() < ancarray[ct].size()) ? cs : ct;
        if(ancarray[cs].size()==ancarray[ct].size())
            l = (cs < ct) ? cs : ct;
        //printf("*%d %d %d %d*\n", l + 1, cs+1, ct+1, ancarray[l].size());
        for (int i = 0; i + 1 < ancarray[l].size(); i++)//iterate over each hoplink
        {
            ind = ancarray[l][i];
            optw = min(optw, Lh2h[s][ind] + Lh2h[t][ind]);
        }
    }
    return optw;
}
vector<edge> alledges;

long long cachesize;
long long H2Hindexsize;
void saveH2H(string filename){
    filename += string("H2Hindex");
    ofstream of;
    of.open(filename.c_str(), ios::binary);
    // FILE *fp_index=fopen("index.txt","w");
    // fprintf(fp_index, "%d ", N);
    of.write(reinterpret_cast<const char *>(&N), sizeof(int));
    bfssave.push(root);
    while(!bfssave.empty()){
        int v = bfssave.front();
        bfssave.pop();
        int lenl1 = Lh2h[v].size(), nx = T[v].X.size();
        H2Hindexsize += 4 + nx;
        //fprintf(fp_index, "%d %d %d %d%c", v, T[v].parent, nx, lenl,' ');
        of.write(reinterpret_cast<const char *>(&v), sizeof(int));
        of.write(reinterpret_cast<const char *>(&T[v].parent), sizeof(int));
        of.write(reinterpret_cast<const char *>(&nx), sizeof(int));
        of.write(reinterpret_cast<const char *>(&lenl1), sizeof(int));
        /*for (int i = 0; i < nx; i++){
            //fprintf(fp_index, "%d%c", T[v].X[i].first, (i == nx - 1) ? ' ' : ' ');
            of.write(reinterpret_cast<const char *>(&T[v].X[i]), sizeof(int));
        }*/
        for (int i = 0; i < lenl1; i++){
            H2Hindexsize += 2;
            //fprintf(fp_index, "%d %d ", L[v][i].first, lend);
            of.write(reinterpret_cast<const char *>(&Lh2h[v][i]), sizeof(double));
        }
        for (int i = 0; i < T[v].children.size();i++){
            bfssave.push(T[v].children[i]);
        }
    }
    //fclose(fp_index);
    of.close();
}

typedef pair<int, double> ID;
vector<ID> G_Dij[MAX_V];
double dijkstra(int start, int end) {
    if (start == end) return 0.0;
    
    // 距离数组
    vector<double> dist(MAX_V, DBL_MAX);
    vector<bool> visited(MAX_V, false);
    
    // 优先队列：pair<距离, 节点>，距离小的优先
    priority_queue<pair<double, int>, vector<pair<double, int>>, greater<pair<double, int>>> pq;
    
    // 初始化起点
    dist[start] = 0.0;
    pq.push({0.0, start});
    
    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        
        // 如果已经访问过，跳过
        if (visited[u]) continue;
        if(dist[u]<d)
            continue;

        // 标记为已访问
        visited[u] = true;
        
        // 如果到达终点，返回距离
        if (u == end) {
            return dist[u];
        }
        
        // 扩展邻居节点
        for (auto& edge : G_Dij[u]) {
            int v = edge.first;
            double w = edge.second;
            
            // 如果找到更短路径，更新距离
            if (!visited[v] && dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                pq.push({dist[v], v});
            }
        }
    }
    
    // 如果无法到达终点
    return -1.0;
}

void buildH2HIndex(string sfile){
    FILE *fp_query, *fp_network;
    string prefix = pathJoin(pathJoin(marsRoot(), "data"), sfile) + "/";
    string graphfile = prefix + string("USA-road-d.") + sfile + (".gr");
    fp_network = fopen(graphfile.c_str(), "r");
    char ch, buffer[100];
    int u, v, intw;
    double w;
    char cat1;
    //read graph
    for (int i = 0; i < 4; i++)
        fgets(buffer, 90, fp_network);
    for (int i = 0; i < 4; i++)
        fgetc(fp_network);
    fscanf(fp_network, "%d%d", &N, &M);
    for (int i = 0; i < 3; i++)
        fgets(buffer, 90, fp_network);
    for (int i = 0; i < M; i++) {
        fscanf(fp_network, "%c%d%d%d", &ch, &u, &v, &intw);
        fgets(buffer, 90, fp_network);
        w = (double)intw / 10.0;
        u--;
        v--;
        //printf("%d %d %c%d %c\n", u, v, cat1, cat2, l);
        if (i % 2 == 0){
            adjo[u][v] = 1;
            adjo[v][u] = 1;
            alledges.push_back(edge(u, v, w));
            G_Dij[u].push_back(ID(v, w));
            G_Dij[v].push_back(ID(u, w));
        }
    }

    std::chrono::high_resolution_clock::time_point t1, t2;
	std::chrono::duration<double> time_span;
	double runT;
    string setres=sfile+string("\n");

    t1=std::chrono::high_resolution_clock::now();
    string ordername = prefix + string("order.txt");
    if(0){//generate order for vertices
        genorder(ordername, 1);
    }
    else{//get order from file
        ordergen.assign(N, -1);
        FILE *fpord = fopen(ordername.c_str(), "r");
        for (int i = 0; i < N; i++){
            fscanf(fpord, "%d", &T[i].ran);
            ordergen[T[i].ran] = i;
        }
    }
    t2 = std::chrono::high_resolution_clock::now();
    time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
	runT= time_span.count();
	cout<<"Order Generation Time "<<runT<<endl;

    // distribute edges
    for (int i = 0; i < alledges.size(); i++)
    {
        int f = alledges[i].from, t = alledges[i].to;
        double w = alledges[i].weight;
        if(T[f].ran>T[t].ran)
            swap(f, t);
        //adjT[f][t] = 1;
        adj[f][t]= w;
    }

    memset(logn2, -1, sizeof(logn2));
    logn2[1] = 0;
    int val = 1;
    for (int i = 0; i < 27;i++){
        logn2[val] = i;
        val *= 2;
    }

    t1=std::chrono::high_resolution_clock::now();
    treedec();
    t2=std::chrono::high_resolution_clock::now();
    time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
	runT= time_span.count();
	cout<<"Tree Decomposition Time "<<runT<<endl;
    cout << "Tree Width " << treewidth << endl;
    setres += string("Tree Decomposition Time ") + to_string(runT)+ string("\n");

    lcamain();

    t1=std::chrono::high_resolution_clock::now();
    labeling();
    t2=std::chrono::high_resolution_clock::now();
    time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
	runT= time_span.count();
	cout<<"Labeling Time "<<runT<<endl;
    cout << "Tree Avg. Height " << treeavgheight / N << endl;
    cout << "Tree Max. Width " << treewidth << endl;
    setres += string("Labeling Time ") + to_string(runT) + string("\n");
    setres += string("Tree Width ") + to_string(treewidth) + string("\n");    
    setres += string("Max. Label Size ") + to_string(maxlabelsize) + string("\n");
    setres += string("Avg. Label Size ") + to_string((double)avglabelsize / treeavgheight) + string("\n");

    t1 = std::chrono::high_resolution_clock::now();
    //FILE *fp = fopen("H2Hindex", "w");
    //saveH2H(string("../data/") + sfile + string("/"));// test without save
    t2=std::chrono::high_resolution_clock::now();
    time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2-t1);
	runT= time_span.count();
    cout << "Saving H2H Index Time " << runT << endl;
    cout << "H2H Index Size " << (double)H2Hindexsize * 4 / 1000000 << "MB" << endl;
    setres += string("Saving H2H Index Time ") + to_string(runT) + string("\n");
    setres += string("H2H Index Size ") + to_string((double)H2Hindexsize * 4 / 1000000) + string("MB\n");

    /*
    for (int k = 0; k < 100;k++)
        //for (int j = i; j < 10;j++)
        {
            int i = rand() % N;
            int j = rand() % N;
            double z1 = h2hQuery(i, j), z2 = dijkstra(i, j);
            if(abs(z1-z2)>1e-8)
            printf("%d %d %f %f %d\n", i+1,j+1,z1,z2,z1==z2);
        }
    FILE *fp_record = fopen("H2HRecord.txt", "a");
    fprintf(fp_record, "%s\n", setres.c_str());
    fclose(fp_record);*/
}

/*int main(int argc , char * argv[]){
    string sfile, sq, srandom, sreg;
    double sl;
    if (argc > 1)
        sfile = string(argv[1]);
    else
        sfile = string("NYC");
    if (argc > 2)
        sq = string(argv[2]);
    else
        sq = string("q1");

    buildH2HIndex(sfile);
    return 0;
}*/
