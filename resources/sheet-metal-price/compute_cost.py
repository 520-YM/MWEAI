#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""钣金外壳成本计算：玛威尔内部单价与当前计价规则。"""
import re
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

# 材料单价表（与《玛威尔结构工程师钣金成本估算.xlsx》一致）
MATERIAL_PRICES = {
    "SUS304": 16, "SUS316": 24, "SUS201": 9,
    "SPCC": 6,    "SECC": 6.5,   "SGCC": 7,
    "AL5052": 30, "AL6061": 28,
}  # 元/千克（与《玛威尔结构工程师钣金成本估算.xlsx》同步，最后更新 2026-06-18）

# 工时单价（与《玛威尔结构工程师钣金成本估算.xlsx》同步，2026-08-17 更新）
LASER_RATE = 220    # 下料 元/小时
BEND_RATE  = 144    # 折弯 元/小时（原 120 上调 20%）
RIVET_RATE = 72     # 压铆 元/小时（原 60 上调 20%）
WELD_RATE  = 80     # 焊接 元/小时（原焊接打磨 70 拆分）
GRIND_RATE = 60     # 打磨 元/小时（新增）
SPRAY_RATE = 30     # 喷涂 元/平方米（2026-08-13 从 55 调回 30）

# 综合计价基准与拆分规则
OLD_BEND_RATE  = 130  # 原折弯压铆综合单价，作为 6:4 分配基准
OLD_GRIND_RATE = 70   # 原焊接打磨综合单价，焊接/打磨合计保持不变
BEND_SPLIT = 0.60
RIVET_SPLIT = 0.40
BEND_PRICE_MULTIPLIER = 1.20   # 折弯价格上调 20%
RIVET_PRICE_MULTIPLIER = 1.20  # 压铆价格上调 20%
GRIND_MULTIPLIER = 2.5  # 焊接打磨查表工时倍率（查表工时 × 2.5，如 2h → 5h）
WELD_SPLIT = 0.48  # 焊接:打磨 = 4.8:5.2（打磨占 52%）
GRIND_SPLIT = 0.52

# 材料损耗率（损耗量占投入量的比例，如净重80kg → 投入量100kg，损耗20kg）
MATERIAL_LOSS_RATE = 0.20

# 经验系数
SHEET_EXPANSION = 0.65   # 展开面积系数（2026-08-13 调回 0.65）
LASER_CUT_RATIO = 2/3   # 激光下料仅算展开面积的 2/3
LASER_TIME_PER_SQM = 0.3
BEND_TIME_PER_FOLD_MIN = 3

# 最低工时门槛（防止小件计费过低）
MIN_LASER_HOURS = 0.4
MIN_BEND_HOURS = 0.5


def normalize_material(material: str) -> str:
    """统一材料牌号；允许 s.e.c.c、secc 等常见写法。"""
    return re.sub(r"[^A-Z0-9]", "", str(material).upper())


def estimate_bend_count(H_mm: float) -> int:
    if H_mm < 500:    return 7
    elif H_mm < 1000: return 12
    elif H_mm < 1500: return 18
    elif H_mm < 2200: return 25
    else:             return 30


def estimate_grind_hours(weight_kg: float) -> float:
    if weight_kg < 30:  return 0.8
    elif weight_kg < 50: return 1.0
    elif weight_kg < 80: return 1.2
    elif weight_kg < 100: return 1.5
    elif weight_kg < 150: return 2.0
    else:                return 2.5


def compute_cost(
    product_name: str,
    W_mm: float, D_mm: float, H_mm: float,
    weight_kg: float, material: str = "SGCC",
    qty: int = 1,
    with_spray: bool = True,
) -> dict:
    """计算钣金外壳成本 — 折弯/压铆/焊接/打磨 已独立拆分

    外表面积统一 = 6面箱体公式 × 3（2026-08-13 更新9：不再使用 STP 表面积计价，
    STP 仅用于读取尺寸/重量参考）。
    """
    if not str(product_name).strip():
        raise ValueError("订单名称不能为空")
    dimensions = {"W": W_mm, "D": D_mm, "H": H_mm}
    for label, value in dimensions.items():
        if value is None or float(value) <= 0:
            raise ValueError(f"尺寸 {label} 必须为正数，当前值: {value}")
    if weight_kg is None or float(weight_kg) <= 0:
        raise ValueError(f"净重必须为正数，当前值: {weight_kg}")
    if isinstance(qty, bool) or int(qty) != qty or int(qty) <= 0:
        raise ValueError(f"数量必须为正整数，当前值: {qty}")

    W_mm, D_mm, H_mm = float(W_mm), float(D_mm), float(H_mm)
    weight_kg = float(weight_kg)
    qty = int(qty)
    material = normalize_material(material)
    if material not in MATERIAL_PRICES:
        raise ValueError(f"未知材料: {material}，可选: {list(MATERIAL_PRICES.keys())}")

    unit_price = MATERIAL_PRICES[material]
    effective_weight = weight_kg / (1 - MATERIAL_LOSS_RATE)  # 含损耗的计费重量
    material_cost = effective_weight * unit_price

    W, D, H = W_mm/1000, D_mm/1000, H_mm/1000
    # 外表面积统一 = 6面箱体公式 × 3（更新9：STP 表面积不再参与计价）
    surface_area = 6*(W*H + D*H + W*D)   # = 2×(W*H+D*H+W*D) × 3
    surface_source = "formula"
    sheet_area = surface_area * SHEET_EXPANSION

    # 铝材加工系数
    al_factor = 1.2 if material.startswith("AL") else 1.0

    # ---- 下料 ----
    laser_hours = sheet_area * LASER_CUT_RATIO * LASER_TIME_PER_SQM * al_factor
    laser_hours = max(laser_hours, MIN_LASER_HOURS)
    laser_cost = laser_hours * LASER_RATE

    # ---- 折弯 + 压铆（原 6:4 分配基准上，各自上调 20%） ----
    bend_count = estimate_bend_count(H_mm)
    bend_hours = bend_count * BEND_TIME_PER_FOLD_MIN / 60 * al_factor
    bend_hours = max(bend_hours, MIN_BEND_HOURS)

    # 基准总价 = bend_hours × 130
    old_bend_total = bend_hours * OLD_BEND_RATE
    # 折弯、压铆在原 6:4 分配金额上分别上调 20%；二者合计 = 基准总价 × 1.2
    bend_cost = old_bend_total * BEND_SPLIT * BEND_PRICE_MULTIPLIER
    rivet_cost = old_bend_total * RIVET_SPLIT * RIVET_PRICE_MULTIPLIER
    bend_hours_display = bend_cost / BEND_RATE if BEND_RATE else 0   # 仅展示用
    rivet_hours = rivet_cost / RIVET_RATE if RIVET_RATE else 0

    # ---- 焊接 + 打磨（查表工时 ×2.5 后按 48/52 分摊，总价不变） ----
    grind_hours = estimate_grind_hours(weight_kg) * GRIND_MULTIPLIER
    old_grind_total = grind_hours * OLD_GRIND_RATE

    weld_cost = old_grind_total * WELD_SPLIT       # 48%
    grind_cost_new = old_grind_total * GRIND_SPLIT  # 52%
    weld_hours = weld_cost / WELD_RATE if WELD_RATE else 0
    grind_hours_new = grind_cost_new / GRIND_RATE if GRIND_RATE else 0

    # ---- 加工费合计 ----
    process_total = laser_cost + bend_cost + rivet_cost + weld_cost + grind_cost_new

    # ---- 喷涂 ----
    spray_extra = 0
    m = re.search(r'(\d+\.?\d*)寸', product_name)
    if m:
        inch = float(m.group(1))
        if inch <= 49:
            spray_extra = 1.0
        elif inch >= 55:
            spray_extra = 2.0
    spray_area = surface_area + spray_extra
    spray_cost = spray_area * SPRAY_RATE if with_spray else 0.0

    # ---- 汇总 ----
    # 先把展示/入账的各大项四舍五入到分，再计算单台和订单总价，
    # 保证“展示单台价 × 数量 = 展示订单总价”。
    unit_total = round(
        round(material_cost, 2) + round(process_total, 2) + round(spray_cost, 2),
        2,
    )
    order_total = round(unit_total * qty, 2)

    return {
        "product_name": product_name,
        "size_mm": f"{float(W_mm):g}*{float(D_mm):g}*{float(H_mm):g}",
        "weight_kg": weight_kg,
        "effective_weight": round(effective_weight, 2),
        "material": material,
        "qty": qty,
        "unit_price": unit_price,
        "material_cost": round(material_cost, 2),
        "surface_area_sqm": round(surface_area, 3),
        "surface_source": surface_source,
        "sheet_area_sqm": round(sheet_area, 3),
        # 下料
        "laser_hours": round(laser_hours, 2),
        "laser_cost": round(laser_cost, 2),
        # 折弯
        "bend_count": bend_count,
        "bend_hours": round(bend_hours_display, 2),   # 折弯费用对应等效工时（展示用）
        "bend_cost": round(bend_cost, 2),
        # 压铆
        "rivet_hours": round(rivet_hours, 2),
        "rivet_cost": round(rivet_cost, 2),
        # 焊接
        "weld_hours": round(weld_hours, 2),
        "weld_cost": round(weld_cost, 2),
        # 打磨
        "grind_hours": round(grind_hours_new, 2),
        "grind_cost": round(grind_cost_new, 2),
        # 汇总
        "process_total": round(process_total, 2),
        "spray_area_sqm": round(spray_area, 3),
        "spray_cost": round(spray_cost, 2),
        "unit_total": unit_total,
        "order_total": order_total,
        "with_spray": with_spray,
    }


def format_chat_report(cost: dict) -> str:
    """聊天平台展示用：完整明细 + 单台 + 订单总价"""
    return f"""
===== 钣金成本估算 =====

📐 订单名：{cost['product_name']}
   尺寸：{cost['size_mm']} mm
   数量：{cost['qty']} 台
   重量：{cost['weight_kg']} kg（含20%损耗 → {cost['effective_weight']} kg）× {cost['unit_price']}元/kg = ¥{cost['material_cost']}
   材料：{cost['material']}
   负责人：{cost.get('structure_lead') or '未提供'}

💰 加工费用：
   下料：     ¥{cost['laser_cost']}  ({cost['laser_hours']}h × 220元/h)
   折弯：     ¥{cost['bend_cost']}  ({cost['bend_hours']}h × {BEND_RATE}元/h, {cost['bend_count']}道)
   压铆：     ¥{cost['rivet_cost']}  ({cost['rivet_hours']}h × {RIVET_RATE}元/h)
   焊接：     ¥{cost['weld_cost']}  ({cost['weld_hours']}h × 80元/h)
   打磨：     ¥{cost['grind_cost']}  ({cost['grind_hours']}h × 60元/h)
   ─────────────────────
   加工费小计：¥{cost['process_total']}

🎨 喷涂费用：
   {cost['spray_area_sqm']}㎡ × 30元/㎡ = ¥{cost['spray_cost']}

━━━━━━━━━━━━━━━━━━━━
💵 单台估算价：¥{cost['unit_total']}
💰 订单总价（{cost['qty']}台）：¥{cost['order_total']}
━━━━━━━━━━━━━━━━━━━━
"""


if __name__ == "__main__":
    c = compute_cost("55寸户外广告机", 880, 230, 2200, 131, "SECC", 1)
    print(format_chat_report(c))
