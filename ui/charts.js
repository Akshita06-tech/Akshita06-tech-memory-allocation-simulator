const SVG_NS = "http://www.w3.org/2000/svg";

function createSvg(width, height, label) {
  const svg = document.createElementNS(SVG_NS, "svg");
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("class", "chart-svg");
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-label", label);
  return svg;
}

function addEl(parent, tag, attrs = {}, text = null) {
  const node = document.createElementNS(SVG_NS, tag);
  for (const [k, v] of Object.entries(attrs)) {
    node.setAttribute(k, String(v));
  }
  if (text !== null) node.textContent = text;
  parent.appendChild(node);
  return node;
}

function fmtShortMb(kb) {
  const mb = kb / 1024;
  if (Number.isInteger(mb)) return `${mb} MB`;
  if (mb >= 10) return `${mb.toFixed(1)} MB`;
  if (mb >= 1) return `${mb.toFixed(2)} MB`;
  return `${kb.toLocaleString()} KB`;
}

function renderEmpty(container, message) {
  container.innerHTML = `<div class="chart-empty">${message}</div>`;
}

export function renderFragmentationChart(container, timeline, hasActivity, opts = {}) {
  const emptyText = opts.emptyText || "Upload a file to start the chart";
  const xTitle = opts.xTitle || "Action number";
  const pointPrefix = opts.pointPrefix || "Action";
  if (!hasActivity || timeline.length === 0) {
    renderEmpty(container, emptyText);
    return;
  }

  const w = 440;
  const h = 224;
  const pad = { top: 16, right: 16, bottom: 42, left: 42 };
  const plotW = w - pad.left - pad.right;
  const plotH = h - pad.top - pad.bottom;

  const svg = createSvg(w, h, "External fragmentation percentage over time");

  // Horizontal grid & Y labels (0% - 100%)
  for (const pct of [0, 25, 50, 75, 100]) {
    const y = pad.top + plotH - (pct / 100) * plotH;
    addEl(svg, "line", {
      x1: pad.left,
      y1: y,
      x2: w - pad.right,
      y2: y,
      class: "chart-grid"
    });
    addEl(svg, "text", {
      x: pad.left - 8,
      y: y + 4,
      "text-anchor": "end",
      class: "chart-axis-text"
    }, `${pct}%`);
  }

  // Axes
  addEl(svg, "line", { x1: pad.left, y1: pad.top, x2: pad.left, y2: pad.top + plotH, class: "chart-axis-line" });
  addEl(svg, "line", { x1: pad.left, y1: pad.top + plotH, x2: w - pad.right, y2: pad.top + plotH, class: "chart-axis-line" });

  const n = timeline.length;
  const xFor = (i) => (n === 1 ? pad.left + plotW / 2 : pad.left + (i / (n - 1)) * plotW);
  const yFor = (pct) => pad.top + plotH - (Math.max(0, Math.min(100, pct)) / 100) * plotH;

  const points = timeline.map((pt, i) => [xFor(i), yFor(pt.fragmentation_pct), pt]);

  if (points.length >= 2) {
    const areaD = [
      `M ${points[0][0].toFixed(1)} ${(pad.top + plotH).toFixed(1)}`,
      ...points.map(([x, y]) => `L ${x.toFixed(1)} ${y.toFixed(1)}`),
      `L ${points[points.length - 1][0].toFixed(1)} ${(pad.top + plotH).toFixed(1)} Z`
    ].join(" ");
    addEl(svg, "path", { d: areaD, class: "chart-area-accent" });

    const lineD = points.map(([x, y], idx) => `${idx === 0 ? "M" : "L"} ${x.toFixed(1)} ${y.toFixed(1)}`).join(" ");
    addEl(svg, "path", { d: lineD, class: "chart-line-accent" });
  }

  for (const [x, y, pt] of points) {
    const circle = addEl(svg, "circle", {
      cx: x.toFixed(1),
      cy: y.toFixed(1),
      r: points.length > 24 ? 2.5 : 4,
      class: "chart-dot-accent"
    });
    addEl(circle, "title", {}, `${pointPrefix} ${pt.step} (${pt.action}): ${pt.fragmentation_pct.toFixed(2)}% fragmentation`);
  }

  // X labels (start, mid, end)
  const xIndices = n <= 3 ? timeline.map((_, i) => i) : [0, Math.floor((n - 1) / 2), n - 1];
  for (const idx of xIndices) {
    addEl(svg, "text", {
      x: xFor(idx).toFixed(1),
      y: pad.top + plotH + 15,
      "text-anchor": "middle",
      class: "chart-axis-text"
    }, `${timeline[idx].step}`);
  }
  addEl(svg, "text", {
    x: (pad.left + plotW / 2).toFixed(1),
    y: h - 5,
    "text-anchor": "middle",
    class: "chart-axis-title"
  }, xTitle);

  container.innerHTML = "";
  container.appendChild(svg);
}

export function renderUsedFreeChart(container, timeline, poolKb, hasActivity, opts = {}) {
  const emptyText = opts.emptyText || "Upload a file to start the chart";
  const xTitle = opts.xTitle || "Action number";
  const pointPrefix = opts.pointPrefix || "Action";
  if (!hasActivity || timeline.length === 0 || !poolKb) {
    renderEmpty(container, emptyText);
    return;
  }

  const w = 440;
  const h = 224;
  const pad = { top: 16, right: 16, bottom: 42, left: 52 };
  const plotW = w - pad.left - pad.right;
  const plotH = h - pad.top - pad.bottom;

  const svg = createSvg(w, h, "Used versus free memory over time");

  for (const frac of [0, 0.5, 1]) {
    const y = pad.top + plotH - frac * plotH;
    addEl(svg, "line", { x1: pad.left, y1: y, x2: w - pad.right, y2: y, class: "chart-grid" });
    addEl(svg, "text", {
      x: pad.left - 8,
      y: y + 4,
      "text-anchor": "end",
      class: "chart-axis-text"
    }, fmtShortMb(Math.round(poolKb * frac)));
  }

  const n = timeline.length;
  const xFor = (i) => (n === 1 ? pad.left + plotW / 2 : pad.left + (i / (n - 1)) * plotW);
  const yUsed = (usedKb) => pad.top + plotH - (Math.max(0, Math.min(poolKb, usedKb)) / poolKb) * plotH;

  if (n === 1) {
    const pt = timeline[0];
    const barW = Math.min(80, plotW * 0.4);
    const x = pad.left + (plotW - barW) / 2;
    const usedH = (pt.used_kb / poolKb) * plotH;
    const freeH = plotH - usedH;
    addEl(svg, "rect", { x, y: pad.top, width: barW, height: freeH, class: "chart-stack-free" });
    addEl(svg, "rect", { x, y: pad.top + freeH, width: barW, height: usedH, class: "chart-stack-used" });
  } else {
    // Top stacked area (Free fills from top of Used up to poolKb)
    const topCoords = timeline.map((_, i) => `${xFor(i).toFixed(1)} ${pad.top.toFixed(1)}`);
    const usedCoordsRev = [...timeline]
      .reverse()
      .map((pt, rIdx) => `${xFor(n - 1 - rIdx).toFixed(1)} ${yUsed(pt.used_kb).toFixed(1)}`);
    const freeD = `M ${topCoords[0]} ${topCoords.slice(1).map((c) => `L ${c}`).join(" ")} ${usedCoordsRev.map((c) => `L ${c}`).join(" ")} Z`;
    addEl(svg, "path", { d: freeD, class: "chart-stack-free" });

    // Bottom stacked area (Used from baseline up to yUsed)
    const usedCoords = timeline.map((pt, i) => [xFor(i), yUsed(pt.used_kb), pt]);
    const usedAreaD = [
      `M ${usedCoords[0][0].toFixed(1)} ${(pad.top + plotH).toFixed(1)}`,
      ...usedCoords.map(([x, y]) => `L ${x.toFixed(1)} ${y.toFixed(1)}`),
      `L ${usedCoords[n - 1][0].toFixed(1)} ${(pad.top + plotH).toFixed(1)} Z`
    ].join(" ");
    addEl(svg, "path", { d: usedAreaD, class: "chart-stack-used" });

    const usedLineD = usedCoords.map(([x, y], idx) => `${idx === 0 ? "M" : "L"} ${x.toFixed(1)} ${y.toFixed(1)}`).join(" ");
    addEl(svg, "path", { d: usedLineD, class: "chart-line-accent" });

    for (const [x, y, pt] of usedCoords) {
      const dot = addEl(svg, "circle", {
        cx: x.toFixed(1),
        cy: y.toFixed(1),
        r: n > 24 ? 2.5 : 3.5,
        class: "chart-dot-accent"
      });
      addEl(dot, "title", {}, `${pointPrefix} ${pt.step} (${pt.action}): Used ${fmtShortMb(pt.used_kb)}, Free ${fmtShortMb(pt.total_free_kb)}`);
    }
  }

  addEl(svg, "line", { x1: pad.left, y1: pad.top, x2: pad.left, y2: pad.top + plotH, class: "chart-axis-line" });
  addEl(svg, "line", { x1: pad.left, y1: pad.top + plotH, x2: w - pad.right, y2: pad.top + plotH, class: "chart-axis-line" });

  const xIndices = n <= 3 ? timeline.map((_, i) => i) : [0, Math.floor((n - 1) / 2), n - 1];
  for (const idx of xIndices) {
    addEl(svg, "text", {
      x: xFor(idx).toFixed(1),
      y: pad.top + plotH + 15,
      "text-anchor": "middle",
      class: "chart-axis-text"
    }, `${timeline[idx].step}`);
  }
  addEl(svg, "text", {
    x: (pad.left + plotW / 2).toFixed(1),
    y: h - 5,
    "text-anchor": "middle",
    class: "chart-axis-title"
  }, xTitle);

  container.innerHTML = "";
  container.appendChild(svg);
}

export function renderFreeBlocksChart(container, blocks, hasActivity) {
  if (!hasActivity) {
    renderEmpty(container, "Upload a file to start the chart");
    return;
  }

  const freeBlocks = (blocks || []).filter((b) => b.is_free);
  if (freeBlocks.length === 0) {
    renderEmpty(container, "No free blocks remaining (0 KB free)");
    return;
  }

  const w = 440;
  const h = 224;
  const pad = { top: 22, right: 16, bottom: 44, left: 52 };
  const plotW = w - pad.left - pad.right;
  const plotH = h - pad.top - pad.bottom;

  const maxKb = Math.max(...freeBlocks.map((b) => b.size_kb), 1);
  const svg = createSvg(w, h, "Current free block sizes");

  for (const frac of [0, 0.5, 1]) {
    const y = pad.top + plotH - frac * plotH;
    addEl(svg, "line", { x1: pad.left, y1: y, x2: w - pad.right, y2: y, class: "chart-grid" });
    addEl(svg, "text", {
      x: pad.left - 8,
      y: y + 4,
      "text-anchor": "end",
      class: "chart-axis-text"
    }, fmtShortMb(Math.round(maxKb * frac)));
  }

  addEl(svg, "line", { x1: pad.left, y1: pad.top, x2: pad.left, y2: pad.top + plotH, class: "chart-axis-line" });
  addEl(svg, "line", { x1: pad.left, y1: pad.top + plotH, x2: w - pad.right, y2: pad.top + plotH, class: "chart-axis-line" });

  const slotW = plotW / freeBlocks.length;
  const barW = Math.max(8, Math.min(54, slotW * 0.68));

  freeBlocks.forEach((b, i) => {
    const x = pad.left + i * slotW + (slotW - barW) / 2;
    const barH = Math.max(2, (b.size_kb / maxKb) * plotH);
    const y = pad.top + plotH - barH;
    const rect = addEl(svg, "rect", {
      x: x.toFixed(1),
      y: y.toFixed(1),
      width: barW.toFixed(1),
      height: barH.toFixed(1),
      rx: 3,
      class: "chart-bar-accent"
    });
    addEl(rect, "title", {}, `Free hole #${i + 1}: [${b.start_kb}, ${b.end_kb}) KB (${fmtShortMb(b.size_kb)})`);

    if (freeBlocks.length <= 8) {
      addEl(svg, "text", {
        x: (x + barW / 2).toFixed(1),
        y: Math.max(12, y - 5).toFixed(1),
        "text-anchor": "middle",
        class: "chart-value-text"
      }, fmtShortMb(b.size_kb));
    }

    if (freeBlocks.length <= 10 || i === 0 || i === freeBlocks.length - 1) {
      addEl(svg, "text", {
        x: (x + barW / 2).toFixed(1),
        y: pad.top + plotH + 15,
        "text-anchor": "middle",
        class: "chart-axis-text"
      }, `#${i + 1}`);
    }
  });

  addEl(svg, "text", {
    x: (pad.left + plotW / 2).toFixed(1),
    y: h - 5,
    "text-anchor": "middle",
    class: "chart-axis-title"
  }, "Free block # (address order)");

  container.innerHTML = "";
  container.appendChild(svg);
}

const COMPARE_METRICS = {
  avg_fragmentation: { label: "Avg fragmentation (%)", higherBetter: false, fmt: (v) => `${v.toFixed(2)}%` },
  success_rate: { label: "Success rate (%)", higherBetter: true, fmt: (v) => `${v.toFixed(1)}%` },
  avg_wait: { label: "Average wait (ticks)", higherBetter: false, fmt: (v) => `${v.toFixed(2)}` },
  avg_search_length: { label: "Avg search (blocks)", higherBetter: false, fmt: (v) => `${v.toFixed(2)}` },
  makespan: { label: "Makespan (ticks)", higherBetter: false, fmt: (v) => `${Math.round(v)}` }
};

export function renderStrategyComparisonChart(container, results, metricKey = "avg_fragmentation") {
  if (!results || results.length === 0) {
    renderEmpty(container, "Run 4-strategy comparison to view chart");
    return;
  }

  const meta = COMPARE_METRICS[metricKey] || COMPARE_METRICS.avg_fragmentation;
  const values = results.map((r) => Number(r[metricKey] ?? 0));
  const rawMax = Math.max(...values, 0);
  const maxVal = metricKey === "success_rate" ? 100 : (rawMax > 0 ? rawMax * 1.15 : 1);
  const bestVal = meta.higherBetter ? Math.max(...values) : Math.min(...values);

  const w = 480;
  const h = 232;
  const pad = { top: 24, right: 16, bottom: 44, left: 50 };
  const plotW = w - pad.left - pad.right;
  const plotH = h - pad.top - pad.bottom;

  const svg = createSvg(w, h, `Strategy comparison bar chart for ${meta.label}`);

  for (const frac of [0, 0.5, 1]) {
    const y = pad.top + plotH - frac * plotH;
    addEl(svg, "line", { x1: pad.left, y1: y, x2: w - pad.right, y2: y, class: "chart-grid" });
    const tickVal = maxVal * frac;
    const label = metricKey === "success_rate" ? `${Math.round(tickVal)}%` : tickVal >= 10 ? tickVal.toFixed(0) : tickVal.toFixed(1);
    addEl(svg, "text", { x: pad.left - 8, y: y + 4, "text-anchor": "end", class: "chart-axis-text" }, label);
  }

  addEl(svg, "line", { x1: pad.left, y1: pad.top, x2: pad.left, y2: pad.top + plotH, class: "chart-axis-line" });
  addEl(svg, "line", { x1: pad.left, y1: pad.top + plotH, x2: w - pad.right, y2: pad.top + plotH, class: "chart-axis-line" });

  const slotW = plotW / results.length;
  const barW = Math.min(60, slotW * 0.58);

  results.forEach((r, i) => {
    const val = Number(r[metricKey] ?? 0);
    const x = pad.left + i * slotW + (slotW - barW) / 2;
    const barH = Math.max(2, (val / maxVal) * plotH);
    const y = pad.top + plotH - barH;
    const isBest = val === bestVal;

    const rect = addEl(svg, "rect", {
      x: x.toFixed(1),
      y: y.toFixed(1),
      width: barW.toFixed(1),
      height: barH.toFixed(1),
      rx: 4,
      class: isBest ? "chart-bar-accent" : "chart-bar-muted"
    });
    addEl(rect, "title", {}, `${r.strategy}: ${meta.fmt(val)}${isBest ? " (best)" : ""}`);

    addEl(svg, "text", {
      x: (x + barW / 2).toFixed(1),
      y: Math.max(13, y - 5).toFixed(1),
      "text-anchor": "middle",
      class: "chart-value-text"
    }, meta.fmt(val));

    addEl(svg, "text", {
      x: (x + barW / 2).toFixed(1),
      y: pad.top + plotH + 15,
      "text-anchor": "middle",
      class: "chart-axis-text"
    }, r.strategy);
  });

  addEl(svg, "text", {
    x: (pad.left + plotW / 2).toFixed(1),
    y: h - 5,
    "text-anchor": "middle",
    class: "chart-axis-title"
  }, "Strategy");

  container.innerHTML = "";
  container.appendChild(svg);
}

const SERIES_STYLES = {
  "First Fit": { color: "#0f766e", dash: "" },
  "Best Fit":  { color: "#0369a1", dash: "6 3" },
  "Worst Fit": { color: "#b45309", dash: "2 3" },
  "Smart":     { color: "#be123c", dash: "" }
};

export function renderTimingChart(container, rows) {
  if (!rows || rows.length === 0) {
    renderEmpty(container, "No timing_results.csv data found");
    return;
  }

  const procCounts = [...new Set(rows.map((r) => r.processes))].sort((a, b) => a - b);
  const strategies = ["First Fit", "Best Fit", "Worst Fit", "Smart"];
  const maxSec = Math.max(...rows.map((r) => r.average_elapsed_seconds), 0.1) * 1.12;

  const w = 480;
  const h = 232;
  const pad = { top: 20, right: 20, bottom: 44, left: 54 };
  const plotW = w - pad.left - pad.right;
  const plotH = h - pad.top - pad.bottom;

  const svg = createSvg(w, h, "Runtime versus process count from timing_results.csv");

  for (const frac of [0, 0.5, 1]) {
    const y = pad.top + plotH - frac * plotH;
    addEl(svg, "line", { x1: pad.left, y1: y, x2: w - pad.right, y2: y, class: "chart-grid" });
    addEl(svg, "text", {
      x: pad.left - 8,
      y: y + 4,
      "text-anchor": "end",
      class: "chart-axis-text"
    }, `${(maxSec * frac).toFixed(1)}s`);
  }

  addEl(svg, "line", { x1: pad.left, y1: pad.top, x2: pad.left, y2: pad.top + plotH, class: "chart-axis-line" });
  addEl(svg, "line", { x1: pad.left, y1: pad.top + plotH, x2: w - pad.right, y2: pad.top + plotH, class: "chart-axis-line" });

  const xForIdx = (i) => (procCounts.length === 1 ? pad.left + plotW / 2 : pad.left + (i / (procCounts.length - 1)) * plotW);
  const yForSec = (s) => pad.top + plotH - (s / maxSec) * plotH;

  for (const strat of strategies) {
    const pts = procCounts.map((pc, i) => {
      const match = rows.find((r) => r.strategy === strat && r.processes === pc);
      return match ? [xForIdx(i), yForSec(match.average_elapsed_seconds), match] : null;
    }).filter(Boolean);

    if (pts.length === 0) continue;
    const style = SERIES_STYLES[strat] || { color: "#0f766e", dash: "" };

    if (pts.length >= 2) {
      const d = pts.map(([x, y], idx) => `${idx === 0 ? "M" : "L"} ${x.toFixed(1)} ${y.toFixed(1)}`).join(" ");
      const pathAttrs = {
        d,
        fill: "none",
        stroke: style.color,
        "stroke-width": "2.3",
        "stroke-linecap": "round",
        "stroke-linejoin": "round"
      };
      if (style.dash) pathAttrs["stroke-dasharray"] = style.dash;
      addEl(svg, "path", pathAttrs);
    }

    for (const [x, y, row] of pts) {
      const dot = addEl(svg, "circle", {
        cx: x.toFixed(1),
        cy: y.toFixed(1),
        r: 4,
        fill: style.color,
        stroke: "var(--surface)",
        "stroke-width": "1.5"
      });
      addEl(dot, "title", {}, `${strat} (${row.processes.toLocaleString()} processes): ${row.average_elapsed_seconds.toFixed(4)} s avg (${row.repetitions} runs)`);
    }
  }

  procCounts.forEach((pc, i) => {
    addEl(svg, "text", {
      x: xForIdx(i).toFixed(1),
      y: pad.top + plotH + 15,
      "text-anchor": "middle",
      class: "chart-axis-text"
    }, pc.toLocaleString());
  });

  addEl(svg, "text", {
    x: (pad.left + plotW / 2).toFixed(1),
    y: h - 5,
    "text-anchor": "middle",
    class: "chart-axis-title"
  }, "Process count (timing_results.csv)");

  container.innerHTML = "";
  container.appendChild(svg);
}

