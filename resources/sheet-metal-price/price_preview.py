"""MWEAI read-only adapter for the portable estimator."""
import csv
import math
import sys
from pathlib import Path

from compute_cost import compute_cost, format_chat_report


def spreadsheet_text(value):
    return "'" + value if value.lstrip().startswith(("=", "+", "-", "@")) else value


def write_export(path, cost, dimensions):
    width, depth, height, _ = dimensions
    rows = [
        ("订单名称", spreadsheet_text(cost["product_name"]), ""),
        ("结构负责人", spreadsheet_text(cost["structure_lead"]), ""),
        ("宽 W", width, "mm"),
        ("深 D", depth, "mm"),
        ("高 H", height, "mm"),
        ("材料牌号", cost["material"], ""),
        ("数量", cost["qty"], "台"),
        ("净重", cost["weight_kg"], "kg"),
        ("计费重量", cost["effective_weight"], "kg"),
        ("材料单价", cost["unit_price"], "元/kg"),
        ("材料费", cost["material_cost"], "元/台"),
        ("计价表面积", cost["surface_area_sqm"], "m²"),
        ("估算下料面积", cost["sheet_area_sqm"], "m²"),
        ("下料估算工时", cost["laser_hours"], "h"),
        ("下料费", cost["laser_cost"], "元/台"),
        ("估算折弯道数", cost["bend_count"], "道"),
        ("折弯等效工时", cost["bend_hours"], "h"),
        ("折弯费", cost["bend_cost"], "元/台"),
        ("压铆等效工时", cost["rivet_hours"], "h"),
        ("压铆费", cost["rivet_cost"], "元/台"),
        ("焊接等效工时", cost["weld_hours"], "h"),
        ("焊接费", cost["weld_cost"], "元/台"),
        ("打磨等效工时", cost["grind_hours"], "h"),
        ("打磨费", cost["grind_cost"], "元/台"),
        ("加工费小计", cost["process_total"], "元/台"),
        ("计入喷涂", "是" if cost["with_spray"] else "否", ""),
        ("计费喷涂面积", cost["spray_area_sqm"] if cost["with_spray"] else 0, "m²"),
        ("喷涂费", cost["spray_cost"], "元/台"),
        ("单台估算价", cost["unit_total"], "元/台"),
        ("订单估算总价", cost["order_total"], "元"),
        ("估算口径", "内部成本估算", ""),
    ]
    with Path(path).open("w", encoding="utf-8-sig", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("项目", "数值", "单位"))
        writer.writerows(rows)


def main(input_path, result_path, export_path):
    try:
        name, lead, w, d, h, weight, material, quantity, spray = Path(input_path).read_text(
            encoding="utf-16"
        ).splitlines()
        if not lead.strip():
            raise ValueError("请填写结构负责人。")
        dimension_weight = tuple(float(v) for v in (w, d, h, weight))
        if not all(math.isfinite(v) and v > 0 for v in dimension_weight):
            raise ValueError("长宽高及净重必须是有限的正数。")
        if not quantity.isascii() or not quantity.isdecimal():
            raise ValueError("数量必须是正整数。")
        cost = compute_cost(name.strip(), *dimension_weight, material, int(quantity), spray == "1")
        cost["structure_lead"] = lead.strip()
        write_export(export_path, cost, dimension_weight)
        lines = format_chat_report(cost)
        for symbol in ("📐 ", "💰 ", "🎨 ", "💵 "):
            lines = lines.replace(symbol, "")
        lines = lines.replace("━", "─")
        charge_note = "喷涂按箱体面积公式及屏幕尺寸加量。" if cost["with_spray"] else "本单不计喷涂费用。"
        result = f"内部成本估算（人民币）\n{lines.strip()}\n\n{charge_note}\n加工工时为估算字段。"
    except (ValueError, OverflowError) as exc:
        result = f"输入未通过校验\n{exc}"
    Path(result_path).write_bytes(result.encode("utf-16"))


if __name__ == "__main__":
    main(*sys.argv[1:])
