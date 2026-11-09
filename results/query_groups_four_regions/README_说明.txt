NYC、BAY、COL、FLA 各使用 popular、medium、rare 三组查询，每组 1000 条，τ=5 km、k=10。MARS 使用 α=64、ef=1600，HNSW 使用空候选重启修复版，RoadScan 关闭评分复用。

结果共 180 轮。NYC 的 45 轮沿用已有记录，BAY、COL、FLA 新增 135 轮查询测量，使用已有索引。

BAY、COL、FLA 在 CPU96/NUMA4 上串行单线程运行。NYC 历史记录没有统一绑核，36 轮 NYC 基线中有 10 个运行窗口记录到其他活跃进程。

每个配置统计三份索引实例。表中的 ± 表示样本标准差，Recall 长表使用 0 至 1 的数值，另提供百分比列。

BAY、COL、FLA 的 popular/rare 旧 GT 对应不同的查询向量。本次使用原查询向量和评分方法重算六份 GT，要求整数道路距离不超过 5000 米。生成程序先在固定的 medium 查询上与已有 GT 核对结果数量和分数。
