# uCore 第九章：I/O 设备管理

本章在单核 QEMU RISC-V 环境中提供 UART 串口、virtio-blk 块设备与 virtio-gpu 图形设备，以及接入设备所需的中断、线程等待、文件系统和图形应用。学习内容包括串口中断接收与阻塞读取、块请求提交与完成通知，以及帧缓冲初始化、访问和显示刷新。

阅读材料为[《rCore-Tutorial-Book-v3》第九章：I/O 设备管理](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter9/index.html)。本仓库的接口和代码使用 C 语言。

先阅读[第九章教学正文](docs/chapter9.md)，理解设备访问、中断与线程等待，再进入下方的分析和独立实现任务。

| 分支 | 用途 | 说明文档 |
| --- | --- | --- |
| `ch9` | 完整参考代码、静态分析与 GDB 动态跟踪 | [源码分析与动态跟踪](ucore-ch9-analyze.md) |
| `ch9-api` | 三个驱动、八处接口的独立实现 | [API 实验文档](ucore-ch9-api.md) |
| `ch9-api-impl` | 与学生任务配套的完整参考实现 | [API 实验文档](ucore-ch9-api.md) |

实验报告保存到 `reports/lab9.md`。分析阶段阅读完整参考实现；独立实现阶段在 `ch9-api` 提交代码、报告与过程日志。允许修改的代码范围由 [lab.json](lab.json) 和 [API 实验文档](ucore-ch9-api.md) 规定。

## 环境准备

构建需要 GNU Make、主机 C 编译器、Python 3、RISC-V 裸机 GCC 与 binutils。运行需要支持 RISC-V `virt` 平台及 VirtIO GPU 的 QEMU；动态跟踪需要支持 RISC-V 的 GDB。

Ubuntu/Debian 可安装：

```bash
sudo apt-get update
sudo apt-get install make gcc python3 gcc-riscv64-unknown-elf \
  binutils-riscv64-unknown-elf qemu-system-misc gdb-multiarch
```

默认交叉工具前缀为 `riscv64-unknown-elf-`；使用其他兼容工具链时，通过 Make 变量 `TOOLPREFIX` 指定前缀。默认使用 `gdb-multiarch`，可以通过 `GDB` 变量指定其他支持 RISC-V 的 GDB。本章提供用户程序、链接脚本和文件系统镜像构建入口，不需要另行克隆用户程序仓库。

## 构建与运行

在仓库根目录执行：

```bash
make build
make run GUI=on
```

`make build` 构建内核、配套用户程序和文件系统镜像。在 QEMU 串口 shell 中依次输入：

```text
filetest_simple
huge_write
gui_shape
```

`filetest_simple` 验证文件写入与读回一致；`huge_write` 写入 1 MiB 文件并输出耗时与速度；`gui_shape` 在图形窗口显示蓝色圆形、红色方框和绿色三角形。具体预期结果见 [API 实验文档](ucore-ch9-api.md#运行与验收)。自动运行相同验收程序可执行 `make test-ch9`。

串口中的命令输入、应用输出与图形窗口分别由 UART 和 GPU 提供。没有字符输入时，读取线程应阻塞等待中断通知；块设备启动阶段使用轮询访问，任务运行后使用提交请求、阻塞等待和完成中断。

如果在 Docker 环境运行图形程序时无法显示，请借助 AI 结合宿主机显示服务、容器的显示连接和 QEMU 显示后端排查并解决。在 `reports/lab9.md` 记录问题现象、排查依据、解决方法及验证结果，并保留相关 AI 交互记录。

## 实验过程记录

在实验分支的仓库根目录运行：

```bash
python3 course.py
```

工具初始化 AI 会话归档并打开实验工作区。安装要求、日志位置和日常命令见[实验过程记录说明](docs/course-recording.md)。可通过 `python3 course.py --agent codex` 指定客户端；支持的客户端及归档设置见[AI 会话归档说明](docs/agent-session-archive.md)。

实验代码提交应包含开发过程的操作日志和 AI 交互日志。`.ai/events/`、`.ai/agent-sessions/` 和 `.ai/submissions/` 的相关记录随实验代码一起提交；请勿改动或删除过程记录。安装后可运行 `python3 course.py status` 或 `git course status` 检查记录状态，运行 `git course logs` 查看日志。

Codex 首次使用时，在实验仓库终端运行 `codex`，输入 `/hooks`，审阅并信任 `ucore-session-archive` 的 `Stop` 和 `SessionEnd` hooks。使用 VS Code Codex 时，还需重载窗口并新建会话；更新插件后如提示 hooks 变化，应重新审阅。
