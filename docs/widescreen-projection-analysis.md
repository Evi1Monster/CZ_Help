# 宽屏投影偏移：只读分析记录

日期：2026-09-06。

用户已经确认全屏显示和 F8 / F9 / F11 正常；本次问题是 BOT 偏离准星后，方框和 HP 与实际角色位置的偏差增大。本记录只分析投影，不修改或控制运行中的游戏。

## 本机证据

分析对象：`D:\SteamLibrary\steamapps\common\Half-Life\hw.dll`，文件大小 3,598,176 字节。

SHA-256：`9BA9A2DB5E07598FD59AFA35507A98C86162E4E15B3835177B78C11842CD2295`。

`czero/config.cfg` 的保存值：

- 第 123 行：`default_fov "90"`。
- 第 147 行：`gl_use_shaders "1"`。
- 第 150 行：`gl_widescreen_yfov "1"`。
- 第 208 行：`viewsize "120.000000"`。

精确变量名是 **`gl_widescreen_yfov`**；`hw.dll` 的字符串和对应 cvar 初始数据也确认了名称及默认字符串 `1`。配置文件是保存值，运行中的当前值应通过现有引擎 cvar 接口读取。

Valve 的 [Half-Life 25 周年官方更新说明](https://steamcommunity.com/app/70/allnews?l=english)明确记载新增 “Allow widescreen Field of View” 设置。官方说明证实功能存在；以下严格阈值与数学公式来自本机二进制本身，而不是将其他 GoldSrc 分支的实现当成 Steam 源码。

## 分析方法与确定的规则

使用 MSVC `dumpbin /disasm:nobytes` 和 Python 标准库，只读取磁盘 PE 文件：

1. 通过 PE 节表解析 cvar 名称字符串引用及其初始结构。
2. 检查读取该 cvar 浮点值的两段投影代码。
3. 解码比较与三角运算的常量；按 PE 导入表确认调用的数学函数为 `_libm_sse2_tan_precise` 和 `_libm_sse2_atan_precise`。

两段代码的条件一致：cvar 非零，并且 **`float(width) / float(height) > 1.5f`**。宽高比小于或等于 1.5 时，比较后的 `jbe` 分支直接跳过宽屏修正。

启用修正时：

```text
aspect = width / height
horizontalFov = 2 * atan(tan(baseFov * pi / 360) * aspect * 0.75) * 180 / pi
```

解码的常量分别为 `1.5f`、`0.008726646259971648`（π / 360）、`0.75f` 和 `57.29577951308232`（180 / π）。修正前没有 `baseFov == 90` 的条件；有效的开镜基准 FOV 同样参与计算。该引擎条件是 cvar 非零，不存在某些其他渲染器所提供的值 `2` 专用拉伸分支。

本机规则的计算示例：

| 尺寸 | 基准 FOV | 有效水平 FOV |
|---|---:|---:|
| 1920 × 1080 | 90° | 106.2602047° |
| 1920 × 1080 | 40° | 51.7740088° |
| 1920 × 1080 | 15° | 19.9121553° |
| 1440 × 900 | 90° | 100.3888578° |
| 1280 × 1024 | 90° | 90° |
| 1200 × 800（恰好 3:2） | 90° | 90° |

因此不应对全部宽高比统一应用 Hor+，也不应将条件写成 `aspect > 4 / 3`。

## 最小修复建议

通过既有引擎 cvar 接口读取当前开关，将宽屏模式传给共享投影计算，在上面的严格阈值下换算有效水平 FOV。内部绘制和桌面回退需要采用相同规则。无需改变用户视频设置，也无需新增矩阵钩子。

1920 × 1080、基准 90°时，当前算法的投影尺度是 `960`，正确宽屏尺度是 `720`。因此当前方框相对屏幕中心的水平和垂直偏移都会大约放大到真实偏移的 4 / 3；BOT 越靠近边缘，偏差越明显，位于准星附近时误差较小。这与用户反馈一致。

建议回归覆盖：关闭开关、4:3、5:4、恰好 3:2、略高于 3:2、16:10、16:9、超宽屏以及开镜基准 FOV；用手工推导的屏幕坐标检查中心和离中心目标。

## 精确矩阵选项与本次边界

只读导入检查表明，`hw.dll` 静态导入了 `glBegin`、`glViewport`、`glMatrixMode`、`glLoadIdentity`、`glOrtho`，也导入 `SDL_GL_GetProcAddress`。`glFrustum`、`glLoadMatrixf` 等名字存在于文件字符串中，但不是其静态 OpenGL 导入项，不能直接假设已有对应 IAT 槽可替换。

交换缓冲时的当前矩阵可能已经用于 HUD、菜单或最终合成，不能把该时刻的 `GL_MODELVIEW_MATRIX` / `GL_PROJECTION_MATRIX` 直接当成世界摄像机矩阵。即使在命名 OpenGL 入口采样，也需要先验证该入口真正用于世界绘制，并区分世界、武器、天空和多视口；这些工作超出本次宽屏修复。

Valve SDK 的 [triangleapi.h](https://github.com/ValveSoftware/halflife/blob/master/common/triangleapi.h)提供客户端 `WorldToScreen` 和 `GetMatrix` 接口，但这是客户端引擎函数表，不能凭服务器 Metamod 函数表直接取得。SDK 的 [view.cpp](https://github.com/ValveSoftware/halflife/blob/master/cl_dll/view.cpp)也展示客户端对最终视点的调整，因此服务器快照与最终画面仍可能存在预测、视角晃动和实体插值差异。

本次没有对运行中游戏添加钩子、读取私有进程地址或更改设置。磁盘分析中的地址仅用于确认算法，不应写入运行时代码。此规则针对上述哈希标识的本机 Steam 引擎；实际修复后仍需验证角色位于屏幕左右及上下时的对齐效果，不能仅凭单元测试宣称游戏对齐已实测。
