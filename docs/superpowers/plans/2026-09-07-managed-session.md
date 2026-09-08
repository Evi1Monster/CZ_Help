# 自动配置与清理实现计划

> 在当前已授权任务中并行实施：根代理负责 Win32 接入，子代理负责会话脚本与回归，独立代理负责打包入口与文档。目录不是 Git 仓库，不创建提交。

**目标：** 发布 1.3.0，用户直接运行 EXE 自动配置并获得明确状态，关闭后自动恢复插件。

**架构：** Win32 SessionManager 启动 Hidden PowerShell Session.ps1；后者持有每游戏互斥锁并检测 owner PID+创建时间。状态文件 UTF-8 首行使用 starting/preparing/ready/waiting_game_exit/waiting_cleanup/cleaning/cleaned/error，其后为可选错误详情。

**技术栈：** C++17/Win32、PowerShell 5.1、CMake/CTest。

## 1. 会话管理脚本

文件：scripts/Session.ps1、Common.ps1、Install.ps1、Update.ps1、Uninstall.ps1、tests/session_tests.ps1。

- [x] 测试首次准备、退出/崩溃恢复、活动游戏延迟、版本不匹配等待、owner 身份、并发守护和原文件冲突；观察预期失败。
- [x] 实现 owner 生命周期、目录锁、原子 UTF-8 状态发布及准确游戏路径检查。
- [x] 卸载逐项接受 Installed/Original/Absent 状态，拒绝未知修改，处理残留事务。
- [x] 运行 session_tests.ps1 与 install_tests.ps1/update_tests.ps1。

## 2. 原生界面接入

文件：CZ_Help/session.h/.cpp、main.cpp、settings.h/.cpp、tests/session_client_tests.cpp、CMakeLists.txt。

- [x] 为状态解析、路径/Windows 参数引号、旧配置与自定义路径添加失败测试。
- [x] SessionManager.Start 使用当前进程 FILETIME、绝对 bundle 路径、独立状态文件启动 Hidden worker，异步读取状态并检查 worker 是否退出。
- [x] 启动显示准备状态；只识别所选游戏。准备成功后支持启动游戏按钮；关闭 UI 发布 off，后台 worker 完成清理。
- [x] smoke/probe 不执行配置，旧热键与显示回归保持通过。

## 3. 打包与验收

文件：scripts/Package.ps1、Launch.ps1、版本、README、release-1.3.0.md、verification.md。

- [x] 根目录 EXE/INI 与完整脚本/payload；启动器转交 --game-path/--launch-game/--windowed，不要求手动安装。
- [x] Debug/Release 构建成功，Release 11/11；Debug 10/11，前台焦点环境失败已记录。会话集成与安装恢复通过。
- [x] 在本机游戏退出时验证自动准备和自动清理，保留其他游戏文件与用户配置。
- [x] 打包 1.3.0，解压核对哈希和真实包内启动/清理后交付。

## 后续需求

发布 1.3.0 后再处理现有自动跳跃与 AutoBhopJump、LongJump、MCJ、BhopJump、狗跳的 radio 选择。已向用户询问各模式具体动作及差异，本版不提前猜测实现。
