# MARS

Efficient Multi-Aspect Road-Constrained Vector Search

MARS 在给定道路距离范围内检索语义相近的对象。
高频向量使用 pivot 倒排表，其余向量进入 rest 语义图。
当前代码使用 α=64，并提供 RoadScan、HNSW、SymphonyQG 和 Mesh-Road 四种对比方法。

### 编译

需要支持 AVX2/FMA 的 Linux x86-64、Python 3.12 及以上版本，
以及支持 OpenMP 的 C++17 编译器。实验使用 GCC 13.3。
Python 脚本依赖标准库，查询在 CPU 上运行，直接读取提供的 768 维向量。

```bash
cd MARS
python3 scripts/build.py
```

可执行文件在 `build/bin/`。先运行小规模测试：

```bash
python3 -m unittest discover -s tests -v
build/bin/mars_accept unit build/toy_acceptance
```

C++ 测试检查向量划分、对象关联、评分和索引保存与加载，通过后输出 `TOY_ACCEPTANCE_PASS`。

### 运行

按 [DATA.md](docs/DATA.md) 的目录结构，把准备好的数据放在
`../mars-artifacts/MARSExp2`。下面构建一份 NYC 索引，再运行它对应的查询配置：

```bash
python3 scripts/verify_data.py --data-root ../mars-artifacts/MARSExp2

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage build \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage perf \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
```

索引位于 `../mars-run/instances/MARS/NYC/rep1`，
查询结果位于 `../mars-run/results/MARS/NYC/rep1/perf`。
构建和查询使用同一个输出根目录。运行其他方法时，将两条命令中的 `MARS`
换成 `HNSW`、`SymphonyQG`、`Mesh-Road` 或 `RoadScan`。

完整 MARS 实验覆盖四个地区，每个地区分别构建三份索引：

```bash
python3 scripts/run_matrix.py \
  --data-root ../mars-artifacts/MARSExp2 \
  --output-root ../mars-full --execute
```

脚本依次运行 144 项性能测量和 48 项阶段诊断。
省略 `--execute` 时，它只准备任务文件并打印计划。
α 扫描、Graph-only 和补充查询组实验的入口，
见 [EXPERIMENTS.md](docs/EXPERIMENTS.md) 和 [ARCHIVE.md](docs/ARCHIVE.md)。

已有汇总表和逐查询 CSV 在 [`results/alpha64/`](results/alpha64/) 与
[`results/query_groups_four_regions/`](results/query_groups_four_regions/) 中。
内容包括查询效率、随 τ 变化的结果、Pareto 曲线、三类查询、α 与 ef 参数、
三项消融，以及索引构建时间和大小。核对这些结果不需要构建索引：

```bash
python3 scripts/verify_sources.py
python3 scripts/verify_raw_results.py
python3 scripts/verify_results.py
```

### 参数

默认 α=64、k=10、rest ef=1600。
相同的归一化向量合并后，按关联的不同对象数量统计频率。
关联至少 α 个对象的向量进入 pivot，其余向量按“向量、对象”关联建立 rest 图节点。

| 参数 | 用途 |
|---|---|
| `--method`、`--region` | 选择方法和 `NYC`、`BAY`、`COL`、`FLA` 中的一个地区。 |
| `--data-root`、`--output-root` | 指定数据位置和运行目录。新实验使用新的输出根目录。 |
| `--rep` | 选择第 1、2 或 3 份索引，各实例分别构建。 |
| `--alpha` | 设置频率阈值，不同取值使用各自的输出根目录。 |
| `--stage` | 选择 `build`、`perf` 等阶段，MARS 还支持 `diag`。 |
| `--dry-run` | 准备任务文件并打印命令。 |

半径、ef、查询组和变体写在 `configs/mars/performance_matrix.json`，
基线设置在 `configs/baselines/`。`pivot` 变体关闭 rest，`pointwise` 使用逐点道路距离检查。
去掉 pivot 的 Graph-only 消融使用 [`scripts/graph_only.py`](scripts/graph_only.py)，
命令见 [ARCHIVE.md](docs/ARCHIVE.md)。

QPS 按查询条数除以核心查询总耗时计算。
Recall 使用分数阈值标准，分母为 `min(k, GT 结果数)`。
表中给出各索引实例的均值和样本标准差。
计时范围和历史测量条件见 [EXPERIMENTS.md](docs/EXPERIMENTS.md)。

### 数据集

| 地区 | 对象数 | 目录 |
|---|---:|---|
| `NYC` | 1,140,399 | `data/NYC` |
| `BAY` | 700,864 | `data/BAY` |
| `COL` | 393,709 | `data/COL` |
| `FLA` | 1,534,361 | `data/FLA` |

每个地区包含道路图、POI 映射、名称和类别向量、查询向量及 GT。
四个地区的压缩包合计约 8.84 GB，解压和索引构建还需要额外空间。

ANN 索引在本地构建。建图包含随机过程，重新构建后，近似查询的答案可能与已有实例不同。
归档中还保留了较早的类别版 MARS 和其他 α 实验。
