# 服务端插件实现记录

日期：2026-09-05。此记录覆盖 SDK 类型边界测试和构建，不等同于实际 Condition Zero 对局验证。

## 实现与接口

- `plugin/plugin.cpp` / `plugin/sdk_bridge.h`：C++17、Windows x86、静态 CRT。源码使用 GPL-2.0-or-later，沿用 Metamod 的 HL Engine 链接例外；依赖来源与许可见 `third_party/SOURCES.md`。
- `plugin/exports.def` 提供未修饰 `GiveFnptrsToDll`（stdcall 8 字节参数）、`Meta_Query`、`Meta_Attach`、`Meta_Detach`、`GetEntityAPI2`、`GetEntityAPI2_Post`、`GetEngineFunctions`。
- 游戏 `StartFrame` 后钩子从公开 SDK 的 `edict_t::v` 生成 `cz::Snapshot`。共享内存名称和非阻塞互斥锁由根代理的 `shared/channel.h` 提供。锁忙时丢弃当前发布并累计 `dropped`，不等待。
- 活动输出要求：地图激活、非 dedicated、`sv_lan=1`、恰好一名真人、该真人位于 listen host 的第 1 槽、未观察到远程连接地址、存活且 `iuser1=0`、明确有效队伍。
- 仅发布敌方存活 `FL_FAKECLIENT` BOT。血量为 `edict->v.health` 原值；包围盒为实际 `origin/mins/maxs`，支持 crouch hull。未读取私有数据指针或猜测偏移。
- 队伍优先从 `TeamInfo` 的玩家 byte 和队名 string 获取；未知 / spectator 消息明确阻止模型回退。未收到消息时只允许 stock CS/CZ 模型映射，包含 militia、spetsnaz。`entvars.team` 未作为判断依据。
- 相机为 `origin + view_ofs`、`v_angle + punchangle`、`v.fov`；FOV 为零时用 GoldSrc 默认 90。拒绝非有限坐标、非法 FOV 和退化 BOT hull。
- 切图的 `ServerDeactivate` 清空队伍与共享快照，卸载清空并关闭映射。普通运行期卸载禁用，因为 GoldSrc 的控制台命令无法直接安全注销；请退出游戏后更新插件，勿强制 live unload。
- `cz_help_status` 输出 PID、map/channel、状态码、local/team、enemy_bots、丢帧数和 TeamInfo ID。状态码：0 Waiting、1 Active、2 NotLocal、3 NoPlayer、4 Spectating。
- `cz_help_dump` 在活动本地对局中输出相机和所有 BOT 的实际队伍、HP、存活状态、位置与 hull Z，供根代理实机核对。非活动对局拒绝明细输出。

## 验证证据

构建工具：`E:/Program/qt/Tools/CMake_64/bin/cmake.exe`，Visual Studio 2022 Win32，MSVC 14.44.35207。构建命令：

```powershell
cmake --build build --config Debug --target cz_help_mm plugin_boundary_tests
cmake --build build --config Release --target cz_help_mm plugin_boundary_tests
ctest --test-dir build -C Debug -R plugin_boundaries --output-on-failure
ctest --test-dir build -C Release -R plugin_boundaries --output-on-failure
dumpbin /exports /headers build/Release/cz_help_mm.dll
```

最终验证结果：Debug 和 Release 的 DLL 与测试程序均构建成功；两种配置的 `plugin_boundaries` CTest 均为 1/1 通过、0 失败。Release DLL 的 `dumpbin /headers` 显示 `14C machine (x86)`、`PE32`；`/exports` 恰好列出上述 7 个未修饰导出名称。产物为 `build/Debug/cz_help_mm.dll` 和 `build/Release/cz_help_mm.dll`。

边界测试使用真实上游 SDK 类型构造隔离 fixtures，检查敌我过滤、实际小数 HP、相机 / punch / FOV、身份与时间戳、死亡 / 旁观 / 第二真人 / dedicated / sv_lan / 远程地址禁用、空闲 edict、未知模型 / 队伍、NaN、退化和 crouch hull、TeamInfo、地图清空、SDK / Metamod 版本协商以及旧 engine 表尾哨兵。fixtures 没有读取实际游戏。

## 仍需实机确认

- 由根代理在已授权游戏中确认 Metamod 加载、`TeamInfo` ID、Steam CZ BOT 标志及 host slot，并使用 `cz_help_status`、`cz_help_dump`、覆盖程序 `--probe PID` 对照。
- 开镜、蹲下、受伤和切图的数据更新需要实际对局验证。
- 服务端相机没有客户端插值、head bob、预测运动和局部视觉效果；快速移动与后坐力可能存在瞬态像素偏差。不能将 fixture 的投影坐标称为像素级游戏匹配。
- 模型回退限定原版模型；自定义模型在缺少 TeamInfo 时被忽略。共享映射发布失败时显示端必须执行协议的超时隐藏。

本子任务未安装、修改或启动任何游戏目录。
