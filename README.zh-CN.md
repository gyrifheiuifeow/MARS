# MARS

Efficient Multi-Aspect Road-Constrained Vector Search

MARS 面向道路网络中的多方面向量检索，在给定道路距离范围内查找与查询语义相关的对象。
算法利用道路谓词筛选可达对象，并通过 pivot 倒排表和 rest 语义图检索候选对象。
本仓库提供 MARS 与四种对比方法的实现、实验脚本、处理好的数据集和逐查询结果。

### 编译

运行环境为支持 AVX2/FMA 的 Linux x86-64，需要 Python 3.12 及以上版本，
以及支持 OpenMP 的 C++17 编译器。实验使用 GCC 13.3。
Python 脚本只依赖标准库，使用已有向量进行检索不需要 GPU。

```bash
git clone https://github.com/gyrifheiuifeow/MARS.git
cd MARS
python3 scripts/build.py
```

编译后的 MARS、基线程序和验证程序位于 `build/bin/`。
仓库包含所需的第三方源码及其许可声明，
[Dockerfile](Dockerfile) 提供了 Ubuntu 24.04 的构建环境。

### 运行

可以先运行小规模正确性检查，无需下载数据集或构建完整索引：

```bash
python3 -m unittest discover -s tests -v
build/bin/mars_accept unit build/toy_acceptance
```

C++ 检查覆盖频率划分、对象关联、评分，以及索引的保存与加载。
通过后会输出 `TOY_ACCEPTANCE_PASS`。

下面以 NYC 为例，下载数据、构建一份 MARS 索引，再运行对应的性能实验。
运行其他地区时，将两条命令中的 `NYC` 替换为数据集表中的地区代码。

```bash
python3 scripts/fetch_artifacts.py --kind data --output ../mars-artifacts
python3 scripts/verify_data.py --data-root ../mars-artifacts/MARSExp2

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage build \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage perf \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
```

索引保存到 `../mars-run/instances/MARS/NYC/rep1`，
查询结果保存到 `../mars-run/results/MARS/NYC/rep1/perf`。
构建和查询阶段使用同一个输出根目录。
运行对比方法时，将两条命令中的 `MARS` 替换为 `HNSW`、`SymphonyQG`、
`Mesh-Road` 或 `RoadScan`。

完整 MARS 实验会为每个地区分别构建三份索引，随后串行运行各项配置：

```bash
python3 scripts/run_matrix.py \
  --data-root ../mars-artifacts/MARSExp2 \
  --output-root ../mars-full --execute
```

省略 `--execute` 时，脚本只准备任务文件并打印实验计划。
该实验矩阵包括 144 轮性能测量和 48 轮独立的阶段诊断。
额外的 α 参数实验、Graph-only 消融及四地区查询组实验，
见[实验设置](docs/EXPERIMENTS.md)和[归档说明](docs/ARCHIVE.md)。

已有结果位于 [`results/alpha64/`](results/alpha64/) 和
[`results/query_groups_four_regions/`](results/query_groups_four_regions/)，
包括查询效率、随 τ 变化的 QPS 和 Recall、Pareto 曲线、查询组对比、
参数实验、消融实验及索引构建成本。以下命令可核对原始文件并重新计算汇总值：

```bash
python3 scripts/verify_sources.py
python3 scripts/verify_raw_results.py
python3 scripts/verify_results.py
```

QPS 按查询条数除以核心查询总耗时计算。Recall 采用实验中的分数阈值标准，
分母为 `min(k, GT 结果数)`。表中报告各索引实例的均值和样本标准差。
计时范围、基线参数和索引大小的计算方法见[实验设置](docs/EXPERIMENTS.md)，
CPU 绑定及各轮运行条件随原始记录保存。

### 参数

- `--method` 选择 MARS 或四种对比方法；`--region` 选择 `NYC`、`BAY`、`COL` 或 `FLA`。
- `--data-root` 指向包含 `data/<region>` 的目录；`--output-root` 指定索引、日志和结果的保存位置。
  新实验使用新的输出目录。
- `--rep` 选择第 1、2 或 3 份索引。每份索引独立构建，并在不同查询配置之间复用。
- `--alpha` 设置 MARS 的频率阈值。一个向量关联至少 α 个不同对象时进入 pivot，
  其余向量进入 rest 图。默认 α 为 64，不同 α 使用各自的输出目录。
- 默认查询参数为 `k=10`、rest `ef=1600`。半径、ef、查询组和消融变体记录在
  `configs/mars/performance_matrix.json` 中，基线配置位于 `configs/baselines/`。
- `--stage diag` 单独测量 MARS 各阶段的开销；`--dry-run` 只准备输入并打印命令，
  不构建索引或执行查询。

去掉 rest 使用 `pivot` 变体，道路编译消融使用 `pointwise` 变体，
两者均包含在 MARS 实验矩阵中。去掉 pivot 使用共享类别节点的 Graph-only 实现，
通过 [`scripts/graph_only.py`](scripts/graph_only.py) 准备和运行，
具体命令见[归档说明](docs/ARCHIVE.md)。

### 数据集

处理好的数据集放在仓库的
[Release](https://github.com/gyrifheiuifeow/MARS/releases/tag/v2026.10.18-reproducibility)
中。下载脚本会拼接分卷、核对 SHA-256 并解包。
访问私有仓库时，脚本读取 `GITHUB_TOKEN` 或使用已有的 Git 登录凭据。

| 地区代码 | 对象数 | 数据目录 |
|---|---:|---|
| `NYC` | 1,140,399 | `data/NYC` |
| `BAY` | 700,864 | `data/BAY` |
| `COL` | 393,709 | `data/COL` |
| `FLA` | 1,534,361 | `data/FLA` |

每个地区的数据包括道路图、POI 映射、768 维名称和类别向量、查询向量及 GT。
实验直接使用提供的向量，文件格式和输入哈希见 [DATA.md](docs/DATA.md)。

四个地区的压缩包合计约 8.84 GB，解压和索引构建还需要额外磁盘空间。
ANN 索引在本地构建，历史构建记录及索引文件哈希保存在归档中。
重新构建近似图时，索引字节和部分查询结果可能与已有记录不同。

下载时将 `--kind data` 改为 `--kind all`，还可取得较早的源码、实验轮次、
日志和绘图文件。当前实现使用 α64，原类别版及其他 α 实验单独保留。
版本关系见 [VERSIONS.md](docs/VERSIONS.md) 和 [CHANGELOG.md](docs/CHANGELOG.md)。
英文说明见 [README.md](README.md)。
