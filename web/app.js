"use strict";

// ---- Bridge to the plugin -------------------------------------------------------
const backend = window.__JUCE__ && window.__JUCE__.backend;

function send(eventId, payload) {
  if (backend) backend.emitEvent(eventId, payload || {});
}

function log(type, data) {
  const entry = Object.assign({ type: type }, data || {});
  console.debug("[transcriber]", JSON.stringify(entry));
  send("log", entry);
}

function on(eventId, fn) {
  if (backend) backend.addEventListener(eventId, fn);
}

window.addEventListener("error", function (e) {
  log("jsError", { message: e.message, source: e.filename, line: e.lineno });
});
window.addEventListener("unhandledrejection", function (e) {
  log("jsError", { message: String(e.reason) });
});

const $ = function (id) { return document.getElementById(id); };

// ---- Capture ------------------------------------------------------------------------
let capStatus = null;
let capPreview = null;

const stateLabels = {
  idle: "Idle",
  armed: "Armed: waiting for Live to play",
  recording: "Recording",
  stopped: "Stopped"
};

function describeReading(s) {
  if (!s.versions.length && s.state !== "recording" && s.state !== "armed")
    return s.state === "stopped" && s.lastStopEmpty
      ? "Nothing was recorded (no notes were played), so no version was made."
      : "Nothing recorded yet.";
  if (s.state === "armed") return "Waiting for Live to play…";
  if (s.state === "recording")
    return s.rawNotes + " notes so far, " + s.bars + " bars. The reading is chosen when the recording stops.";

  const sources = { host: "from the host's loop", detected: "detected from the notes", user: "set by you", none: "" };
  const lines = [];
  if (s.state === "stopped" && s.lastStopEmpty) {
    const message = "Nothing was recorded (no notes were played), so no version was made.";
    if (!s.versions.length) return message;
    lines.push(message);
  }

  if (s.mode === "oneLoop") {
    lines.push("One loop · " + s.loopBars + " bar" + (s.loopBars === 1 ? "" : "s") + " (" + sources[s.source] + ")");
    lines.push(s.passes + " pass" + (s.passes === 1 ? "" : "es") + " recorded; each pass replaces the one before it. " +
               "If you stopped partway, the end of the loop comes from the previous pass.");
    if (s.source === "detected" && s.passes < 6)
      lines.push("If this was a long clip that was played once and happens to repeat inside itself, press As played.");
  } else {
    lines.push("As played (" + sources[s.source] + ")");
    lines.push("Everything is written out in order, exactly as it was played.");
    if (s.candidateBars)
      lines.push("Closest repeat: every " + s.candidateBars + " bars, but " + Math.round(s.candidateMismatch * 100) +
                 "% of the bars differ, so it was not treated as a loop. Press One loop to use it anyway.");
  }

  return lines.join("\n");
}

// ---- Versions ------------------------------------------------------------------------
let renamingId = null;      // the version whose name is being edited
let confirmingId = null;    // the version waiting for "Delete?" to be confirmed
let lastVersionsKey = "";

// Draws the list again at once (after a click), instead of waiting for the next status from the plugin.
function rerenderVersions() {
  lastVersionsKey = "";
  if (capStatus) renderVersions(capStatus);
}

function formatSize(bytes) {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(2) + " MB";
  if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB";
  return bytes + " bytes";
}

function versionMeta(v) {
  const when = new Date(v.createdAt).toLocaleString([], { dateStyle: "short", timeStyle: "short" });
  const reading = v.mode === "oneLoop" ? "One loop · " + v.loopBars + " bar" + (v.loopBars === 1 ? "" : "s") : "As played";
  return when + " · " + v.notes + " notes · " + reading;
}

function renderVersions(s) {
  const key = JSON.stringify([s.versions, s.activeVersion, renamingId, confirmingId, s.stateBytes, s.stateTooBig, s.loadMessage]);
  if (key === lastVersionsKey) return;   // nothing changed: keep the rename box and its focus
  lastVersionsKey = key;

  const list = $("ver-list");
  list.textContent = "";
  $("ver-count").textContent = s.versions.length ? "(" + s.versions.length + ")" : "";
  $("ver-empty").style.display = s.versions.length ? "none" : "";

  s.versions.forEach(function (v) {
    const li = document.createElement("li");
    if (v.active) li.className = "active";
    li.addEventListener("click", function (e) {
      if (e.target.closest("button, input")) return;
      if (!v.active) send("verSelect", { id: v.id });
    });

    if (renamingId === v.id) {
      const input = document.createElement("input");
      input.type = "text";
      input.value = v.name;
      input.maxLength = 80;
      let done = false;
      const finish = function (commit) {
        if (done) return;
        done = true;
        renamingId = null;
        if (commit && input.value.trim() && input.value.trim() !== v.name) send("verRename", { id: v.id, name: input.value.trim() });
        rerenderVersions();   // draw the list again
      };
      input.addEventListener("keydown", function (e) {
        if (e.key === "Enter") finish(true);
        else if (e.key === "Escape") finish(false);
        e.stopPropagation();
      });
      input.addEventListener("blur", function () { finish(true); });
      li.appendChild(input);
      setTimeout(function () { input.focus(); input.select(); }, 0);
    } else {
      const name = document.createElement("div");
      name.className = "ver-name";
      name.textContent = v.name;
      li.appendChild(name);
    }

    const meta = document.createElement("div");
    meta.className = "ver-meta";
    meta.textContent = versionMeta(v);
    li.appendChild(meta);

    const actions = document.createElement("div");
    actions.className = "ver-actions";

    function button(label, onClick, cls) {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = label;
      if (cls) b.className = cls;
      b.addEventListener("click", function (e) { e.stopPropagation(); onClick(); });
      actions.appendChild(b);
    }

    if (confirmingId === v.id) {
      button("Delete \"" + v.name.slice(0, 18) + (v.name.length > 18 ? "…" : "") + "\"?", function () {
        confirmingId = null;
        send("verDelete", { id: v.id });
      }, "danger");
      button("Cancel", function () { confirmingId = null; rerenderVersions(); });
    } else {
      button("Rename", function () { renamingId = v.id; rerenderVersions(); });
      button("Duplicate", function () { send("verDuplicate", { id: v.id }); });
      button("Delete", function () { confirmingId = v.id; rerenderVersions(); });
    }

    li.appendChild(actions);
    list.appendChild(li);
  });

  const size = $("state-size");
  size.className = s.stateTooBig ? "result bad" : "dim";
  size.textContent = "Saved with the Live set: " + formatSize(s.stateBytes) + (s.stateTooBig
    ? ". This is above " + formatSize(s.stateWarnBytes) + ": delete old versions you no longer need."
    : " (the warning starts at " + formatSize(s.stateWarnBytes) + ")");
  $("load-message").textContent = s.loadMessage || "";
}

on("capture", function (s) {
  capStatus = s;
  renderVersions(s);
  showTranscription(s);

  const pill = $("cap-state");
  pill.textContent = stateLabels[s.state] || s.state;
  pill.className = "pill " + s.state;

  const rec = $("rec");
  rec.className = "record" + (s.state === "recording" || s.state === "armed" ? " active" : "");
  rec.textContent = s.state === "recording" ? "Stop recording" : s.state === "armed" ? "Cancel" : "Record";

  const summary = [];
  summary.push(s.rawNotes + " notes recorded");
  if (s.state !== "idle" || s.rawNotes) summary.push("to beat " + s.endPpq.toFixed(2));
  if (s.incomplete) summary.push("SOME DATA WAS LOST (" + s.dropped + " records dropped)");
  $("cap-summary").textContent = summary.join(" · ");

  // The reading of a take can be changed whenever the take is not being recorded (also after Live was reopened).
  const hasCapture = s.state !== "recording" && s.state !== "armed" && s.rawNotes > 0;
  $("mode-loop").classList.toggle("active", s.mode === "oneLoop");
  $("mode-played").classList.toggle("active", s.mode === "asPlayed");
  $("mode-loop").disabled = $("mode-played").disabled = $("redetect").disabled = !hasCapture;

  const bars = $("loop-bars");
  bars.disabled = !hasCapture;
  if (document.activeElement !== bars && s.loopBars) bars.value = s.loopBars;

  const text = $("reading-text");
  text.className = "result" + (s.incomplete ? " bad" : "");
  text.textContent = describeReading(s);
});

// ---- Notation settings and the score -----------------------------------------------
const majorNames = ["Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#"];
const minorNames = ["Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#"];
const pitchNames = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];

function keyName(fifths, minor) {
  const names = minor ? minorNames : majorNames;
  return names[Math.max(0, Math.min(14, fifths + 7))] + (minor ? " minor" : " major");
}

function signatureText(fifths) {
  if (fifths === 0) return "no sharps or flats";
  return Math.abs(fifths) + (fifths > 0 ? " sharp" : " flat") + (Math.abs(fifths) === 1 ? "" : "s");
}

function noteName(midi) {
  return pitchNames[midi % 12] + (Math.floor(midi / 12) - 1);
}

(function fillKeyList() {
  const select = $("set-key");
  const auto = document.createElement("option");
  auto.value = "auto";
  auto.textContent = "Detect";
  select.appendChild(auto);
  [false, true].forEach(function (minor) {
    for (let tonic = 0; tonic < 12; tonic++) {
      const o = document.createElement("option");
      o.value = tonic + (minor ? "m" : "M");
      o.textContent = pitchNames[tonic] + (minor ? " minor" : " major");
      select.appendChild(o);
    }
  });
})();

function setSetting(name, value) { send("setSetting", { name: name, value: value }); }

$("set-grid").addEventListener("change", function (e) { setSetting("grid", parseInt(e.target.value, 10)); });
$("set-triplets").addEventListener("change", function (e) { setSetting("triplets", e.target.checked); });
$("set-pickup").addEventListener("change", function (e) { setSetting("autoPickup", e.target.checked); });
$("set-split").addEventListener("change", function (e) {
  const v = parseInt(e.target.value, 10);
  if (v >= 21 && v <= 108) setSetting("splitPoint", v);
});
$("set-key").addEventListener("change", function (e) {
  if (e.target.value === "auto") { setSetting("key", -1); return; }
  setSetting("key", parseInt(e.target.value, 10) * 2 + (e.target.value.endsWith("m") ? 1 : 0));
});

function showTranscription(s) {
  const has = !!s.settings;
  ["set-grid", "set-triplets", "set-split", "set-pickup", "set-key"].forEach(function (id) { $(id).disabled = !has || s.state === "recording"; });

  if (!has) {
    $("score-info").textContent = s.state === "recording" ? "The score is made when the recording stops." : "No score yet.";
    $("score-text").textContent = "";
    return;
  }

  const cfg = s.settings, t = s.transcription;
  const idle = function (id) { return document.activeElement !== $(id); };
  if (idle("set-grid")) $("set-grid").value = String(cfg.grid);
  if (idle("set-triplets")) $("set-triplets").checked = cfg.triplets;
  if (idle("set-pickup")) $("set-pickup").checked = cfg.autoPickup;
  if (idle("set-split")) $("set-split").value = cfg.splitPoint;
  if (idle("set-key")) $("set-key").value = cfg.keyTonic < 0 ? "auto" : cfg.keyTonic + (cfg.keyMinor ? "m" : "M");
  $("split-name").textContent = noteName(cfg.splitPoint);

  const lines = [];
  lines.push("Key: " + keyName(t.keyFifths, t.keyMinor) + " (" + signatureText(t.keyFifths) + ")" +
             (cfg.keyTonic < 0 ? ", detected" : ", set by you") + " · " + t.measures + " measure" + (t.measures === 1 ? "" : "s") +
             " · up to " + t.voices + " voice" + (t.voices === 1 ? "" : "s") + " in a hand" +
             (t.edited ? " · edited by you" : ""));
  t.warnings.forEach(function (w) { lines.push("Note: " + w); });
  $("score-info").className = "result" + (t.warnings.length ? " warn" : "");
  $("score-info").textContent = lines.join("\n");
  if ($("score-details").open) $("score-text").textContent = s.scoreText || "";
}

$("score-details").addEventListener("toggle", function () {
  if ($("score-details").open && capStatus) $("score-text").textContent = capStatus.scoreText || "";
});

on("preview", function (p) {
  capPreview = p;
  drawRoll();
});

$("rec").addEventListener("click", function () {
  const state = capStatus ? capStatus.state : "idle";
  send(state === "recording" || state === "armed" ? "capStop" : "capArm");
});
$("mode-loop").addEventListener("click", function () { send("capMode", { mode: "oneLoop" }); });
$("mode-played").addEventListener("click", function () { send("capMode", { mode: "asPlayed" }); });
$("redetect").addEventListener("click", function () { send("capRedetect"); });
$("loop-bars").addEventListener("change", function (e) {
  const bars = parseInt(e.target.value, 10);
  if (bars >= 1) send("capLoopBars", { bars: bars });
});

// A simple piano-roll picture of the score notes. Each pass of a loop gets its own colour.
function drawRoll() {
  const canvas = $("roll");
  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth, height = canvas.clientHeight;
  if (!width || !height) return;

  if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
  }

  const g = canvas.getContext("2d");
  g.setTransform(ratio, 0, 0, ratio, 0, 0);
  g.clearRect(0, 0, width, height);

  const note = $("roll-note");
  if (!capPreview || !capPreview.notes.length) {
    g.fillStyle = "#9aa3b5";
    g.font = "13px Segoe UI, sans-serif";
    g.fillText("The recorded notes appear here.", 12, 24);
    note.textContent = "";
    return;
  }

  const notes = capPreview.notes, bars = capPreview.bars;
  let lowest = 127, highest = 0, end = capPreview.length;
  for (let i = 0; i < notes.length; i += 5) {
    lowest = Math.min(lowest, notes[i + 2]);
    highest = Math.max(highest, notes[i + 2]);
    end = Math.max(end, notes[i + 1]);
  }
  lowest = Math.max(0, lowest - 2);
  highest = Math.min(127, highest + 2);

  const left = 8, top = 16, plotW = width - left - 8, plotH = height - top - 8;
  const x = function (ppq) { return left + ppq / end * plotW; };
  const rowH = plotH / (highest - lowest + 1);

  // bar lines
  g.font = "11px Segoe UI, sans-serif";
  g.textBaseline = "top";
  for (let i = 0; i < bars.length; i += 3) {
    const bx = x(bars[i]);
    g.strokeStyle = "#2e3542";
    g.beginPath(); g.moveTo(bx, top); g.lineTo(bx, top + plotH); g.stroke();
    g.fillStyle = "#6b7488";
    g.fillText(String(i / 3 + 1), bx + 3, 2);
  }

  for (let i = 0; i < notes.length; i += 5) {
    const nx = x(notes[i]);
    const nw = Math.max(2, x(notes[i + 1]) - nx - 1);
    const ny = top + (highest - notes[i + 2]) * rowH;
    g.fillStyle = "hsl(" + ((notes[i + 4] * 47 + 210) % 360) + ", 65%, 62%)";
    g.fillRect(nx, ny, nw, Math.max(2, rowH - 1));
  }

  note.textContent = (notes.length / 5) + " notes" + (capPreview.truncated ? " (only the first part is drawn)" : "") +
    " · " + end.toFixed(2) + " quarter notes";
}

window.addEventListener("resize", drawRoll);

// ---- Host status (tests 1.1, 1.2, 1.8) ------------------------------------------
const statusFields = [
  ["host", "Host"],
  ["hasPos", "Position info"],
  ["playing", "Playing"],
  ["looping", "Looping"],
  ["offline", "Offline render"],
  ["ppq", "Position (quarter notes)"],
  ["barPpq", "Last bar start"],
  ["bars", "Bar count"],
  ["bpm", "Tempo (BPM)"],
  ["ts", "Time signature"],
  ["loop", "Loop points"],
  ["sampleRate", "Sample rate"],
  ["diagnostics", "Diagnostic log"],
  ["logLines", "Log lines written"],
  ["dropped", "Dropped log records"],
  ["scale", "Window scale"],
  ["logFile", "Log file"]
];

function formatValue(key, value) {
  if (value === undefined) return "not provided";
  if (typeof value === "boolean") return value ? "yes" : "no";
  if (typeof value === "number" && (key === "ppq" || key === "barPpq")) return value.toFixed(3);
  if (typeof value === "number" && key === "bpm") return value.toFixed(2);
  return String(value);
}

on("status", function (s) {
  $("diagnostics").checked = !!s.diagnostics;
  $("instance").textContent = "Instance " + s.instance;

  const dl = $("status");
  dl.textContent = "";

  statusFields.forEach(function (field) {
    const dt = document.createElement("dt");
    const dd = document.createElement("dd");
    dt.textContent = field[1];
    dd.textContent = formatValue(field[0], s[field[0]]);
    dl.append(dt, dd);
  });
});

$("diagnostics").addEventListener("change", function (e) { send("diagnostics", { enabled: e.target.checked }); });
$("open-log").addEventListener("click", function () { send("openLogFolder"); });

// ---- Timing comparison (test 1.3) -----------------------------------------------
$("compare").addEventListener("click", function () { send("compareMidi"); });

on("compareResult", function (r) {
  const out = $("compare-result");

  if (r.error) {
    out.className = "result bad";
    out.textContent = r.error;
    return;
  }

  const lines = [
    (r.passed ? "PASSED" : "FAILED") + "  (tolerance " + r.tolerance + " quarter notes)",
    "Notes in file: " + r.fileNotes + "   captured: " + r.capturedNotes + "   matched: " + r.matched,
    "Missing: " + r.missing + "   extra: " + r.extra,
    "Max onset error: " + r.maxOnsetError.toFixed(5) + "   mean: " + r.meanOnsetError.toFixed(5),
    "Max duration difference: " + r.maxDurationError.toFixed(5),
    "Clip offset in song: " + r.offsetQuarterNotes.toFixed(3) + " quarter notes"
  ];

  if (r.firstMissing && r.firstMissing.length)
    lines.push("First missing: " + JSON.stringify(r.firstMissing));
  if (r.firstExtra && r.firstExtra.length)
    lines.push("First extra: " + JSON.stringify(r.firstExtra));

  out.className = "result " + (r.passed ? "ok" : "bad");
  out.textContent = lines.join("\n");
});

// ---- Keyboard (test 1.4) ----------------------------------------------------------
const keyLog = $("key-log");

function recordKey(e) {
  const mods = ["ctrlKey", "altKey", "shiftKey", "metaKey"].filter(function (m) { return e[m]; })
    .map(function (m) { return m.replace("Key", ""); });
  const text = e.type + "  key=" + JSON.stringify(e.key) + "  code=" + e.code + (mods.length ? "  +" + mods.join("+") : "");

  const li = document.createElement("li");
  li.textContent = text;
  keyLog.prepend(li);
  while (keyLog.children.length > 40) keyLog.lastChild.remove();

  log("key", { event: e.type, key: e.key, code: e.code, mods: mods.join("+") });

  // Keep focus in the box so Tab, arrows and Space can be tested too.
  if (e.type === "keydown") e.preventDefault();
}

$("key-target").addEventListener("keydown", recordKey);
$("key-target").addEventListener("keyup", recordKey);
$("key-target").addEventListener("focus", function () { log("keyTargetFocus"); });
$("key-target").addEventListener("blur", function () { log("keyTargetBlur"); });

// ---- Display (test 1.4) ----------------------------------------------------------
let displayTimer = null;

function logDisplay() {
  log("display", {
    devicePixelRatio: window.devicePixelRatio,
    innerWidth: window.innerWidth,
    innerHeight: window.innerHeight,
    userAgent: navigator.userAgent
  });
}

window.addEventListener("resize", function () {
  clearTimeout(displayTimer);
  displayTimer = setTimeout(logDisplay, 300);
});

// Moving the window to a monitor with different scaling changes devicePixelRatio
// without a resize event, so watch the ratio itself.
function watchPixelRatio() {
  matchMedia("(resolution: " + window.devicePixelRatio + "dppx)")
    .addEventListener("change", function () { logDisplay(); watchPixelRatio(); }, { once: true });
}
watchPixelRatio();

// ---- Notation (tests 1.5, 1.6) ---------------------------------------------------
const samples = ["piano.mei", "drums.mei", "guitar.mei"];
const screenOptions = { pageWidth: 2100, pageHeight: 2970, scale: 40, adjustPageHeight: true,
                        footer: "none", font: "Bravura", breaks: "auto" };
// A4 in mm at a smaller notation size: page dimensions scaled up by the same factor as scale.
const pdfOptions = { pageWidth: 4200, pageHeight: 5940, scale: 50, adjustPageHeight: false,
                     mmOutput: true, footer: "none", font: "Bravura", breaks: "auto" };

let toolkit = null;
const meiCache = {};

function loadMei(name) {
  if (meiCache[name]) return Promise.resolve(meiCache[name]);
  return fetch(name).then(function (r) {
    if (!r.ok) throw new Error("Could not load " + name + " (" + r.status + ")");
    return r.text();
  }).then(function (text) { meiCache[name] = text; return text; });
}

function renderPages(options, mei) {
  toolkit.setOptions(options);   // options must be set before loadData
  toolkit.loadData(mei);
  const pages = [];
  for (let p = 1; p <= toolkit.getPageCount(); p++) pages.push(toolkit.renderToSVG(p));
  return { pages: pages, verovioLog: toolkit.getLog() };
}

// quiet: re-render without replacing the status message (used after a PDF export).
function showSample(name, quiet) {
  const status = $("render-status");
  return loadMei(name).then(function (mei) {
    const start = performance.now();
    const result = renderPages(screenOptions, mei);
    const ms = performance.now() - start;

    $("score").innerHTML = result.pages.join("");
    if (quiet) return;
    status.className = "result " + (result.verovioLog ? "bad" : "ok");
    status.textContent = name + ": " + result.pages.length + " page(s) in " + ms.toFixed(0) + " ms" +
      (result.verovioLog ? "\nVerovio log:\n" + result.verovioLog : "\nVerovio log: empty");
    log("render", { sample: name, pages: result.pages.length, ms: ms, verovioLog: result.verovioLog });
  }).catch(function (err) {
    status.className = "result bad";
    status.textContent = "Rendering failed: " + err.message;
    log("renderError", { sample: name, message: err.message });
  });
}

function exportPdf() {
  const status = $("render-status");
  const jsPDF = window.jspdf.jsPDF;
  const doc = new jsPDF({ unit: "mm", format: "a4", orientation: "portrait" });
  const start = performance.now();
  let first = true;

  status.className = "result";
  status.textContent = "Building PDF…";

  return samples.reduce(function (chain, name) {
    return chain.then(function () { return loadMei(name); }).then(function (mei) {
      const result = renderPages(pdfOptions, mei);

      return result.pages.reduce(function (pageChain, svgText) {
        return pageChain.then(function () {
          // svg2pdf needs the SVG attached to a live DOM.
          const host = document.createElement("div");
          host.style.position = "absolute";
          host.style.left = "-10000px";
          host.innerHTML = svgText;
          document.body.appendChild(host);

          if (!first) doc.addPage("a4", "portrait");
          first = false;

          return doc.svg(host.querySelector("svg"), { x: 0, y: 0, width: 210, height: 297 })
            .then(function () { host.remove(); });
        });
      }, Promise.resolve());
    });
  }, Promise.resolve()).then(function () {
    const ms = performance.now() - start;
    const dataUri = doc.output("datauristring");
    log("pdfBuilt", { pages: doc.getNumberOfPages(), ms: ms, base64Length: dataUri.length });
    status.className = "result ok";
    status.textContent = "PDF built: " + doc.getNumberOfPages() + " pages in " + ms.toFixed(0) + " ms. Choose where to save it.";
    send("savePdf", { base64: dataUri });
  }).catch(function (err) {
    status.className = "result bad";
    status.textContent = "PDF export failed: " + err.message;
    log("pdfError", { message: err.message });
  }).then(function () {
    return showSample($("sample").value, true);   // restore the on-screen rendering options
  });
}

on("pdfSaved", function (r) {
  const status = $("render-status");
  status.className = "result " + (r.ok ? "ok" : "bad");
  status.textContent = r.error ? r.error : (r.ok ? "Saved " + r.bytes + " bytes to " + r.path : "Could not write " + r.path);
});

$("sample").addEventListener("change", function (e) { showSample(e.target.value); });
$("export-pdf").addEventListener("click", exportPdf);

// A start-up that never finishes should show up as an error, not as "Loading…" forever.
const verovioTimeout = new Promise(function (resolve, reject) {
  setTimeout(function () { reject(new Error("Verovio did not start within 30 s")); }, 30000);
});

Promise.race([window.verovioReady || Promise.reject(new Error("verovioReady is missing from the Verovio script")),
              verovioTimeout]).then(function () {
  toolkit = new verovio.toolkit();
  log("verovioReady", { version: toolkit.getVersion() });
  logDisplay();
  return showSample($("sample").value);
}).catch(function (err) {
  $("render-status").className = "result bad";
  $("render-status").textContent = "Verovio failed to start: " + err.message;
  log("verovioError", { message: err.message });
});

log("pageLoaded", { juceBridge: !!backend });
send("pageReady");
