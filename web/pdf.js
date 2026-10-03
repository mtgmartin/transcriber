"use strict";

// ---- Saving the score as a PDF ---------------------------------------------------------------
// The score is engraved again with the options of the A4 page (the same ones the Pages view uses, so the PDF is what the
// page shows), every page goes into the PDF as vector graphics (svg2pdf.js), and the plugin asks where to save the file.
// The text is set in DejaVu Serif, embedded in the PDF. Uses $, send, on and log from app.js, window.transcriberScore from score.js.

(function () {
  const button = $("export-score-pdf");
  const message = $("pdf-msg");
  let status = null;
  let busy = false;
  let fonts = null;   // [{file, style, base64}], read once

  function say(text, bad) {
    message.className = "result" + (bad ? " bad" : "");
    message.textContent = text || "";
  }

  function toBase64(buffer) {
    const bytes = new Uint8Array(buffer);
    let text = "";
    for (let i = 0; i < bytes.length; i += 8192) text += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192));
    return btoa(text);
  }

  function loadFonts() {
    if (fonts) return Promise.resolve(fonts);
    const list = [["DejaVuSerif.ttf", "normal"], ["DejaVuSerif-Bold.ttf", "bold"], ["DejaVuSerif-Italic.ttf", "italic"]];

    return Promise.all(list.map(function (f) {
      return fetch(f[0]).then(function (r) {
        if (!r.ok) throw new Error("The font " + f[0] + " could not be loaded (" + r.status + ")");
        return r.arrayBuffer();
      }).then(function (buffer) { return { file: f[0], style: f[1], base64: toBase64(buffer) }; });
    })).then(function (loaded) { fonts = loaded; return fonts; });
  }

  // The note in a tempo mark ("♩ = 120") is a character of the music font Bravura; the PDF has no music font, so the text is
  // made again as one run with the quarter note of the text font (svg2pdf would place the pieces of a text one after the
  // other by the widths of another font).
  function fixSymbols(root) {
    const ns = "http://www.w3.org/2000/svg";

    root.querySelectorAll("text").forEach(function (text) {
      if (!text.querySelector('tspan[font-family="Bravura"]')) return;

      let size = 0, line = "";

      text.querySelectorAll("tspan").forEach(function (t) {
        if (t.querySelector("tspan")) return;   // only the innermost ones hold text
        const symbol = t.getAttribute("font-family") === "Bravura";

        if (symbol) {
          line += /[]/.test(t.textContent) ? "♩" : "";
        } else {
          line += t.textContent;
          if (!size) size = parseFloat(t.getAttribute("font-size")) || 0;
        }
      });

      const run = document.createElementNS(ns, "tspan");
      run.setAttribute("font-size", (size || 405) + "px");
      run.textContent = line;
      text.textContent = "";
      text.appendChild(run);
    });
  }

  function later() { return new Promise(function (resolve) { setTimeout(resolve, 0); }); }

  function exportPdf() {
    const data = window.transcriberScore && window.transcriberScore.exportData();

    if (busy) return;
    if (!data || !data.mei) { say("There is no score to save yet.", true); return; }
    if (!window.jspdf || !window.svg2pdf) { say("The PDF library is missing.", true); return; }
    if (!window.verovioReady) { say("The engraving library is missing.", true); return; }

    busy = true;
    button.disabled = true;
    say("Making the PDF…");
    const started = performance.now();
    let toolkit = null;

    return loadFonts().then(function (loaded) {
      toolkit = new verovio.toolkit();
      toolkit.setOptions(data.options);
      toolkit.loadData(data.mei);
      const pages = toolkit.getPageCount();

      const doc = new window.jspdf.jsPDF({ unit: "mm", format: "a4", orientation: "portrait", compress: true });
      loaded.forEach(function (f) {
        doc.addFileToVFS(f.file, f.base64);
        doc.addFont(f.file, "DejaVu Serif", f.style);
      });

      let chain = Promise.resolve();

      for (let p = 1; p <= pages; p++) {
        chain = chain.then(function () {
          say("Making the PDF: page " + p + " of " + pages + "…");
          return later();
        }).then(function () {
          // svg2pdf reads colours (currentColor) and fonts from a SVG that is in the document; black on white, not the dark page
          const host = document.createElement("div");
          host.style.cssText = "position:absolute;left:-10000px;top:0;color:#000;background:#fff";
          host.innerHTML = data.engraved(toolkit.renderToSVG(p));
          fixSymbols(host);
          document.body.appendChild(host);

          if (p > 1) doc.addPage("a4", "portrait");
          return doc.svg(host.querySelector("svg"), { x: 0, y: 0, width: 210, height: 297 }).then(function () { host.remove(); }, function (err) { host.remove(); throw err; });
        });
      }

      return chain.then(function () { return { doc: doc, pages: pages }; });
    }).then(function (made) {
      const active = status && status.versions ? status.versions.filter(function (v) { return v.active; })[0] : null;
      const name = (status && status.title) || (active && active.name) || "Transcription";
      const ms = performance.now() - started;
      log("pdfBuilt", { pages: made.pages, ms: Math.round(ms) });
      say("The PDF has " + made.pages + " page" + (made.pages === 1 ? "" : "s") + " (made in " + (ms / 1000).toFixed(1) + " s). Choose where to save it.");
      send("savePdf", { base64: made.doc.output("datauristring"), name: name });
    }).catch(function (err) {
      say("The PDF could not be made: " + err.message, true);
      log("pdfError", { message: String(err) });
    }).then(function () {
      busy = false;
      update();
    });
  }

  on("pdfSaved", function (r) {
    if (r.cancelled) say("Not saved.");
    else if (r.error) say(r.error, true);
    else if (r.ok) say("Saved " + Math.round(r.bytes / 1024) + " KB to " + r.path);
    else say("The file " + r.path + " could not be written.", true);
  });

  function update() {
    button.disabled = busy || !(status && status.title !== undefined && status.scoreKey);
  }

  on("capture", function (s) { status = s; update(); });
  button.addEventListener("click", exportPdf);
  update();
})();
