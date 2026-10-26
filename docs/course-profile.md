# 课程配置

`course-profile.json` 是课程工具的配置来源。`scripts/course_profile.py` 校验配置，安装器、编辑器 hooks 和跨分支测试共同使用它。

| 字段 | 含义 |
| --- | --- |
| `schema_version` | 配置格式版本，当前为 `1` |
| `project` | 项目标识；派生 Cursor/Copilot 的 `<project>-hooks` 目录 |
| `display_name` | 安装与诊断信息中的课程名称 |
| `plugin_name` | 插件名及 `plugins/<plugin_name>` 源目录 |
| `marketplace_name` | 插件源名称；完整插件 ID 为 `<plugin_name>@<marketplace_name>` |
| `bootstrap_branch` | 工具基线和完整集成测试所在分支，目前为 `main`；首次安装也可在任一实验分支执行 |
| `lab_branches` | 实验分支到必要文件或目录的映射，当前为 `ch3-api` |

分支集合表示课程验证范围，不限制工具在其他分支运行。实验合同中的路径来自实际分支树；当前 `ch3-api` 检查 `os/main.c`、`Makefile` 和 `bootloader`。它只验证实验结构，内核构建和实验功能仍由各章测试负责。

在任一实验分支安装后，配置与工具复制到 `.ai/course-tools/`。Cursor/Copilot 的本地 hook 目录还保存配置和加载器，因此切换实验分支后，记录、归档与再次配置仍然可用。从当前分支重新安装可刷新配置；已有记录开关、归档模式、日志和自定义 Git 排除规则继续保留。

客户端需要静态插件声明，因此 `.agents/plugins/marketplace.json`、`.claude-plugin/marketplace.json` 及插件目录下两份 `plugin.json` 仍然保留。安装前会校验它们的名称和路径与 profile 一致。移植到其他课程时，修改 profile、重命名插件源目录，并同步这些声明及示例配置；不必修改通用 Python 运行逻辑。已安装项目的身份重命名迁移不在本次范围内，应另行处理旧插件与旧 hook。

在包含完整集成测试的 `main` 分支运行：

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s plugins/ucore-session-archive/tests -v
```

集成测试使用临时克隆和模拟客户端，安装一次后逐一切换 profile 中的真实实验分支，验证课程事件、四类 Agent 归档、配置副本、实验结构、Git 工作区清洁，以及三类课程记录通过普通 `git add .` 进入测试提交。测试不会切换开发者当前仓库分支或安装真实客户端插件。另一套项目身份的测试验证名称和路径确实由配置驱动。测试所需 `origin/<实验分支>` 引用必须已存在于本地仓库，VSIX 集成验证还需要 Node.js。

课程事件中的 Codex 会话结果根据最终状态记录：正常完成为 `completed`，临时错误恢复后完成为 `completed_after_retry`，未恢复的 turn 失败为 `codex_turn_failed`，进程非零退出为 `codex_process_failed`。未恢复的 turn 失败即使客户端错误地返回零，也向调用者返回失败。
