# MWEAI 实时任务接续

MWEAI 用自己的 Creo TOOLKIT 接口执行。Codex 的本机客户端与插件共用一个任务记录，执行结果来自同一个 Creo 进程。插件聊天仍由该进程的常驻 Codex 会话管理。现有桌面聊天在收到下一条指令时可读最新任务，也可在本机监看器中查看实时事件。桌面聊天本身的自动事件接入要单独验证。

每个 Creo 进程按编号、进程 ID 和启动时间绑定。打开、切换、重建、交接都要提交这次读取的 context。只在已核对的 E 盘当前项目目录打开指定 PRT/ASM 文件。同名装配和零件用扩展名区分。切换回执在 Creo 确认窗口后产生。重建调用原特征树重放，把验收通过的结果放在项目的独立新目录。

## 在同一个 Creo 会话验证

先确认正在运行的 Creo 已加载本版本，切到源件所在的 E 盘工作目录。示例用会话 1 和已授权的公司零件。

> **回溯风险提醒**　边法兰在附加壁不完整或失败回滚释放草绘时曾触发 Creo 回溯。前版在独立 Creo 10 验证过连续五道单边凸缘及一次尺寸试改。同一原件的本次验证仍须看实际回执。

~~~powershell
$active = Get-Content -LiteralPath 'E:/BWTL-Data/AppData/Local/BWTL/active.json' -Raw | ConvertFrom-Json
$bwtl = Join-Path $active.Directory 'scripts/Invoke-BWTL.ps1'
& $bwtl -Action List
$session = 1
$status = & $bwtl -Action Status -Session $session
& $bwtl -Action ListModels -Session $session -ExpectedContext $status.Data.context
& $bwtl -Action Handoff -Session $session -ExpectedContext $status.Data.context -Command '核对 M26G2081024.PRT 后，在本会话从空白模板按原特征树重建'
& $bwtl -Action OpenModel -Session $session -ExpectedContext $status.Data.context -Command 'm26g2081024.prt.3'
$status = & $bwtl -Action Status -Session $session
& $bwtl -Action Read -Session $session -ExpectedContext $status.Data.context -Command MODEL
& $bwtl -Action TaskState -Session $session
~~~

此时可在插件里继续发指令，例如「读取刚才打开的压板，列出特征顺序」。回到 Codex 时再读一次 TaskState，核对本机收到的用户消息、当前模型和回执。如需运行原生重建，再读取最新 context 后提交 RebuildModel，保留重建的新文件路径和实际验收回执。

另一个本机客户端可运行 Watch 观察同一会话的事件。使用上次已读的序号续看。省略序号时从日志已有记录开始读取。只看后续事件时，先从 TaskState 取得 LatestSequence。长任务开始前可先启动 Watch，以便读取每一步的回执。Watch 在当前 Creo 会话退出时结束。TaskState 只返回该会话中最新模型对应项目的接续记录。重启后旧事件仍在各自的 E 盘聊天归档中，先核对这次新会话的实际模型后再续做。

~~~powershell
& $bwtl -Action Watch -Session $session -AfterSequence 0 -WatchSeconds 45 -Json
~~~

也可用 SwitchModel 按完整 PRT/ASM 名称或 WINDOW 加窗口 ID 切到已加载的模型。当前模型发生变化后须重新读 Status。输出切换成功与源文件需要修复参照等失败情况均以 Creo 回执为准。每次只给同一个进程提交一条写操作，收到回执再继续。进度读取采用本机事件记录，无需另起 MCP 或触发 AI 推理。

## Creo 10 验收结果

在单独的 E 盘验收环境加载本发行版 DLL。确认从空白窗口原生打开 M26G2081024.PRT.3、同名 ASM 和 PRT 精确切换、插件回答刚才的图号，以及本机工具连续读取聊天、模型切换、每步重建和最终回执。验收核对了源文件哈希，Creo 未出现回溯。

用 120 × 80 × 1.5 mm 钣金板完成原生草绘和首壁重建、几何核对、保存重开。公司零件 M26G2081024.PRT 的第二道凸缘仍待修复。首壁和第一道凸缘完成后，源特征 1214 引用的边 1184 在重建目标中找不到。补载上游装配后复测，结果相同。这个工件的重建结果仅保留在隔离会话中用于检查。

本次验证了 MWEAI 与 Codex 本机客户端的实时任务接续。自动推送到已打开的 Codex 桌面聊天尚未验收。

## 本机更新与回退

主安装保留正在使用的版本和上一版本备份。候选版先在 E 盘通过构建、离线原生回归及安装清单校验。原生窗口回读、精确重建及插件聊天接续须在受控 Creo 会话实测。运行中的 Creo 继续使用已加载版本，正常退出后重新打开才会使用已安装的新 DLL。

安装时使用 MWEAI 自带安装器并保留现有数据。回退用管理程序的「回退上一版」，或者以同一 E 盘安装根运行安装器的 Rollback。正常重开后核对版本、实际 PID 和隔离日志。会话事件可与原有聊天和公司记忆一起按本机数据策略保留。
