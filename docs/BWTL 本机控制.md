# MWEAI 本机控制

在 Codex 聊天中说明要控制的 Creo 会话和操作。Codex 通过 MWEAI 自带的 PowerShell 客户端连接该 Creo 进程，由已有的 MWEAI 原生工具执行。每个 Creo 进程使用独立的 MWEAI 数据目录和命名管道，连接须属于同一个 Windows 用户和登录会话。

实时任务交接、按图号打开模型和事件监看见《MWEAI 实时任务接续》。

《MWEAI 三路连接》说明 `Connect`、`Navigate` 和实时 `Watch`。在同一会话，可用 `Navigate` 经 Creo VB API 切到指定零件或装配，再把回读的 `Data.context` 交给 `Execute` 执行 MWEAI 原生操作。

## 用法

从普通 Creo 入口启动并等 MWEAI 加载。打开多个进程时，先列出会话，再按编号选择。会话重新启动后应重新读取状态。确认模型名称、类型、窗口和 E 盘工作目录后，再使用本轮的绑定执行写入。

```powershell
$install = 'E:/BWTL-Data/AppData/Local/BWTL'
$active = Get-Content -LiteralPath "$install/active.json" -Raw | ConvertFrom-Json
$bwtl = Join-Path $active.Directory 'scripts/Invoke-BWTL.ps1'
& $bwtl -Action List
& $bwtl -Action Status -Session 2
& $bwtl -Action Read -Session 2 -Command MODEL
```

功能入口包括模型摘要、模型树、基准与草绘参照、孔和钣金信息、装配树及组件定位信息。首批可写指令为 `DIMS`、`REGEN`、`EXTRUDE`、`SKETCH_CREATE`、`SOLID_EXTRUDE`、`HOLE`、`SMT_SKETCH`、`SMT_FIRST_WALL`、`SMT_CUT`、`SMT_FLAT4` 和 `ASM_EDIT_OFFSET`。保存使用单独的 `-Action Save`。执行和保存都须传入最新 `-ExpectedContext`。原生结果返回后重新读取尺寸或模型树，再确认完成状态。

下例只用于已核对模型名、特征号、尺寸符号和原值的测试件。替换示例参数前，应先读目标模型，完成后依据保存要求决定是否写盘。

```powershell
$status = & $bwtl -Action Status -Session 2
$command = "CREO_OP DIMS $($status.Data.model) 115 d13 8 10"
& $bwtl -Action Execute -Session 2 -ExpectedContext $status.Data.context -Command $command
& $bwtl -Action Read -Session 2 -Command MODEL
# 需要保存时重新获取绑定，再执行单独的保存命令。
$status = & $bwtl -Action Status -Session 2
& $bwtl -Action Save -Session 2 -ExpectedContext $status.Data.context
```

同一 Creo 进程的原生指令顺序执行。进程、活动窗口、模型版本或工作目录变更后，旧绑定失效。两个 MWEAI 会话保存同一文件时，落后于磁盘版本的会话会收到保存冲突，其内存修改保留。保护范围是 MWEAI 的保存入口，直接使用 Creo 自带保存时由 Creo 处理。未返回结果的写入先读回模型状态，再决定下一步。

首批接入范围以上述入口为准。MWEAI 菜单内的专用操作仍从插件界面执行。

## 按图号建零件和装配压铆件

- 说「在 Creo 会话 2 创建零件，图号 M26A0181001V03」。本机客户端使用 `CreatePair`，按既有 MWEAI 流程创建同图号同版本的 ASM 和空钣金 PRT，用 Creo 默认方式装入，再打开并核对子件窗口。已有同图号时保留原件。需要保存时另行指定。
- 说「在 Creo 会话 2，给这块板的两个安装孔装 BSO M3×8」。从正在核对的 ASM 和子件几何找实际组件路径、安装面和孔壁，按公司库的实际规格装入。已知组件、安装面和孔壁命令可直接提交 `PressFit`。加 `PREVIEW` 可预查同一组参照和适配性。回执提供已核对的组件与定位结果。参照不唯一时需先查清安装侧和孔位。

两套流程都需要当前会话的新鲜 `-ExpectedContext`。下列图号及 ID 为参数示例，装配前从当前模型回读实际参照。

```powershell
$status = & $bwtl -Action Status -Session 2
& $bwtl -Action CreatePair -Session 2 -ExpectedContext $status.Data.context -Command 'M26A0181001V03'
# 激活目标装配、读取安装参照，再以本次装配的状态提交。
$status = & $bwtl -Action Status -Session 2
& $bwtl -Action PressFit -Session 2 -ExpectedContext $status.Data.context -Command 'PREVIEW BSOM3X8 ID:42 SURFACE:159 SURFACE:488'
```

MWEAI 装配压铆件时把已核对的库件副本放入项目目录，并记下实际磁盘版本。随后保存总装时会校验此副本，副本变化或出现更高版本时仍报告冲突。

## 一条指令创建钣金件并装配压铆件

可以在 MWEAI 聊天直接发送「按图号 M26A9781001V00 建一个 80×60×1.5 的钣金件，中心打 Ø5.4 通孔，装一颗 BSO M3×8，保存到当前项目。」Codex 本机入口使用相同句子。图号包含两位版本号。孔位也可写成「在 X=30、Y=25 处打 Ø5.4 通孔」，坐标从矩形平板左下角起算，长度单位为毫米。

```powershell
$status = & $bwtl -Action Status -Session 2
& $bwtl -Action BuildPanel -Session 2 -ExpectedContext $status.Data.context -Command '按图号 M26A9781002V00 建一个 100×70×1.5 的钣金件，在 X=30、Y=25 处打 Ø5.4 通孔，装一颗 BSO M3×6，保存到当前项目。'
```

流程依次建立同图号总装和空钣金件、建首壁和单个贯穿圆孔、回到原总装、核对实际孔壁和安装面，再从库中匹配具体 BSO 规格。默认从首壁草绘侧安装。试装并读回同轴、贴合、再生和间隙后给出一条结果。明确要求保存时，通过 MWEAI 的版本校验保存总装及其子件。省略保存指示时，总装和钣金修改保留在 Creo 会话中，匹配的库件副本按装配流程放在当前 E 盘项目。

应用于矩形平板、单个通孔和明确的 BSO 规格。工作目录使用当前 Creo 的 E 盘项目。在图号已存在或无法核对实际安装参照时，回复卡点并保留已完成部分。试验样件使用的孔径和板厚由实际验收几何确定，生产尺寸按公司工艺资料执行。

## 用一条指令建六孔板并分组装配

在 MWEAI 聊天发送下面这句话，并使用项目实际分配的图号和版本。坐标从钣金板左下角起算。四角两方向的边距是孔中心到对应板边的距离。中间两孔沿板长居中，孔距为两孔中心距。

> 按图号 M26A9781003V00 建一块 120×80×1.5 的钣金板，打6个 Ø5.4 通孔，四角横向边距15、纵向边距15，装 BSO M3×8，中间两孔孔距40，装 BSO M3×6，保存到当前项目。

MWEAI 读回六个孔及安装面，两组先预核对，再按实际孔位装配。分组指令和实际组件关系经核对后写入 E 盘的 MWEAI 项目记录。当前 Creo 装配、项目目录、钣金件、孔和组件都能对应时，可以继续发送「中间两个改成8长，保存到当前项目。」。MWEAI 会核对并保留四角的组件 ID、规格和位置。修改和保存按实际原生回执完成，已完成组在失败时保留，具体卡点会出现在同一个结果中。

**历史 Creo 回溯风险**　连续替换并重定义旧组件曾触发 Creo 回溯。本流程按已核实的两个孔和旧组件分组，检查依赖，使用压铆替换入口及恢复副本，再核对新旧组件。使用样件的孔径和板厚时，生产适配应以公司工艺资料为准。

Codex 本机客户端把第一句交给 `BuildPanel`，把补充句交给 `RevisePanel`。每次写入前以 `Status` 重新核对 Creo 会话和模型。正常重开总装后也会重新核对实际引用关系，再使用对应的 E 盘记录。

## 按孔位给压铆件分组并更换规格

在 Creo 打开含有一件可识别顶层钣金件的装配，工作目录选 E 盘项目。在 MWEAI 发送「查看可装配孔位」，就能查看圆形通孔的编号、实际坐标、孔径和安装面。编号按零件坐标 Y、X、Z 从小到大。安装侧按孔轴的规范方向选取低端，由实际通孔轮廓和贴合面核对。

> 按孔位分组装配：左组 1、4、6号孔 装 BSO M3×6；右组 2、3、5号孔 装 BSO M3×8，保存到当前项目。

可以自定义组名，一件板最多 8 组，每组最多 12 孔。一个孔只能归一组，未指定的孔不装。规格必须能对应到公司库的具体实例，并与目标孔和安装面匹配。本功能适用于现有钣金件上的原生圆形通孔，孔距和分布可不同。每个孔单独核对实际安装面。

> 左组改成8长，保存到当前项目。

也可以说「把左组改成 BSO M3×8」。按组改动前会从 E 盘重新读取该装配和板件的分组记录，核对原有组件及其实际定位。其余组的组件 ID、位置和约束在更换后再次复核。模型仅在明确要求时保存。若装配中途失败，保留已经做成的部分，并用原分组指令重新核对后续做。重开已保存的装配可以继续调用该 E 盘记录。

**历史 Creo 回溯风险**　连续替换并重定义旧组件曾触发 Creo 回溯。当日常工作需改组时，MWEAI 会用恢复副本、实际依赖检查和原生读回来保护装配。

Codex 本机入口依次使用 ListPressfitHoles、GroupPressfit 和 RevisePressfitGroup。三个入口的会话绑定都用 Status 获得，写入操作在提交前重新读取 Status。参照核对与保存均走 MWEAI 原生接口。

回退整包时先保存并退出对应的 Creo 会话，再在 MWEAI 管理功能中恢复 0.23.0-local.multipressfit.1。各装配的分组记录位于对应 MWEAI 数据目录的压铆任务文件夹。

## 验证与回退

独立 Creo 10 双进程验收覆盖真实模型读回、改孔、并发改尺寸以及 Codex 和插件两条 MWEAI 保存入口的旧版本拒绝。测试还核对了空窗口、装配树读取、工作目录变更和失效绑定的处理。MWEAI 各进程在 E 盘各自存放聊天和执行回执。

可在对应会话的 MWEAI 设置中将 `[LocalControl]` 下的 `Enabled` 设为 `0`，正常重启该 Creo 会话后关闭本机控制入口。整包回退时，先保存工作并退出 Creo，再在 MWEAI 管理功能中恢复 `0.23.0-local.panelworkflow.1`。

当前版本新增多板件、按孔径和截图选孔、补漏、工艺、PDF 核对及交付。用法见 [MWEAI 压铆工作流程](BWTL%20压铆工作流程.md)。
