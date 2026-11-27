# MWEAI 钣金 DFM

从 MWEAI 第二行进入「钣金 DFM」，点击「开始分析」读取当前 Creo 零件或装配。左侧按零件和工序筛选，选中结果查看量测、判定依据及建议。定位装配结果时高亮首个匹配实例。

## V0.2 公开工艺基准

| 规则 | 默认参考 | 测量口径 |
| --- | --- | --- |
| C01 | 1×板厚 | 可识别的圆形直通孔直径。槽和异形孔另核。 |
| B01 | 常用刀具 R0.762 mm | 原生折弯内半径。小于参考值提示风险，具体小半径刀具可能可用。 |
| B02 | 4×板厚 | 相切壁最小直段包络的预筛参考。实际模具按连续支承和工厂测量口径复核。 |
| B03 | 4×板厚 | 插件可编辑的预筛值，用于同壁可配对圆孔边到折弯切线。本测量口径由插件定义。 |
| A01 | 0 对 | Creo 当前装配精确实体干涉，按涉及的钣金零件归集。 |
| J01 | PEM CLS-M4-1 示例 | 推荐最小板厚 1 mm、板孔 Ø5.41 +0.08/0 mm、孔心距边 ≥6.9 mm。只有确认该料号与基材后才能使用。 |
| F01 | 按实际粉末体系 | 用产品技术数据表与成品图、实测膜厚复核配合。不同涂层体系不能共用统一膜厚。 |

公开基准用于预筛，项目的工艺放行仍由设备、模具、材料及成品图决定。M02 通过仅代表名义板厚读取成功。M04 仅读现有展平特征，展开轮廓校验仍需展开或 DXF。本页不自动修改或展开模型。J01、F01、A03 缺少项目专属输入时标记待确认。

## 配置与验证

- 修改 MWEAI 数据目录内的 DFM-screening.ini 后，点击「开始分析」重新读取。缺失项使用版本自带的 resources/dfm/screening.ini。所有数字需为正数。
- 「导出报告」将结果存为 UTF-8 CSV，目录为 E 盘上的 Creo 当前工作目录。
- 可用安装管理回退至 .27。保存手头工作后，重开 Creo。
- 来源：[Protolabs 钣金设计指南](https://www.protolabs.com/services/sheet-metal-fabrication/design-guidelines/)、[Protolabs 成形与制造指南](https://www.protolabs.com/media/roognect/sheet-metal-tolerances-one-pager-2.pdf)、[PEM CLS-M4-1](https://catalog.pemnet.com/item/nuts-types-s-ss-cls-clss-cla-sp/self-clinching-nuts-types-s-ss-cls-clss-sp-metric/cls-m4-1)、[AkzoNobel 粉末体系](https://www.interpon.com/us/en/powder-coatings-for-data-centers)。
结果表的状态格使用与页首统计色块相同的配色。行选中后保留状态格底色，其余列继续显示选中状态。可通过安装管理回退至 .29。
