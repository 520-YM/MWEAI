# MWEAI 本机品牌升级

本版沿用 0.23.0-local.connectionstartup.1 的功能，将 Creo 的选项卡、命令与聊天窗口、安装管理和开始菜单入口统一显示为 MWEAI。升级版本为 `0.23.0-local.mweai.1`。已启动的 Creo 会继续使用它加载的版本，正常退出并再次启动后显示新名称。

## 数据位置

本机主安装沿用 `E:\BWTL-Data\AppData\Local\BWTL`，多会话实例沿用相邻的 `BWTL-Instances`。原有安装路径、DLL 文件名、会话协议、学习文件扩展名和历史文件名继续可用，以便升级前后的快捷方式、模型参照及各自的聊天和规则能接着使用。

## 核对

正常打开桌面的 Creo，从功能区进入 **MWEAI**。聊天标题应显示 MWEAI。已有业务资料、聊天归档及方法库仍可从原入口打开。

## 回退

各实例的 `active.json` 记录上一个版本及该实例在 E 盘的备份目录。可以从开始菜单的 MWEAI 安装管理里回退主实例。多实例管理员可在需要回退的每个安装目录用 PowerShell 执行 `& .\Install-BWTL.ps1 -InstallRoot (Get-Location).Path -Rollback`。正在运行的 Creo 等正常退出后再启动，使用回退版本。回退不需要复写聊天、规则或公司资料。
