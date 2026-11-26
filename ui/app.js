const PROCESS_PALETTE = [
  "#0f766e",
  "#0369a1",
  "#b45309",
  "#6d28d9",
  "#be123c",
  "#15803d",
  "#4338ca",
  "#c2410c",
  "#0e7490",
  "#7e22ce"
];

const STRATEGY_REASONS = {
  "First Fit": "First Fit scans from address 0 and picks the earliest free block large enough.",
  "Best Fit": "Best Fit scans all free blocks and picks the smallest one that fits to minimize leftover space.",
  "Worst Fit": "Worst Fit scans all free blocks and picks the largest free block available.",
  "Smart": "Smart uses engine_choose(): First Fit when >60% is free, Best Fit when tighter, or compacts first when total free is enough but scattered."
};

const state = {
  sim: null,
  strategy: "first",
  stagedFile: null,
  precheck: null,
  lastPlacedOwner: null
};

const el = (id) => document.getElementById(id);

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function fmtMb(kb) {
  const mb = kb / 1024;
  if (Number.isInteger(mb)) return `${mb} MB`;
  if (mb >= 1) return `${mb.toFixed(2)} MB`;
  return `${mb.toFixed(3)} MB`;
}

function fmtMbNumber(kb) {
  const mb = kb / 1024;
  if (Number.isInteger(mb)) return String(mb);
  return mb >= 1 ? mb.toFixed(2) : mb.toFixed(3);
}

function fmtRangeMb(startKb, endKb) {
  return `${fmtMbNumber(startKb)} to ${fmtMbNumber(endKb)} MB`;
}

function fmtKb(kb) {
  return `${Number(kb).toLocaleString()} KB`;
}

function fmtBytes(bytes) {
  return `${Number(bytes).toLocaleString()} bytes`;
}

function colorForPid(pid) {
  if (!pid || pid < 1) return "#0f766e";
  return PROCESS_PALETTE[(pid - 1) % PROCESS_PALETTE.length];
}

function initTheme() {
  const saved = localStorage.getItem("mem_sim_theme");
  const prefersDark = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
  const theme = saved || (prefersDark ? "dark" : "light");
  applyTheme(theme);
}

function applyTheme(theme) {
  document.documentElement.setAttribute("data-theme", theme);
  localStorage.setItem("mem_sim_theme", theme);
  el("theme-label").textContent = theme === "dark" ? "Light mode" : "Dark mode";
}

async function apiJson(url, options = {}) {
  const res = await fetch(url, options);
  const data = await res.json();
  if (!res.ok && !data.error) {
    throw new Error(`HTTP ${res.status}`);
  }
  return data;
}

function syncHeaderControls() {
  if (!state.sim) return;
  const { pool_kb, system_kb, system_reserve_pct, stats } = state.sim;
  el("system-pct-note").textContent = `${system_reserve_pct}%`;

  el("capacity-summary").textContent = `${fmtMbNumber(stats.used_kb)} of ${fmtMbNumber(pool_kb)} MB used`;
  const userKb = Math.max(0, stats.used_kb - system_kb);
  el("capacity-detail").textContent =
    `${fmtKb(system_kb)} system · ${fmtKb(userKb)} user · ${fmtKb(stats.total_free_kb)} free`;

  const sysPct = (system_kb / pool_kb) * 100;
  const userPct = (userKb / pool_kb) * 100;
  el("cap-system-fill").style.width = `${sysPct}%`;
  el("cap-user-fill").style.width = `${userPct}%`;

  const poolMb = pool_kb / 1024;
  el("map-end-label").textContent = `${poolMb} MB`;
  el("axis-mid").textContent = `${poolMb / 2} MB`;
  el("axis-end").textContent = `${poolMb} MB`;

  document.querySelectorAll("#capacity-selector .seg-btn").forEach((btn) => {
    btn.classList.toggle("is-active", Number(btn.dataset.pool) === pool_kb);
  });
}

function renderMemoryMap() {
  const container = el("memory-map");
  container.innerHTML = "";
  if (!state.sim) return;

  const totalKb = state.sim.pool_kb;
  for (const b of state.sim.blocks) {
    const node = document.createElement("div");
    const pct = (b.size_kb / totalKb) * 100;
    node.className = "map-block";
    node.setAttribute("role", "listitem");
    node.setAttribute("tabindex", "0");
    node.style.flex = `${b.size_kb} 0 auto`;
    node.style.width = `${pct}%`;

    if (b.is_system) {
      node.classList.add("is-system");
      node.style.color = "#ffffff";
    } else if (b.is_free) {
      node.classList.add("is-free");
    } else {
      node.style.backgroundColor = colorForPid(b.pid);
      node.style.color = "#ffffff";
      if (state.lastPlacedOwner && b.owner === state.lastPlacedOwner) {
        node.classList.add("is-new");
      }
    }

    const label = b.is_free ? `Free (${fmtMb(b.size_kb)})` : b.owner;
    if (pct >= 6) {
      const span = document.createElement("span");
      span.className = "map-block-label";
      span.textContent = label;
      node.appendChild(span);
    }

    const tipText = `${b.is_free ? "Free block" : b.is_system ? "System (reserved)" : `${b.owner} (PID #${b.pid})`} · ${fmtRangeMb(b.start_kb, b.end_kb)} ([${b.start_kb}, ${b.end_kb}) KB) · ${fmtMb(b.size_kb)} (${fmtKb(b.size_kb)})`;
    node.setAttribute("aria-label", tipText);

    const showTip = () => {
      const tip = el("map-tooltip");
      tip.textContent = tipText;
      tip.hidden = false;
      const rect = node.getBoundingClientRect();
      const stageRect = container.parentElement.getBoundingClientRect();
      const left = Math.max(0, Math.min(rect.left - stageRect.left, stageRect.width - 280));
      tip.style.left = `${left}px`;
    };
    const hideTip = () => {
      el("map-tooltip").hidden = true;
    };

    node.addEventListener("mouseenter", showTip);
    node.addEventListener("focus", showTip);
    node.addEventListener("mouseleave", hideTip);
    node.addEventListener("blur", hideTip);

    container.appendChild(node);
  }
}

function renderStats() {
  if (!state.sim) return;
  const s = state.sim.stats;
  el("stat-free").textContent = fmtMb(s.total_free_kb);
  el("stat-free-sub").textContent = fmtKb(s.total_free_kb);

  el("stat-used").textContent = fmtMb(s.used_kb);
  el("stat-used-sub").textContent = fmtKb(s.used_kb);

  el("stat-largest").textContent = fmtMb(s.largest_free_kb);
  el("stat-largest-sub").textContent = fmtKb(s.largest_free_kb);

  el("stat-fragments").textContent = String(s.free_fragments);
  el("stat-frag-pct").textContent = `${s.fragmentation_pct.toFixed(2)}%`;

  el("stat-failures").textContent = `${s.frag_failures} / ${s.oom_failures}`;
  el("stat-compactions").textContent = String(s.compactions);
  el("stat-compactions-sub").textContent = `${fmtKb(s.units_moved_kb)} moved`;
  el("stat-wait").textContent = `${s.avg_wait_ticks.toFixed(2)} ticks`;
}

function renderAllocationsTable() {
  const tbody = el("allocs-tbody");
  tbody.innerHTML = "";
  if (!state.sim) return;

  const procs = state.sim.processes;
  const sysBlock = state.sim.blocks.find((b) => b.is_system);
  el("alloc-count-badge").textContent = `${1 + procs.length} partition${procs.length === 0 ? "" : "s"}`;

  if (sysBlock) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>—</td>
      <td><span class="owner-cell"><span class="color-dot" style="background:var(--system-bg)"></span>System (reserved assumption)</span></td>
      <td>—</td>
      <td>${fmtMb(sysBlock.size_kb)} (${fmtKb(sysBlock.size_kb)})</td>
      <td>${fmtRangeMb(sysBlock.start_kb, sysBlock.end_kb)} ([${sysBlock.start_kb}, ${sysBlock.end_kb}) KB)</td>
      <td>Fixed at start</td>
      <td class="col-action"><span class="lock-tag">Locked</span></td>
    `;
    tbody.appendChild(tr);
  }

  for (const p of procs) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>#${p.pid}</td>
      <td><span class="owner-cell"><span class="color-dot" style="background:${colorForPid(p.pid)}"></span>${escapeHtml(p.owner)}</span></td>
      <td>${fmtBytes(p.size_bytes)}</td>
      <td>${fmtMb(p.units_kb)} (${fmtKb(p.units_kb)})</td>
      <td>${fmtRangeMb(p.start_kb, p.end_kb)} ([${p.start_kb}, ${p.end_kb}) KB)</td>
      <td>${escapeHtml(p.strategy)}</td>
      <td class="col-action"></td>
    `;
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn btn-danger-outline btn-sm";
    btn.textContent = "Delete";
    btn.addEventListener("click", () => handleDelete(p.owner));
    tr.querySelector(".col-action").appendChild(btn);
    tbody.appendChild(tr);
  }
}

function renderAll() {
  syncHeaderControls();
  renderMemoryMap();
  renderStats();
  renderAllocationsTable();
}

function clearOutcomeCards() {
  el("outcome-empty").hidden = true;
  el("receipt-card").hidden = true;
  el("failure-card").hidden = true;
  el("diff-card").hidden = true;
}

function showPlacementReceipt(receipt) {
  clearOutcomeCards();
  const card = el("receipt-card");
  card.hidden = false;
  const smartNote = receipt.smart_compacted
    ? ` Smart compacted scattered free blocks first (${fmtKb(receipt.smart_units_moved_kb)} moved), then placed with ${receipt.effective_strategy}.`
    : receipt.strategy === "Smart"
      ? ` Smart picked ${receipt.effective_strategy} (free memory was ${(receipt.pre_total_free_kb / receipt.pool_kb * 100).toFixed(1)}% of pool).`
      : "";

  card.innerHTML = `
    <h3 class="outcome-title">Stored in device memory</h3>
    <p class="outcome-headline">Stored in device memory at ${fmtRangeMb(receipt.start_kb, receipt.end_kb)} ([${receipt.start_kb}, ${receipt.end_kb}) KB)</p>
    <dl class="outcome-kv">
      <div><dt>Process ID</dt><dd>#${receipt.pid} (${escapeHtml(receipt.owner)})</dd></div>
      <div><dt>Allocated size</dt><dd>${fmtMb(receipt.units_kb)} (${fmtKb(receipt.units_kb)})</dd></div>
      <div><dt>Strategy used</dt><dd>${escapeHtml(receipt.strategy)}</dd></div>
      <div><dt>Blocks searched</dt><dd>${receipt.blocks_searched}</dd></div>
      <div><dt>Split from free block</dt><dd>[${receipt.split_from_start_kb}, ${receipt.split_from_end_kb}) KB (${fmtKb(receipt.split_from_size_kb)})</dd></div>
      <div><dt>Leftover free size</dt><dd>${fmtMb(receipt.leftover_kb)} (${fmtKb(receipt.leftover_kb)})</dd></div>
    </dl>
    <p style="margin: 10px 0 0; font-size: 0.82rem;">${escapeHtml(STRATEGY_REASONS[receipt.strategy] || "")}${escapeHtml(smartNote)}</p>
  `;
}

function showFailureCard(res) {
  clearOutcomeCards();
  const card = el("failure-card");
  card.hidden = false;
  const isFrag = res.failure_kind === 1;
  const title = isFrag ? "Fragmentation failure" : "Out-of-memory failure";
  const explanation = isFrag
    ? `Total free memory (${fmtKb(res.total_free_kb)}) is enough for ${fmtKb(res.requested_kb)}, but it is split across ${res.free_fragments} blocks and the largest block is only ${fmtKb(res.largest_free_kb)}.`
    : res.reason === "exceeds_pool"
      ? `Requested ${fmtKb(res.requested_kb)} exceeds the entire ${fmtMb(res.pool_kb)} (${fmtKb(res.pool_kb)}) device pool.`
      : `Requested ${fmtKb(res.requested_kb)}, but total free memory is only ${fmtKb(res.total_free_kb)} across the pool.`;

  card.innerHTML = `
    <h3 class="outcome-title">${title}</h3>
    <p class="outcome-headline">${explanation}</p>
    <dl class="outcome-kv">
      <div><dt>Requested</dt><dd>${fmtMb(res.requested_kb)} (${fmtKb(res.requested_kb)})</dd></div>
      <div><dt>Total free</dt><dd>${fmtMb(res.total_free_kb)} (${fmtKb(res.total_free_kb)})</dd></div>
      <div><dt>Largest free block</dt><dd>${fmtMb(res.largest_free_kb)} (${fmtKb(res.largest_free_kb)})</dd></div>
      <div><dt>Blocks searched</dt><dd>${res.blocks_searched}</dd></div>
    </dl>
    <div class="outcome-actions" id="failure-actions"></div>
  `;

  if (res.can_compact_and_retry && state.stagedFile) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn btn-primary btn-sm";
    btn.textContent = "Compact and retry";
    btn.addEventListener("click", handleCompactAndRetry);
    el("failure-actions").appendChild(btn);
  }
}

function buildMiniMapHtml(blocks, poolKb) {
  return blocks.map((b) => {
    const pct = (b.size_kb / poolKb) * 100;
    const cls = b.is_free ? "mini-block is-free" : b.is_system ? "mini-block is-system" : "mini-block";
    const bg = (!b.is_free && !b.is_system) ? `background:${colorForPid(b.pid)};` : "";
    const label = b.is_free ? `Free [${b.start_kb}, ${b.end_kb})` : `${b.owner} [${b.start_kb}, ${b.end_kb})`;
    return `<div class="${cls}" style="width:${pct}%;${bg}" title="${escapeHtml(label)} · ${fmtKb(b.size_kb)}"></div>`;
  }).join("");
}

function showDeleteDiff(diff) {
  clearOutcomeCards();
  const card = el("diff-card");
  card.hidden = false;

  let mergeText = "No adjacent free neighbours to merge with.";
  if (diff.merged_left && diff.merged_right) {
    mergeText = `Merged with left free block [${diff.left_start_kb}, ${diff.freed_start_kb}) KB (${fmtKb(diff.left_size_kb)}) and right free block [${diff.right_start_kb}, ${diff.right_start_kb + diff.right_size_kb}) KB (${fmtKb(diff.right_size_kb)}) into [${diff.coalesced_start_kb}, ${diff.coalesced_end_kb}) KB.`;
  } else if (diff.merged_left) {
    mergeText = `Merged with left free neighbour [${diff.left_start_kb}, ${diff.freed_start_kb}) KB (${fmtKb(diff.left_size_kb)}) into [${diff.coalesced_start_kb}, ${diff.coalesced_end_kb}) KB.`;
  } else if (diff.merged_right) {
    mergeText = `Merged with right free neighbour [${diff.right_start_kb}, ${diff.right_start_kb + diff.right_size_kb}) KB (${fmtKb(diff.right_size_kb)}) into [${diff.coalesced_start_kb}, ${diff.coalesced_end_kb}) KB.`;
  }

  const poolKb = diff.after_stats.pool_kb;
  card.innerHTML = `
    <h3 class="outcome-title">Deleted ${escapeHtml(diff.owner)}</h3>
    <p class="outcome-headline">Freed ${fmtMb(diff.freed_kb)} (${fmtKb(diff.freed_kb)}) from device memory.</p>
    <p style="margin:0 0 10px;">${mergeText}</p>
    <dl class="outcome-kv">
      <div><dt>Largest free block</dt><dd>${fmtKb(diff.before_stats.largest_free_kb)} → ${fmtKb(diff.after_stats.largest_free_kb)}</dd></div>
      <div><dt>External fragmentation</dt><dd>${diff.before_stats.fragmentation_pct.toFixed(2)}% → ${diff.after_stats.fragmentation_pct.toFixed(2)}%</dd></div>
    </dl>
    <div class="diff-bars">
      <div>
        <div class="diff-row-label">Before delete (${diff.before_stats.free_fragments} free block${diff.before_stats.free_fragments === 1 ? "" : "s"})</div>
        <div class="mini-map">${buildMiniMapHtml(diff.before_blocks, poolKb)}</div>
      </div>
      <div>
        <div class="diff-row-label">After delete (${diff.after_stats.free_fragments} free block${diff.after_stats.free_fragments === 1 ? "" : "s"})</div>
        <div class="mini-map">${buildMiniMapHtml(diff.after_blocks, poolKb)}</div>
      </div>
    </div>
  `;
}

function showCompactDiff(diff) {
  clearOutcomeCards();
  const card = el("diff-card");
  card.hidden = false;
  const poolKb = diff.after_stats.pool_kb;

  card.innerHTML = `
    <h3 class="outcome-title">Device memory compacted</h3>
    <p class="outcome-headline">Moved ${fmtMb(diff.units_moved_kb)} (${fmtKb(diff.units_moved_kb)}) to consolidate free memory into a single contiguous block.</p>
    <dl class="outcome-kv">
      <div><dt>Largest free block</dt><dd>${fmtKb(diff.before_stats.largest_free_kb)} → ${fmtKb(diff.after_stats.largest_free_kb)}</dd></div>
      <div><dt>External fragmentation</dt><dd>${diff.before_stats.fragmentation_pct.toFixed(2)}% → ${diff.after_stats.fragmentation_pct.toFixed(2)}%</dd></div>
    </dl>
    <div class="diff-bars">
      <div>
        <div class="diff-row-label">Before compaction (${diff.before_stats.free_fragments} free block${diff.before_stats.free_fragments === 1 ? "" : "s"})</div>
        <div class="mini-map">${buildMiniMapHtml(diff.before_blocks, poolKb)}</div>
      </div>
      <div>
        <div class="diff-row-label">After compaction (${diff.after_stats.free_fragments} free block${diff.after_stats.free_fragments === 1 ? "" : "s"})</div>
        <div class="mini-map">${buildMiniMapHtml(diff.after_blocks, poolKb)}</div>
      </div>
    </div>
  `;
}

async function runPrecheck(file) {
  state.stagedFile = file;
  el("precheck-card").hidden = false;
  el("pre-name").textContent = file.name;
  el("pre-bytes").textContent = fmtBytes(file.size);
  el("pre-kb").textContent = file.size === 0 ? "0 KB" : fmtKb(Math.ceil(file.size / 1024));
  el("pre-total-free").textContent = fmtKb(state.sim.stats.total_free_kb);
  el("pre-largest-free").textContent = fmtKb(state.sim.stats.largest_free_kb);

  const fitBox = el("pre-fit-box");
  const confirmBtn = el("confirm-alloc-btn");

  if (file.size > 260 * 1024 * 1024) {
    fitBox.className = "fit-box is-err";
    fitBox.textContent = "File exceeds the 260 MB upload cap for this simulator server.";
    confirmBtn.disabled = true;
    return;
  }

  try {
    const res = await apiJson("/api/precheck", {
      method: "POST",
      headers: {
        "X-File-Name": encodeURIComponent(file.name),
        "X-Strategy": state.strategy
      },
      body: file
    });
    state.precheck = res;

    if (!res.ok) {
      fitBox.className = "fit-box is-err";
      fitBox.textContent = res.error || "File cannot be stored.";
      confirmBtn.disabled = true;
      return;
    }

    el("pre-bytes").textContent = fmtBytes(res.size_bytes);
    el("pre-kb").textContent = `${fmtKb(res.units_kb)} (${fmtMb(res.units_kb)})`;
    el("pre-total-free").textContent = `${fmtKb(res.total_free_kb)} (${fmtMb(res.total_free_kb)})`;
    el("pre-largest-free").textContent = `${fmtKb(res.largest_free_kb)} (${fmtMb(res.largest_free_kb)})`;

    if (!res.name_valid) {
      fitBox.className = "fit-box is-err";
      fitBox.textContent = res.name_error;
      confirmBtn.disabled = true;
      return;
    }

    confirmBtn.disabled = false;
    if (res.smart_compact_first) {
      fitBox.className = "fit-box is-warn";
      fitBox.textContent = `Will fit after Smart compaction: total free (${fmtKb(res.total_free_kb)}) is sufficient, though largest single block is ${fmtKb(res.largest_free_kb)}. Smart will compact first and place with First Fit.`;
    } else if (res.will_fit && res.candidate) {
      const c = res.candidate;
      fitBox.className = "fit-box is-ok";
      fitBox.textContent = `Fits! ${res.strategy_label} (${res.effective_strategy}) will pick free block [${c.start_kb}, ${c.end_kb}) KB (${fmtRangeMb(c.start_kb, c.end_kb)}, ${fmtKb(c.size_kb)}), leaving ${fmtKb(c.leftover_kb)} free after searching ${res.blocks_searched} block(s).`;
    } else if (res.failure_kind === 1) {
      fitBox.className = "fit-box is-warn";
      fitBox.textContent = `Fragmentation block: ${fmtKb(res.total_free_kb)} total free is enough for ${fmtKb(res.units_kb)}, but the largest block is ${fmtKb(res.largest_free_kb)}. Click store to inspect the failure & Compact and retry.`;
    } else {
      fitBox.className = "fit-box is-err";
      fitBox.textContent = `Out of memory: requires ${fmtKb(res.units_kb)}, which exceeds ${res.units_kb > res.pool_kb ? `the ${fmtKb(res.pool_kb)} pool` : `total free (${fmtKb(res.total_free_kb)})`}.`;
    }
  } catch (err) {
    fitBox.className = "fit-box is-err";
    fitBox.textContent = `Pre-check failed: ${err.message}`;
    confirmBtn.disabled = true;
  }
}

async function handleConfirmAlloc() {
  if (!state.stagedFile) return;
  const file = state.stagedFile;
  const res = await apiJson("/api/alloc", {
    method: "POST",
    headers: {
      "X-File-Name": encodeURIComponent(file.name),
      "X-Strategy": state.strategy
    },
    body: file
  });

  if (res.blocks) {
    state.sim = res;
  }

  if (res.ok) {
    state.lastPlacedOwner = res.receipt.owner;
    showPlacementReceipt(res.receipt);
    renderAll();
    clearStagedFile();
  } else if (res.failure_kind) {
    state.lastPlacedOwner = null;
    showFailureCard(res);
    renderAll();
  } else {
    el("pre-fit-box").className = "fit-box is-err";
    el("pre-fit-box").textContent = res.error || "Allocation rejected.";
  }
}

async function handleCompactAndRetry() {
  if (!state.stagedFile) return;
  const compactRes = await apiJson("/api/compact", { method: "POST", body: "{}" });
  if (compactRes.ok) {
    state.sim = compactRes;
    renderAll();
  }
  await handleConfirmAlloc();
}

async function handleDelete(owner) {
  const res = await apiJson("/api/free", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ owner })
  });
  if (!res.ok) {
    alert(res.error || "Delete failed.");
    return;
  }
  state.lastPlacedOwner = null;
  state.sim = res;
  showDeleteDiff(res.delete_diff);
  renderAll();
  if (state.stagedFile) {
    await runPrecheck(state.stagedFile);
  }
}

async function handleCompact() {
  const res = await apiJson("/api/compact", { method: "POST", body: "{}" });
  if (!res.ok) return;
  state.lastPlacedOwner = null;
  state.sim = res;
  showCompactDiff(res.compact_diff);
  renderAll();
  if (state.stagedFile) {
    await runPrecheck(state.stagedFile);
  }
}

async function handleInitPool(poolKb) {
  const res = await apiJson("/api/init", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ pool_kb: poolKb })
  });
  if (!res.ok) return;
  state.lastPlacedOwner = null;
  state.sim = res;
  el("outcome-empty").hidden = false;
  el("receipt-card").hidden = true;
  el("failure-card").hidden = true;
  el("diff-card").hidden = true;
  renderAll();
  if (state.stagedFile) {
    await runPrecheck(state.stagedFile);
  }
}

function clearStagedFile() {
  state.stagedFile = null;
  state.precheck = null;
  el("file-input").value = "";
  el("precheck-card").hidden = true;
}

function setupEvents() {
  el("theme-toggle").addEventListener("click", () => {
    const cur = document.documentElement.getAttribute("data-theme");
    applyTheme(cur === "dark" ? "light" : "dark");
  });

  document.querySelectorAll("#capacity-selector .seg-btn").forEach((btn) => {
    btn.addEventListener("click", () => handleInitPool(Number(btn.dataset.pool)));
  });

  document.querySelectorAll("#strategy-selector .seg-btn").forEach((btn) => {
    btn.addEventListener("click", async () => {
      state.strategy = btn.dataset.strategy;
      document.querySelectorAll("#strategy-selector .seg-btn").forEach((b) => {
        b.classList.toggle("is-active", b === btn);
      });
      if (state.stagedFile) {
        await runPrecheck(state.stagedFile);
      }
    });
  });

  el("compact-btn").addEventListener("click", handleCompact);
  el("reset-btn").addEventListener("click", () => {
    clearStagedFile();
    handleInitPool(state.sim ? state.sim.pool_kb : 65536);
  });

  const dropzone = el("dropzone");
  const fileInput = el("file-input");

  dropzone.addEventListener("click", () => fileInput.click());
  dropzone.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      fileInput.click();
    }
  });
  fileInput.addEventListener("change", () => {
    if (fileInput.files && fileInput.files[0]) {
      runPrecheck(fileInput.files[0]);
    }
  });
  dropzone.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropzone.classList.add("is-dragover");
  });
  dropzone.addEventListener("dragleave", () => dropzone.classList.remove("is-dragover"));
  dropzone.addEventListener("drop", (e) => {
    e.preventDefault();
    dropzone.classList.remove("is-dragover");
    if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]) {
      runPrecheck(e.dataTransfer.files[0]);
    }
  });

  el("clear-file-btn").addEventListener("click", clearStagedFile);
  el("confirm-alloc-btn").addEventListener("click", handleConfirmAlloc);

  document.querySelectorAll(".mode-tab").forEach((tab) => {
    tab.addEventListener("click", () => {
      const target = tab.dataset.tab;
      document.querySelectorAll(".mode-tab").forEach((t) => {
        const active = t === tab;
        t.classList.toggle("is-active", active);
        t.setAttribute("aria-selected", active ? "true" : "false");
      });
      el("panel-interactive").hidden = target !== "interactive";
      el("panel-simulation").hidden = target !== "simulation";
    });
  });
}

async function boot() {
  initTheme();
  setupEvents();
  state.sim = await apiJson("/api/state");
  renderAll();
}

boot();
