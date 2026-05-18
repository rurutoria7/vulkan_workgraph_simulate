import fs from "node:fs/promises";
import { readFileSync } from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath, pathToFileURL } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "..", "..");
const runtimeNodeDir =
  process.env.CODEX_NODE_DEPS_ROOT ??
  "C:/Users/Kevin/.cache/codex-runtimes/codex-primary-runtime/dependencies/node";

const require = createRequire(import.meta.url);
const artifactEntry = require.resolve("@oai/artifact-tool", {
  paths: [runtimeNodeDir],
});
const skiaEntry = require.resolve("skia-canvas", {
  paths: [path.join(runtimeNodeDir, "node_modules", "@oai", "artifact-tool")],
});

const {
  Presentation,
  PresentationFile,
  FileBlob,
  column,
  row,
  grid,
  panel,
  text,
  chart,
  image,
  rule,
  fill,
  hug,
  fixed,
  fr,
} = await import(pathToFileURL(artifactEntry).href);
const { Canvas, loadImage } = await import(pathToFileURL(skiaEntry).href);

const OUT_DIR = path.join(repoRoot, "reports", "2026-05", "weekly");
const PPTX_PATH = path.join(OUT_DIR, "weekly_report_260511_plain.pptx");
const ASSET_DIR = path.join(OUT_DIR, "assets", "weekly_report_260511");
const NVIDIA_DIR = path.join(ASSET_DIR, "nvidia_nsight");
const VERIFY_DIR = path.join(
  OUT_DIR,
  "verification",
  "weekly_report_260511_plain",
);
const RGP_DIR = path.join(
  repoRoot,
  "reports",
  "2026-05",
  "metrics",
  "rgp_occupancy_20260509",
);

const W = 1920;
const H = 1080;
const C = {
  bg: "#FBFAF5",
  ink: "#111827",
  body: "#374151",
  muted: "#6B7280",
  line: "#D1D5DB",
  accent: "#B45309",
  accent2: "#0F766E",
  highlight: "#FEF3C7",
  highlight2: "#DCFCE7",
  danger: "#B91C1C",
};

const baseText = {
  typeface: "Microsoft JhengHei",
  color: C.body,
};
const titleStyle = {
  ...baseText,
  fontSize: 70,
  bold: true,
  color: C.ink,
};
const eyebrowStyle = {
  ...baseText,
  fontSize: 18,
  color: C.accent,
  bold: true,
};

function tx(value, options = {}) {
  return text(value, {
    name: options.name,
    width: options.width ?? fill,
    height: options.height ?? hug,
    columnSpan: options.columnSpan,
    rowSpan: options.rowSpan,
    style: {
      ...baseText,
      fontSize: options.size ?? 28,
      bold: options.bold ?? false,
      color: options.color ?? C.body,
      alignment: options.align,
      lineSpacing: options.lineSpacing,
    },
  });
}

function addSlide(prs, { title, eyebrow = null, body }) {
  const slide = prs.slides.add();
  const titleChildren = [
    ...(eyebrow
      ? [
          text(eyebrow, {
            name: "eyebrow",
            width: fill,
            height: hug,
            style: eyebrowStyle,
          }),
        ]
      : []),
    text(title, {
      name: "slide-title",
      width: fill,
      height: hug,
      style: titleStyle,
    }),
    rule({
      name: "title-rule",
      width: fixed(260),
      stroke: C.accent,
      weight: 5,
    }),
  ];
  slide.compose(
    panel(
      {
        name: `slide-${prs.slides.count}-bg`,
        width: fill,
        height: fill,
        fill: C.bg,
        padding: { x: 58, y: 42 },
      },
      column(
        { name: "slide-root", width: fill, height: fill, gap: 28 },
        [
          column(
            { name: "title-stack", width: fill, height: hug, gap: 14 },
            titleChildren,
          ),
          body,
        ],
      ),
    ),
    { frame: { left: 0, top: 0, width: W, height: H }, baseUnit: 8 },
  );
  return slide;
}

function bulletList(items, options = {}) {
  return column(
    { name: options.name, width: fill, height: hug, gap: options.gap ?? 22 },
    items.map((item, idx) =>
      row(
        { name: `${options.name ?? "bullet"}-${idx}`, width: fill, height: hug, gap: 12 },
        [
          tx("•", {
            width: fixed(28),
            size: item.size ?? options.size ?? 34,
            color: item.color ?? options.color ?? C.accent,
            bold: true,
          }),
          tx(item.text, {
            size: item.size ?? options.size ?? 34,
            color: item.color ?? options.color ?? C.body,
            bold: item.bold ?? false,
            width: fill,
          }),
        ],
      ),
    ),
  );
}

function callout(value, options = {}) {
  return row(
    { name: options.name, width: fill, height: hug, gap: 18, align: "center" },
    [
      rule({
        name: `${options.name ?? "callout"}-rule`,
        width: fixed(10),
        height: fixed(options.ruleHeight ?? 58),
        stroke: options.ruleColor ?? C.accent,
        weight: 10,
      }),
      tx(value, {
        name: `${options.name ?? "callout"}-text`,
        size: options.size ?? 33,
        color: options.color ?? C.ink,
        bold: options.bold ?? true,
      }),
    ],
  );
}

function metricLine(items) {
  return grid(
    {
      name: "metric-line",
      width: fill,
      height: hug,
      columns: items.map(() => fr(1)),
      columnGap: 18,
    },
    items.map((item, idx) =>
      column(
        { name: `metric-${idx}`, width: fill, height: hug, gap: 8 },
        [
          tx(item.value, {
            size: item.size ?? 62,
            bold: true,
            color: item.color ?? C.ink,
            align: "center",
          }),
          rule({ width: fill, stroke: item.color ?? C.accent, weight: 3 }),
          tx(item.label, {
            size: 24,
            color: C.muted,
            align: "center",
          }),
        ],
      ),
    ),
  );
}

function metricStack(items) {
  return column(
    { name: "metric-stack", width: fill, height: hug, gap: 28 },
    items.map((item, idx) =>
      row(
        { name: `metric-stack-${idx}`, width: fill, height: hug, gap: 22, align: "center" },
        [
          tx(item.value, {
            width: fixed(item.valueWidth ?? 310),
            size: item.size ?? 64,
            bold: true,
            color: item.color ?? C.accent2,
          }),
          tx(item.label, {
            size: item.labelSize ?? 34,
            color: C.body,
            bold: item.bold ?? false,
          }),
        ],
      ),
    ),
  );
}

function stepFlow(items) {
  const children = [];
  items.forEach((item, idx) => {
    children.push(
      column(
        { name: `step-${idx}`, width: fixed(230), height: hug, gap: 5 },
        [
          tx(item.step, {
            size: 36,
            bold: true,
            color: idx === items.length - 1 ? C.accent2 : C.accent,
            align: "center",
          }),
          tx(item.label, {
            size: 22,
            color: C.body,
            align: "center",
          }),
        ],
      ),
    );
    if (idx !== items.length - 1) {
      children.push(rule({ width: fixed(58), stroke: C.line, weight: 2 }));
    }
  });
  return row({ name: "step-flow", width: fill, height: hug, gap: 12, align: "center" }, children);
}

function evidenceStack(items, options = {}) {
  return column(
    { name: options.name, width: fill, height: hug, gap: options.gap ?? 16 },
    items.map((item, idx) =>
      row(
        { name: `${options.name ?? "evidence"}-${idx}`, width: fill, height: hug, gap: 16, align: "center" },
        [
          tx(item.label, {
            width: fixed(options.labelWidth ?? 104),
            size: options.labelSize ?? 22,
            bold: true,
            color: item.color ?? C.accent,
          }),
          tx(item.text, {
            size: item.size ?? options.size ?? 28,
            color: item.textColor ?? C.body,
            bold: item.bold ?? false,
          }),
        ],
      ),
    ),
  );
}

function evidenceStrip(items, options = {}) {
  return grid(
    {
      name: options.name,
      width: fill,
      height: hug,
      columns: items.map(() => fr(1)),
      columnGap: options.columnGap ?? 26,
    },
    items.map((item, idx) =>
      column(
        { name: `${options.name ?? "evidence-strip"}-${idx}`, width: fill, height: hug, gap: 8 },
        [
          tx(item.label, {
            size: options.labelSize ?? 22,
            bold: true,
            color: item.color ?? C.accent,
          }),
          rule({ width: fill, stroke: item.color ?? C.accent, weight: 2 }),
          tx(item.text, {
            size: item.size ?? options.size ?? 28,
            color: item.textColor ?? C.body,
          }),
        ],
      ),
    ),
  );
}

function dataTable({ name, columns, rows, widths, fontSize = 25, headerSize = 23, rowGap = 12 }) {
  const colWidths = widths.map((w) => fixed(w));
  const rowNode = (cells, idx, isHeader = false) =>
    grid(
      {
        name: `${name}-row-${idx}`,
        width: fill,
        height: hug,
        columns: colWidths,
        columnGap: 24,
        padding: { y: 6 },
      },
      cells.map((cell, cellIdx) =>
        tx(cell, {
          name: `${name}-r${idx}-c${cellIdx}`,
          size: isHeader ? headerSize : fontSize,
          bold: isHeader,
          color: isHeader ? C.accent : C.body,
          width: fill,
        }),
      ),
    );

  const children = [rowNode(columns, "header", true), rule({ stroke: C.line, weight: 2 })];
  rows.forEach((r, idx) => {
    children.push(rowNode(r, idx, false));
    if (idx !== rows.length - 1) children.push(rule({ stroke: "#E5E7EB", weight: 1 }));
  });
  return column({ name, width: fill, height: hug, gap: rowGap }, children);
}

function imageEvidence({
  name,
  title,
  note,
  file,
  imageHeight = 320,
  titleSize = 24,
  noteSize = 20,
  fit = "contain",
}) {
  const imagePath = path.isAbsolute(file)
    ? file
    : file.includes("/") || file.includes("\\")
      ? path.join(repoRoot, file)
      : path.join(RGP_DIR, file);
  const dataUrl = `data:image/png;base64,${readFileSync(imagePath).toString("base64")}`;
  return column(
    { name, width: fill, height: hug, gap: 9 },
    [
      tx(title, {
        name: `${name}-title`,
        size: titleSize,
        bold: true,
        color: C.accent,
      }),
      image({
        name: `${name}-image`,
        dataUrl,
        contentType: "image/png",
        width: fill,
        height: fixed(imageHeight),
        fit,
        alt: title,
      }),
      tx(note, {
        name: `${name}-note`,
        size: noteSize,
        color: C.body,
      }),
    ],
  );
}

function documentShot({ name, title, file, imageHeight, titleColor = C.accent }) {
  const dataUrl = `data:image/png;base64,${readFileSync(file).toString("base64")}`;
  return column(
    { name, width: fill, height: hug, gap: 8 },
    [
      tx(title, {
        name: `${name}-title`,
        size: 22,
        bold: true,
        color: titleColor,
      }),
      image({
        name: `${name}-image`,
        dataUrl,
        contentType: "image/png",
        width: fill,
        height: fixed(imageHeight),
        fit: "contain",
        alt: title,
      }),
    ],
  );
}

const rgpCrop = { sx: 250, sy: 150, sw: 1280, sh: 720 };
const rgpCropped = {
  wavefront: path.join(ASSET_DIR, "rgp_wavefront_only_crop.png"),
  aluTail: path.join(ASSET_DIR, "rgp_alu_tail_crop.png"),
  aluOnly: path.join(ASSET_DIR, "rgp_alu_only_crop.png"),
};
const nvidiaShots = {
  gtxList: path.join(NVIDIA_DIR, "nvidia_nsight_gtx1080ti_list.png"),
  footnote: path.join(NVIDIA_DIR, "nvidia_nsight_gtx1080ti_footnote.png"),
  traceOverview: path.join(NVIDIA_DIR, "nvidia_gpu_trace_low_level_units.png"),
  turingSupport: path.join(NVIDIA_DIR, "nvidia_gpu_trace_turing_support.png"),
};

async function cropRgpImage(sourceFile, outPath) {
  const src = await loadImage(path.join(RGP_DIR, sourceFile));
  const canvas = new Canvas(rgpCrop.sw, rgpCrop.sh);
  const ctx = canvas.getContext("2d");
  ctx.drawImage(
    src,
    rgpCrop.sx,
    rgpCrop.sy,
    rgpCrop.sw,
    rgpCrop.sh,
    0,
    0,
    rgpCrop.sw,
    rgpCrop.sh,
  );
  await fs.writeFile(outPath, await canvas.toBuffer("png"));
}

async function prepareRgpAssets() {
  await fs.mkdir(ASSET_DIR, { recursive: true });
  await cropRgpImage("wavefront_only_occupancy_rgp.png", rgpCropped.wavefront);
  await cropRgpImage("debug_alu_tail_20m_wavefront_only_occupancy_rgp.png", rgpCropped.aluTail);
  await cropRgpImage("alu_only_probe_wavefront_only_occupancy_rgp.png", rgpCropped.aluOnly);
}

function cover(prs) {
  const slide = prs.slides.add();
  slide.compose(
    panel(
      {
        name: "cover-bg",
        width: fill,
        height: fill,
        fill: C.bg,
        padding: { x: 110, y: 92 },
      },
      column(
        { width: fill, height: fill, gap: 26, justify: "center" },
        [
          tx("Weekly Report 260511", {
            name: "cover-title",
            size: 86,
            bold: true,
            color: C.ink,
          }),
          rule({ name: "cover-rule", width: fixed(320), stroke: C.accent, weight: 6 }),
          tx("Vulkan Persistent Thread WorkGraph POC", {
            name: "cover-subtitle",
            size: 40,
            color: C.body,
          }),
          tx("Bottleneck timeline: Q1/Q2 sharding + Q1 lane-pop", {
            name: "cover-thesis",
            size: 34,
            color: C.accent2,
            bold: true,
          }),
          tx("2026 / 05 / 11", {
            name: "cover-date",
            size: 24,
            color: C.muted,
          }),
        ],
      ),
    ),
    { frame: { left: 0, top: 0, width: W, height: H }, baseUnit: 8 },
  );
}

function buildDeck() {
  const prs = Presentation.create({ slideSize: { width: W, height: H } });

  cover(prs);

  addSlide(prs, {
    title: "本週摘要",
    body: column(
      { width: fill, height: fill, gap: 34, justify: "between" },
      [
        bulletList(
          [
            { text: "把 2026-05 bottleneck investigation 整理成 canonical 主線。" },
            { text: "主線：RDP/RGP occupancy validation -> duration 修正 -> B/C ratio -> Q1/Q2 sharding -> Q1 lane-pop -> high shard。" },
            { text: "最後採用的是 Q1/Q2 sharding + Q1 lane-pop，不是 wave/request batching。", bold: true, color: C.accent2 },
          ],
          { name: "summary-bullets", size: 31, gap: 26 },
        ),
        stepFlow([
          { step: "01", label: "RGP validation" },
          { step: "02", label: "timestamp duration" },
          { step: "03", label: "B/C ratio" },
          { step: "04", label: "Q1/Q2 sharding" },
          { step: "05", label: "Q1 lane-pop" },
          { step: "06", label: "256 shards" },
        ]),
        metricLine([
          { value: "30 ms", label: "修正量測後 baseline", fill: "#FFFFFF" },
          { value: "3.84 ms", label: "Q1 lane-pop median", fill: C.highlight },
          { value: "0.99 ms", label: "high shard default median", fill: C.highlight2, color: C.accent2 },
        ]),
        callout("本週主訊息：queue bottleneck 的有效路徑是 sharding + shard selection strategy。"),
      ],
    ),
  });

  addSlide(prs, {
    title: "推導方法",
    body: column(
      { width: fill, height: fill, gap: 42, justify: "between" },
      [
        tx("先分清楚：duration 是結果，counters 是壓力結構，不混在一起判斷。", {
          size: 40,
          bold: true,
          color: C.ink,
        }),
        evidenceStrip(
          [
            {
              label: "Duration",
              text: "只採 app 端 GPU timestamp；counter-enabled run 不拿來比時間。",
            },
            {
              label: "Counters",
              text: "只看 CAS fail/ok、Q2 high-water、ready fail、empty probe。",
              color: C.accent2,
            },
            {
              label: "RGP",
              text: "Wavefront occupancy 只作輔助，不單獨推論 dispatch lifetime。",
            },
          ],
          { name: "method-strip", size: 31, labelSize: 25 },
        ),
        dataTable({
          name: "method-metrics-table",
          columns: ["Metric", "代表什麼", "用法"],
          widths: [340, 650, 540],
          fontSize: 27,
          headerSize: 25,
          rowGap: 18,
          rows: [
            ["CAS fail / ok", "同一 queue head/count/tail 的競爭程度", "定位 global queue hot spot"],
            ["Q2 high-water", "Q2 backlog 的高水位", "判斷 C writers 是否追得上"],
            ["ready fail", "consumer claim 後等待 ready flag", "檢查 publish / ready protocol"],
            ["empty probe", "worker 掃到空 shard 的成本", "檢查 polling / scan-start 策略"],
          ],
        }),
        callout("規則：只有 duration 下降且 counters 壓力同步下降，才把假設推進成結論。", {
          size: 36,
          color: C.accent2,
          ruleHeight: 74,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "Bottleneck 推導鏈",
    body: column(
      { width: fill, height: fill, gap: 34 },
      [
        tx("每一步都先看 observation，再提出 hypothesis，最後用 timestamp / counters 驗證或排除。", {
          size: 31,
          bold: true,
          color: C.ink,
        }),
        dataTable({
          name: "chain-table",
          columns: ["Step", "觀察到的 metrics", "猜測", "驗證 / 排除"],
          widths: [220, 510, 370, 500],
          fontSize: 22,
          headerSize: 22,
          rowGap: 9,
          rows: [
            ["RGP", "0 occupancy 但 event bar 很長；ALU-only 仍同型態", "RGP 視圖不足以判斷 lifetime", "降級為輔助證據"],
            ["B/C", "Q2 high-water 50k；Q2 enq fail 67.8x", "C writers 不足", "C40 -> 27.97 ms；high-water 9k"],
            ["Q1", "Q1 enqueue fail 仍高；Q1s16 -> 22.96 ms", "Q1 global queue hot", "C38/B58 -> 13.70 ms"],
            ["Q2", "Q2 enq/deq 47.8x / 255.9x；high-water 68k", "Q2 count/head/tail hot", "Q2s16 -> 5.57 ms；high-water 1.5k"],
            ["Lane-pop", "Q1 deq fail/ok 60.54x", "lane 從同 shard 起掃造成局部競爭", "15.38x -> 12.98x；3.84 ms"],
            ["256 shards", "Q1/Q2 16 時 CAS 仍高", "剩餘主因仍是 queue atomic", "CAS < 0.5x；median 0.99 ms"],
          ],
        }),
        callout("這條鏈的核心不是單一 profiler 圖，而是 duration trend + queue-pressure metrics 一起收斂。", {
          size: 34,
          color: C.accent2,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "結果總表",
    body: column(
      { width: fill, height: fill, gap: 28, justify: "between" },
      [
        dataTable({
          name: "result-table",
          columns: ["階段", "設定", "Compute duration"],
          widths: [430, 790, 360],
          fontSize: 26,
          headerSize: 25,
          rowGap: 13,
          rows: [
            ["RDP/RGP occupancy validation", "多組 RGP capture / RDP-RDS trace", "排除 wavefront=0"],
            ["修正量測後 baseline", "Q1 batch on, Q2 scalar, C=24/B=72", "30.13-30.35 ms avg"],
            ["B/C ratio", "Q1 batch on, Q2 scalar, C=40/B=56", "27.97 ms avg"],
            ["Q1 sharding + B/C refine", "Q1s16/Q2s1, C=38/B=58", "13.70 ms avg"],
            ["Q2 sharding + rebalance", "Q1s16/Q2s16, C=16/B=80", "5.57 ms avg"],
            ["Q1 lane-pop", "Q1s16/Q2s16, lane-pop on, C=24/B=72", "3.84 ms median"],
            ["High shard default", "Q1s256/Q2s256, lane-pop on, C=24/B=72", "0.99 ms median"],
          ],
        }),
        callout("從 baseline 約 30 ms 收斂到約 0.99 ms median；主要改善都來自 queue contention 分散。", {
          size: 35,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "量測修正",
    body: column(
      { width: fill, height: fill, gap: 40, justify: "between" },
      [
        tx("問題不是新的 shader bottleneck，而是部分 duration run 被 shader-side metrics counters 污染。", {
          size: 38,
          bold: true,
          color: C.ink,
        }),
        bulletList(
          [
            { text: "duration 結論只採 app 端 GPU timestamp。" },
            { text: "shader counters 只用來看 queue pressure、atomic contention、backlog 等結構。" },
            { text: "RGP event timing 只作輔助，不作單獨結論。" },
          ],
          { name: "measurement-bullets", size: 36, gap: 34 },
        ),
        evidenceStrip(
          [
            { label: "觀察", text: "counter-enabled duration 和 counter reduction 不一致。" },
            { label: "猜測", text: "metrics atomics 污染 timing。", color: C.accent2 },
            { label: "驗證", text: "改用 timestamp-only 後再重掃主線。" },
          ],
          { name: "measurement-evidence", size: 29, labelSize: 23 },
        ),
        callout("這一步先修正證據來源，再繼續追 bottleneck。", {
          fill: C.highlight2,
          color: C.accent2,
          size: 38,
          ruleHeight: 76,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "RGP occupancy validation",
    body: column(
      { width: fill, height: fill, gap: 20 },
      [
        tx("`wavefront=0` 不能直接推論 shader 結束，也不能直接推論 memory/atomic stall。", {
          size: 33,
          bold: true,
          color: C.ink,
        }),
        grid(
          {
            name: "rgp-image-grid",
            width: fill,
            height: hug,
            columns: [fr(1.52), fr(0.9)],
            columnGap: 28,
          },
          [
            imageEvidence({
              name: "rgp-wavefront-only",
              title: "Wavefront-only RGP",
              note: "關掉 counters / tracing 後，occupancy 仍只集中前段。",
              file: rgpCropped.wavefront,
              imageHeight: 565,
              titleSize: 27,
              noteSize: 22,
            }),
            column(
              { width: fill, height: hug, gap: 18 },
              [
                imageEvidence({
                  name: "rgp-alu-tail",
                  title: "Artificial ALU tail",
                  note: "刻意拉長 ALU tail 後，occupancy 形狀仍不代表完整 lifetime。",
                  file: rgpCropped.aluTail,
                  imageHeight: 270,
                  titleSize: 23,
                  noteSize: 18,
                }),
                imageEvidence({
                  name: "rgp-alu-only",
                  title: "ALU-only shader",
                  note: "移除 queue / atomic 後仍有相同現象，不是 queue 特有。",
                  file: rgpCropped.aluOnly,
                  imageHeight: 270,
                  titleSize: 23,
                  noteSize: 18,
                }),
              ],
            ),
          ],
        ),
        callout("觀察：0 occupancy + long event bar；驗證：ALU-only 仍同型態，所以這組 RGP 圖只能作輔助。", {
          size: 31,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "為什麼 GTX 1080 Ti 不夠？",
    body: grid(
      {
        name: "nvidia-support-grid",
        width: fill,
        height: fill,
        columns: [fr(1.22), fr(1)],
        columnGap: 34,
      },
      [
        column(
          { name: "nvidia-doc-collage", width: fill, height: fill, gap: 10 },
          [
            documentShot({
              name: "nvidia-gtx-row",
              title: "Nsight Graphics GPUs Full List：GeForce GTX 1080 Ti[1]",
              file: nvidiaShots.gtxList,
              imageHeight: 148,
            }),
            documentShot({
              name: "nvidia-footnote",
              title: "註解 [1]：profiling activities are not supported",
              file: nvidiaShots.footnote,
              imageHeight: 136,
              titleColor: C.danger,
            }),
            documentShot({
              name: "nvidia-trace-overview",
              title: "GPU Trace Profiler：低階 profiler，觀察 GPU units utilization",
              file: nvidiaShots.traceOverview,
              imageHeight: 238,
              titleColor: C.accent2,
            }),
            documentShot({
              name: "nvidia-turing-support",
              title: "GPU Trace 支援範圍：Turing architecture and above",
              file: nvidiaShots.turingSupport,
              imageHeight: 166,
              titleColor: C.accent2,
            }),
          ],
        ),
        panel(
          {
            name: "nvidia-conclusion-box",
            width: fill,
            height: fill,
            fill: "#FFF7ED",
            padding: { x: 32, y: 30 },
          },
          column(
            { width: fill, height: fill, gap: 24, justify: "center" },
            [
              tx("官方文件結論", {
                size: 42,
                bold: true,
                color: C.ink,
              }),
              rule({ width: fixed(260), stroke: C.danger, weight: 5 }),
              bulletList(
                [
                  { text: "GeForce GTX 1080 Ti 屬 Pascal，低於 GPU Trace 要求的 Turing+。" },
                  { text: "官方標註 GeForce GTX 1080 Ti 不支援 profiling activities。" },
                  { text: "我們需要 occupancy / SM / warp / memory profiling，不是 frame debugging。" },
                ],
                { name: "nvidia-conclusion-bullets", size: 30, gap: 24, color: C.danger },
              ),
              callout("profiling = GPU 性能分析；frame debugging = 單幀除錯。", {
                name: "nvidia-term-note",
                size: 27,
                color: C.ink,
                ruleColor: C.accent2,
                ruleHeight: 60,
              }),
              column(
                { name: "nvidia-short-quotes", width: fill, height: hug, gap: 10 },
                [
                  tx("短引：\"profiling activities are not supported.\"", {
                    size: 21,
                    color: C.body,
                  }),
                  tx("短引：\"Turing architecture and above.\"", {
                    size: 21,
                    color: C.body,
                  }),
                ],
              ),
            ],
          ),
        ),
      ],
    ),
  });

  addSlide(prs, {
    title: "B/C ratio",
    body: grid(
      {
        width: fill,
        height: fill,
        columns: [fr(0.9), fr(1.1)],
        columnGap: 48,
      },
      [
        column(
          { width: fill, height: fill, gap: 30, justify: "between" },
          [
            tx("問題：Node C writer 是否不足，造成 Q2 backlog？", {
              size: 35,
              bold: true,
              color: C.ink,
            }),
            evidenceStack(
              [
                { label: "觀察", text: "Q2 high-water 50k；Q2 enq fail 67.8x。" },
                { label: "猜測", text: "C writers 原本不足，Q2 backlog 放大 enqueue 競爭。", color: C.accent2 },
                { label: "驗證", text: "C40 後 high-water 9k、enq fail 31.8x、27.97 ms。" },
              ],
              { name: "bc-evidence", size: 26, labelSize: 21, gap: 12 },
            ),
            metricLine([
              { value: "C=40", label: "best writer count", fill: C.highlight },
              { value: "27.97 ms", label: "avg compute", fill: C.highlight2, color: C.accent2 },
            ]),
            callout("Q2 high-water 約從 50k 降到 9k。", { size: 34, ruleHeight: 64 }),
          ],
        ),
        dataTable({
          name: "bc-table",
          columns: ["C", "B", "Avg compute"],
          widths: [140, 140, 260],
          fontSize: 32,
          headerSize: 29,
          rowGap: 18,
          rows: [
            ["24", "72", "30.35 ms"],
            ["32", "64", "28.44 ms"],
            ["40", "56", "27.97 ms"],
            ["48", "48", "28.59 ms"],
            ["64", "32", "30.25 ms"],
          ],
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "Q1 sharding + B/C refine",
    body: grid(
      {
        width: fill,
        height: fill,
        columns: [fr(1), fr(1)],
        columnGap: 48,
      },
      [
        column(
          { width: fill, height: fill, gap: 30, justify: "between" },
          [
            tx("Q1 enqueue/dequeue contention 仍然明顯；先把 Q1 global queue 分散。", {
              size: 35,
              bold: true,
              color: C.ink,
            }),
            evidenceStack(
              [
                { label: "觀察", text: "Q1s16 單獨把 compute 拉到 22.96 ms。" },
                { label: "猜測", text: "Q1 global queue 是 hot atomic 結構。", color: C.accent2 },
                { label: "驗證", text: "B/C refine 疊加後到 13.70 ms；Q2 counters 仍高。" },
              ],
              { name: "q1-evidence", size: 28, labelSize: 22, gap: 15 },
            ),
            callout("13.70 ms avg", { fill: C.highlight2, color: C.accent2, size: 38, ruleHeight: 70 }),
          ],
        ),
        dataTable({
          name: "q1-table",
          columns: ["Q1 shards", "C/B", "Avg"],
          widths: [220, 210, 210],
          fontSize: 31,
          headerSize: 29,
          rowGap: 18,
          rows: [
            ["4", "40/56", "23.24 ms"],
            ["8", "40/56", "17.90 ms"],
            ["16", "36/60", "13.95 ms"],
            ["16", "38/58", "13.70 ms"],
            ["16", "40/56", "14.08 ms"],
          ],
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "Q2 sharding + rebalance",
    body: grid(
      {
        width: fill,
        height: fill,
        columns: [fr(1), fr(1)],
        columnGap: 48,
      },
      [
        dataTable({
          name: "q2-shards-table",
          columns: ["Q2 shards", "Avg compute"],
          widths: [250, 280],
          fontSize: 32,
          headerSize: 30,
          rowGap: 20,
          rows: [
            ["1", "16.95 ms"],
            ["2", "10.24 ms"],
            ["4", "8.38 ms"],
            ["8", "6.87 ms"],
            ["16", "6.08 ms"],
          ],
        }),
        column(
          { width: fill, height: fill, gap: 30, justify: "between" },
          [
            tx("Q2 global `count/head/tail` 是 Q1 sharded 後的下一個主要壓力點。", {
              size: 34,
              bold: true,
              color: C.ink,
            }),
            evidenceStack(
              [
                { label: "觀察", text: "Q2 enq/deq 47.8x / 255.9x；high-water 68k。" },
                { label: "猜測", text: "Q2 count/head/tail 是下一個 hot spot。", color: C.accent2 },
                { label: "驗證", text: "Q2s16 後 high-water 68k -> 1.5k，最佳到 5.57 ms。" },
              ],
              { name: "q2-evidence", size: 25, labelSize: 20, gap: 11 },
            ),
            dataTable({
              name: "q2-bc-table",
              columns: ["C/B", "Avg compute"],
              widths: [210, 270],
              fontSize: 30,
              headerSize: 28,
              rowGap: 17,
              rows: [
                ["12/84", "5.62 ms"],
                ["16/80", "5.57 ms"],
                ["24/72", "5.66 ms"],
                ["38/58", "6.09 ms"],
              ],
            }),
            callout("最佳點：Q1s16/Q2s16, C=16/B=80, 約 5.57 ms avg。", {
              size: 33,
              ruleHeight: 64,
            }),
          ],
        ),
      ],
    ),
  });

  addSlide(prs, {
    title: "Q1 lane-pop",
    body: column(
      { width: fill, height: fill, gap: 30, justify: "between" },
      [
        tx("同一 workgroup 內所有 lane 從同一 shard 開始 pop，會造成局部 shard contention。", {
          size: 35,
          bold: true,
          color: C.ink,
        }),
        evidenceStrip(
          [
            { label: "觀察", text: "Q1 deq fail/ok 60.54x。" },
            { label: "猜測", text: "同一 workgroup lane 從同一 shard 起掃。", color: C.accent2 },
            { label: "驗證", text: "15.38x -> 12.98x；median 3.84 ms。" },
          ],
          { name: "lane-pop-evidence", size: 27, labelSize: 21 },
        ),
        dataTable({
          name: "lanepop-table",
          columns: ["Mode", "Avg", "Median", "Min"],
          widths: [710, 220, 220, 220],
          fontSize: 29,
          headerSize: 27,
          rowGap: 16,
          rows: [
            ["lane-pop off, C=16/B=80", "6.01 ms", "5.60 ms", "5.54 ms"],
            ["lane-pop on, C=16/B=80", "4.39 ms", "4.00 ms", "3.90 ms"],
            ["lane-pop on, C=24/B=72", "3.85 ms", "3.84 ms", "3.73 ms"],
          ],
        }),
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1)], columnGap: 24 },
          [
            callout("Old: wgId % Q1_QUEUE_SHARDS", { fill: "#FFFFFF", size: 29 }),
            callout("New: (wgId * local_size_x + localId) % Q1_QUEUE_SHARDS", {
              fill: C.highlight2,
              color: C.accent2,
              size: 29,
            }),
          ],
        ),
        callout("Q1 lane-pop 是 shard selection / scan-start strategy，不是 request batching。", {
          size: 32,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "High shard scaling",
    body: column(
      { width: fill, height: fill, gap: 22, justify: "between" },
      [
        tx("Q1/Q2 都升到 256 shards 時，median compute 約 0.99 ms；再往上開始反彈。", {
          size: 34,
          bold: true,
          color: C.ink,
        }),
        evidenceStrip(
          [
            { label: "觀察", text: "16 shards 時 Q1/Q2 CAS fail 仍高。" },
            { label: "猜測", text: "剩餘主因仍是 queue atomic contention。", color: C.accent2 },
            { label: "驗證", text: "256 shards 後 CAS < 0.5x，3.86 -> 0.99 ms。" },
          ],
          { name: "high-shard-evidence", size: 26, labelSize: 21 },
        ),
        chart({
          name: "high-shard-chart",
          chartType: "line",
          width: fill,
          height: fixed(465),
          config: {
            categories: ["16", "32", "64", "128", "192", "256", "384", "512"],
            series: [
              {
                name: "Median compute ms",
                values: [3.86, 2.4, 1.54, 1.14, 1.08, 0.99, 1.05, 1.12],
              },
            ],
          },
        }),
        dataTable({
          name: "shard-key-table",
          columns: ["16", "64", "128", "256", "512"],
          widths: [210, 210, 210, 210, 210],
          fontSize: 29,
          headerSize: 27,
          rowGap: 14,
          rows: [["3.86 ms", "1.54 ms", "1.14 ms", "0.99 ms", "1.12 ms"]],
        }),
        callout("代表性非對稱結果：Q1=256/Q2=128 會退到 1.955 ms，Q2 shard count 不能太低。", {
          size: 31,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "未採用方案 / 排除項目",
    body: column(
      { width: fill, height: fill, gap: 34, justify: "between" },
      [
        bulletList(
          [
            { text: "Output writes off 只改善約 3.86 ms -> 3.78 ms，不是主瓶頸。" },
            { text: "Q2 ready-first 較慢：3.86 -> 5.06 ms；ready fail 159K -> 6,682K。" },
            { text: "C output batching 沒有打贏 batch size 1，global output atomics 不是主因。" },
            { text: "Q2 naive request batching 會放大 ready-spin，timestamp duration 變差。" },
            { text: "Wave/request batching 不屬於目前 main optimized path。", bold: true, color: C.danger },
          ],
          { name: "rejected-bullets", size: 32, gap: 30 },
        ),
        callout("本週採用路徑保持簡單：sharding + shard selection strategy。", {
          fill: C.highlight2,
          color: C.accent2,
          size: 37,
          ruleHeight: 76,
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "Adopted default",
    body: grid(
      {
        width: fill,
        height: fill,
        columns: [fr(0.9), fr(1.1)],
        columnGap: 46,
      },
      [
        column(
          { width: fill, height: fill, gap: 30, justify: "between" },
          [
            metricLine([
              { value: "256", label: "Q1 default shards", fill: C.highlight2, color: C.accent2 },
              { value: "256", label: "Q2 default shards", fill: C.highlight2, color: C.accent2 },
            ]),
            bulletList(
              [
                { text: "MAX_QUEUE_SHARDS=256" },
                { text: "Q1_QUEUE_SHARDS_DEFAULT=256" },
                { text: "Q2_QUEUE_SHARDS_DEFAULT=256" },
                { text: "Q1 lane-pop enabled by default" },
                { text: "NODE_C_START=72, meaning C=24/B=72" },
              ],
              { name: "default-bullets", size: 30, gap: 26 },
            ),
          ],
        ),
        dataTable({
          name: "repeat-table",
          columns: ["Run", "Median", "P10", "P90", "Min"],
          widths: [170, 170, 170, 170, 170],
          fontSize: 28,
          headerSize: 26,
          rowGap: 18,
          rows: [
            ["repeat 1", "0.9905", "0.9804", "1.0043", "0.9668"],
            ["repeat 2", "0.9915", "0.9819", "1.0056", "0.9682"],
            ["repeat 3", "0.9906", "0.9810", "1.0046", "0.9593"],
            ["repeat 4", "0.9902", "0.9806", "1.0042", "0.9615"],
          ],
        }),
      ],
    ),
  });

  addSlide(prs, {
    title: "本週結論 / Next",
    body: column(
      { width: fill, height: fill, gap: 36, justify: "between" },
      [
        bulletList(
          [
            { text: "RGP Wavefront occupancy 在本輪 capture 中不能單獨作為 dispatch lifetime 證據。" },
            { text: "duration 判斷已改以 app 端 GPU timestamp 為準。" },
            { text: "Q1 sharding、Q2 sharding、Q1 lane-pop 是本週主要有效優化。" },
            { text: "最後採用的 main path 是 sharding + shard selection strategy，不是 wave/request batching。", bold: true, color: C.accent2 },
            { text: "後續優化以 Q1/Q2=256, Q1 lane-pop on, C=24/B=72 作為起點。" },
          ],
          { name: "next-bullets", size: 34, gap: 30 },
        ),
        callout("下一輪實驗固定從 adopted default 起跑；舊路徑只保留作回歸對照。", {
          size: 39,
          color: C.accent2,
          ruleHeight: 78,
        }),
        metricLine([
          { value: "Q1/Q2 256", label: "default shard count", fill: C.highlight2, color: C.accent2 },
          { value: "lane-pop on", label: "default shard selection", fill: C.highlight },
          { value: "0.99 ms", label: "median compute", fill: C.highlight2, color: C.accent2 },
        ]),
      ],
    ),
  });

  return prs;
}

async function saveBlob(blob, filePath) {
  await fs.writeFile(filePath, Buffer.from(await blob.arrayBuffer()));
}

async function buildMontage(pngPaths, outPath) {
  const cols = 3;
  const thumbW = 500;
  const thumbH = 281;
  const pad = 34;
  const labelH = 28;
  const gapX = 34;
  const gapY = 42;
  const rows = Math.ceil(pngPaths.length / cols);
  const canvas = new Canvas(
    pad * 2 + cols * thumbW + (cols - 1) * gapX,
    pad * 2 + rows * (thumbH + labelH) + (rows - 1) * gapY,
  );
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.font = "18px Consolas";
  ctx.fillStyle = "#111827";
  ctx.strokeStyle = "#9CA3AF";
  ctx.lineWidth = 1;
  for (let i = 0; i < pngPaths.length; i += 1) {
    const img = await loadImage(pngPaths[i]);
    const col = i % cols;
    const rowIdx = Math.floor(i / cols);
    const x = pad + col * (thumbW + gapX);
    const y = pad + rowIdx * (thumbH + labelH + gapY);
    const label = `slide_${String(i + 1).padStart(2, "0")}.png`;
    ctx.fillText(label, x, y + 18);
    ctx.drawImage(img, x, y + labelH, thumbW, thumbH);
    ctx.strokeRect(x, y + labelH, thumbW, thumbH);
  }
  await fs.writeFile(outPath, await canvas.toBuffer("png"));
}

await fs.mkdir(OUT_DIR, { recursive: true });
await prepareRgpAssets();
await fs.rm(VERIFY_DIR, { recursive: true, force: true });
await fs.mkdir(VERIFY_DIR, { recursive: true });

const presentation = buildDeck();
const pptxBlob = await PresentationFile.exportPptx(presentation);
await pptxBlob.save(PPTX_PATH);

const savedPresentation = await PresentationFile.importPptx(await FileBlob.load(PPTX_PATH));
const slides = savedPresentation.slides.items;
const pngPaths = [];
for (let i = 0; i < slides.length; i += 1) {
  const pngPath = path.join(VERIFY_DIR, `slide_${String(i + 1).padStart(2, "0")}.png`);
  const pngBlob = await slides[i].export({ format: "png" });
  await saveBlob(pngBlob, pngPath);
  pngPaths.push(pngPath);
}
await buildMontage(pngPaths, path.join(VERIFY_DIR, "montage.png"));

console.log(`Exported PPTX: ${PPTX_PATH}`);
console.log(`Rendered ${pngPaths.length} PNG previews from saved PPTX: ${VERIFY_DIR}`);
