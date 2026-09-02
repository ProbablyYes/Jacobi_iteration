# Jacobi Iteration：二维 Poisson 方程的 MPI 并行实验

本项目使用五点差分和 Jacobi 迭代求解二维 Poisson 方程，比较串行、阻塞 MPI 和通信—计算重叠 MPI 三种实现。实验重点不是堆叠并行技术，而是用可复现数据回答正确性、二阶空间精度、单节点强扩展、通信瓶颈和非阻塞优化是否有效。

## 数学问题

在单位正方形上求解

\[
-\Delta u=f,\qquad u|_{\partial\Omega}=0,
\]

取解析解

\[
u(x,y)=\sin(\pi x)\sin(\pi y),
\qquad f(x,y)=2\pi^2\sin(\pi x)\sin(\pi y).
\]

`--size N` 表示每个坐标方向有 `N` 个内部网格点。MPI 程序把内部行连续分给各进程，每个进程只保存本地行和上下两个 halo。Blocking 版先完成 halo 交换再计算；Overlap 版在非阻塞通信进行时先计算不依赖新 halo 的内部行。

收敛判据是全局离散残差的无穷范数。`relative_l2_error` 是相对于连续解析解的离散相对误差，因此即使迭代残差趋近零，仍会保留空间离散误差。

## 环境安装

推荐使用仓库内的隔离环境，不需要 root 权限，也不会用其他 MPI 实现代替 OpenMPI：

```bash
make env
source scripts/activate_env.sh
make
```

`make env` 会从 OpenMPI 官方源码把 4.1.6 安装到 `.local/openmpi`，并优先在 `.venv` 安装绘图依赖；若系统缺少 `python3-venv`，则自动回退到 `.local/python`。这些目录均被 Git 忽略，可删除后完整重建。构建需要 GCC、Make、curl、tar 和 Python/pip。

如果课程集群已经由管理员提供 OpenMPI，也可跳过项目环境，直接使用系统环境。Ubuntu 22.04 的系统级安装方式为：

Ubuntu 22.04 可执行：

```bash
sudo apt update
sudo apt install -y build-essential openmpi-bin libopenmpi-dev python3-pip
python3 -m pip install --user -r requirements.txt
```

本项目使用 GNU Make，不依赖 CMake。检查环境：

```bash
gcc --version
mpicc --version
mpirun --version
python3 -c "import matplotlib; print(matplotlib.__version__)"
```

## 编译和运行

```bash
make

./bin/jacobi_serial --size 64 --tol 1e-6
mpirun -np 4 ./bin/jacobi_mpi_blocking --size 64 --tol 1e-6
mpirun -np 4 ./bin/jacobi_mpi_overlap --size 64 --tol 1e-6
```

所有程序支持同一套接口：

```text
--size N          每个方向的内部网格点数
--tol EPS         全局无穷范数残差阈值
--max-iters K     最大迭代次数
--fixed-iters K   固定执行 K 轮，不提前停止
--trace FILE      保存 iteration,residual
--format text|csv 人类可读摘要或机器可读 CSV
```

性能对照必须使用 `--fixed-iters`，避免某个版本因浮点归约或停止时刻不同而少执行一轮。CSV 输出字段稳定，可直接被实验脚本收集。

退出码：成功或固定轮数完成为 `0`，达到最大轮数仍未收敛为 `2`，参数/环境错误为 `64`。

## 验证和实验

```bash
make test       # 数值一致性、异常输入、收敛、二阶精度、流水线冒烟
make sanitize   # 串行版 AddressSanitizer/UBSan
make pilot      # 选择合适的正式强扩展规模
make benchmark  # 重复测量并保存原始 CSV
make accuracy   # N=32/64/128/256 网格加密实验
make plots      # 从真实 CSV 生成报告图
```

正式强扩展前先运行 `make pilot`。默认候选规模只是起点，应选择本机单进程耗时约 2–30 秒的工作量，例如：

```bash
PILOT_ITERS=200 PILOT_SIZES="1024 2048 4096" make pilot
GRID_SIZE=2048 FIXED_ITERS=200 REPETITIONS=5 make benchmark
```

常用环境变量：

- `MPIEXEC`：MPI 启动器，默认 `mpirun`。
- `MPIEXEC_ARGS`：覆盖自动进程绑定参数，适配调度器或特殊机器。
- `PROCESSES`：强扩展进程列表，默认 `1 2 4 8`。
- `GRID_SIZE`、`FIXED_ITERS`：正式强扩展工作量。
- `REPETITIONS`、`WARMUPS`：默认正式 5 次、预热 1 次。
- `SIZE_LIST`、`TOLERANCE_LIST`：敏感性实验取值。
- `SMOKE=1`：只运行很小的流水线冒烟实验。

原始数据位于 `results/raw.csv`，环境信息位于 `results/system_info.txt`，图片位于 `results/figures/`。这些产物默认不提交，防止把不同机器的数据混在一起。

## 计时口径

- `total_seconds`：同步开始到全部进程完成迭代的墙钟时间。
- `compute_seconds`：stencil 更新耗时。
- `halo_issue_seconds`：非阻塞 MPI 调用本身的开销；Blocking 版为零。
- `halo_wait_seconds`：关键路径上实际暴露的 halo 等待时间；Blocking 版包含阻塞交换耗时。
- `reduction_seconds`：每轮全局最大残差的 `MPI_Allreduce` 时间。

各分量输出所有进程中的最大值。由于每一列的最大值可能来自不同进程，分量之和不要求严格等于总时间。Overlap 减少的是可能暴露在关键路径上的等待，而不是网络传输的数据量。

## 单节点实验原则

- 1、2、4 进程作为物理核心扩展性主数据；8 进程单独讨论超线程。
- benchmark 自动检测物理核数：不超过物理核数时按 core 绑定，更多进程时显式使用并绑定 hardware thread。
- 每组预热一次，正式重复五次，以中位数作图，同时保留全部原始值。
- 不要求每一轮残差严格单调，只验证最终阈值与整体收敛趋势。
- 如果 Overlap 没有提速，如实从消息规模、计算通信比和 MPI 异步进展解释。
- 多节点代码路径与单节点相同；SSH、hostfile 和节点间实验等真实集群准备好后再增加。

详细实验报告草稿见 [docs/experiment_report.md](docs/experiment_report.md)。
