# CZ Help：零点行动本地 BOT 辅助

Windows x86 C++ 程序，用于 Steam《Counter-Strike: Condition Zero》普通本地 BOT 对战。

## 开始使用

先启动零点行动，再打开辅助，即可从游戏进程自动找到安装目录。面板下方显示“自动识别”和完整路径，并保存到 EXE 旁的 `CZ_Help.ini`。下次未开游戏时会使用记住的目录。尚未识别且没有修改配置时，默认路径为 `D:\SteamLibrary\steamapps\common\Half-Life`。


| 操作 | 默认快捷键 |
| --- | --- |
| 透视总开关，保留独立选择 | F8 |
| 方框单独开启 / 关闭 | F9 |
| HP 数字单独开启 / 关闭 | F11 |
| 自动连跳开关，开启后按住空格 | F3 |


## 跳跃模式

| Mode | 单选模式 | 按住空格时的动作 |
| --- | --- | --- |
| 0 | 自动跳跃 | 落地自动续跳，移动和视角手动 |
| 4 | BHopJump | 连跳，空中根据手动转视角的方向配合侧移 |

按 F3 开启后按住空格执行，松开停止。

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

119 / 120 / 122 分别是 F8 / F9 / F11，114 是 F3。前三个快捷键非法或重复时回退到默认值；
修改游戏目录后需关闭并重开辅助；


依赖来源和许可见 `third_party/SOURCES.md`、`third_party/metamod-runtime/SOURCE.md`。
