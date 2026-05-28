# -*- coding: utf-8 -*-
from __future__ import annotations

import csv
import re
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

from PIL import Image
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.platypus import (
    Image as PdfImage,
    KeepTogether,
    ListFlowable,
    ListItem,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont


ROOT = Path(__file__).resolve().parents[1]
OUT_MD = ROOT / "rgp_nsight_wavefront_occupancy_report_20260528.md"
OUT_PDF = ROOT / "rgp_nsight_wavefront_occupancy_report_20260528.pdf"
FIG_DIR = ROOT / "figures"
AMD_SCREENSHOT = ROOT / "amd_rgp" / "amd_rgp_wavefront_occupancy_problem_overview.png"
AMD_PIXEL_CSV = ROOT / "amd_rgp" / "amd_rgp_wavefront_pixel_summary.csv"
NVIDIA_REGIME_CSV = ROOT / "nvidia_nsight_trace" / "nvidia_gpu_trace_regime_summary.csv"
CHART = FIG_DIR / "cross_vendor_wavefront_summary.png"
SOURCE_DECK = (
    ROOT.parents[1]
    / "monthly"
    / "decks"
    / "260525_MTK_WorkGraph_VulkanPersistentThread_POC_OptimizationEvaluation.pptx"
)


def extract_deck_context() -> str:
    ns = {"a": "http://schemas.openxmlformats.org/drawingml/2006/main"}
    with zipfile.ZipFile(SOURCE_DECK) as zf:
        names = sorted(
            [n for n in zf.namelist() if re.match(r"ppt/slides/slide\d+\.xml$", n)],
            key=lambda name: int(re.search(r"(\d+)", name).group(1)),
        )
        for index, name in enumerate(names, 1):
            root = ET.fromstring(zf.read(name))
            text = "\n".join(
                node.text or "" for node in root.findall(".//a:t", ns) if node.text
            )
            if "RDP/RGP occupancy validation" in text:
                return f"原始 deck 第 {index} 頁：{text.replace(chr(10), ' / ')}"
    return "原始 deck 脈絡：未能自動抽出指定頁。"


def analyze_amd_screenshot() -> dict[str, float]:
    img = Image.open(AMD_SCREENSHOT).convert("RGB")
    # RGP visible axis in the captured 2560x1600 screenshot: 0 to 55,000 us.
    x0, x1 = 294, 2110
    axis_end_us = 55000.0

    yellow_pixels = []
    for y in range(180, 315):
        for x in range(x0, x1 + 1):
            r, g, b = img.getpixel((x, y))
            if r > 180 and g > 120 and b < 80 and (r - g) < 120:
                yellow_pixels.append((x, y))

    magenta_pixels = []
    for y in range(480, 525):
        for x in range(x0, x1 + 1):
            r, g, b = img.getpixel((x, y))
            if r > 170 and g < 120 and b > 150:
                magenta_pixels.append((x, y))

    def span(points: list[tuple[int, int]]) -> tuple[float, float, float]:
        xs = [p[0] for p in points]
        start_us = (min(xs) - x0) / (x1 - x0) * axis_end_us
        end_us = (max(xs) - x0) / (x1 - x0) * axis_end_us
        return start_us / 1000.0, end_us / 1000.0, (end_us - start_us) / 1000.0

    cs_start_ms, cs_end_ms, cs_span_ms = span(yellow_pixels)
    evt_start_ms, evt_end_ms, evt_span_ms = span(magenta_pixels)
    result = {
        "axis_end_ms": axis_end_us / 1000.0,
        "cs_nonzero_start_ms": cs_start_ms,
        "cs_nonzero_end_ms": cs_end_ms,
        "cs_nonzero_span_ms": cs_span_ms,
        "event_span_start_ms": evt_start_ms,
        "event_span_end_ms": evt_end_ms,
        "event_span_ms": evt_span_ms,
        "visible_zero_after_cs_ms": max(0.0, evt_end_ms - cs_end_ms),
        "cs_visible_ratio_pct": cs_span_ms / evt_span_ms * 100.0,
    }

    with AMD_PIXEL_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["metric", "value"])
        for key, value in result.items():
            writer.writerow([key, f"{value:.6f}"])

    return result


def load_nvidia_compute_row() -> dict[str, str]:
    with NVIDIA_REGIME_CSV.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["event_text"] == "WG compute dispatch":
                return row
    raise RuntimeError("WG compute dispatch row not found in Nsight summary")


def make_chart(amd: dict[str, float], nv: dict[str, str]) -> None:
    import matplotlib.pyplot as plt

    FIG_DIR.mkdir(exist_ok=True)
    try:
        plt.rcParams["font.family"] = ["Microsoft JhengHei", "DejaVu Sans"]
    except Exception:
        pass

    nv_time = float(nv["time_ms"])
    nv_warps = float(
        nv[
            "TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.pct_of_peak_sustained_elapsed"
        ]
    )

    fig, axes = plt.subplots(1, 2, figsize=(13, 4.8), dpi=160)
    fig.patch.set_facecolor("#f7f7f2")

    ax = axes[0]
    ax.set_title("AMD RGP: visible CS occupancy disappears early", loc="left", fontsize=11, weight="bold")
    ax.barh(
        ["RGP timeline"],
        [amd["cs_nonzero_span_ms"]],
        left=[amd["cs_nonzero_start_ms"]],
        color="#f2b705",
        label="visible CS occupancy",
    )
    ax.barh(
        ["RGP timeline"],
        [amd["visible_zero_after_cs_ms"]],
        left=[amd["cs_nonzero_end_ms"]],
        color="#b8b8b8",
        label="visible CS occupancy ~= 0",
    )
    ax.set_xlim(0, amd["axis_end_ms"])
    ax.set_xlabel("RGP visible timeline (ms)")
    ax.text(
        amd["cs_nonzero_end_ms"] + 1,
        0,
        f"CS visible only {amd['cs_nonzero_span_ms']:.2f} ms\nof ~{amd['event_span_ms']:.2f} ms event span",
        va="center",
        fontsize=9,
    )
    ax.legend(loc="lower right", frameon=False, fontsize=8)
    ax.spines[["top", "right", "left"]].set_visible(False)

    ax = axes[1]
    ax.set_title("NVIDIA Nsight: compute dispatch has non-zero CS warps", loc="left", fontsize=11, weight="bold")
    ax.barh(["WG compute dispatch"], [nv_time], color="#2f6f73")
    ax.set_xlabel("Nsight debug-label duration (ms)")
    ax.set_xlim(0, max(100, nv_time * 1.15))
    ax.text(
        nv_time * 0.04,
        0,
        f"tpc__warps_active_shader_cs = {nv_warps:.2f}% of peak\nper-cycle elapsed = {float(nv['TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.per_cycle_elapsed']):.2f}",
        va="center",
        color="white",
        fontsize=9,
        weight="bold",
    )
    ax.spines[["top", "right", "left"]].set_visible(False)

    for axis in axes:
        axis.grid(axis="x", color="#d7d7d0", linewidth=0.8)
        axis.set_axisbelow(True)

    fig.suptitle("Cross-vendor profiler evidence for the same problem-reproduction workload", fontsize=13, weight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    fig.savefig(CHART, bbox_inches="tight")
    plt.close(fig)


def fmt(value: float, digits: int = 2) -> str:
    return f"{value:.{digits}f}"


def make_markdown(deck_context: str, amd: dict[str, float], nv: dict[str, str]) -> None:
    nv_warps = float(
        nv[
            "TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.pct_of_peak_sustained_elapsed"
        ]
    )
    nv_dispatch = float(nv["time_ms"])
    md = f"""# RDP/RGP wavefront=0 現象跨 GPU 對照實驗報告

日期：2026-05-28  
分支：`experiment/rgp-nsight-occupancy`  
報告目的：確認 `workgraph_vulkan_poc` 的 RDP/RGP `wavefront occupancy ~= 0` 是否能重現，並用 NVIDIA Nsight GPU Trace 做同 workload 對照。

## 結論

1. AMD RX 7900 XTX + RDP/RGP 上，`problem_reproduction` workload 已在新 trace 中重現：RGP `Wavefront occupancy` 視圖只在前段約 {fmt(amd['cs_nonzero_span_ms'])} ms 有可見 CS occupancy，之後到約 {fmt(amd['event_span_end_ms'])} ms 的事件尾端幾乎顯示為 0。
2. NVIDIA RTX 2080 + Nsight Graphics 2026.2.0 上，同一組 workload 沒有出現「active CS warps 全程為 0」：`WG compute dispatch` duration 為 {fmt(nv_dispatch)} ms，`tpc__warps_active_shader_cs_realtime.avg.pct_of_peak_sustained_elapsed` 為 {fmt(nv_warps)}%。
3. 目前比較支持的結論不是「shader 真的沒有 wavefront」，而是：AMD RGP 這個 Wavefront occupancy view 不能當作 full-GPU execution/completion 的單一證據。特別是本次 RGP 標題列顯示 `Instruction tracing: Full frame, limited to Shader Engine 0`。
4. 處置建議：後續報告中可以繼續忽略 RGP `wavefront=0` 作為效能瓶頸判據，但措辭應改成「RGP occupancy view 在此 workload / capture scope 下不可靠或不完整」，不要泛化成「RDP/RGP 全部不準」。

## 原始脈絡

{deck_context}

原本 deck 的重點是「這張圖只能輔助觀察，不能單獨當因果證據」。本次實驗把這點補強成跨 profiler 對照。

## workload 與控制條件

問題重現組：

```text
--resourcepath <repo-or-package-root> --gpu 0
--benchmark --benchwarmup 0 --width 640 --height 480
--wg-no-metrics
--wg-queue-shards 1 --wg-q2-shards 1
--wg-node-c-start 72
--wg-no-q1-lane-pop --wg-no-q2-deq-batch
```

注意：本報告不使用 duration 作為最佳化結論。duration 只用來確認 profiler trace 覆蓋的 dispatch span 與 debug-label 區間，因為 RGP/Nsight instrumentation 會改變時間。

## 關鍵證據

![AMD RGP wavefront occupancy](amd_rgp/amd_rgp_wavefront_occupancy_problem_overview.png)

AMD RGP 截圖量化結果：

| 指標 | 數值 |
|---|---:|
| RGP 可見時間軸 | {fmt(amd['axis_end_ms'])} ms |
| CS occupancy 可見非零區間 | {fmt(amd['cs_nonzero_start_ms'])} - {fmt(amd['cs_nonzero_end_ms'])} ms |
| CS occupancy 可見非零長度 | {fmt(amd['cs_nonzero_span_ms'])} ms |
| event overlay span | {fmt(amd['event_span_ms'])} ms |
| event 後段但 CS occupancy 近 0 的可見區間 | {fmt(amd['visible_zero_after_cs_ms'])} ms |
| CS 可見非零比例 | {fmt(amd['cs_visible_ratio_pct'])}% |

![Cross-vendor summary](figures/cross_vendor_wavefront_summary.png)

NVIDIA Nsight GPU Trace 自動匯出的 `WG compute dispatch` 關鍵欄位：

| 欄位 | 數值 |
|---|---:|
| `time_ms` | {fmt(nv_dispatch, 4)} |
| `gpu__engine_cycles_active_gr_or_ce` | {nv['FE_A.TriageA.gpu__engine_cycles_active_gr_or_ce.avg.pct_of_peak_sustained_elapsed']}% |
| `gr__dispatch_cycles_active_queue_sync` | {nv['HUB.TriageA.gr__dispatch_cycles_active_queue_sync.avg.pct_of_peak_sustained_elapsed']}% |
| `tpc__warps_active_shader_cs_realtime` | {nv['TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.pct_of_peak_sustained_elapsed']}% |
| `tpc__warps_active_shader_cs_realtime.avg.per_cycle_elapsed` | {nv['TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.per_cycle_elapsed']} |

## 觀察、假設、結論分離

觀察：

- AMD RGP 新 trace 成功擷取，`Wavefront occupancy` 畫面重現「大部分 compute/event span 內 CS occupancy 近 0」。
- 同一問題重現 workload 在 NVIDIA Nsight GPU Trace 中，`WG compute dispatch` 有非零 CS active warps。
- AMD app metrics 與 NVIDIA app metrics 都維持 deterministic output：`edges=196608`、`vertices=393216`。

假設：

- AMD RGP 的現象可能來自 capture / instruction tracing scope，尤其標題列明示 limited to Shader Engine 0。
- 也可能與 RGP 對 persistent-thread / long-running compute 的 sampling 或 visualization 有關。

結論：

- `wavefront=0` 現象在 AMD/RGP 可以重現。
- 本次 NVIDIA/Nsight 對照沒有重現「active compute warps 全為 0」。
- 因此它不是足以證明 shader 已結束或 GPU 沒有 active compute work 的證據。

## 限制

- AMD RGP 的數值是從截圖像素量化，因為 RGP GUI 沒有在本次流程中提供可匯出的 wavefront occupancy 數字。
- RGP trace title 明示只限 Shader Engine 0；因此這個 view 不等於 full-chip occupancy。
- NVIDIA RTX 2080 是 Turing，Nsight CLI 顯示 HES 不支援；部分 SM throughput / instruction fields 為 0，本報告只使用 debug-label duration 與 `tpc__warps_active_shader_cs_realtime` 作為對照。
- NVIDIA GUI 截圖開啟不穩，但 CLI trace 與 auto-export 成功；本報告以 export 表格與 `.ngfx-gputrace` 檔案作為主要 Nsight 證據。
- 跨 GPU 對照不能證明 AMD hardware counter 的 ground truth，只能證明「同類 workload 在 Nsight 不呈現全零 active CS warps」。

## 附錄資料

| 類別 | 路徑 |
|---|---|
| AMD RGP trace | `amd_rgp/traces/workgraph_poc_problem_reproduction_20260528_223613.rgp` |
| AMD RGP key screenshot | `amd_rgp/amd_rgp_wavefront_occupancy_problem_overview.png` |
| AMD screenshot pixel summary | `amd_rgp/amd_rgp_wavefront_pixel_summary.csv` |
| AMD app metrics | `amd_rgp/metrics_summary.csv` |
| NVIDIA Nsight trace | `nvidia_nsight_trace/workgraph_poc_2026_05_28_22_42_19.ngfx-gputrace` |
| NVIDIA Nsight raw export | `nvidia_nsight_trace/BASE/*.xls` |
| NVIDIA regime summary | `nvidia_nsight_trace/nvidia_gpu_trace_regime_summary.csv` |
| NVIDIA app metrics | `nvidia_nsight/metrics_summary.csv` |
| environment records | `appendix/amd_environment_20260528.txt`, `appendix/nvidia_environment_20260528.txt` |
| rerun scripts | `scripts/*.ps1`, `scripts/generate_wavefront_report_20260528.py` |

## 參考

- NVIDIA Nsight Graphics User Guide - GPU Trace：<https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html>
- NVIDIA Nsight Graphics User Guide - command line options：<https://docs.nvidia.com/nsight-graphics/UserGuide/index.html>
- GPUOpen Radeon GPU Profiler documentation：<https://gpuopen.com/rgp/>
"""
    OUT_MD.write_text(md, encoding="utf-8")


def register_fonts() -> str:
    font_path = Path(r"C:\Windows\Fonts\msjh.ttc")
    pdfmetrics.registerFont(TTFont("MSJH", str(font_path)))
    pdfmetrics.registerFont(TTFont("MSJH-Bold", str(font_path)))
    return "MSJH"


def make_pdf(deck_context: str, amd: dict[str, float], nv: dict[str, str]) -> None:
    # Older Python builds on this workstation expose hashlib.md5 without the
    # usedforsecurity keyword that this ReportLab version attempts to pass.
    import reportlab.pdfbase.pdfdoc as pdfdoc
    import reportlab.lib.utils as rl_utils

    original_md5 = pdfdoc.md5

    def md5_compat(*args, **kwargs):
        kwargs.pop("usedforsecurity", None)
        return original_md5(*args, **kwargs)

    pdfdoc.md5 = md5_compat
    rl_utils.md5 = md5_compat

    font = register_fonts()
    styles = getSampleStyleSheet()
    base = ParagraphStyle(
        "BaseCJK",
        parent=styles["BodyText"],
        fontName=font,
        fontSize=9.4,
        leading=14,
        wordWrap="CJK",
        spaceAfter=6,
    )
    title = ParagraphStyle(
        "TitleCJK",
        parent=base,
        fontName=font,
        fontSize=18,
        leading=24,
        alignment=TA_CENTER,
        spaceAfter=14,
    )
    h1 = ParagraphStyle(
        "H1CJK",
        parent=base,
        fontName=font,
        fontSize=14,
        leading=18,
        textColor=colors.HexColor("#2f4f4f"),
        spaceBefore=10,
        spaceAfter=8,
    )
    small = ParagraphStyle("SmallCJK", parent=base, fontSize=7.5, leading=10)

    doc = SimpleDocTemplate(
        str(OUT_PDF),
        pagesize=A4,
        rightMargin=1.45 * cm,
        leftMargin=1.45 * cm,
        topMargin=1.35 * cm,
        bottomMargin=1.35 * cm,
        title="RDP/RGP wavefront=0 現象跨 GPU 對照實驗報告",
    )

    def p(text: str, style=base):
        return Paragraph(text, style)

    def bullets(items: list[str]):
        return ListFlowable(
            [ListItem(p(item, base), leftIndent=8) for item in items],
            bulletType="bullet",
            start="circle",
            leftIndent=14,
        )

    def table(rows: list[list[str]], widths: list[float] | None = None):
        data = [[p(str(cell), small) for cell in row] for row in rows]
        tbl = Table(data, colWidths=widths, repeatRows=1)
        tbl.setStyle(
            TableStyle(
                [
                    ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#e6eee9")),
                    ("TEXTCOLOR", (0, 0), (-1, 0), colors.HexColor("#1f2f2f")),
                    ("GRID", (0, 0), (-1, -1), 0.3, colors.HexColor("#b8c2bd")),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f8faf7")]),
                ]
            )
        )
        return tbl

    nv_warps = float(
        nv[
            "TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.pct_of_peak_sustained_elapsed"
        ]
    )
    nv_dispatch = float(nv["time_ms"])

    story = [
        p("RDP/RGP wavefront=0 現象跨 GPU 對照實驗報告", title),
        p("日期：2026-05-28；分支：experiment/rgp-nsight-occupancy", small),
        p("結論", h1),
        bullets(
            [
                f"AMD RX 7900 XTX + RDP/RGP 已重現 wavefront occupancy 在大部分 compute span 內近 0：CS 可見非零區間約 {fmt(amd['cs_nonzero_span_ms'])} ms，event overlay span 約 {fmt(amd['event_span_ms'])} ms。",
                f"NVIDIA RTX 2080 + Nsight GPU Trace 同 workload 沒有呈現 active CS warps 全零；WG compute dispatch duration {fmt(nv_dispatch)} ms，CS active warps {fmt(nv_warps)}%。",
                "建議處置：後續不要把 RGP wavefront=0 當成 shader 結束或 GPU 無 active work 的證據；應描述為該 RGP view 在此 capture scope 下不可靠或不完整。",
            ]
        ),
        p("原始脈絡", h1),
        p(deck_context),
        p("關鍵截圖與數據", h1),
        PdfImage(str(AMD_SCREENSHOT), width=18.0 * cm, height=11.25 * cm),
        p("AMD RGP Wavefront occupancy。標題列顯示 Instruction tracing: Full frame, limited to Shader Engine 0。", small),
        PageBreak(),
        p("截圖量化與 Nsight 對照", h1),
        table(
            [
                ["指標", "數值"],
                ["RGP 可見時間軸", f"{fmt(amd['axis_end_ms'])} ms"],
                ["CS occupancy 可見非零區間", f"{fmt(amd['cs_nonzero_start_ms'])} - {fmt(amd['cs_nonzero_end_ms'])} ms"],
                ["CS occupancy 可見非零長度", f"{fmt(amd['cs_nonzero_span_ms'])} ms"],
                ["event overlay span", f"{fmt(amd['event_span_ms'])} ms"],
                ["event 後段但 CS occupancy 近 0 的可見區間", f"{fmt(amd['visible_zero_after_cs_ms'])} ms"],
                ["CS 可見非零比例", f"{fmt(amd['cs_visible_ratio_pct'])}%"],
            ],
            widths=[8.5 * cm, 8.5 * cm],
        ),
        Spacer(1, 0.35 * cm),
        PdfImage(str(CHART), width=18.0 * cm, height=6.65 * cm),
        Spacer(1, 0.2 * cm),
        table(
            [
                ["Nsight 欄位", "WG compute dispatch"],
                ["time_ms", f"{fmt(nv_dispatch, 4)}"],
                ["gpu__engine_cycles_active_gr_or_ce", f"{nv['FE_A.TriageA.gpu__engine_cycles_active_gr_or_ce.avg.pct_of_peak_sustained_elapsed']}%"],
                ["gr__dispatch_cycles_active_queue_sync", f"{nv['HUB.TriageA.gr__dispatch_cycles_active_queue_sync.avg.pct_of_peak_sustained_elapsed']}%"],
                ["tpc__warps_active_shader_cs_realtime", f"{nv['TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.pct_of_peak_sustained_elapsed']}%"],
                ["tpc__warps_active_shader_cs_realtime.avg.per_cycle_elapsed", nv["TPC.TriageA.tpc__warps_active_shader_cs_realtime.avg.per_cycle_elapsed"]],
            ],
            widths=[10.5 * cm, 6.5 * cm],
        ),
        PageBreak(),
        p("觀察 / 假設 / 結論", h1),
        bullets(
            [
                "觀察：AMD RGP 新 trace 顯示 wavefront occupancy 早早消失，但 event timeline 仍延伸到 frame 後段。",
                "觀察：NVIDIA Nsight 的同 workload export 顯示 WG compute dispatch 有非零 CS active warps。",
                "假設：AMD 現象可能來自 Shader Engine 0 scope、persistent-thread sampling、或 RGP visualization 限制。",
                "結論：這不是可用來證明 shader 已結束或 GPU 沒 active compute work 的證據。",
            ]
        ),
        p("限制", h1),
        bullets(
            [
                "AMD occupancy 數值是截圖像素量化，不是 RGP 官方 numeric export。",
                "RGP trace 明示 limited to Shader Engine 0，不能直接代表 full-chip occupancy。",
                "Nsight 在 RTX 2080/Turing 上顯示 HES not supported；部分 instruction / SM throughput 欄位為 0，本報告只使用 CS active warps 與 debug-label timing。",
                "NVIDIA GUI 截圖不穩；CLI trace 與 auto-export 成功，因此以 export 資料作為主證據。",
            ]
        ),
        p("附錄資料", h1),
        table(
            [
                ["類別", "路徑"],
                ["AMD RGP trace", "amd_rgp/traces/workgraph_poc_problem_reproduction_20260528_223613.rgp"],
                ["AMD RGP screenshot", "amd_rgp/amd_rgp_wavefront_occupancy_problem_overview.png"],
                ["AMD pixel summary", "amd_rgp/amd_rgp_wavefront_pixel_summary.csv"],
                ["NVIDIA Nsight trace", "nvidia_nsight_trace/workgraph_poc_2026_05_28_22_42_19.ngfx-gputrace"],
                ["NVIDIA raw export", "nvidia_nsight_trace/BASE/*.xls"],
                ["NVIDIA regime summary", "nvidia_nsight_trace/nvidia_gpu_trace_regime_summary.csv"],
                ["App metrics", "amd_rgp/metrics_summary.csv, nvidia_nsight/metrics_summary.csv"],
                ["Environment", "appendix/amd_environment_20260528.txt, appendix/nvidia_environment_20260528.txt"],
                ["Scripts", "scripts/*.ps1, scripts/generate_wavefront_report_20260528.py"],
            ],
            widths=[4.2 * cm, 12.8 * cm],
        ),
        p("參考", h1),
        bullets(
            [
                "NVIDIA Nsight Graphics User Guide - GPU Trace: https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html",
                "NVIDIA Nsight Graphics User Guide - command line options: https://docs.nvidia.com/nsight-graphics/UserGuide/index.html",
                "GPUOpen Radeon GPU Profiler: https://gpuopen.com/rgp/",
            ]
        ),
    ]

    doc.build(story)


def main() -> None:
    deck_context = extract_deck_context()
    amd = analyze_amd_screenshot()
    nv = load_nvidia_compute_row()
    make_chart(amd, nv)
    make_markdown(deck_context, amd, nv)
    make_pdf(deck_context, amd, nv)
    print(OUT_MD)
    print(OUT_PDF)
    print(CHART)
    print(AMD_PIXEL_CSV)


if __name__ == "__main__":
    main()
