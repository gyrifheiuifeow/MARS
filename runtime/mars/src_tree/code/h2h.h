#ifndef H2H_H
#define H2H_H
#define MAX_V 1070378
#include<bits/stdc++.h>
using namespace std;
extern int N, M;
typedef pair<double, double> DD;
typedef pair<int, double> ID;
typedef pair<double, int> DI;
typedef pair<int, int> II;
extern vector<DD> coords;

// 函数声明 - buildIndex.cpp 需要调用的函数
void buildH2HIndex(string sfile);
double h2hQuery(int s, int t);
double dijkstra(int start, int end);

#endif // H2H_H