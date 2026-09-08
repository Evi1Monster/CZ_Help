# CS 零点行动单机辅助实现计划

> 使用 subagent-driven-development 执行。用户已批准设计及实现，不再请求执行方式确认。此目录不是 Git 仓库，直接在现有空工程内实现，不创建或提交 Git 元数据。

**目标：** 为当前 Steam x86 Condition Zero 本地 BOT 对战交付可编译的插件、透明覆盖程序、可配置独立开关与安装恢复工具。

**架构：** Metamod 插件从服务端 edict 读取存活敌方 BOT，按帧更新带进程身份和时间戳的共享内存。桌面程序用命名互斥锁读取完整快照，计算屏幕包围框，绘制鼠标穿透覆盖窗口。

**技术栈：** C++17、MSVC v143、Win32/GDI、GoldSrc SDK、Metamod、CMake/CTest、PowerShell。

## 任务 1：协议、投影和控制行为（根代理）

文件：`shared/protocol.h`、`shared/core.h`、`shared/core.cpp`、`tests/core_tests.cpp`、`CMakeLists.txt`。

- [x] 定义固定宽度的 Snapshot / Camera / Player，32 个玩家槽，版本及有效标志。命名对象以本地游戏 PID 区分，快照需互斥锁保护。
- [x] 测试先行：正前方映射屏幕中心；右侧与上方点的方向正确；背后、无效 FOV、NaN、无效窗口被拒绝；近裁面穿过包围盒时裁剪；长按只切一次；失去焦点不切换；总开关保留独立选择；过期/错误 PID/协议被拒绝。
- [x] 运行 `cmake --build build --config Debug --target core_tests` 与 `ctest --test-dir build -C Debug --output-on-failure`，先确认缺少行为导致失败，再实现。
- [x] 投影使用 GoldSrc AngleVectors 基向量和水平 FOV，按实际客户区比例计算垂直投影；相机瞬态误差在游戏验证中记录。

## 任务 2：服务端插件（插件代理）

文件：`plugin/*`、`third_party/halflife/*`、`third_party/metamod/*`、依赖来源清单。

- [x] 获取固定提交的一手 SDK 与 Metamod 头文件并保留许可；不使用猜测的游戏私有内存偏移。
- [x] 实现标准 Meta_Query/Attach/Detach、GiveFnptrsToDll、GetEntityAPI2 接口，x86 导出名称正确。
- [x] 通过本地 listen server、唯一真人、BOT 标志、队伍与存活状态构造快照；通过 TeamInfo 消息或已验证实体数据判断队伍，不以未经验证的私有字段作真值。
- [x] 写端使用非阻塞互斥锁；时间来自 GetTickCount64；切图与卸载清空数据；提供 `cz_help_status` 控制台诊断命令。
- [x] 构建 DLL 并检查导出；使用实际游戏或隔离本机对局验证加载、实体数量、血量和相机数据。

## 任务 3：桌面覆盖和设置（根代理）

文件：`CZ_Help/main.cpp`、`CZ_Help/settings.*`、`shared/channel.h`、原 vcxproj 和 filters。

- [x] Win32 透明置顶窗口跟随对应 PID 的游戏客户区，失去前台、最小化、超时后隐藏；鼠标穿透且不抢焦点。
- [x] F8 总开关、F9 方框、F11 血量；INI 可配置 VK 码，检查重复/非法键；总开关每次启动关闭，独立选项持久化。
- [x] 绘制二维边框和随血量渐变颜色的 HP 数字；只绘制有效敌方存活 BOT；无数据时在控制窗口报告原因。
- [x] 提供只读 `--probe PID` 输出实际插件快照用于验证，禁止把测试夹具伪装成实际游戏连接。
- [x] 测试真实共享内存的读取/超时以及应用启动退出；编译 Debug 和 Release。

## 任务 4：安装恢复与交付（脚本代理）

文件：`scripts/Install.ps1`、`scripts/Uninstall.ps1`、`scripts/Launch.ps1`、`scripts/Build.ps1`、`tests/install_tests.ps1`、`README.md`。

- [x] 获取官方 Metamod Windows 二进制并记录确切 URL、版本和 SHA-256。
- [x] 安装前要求游戏退出，验证游戏路径和产物；备份原文件，幂等安装；已有插件配置予以保留。恢复仅处理拥有的文件，拒绝覆盖安装后用户的冲突变更。
- [x] 在项目内临时游戏目录执行首次安装、重复安装、保留其他插件、恢复、冲突恢复测试；在真实游戏安装前先构建并审查所有文件。
- [x] Launch 以用户确认的路径启动窗口模式本地 BOT 游戏；提供明确的启动与三个快捷键说明。

## 集成与验收（根代理）

- [ ] 额外独立代理复审未完成；根代理已检查协议、插件生命周期、窗口同步与安装恢复，并完成集成测试。
- [x] x86 Debug/Release 构建与 CTest 通过；保留证据于 `docs/verification.md`。
- [x] 获授权范围内安装并在当前游戏做本地加载验证；记录墙后标记、蹲下、开镜、伤害更新的已测/未测范围。
- [x] 输出可启动程序、使用说明和限制；不将模拟或仅编译成功称为实机验证。

## 交付状态

已完成实现、安装、Debug/Release 构建和各 5 组自动测试。用户已确认方框透视实机可用，并要求移除血条；现已改为只有变色 HP 数字。伤害变化、开镜精确对齐等实机验收的未完成项见 docs/verification.md。
