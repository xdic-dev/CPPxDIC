"use strict";

// ---------- tiny helpers ----------
const $ = (sel) => document.querySelector(sel);
const api = async (url, opts) => {
  const r = await fetch(url, opts);
  if (!r.ok) {
    const detail = await r.json().catch(() => ({}));
    throw new Error(detail.detail || `${r.status} ${r.statusText}`);
  }
  return r.json();
};
const setStatus = (el, msg, kind) => {
  el.textContent = msg;
  el.className = "status" + (kind ? " " + kind : "");
};

// ---------- tab switching ----------
document.querySelectorAll(".tab").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
    btn.classList.add("active");
    $("#tab-" + btn.dataset.tab).classList.add("active");
  });
});

// ============================================================
// Feature 1: parameter forms
// ============================================================
const paramFileSel = $("#param-file");
const paramForm = $("#param-form");

async function initConfig() {
  const cfg = await api("/api/config");
  $("#repo-root").textContent = "repo: " + cfg.repo_root;
  paramFileSel.innerHTML = cfg.target_files
    .map((f) => `<option value="${f}">${f}</option>`)
    .join("");
  if (cfg.data_path) $("#media-datapath").placeholder = cfg.data_path;
  if (cfg.dic_path) $("#plot-root").placeholder = cfg.dic_path;
  await loadParams();
}

function fieldInput(field, value) {
  const t = field.type;
  if (t === "bool") {
    return `<input type="checkbox" data-key="${field.key}" data-type="bool" ${value ? "checked" : ""}/>`;
  }
  if (t === "enum") {
    const opts = field.enum
      .map((o) => `<option value="${o}" ${o === value ? "selected" : ""}>${o === "" ? "(unset)" : o}</option>`)
      .join("");
    return `<select data-key="${field.key}" data-type="enum">${opts}</select>`;
  }
  if (t === "int" || t === "float") {
    const step = t === "int" ? "1" : "any";
    return `<input type="number" step="${step}" data-key="${field.key}" data-type="${t}" value="${value ?? ""}"/>`;
  }
  if (t === "int_list" || t === "double_list" || t === "string_list") {
    const v = Array.isArray(value) ? value.join(", ") : value ?? "";
    return `<input type="text" class="list" data-key="${field.key}" data-type="${t}" value="${v}" placeholder="comma,separated"/>`;
  }
  // string
  return `<input type="text" data-key="${field.key}" data-type="string" value="${value ?? ""}"/>`;
}

async function loadParams() {
  const file = paramFileSel.value;
  setStatus($("#param-status"), "loading…");
  const [{ groups }, { values }] = await Promise.all([
    api("/api/schema?file=" + encodeURIComponent(file)),
    api("/api/params?file=" + encodeURIComponent(file)),
  ]);
  paramForm.innerHTML = groups
    .map((g) => {
      const fields = g.fields
        .map((f) => {
          const isCheckbox = f.type === "bool";
          const control = fieldInput(f, values[f.key]);
          if (isCheckbox) {
            return `<div class="field"><div class="field-row">${control}
              <label class="key">${f.key}</label></div>
              <span class="help">${f.help}</span></div>`;
          }
          return `<div class="field"><label class="key">${f.key}</label>
            ${control}<span class="help">${f.help}</span></div>`;
        })
        .join("");
      return `<fieldset class="group"><legend>${g.title}</legend>
        <div class="field-grid">${fields}</div></fieldset>`;
    })
    .join("");
  setStatus($("#param-status"), `loaded ${file}`, "ok");
}

function collectParams() {
  const out = {};
  paramForm.querySelectorAll("[data-key]").forEach((el) => {
    const key = el.dataset.key;
    const type = el.dataset.type;
    if (type === "bool") out[key] = el.checked;
    else if (type === "int") out[key] = el.value === "" ? null : parseInt(el.value, 10);
    else if (type === "float") out[key] = el.value === "" ? null : parseFloat(el.value);
    else if (type.endsWith("_list")) {
      const parts = el.value.split(",").map((s) => s.trim()).filter((s) => s !== "");
      out[key] = type === "string_list" ? parts
        : type === "int_list" ? parts.map((s) => parseInt(s, 10))
        : parts.map((s) => parseFloat(s));
    } else out[key] = el.value;
  });
  return out;
}

async function saveParams() {
  const file = paramFileSel.value;
  setStatus($("#param-status"), "saving…");
  try {
    const res = await api("/api/params?file=" + encodeURIComponent(file), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ values: collectParams() }),
    });
    setStatus($("#param-status"), "saved → " + res.written, "ok");
  } catch (e) {
    setStatus($("#param-status"), "error: " + e.message, "err");
  }
}

$("#param-load").addEventListener("click", loadParams);
$("#param-save").addEventListener("click", saveParams);
paramFileSel.addEventListener("change", loadParams);

// ============================================================
// Feature 2: media viewer
// ============================================================
const mediaVideoSel = $("#media-video");
const mediaFrame = $("#media-frame");

async function refreshVideos() {
  const dp = $("#media-datapath").value.trim();
  setStatus($("#media-status"), "scanning…");
  try {
    const res = await api("/api/media/videos?data_path=" + encodeURIComponent(dp));
    mediaVideoSel.innerHTML = res.videos.map((v) => `<option>${v}</option>`).join("");
    setStatus($("#media-status"), `${res.videos.length} videos under ${res.data_path}`, "ok");
    if (res.videos.length) await onVideoChange();
  } catch (e) {
    setStatus($("#media-status"), "error: " + e.message, "err");
  }
}

async function onVideoChange() {
  const dp = $("#media-datapath").value.trim();
  const path = mediaVideoSel.value;
  if (!path) return;
  try {
    const info = await api(
      `/api/media/info?path=${encodeURIComponent(path)}&data_path=${encodeURIComponent(dp)}`
    );
    const max = Math.max(0, info.frames - 1);
    mediaFrame.max = max;
    mediaFrame.value = 0;
    $("#media-frame-max").textContent = max;
    showFrame();
  } catch (e) {
    setStatus($("#media-status"), "error: " + e.message, "err");
  }
}

function showFrame() {
  const dp = $("#media-datapath").value.trim();
  const path = mediaVideoSel.value;
  if (!path) return;
  const idx = mediaFrame.value;
  $("#media-frame-val").textContent = idx;
  const filter = $("#media-filter").checked;
  const limit = $("#media-limit").value;
  const url = `/api/media/frame?path=${encodeURIComponent(path)}&index=${idx}` +
    `&filter=${filter}&limit_grayscale=${limit}&data_path=${encodeURIComponent(dp)}&_=${Date.now()}`;
  $("#media-image").src = url;
}

function showOverlay() {
  const dp = $("#media-datapath").value.trim();
  const path = mediaVideoSel.value;
  if (!path) return;
  const idx = mediaFrame.value;
  const mask = $("#media-mask").value.trim();
  const seed = $("#media-seed").value.trim();
  const url = `/api/media/overlay?path=${encodeURIComponent(path)}&index=${idx}` +
    `&mask=${encodeURIComponent(mask)}&seed=${encodeURIComponent(seed)}` +
    `&data_path=${encodeURIComponent(dp)}&_=${Date.now()}`;
  $("#media-image").src = url;
  setStatus($("#media-status"), "overlay rendered", "ok");
}

$("#media-refresh").addEventListener("click", refreshVideos);
mediaVideoSel.addEventListener("change", onVideoChange);
mediaFrame.addEventListener("input", showFrame);
$("#media-filter").addEventListener("change", showFrame);
$("#media-limit").addEventListener("change", showFrame);
$("#media-show-overlay").addEventListener("click", showOverlay);

// ============================================================
// Feature 3: interactive plots
// ============================================================
const plotFileSel = $("#plot-file");
const plotFieldSel = $("#plot-field");
const plotFrame = $("#plot-frame");
const plotScalarSel = $("#plot-scalar");

async function refreshResults() {
  const root = $("#plot-root").value.trim();
  setStatus($("#plot-status"), "scanning…");
  try {
    const res = await api("/api/results/list?root=" + encodeURIComponent(root));
    plotFileSel.innerHTML = res.files.map((f) => `<option>${f}</option>`).join("");
    setStatus($("#plot-status"), `${res.files.length} result files under ${res.root}`, "ok");
  } catch (e) {
    setStatus($("#plot-status"), "error: " + e.message, "err");
  }
}

async function loadResult() {
  const root = $("#plot-root").value.trim();
  const path = plotFileSel.value;
  if (!path) return;
  const isNcorr = /ncorr/i.test(path);
  $("#plot-2d-controls").hidden = !isNcorr;
  $("#plot-3d-controls").hidden = isNcorr;
  Plotly.purge("plot-2d");
  Plotly.purge("plot-3d");
  setStatus($("#plot-status"), "loading…");
  try {
    if (isNcorr) await loadNcorr(root, path);
    else await loadDic3d(root, path);
    setStatus($("#plot-status"), "loaded " + path, "ok");
  } catch (e) {
    setStatus($("#plot-status"), "error: " + e.message, "err");
  }
}

async function loadNcorr(root, path) {
  const meta = await api(
    `/api/results/ncorr?path=${encodeURIComponent(path)}&root=${encodeURIComponent(root)}`
  );
  if (!meta.available.length) throw new Error("no U/V/C fields found");
  plotFieldSel.innerHTML = meta.available.map((f) => `<option>${f}</option>`).join("");
  const nframes = Math.max(...meta.available.map((f) => meta.frames[f]));
  plotFrame.max = Math.max(0, nframes - 1);
  plotFrame.value = 0;
  $("#plot-frame-max").textContent = plotFrame.max;
  plotFieldSel.onchange = () => drawNcorr(root, path);
  plotFrame.oninput = () => drawNcorr(root, path);
  await drawNcorr(root, path);
}

async function drawNcorr(root, path) {
  const field = plotFieldSel.value;
  const frame = plotFrame.value;
  $("#plot-frame-val").textContent = frame;
  const res = await api(
    `/api/results/ncorr_field?path=${encodeURIComponent(path)}&field=${field}` +
    `&frame=${frame}&root=${encodeURIComponent(root)}`
  );
  Plotly.react(
    "plot-2d",
    [{ z: res.z, type: "heatmap", colorscale: "Jet", colorbar: { title: field } }],
    { title: `${field} — frame ${frame}/${res.nframes - 1}`, margin: { t: 40 } },
    { responsive: true }
  );
}

async function loadDic3d(root, path) {
  const data = await api(
    `/api/results/dic3d?path=${encodeURIComponent(path)}&root=${encodeURIComponent(root)}`
  );
  if (!data.points) throw new Error("no 3D points in file. keys: " + (data.keys || []).join(", "));
  plotScalarSel.innerHTML = (data.scalar_fields || [])
    .map((f) => `<option>${f}</option>`)
    .join("");
  plotScalarSel.onchange = () => drawDic3d(root, path, data);
  await drawDic3d(root, path, data);
}

async function drawDic3d(root, path, data) {
  const pts = data.points;
  const x = pts.map((p) => p[0]);
  const y = pts.map((p) => p[1]);
  const z = pts.map((p) => p[2]);
  let intensity = null;
  const field = plotScalarSel.value;
  if (field) {
    const sc = await api(
      `/api/results/dic3d_scalar?path=${encodeURIComponent(path)}&field=${field}` +
      `&root=${encodeURIComponent(root)}`
    );
    intensity = sc.values;
  }
  const trace = data.faces
    ? {
        type: "mesh3d",
        x, y, z,
        i: data.faces.map((f) => f[0]),
        j: data.faces.map((f) => f[1]),
        k: data.faces.map((f) => f[2]),
        intensity: intensity && intensity.length === pts.length ? intensity : undefined,
        colorscale: "Jet",
        showscale: !!intensity,
      }
    : {
        type: "scatter3d",
        mode: "markers",
        x, y, z,
        marker: {
          size: 2,
          color: intensity || z,
          colorscale: "Jet",
          showscale: true,
        },
      };
  Plotly.react("plot-3d", [trace],
    { title: field || "3D surface", margin: { t: 40 }, scene: { aspectmode: "data" } },
    { responsive: true });
}

$("#plot-refresh").addEventListener("click", refreshResults);
$("#plot-load").addEventListener("click", loadResult);

// ---------- boot ----------
initConfig().catch((e) => setStatus($("#param-status"), "init error: " + e.message, "err"));
