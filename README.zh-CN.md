# MARS：道路距离约束下的多方面向量检索

这个仓库保存 MARS 的实现、实验输入和各轮测量记录。当前主入口采用 **2026 年 10 月 17 日完成的频率划分版本，默认 α=64、ef=1600**。较早的“类别进入 pivot、名称进入 rest”版本，以及 α32 和其他参数试验，都保留在历史材料中。不同版本的数据没有合并成同一条曲线。

如果只是核对论文中的数值，可以先看 `results/alpha64` 和 `results/query_groups_four_regions`。这里不仅有汇总表，也有对应的逐查询 CSV。`python3 scripts/verify_raw_results.py` 会检查原始文件哈希并重新计算 QPS、Recall；`python3 scripts/verify_results.py` 则从逐轮结果重新计算均值和样本标准差。这两步不需要编译程序，也不需要下载向量数据集。

| 实验 | 对应结果 |
|---|---|
| Query Efficiency、随 τ 变化的 QPS/Recall、Pareto | `results/alpha64/Exp1_3_final_summary.csv` |
| popular、medium、rare 三类查询，四地区、五种方法 | `results/query_groups_four_regions/all_four_regions_summary.csv` |
| α 参数，11 档、每档三个实例 | `results/alpha64/Alpha_parameter_summary.csv` |
| α64 版本的 ef 参数 | `results/alpha64/Exp4_MARS_summary.csv` |
| 去掉 pivot 的 Graph-only | `results/alpha64/Graph_only_summary.csv` |
| 去掉 rest 的 Pivot-only | `results/alpha64/Exp5_MARS_summary.csv`、`Exp6_MARS_summary.csv` |
| 道路编译消融 | `results/alpha64/Exp7_paired_road_summary.csv` |
| 修复后 HNSW 和 α64 MARS 的索引构建、大小、内存 | `results/alpha64/Exp8_final_cost_summary.csv` |

实验编号沿用原始文件名，论文修改过程中编号曾发生变化，因此查找时应以实验含义为准。10 月 17 日单独重跑的原类别版 ef 实验放在 `results/history/original_category_ef_rerun`，它不是 α64 的参数实验。

## 从下载到运行

先克隆仓库。GitHub 仓库如果仍为私有，需要使用已获得访问权限的账号。

```bash
git clone https://github.com/gyrifheiuifeow/MARS__Efficient_Multi_Aspect_Road_Constrained_Vector_Search.git
cd MARS__Efficient_Multi_Aspect_Road_Constrained_Vector_Search
python3 scripts/verify_sources.py
python3 scripts/verify_raw_results.py
python3 scripts/verify_results.py
```

正式程序运行在支持 AVX2/FMA 的 Linux x86-64 机器上，使用 C++17、OpenMP 和单线程查询。历史记录中的编译器为 GCC 13.3。Python 工具仅依赖标准库，下载解包工具要求 Python 3.12 及以上。已经提供准备好的向量，查询实验不需要 GPU 或在线调用嵌入模型。`Dockerfile` 给出了 Ubuntu 24.04 的构建环境，实际测量机器的记录见 `provenance/build_environment.json`。

```bash
python3 scripts/build.py
python3 -m unittest discover -s tests -v
build/bin/mars_accept unit build/toy_acceptance
python3 scripts/fetch_artifacts.py --kind data --output ../mars-artifacts
python3 scripts/verify_data.py --data-root ../mars-artifacts/MARSExp2
```

数据集较大，放在同一仓库的 [Release](https://github.com/gyrifheiuifeow/MARS__Efficient_Multi_Aspect_Road_Constrained_Vector_Search/releases/tag/v2026.10.18-reproducibility) 中。下载脚本会自动拼接分卷、核对 SHA-256，再解包。它支持已有 Git 登录凭据或 `GITHUB_TOKEN` 环境变量，凭据不应写入代码。下载完整历史材料时，将 `--kind data` 改成 `--kind all`。

下面的例子先构建 NYC 的一份 α64 索引，再加载它运行对应的性能配置。两步使用同一个输出根目录；程序会拒绝覆盖已经存在的阶段结果。

```bash
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage build --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage perf --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
```

`--method` 还支持 HNSW、SymphonyQG、Mesh-Road 和 RoadScan。`scripts/run_matrix.py` 可以先打印完整 MARS 实验计划，显式加上 `--execute` 后才会串行执行。默认矩阵包括 ef、rest 和道路编译变体。`reproduce.py --alpha` 可在独立输出目录中选择其他频率阈值；`scripts/graph_only.py` 会在新目录准备并编译冻结的 Graph-only 源码。补充查询组、历史 α 扫描和其他入口见[归档说明](docs/ARCHIVE.md)。历史协调脚本保留原来的路径、续跑条件和 CPU/NUMA 设置，应结合对应的绑定记录使用。

## 如何理解统计口径

QPS 按“查询条数除以核心查询总耗时”计算，而不是逐查询 QPS 的平均数。Recall 保留原有的分数阈值口径和 GT 分母。阶段诊断会产生额外开销，因此诊断数据与性能数据分开。HNSW 使用修复后的空候选重启版本；RoadScan 使用当时冻结的近似评分设置，不能因为它扫描道路范围，就把它的 Recall 视为必然等于 100%。

构建耗时、保存耗时、峰值内存分别记录。索引大小同时提供实际序列化字节数，以及扣除明确重复载荷后的结构字节数；后者不等同于文件实际占用空间。

历史数据也有需要说明的条件差异。Graph-only 固定在 CPU96/NUMA4，主 MARS 实验采用未固定单核的单线程配置；补充的 BAY/COL/FLA 查询组固定 CPU/NUMA，NYC 复用已有测量。十个历史基线运行窗口观察到其他活跃进程。α=2 使用较早的专用实现。这些信息都随结果保留，不把它们写成完全同场、完全相同条件下的测量。

## 历史材料和复现边界

Release 保存完整源代码树、各轮配置、日志、结果、审计文件、本地绘图材料，以及四个地区准备好的道路图、POI 映射、向量、查询与 GT。原始文件按原字节归档；可移植入口另放在 `scripts/` 和 `runtime/`，并记录原始源码与发布源码的哈希对应关系。

开发工作树接近 1 TB，其中大部分是各轮生成的 ANN 索引。这些大型二进制索引没有上传，文件清单、已有绑定和哈希记录、构建代码及成本记录均已保留。重建近似图时，即使 seed 标签相同，也可能得到不同的索引字节和个别查询结果。因此，本仓库可以核验保存下来的结果，并提供重新构建和测量的材料；此次整理没有重新跑完全部真实数据实验，也不承诺重建后每一项都逐位相同。

详细参数、版本来源和附件内容分别见 [EXPERIMENTS.md](docs/EXPERIMENTS.md)、[VERSIONS.md](docs/VERSIONS.md) 和 [ARCHIVE.md](docs/ARCHIVE.md)。第三方代码保留原有声明，数据文件的收录不构成新的授权许可。
