# 正式实验数据

本目录保存 2026-09-02 在单节点 Intel Xeon E5-1620 v4 环境采集的真实实验结果。固定迭代实验覆盖 `N=1024/2048/4096` 与 1/2/4/8 个 MPI 进程的完整矩阵，串行程序固定在一个逻辑 CPU 上；每组预热 1 次并记录 5 次正式运行，中位数用于报告。

- `raw.csv`：165 条性能与参数敏感性原始记录，其中固定迭代 135 条、容差收敛 30 条。
- `correctness.csv`：串行、Blocking MPI、Overlap MPI 一致性记录。
- `accuracy.csv`：网格加密与二阶精度数据。
- `convergence.csv`：`N=128, P=4, tolerance=1e-7` 的逐轮残差。
- `pilot.csv`：正式 workload 选择依据。
- `system_info.txt`：采数时的硬件、系统、编译器和 MPI 快照。
- `figures/`：由上述 CSV 自动生成的 10 张报告图片。

数据由仓库脚本直接生成，没有手工修改。完整分析见 `docs/experiment_report.md`。
