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
  image,
  rule,
  fill,
  hug,
  fixed,
  fr,
} = await import(pathToFileURL(artifactEntry).href);
const { Canvas, loadImage } = await import(pathToFileURL(skiaEntry).href);

const W = 1920;
const H = 1080;

const OUT_DIR = path.join(repoRoot, "reports", "2026-05", "monthly", "decks");
const VERIFY_DIR = path.join(
  repoRoot,
  "reports",
  "2026-05",
  "monthly",
  "verification",
  "260525_optimization_evaluation",
);
const PPTX_PATH = path.join(
  OUT_DIR,
  "260525_MTK_WorkGraph_VulkanPersistentThread_POC_OptimizationEvaluation.pptx",
);
const ASSET_DIR = path.join(VERIFY_DIR, "assets");
const RGP_IMAGE = path.join(
  repoRoot,
  "reports",
  "2026-05",
  "weekly",
  "assets",
  "weekly_report_260511",
  "rgp_wavefront_only_crop.png",
);

const C = {
  bg: "#F6F3EA",
  panel: "#FFFDF7",
  ink: "#14211D",
  body: "#30433D",
  muted: "#66756F",
  line: "#D7D0C4",
  teal: "#0F766E",
  tealDark: "#0B4F49",
  tealPale: "#DDEEEA",
  copper: "#B76322",
  copperPale: "#F5DDC6",
  red: "#9A3412",
  green: "#2F7D4A",
  dark: "#1F2C27",
  white: "#FFFFFF",
};

const baseStyle = {
  typeface: "Microsoft JhengHei",
  color: C.body,
};

function tx(value, options = {}) {
  return text(value, {
    name: options.name,
    width: options.width ?? fill,
    height: options.height ?? hug,
    columnSpan: options.columnSpan,
    rowSpan: options.rowSpan,
    style: {
      ...baseStyle,
      fontSize: options.size ?? 28,
      bold: options.bold ?? false,
      color: options.color ?? C.body,
      alignment: options.align,
      lineSpacing: options.lineSpacing,
    },
  });
}

function badge(value, options = {}) {
  return panel(
    {
      name: options.name,
      width: fixed(options.width ?? 160),
      height: fixed(options.height ?? 46),
      fill: options.fill ?? C.teal,
      padding: { x: 14, y: 8 },
    },
    tx(value, {
      size: options.size ?? 19,
      bold: true,
      color: options.color ?? C.white,
      align: "center",
    }),
  );
}

function metricCard(value, label, options = {}) {
  return panel(
    {
      name: options.name,
      width: fill,
      height: fixed(options.height ?? 150),
      fill: options.fill ?? C.panel,
      padding: { x: 22, y: 20 },
    },
    column(
      { width: fill, height: fill, gap: 8, justify: "center" },
      [
        tx(value, {
          size: options.valueSize ?? 48,
          bold: true,
          color: options.color ?? C.teal,
          align: "center",
        }),
        tx(label, {
          size: options.labelSize ?? 20,
          color: options.labelColor ?? C.muted,
          align: "center",
        }),
      ],
    ),
  );
}

function bulletList(items, options = {}) {
  return column(
    { name: options.name, width: fill, height: hug, gap: options.gap ?? 20 },
    items.map((item, idx) =>
      row(
        {
          name: `${options.name ?? "bullet"}-${idx}`,
          width: fill,
          height: hug,
          gap: 12,
          align: "center",
        },
        [
          tx("•", {
            width: fixed(26),
            size: item.size ?? options.size ?? 27,
            color: item.color ?? options.dotColor ?? C.copper,
            bold: true,
          }),
          tx(item.text, {
            size: item.size ?? options.size ?? 27,
            color: item.color ?? options.color ?? C.body,
            bold: item.bold ?? false,
            width: fill,
            lineSpacing: options.lineSpacing,
          }),
        ],
      ),
    ),
  );
}

function callout(value, options = {}) {
  return panel(
    {
      name: options.name,
      width: fill,
      height: fixed(options.height ?? 86),
      fill: options.fill ?? C.tealPale,
      padding: { x: 24, y: 18 },
    },
    row(
      { width: fill, height: fill, gap: 16, align: "center" },
      [
        panel({
          width: fixed(8),
          height: fixed(options.barHeight ?? 48),
          fill: options.barColor ?? C.teal,
        }),
        tx(value, {
          size: options.size ?? 28,
          bold: options.bold ?? true,
          color: options.color ?? C.ink,
        }),
      ],
    ),
  );
}

function slide(prs, title, body, options = {}) {
  const s = prs.slides.add();
  const page = String(prs.slides.count).padStart(2, "0");
  s.compose(
    panel(
      {
        name: `slide-${page}-bg`,
        width: fill,
        height: fill,
        fill: options.dark ? C.dark : C.bg,
        padding: { x: 62, y: 42 },
      },
      column(
        { width: fill, height: fill, gap: 18 },
        [
          row(
            { width: fill, height: fixed(92), align: "center", gap: 20 },
            [
              column(
                { width: fill, height: hug, gap: 4 },
                [
                  tx(options.eyebrow ?? "Vulkan Persistent Thread WorkGraph POC", {
                    size: 16,
                    bold: true,
                    color: options.dark ? "#A8DAD4" : C.teal,
                  }),
                  tx(title, {
                    size: options.titleSize ?? 42,
                    bold: true,
                    color: options.dark ? C.white : C.ink,
                  }),
                ],
              ),
              badge(page, {
                width: 58,
                height: 44,
                fill: options.dark ? C.copper : C.teal,
                size: 18,
              }),
            ],
          ),
          panel(
            {
              width: fill,
              height: fill,
              fill: options.dark ? C.dark : C.bg,
            },
            body,
          ),
          row(
            { width: fill, height: fixed(18), align: "center", gap: 12 },
            [
              tx("2026 / 05 / 25", {
                width: fixed(180),
                size: 14,
                color: options.dark ? "#B7C8C2" : C.muted,
              }),
              tx("Optimization Evaluation", {
                size: 14,
                color: options.dark ? "#B7C8C2" : C.muted,
                align: "right",
              }),
            ],
          ),
        ],
      ),
    ),
    { frame: { left: 0, top: 0, width: W, height: H }, baseUnit: 8 },
  );
}

function table({ name, columns, rows, widths, fontSize = 24, headerSize = 22, rowGap = 9 }) {
  const cols = widths.map((w) => fixed(w));
  const makeRow = (cells, idx, header = false) =>
    grid(
      {
        name: `${name}-row-${idx}`,
        width: fill,
        height: hug,
        columns: cols,
        columnGap: 18,
        padding: { y: 4 },
      },
      cells.map((cell, cellIdx) =>
        tx(cell, {
          name: `${name}-${idx}-${cellIdx}`,
          size: header ? headerSize : fontSize,
          bold: header,
          color: header ? C.tealDark : C.body,
        }),
      ),
    );

  const children = [makeRow(columns, "h", true), rule({ stroke: C.line, weight: 2 })];
  rows.forEach((r, idx) => {
    children.push(makeRow(r, idx));
    if (idx !== rows.length - 1) {
      children.push(rule({ stroke: "#E7E1D8", weight: 1 }));
    }
  });
  return column({ name, width: fill, height: hug, gap: rowGap }, children);
}

function evidence({ problem, attempt, result, read }) {
  const rows = [
    ["問題", problem, C.copper],
    ["嘗試", attempt, C.teal],
    ["結果", result, C.green],
    ["解讀", read, C.red],
  ];
  return column(
    { width: fill, height: hug, gap: 15 },
    rows.map(([label, value, color], idx) =>
      row(
        { width: fill, height: hug, gap: 15, align: "center" },
        [
          badge(label, {
            name: `evidence-badge-${idx}`,
            width: 80,
            height: 38,
            fill: color,
            size: 17,
          }),
          tx(value, { size: 25, color: C.body }),
        ],
      ),
    ),
  );
}

function flowNodes(items, options = {}) {
  const children = [];
  items.forEach((item, idx) => {
    children.push(
      panel(
        {
          width: fixed(options.nodeWidth ?? 205),
          height: fixed(options.nodeHeight ?? 82),
          fill: item.fill ?? C.panel,
          padding: { x: 16, y: 12 },
        },
        column(
          { width: fill, height: fill, gap: 4, justify: "center" },
          [
            tx(item.title, {
              size: item.titleSize ?? 25,
              bold: true,
              color: item.color ?? C.ink,
              align: "center",
            }),
            item.sub
              ? tx(item.sub, {
                  size: item.subSize ?? 16,
                  color: C.muted,
                  align: "center",
                })
              : tx("", { size: 1 }),
          ],
        ),
      ),
    );
    if (idx !== items.length - 1) {
      children.push(tx("→", { width: fixed(32), size: 34, color: C.copper, bold: true, align: "center" }));
    }
  });
  return row({ width: fill, height: hug, gap: 8, align: "center" }, children);
}

async function createLineChart(outPath) {
  const labels = ["16", "32", "64", "128", "192", "256", "384", "512"];
  const values = [3.86, 2.4, 1.54, 1.14, 1.08, 0.99, 1.05, 1.12];
  const canvas = new Canvas(1120, 430);
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = C.panel;
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  const left = 78;
  const right = 42;
  const top = 46;
  const bottom = 74;
  const chartW = canvas.width - left - right;
  const chartH = canvas.height - top - bottom;
  const min = 0.8;
  const max = 4.0;
  const x = (i) => left + (chartW * i) / (labels.length - 1);
  const y = (v) => top + chartH - ((v - min) / (max - min)) * chartH;

  ctx.strokeStyle = "#E1D9CD";
  ctx.lineWidth = 1;
  ctx.font = "18px Microsoft JhengHei";
  ctx.fillStyle = C.muted;
  for (let tick = 1; tick <= 4; tick += 1) {
    const ty = y(tick);
    ctx.beginPath();
    ctx.moveTo(left, ty);
    ctx.lineTo(left + chartW, ty);
    ctx.stroke();
    ctx.fillText(`${tick}.0`, 18, ty + 6);
  }

  ctx.strokeStyle = C.teal;
  ctx.lineWidth = 5;
  ctx.beginPath();
  values.forEach((v, i) => {
    if (i === 0) ctx.moveTo(x(i), y(v));
    else ctx.lineTo(x(i), y(v));
  });
  ctx.stroke();

  values.forEach((v, i) => {
    ctx.fillStyle = i === 5 ? C.copper : C.teal;
    ctx.beginPath();
    ctx.arc(x(i), y(v), i === 5 ? 9 : 7, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = C.ink;
    ctx.font = i === 5 ? "bold 20px Microsoft JhengHei" : "18px Microsoft JhengHei";
    ctx.fillText(v.toFixed(2), x(i) - 24, y(v) - 18);
    ctx.fillStyle = C.muted;
    ctx.font = "17px Microsoft JhengHei";
    ctx.fillText(labels[i], x(i) - 12, canvas.height - 30);
  });

  ctx.fillStyle = C.ink;
  ctx.font = "bold 24px Microsoft JhengHei";
  ctx.fillText("Median compute ms by Q1/Q2 shard count", left, 28);
  await fs.writeFile(outPath, await canvas.toBuffer("png"));
}

function imageFromFile(file, options = {}) {
  const dataUrl = `data:image/png;base64,${readFileSync(file).toString("base64")}`;
  return image({
    name: options.name,
    dataUrl,
    contentType: "image/png",
    width: options.width ?? fill,
    height: fixed(options.height ?? 360),
    fit: options.fit ?? "contain",
    alt: options.alt ?? path.basename(file),
  });
}

function cover(prs) {
  const s = prs.slides.add();
  s.compose(
    panel(
      {
        width: fill,
        height: fill,
        fill: C.dark,
        padding: { x: 92, y: 80 },
      },
      column(
        { width: fill, height: fill, gap: 34, justify: "center" },
        [
          badge("Monthly Report", { width: 220, height: 48, fill: C.copper }),
          tx("Vulkan Persistent Thread\nWorkGraph POC", {
            size: 76,
            bold: true,
            color: C.white,
            lineSpacing: 0.9,
          }),
          tx("5 月優化歷程：從 queue bottleneck 到 global atomic traffic 結論", {
            size: 36,
            bold: true,
            color: "#B7E3DC",
          }),
          grid(
            { width: fixed(1200), height: hug, columns: [fr(1), fr(1), fr(1)], columnGap: 22 },
            [
              metricCard("30.35 ms", "baseline avg compute", {
                fill: "#273A34",
                color: "#FFD0A8",
                labelColor: "#C7D3CE",
              }),
              metricCard("0.99 ms", "main path median compute", {
                fill: "#273A34",
                color: "#A8E6DC",
                labelColor: "#C7D3CE",
              }),
              metricCard("Wave-local", "next architecture direction", {
                fill: "#273A34",
                color: "#FFFFFF",
                labelColor: "#C7D3CE",
              }),
            ],
          ),
          tx("AMD RX 7900 XTX / RDNA 3 · 2026 / 05 / 25", {
            size: 22,
            color: "#C7D3CE",
          }),
        ],
      ),
    ),
    { frame: { left: 0, top: 0, width: W, height: H }, baseUnit: 8 },
  );
}

function buildDeck(chartPath) {
  const prs = Presentation.create({ slideSize: { width: W, height: H } });

  cover(prs);

  slide(
    prs,
    "Recap：上次已完成 correctness-first POC",
    column(
      { width: fill, height: fill, gap: 34, justify: "between" },
      [
        flowNodes([
          { title: "Node A", sub: "Seed", fill: C.tealPale, color: C.tealDark },
          { title: "Q1", sub: "global queue", fill: C.copperPale, color: C.red },
          { title: "Node B", sub: "Subdivide", fill: C.tealPale, color: C.tealDark },
          { title: "Q2", sub: "global queue", fill: C.copperPale, color: C.red },
          { title: "Node C", sub: "Write VB", fill: C.tealPale, color: C.tealDark },
          { title: "VB", sub: "Render", fill: C.panel, color: C.ink },
        ]),
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1)], columnGap: 34 },
          [
            bulletList(
              [
                { text: "單一 compute dispatch 內，用 persistent threads 模擬 D3D Work Graph producer-consumer network。" },
                { text: "Koch Snowflake recursive workload 可 deterministic 輸出。" },
                { text: "跨 workgroup handoff 使用 atomic ready flag，GPU-side termination 已能完成。" },
              ],
              { name: "recap-ok", size: 29, gap: 26 },
            ),
            panel(
              { width: fill, height: fixed(265), fill: C.panel, padding: { x: 30, y: 26 } },
              column(
                { width: fill, height: fill, gap: 16, justify: "center" },
                [
                  tx("上次結論", { size: 28, bold: true, color: C.copper }),
                  tx("功能已經跑通，但 performance 還在調查中。", {
                    size: 34,
                    bold: true,
                    color: C.ink,
                  }),
                  tx("本月重點：完整追 queue bottleneck 的演進。", {
                    size: 26,
                    color: C.body,
                  }),
                ],
              ),
            ),
          ],
        ),
        callout("這份月報的重點是完整交代 5 月優化歷程，不是只報一組快參數。"),
      ],
    ),
  );

  slide(
    prs,
    "Recap：上次 profile 留下的問題",
    grid(
      { width: fill, height: fill, columns: [fr(1), fr(1), fr(1)], columnGap: 26 },
      [
        panel(
          { width: fill, height: fixed(620), fill: C.panel, padding: { x: 24, y: 26 } },
          column(
            { width: fill, height: fill, gap: 20 },
            [
              badge("01", { width: 70, fill: C.copper }),
              tx("Q1 / Q2 queue 的 atomic contention 是否主導？", {
                size: 34,
                bold: true,
                color: C.ink,
              }),
              tx("如果是，分散 queue hot spot 應該有明顯效果。", { size: 25 }),
            ],
          ),
        ),
        panel(
          { width: fill, height: fixed(620), fill: C.panel, padding: { x: 24, y: 26 } },
          column(
            { width: fill, height: fill, gap: 20 },
            [
              badge("02", { width: 70, fill: C.teal }),
              tx("Node B / Node C worker ratio 是否失衡？", {
                size: 34,
                bold: true,
                color: C.ink,
              }),
              tx("如果 C writers 不足，Q2 backlog 會推高 enqueue 壓力。", { size: 25 }),
            ],
          ),
        ),
        panel(
          { width: fill, height: fixed(620), fill: C.panel, padding: { x: 24, y: 26 } },
          column(
            { width: fill, height: fill, gap: 20 },
            [
              badge("03", { width: 70, fill: C.red }),
              tx("ready-spin、output writes、queue sizing 是否是主因？", {
                size: 34,
                bold: true,
                color: C.ink,
              }),
              tx("本月用 ablation 一項一項排除。", { size: 25 }),
            ],
          ),
        ),
      ],
    ),
  );

  slide(
    prs,
    "本週成果：整理與整合",
    column(
      { width: fill, height: fill, gap: 30, justify: "between" },
      [
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1), fr(1)], columnGap: 24 },
          [
            metricCard("完整 timeline", "B/C -> Q1 -> Q2 -> lane-pop -> high shards", {
              valueSize: 42,
              color: C.copper,
            }),
            metricCard("Adopted default", "Q1/Q2=256, lane-pop on, C=24/B=72", {
              valueSize: 42,
              color: C.teal,
            }),
            metricCard("0.99 ms", "main path median compute", {
              valueSize: 54,
              color: C.green,
            }),
          ],
        ),
        bulletList(
          [
            { text: "整理 5 月完整優化 timeline，將每一輪問題、嘗試、結果、解讀串起來。" },
            { text: "整合目前主線預設，保留 shard flags 作為後續 ablation controls。" },
            { text: "排除 output writes、Q2 ready-first、C output batching 等非主因。" },
            { text: "request batching 目前不納入主線；另派實驗補強資料。" },
            { text: "收斂結論：有效優化幾乎都在降低 queue/global atomic traffic。", bold: true, color: C.tealDark },
          ],
          { name: "week-summary", size: 31, gap: 25 },
        ),
        callout("本週不是新增一個神奇參數，而是把整個月的 evidence chain 收斂清楚。"),
      ],
    ),
  );

  slide(
    prs,
    "Bottleneck timeline：瓶頸演進路徑",
    column(
      { width: fill, height: fill, gap: 24, justify: "between" },
      [
        table({
          name: "timeline-table",
          columns: ["階段", "設定", "Compute", "解讀"],
          widths: [270, 650, 210, 420],
          fontSize: 22,
          headerSize: 21,
          rowGap: 8,
          rows: [
            ["Baseline", "Q1 batch on, Q2 scalar, C=24/B=72", "30.35 ms", "queue 壓力很高"],
            ["B/C ratio", "C=40/B=56", "27.97 ms", "Q2 backlog 降低"],
            ["Q1 sharding", "Q1s16/Q2s1, C=38/B=58", "13.70 ms", "Q1 hot atomic 被分散"],
            ["Q2 sharding", "Q1s16/Q2s16, C=16/B=80", "5.57 ms", "Q2 hot atomic 被分散"],
            ["Q1 lane-pop", "Q1s16/Q2s16, C=24/B=72", "3.84 ms", "shard 起掃策略改善"],
            ["High shards", "Q1s256/Q2s256, C=24/B=72", "0.99 ms", "global contention 大幅下降"],
          ],
        }),
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1), fr(1)], columnGap: 22 },
          [
            metricCard("30.35 ms", "baseline", { color: C.red }),
            metricCard("3.84 ms", "16-shard + lane-pop", { color: C.copper }),
            metricCard("0.99 ms", "256-shard default", { color: C.green }),
          ],
        ),
        callout("重點：每次大幅改善，都對應到 queue atomic traffic 被分散或減少。"),
      ],
    ),
  );

  slide(
    prs,
    "RDP/RGP occupancy validation",
    grid(
      { width: fill, height: fill, columns: [fr(1.15), fr(0.85)], columnGap: 34 },
      [
        column(
          { width: fill, height: fill, gap: 14 },
          [
            tx("Wavefront-only RGP crop", { size: 24, bold: true, color: C.tealDark }),
            imageFromFile(RGP_IMAGE, { height: 525, alt: "RGP wavefront occupancy crop" }),
            tx("這張圖可以輔助觀察，但不能單獨當因果證據。", {
              size: 20,
              color: C.muted,
            }),
          ],
        ),
        column(
          { width: fill, height: fill, gap: 24, justify: "center" },
          [
            tx("wavefront = 0\n不能直接推論 shader 已結束", {
              size: 40,
              bold: true,
              color: C.ink,
              lineSpacing: 0.95,
            }),
            bulletList(
              [
                { text: "RGP occupancy 圖保留作輔助。" },
                { text: "ALU-only probe 也出現相似型態。" },
                { text: "後續判斷主要依據：compute time 與 queue counters trend。", bold: true, color: C.tealDark },
              ],
              { name: "rgp-bullets", size: 27, gap: 24 },
            ),
            callout("這頁只交代 profiler 解讀邊界，不展開細節。", {
              size: 25,
              height: 80,
            }),
          ],
        ),
      ],
    ),
  );

  slide(
    prs,
    "優化 1：B/C ratio",
    grid(
      { width: fill, height: fill, columns: [fr(1), fr(0.9)], columnGap: 42 },
      [
        column(
          { width: fill, height: fill, gap: 28, justify: "between" },
          [
            evidence({
              problem: "Node C writer 是否不足，造成 Q2 backlog？",
              attempt: "固定總 workgroups，掃 C/B worker ratio。",
              result: "C=40/B=56 最佳，avg compute 到 27.97 ms。",
              read: "writer 數量影響 backlog，但真正的大改善仍要處理 queue atomic contention。",
            }),
            grid(
              { width: fill, height: hug, columns: [fr(1), fr(1)], columnGap: 20 },
              [
                metricCard("50k -> 9k", "Q2 high-water", { color: C.copper }),
                metricCard("8.5%", "約略改善幅度", { color: C.teal }),
              ],
            ),
            callout("B/C ratio 是第一個訊號：queue backlog 會放大 atomic traffic。", {
              size: 26,
            }),
          ],
        ),
        table({
          name: "bc-table",
          columns: ["C", "B", "Avg compute"],
          widths: [115, 115, 230],
          fontSize: 29,
          headerSize: 25,
          rowGap: 15,
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
  );

  slide(
    prs,
    "優化 2：Q1 sharding",
    grid(
      { width: fill, height: fill, columns: [fr(1), fr(0.95)], columnGap: 42 },
      [
        column(
          { width: fill, height: fill, gap: 28, justify: "between" },
          [
            evidence({
              problem: "Q1 enqueue/dequeue contention 仍然明顯。",
              attempt: "把 Q1 global queue 拆成多個 shards，再重新掃 B/C。",
              result: "Q1s16/Q2s1, C=38/B=58 到 13.70 ms avg。",
              read: "Q1 hot atomic 被分散後改善很大，接著 Q2 壓力自然浮現。",
            }),
            flowNodes(
              [
                { title: "Q1", sub: "single hot queue", fill: C.copperPale, color: C.red },
                { title: "Q1 shards", sub: "16 queues", fill: C.tealPale, color: C.tealDark },
              ],
              { nodeWidth: 300 },
            ),
            callout("修掉 Q1 後，下一個瓶頸不是結束，而是 Q2 counters 變得更明顯。"),
          ],
        ),
        table({
          name: "q1-table",
          columns: ["Q1 shards", "C/B", "Avg"],
          widths: [190, 165, 190],
          fontSize: 28,
          headerSize: 24,
          rowGap: 14,
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
  );

  slide(
    prs,
    "優化 3：Q2 sharding",
    grid(
      { width: fill, height: fill, columns: [fr(0.9), fr(1)], columnGap: 42 },
      [
        table({
          name: "q2-table",
          columns: ["Q2 shards", "Avg compute"],
          widths: [220, 260],
          fontSize: 30,
          headerSize: 25,
          rowGap: 17,
          rows: [
            ["1", "16.95 ms"],
            ["2", "10.24 ms"],
            ["4", "8.38 ms"],
            ["8", "6.87 ms"],
            ["16", "6.08 ms"],
          ],
        }),
        column(
          { width: fill, height: fill, gap: 28, justify: "between" },
          [
            evidence({
              problem: "Q1 sharded 後，Q2 global count/head/tail 成為主要壓力點。",
              attempt: "加入 Q2 shards，並在 Q2 壓力下降後重掃 B/C。",
              result: "Q1s16/Q2s16, C=16/B=80 到 5.57 ms avg。",
              read: "第二個大改善仍然是分散 global queue atomic。",
            }),
            grid(
              { width: fill, height: hug, columns: [fr(1), fr(1)], columnGap: 20 },
              [
                metricCard("47.8x / 255.9x", "Q2 enq/deq fail ratio", { valueSize: 34, color: C.red }),
                metricCard("68k -> 1.5k", "Q2 high-water", { valueSize: 40, color: C.teal }),
              ],
            ),
            callout("Q2 sharding 是第二個主要結構性優化。"),
          ],
        ),
      ],
    ),
  );

  slide(
    prs,
    "優化 4：Q1 lane-pop",
    column(
      { width: fill, height: fill, gap: 26, justify: "between" },
      [
        evidence({
          problem: "Q1/Q2 都 16 shards 後，同一 workgroup 內 lane 仍可能從同一 shard 開始 pop。",
          attempt: "把 Q1 pop 起掃點從 workgroup-level 改成 lane-level。",
          result: "lane-pop on, C=24/B=72 時 median compute 到 3.84 ms。",
          read: "這不是 request batching，而是 shard selection / scan-start strategy。",
        }),
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1)], columnGap: 22 },
          [
            callout("Old: wgId % Q1_QUEUE_SHARDS", { fill: C.copperPale, barColor: C.copper, size: 27 }),
            callout("New: (wgId * local_size_x + localId) % Q1_QUEUE_SHARDS", {
              fill: C.tealPale,
              barColor: C.teal,
              size: 24,
            }),
          ],
        ),
        table({
          name: "lanepop-table",
          columns: ["Mode", "Avg", "Median", "Min"],
          widths: [690, 200, 200, 200],
          fontSize: 25,
          headerSize: 23,
          rowGap: 11,
          rows: [
            ["lane-pop off, C=16/B=80", "6.01 ms", "5.60 ms", "5.54 ms"],
            ["lane-pop on, C=16/B=80", "4.39 ms", "4.00 ms", "3.90 ms"],
            ["lane-pop on, C=24/B=72", "3.85 ms", "3.84 ms", "3.73 ms"],
          ],
        }),
      ],
    ),
  );

  slide(
    prs,
    "優化 5：High shard scaling",
    grid(
      { width: fill, height: fill, columns: [fr(1.2), fr(0.8)], columnGap: 34 },
      [
        column(
          { width: fill, height: fill, gap: 14 },
          [
            imageFromFile(chartPath, { height: 495, alt: "High shard scaling line chart" }),
            table({
              name: "high-shard-table",
              columns: ["16", "64", "128", "256", "512"],
              widths: [145, 145, 145, 145, 145],
              fontSize: 24,
              headerSize: 22,
              rowGap: 8,
              rows: [["3.86", "1.54", "1.14", "0.99", "1.12"]],
            }),
          ],
        ),
        column(
          { width: fill, height: fill, gap: 28, justify: "between" },
          [
            evidence({
              problem: "16 shards + lane-pop 後，剩餘成本仍追著 queue contention 走。",
              attempt: "掃 Q1/Q2 shard count 到 512，並做 asymmetric 檢查。",
              result: "Q1s256/Q2s256, C=24/B=72 約 0.99 ms median。",
              read: "256 接近目前架構有效區間，再加 shard 收益開始被 probing / indexing overhead 抵銷。",
            }),
            metricCard("256 / 256", "best default candidate", {
              height: 160,
              valueSize: 54,
              color: C.teal,
            }),
            callout("Q1=256/Q2=128 退到 1.955 ms：Q2 shard count 不能太低。", {
              size: 24,
            }),
          ],
        ),
      ],
    ),
  );

  slide(
    prs,
    "未採用方案 / 待補驗證",
    column(
      { width: fill, height: fill, gap: 28, justify: "between" },
      [
        table({
          name: "ablation-table",
          columns: ["方案", "結果", "目前判斷"],
          widths: [380, 430, 650],
          fontSize: 24,
          headerSize: 22,
          rowGap: 11,
          rows: [
            ["Output writes off", "3.86 -> 3.78 ms", "不是主瓶頸"],
            ["Q2 ready-first pop", "3.86 -> 5.06 ms", "更慢，ready fail / backlog 上升"],
            ["C output batching", "沒有贏過 batch=1", "global output atomics 不是主因"],
            ["Q2 request batching", "舊資料不足", "不納入主線，另派 controlled experiment 補驗證"],
          ],
        }),
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1), fr(1)], columnGap: 20 },
          [
            metricCard("同一架構", "batching on/off 才能比較", { valueSize: 34, color: C.teal }),
            metricCard("3 repeats+", "看 median / p90 / correctness", { valueSize: 34, color: C.copper }),
            metricCard("不只看 CAS", "compute time 不改善就不算有效", { valueSize: 34, color: C.red }),
          ],
        ),
        callout("這頁避免過度宣稱：request batching 目前只是未採用，還需要補跑確認。", {
          size: 27,
        }),
      ],
    ),
  );

  slide(
    prs,
    "Adopted default：目前主線設定",
    grid(
      { width: fill, height: fill, columns: [fr(0.9), fr(1.1)], columnGap: 42 },
      [
        column(
          { width: fill, height: fill, gap: 24, justify: "between" },
          [
            grid(
              { width: fill, height: hug, columns: [fr(1), fr(1)], columnGap: 20 },
              [
                metricCard("256", "Q1 shards", { valueSize: 64, color: C.teal }),
                metricCard("256", "Q2 shards", { valueSize: 64, color: C.teal }),
              ],
            ),
            bulletList(
              [
                { text: "MAX_QUEUE_SHARDS = 256" },
                { text: "Q1_QUEUE_SHARDS_DEFAULT = 256" },
                { text: "Q2_QUEUE_SHARDS_DEFAULT = 256" },
                { text: "Q1 lane-pop enabled by default" },
                { text: "NODE_C_START = 72，也就是 C=24/B=72" },
              ],
              { name: "default-list", size: 27, gap: 20 },
            ),
            callout("保留 queue shard flags 作為 future ablation-study controls。", {
              size: 25,
            }),
          ],
        ),
        table({
          name: "repeat-table",
          columns: ["Run", "Median", "P10", "P90", "Min"],
          widths: [170, 170, 170, 170, 170],
          fontSize: 25,
          headerSize: 23,
          rowGap: 13,
          rows: [
            ["repeat 1", "0.9905", "0.9804", "1.0043", "0.9668"],
            ["repeat 2", "0.9915", "0.9819", "1.0056", "0.9682"],
            ["repeat 3", "0.9906", "0.9810", "1.0046", "0.9593"],
            ["repeat 4", "0.9902", "0.9806", "1.0042", "0.9615"],
          ],
        }),
      ],
    ),
  );

  slide(
    prs,
    "核心結論：瓶頸分類是 global atomic traffic",
    column(
      { width: fill, height: fill, gap: 30, justify: "between" },
      [
        tx("我們不是只找到一組比較快的參數。", {
          size: 42,
          bold: true,
          color: C.ink,
        }),
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1), fr(1), fr(1)], columnGap: 18 },
          [
            metricCard("Q1", "sharding 分散 Q1 atomic contention", { valueSize: 52, color: C.copper }),
            metricCard("Q2", "sharding 分散 count/head/tail contention", { valueSize: 52, color: C.teal }),
            metricCard("lane", "pop 起掃策略降低局部競爭", { valueSize: 52, color: C.green }),
            metricCard("256", "high shard scaling 繼續降低 contention", { valueSize: 52, color: C.red }),
          ],
        ),
        bulletList(
          [
            { text: "每一輪大幅改善，都對應到 queue/global atomic traffic 被分散或減少。" },
            { text: "output writes off 效果小、ready-first 變慢、C output batching 無明確收益。" },
            { text: "所以目前瓶頸分類不是 render、不是單純 writer 數量，而是 global handoff traffic。", bold: true, color: C.tealDark },
          ],
          { name: "conclusion-bullets", size: 31, gap: 24 },
        ),
        callout("這個結論來自一次次迭代的效果幅度，而不是單一 counter 或單一 profiler 截圖。"),
      ],
    ),
  );

  slide(
    prs,
    "下一步：wave-local work retention",
    grid(
      { width: fill, height: fill, columns: [fr(1), fr(1)], columnGap: 34 },
      [
        panel(
          { width: fill, height: fill, fill: C.panel, padding: { x: 28, y: 28 } },
          column(
            { width: fill, height: fill, gap: 24, justify: "center" },
            [
              badge("目前架構", { width: 140, fill: C.copper }),
              flowNodes(
                [
                  { title: "WG", sub: "producer", fill: C.tealPale },
                  { title: "global Q", sub: "atomic", fill: C.copperPale, color: C.red },
                  { title: "WG", sub: "consumer", fill: C.tealPale },
                ],
                { nodeWidth: 150, nodeHeight: 86 },
              ),
              bulletList(
                [
                  { text: "溝通依賴 global memory。" },
                  { text: "即使 sharding，仍然需要 global atomic。" },
                  { text: "256 shards 已接近目前架構有效區間。" },
                ],
                { name: "current-arch", size: 24, gap: 18 },
              ),
            ],
          ),
        ),
        panel(
          { width: fill, height: fill, fill: C.tealPale, padding: { x: 28, y: 28 } },
          column(
            { width: fill, height: fill, gap: 24, justify: "center" },
            [
              badge("要驗證的方向", { width: 170, fill: C.teal }),
              flowNodes(
                [
                  { title: "Wave", sub: "local work", fill: C.panel, color: C.tealDark },
                  { title: "intrinsic", sub: "wave-level", fill: C.panel, color: C.tealDark },
                  { title: "spill Q", sub: "fallback", fill: C.copperPale, color: C.red },
                ],
                { nodeWidth: 150, nodeHeight: 86 },
              ),
              bulletList(
                [
                  { text: "把工作盡量留在 wave 裡。" },
                  { text: "wave-level 溝通可用 wave intrinsic。" },
                  { text: "global queue 只作 fallback 或 spill path。", bold: true, color: C.tealDark },
                ],
                { name: "next-arch", size: 24, gap: 18 },
              ),
            ],
          ),
        ),
      ],
    ),
  );

  slide(
    prs,
    "下一步計畫",
    column(
      { width: fill, height: fill, gap: 30, justify: "between" },
      [
        grid(
          { width: fill, height: hug, columns: [fr(1), fr(1)], columnGap: 28 },
          [
            panel(
              { width: fill, height: fixed(395), fill: C.panel, padding: { x: 30, y: 26 } },
              column(
                { width: fill, height: fill, gap: 20 },
                [
                  badge("Prototype", { width: 150, fill: C.teal }),
                  bulletList(
                    [
                      { text: "設計 wave-local work retention prototype。" },
                      { text: "保留 256-shard 版本作為 baseline。" },
                      { text: "定義 spill-to-global fallback path。" },
                    ],
                    { name: "prototype-list", size: 27, gap: 22 },
                  ),
                ],
              ),
            ),
            panel(
              { width: fill, height: fixed(395), fill: C.panel, padding: { x: 30, y: 26 } },
              column(
                { width: fill, height: fill, gap: 20 },
                [
                  badge("Metrics", { width: 140, fill: C.copper }),
                  bulletList(
                    [
                      { text: "global atomic count" },
                      { text: "wave-local handoff count" },
                      { text: "spill-to-global ratio" },
                      { text: "compute duration 與 tail stability" },
                    ],
                    { name: "metric-list", size: 27, gap: 18 },
                  ),
                ],
              ),
            ),
          ],
        ),
        callout("下次比較目標：current global-sharded queue vs wave-local architecture。", {
          size: 32,
          height: 92,
        }),
        callout("並行補驗證：request batching on/off controlled experiment，避免報告裡過度宣稱。", {
          fill: C.copperPale,
          barColor: C.copper,
          size: 27,
          height: 88,
        }),
      ],
    ),
  );

  return prs;
}

async function saveBlob(blob, filePath) {
  await fs.writeFile(filePath, Buffer.from(await blob.arrayBuffer()));
}

async function buildMontage(pngPaths, outPath) {
  const cols = 4;
  const thumbW = 420;
  const thumbH = 236;
  const pad = 30;
  const labelH = 26;
  const gapX = 26;
  const gapY = 38;
  const rows = Math.ceil(pngPaths.length / cols);
  const canvas = new Canvas(
    pad * 2 + cols * thumbW + (cols - 1) * gapX,
    pad * 2 + rows * (thumbH + labelH) + (rows - 1) * gapY,
  );
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "#FFFFFF";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.font = "18px Consolas";
  ctx.fillStyle = C.ink;
  ctx.strokeStyle = "#9CA3AF";
  ctx.lineWidth = 1;

  for (let i = 0; i < pngPaths.length; i += 1) {
    const img = await loadImage(pngPaths[i]);
    const colIdx = i % cols;
    const rowIdx = Math.floor(i / cols);
    const x = pad + colIdx * (thumbW + gapX);
    const y = pad + rowIdx * (thumbH + labelH + gapY);
    ctx.fillText(`slide_${String(i + 1).padStart(2, "0")}.png`, x, y + 18);
    ctx.drawImage(img, x, y + labelH, thumbW, thumbH);
    ctx.strokeRect(x, y + labelH, thumbW, thumbH);
  }

  await fs.writeFile(outPath, await canvas.toBuffer("png"));
}

await fs.mkdir(OUT_DIR, { recursive: true });
await fs.rm(VERIFY_DIR, { recursive: true, force: true });
await fs.mkdir(ASSET_DIR, { recursive: true });

const chartPath = path.join(ASSET_DIR, "high_shard_scaling.png");
await createLineChart(chartPath);

const presentation = buildDeck(chartPath);
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
console.log(`Rendered ${pngPaths.length} PNG previews: ${VERIFY_DIR}`);
