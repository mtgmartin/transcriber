"use strict";

// ---- The score as sheet music -----------------------------------------------------------
// The plugin sends the score of the shown version as MEI ("mei" event). Verovio engraves it here;
// a click on a note asks the plugin what it is ("nodeInfo" event). Uses $, send, on and log from app.js.

(function () {
  const view = $("notation-view");
  const holder = $("notation-pages");
  const info = $("selection-info");
  const renderInfo = $("render-info");

  const zoomSteps = [50, 60, 70, 85, 100, 120, 140, 170, 200];
  // the notation size of the pages and the PDF: Verovio's scale in percent (a staff is 7.2 mm high at 100)
  const sizes = { small: 70, medium: 85, large: 100 };
  const settings = { view: "scroll", zoom: 100, size: "medium", margin: 15 };

  try {
    const saved = JSON.parse(localStorage.getItem("transcriber.scoreView") || "{}");
    if (saved.view === "pages" || saved.view === "scroll") settings.view = saved.view;
    if (zoomSteps.indexOf(saved.zoom) >= 0) settings.zoom = saved.zoom;
    if (sizes[saved.size]) settings.size = saved.size;
    if ([10, 15, 20].indexOf(saved.margin) >= 0) settings.margin = saved.margin;
  } catch (e) { /* no stored settings: use the defaults */ }

  let toolkit = null;
  let layout = { systemSpacing: 0, staffSpacing: 0 };   // the spacing the user chose (0: the default)
  let mei = "";            // the MEI that is shown
  let meiKey = "";         // which score it is (version and revision)
  let wantedKey = "";      // the key the plugin has announced
  let asked = "";          // the key we asked for
  let selectedId = "";
  let renderTimer = null;
  let lastWidth = 0;

  function saveSettings() {
    try { localStorage.setItem("transcriber.scoreView", JSON.stringify(settings)); } catch (e) { /* not available */ }
  }

  function updateButtons() {
    $("view-pages").classList.toggle("active", settings.view === "pages");
    $("view-scroll").classList.toggle("active", settings.view === "scroll");
    $("zoom-label").textContent = settings.zoom + "%";
    $("zoom-out").disabled = settings.zoom <= zoomSteps[0];
    $("zoom-in").disabled = settings.zoom >= zoomSteps[zoomSteps.length - 1];
  }

  // The options Verovio engraves with. "pdf" and "pages" are the same A4 page (the Pages view is the PDF, shown smaller or
  // larger by the browser); "scroll" is one long page as wide as the window. A page of `pageWidth` units is
  // pageWidth * scale / 100 pixels wide; one unit is a tenth of a millimetre at a scale of 100.
  function optionsFor(kind) {
    // "smart" with a threshold of 0: the line and page breaks of the score are kept, and a line that is too long for the page
    // is broken again (without breaks in the score it works like "auto"; "encoded" squeezed long lines together)
    // the title and the composer are in the MEI header; without them the page has no header (and no space for one)
    const titled = /<title>[^<]+<\/title>|<composer>/.test(mei);
    const common = { footer: "none", header: titled ? "auto" : "none", font: "Bravura", breaks: "smart", breaksSmartSb: 0,
                     spacingSystem: layout.systemSpacing > 0 ? layout.systemSpacing : 14,
                     spacingStaff: layout.staffSpacing > 0 ? layout.staffSpacing : 10, justifyVertically: false, adjustPageHeight: false };

    if (kind === "scroll") {
      const scale = Math.max(10, Math.round(settings.zoom * 0.55));
      const width = Math.max(200, view.clientWidth - 28 - 17);   // room for the scroll bar
      const pageWidth = Math.floor(width * 100 / scale);
      return Object.assign(common, { scale: scale, pageWidth: pageWidth, pageHeight: Math.max(900, Math.floor(pageWidth * 1.4)) });
    }

    const scale = sizes[settings.size];
    const perMm = 1000 / scale;
    const margin = Math.round(settings.margin * perMm);
    return Object.assign(common, { scale: scale, pageWidth: Math.round(210 * perMm), pageHeight: Math.round(297 * perMm),
                                   pageMarginLeft: margin, pageMarginRight: margin, pageMarginTop: margin, pageMarginBottom: margin,
                                   mmOutput: kind === "pdf", svgViewBox: kind === "pages" });
  }

  function options() { return optionsFor(settings.view === "pages" ? "pages" : "scroll"); }

  // The text of the notation is set in the embedded font (DejaVu Serif) on the page and in the PDF: the same letters, with
  // the accents of every language the user types.
  function engraved(svg) { return svg.replace(/font-family="Times"/g, 'font-family="DejaVu Serif"'); }

  let renderToken = 0;
  let infoBase = "";
  let longLines = false;

  function showInfo() {
    renderInfo.textContent = infoBase + (longLines ? " · Some lines are broken again because they do not fit the window: use fewer measures per line, or a smaller zoom" : "");
  }

  let observer = null;

  // Verovio lays out the whole score when it is loaded (the part that takes long); the pages are drawn
  // only when they come near the visible area, so a long score appears quickly.
  function render() {
    if (!toolkit || !mei) return;
    const token = ++renderToken;
    longLines = false;
    const scrollTop = view.scrollTop, scrollLeft = view.scrollLeft;
    const start = performance.now();

    if (observer) observer.disconnect();
    const opts = options();
    toolkit.setOptions(opts);
    toolkit.loadData(mei);
    const pages = toolkit.getPageCount();
    const loaded = performance.now();
    const measures = (mei.match(/<measure /g) || []).length;

    // the measures that start a line or a page by a break of the score
    const breakIds = new Set();
    const breakRe = /(?:<(?:sb|pb)\/>\s*)+<measure[^>]*xml:id="([^"]+)"/g;
    let found;
    while ((found = breakRe.exec(mei))) breakIds.add(found[1]);

    holder.className = settings.view;
    holder.innerHTML = "";

    // placeholders of the size of a page, so the scroll bar is right at once; the A4 pages are as wide as the window
    // (the zoom is a part of that width) and keep their proportions
    const fit = settings.view === "pages";
    const width = fit ? Math.round(Math.max(200, view.clientWidth - 28 - 17) * settings.zoom / 100) : Math.round(opts.pageWidth * opts.scale / 100);
    const height = fit ? Math.round(width * 297 / 210) : Math.round(opts.pageHeight * opts.scale / 100);
    const html = [];
    for (let p = 1; p <= pages; p++)
      html.push('<div class="sheet" data-page="' + p + '"' + ' style="width:' + width + "px;height:" + height + 'px"' + "></div>");
    holder.innerHTML = html.join("");

    let drawn = 0;

    function draw(el) {
      if (token !== renderToken || el.firstChild) return;
      el.innerHTML = engraved(toolkit.renderToSVG(parseInt(el.dataset.page, 10)));
      el.style.height = "";
      if (!fit) el.style.width = "";   // the drawing sets the size (the A4 pages keep the width of the window)
      ++drawn;

      // a line that does not start at a break of the score was broken again, because it did not fit the window: say so
      if (breakIds.size > 0 && !longLines) {
        const systems = el.querySelectorAll("g.system");
        for (let i = 1; i < systems.length; i++) {
          const m = systems[i].querySelector("g.measure");
          if (m && !breakIds.has(m.id)) { longLines = true; showInfo(); break; }
        }
      }
      applySelection(false);
    }

    observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) { if (entry.isIntersecting) draw(entry.target); });
    }, { root: view, rootMargin: "800px 0px" });
    holder.querySelectorAll(".sheet").forEach(function (el) { observer.observe(el); });

    // the first page, without waiting for the observer
    const first = holder.querySelector(".sheet");
    if (first) draw(first);

    view.scrollTop = scrollTop;
    view.scrollLeft = scrollLeft;
    lastWidth = view.clientWidth;

    const end = performance.now();
    infoBase = measures + " measure" + (measures === 1 ? "" : "s") + ", " + pages + " page" + (pages === 1 ? "" : "s") +
               " · laid out in " + Math.round(end - start) + " ms";
    const problem = toolkit.getLog();
    if (problem) infoBase += " · Verovio says: " + problem;
    showInfo();
    log("scoreRender", { measures: measures, pages: pages, loadMs: Math.round(loaded - start), totalMs: Math.round(end - start),
                         view: settings.view, zoom: settings.zoom, meiBytes: mei.length });
  }

  // for the PDF export: what is shown, and how the PDF is engraved (the same page as the Pages view)
  function exportData() {
    return { mei: mei, key: meiKey, options: optionsFor("pdf"), engraved: engraved };
  }

  function scheduleRender(delay) {
    clearTimeout(renderTimer);
    renderTimer = setTimeout(function () {
      try { render(); } catch (err) {
        renderInfo.textContent = "The score could not be engraved: " + err.message;
        log("scoreRenderError", { message: String(err) });
      }
    }, delay === undefined ? 30 : delay);
  }

  function showEmpty(show) {
    $("notation-empty").style.display = show ? "" : "none";
    holder.style.display = show ? "none" : "";
  }

  // ---- getting the MEI ----
  // The page polls nothing: the capture status carries the key of the score, and a new key asks for the MEI.
  function noteKey(key) {
    wantedKey = key || "";

    if (!wantedKey) {
      mei = ""; meiKey = ""; asked = "";
      holder.innerHTML = "";
      showEmpty(true);
      renderInfo.textContent = "";
      return;
    }

    if (wantedKey !== meiKey && wantedKey !== asked) {
      asked = wantedKey;
      send("needMei");
    }
  }

  // While a new recording is made the status has no score key; the score of the shown version stays on screen.
  on("capture", function (s) {
    if (s.scoreKey === undefined && s.versions && s.versions.length) return;
    noteKey(s.scoreKey);
  });

  on("mei", function (m) {
    asked = "";
    if (!m.key) { noteKey(""); return; }
    // another version: start at the top; the same version drawn again (zoom, settings): stay where you are
    if (m.key.split("#")[0] !== meiKey.split("#")[0]) { view.scrollTop = 0; view.scrollLeft = 0; selectedId = ""; info.textContent = "Click a note or a rest to see what it is."; }
    // a selected note that is not in the new score (another instrument, other settings) is let go
    if (selectedId && m.mei.indexOf('xml:id="' + selectedId + '"') < 0) {
      selectedId = "";
      info.textContent = "Click a note or a rest to see what it is.";
      window.dispatchEvent(new Event("transcriber-selection"));
    }
    meiKey = m.key;
    mei = m.mei;
    layout = m.layout || { systemSpacing: 0, staffSpacing: 0 };
    showEmpty(false);
    scheduleRender(0);

    // the score changed again while this one was on its way
    if (wantedKey && wantedKey !== meiKey) { asked = wantedKey; send("needMei"); }
  });

  // ---- clicks ----
  function applySelection(announce) {
    holder.querySelectorAll(".selected").forEach(function (el) { el.classList.remove("selected"); });
    if (!selectedId) return;
    const el = holder.querySelector('[id="' + selectedId.replace(/"/g, '\\"') + '"]');
    if (el) el.classList.add("selected");
    else if (announce) selectedId = "";
  }

  // The note, rest or chord that was clicked. The inside of an open notehead (half and whole notes) is a hole in
  // the drawing and does not take the click, so the boxes of the notes of that page are tried as well.
  function hit(e) {
    const direct = e.target.closest ? e.target.closest("g.note, g.rest, g.mRest, g.chord") : null;
    if (direct) return direct;

    const sheet = e.target.closest ? e.target.closest(".sheet") : null;
    if (!sheet) return null;

    let best = null, bestArea = Infinity;
    const pad = 2;

    sheet.querySelectorAll("g.note, g.rest, g.mRest").forEach(function (el) {
      const r = el.getBoundingClientRect();
      if (e.clientX < r.left - pad || e.clientX > r.right + pad || e.clientY < r.top - pad || e.clientY > r.bottom + pad) return;
      const area = r.width * r.height;
      if (area < bestArea) { best = el; bestArea = area; }
    });

    return best;
  }

  holder.addEventListener("click", function (e) {
    const target = hit(e);
    if (!target) {
      select("");
      return;
    }

    // a note inside a chord selects the chord, and a second click on one of its notes selects just that note;
    // a note of a tablature group of several notes selects the group
    const chord = target.parentElement && target.parentElement.closest ? target.parentElement.closest("g.chord") : null;
    const group = target.closest("g.tabGrp");
    const tabChord = group && group.querySelectorAll("g.note").length > 1 ? group : null;
    const current = selectedId ? holder.querySelector('[id="' + selectedId.replace(/"/g, '\\"') + '"]') : null;
    const together = chord || tabChord;
    const sameChord = together && (selectedId === together.id || (current && together.contains(current)));
    select(sameChord && target.classList.contains("note") ? target.id : (together || target).id);
  });

  // The selection: a click, or the editor (after it made a new note, or to ask again what the selected one is).
  function select(id) {
    selectedId = id || "";
    applySelection(false);

    if (selectedId) { info.textContent = "…"; send("nodeInfo", { id: selectedId }); }
    else info.textContent = "Click a note or a rest to see what it is.";

    window.dispatchEvent(new Event("transcriber-selection"));
  }

  on("nodeInfo", function (r) {
    if (r.id !== selectedId) return;
    info.textContent = r.text || "(not found in the score)";
  });

  // ---- toolbar ----
  $("view-pages").addEventListener("click", function () { settings.view = "pages"; saveSettings(); updateButtons(); scheduleRender(0); });
  $("view-scroll").addEventListener("click", function () { settings.view = "scroll"; saveSettings(); updateButtons(); scheduleRender(0); });

  function zoomBy(step) {
    const i = zoomSteps.indexOf(settings.zoom) + step;
    if (i < 0 || i >= zoomSteps.length) return;
    settings.zoom = zoomSteps[i];
    saveSettings(); updateButtons(); scheduleRender(0);
  }

  $("zoom-out").addEventListener("click", function () { zoomBy(-1); });
  $("zoom-in").addEventListener("click", function () { zoomBy(1); });
  $("zoom-fit").addEventListener("click", function () { settings.zoom = 100; saveSettings(); updateButtons(); scheduleRender(0); });

  // the size of the notation and the margins of the pages and the PDF
  $("pdf-size").value = settings.size;
  $("pdf-margin").value = String(settings.margin);
  $("pdf-size").addEventListener("change", function (e) { settings.size = e.target.value; saveSettings(); if (settings.view === "pages") scheduleRender(0); });
  $("pdf-margin").addEventListener("change", function (e) { settings.margin = parseInt(e.target.value, 10); saveSettings(); if (settings.view === "pages") scheduleRender(0); });

  // the title and the composer of the score, written above it on the page and in the PDF
  function sendMeta() { send("setMeta", { title: $("meta-title").value, composer: $("meta-composer").value }); }
  ["meta-title", "meta-composer"].forEach(function (id) {
    const input = $(id);
    input.addEventListener("change", sendMeta);
    input.addEventListener("keydown", function (e) { if (e.key === "Enter") { input.blur(); } e.stopPropagation(); });   // the editor keys do not act while typing
  });

  on("capture", function (s) {
    const has = s.title !== undefined;
    ["meta-title", "meta-composer"].forEach(function (id) {
      const input = $(id);
      input.disabled = !has;
      if (has && document.activeElement !== input) input.value = id === "meta-title" ? s.title : s.composer;
    });
  });

  // The window was made wider or narrower: the lines are broken again.
  window.addEventListener("resize", function () {
    if (view.clientWidth !== lastWidth) scheduleRender(250);
  });

  updateButtons();
  showEmpty(true);

  // Verovio is started by app.js; its own toolkit for the score is made when it is ready.
  (window.verovioReady || Promise.reject(new Error("verovioReady is missing"))).then(function () {
    toolkit = new verovio.toolkit();
    if (mei) scheduleRender(0);
  }).catch(function (err) {
    renderInfo.textContent = "Verovio failed to start: " + err.message;
  });

  // for tests in a browser
  window.transcriberScore = { exportData: exportData, render: render, settings: settings, selected: function () { return selectedId; }, select: select, setMei: function (m) { mei = m; showEmpty(false); render(); } };
})();
