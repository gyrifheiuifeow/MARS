#ifndef EMBEDDING_SERVICE_H
#define EMBEDDING_SERVICE_H

#include <vector>
#include <string>
#include <cstdio>

class EmbeddingService {
private:
    FILE* python_in;   // 保留接口一致性
    FILE* python_out;  // 保留接口一致性
    
public:
    EmbeddingService();
    ~EmbeddingService();
    
    // 禁止拷贝构造和赋值
    EmbeddingService(const EmbeddingService&) = delete;
    EmbeddingService& operator=(const EmbeddingService&) = delete;
    
    void getEmbeddings(const std::vector<std::string>& texts, std::vector<std::vector<double>> &queryEmb);
};

#endif // EMBEDDING_SERVICE_H