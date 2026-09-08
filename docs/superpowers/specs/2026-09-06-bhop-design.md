# 1.2.0 自动连跳设计

用户已明确要求“取消存点和回点，仅有连跳功能，即按住空格键自动跳”，并要求继续。按此确认范围实施。

- 新增“自动连跳（按住空格）”复选框及独立 F3 开关，启动默认关闭。快捷键可通过 INI 修改。
- 开启后按住空格，落地连续起跳；松开停止。沿用正常跳跃物理，不修改速度、重力或跳跃高度。
- F8/F9/F11、全屏方框和变色 HP 保持原用途。不包含存回点、自动加速、跳蹲、边缘跳、速度/跳距统计和计时。
- 沿用 Metamod，在服务端 PM_Move 前为有效本地落地玩家清除旧跳跃锁存位；保持当前输入、其他按钮和速度不变。
- 前置钩子使用当前 `flags & FL_ONGROUND` 判断落地，水位读取当前 host 的 `entvars.waterlevel`。`playermove_t.onground/waterlevel` 属于游戏移动函数内计算的结果，可能残留上一名 BOT 的值，不用于前置判断。
- 仅本地单人会话、软件开启、空格按下、游戏跳跃命令有效时处理。水中、梯子、旁观、死亡、冻结、火车和水跳不处理。
- 独立 BhopControl/BhopStatus 本机通道携带 PID/版本/时间戳，150ms 过期停止；不改变原 ESP 协议。失去前台或退出软件发布关闭。
- 先测试重置锁存及所有停止条件，再验证真实通道和插件边界；保留现有全部回归，构建 Debug/Release，打包 1.2.0。
- 游戏退出后才能更新插件。服务器修正可能受客户端预测影响；未实际验证时不声称已验证长按跳跃体验。

接口核查参考：[Valve SDK PM_Jump / PM_PlayerMove](https://raw.githubusercontent.com/ValveSoftware/halflife/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c)、[ReHLDS SV_RunCmd](https://raw.githubusercontent.com/rehlds/ReHLDS/master/rehlds/engine/sv_user.cpp)。后者用于检查调用前字段来源，不当作当前 Steam 二进制实现的实测证明。
