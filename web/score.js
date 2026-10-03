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
  const settings = { view: "scroll", zoom: 100 };

  try {
    const saved = JSON.parse(localStorage.getItem("transcriber.scoreView") || "{}");
    if (saved.view === "pages" || saved.view === "scroll") settings.view = saved.view;
    if (zoomSteps.indexOf(saved.zoom) >= 0) settings.zoom = saved.zoom;
  } catch (e) { /* no stored settings: use the defaults */ }

  let toolkit = null;
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

  // Verovio's scale is a percentage; a page of `pageWidth` units is pageWidth * scale / 100 pixels wide.
  function options() {
    const scale = Math.max(10, Math.round(settings.zoom * 0.55));
    const width = Math.max(200, view.clientWidth - 28 - 17);   // room for the scroll bar
    const pageWidth = Math.floor(width * 100 / scale);
    const common = { scale: scale, pageWidth: pageWidth, footer: "none", header: "none", font: "Bravura", breaks: "auto",
                     spacingSystem: 14, justifyVertically: false };
    return settings.view === "pages"
      ? Object.assign(common, { pageHeight: Math.floor(pageWidth * 1.4142), adjustPageHeight: false })
      : Object.assign(common, { pageHeight: Math.max(900, Math.floor(pageWidth * 1.4)), adjustPageHeight: false });
  }

  let renderToken = 0;
  let observer = null;

  // Verovio lays out the whole score when it is loaded (the part that takes long); the pages are drawn
  // only when they come near the visible area, so a long score appears quickly.
  function render() {
    if (!toolkit || !mei) return;
    const token = ++renderToken;
    const scrollTop = view.scrollTop, scrollLeft = view.scrollLeft;
    const start = performance.now();

    if (observer) observer.disconnect();
    const opts = options();
    toolkit.setOptions(opts);
    toolkit.loadData(mei);
    const pages = toolkit.getPageCount();
    const loaded = performance.now();
    const measures = (mei.match(/<measure /g) || []).length;

    holder.className = settings.view;
    holder.innerHTML = "";

    // placeholders of the size of a page, so the scroll bar is right at once
    const width = Math.round(opts.pageWidth * opts.scale / 100);
    const height = Math.round(opts.pageHeight * opts.scale / 100);
    const html = [];
    for (let p = 1; p <= pages; p++)
      html.push('<div class="sheet" data-page="' + p + '"' + ' style="width:' + width + "px;height:" + height + 'px"' + "></div>");
    holder.innerHTML = html.join("");

    let drawn = 0;

    function draw(el) {
      if (token !== renderToken || el.firstChild) return;
      el.innerHTML = toolkit.renderToSVG(parseInt(el.dataset.page, 10));
      el.style.width = el.style.height = "";   // the drawing sets the size
      ++drawn;
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
    renderInfo.textContent = measures + " measure" + (measures === 1 ? "" : "s") + ", " + pages + " page" + (pages === 1 ? "" : "s") +
                             " · laid out in " + Math.round(end - start) + " ms";
    const problem = toolkit.getLog();
    if (problem) renderInfo.textContent += " · Verovio says: " + problem;
    log("scoreRender", { measures: measures, pages: pages, loadMs: Math.round(loaded - start), totalMs: Math.round(end - start),
                         view: settings.view, zoom: settings.zoom, meiBytes: mei.length });
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
    meiKey = m.key;
    mei = m.mei;
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

  holder.addEventListener("click", function (e) {
    const target = e.target.closest ? e.target.closest("g.note, g.rest, g.mRest, g.chord") : null;
    if (!target) {
      selectedId = "";
      applySelection(false);
      info.textContent = "Click a note or a rest to see what it is.";
      return;
    }

    // a note inside a chord selects the chord; so does a note of a tablature group of several notes
    const chord = target.parentElement && target.parentElement.closest ? target.parentElement.closest("g.chord") : null;
    const group = target.closest("g.tabGrp");
    const tabChord = group && group.querySelectorAll("g.note").length > 1 ? group : null;
    selectedId = (chord || tabChord || target).id;
    applySelection(true);
    info.textContent = "…";
    send("nodeInfo", { id: selectedId });
  });

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
  window.transcriberScore = { render: render, settings: settings, setMei: function (m) { mei = m; showEmpty(false); render(); } };
})();
