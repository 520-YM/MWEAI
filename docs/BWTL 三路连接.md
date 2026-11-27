# MWEAI 三路连接

打开 Creo 并等待 MWEAI 加载。可以在 Codex 说「操作 Creo 会话 2」或「打开当前总装里的同号钣金件」。Codex 先核对进程及 E 盘工作目录，再按任务选择在 Creo 中执行的连接入口。后台使用的三个入口都归 MWEAI 管理。

## 随 Creo 启动

通过电脑上已经配置 MWEAI 的 Creo 入口启动时，插件加载后会立即建立原生和常驻管道，并在后台连接该进程的 VB API。空白 Creo 也会进行这一步。每次启动按 Creo 进程 ID 和启动时间确认连接，实例之间的读取、操作和状态仍各自绑定。可用 `Status` 回读该进程的 `comStartup`。`starting` 表示正在连接，`connected` 表示当前绑定了这个 Creo 进程，`ready` 表示已完成首次连接验证且可自动切回，`unavailable` 表示连接未就绪，`disabled` 表示自动连接已关闭。连接尚处于准备状态时，原生入口继续可用。

MWEAI 从 Creo 安装路径取本次需要的 PTC 通信程序，将 HOME、HOMEDRIVE、HOMEPATH 对齐到该 MWEAI 会话的 E 盘隔离目录。这些环境变量随本次 Creo 启动传给子进程。出错的具体阶段可在当前 MWEAI 会话状态及 E 盘连接运行目录核对。[PTC VB API 通信设置](https://support.ptc.com/help/creo_toolkit/vbapi_pma/r2027/usascii/creo_toolkit/user_guide/Getting_Started.html)

三条通路随同一个 Creo 启动。多个 Creo 在同一个 Windows 登录会话中共用 PTC 的 COM 服务。MWEAI 依次验证新 Creo 的 VB API 连接，每次切换前重新绑定所选进程并回读模型、窗口和工作目录。每个 Creo 的常驻消息、实时事件和原生建模通路各自运行。[PTC VB API 会话连接](https://support.ptc.com/help/creo_toolkit/vbapi_pma/r11.0/usascii/creo_toolkit/api/dita/t-pfcAsyncConnection-AsyncConnection.html)

| 通路 | 用途 |
| --- | --- |
| 常驻 Toolkit 通路 | Creo 内的 MWEAI 驱动持续应答，在同一管道中处理多次会话查询。在独立事件管道推送模型变更和实际执行回执。 |
| Creo VB API 通路 | 从已选 Creo 进程读取原生连接 ID，用 PTC 的 `ConnectById()` 核对模型与窗口。要打开或激活零件及装配时，先在 Toolkit 端校验目标，再由 VB API 执行并回读。 |
| 现有 MWEAI 原生控制 | 真实零件和装配的建模、修改、再生及保存仍走现有 MWEAI 原生执行队列。每次写操作使用当前 Creo 状态返回的绑定。 |

三路共用同一进程 ID、启动时间、窗口、模型及工作目录。按完整 `名称.PRT` 或 `名称.ASM` 切换可区分同名零件和装配。按精确文件名打开以当前 E 盘项目为范围。COM 窗口交接期间，MWEAI 暂占执行队列，完成时再由 Toolkit 读取目标。核对成功后取得新的绑定，继续进行建模或装配。超时或回执中断时请用 `Status` 核对活动目标与模型状态，再决定是否继续。

## 桌面 Codex 本机调用

```powershell
$install = 'E:\BWTL-Data\AppData\Local\BWTL'
$active = Get-Content -LiteralPath "$install\active.json" -Raw | ConvertFrom-Json
$bwtl = Join-Path $active.Directory 'scripts\Invoke-BWTL.ps1'
& $bwtl -Action List
$connected = & $bwtl -Action Connect -Session 2
& $bwtl -Action Navigate -Session 2 -ExpectedContext $connected.Data.context -Command 'ACTIVATE M26A1481001V00.PRT'
$current = & $bwtl -Action Status -Session 2
& $bwtl -Action Read -Session 2 -Command MODEL
& $bwtl -Action Watch -Session 2 -WatchSeconds 6 -Json
```

`Navigate` 也接受 `OPEN example.prt.1` 或 `OPEN example.asm.4`。读取工作目录、模型和窗口以 `Status` 为准。`Navigate` 回执中的 `route=COM` 表明最终活动目标同时通过了原生状态核对。`Watch` 支持用 `AfterSequence` 从上次读到的事件继续。涉及保存时调用 `Save`，按职责交给原生保存保护。

Creo 10 的 VB API 需要安装中自带 `pfclscom.exe` 和 `pro_comm_msg.exe`。MWEAI 从已选 Creo 的本机安装识别这两个组件。调用进程的工作目录、日志及临时运行数据留在该 MWEAI 会话的 E 盘目录。若 COM 通路报错，先通过 `Connect` 核对原因与会话身份。对已确认的 Creo 模型可直接使用原有 `Status`、`Read` 和原生入口。

Creo 的 VB API 窗口方法可能忽略文件版本参数，因此 `OPEN` 由 Toolkit 先读取工作目录内的精确版本、核对文件来源，再交给 COM 激活该模型。MWEAI 最后复核窗口和文件来源。[PTC 进程连接 ID](https://support.ptc.com/help/creo_toolkit/vbapi_pma/r2027/usascii/creo_toolkit/user_guide/Connecting_Via_Connection_ID.html) · [PTC 模型读取](https://support.ptc.com/help/creo_toolkit/vbapi_pma/r13/usascii/creo_toolkit/user_guide/Retrieving_Models.html)

若当前用户尚无 Creo 10 VB API 类型库，MWEAI 先检查用户和机器注册，再按需要在当前用户的 `Software\Classes\TypeLib` 和 `Software\Classes\Interface` 临时注册 PTC 类型库 `{176453F2-6934-4304-8C9D-126D98C1700E}` 及其接口。最后一个使用 MWEAI 自动连接的 Creo 进程退出后，MWEAI 关闭自己启动的 COM 服务，并撤销本次的临时注册。有同一版本的有效注册时直接复用；发现其他来源或正在运行的 PTC COM 服务时报告原因。[Microsoft 用户级类型库注册接口](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-registertypelibforuser)

## 验收与回退

自动启动版 `0.23.0-local.connectionstartup.1` 使用同一个 MWEAI 启动脚本在两个隔离 Creo 10 进程验收。两个空白会话在收到第一条操作前都已验证 VB API。分别打开 E 盘工作目录的相同图号零件后，回读的进程、窗口和目录符合各自绑定。MWEAI 的模型清单和实时推送都通过，关闭当前绑定的 Creo 后，另一进程恢复 COM 连接。测试件的详细模型快照返回 Creo -8，模型清单正常。12 组离线回归通过。

本机验收使用 `0.23.0-local.connectionfusion.1` 的两个独立 Creo 10 进程及分离的 E 盘工作目录。会话 1 经 VB API 打开、切换及返回装配，MWEAI 实测创建 2 mm 钣金板及四个 Ø4.2 通孔，回读孔距 50 × 50 mm。常驻通路在一根连接上连续应答，并推送了第四个孔的原生回执。Windows PowerShell 5.1 的 `Watch` 从指定事件序号续接了切回装配的回执。旧绑定和交接期间的写入均被拒绝。会话 2 的活动窗口、模型版本和磁盘文件在会话 1 操作前后校验一致。临时注册和 COM 服务在验收末尾已核对清理。原生实测状态可在发布回执核对。

可以用 MWEAI 管理程序回退到 `0.23.0-local.connectionfusion.1`。先保存正在编辑的工作，再正常关闭并重新打开 Creo 以加载相应版本。E 盘聊天及项目数据沿用原目录。
