# 实验三：Stride 调度算法实现

## 实验目标

在本实验中，我们将实现一个带优先级的进程调度算法 —— Stride 调度算法，使系统能够根据进程的优先级合理分配 CPU 时间。

## Stride 调度算法详解

### 基本概念
1. **stride**：表示进程当前的"步长"，初始值为 0
2. **priority**：进程优先级，必须 ≥ 2
3. **pass**：每次调度后 stride 的增加值，由 BigStride/priority 计算得出
4. **BigStride**：一个大常数，建议使用 2^32

### 算法流程
1. **初始化**：
   - 为每个进程设置 stride = 0
   - 设置初始优先级 priority = 16
   - 计算 pass = BigStride/priority

2. **调度过程**：
   - 选择当前 stride 值最小的可运行进程
   - 被选中进程运行一个时间片
   - 运行后将该进程的 stride += pass
   - 重复以上步骤

### 举例说明
假设有三个进程 A、B、C，优先级分别为 4、2、8：
```
BigStride = 100 (简化计算用)
A.pass = 100/4 = 25
B.pass = 100/2 = 50
C.pass = 100/8 = 12.5

调度过程：
初始态：A(0), B(0), C(0)      -> 选择C运行
第1轮后：A(0), B(0), C(12.5)  -> 选择A运行
第2轮后：A(25), B(0), C(12.5) -> 选择B运行
...
```

## 系统调用实现

需要实现新的系统调用 `sys_set_priority`：
```rust
/// 设置进程优先级
/// - syscall ID: 140
/// - 参数: prio: 进程优先级(≥2)
/// - 返回值: 成功返回 prio，失败返回 -1
fn sys_set_priority(prio: isize) -> isize;
```

## 实现提示

1. **数据结构设计**：
```rust
struct Stride {
    stride: u64,        // 当前步长
    priority: usize,    // 优先级
    pass: u64,         // 步长增量
}
```

2. **关键注意点**：
   - 优先级必须 ≥ 2
   - stride 可能会溢出，但不影响算法正确性
   - 初始 stride 设为 0
   - 初始优先级设为 16

## 测试要求

1. **基础测试**：`make run TEST=1`
   - 检查 sys_write 的安全性

2. **优先级测试**：`make run TEST=2`
   - 验证 set_priority 的正确实现

3. **公平性测试**：`make run TEST=3`
   - 验证调度算法的公平性
   - 要求：max(runtime/prio) / min(runtime/prio) < 1.5

## 实验约定

1. 用户栈要求：
   - 大小：4096 字节
   - 对齐：4096 字节对齐
   - 注：此要求仅为测试需要，可在实验4后移除

## Challenge（选做）

实现多核并行调度：
1. 支持多个 CPU 核心同时运行进程
2. 保证调度的公平性
3. 实现核心间的负载均衡