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
  ["capturedNotes", "Captured notes"],
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

  $("state-status").textContent =
    "Test data in state: " + s.stateTestMB + " MB\nLast restore: " + s.stateCheck;
});

$("clear-capture").addEventListener("click", function () { send("clearCapture"); });
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

// ---- Saved state (test 1.7) -----------------------------------------------------
$("state-5").addEventListener("click", function () { send("stateTest", { megabytes: 5 }); });
$("state-0").addEventListener("click", function () { send("stateTest", { megabytes: 0 }); });

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
