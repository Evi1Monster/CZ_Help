# CZ Help 1.1.1 使用说明

适用于 Windows 上 Steam《CS 零点行动》的普通单机 BOT 对战，使用 OpenGL 渲染。支持全屏与窗口模式。

## 安装与启动

1. 将整个 ZIP 解压到一个固定目录；先退出游戏和旧版辅助程序。
2. 双击 **1-Install-or-Update.cmd**。默认游戏路径为 `D:\SteamLibrary\steamapps\common\Half-Life`。已有安装会更新插件并保留最初的恢复备份。
3. 双击 **2-Launch.cmd** 启动辅助和游戏，进入本地 BOT 对局，加入队伍。启动器沿用游戏保存的全屏 / 窗口选项，并选择 OpenGL。
4. 按 **F8** 开启透视。**F9** 控制方框，**F11** 控制 HP 数字。每次启动总开关默认关闭。

HP 只显示数字，随血量比例从绿色渐变为红色，没有血条。辅助程序需要一直运行；快捷键只在游戏前台响应。

其他游戏目录：在 PowerShell 中运行 `scripts\Update.ps1 -GamePath '你的 Half-Life 目录'`，再运行 `scripts\Launch.ps1 -GamePath '你的 Half-Life 目录'`。需要窗口化时加 `-Windowed`。

## 本版变化

- 修复启用宽屏视野时，BOT 偏离准星后方框与 HP 偏离真实位置的问题。投影实时跟随游戏的 `gl_widescreen_yfov` 设置、当前尺寸和开镜 FOV。
- 新版插件直接向游戏 OpenGL 画面绘制，解决独占全屏中桌面覆盖窗口不可见的问题。
- 控制面板在连接成功后显示“游戏内绘制”。全屏修复需要同时更新 EXE 和插件 DLL。
- 保留方框开关、变色 HP 数字、快捷键和设置保存。
- 新增安装或更新入口，卸载仍能恢复初次安装前的文件。

连跳 / KZ 功能不包含在本版中。

用户已实机确认全屏与 F8 / F9 / F11 正常。宽屏位置修复增加了独立 OpenGL 投影对照测试；具体构建和测试结果见源码中的 `docs\verification.md`。这一位置修复仍需用户在实际 BOT 对局中复核。

1.1.1 最终 Debug / Release 全量构建和自动测试均通过（各 7/7），包括宽屏开 / 关时方框和 HP 的像素对照。

## 设置与排查

- 设置文件是 `bin\CZ_Help.ini`；也可在控制面板中点击“编辑快捷键设置”。F8 / F9 / F11 键码为 119 / 120 / 122。
- 显示“桌面绘制（全屏请更新插件）”：退出游戏后重新运行安装或更新，再启动。请确认视频渲染为 OpenGL。
- 本地对局需 `sv_lan 1`，只有一名真人，玩家存活且处于第一人称。需要至少一名存活敌方 BOT；进入远程对局、死亡、旁观或数据过期会停止标记。
- 控制台 `bot_add_t` / `bot_add_ct` 添加 BOT；如 BOT 不行动，输入 `bot_stop 0`。
- 游戏控制台 `meta list`、`cz_help_status`、`cz_help_dump` 可查看插件和数据状态。
- 服务端相机与客户端预测可能存在瞬态偏差；快速移动、后坐力和开镜对齐仍有精度限制。

## 卸载与源码

退出游戏后双击 **3-Uninstall.cmd**。游戏目录 `czero\addons\cz_help\install-state.json` 保存初始恢复数据，请保留。安装后自行修改过的文件会触发冲突提示，不会直接覆盖。

`source` 包含本版源码、构建脚本、验证记录和依赖许可。构建需要 Visual Studio 2022 C++ 桌面开发组件、Windows SDK、CMake 3.20 以上；运行 `source\scripts\Build.ps1 -Configuration Release`。源码中的 `docs\verification.md` 记录具体测试范围。
