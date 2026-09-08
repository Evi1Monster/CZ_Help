# CZ Help：零点行动本地 BOT 辅助

Windows x86 C++ 程序，用于 Steam《Counter-Strike: Condition Zero》普通本地 BOT 对战。1.4.2 可从已运行的零点行动自动识别并记住目录，在运行时自动准备插件，关闭后自动恢复配置和清理。保留 OpenGL 全屏 / 窗口方框、变色 HP 数字及自动跳跃与 BHopJump 两种单选模式。

## 开始使用

先启动零点行动，再打开辅助，即可从游戏进程自动找到安装目录。面板下方显示“自动识别”和完整路径，并保存到 EXE 旁的 `CZ_Help.ini`。下次未开游戏时会使用记住的目录。尚未识别且没有修改配置时，默认路径为 `D:\SteamLibrary\steamapps\common\Half-Life`。

1. 完整解压发布包，直接打开根目录 **CZ_Help.exe**，等待显示“准备就绪 · 等待游戏启动”。无需手动安装。
2. 点击 **启动本地游戏**，新建 BOT 对局并选择队伍。也可自行启动游戏；对局需 `sv_lan 1`。至少有一名存活敌方 BOT 时才会出现标记。
3. 在游戏中按 **F8** 开启透视。每次启动辅助程序时总开关默认关闭。
4. 在面板选择一种跳跃模式，按 **F3** 开启，再在游戏中按住 **空格**。松开停止，F3 再次关闭；每次启动总开关默认关闭。
5. 关闭辅助即停止辅助功能。若游戏已退出，自动恢复配置并删除本软件的插件；若游戏仍在运行，后台清理进程等游戏退出后完成。

首次配置或版本不同而游戏已打开时，会显示“等待游戏退出后自动配置”；先退出游戏，软件会自动准备。准备期间不会强行关闭游戏。若旧清理进程仍在等待游戏退出，重新打开的辅助会等待该次清理完成。

自动识别目录不代表插件可以在游戏运行中首次加载。先开游戏的用法是：打开辅助识别目录 → 按提示退出游戏 → 等待准备就绪 → 点击“启动本地游戏”。

目录优先级为：`--game-path` 参数 → 已运行的零点行动 → INI 路径。软件读取 `hl.exe` 的进程路径与命令行，要求 `-game czero`，并检查 `hl.exe`、`czero/liblist.gam` 和 `czero/dlls/mp.dll`。不会把以 `-game cstrike` 启动的 CS 1.6 当作零点行动。多个安装目录同时运行时使用与配置匹配的目录；无匹配则提示指定目录，不自动安装。进程权限不足或 Windows WMI 不可用时会提示，并回退到配置。目录只在辅助启动时选择一次。

| 操作 | 默认快捷键 |
| --- | --- |
| 透视总开关，保留独立选择 | F8 |
| 方框单独开启 / 关闭 | F9 |
| HP 数字单独开启 / 关闭 | F11 |
| 自动连跳开关，开启后按住空格 | F3 |

复选框可切换开关；下方两个单选按钮选择跳跃模式。血量只显示 HP 数字，没有条状血量；数字颜色按当前血量比例从绿色渐变到红色。长按开关键只切换一次，切换到其他应用后不响应。控制台 `bot_add_t` / `bot_add_ct` 可增加对应队伍 BOT。

连跳需要空格保持绑定为 `+jump`（默认设置），辅助程序持续运行；关闭、切后台、死亡、旁观或非本地对局时停止生效。水中、梯子和冻结状态保留原操作。连跳沿用原游戏速度、重力和跳跃规则，不包含存点或回点。

测试曾尝试设置 `bot_stop 1`；如果 BOT 不行动，输入 **`bot_stop 0`** 恢复。`bot_quota 4` 可设置 BOT 数量，具体敌我平衡由游戏设置控制。

## 跳跃模式

| Mode | 单选模式 | 按住空格时的动作 |
| --- | --- | --- |
| 0 | 自动跳跃 | 落地自动续跳，移动和视角手动 |
| 4 | BHopJump | 连跳，空中根据手动转视角的方向配合侧移 |

模式选择自动保存，F3 总开关每次启动关闭。保留原来的 BHopJump 配置编号 4；旧配置中的其他模式会回退自动跳跃并提示。移除 AutoBhopJump、LongJump、MCJ、狗跳及自动视角控制。

按 F3 开启后按住空格执行，松开停止。BHopJump 的实际游戏手感和客户端预测校正仍待实际游玩验收。说明见 `docs/release-1.4.1.md`。

## 修改快捷键

EXE 旁的 `CZ_Help.ini` 保存设置。点击“编辑设置”，修改 Windows 虚拟键码，再点击“重新加载设置”：

```ini
[Game]
Path=D:\SteamLibrary\steamapps\common\Half-Life
[Hotkeys]
Toggle=119
Boxes=120
Health=122
BhopToggle=114
[Movement]
Mode=0
[Display]
Boxes=1
Health=1
```

119 / 120 / 122 分别是 F8 / F9 / F11，114 是 F3。前三个快捷键非法或重复时回退到默认值；连跳键非法、占用空格或与透视键冲突时，仅禁用连跳快捷键并提示，仍可用复选框开启。方框、HP 与跳跃模式偏好自动保存；透视总开关和连跳每次启动关闭。

修改游戏目录后需关闭并重开辅助；正在运行的零点行动优先于 INI。需要明确选择其他目录时，使用 `--game-path`，该参数不覆盖保存的路径。完整包内的脚本和依赖必须保留，不要只复制 EXE。使用命令行 `CZ_Help.exe --game-path "游戏目录" --launch-game` 可自动准备后启动游戏，加 `--windowed` 可强制窗口化；`scripts/Launch.ps1` 是兼容入口。

## 构建

需要 Visual Studio 2022 C++ 桌面开发组件、Windows SDK 和 CMake 3.20 以上。SDK 头文件及运行时已包含，构建无需联网。

```powershell
.\scripts\Build.ps1 -Configuration Release
.\scripts\Build.ps1 -Configuration Debug
.\tests\install_tests.ps1
.\tests\update_tests.ps1
.\scripts\Package.ps1
```

也可打开原来的 `CZ_Help.sln`，选择 **Release / x86** 并生成；工程调用同一 CMake 构建，包含插件和测试。

输出为 `build/Release/CZ_Help.exe` 和 `build/Release/cz_help_mm.dll`。

## 自动恢复与诊断脚本

日常使用直接运行 EXE。下列脚本保留用于排查和恢复，运行前需退出辅助和游戏：

```powershell
.\scripts\Install.ps1
.\scripts\Update.ps1
.\scripts\Uninstall.ps1
```

其他安装目录可使用 `-GamePath 'D:\YourLibrary\Half-Life'`，Launch.ps1 也支持此参数。

会话管理保留最初修改前的文件字节和其他插件内容。已有 1.2.0 安装会沿用初始备份进行迁移。辅助意外退出后，隐藏管理进程执行同样的清理；如果整个系统中断，下次启动尝试恢复可识别的残留。

会话期间的备份位于 `czero/addons/cz_help/install-state.json`，成功清理后自动移除。恢复前检查全部文件：若你修改过其中某个文件，会指出冲突并保留当前文件与备份；此时不会误报清理完成，不要手动删除备份。

插件只在本地 listen server、`sv_lan=1`、只有一名真人且玩家存活时发布数据。远程对局、第二名真人、旁观、死亡、切图或数据超时后停止绘制。

## 诊断与验证范围

游戏控制台：`meta list`、`cz_help_status`、`cz_help_dump`。

外部只读诊断，替换为 `hl.exe` 的真实 PID：

```powershell
.\build\Release\CZ_Help.exe --probe 1234 | Out-Host
```

用户已确认上一版软件正常运行，并确认全屏显示及 F8 / F9 / F11 正常。宽屏位置修复使用真实 OpenGL 参考投影核对方框和 HP 像素。新增连跳经过 SDK 边界与控制通道测试，**实际游戏中的连续跳跃与预测手感尚未实机验收**。具体构建与测试结果见 `docs/verification.md`。

相机来自服务端，快速移动、后坐力、开镜及客户端预测可能造成瞬态偏差。尚未验收的精度不能视为已经保证。

依赖来源和许可见 `third_party/SOURCES.md`、`third_party/metamod-runtime/SOURCE.md`。保留了 Valve SDK 的非商业增强使用条件和 Metamod 的上游版权说明。
