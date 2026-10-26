# uCore 第九章：源代码分析与 GDB 动态跟踪

阅读 `ch9` 的完整参考实现，结合[《rCore-Tutorial-Book-v3》第九章：I/O 设备管理](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter9/index.html)，分析 UART 串口、virtio-blk 和 virtio-gpu 三个驱动。本阶段保留完整代码，完成静态分析、运行观察与实验报告。

## 1. 阅读范围

| 阅读位置 | 分析内容 |
| --- | --- |
| [os/uart.c](os/uart.c) | 原始寄存器操作、初始化和轮询输出；接收缓冲、阻塞读取与中断唤醒 |
| [os/virtio_blk.c](os/virtio_blk.c) | 设备和条件变量表的初始化；读写接口的两种访问路径；请求 token 与完成通知的对应关系 |
| [os/virtio_gpu.c](os/virtio_gpu.c) | GPU 构造、显存地址与长度保存；访问同一块帧缓冲；提交显示刷新。光标素材转换不单列分析任务 |

关联代码只阅读必要的调用关系：标准输入如何调用 UART，文件系统如何发起块请求，图形系统调用如何取得显存并请求刷新。中断入口、PLIC、条件变量、调度器、页表和底层设备协议不单独展开；DMA 只解释设备访问内存与地址转换的作用。

## 2. 调试准备

先按 [README](README.md#环境准备) 安装工具并构建。以下三个样例分别从新的 QEMU 调试会话开始，使用完整参考实现及其配套用户程序和文件系统镜像。

终端一在仓库根目录执行：

```bash
make build
make gdbserver GUI=on GDB_PORT=15234
```

终端二在同一仓库根目录执行：

```bash
mkdir -p reports
gdb-multiarch -nx -q build/kernel
```

在 GDB 中连接并记录操作：

```gdb
set pagination off
set architecture riscv:rv64
set language c
set logging file reports/ch9-gdb.log
set logging overwrite off
set logging enabled on
target remote localhost:15234
```

如使用其他兼容 GDB，替换启动命令中的 `gdb-multiarch`。以下使用函数断点和驱动内存变量；用 `next` 越过已提供的接口，使用 `finish` 返回调用者。不通过 GDB 调用驱动函数，也不为观察状态而读取有副作用的 MMIO 寄存器。

编译优化可能使部分局部变量显示为 `<optimized out>`。此时结合 `list` 查找当前语句，并观察下面给出的全局状态或函数参数；在报告中记录实际可观察的内容。寄存器 `$a0` 在函数刚返回时保存返回值。

每组样例结束后，在 GDB 中按 `Ctrl+C` 暂停，再执行 `monitor quit` 退出 QEMU、`quit` 退出 GDB。下一组重新启动两个终端的调试会话。退出 QEMU 后出现远程连接断开提示是正常的。

## 3. UART：等待字符与中断接收

启动前设置等待登记位置的临时断点：

```gdb
tbreak io_wait_no_sched
continue
bt 5
p cond == &uart.available
p uart.count
p uart.available.wait_queue
set $reader = current_thread
p $reader->state
finish
p $reader->state
p uart.available.wait_queue
tbreak uart_handle_irq
continue
```

第一次暂停应位于 shell 读取标准输入的路径，`cond` 对应 UART 的 `available`。比较等待登记前后的线程状态与等待队列。`io_wait_no_sched` 只把线程登记为等待者；返回后，`uart_read` 还要结束设备保护再调用 `sched()`。

最后一次 `continue` 后，在终端一输入一个字符。GDB 会停在串口中断处理入口：

```gdb
bt 5
p uart.count
p uart.available.wait_queue
finish
p uart.count
p uart.available.wait_queue
p $reader->state
continue
```

记录中断前后接收缓冲和等待队列的变化。中断处理把当前所有字符放入 FIFO，通知一个等待者；此时线程进入就绪状态，尚未由中断处理直接切换执行。继续运行后确认字符在 shell 中回显，并结合源码解释等待线程恢复后为何重新检查缓冲。UART 输出只需静态分析等待发送寄存器可写的过程。

若第一次等待断点命中其他设备路径，先通过调用栈辨认，并在 `uart_read` 中设置断点后重新跟踪 UART 的等待调用。报告应记录实际命中的设备与线程。

## 4. virtio-blk：提交请求与完成通知

重新启动调试会话，只在任务运行阶段的写请求提交处暂停：

```gdb
tbreak block_submit if dev_non_blocking_access && write
continue
```

出现 shell 后，在终端一输入 `huge_write`。命中断点时记录本次请求：

```gdb
p block_id
p write
p/x buf
p/x resp
set $response = resp
set $issuer = current_thread
bt 5
finish
set $submitted_token = (int)$a0
p $submitted_token
tbreak io_wait_no_sched
continue
p cond == &block.condvars[$submitted_token]
p block.condvars[$submitted_token].wait_queue
finish
p $issuer->state
p block.condvars[$submitted_token].wait_queue
tbreak io_signal if cond == &block.condvars[$submitted_token]
continue
```

提交操作返回 token 后，调用者在同一个关中断保护中登记等待。观察 token 与条件变量的对应关系、等待登记后的线程状态，以及结束保护前已经保存的响应对象地址。

下一次停在完成中断发出通知的位置：

```gdb
bt 5
p *$response
p block.queue.used_idx
p block.queue.used->idx
p block.condvars[$submitted_token].wait_queue
finish
p $issuer->state
continue
```

结合中断处理中的 `vq_pop_used` 循环，说明完成 token 如何定位等待者。记录响应成功状态与线程进入就绪队列的时刻；继续运行直到 `huge_write` 输出耗时、速度并返回 shell，不要求固定性能数值。

读请求采用对应的流程。再运行 `filetest_simple`，观察文件读写结果。文件缓存可能使一次文件操作不产生设备请求；提交、完成中断和线程恢复是三个执行阶段，并非同一条连续调用栈。底层接口只提交请求，外层块读写接口仍然等到请求完成后才返回。

启动阶段 `dev_non_blocking_access` 为 `0`，底层同步轮询完成 I/O；任务开始运行前该标志才设为 `1`。启动阶段没有当前应用线程可供阻塞。请结合代码比较这两条路径，不在驱动中切换该标志。

## 5. virtio-gpu：帧缓冲初始化与刷新

重新启动调试会话，在底层帧缓冲建立处暂停：

```gdb
tbreak gpu_raw_setup_framebuffer
continue
finish
p gpu_driver.width
p gpu_driver.height
p gpu_driver.length
p/x gpu_driver.framebuffer
tbreak gpu_raw_setup_cursor
continue
p/x gpu_fb
p gpu_fb_len
p gpu_fb == gpu_driver.framebuffer
```

记录设备查询得到的尺寸、帧缓冲地址和长度，以及包装层保存的显存视图。光标初始化已提供，只需解释其与 GPU 构造的衔接。

继续至应用获取帧缓冲：

```gdb
tbreak virtio_gpu_get_framebuffer
continue
```

出现 shell 后，在终端一输入 `gui_shape`。命中断点时：

```gdb
bt 5
p/x gpu_fb
p gpu_fb_len
finish
p/x $a0
tbreak virtio_gpu_flush
continue
bt 5
p/x gpu_fb
p gpu_fb_len
continue
```

比较初始化与运行阶段的地址和长度，从调用栈识别图形系统调用。应用通过已有映射写入同一块设备显存；刷新系统调用向 GPU 提交显示更新。继续后，在图形窗口中观察蓝色圆形、红色方框和绿色三角形，不要求逐像素跟踪。

Docker 中如果无法正常显示，结合宿主机和容器环境，借助 AI 排查并解决问题。在 `reports/lab9.md` 中记录环境、现象、AI 辅助排查过程、采用的方法与最终显示效果，并保留相关 AI 交互记录。

## 6. 需要解释的调用关系

| 调用链 | 触发方式 | 需要解释的状态变化 |
| --- | --- | --- |
| 标准输入 → `uart_read` → `io_wait_no_sched` → `sched` | shell 等待输入 | 缓冲为空，登记等待并阻塞；切换前结束设备保护 |
| 中断分发 → `uart_handle_irq` → 接收缓冲入队 → `io_signal`；随后恢复读取 | 串口输入字符 | 字符进入缓冲，等待者进入就绪队列，获得 CPU 后重试读取 |
| 文件系统块适配 → `virtio_block_read_block/write_block` → 轮询或提交、等待 | 启动过程；文件操作 | 启动阶段无应用线程可阻塞，运行阶段按请求 token 等待；文件系统块与设备块由提供的适配层衔接 |
| `virtio_block_handle_irq` → `vq_pop_used` → 对应条件变量的 `io_signal`；随后恢复块读写 | 块请求完成 | 完成 token 对应等待者，恢复后检查响应状态 |
| `virtio_gpu_init` → 底层帧缓冲初始化 → 保存地址和长度 | 内核初始化 | 设备持有 DMA 资源，包装层保存同一块显存的视图 |
| 图形系统调用 → `virtio_gpu_get_framebuffer`；应用写入像素；刷新调用 → `virtio_gpu_flush` | `gui_shape` | 应用访问已有显存，驱动提交显示更新；用户映射只解释接口衔接 |

三组动态样例记录关键状态，其余关联过程结合源码说明。重点分析等待登记与设备操作为何要处于同一保护范围内，以及为什么调度前必须结束设备状态的受保护访问。

## 7. 报告要求

报告保存到 `reports/lab9.md`，保留对应 `reports/ch9-gdb.log`，至少包含：

1. **静态分析**：分别说明三个驱动的数据结构、主要接口与执行过程。比较 UART 轮询输出和中断输入、块设备两种访问路径，解释 GPU 显存访问与刷新之间的关系。可附必要代码片段或源码链接。
2. **动态跟踪**：记录实际断点、命令、关键状态和运行结果。UART 记录缓冲与等待队列变化；块设备记录提交和完成的 token、响应状态；GPU 记录显存地址、长度与显示结果。区分实际观察与源码推导。
3. **主要问题与解决思路**：描述现象、排查依据、解决过程与结果，未解决的问题如实记录。

本阶段不要求修改驱动或扩展其他设备。独立实现任务见 [API 实验文档](ucore-ch9-api.md)。
