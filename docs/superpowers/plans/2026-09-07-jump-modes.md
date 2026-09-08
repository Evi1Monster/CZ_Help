# 1.4.0 跳跃模式实现计划

在当前已授权任务内顺序实现；没有 Git 仓库，不创建提交或新任务。

**目标：** 六种可解释、互斥、可随时中止的跳跃模式，并保留自动配置版。

**架构：** shared 模式定义 / v2 控制包 → Win32 radio 与 INI → plugin JumpAssist SDK 状态机 → 原游戏物理和视角同步。

**技术栈：** C++17、Valve SDK、Win32、PowerShell 5.1、CTest。

- [x] 新增 shared/jump_modes.h 和 plugin/kz_movement.h 接口；编写 plugin/kz_tests.cpp，使用真实 playermove_t 对六模式和中断建立失败用例。为编译提供空实现，运行 kz_movement 观察动作缺失失败。
- [x] 实现 plugin/kz_movement.cpp：落地连跳、手动转向配侧移、定时自动侧移、一次四段 LJ、三次小跳 MCJ、Double Duck。调用间保存阶段，原始按键丢失 / 模式代次变化时复位。测试基本模式与 ApplyBhop 字节一致、姿态 / 速度 / 重力不变。
- [x] shared/bhop_control.h 添加 mode / generation，拒绝 v1 / 非法模式；tests/bhop_channel_tests.cpp 增加模式传输和拒绝测试；plugin/plugin.cpp 接入状态机及本地视角同步，boundary_tests.cpp 验证 BOT / 水中 / 死亡 / 过期不变。
- [x] CZ_Help/settings.*、tests/settings_tests.cpp 保存 [Movement] Mode=0..5，非法值回退 0 并提示。main.cpp 增加同组 BS_AUTORADIOBUTTON、模式说明和当前模式状态；F3 每次启动关闭，模式修改推进 generation。
- [x] 扩充 tests/native_session_tests.ps1 可选模式验证：实际 EXE 的六 radio 互斥、选项重启持久化、自动准备 / 清理不回归。
- [x] 构建 Debug / Release，运行 SDK / 控制 / 配置 / 界面和原绘制测试；检查边界与剩余限制。更新 1.4.0 版本、说明与验证记录。
- [x] 单独打包 1.4.0、核对 ZIP 和全部文件哈希，测试真实包内 EXE 启动 / radio / 自动清理，交付两个版本链接。
