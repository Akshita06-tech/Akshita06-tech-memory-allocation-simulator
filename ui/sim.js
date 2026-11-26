import {
  renderFragmentationChart,
  renderStrategyComparisonChart,
  renderTimingChart
} from "/charts.js";

export function createSimulationController({ el, apiJson, escapeHtml, fmtKb, colorForPid }) {
  const simState = {
    initialized: false,
    runner: null,
    tickTimeline: [],
    playing: false,
    timerId: null,
    procFilter: "ALL",
    compareData: null,
    compareMetric: "avg_fragmentation",
    timingRows: null,
    expRows: null
  };

  function getSimParams() {
    const count = Math.max(1, Math.min(2000, Number(el("sim-count").value) || 40));
    el("sim-count").value = String(count);
    const seed = Math.max(1, Number(el("sim-seed").value) || 42);
    el("sim-seed").value = String(seed);
    return {
      mode: el("sim-mode").value,
      pattern: el("sim-pattern").value,
      pool_kb: Number(el("sim-pool").value),
      seed,
      count,
      strategy: el("sim-strategy").value
    };
  }

  function stopPlayback() {
    if (simState.timerId) {
      clearTimeout(simState.timerId);
      simState.timerId = null;
    }
    simState.playing = false;
    el("sim-play-btn").textContent = "Play";
  }

  function renderSimMemoryMap(sim) {
    const container = el("sim-memory-map");
    container.innerHTML = "";
    const totalKb = sim.pool_kb;
    el("sim-axis-mid").textContent = `${Math.round(totalKb / 2).toLocaleString()} KB`;
    el("sim-axis-end").textContent = `${totalKb.toLocaleString()} KB`;

    for (const b of sim.blocks) {
      const node = document.createElement("div");
      const pct = (b.size_kb / totalKb) * 100;
      node.className = "map-block";
      node.setAttribute("role", "listitem");
      node.setAttribute("tabindex", "0");
      node.style.flex = `${b.size_kb} 0 auto`;
      node.style.width = `${pct}%`;

      if (b.is_free) {
        node.classList.add("is-free");
      } else {
        node.style.backgroundColor = colorForPid(b.pid);
        node.style.color = "#ffffff";
      }

      if (pct >= 7) {
        const span = document.createElement("span");
        span.className = "map-block-label";
        span.textContent = b.is_free ? `Free (${b.size_kb} KB)` : b.owner;
        node.appendChild(span);
      }

      const tipText = `${b.is_free ? "Free block" : `${b.owner} (PID #${b.pid})`} · [${b.start_kb}, ${b.end_kb}) KB · ${fmtKb(b.size_kb)}`;
      node.setAttribute("aria-label", tipText);

      const showTip = () => {
        const tip = el("sim-map-tooltip");
        tip.textContent = tipText;
        tip.hidden = false;
        const rect = node.getBoundingClientRect();
        const stageRect = container.parentElement.getBoundingClientRect();
        const left = Math.max(0, Math.min(rect.left - stageRect.left, stageRect.width - 280));
        tip.style.left = `${left}px`;
      };
      const hideTip = () => {
        el("sim-map-tooltip").hidden = true;
      };
      node.addEventListener("mouseenter", showTip);
      node.addEventListener("focus", showTip);
      node.addEventListener("mouseleave", hideTip);
      node.addEventListener("blur", hideTip);
      container.appendChild(node);
    }
  }

  function renderSimView() {
    const sim = simState.runner;
    if (!sim) return;

    el("sim-tick-val").textContent = String(sim.tick);
    el("sim-status-badge").textContent = sim.done ? "Completed" : sim.tick === 0 ? "Ready" : "Running";
    el("sim-step-btn").disabled = sim.done;

    const st = sim.stats;
    const mt = sim.metrics;
    el("sim-map-sub").textContent =
      `Pool ${fmtKb(sim.pool_kb)} · Used ${fmtKb(st.used_kb)} · Free ${fmtKb(st.total_free_kb)} · Fragmentation ${st.fragmentation_pct.toFixed(2)}%`;
    el("sim-kpi-success").textContent = `${mt.success_rate.toFixed(1)}%`;
    el("sim-kpi-fails").textContent = `${mt.frag_failures} / ${mt.oom_failures}`;
    el("sim-kpi-wait").textContent = `${mt.avg_wait.toFixed(2)} ticks`;
    el("sim-kpi-compacts").textContent = String(mt.compactions);

    renderSimMemoryMap(sim);

    renderFragmentationChart(
      el("sim-chart-frag"),
      simState.tickTimeline,
      simState.tickTimeline.length > 0,
      {
        xTitle: "Tick",
        pointPrefix: "Tick",
        emptyText: "Press Step or Play to chart fragmentation per tick"
      }
    );

    el("sim-queue-badge").textContent = `${sim.queue.length} waiting`;
    if (sim.queue.length === 0) {
      el("sim-queue-empty").hidden = false;
      el("sim-queue-wrap").hidden = true;
    } else {
      el("sim-queue-empty").hidden = true;
      el("sim-queue-wrap").hidden = false;
      el("sim-queue-tbody").innerHTML = sim.queue.map((q) => `
        <tr>
          <td>#${q.id}</td>
          <td>${escapeHtml(q.name)}</td>
          <td>${fmtKb(q.units_kb)}</td>
          <td>${q.wait_ticks}</td>
          <td>${q.failure_kind === 1 ? "Fragmentation" : "Out of memory"}</td>
        </tr>
      `).join("");
    }

    const filter = simState.procFilter;
    const procs = filter === "ALL" ? sim.processes : sim.processes.filter((p) => p.state === filter);
    el("sim-proc-tbody").innerHTML = procs.map((p) => {
      const addr = p.start_tick >= 0 ? `[${p.start_addr}, ${p.end_addr}) KB` : "—";
      const cls = `is-${p.state.toLowerCase()}`;
      return `
        <tr>
          <td>#${p.id}</td>
          <td><span class="owner-cell"><span class="color-dot" style="background:${colorForPid(p.id)}"></span>${escapeHtml(p.name)}</span></td>
          <td>${escapeHtml(p.type)}</td>
          <td>${fmtKb(p.units_kb)}</td>
          <td>t=${p.arrive_tick} / ${p.duration}t</td>
          <td><span class="state-pill ${cls}">${escapeHtml(p.state)}</span></td>
          <td>${addr}</td>
          <td>${p.wait_ticks}</td>
        </tr>
      `;
    }).join("");
  }

  function bestValue(rows, key, higherBetter) {
    const vals = rows.map((r) => Number(r[key]));
    return higherBetter ? Math.max(...vals) : Math.min(...vals);
  }

  function renderCompareSection() {
    const data = simState.compareData;
    if (!data || !data.results) return;

    const rec = el("compare-recommendation");
    rec.hidden = false;
    rec.className = data.is_tie ? "fit-box is-warn" : "fit-box is-ok";
    rec.innerHTML = `<strong>${data.is_tie ? "Tied rank-sum result:" : `Recommended: ${escapeHtml(data.winner)}`}</strong> ${escapeHtml(data.recommendation_why)}`;

    const rows = data.results;
    const best = {
      success_rate: bestValue(rows, "success_rate", true),
      frag_failures: bestValue(rows, "frag_failures", false),
      oom_failures: bestValue(rows, "oom_failures", false),
      avg_wait: bestValue(rows, "avg_wait", false),
      makespan: bestValue(rows, "makespan", false),
      avg_fragmentation: bestValue(rows, "avg_fragmentation", false),
      avg_search_length: bestValue(rows, "avg_search_length", false),
      compactions: bestValue(rows, "compactions", false),
      rank_sum: bestValue(rows, "rank_sum", false)
    };

    const cell = (val, key, formatted) =>
      `<td class="${Number(val) === best[key] ? "is-best-cell" : ""}">${formatted}</td>`;

    el("compare-tbody").innerHTML = rows.map((r) => `
      <tr>
        <td><strong>${escapeHtml(r.strategy)}</strong></td>
        ${cell(r.success_rate, "success_rate", `${r.success_rate.toFixed(2)}%`)}
        ${cell(r.frag_failures, "frag_failures", String(r.frag_failures))}
        ${cell(r.oom_failures, "oom_failures", String(r.oom_failures))}
        ${cell(r.avg_wait, "avg_wait", r.avg_wait.toFixed(2))}
        ${cell(r.makespan, "makespan", String(r.makespan))}
        ${cell(r.avg_fragmentation, "avg_fragmentation", `${r.avg_fragmentation.toFixed(2)}%`)}
        ${cell(r.avg_search_length, "avg_search_length", r.avg_search_length.toFixed(2))}
        ${cell(r.compactions, "compactions", `${r.compactions} (${fmtKb(r.units_moved)})`)}
        ${cell(r.rank_sum, "rank_sum", String(r.rank_sum))}
      </tr>
    `).join("");

    renderStrategyComparisonChart(el("chart-compare-bars"), rows, simState.compareMetric);
  }

  function renderExperimentExplorer() {
    const allRows = simState.expRows;
    if (!allRows) return;

    const mode = el("exp-mode").value;
    const pattern = el("exp-pattern").value;
    const pool = el("exp-pool").value;

    const filtered = allRows.filter((r) =>
      (mode === "all" || r.mode === mode) &&
      (pattern === "all" || r.pattern === pattern) &&
      (pool === "all" || r.pool_kb === Number(pool))
    );

    const strategies = ["First Fit", "Best Fit", "Worst Fit", "Smart"];
    const avg = (arr, k) => arr.reduce((acc, x) => acc + Number(x[k]), 0) / arr.length;

    const summary = strategies.map((strat) => {
      const sub = filtered.filter((r) => r.strategy === strat);
      if (sub.length === 0) return null;
      return {
        strategy: strat,
        runs: sub.length,
        success_rate_pct: avg(sub, "success_rate_pct"),
        fragmentation_failures: avg(sub, "fragmentation_failures"),
        oom_failures: avg(sub, "oom_failures"),
        avg_wait_ticks: avg(sub, "avg_wait_ticks"),
        makespan_ticks: avg(sub, "makespan_ticks"),
        avg_fragmentation_pct: avg(sub, "avg_fragmentation_pct"),
        avg_search_length: avg(sub, "avg_search_length"),
        compactions: avg(sub, "compactions"),
        rank_sum: avg(sub, "rank_sum")
      };
    }).filter(Boolean);

    el("exp-summary").textContent = summary.length
      ? `Showing averages across ${summary[0].runs} experiment run(s) per strategy (${filtered.length} total rows from experiment_results.csv).`
      : "No experiment rows match the selected filters.";

    if (summary.length === 0) {
      el("exp-tbody").innerHTML = "";
      return;
    }

    const best = {
      success_rate_pct: bestValue(summary, "success_rate_pct", true),
      fragmentation_failures: bestValue(summary, "fragmentation_failures", false),
      oom_failures: bestValue(summary, "oom_failures", false),
      avg_wait_ticks: bestValue(summary, "avg_wait_ticks", false),
      makespan_ticks: bestValue(summary, "makespan_ticks", false),
      avg_fragmentation_pct: bestValue(summary, "avg_fragmentation_pct", false),
      avg_search_length: bestValue(summary, "avg_search_length", false),
      compactions: bestValue(summary, "compactions", false),
      rank_sum: bestValue(summary, "rank_sum", false)
    };
    const cell = (val, key, formatted) =>
      `<td class="${Number(val) === best[key] ? "is-best-cell" : ""}">${formatted}</td>`;

    el("exp-tbody").innerHTML = summary.map((r) => `
      <tr>
        <td><strong>${escapeHtml(r.strategy)}</strong></td>
        <td>${r.runs}</td>
        ${cell(r.success_rate_pct, "success_rate_pct", `${r.success_rate_pct.toFixed(2)}%`)}
        ${cell(r.fragmentation_failures, "fragmentation_failures", r.fragmentation_failures.toFixed(2))}
        ${cell(r.oom_failures, "oom_failures", r.oom_failures.toFixed(2))}
        ${cell(r.avg_wait_ticks, "avg_wait_ticks", r.avg_wait_ticks.toFixed(2))}
        ${cell(r.makespan_ticks, "makespan_ticks", r.makespan_ticks.toFixed(1))}
        ${cell(r.avg_fragmentation_pct, "avg_fragmentation_pct", `${r.avg_fragmentation_pct.toFixed(2)}%`)}
        ${cell(r.avg_search_length, "avg_search_length", r.avg_search_length.toFixed(2))}
        ${cell(r.compactions, "compactions", r.compactions.toFixed(2))}
        ${cell(r.rank_sum, "rank_sum", r.rank_sum.toFixed(2))}
      </tr>
    `).join("");
  }

  async function simInit() {
    stopPlayback();
    const res = await apiJson("/api/sim/init", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(getSimParams())
    });
    if (!res.ok) return;
    simState.runner = res;
    simState.tickTimeline = [];
    renderSimView();
  }

  async function simStep(steps = 1) {
    if (!simState.runner) await simInit();
    if (simState.runner && simState.runner.done) {
      stopPlayback();
      return;
    }
    const res = await apiJson("/api/sim/step", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ steps })
    });
    if (!res.ok) {
      stopPlayback();
      return;
    }
    for (const snap of res.tick_snapshots || []) {
      simState.tickTimeline.push({
        step: snap.tick,
        action: "Tick",
        fragmentation_pct: snap.fragmentation_pct,
        used_kb: snap.used_kb,
        total_free_kb: snap.total_free_kb
      });
    }
    simState.runner = res;
    if (res.done) stopPlayback();
    renderSimView();
  }

  function scheduleNextTick() {
    if (!simState.playing) return;
    const speed = Number(el("sim-speed").value) || 2;
    const delay = speed === 1 ? 400 : speed === 2 ? 180 : speed === 5 ? 70 : 30;
    const steps = speed >= 10 ? 3 : 1;
    simState.timerId = setTimeout(async () => {
      if (!simState.playing) return;
      await simStep(steps);
      if (simState.playing && simState.runner && !simState.runner.done) {
        scheduleNextTick();
      }
    }, delay);
  }

  function togglePlay() {
    if (simState.playing) {
      stopPlayback();
      return;
    }
    if (simState.runner && simState.runner.done) {
      simInit().then(() => {
        simState.playing = true;
        el("sim-play-btn").textContent = "Pause";
        scheduleNextTick();
      });
      return;
    }
    simState.playing = true;
    el("sim-play-btn").textContent = "Pause";
    scheduleNextTick();
  }

  async function simCompare() {
    const res = await apiJson("/api/sim/compare", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(getSimParams())
    });
    if (!res.ok) return;
    simState.compareData = res;
    renderCompareSection();
  }

  async function loadCsvData() {
    const [timingRes, expRes] = await Promise.all([
      apiJson("/api/csv/timing"),
      apiJson("/api/csv/experiments")
    ]);
    if (timingRes.ok) {
      simState.timingRows = timingRes.rows;
      renderTimingChart(el("chart-timing"), timingRes.rows);
    }
    if (expRes.ok) {
      simState.expRows = expRes.rows;
      renderExperimentExplorer();
    }
  }

  async function ensureLoaded() {
    if (simState.initialized) return;
    simState.initialized = true;
    await Promise.all([simInit(), simCompare(), loadCsvData()]);
  }

  function setupEvents() {
    el("sim-generate-btn").addEventListener("click", simInit);
    el("sim-reset-btn").addEventListener("click", simInit);
    el("sim-step-btn").addEventListener("click", () => simStep(1));
    el("sim-play-btn").addEventListener("click", togglePlay);
    el("sim-compare-btn").addEventListener("click", simCompare);
    el("sim-strategy").addEventListener("change", simInit);

    el("compare-metric-select").addEventListener("change", () => {
      simState.compareMetric = el("compare-metric-select").value;
      if (simState.compareData) {
        renderStrategyComparisonChart(
          el("chart-compare-bars"),
          simState.compareData.results,
          simState.compareMetric
        );
      }
    });

    document.querySelectorAll("#sim-proc-filter .seg-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        simState.procFilter = btn.dataset.state;
        document.querySelectorAll("#sim-proc-filter .seg-btn").forEach((b) => {
          b.classList.toggle("is-active", b === btn);
        });
        renderSimView();
      });
    });

    for (const id of ["exp-mode", "exp-pattern", "exp-pool"]) {
      el(id).addEventListener("change", renderExperimentExplorer);
    }
  }

  return { ensureLoaded, setupEvents };
}
