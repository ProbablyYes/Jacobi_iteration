# 二维 Poisson Jacobi 的 MPI 域分解、性能分析与通信重叠优化

> 本文档是可直接补充真实数据的中文实验报告。所有标记为“待实测”的位置必须由 `results/` 中的真实输出填写，不得使用推测数据。

## 1. 实验内容

本实验求解单位正方形上的二维 Poisson 方程：

\[
-\Delta u=f,\qquad u|_{\partial\Omega}=0.
\]

采用均匀网格、二阶五点差分和 Jacobi 迭代。为验证数值结果，选择

\[
u(x,y)=\sin(\pi x)\sin(\pi y),\qquad
f(x,y)=2\pi^2\sin(\pi x)\sin(\pi y).
\]

实现串行、Blocking MPI 和 Overlap MPI 三个版本。实验依次研究程序一致性、空间离散精度、单节点强扩展、问题规模和容差敏感性、时间组成及通信—计算重叠效果。

## 2. 实验目的

1. 掌握 MPI 进程模型、一维区域分解、halo 交换和全局归约。
2. 理解 Jacobi 迭代收敛误差与有限差分空间离散误差的区别。
3. 掌握 Speedup、Efficiency、强扩展和重复测量的实验方法。
4. 使用分项计时定位计算、邻居通信和全局同步瓶颈。
5. 通过非阻塞通信检验通信—计算重叠能否降低暴露通信时间。

## 3. 实验环境

开发机当前配置：Ubuntu 22.04、Intel Xeon E5-1620 v4、4 个物理核心/8 个逻辑 CPU、31 GiB 内存。正式实验前以 `results/system_info.txt` 为准填写 GCC、OpenMPI、操作系统内核和 Python/Matplotlib 版本。

本阶段只报告单节点实验。1/2/4 进程用于物理核心强扩展，8 进程用于观察超线程影响。多节点实验必须在真实集群上测量后另行补充。

## 4. 实验步骤

### 4.1 环境搭建

```bash
make env
source scripts/activate_env.sh
make
make test
```

上述命令将 OpenMPI 4.1.6 与 Python 绘图环境安装在项目目录内，无需 root 权限。正式报告记录的 MPI 版本以 `results/system_info.txt` 为准。

### 4.2 串行离散与迭代

设网格间距 (h=1/(N+1))。五点差分 Jacobi 更新为

\[
u_{i,j}^{(k+1)}=\frac{1}{4}
\left(u_{i-1,j}^{(k)}+u_{i+1,j}^{(k)}+
u_{i,j-1}^{(k)}+u_{i,j+1}^{(k)}+h^2f_{i,j}\right).
\]

程序由零初值开始，边界保持为零。更新量通过 (4/h^2) 换算为离散残差无穷范数。

### 4.3 MPI 域分解

内部网格按连续行划分，不能整除时前若干 rank 多保存一行。每个进程分配 `local_rows + 2` 行，其中第 0 行和最后一行是 halo。左右边界保存在每行首尾元素中并始终为零。

```text
global top boundary
-------------------
rank 0: local rows + bottom halo
-------------------
rank 1: top halo + local rows + bottom halo
-------------------
...
-------------------
rank P-1: top halo + local rows
-------------------
global bottom boundary
```

Blocking 版使用两次 `MPI_Sendrecv` 完成上下交换，然后更新全部本地行。每轮再使用 `MPI_Allreduce(MPI_MAX)` 得到全局残差。

### 4.4 通信重叠优化

Overlap 版按以下顺序执行：

1. 为上下 halo 发起 `MPI_Irecv`，并发出首尾本地行。
2. 计算不依赖新 halo 的内部行。
3. 执行 `MPI_Waitall`。
4. 计算首尾边界行。
5. 执行全局残差归约。

该优化不减少通信量。实验关注的是 `halo_wait_seconds`，即未被内部计算隐藏、仍暴露在关键路径上的等待。

### 4.5 自动实验

```bash
make pilot
GRID_SIZE=<pilot选择值> FIXED_ITERS=<固定轮数> make benchmark
make accuracy
make plots
```

每组性能配置预热 1 次并正式运行 5 次，图中使用中位数。固定迭代模式用于所有版本间性能对照。

## 5. 实验分析

### 5.1 程序正确性

比较相同网格、相同固定轮数下三版的残差、相对 (L_2) 误差和 checksum。记录最大差异，并说明差异是否落在浮点归约顺序导致的舍入范围内。

**待实测：三版本一致性表。**

### 5.2 空间二阶精度

对 (N=32,64,128,256) 使用足够严格的残差阈值，计算

\[
p=\log_2\frac{E_N}{E_{2N}}.
\]

理论上五点差分具有二阶空间精度，因此 (p\) 应逐渐接近 2。这里的 (E_N) 是解析误差，不是迭代残差。

**待实测：插入 `grid_refinement.png` 和误差/观测阶表。**

### 5.3 单节点强扩展

pilot 后选择单进程耗时约数秒至数十秒的网格，以固定迭代数测试 1/2/4/8 进程：

\[
S_p=\frac{T_{serial}}{T_p},\qquad
\eta_p=\frac{S_p}{p}.
\]

重点分析 1→2→4 的物理核心收益，并将 8 进程作为超线程实验单独解释。

**待实测：插入 `runtime_vs_processes.png`、`speedup.png` 和中位数表。**

### 5.4 性能机理

按计算、halo 调用、暴露 halo 等待和 Allreduce 拆分时间。分量采用各 rank 最大值，最大值可能来自不同 rank，故不强制要求柱段之和等于总时间。

**待实测：插入 `time_breakdown.png`，结合问题规模分析计算通信比。**

### 5.5 参数敏感性

比较小、中、大网格在相同进程数和固定轮数下的表现。小网格通常更容易被进程启动、同步和通信开销支配；大网格有更多内部计算可用于摊薄或隐藏 halo 通信。

容差实验比较 (10^{-3},10^{-5},10^{-7}) 下的迭代次数和总时间。不能把残差趋近零等同于解析误差趋近零，因为后者还包含空间离散误差。

**待实测：填写规模与容差敏感性图表。**

### 5.6 Blocking 与 Overlap

比较两版总时间、halo 调用开销和暴露等待时间。若 Overlap 提速，应验证其来源确实是等待被内部行计算隐藏；若未提速，则从消息较小、内部工作不足、内存带宽竞争或 MPI 缺乏异步进展等方面结合数据分析，不能预设优化一定有效。

**待实测：填写优化前后对照与结论。**

## 6. 问题与讨论

1. 为什么 4→8 进程的收益可能显著低于 2→4？物理核心数、超线程和内存带宽分别起什么作用？
2. 为什么非阻塞通信不等于通信免费？什么条件下内部计算足以覆盖 halo 传输？
3. 为什么残差很小仍可能存在解析误差？网格加密如何区分离散误差和迭代误差？
4. 每轮 `MPI_Allreduce` 是全局同步点。进程数跨节点增加时，它可能如何限制扩展性？
5. 一维行分解只与两个邻居通信，结构简单；未来改为二维分解时，消息数、消息长度和表面积/体积比将如何变化？

## 参考资料

1. MPI Forum, *MPI Standard*: https://www.mpi-forum.org/docs/
2. Open MPI Documentation: https://docs.open-mpi.org/
3. William H. Press et al., *Numerical Recipes*, relaxation methods for elliptic equations.
4. Randall J. LeVeque, *Finite Difference Methods for Ordinary and Partial Differential Equations*.
