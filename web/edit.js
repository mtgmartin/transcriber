"use strict";

// ---- Editing the score: the edit bar and the keyboard ------------------------------------------
// Every button and key sends one "editScore" request for the selected note or rest; the plugin
// does it (as one undo step) and answers with "editResult". Uses $, send and on from app.js and
// window.transcriberScore from score.js.

(function () {
  const bar = $("edit-bar");
  const bar2 = $("edit-bar2");
  const bar3 = $("edit-bar3");
  const bar4 = $("edit-bar4");
  const bar5 = $("edit-bar5");
  const message = $("edit-msg");
  let status = null;
  let timer = null;

  function score() { return window.transcriberScore; }
  function selected() { return score() ? score().selected() : ""; }

  function say(text, bad) {
    message.className = "result" + (bad ? " bad" : "");
    message.textContent = text || "";
    clearTimeout(timer);
    if (text && !bad) timer = setTimeout(function () { message.textContent = ""; }, 6000);
  }

  function blocked() { return status && status.edit ? status.edit.blocked : ""; }

  function update() {
    const edit = status && status.edit;
    const hasScore = !!edit;
    const can = hasScore && !edit.blocked && selected() !== "" && status.state !== "recording";

    // what the instrument has: piano all, guitar and bass the tab string, drums the drum row
    const instrument = (status && status.instrument) || "piano";
    document.querySelectorAll("[data-show]").forEach(function (el) { el.hidden = el.dataset.show.split(" ").indexOf(instrument) < 0; });

    [bar, bar2, bar3, bar4, bar5].forEach(function (row) {
      row.querySelectorAll("button[data-op]").forEach(function (b) {
        const op = b.dataset.op;
        b.disabled = op === "undo" ? !(hasScore && edit.canUndo)
                   : op === "redo" ? !(hasScore && edit.canRedo)
                   : !can;
      });
    });

    $("ed-interval").disabled = !can;
    $("ed-drum").disabled = !(hasScore && !edit.blocked && status.state !== "recording");
    $("ed-drum-voice").disabled = $("ed-drum").disabled;
    $("ed-dyn").disabled = !can;
    $("ed-span").disabled = !can;
    $("ed-perline").disabled = $("ed-spacing").disabled = !(hasScore && !edit.blocked && status.state !== "recording");
    $("ed-key").disabled = !(hasScore && !edit.blocked && status.state !== "recording");

    // the key of the score (it can change by a key change, its undo, or a new take)
    const t = status && status.transcription;
    if (t && document.activeElement !== $("ed-key")) $("ed-key").value = t.keyFifths + (t.keyMinor ? "m" : "M");
    $("ed-undo").title = hasScore && edit.canUndo ? "Undo: " + edit.undoName + " (Ctrl+Z)" : "Nothing to undo";
    $("ed-redo").title = hasScore && edit.canRedo ? "Redo: " + edit.redoName + " (Ctrl+Y)" : "Nothing to redo";

    if (hasScore && edit.blocked) say(edit.blocked, false);
    else if (message.textContent && message.textContent.indexOf("Editing is for") === 0) say("");
  }

  function run(request) {
    if (blocked()) { say(blocked(), true); return; }

    const wholeScore = { undo: 1, redo: 1, perLine: 1, spacing: 1, key: 1 };   // these need no selection
    if (!wholeScore[request.op]) {
      const id = selected();
      if (!id) { say("Click a note or a rest first.", true); return; }
      request.id = id;
      const all = score() ? score().selectedIds() : [];
      if (all.length > 1) request.ids = all;   // several notes: the plugin does it on each, as one undo step
    }

    send("editScore", request);
  }

  on("editResult", function (r) {
    say(r.message, !r.ok);
    if (!score()) return;
    if (r.readOnly) return;   // a selection: score.js sets it
    if (r.ok && r.selection && r.selection.length) score().setSelection(r.selection, r.select);
    else if (r.ok && r.select) score().select(r.select);
    else if (selected()) score().refresh();   // the text of the selected note is asked for again
  });

  on("capture", function (s) { status = s; update(); });
  window.addEventListener("transcriber-selection", update);

  // ---- buttons ----
  function buttonClicked(e) {
    const b = e.target.closest ? e.target.closest("button[data-op]") : null;
    if (!b || b.disabled) return;

    const request = { op: b.dataset.op };
    if (b.dataset.semitones) request.semitones = parseInt(b.dataset.semitones, 10);
    if (b.dataset.dur) { request.dur = parseInt(b.dataset.dur, 10); request.dots = 0; }
    if (b.dataset.voice) request.voice = parseInt(b.dataset.voice, 10);
    if (b.dataset.dir) request.dir = b.dataset.dir;
    if (b.dataset.mode) request.mode = b.dataset.mode;
    if (b.dataset.value) request.value = b.dataset.value;
    if (b.dataset.form) request.form = b.dataset.form;
    if (b.dataset.grace) request.grace = b.dataset.grace;
    if (request.op === "slur" || request.op === "hairpin") request.count = parseInt($("ed-span").value, 10);

    if (request.op === "selectAll") { if (score()) score().selectAll(); b.blur(); return; }
    if (request.op === "selectSame") { if (score()) score().selectSame(b.dataset.mode); b.blur(); return; }

    if (request.op === "text-dialog") { askText(); b.blur(); return; }
    if (request.op === "tempo-dialog") { askTempo(); b.blur(); return; }

    if (request.op === "interval") request.interval = parseInt($("ed-interval").value, 10);

    if (request.op === "drumAdd" || request.op === "drumSet") {
      const entry = selectedDrum();
      if (!entry) { say("Choose a drum in the list first.", true); b.blur(); return; }
      // the voice: the one the user chose in the list, or the one of the drum map
      const chosen = parseInt($("ed-drum-voice").value, 10);
      request.drum = { note: entry.note, name: entry.name, loc: entry.loc, head: entry.head, voice: chosen || entry.voice, force: !!chosen };
    }

    run(request);
    b.blur();
  }

  bar.addEventListener("click", buttonClicked);
  bar2.addEventListener("click", buttonClicked);
  bar3.addEventListener("click", buttonClicked);
  bar4.addEventListener("click", buttonClicked);
  bar5.addEventListener("click", buttonClicked);

  // the drums of the map in use (drums.js tells when they change)
  function drumEntries() { return window.transcriberDrums ? window.transcriberDrums.entries() : []; }
  function selectedDrum() { return drumEntries()[parseInt($("ed-drum").value, 10)] || null; }

  function fillDrums() {
    const select = $("ed-drum");
    const keep = select.value;
    select.textContent = "";
    const first = document.createElement("option");
    first.value = "";
    first.textContent = "choose a drum";
    select.appendChild(first);

    drumEntries().forEach(function (entry, i) {
      const o = document.createElement("option");
      o.value = String(i);
      o.textContent = entry.name + " (" + entry.note + ")";
      select.appendChild(o);
    });

    select.value = keep && select.querySelector('option[value="' + keep + '"]') ? keep : "";
  }

  window.addEventListener("transcriber-drums", fillDrums);

  // the layout lists act at once, on the whole score
  $("ed-perline").addEventListener("change", function () {
    const v = $("ed-perline").value;
    $("ed-perline").value = "";
    if (v !== "") run({ op: "perLine", count: parseInt(v, 10) });
    $("ed-perline").blur();
  });

  $("ed-spacing").addEventListener("change", function () {
    const presets = { tight: [8, 8], normal: [14, 10], roomy: [22, 14] };
    const v = $("ed-spacing").value;
    $("ed-spacing").value = "";
    if (presets[v]) run({ op: "spacing", system: presets[v][0], staff: presets[v][1] });
    $("ed-spacing").blur();
  });

  $("ed-dyn").addEventListener("change", function () {
    const v = $("ed-dyn").value;
    $("ed-dyn").value = "";
    if (v) run({ op: "dynamic", value: v });
    $("ed-dyn").blur();
  });

  // ---- a small dialog for text ----
  const dialog = $("dialog");
  let dialogDone = null;

  function openDialog(title, fields, done) {
    $("dialog-title").textContent = title;
    $("dialog-error").textContent = "";
    const holder = $("dialog-fields");
    holder.textContent = "";

    fields.forEach(function (f) {
      const label = document.createElement("label");
      label.textContent = f.label;
      label.htmlFor = "dlg-" + f.id;
      let input;

      if (f.type === "select") {
        input = document.createElement("select");
        f.options.forEach(function (o) {
          const opt = document.createElement("option");
          opt.value = o[0]; opt.textContent = o[1];
          input.appendChild(opt);
        });
      } else {
        input = document.createElement("input");
        input.type = f.type || "text";
        if (f.max) input.maxLength = f.max;
        if (f.min !== undefined) input.min = f.min;
        if (f.maxValue !== undefined) input.max = f.maxValue;
      }

      input.id = "dlg-" + f.id;
      input.value = f.value !== undefined ? f.value : f.type === "select" ? f.options[0][0] : "";
      holder.appendChild(label);
      holder.appendChild(input);
    });

    dialogDone = done;
    dialog.hidden = false;
    const first = holder.querySelector("input, select");
    if (first) first.focus();
  }

  function closeDialog() { dialog.hidden = true; dialogDone = null; }

  $("dialog-form").addEventListener("submit", function (e) {
    e.preventDefault();
    const values = {};
    $("dialog-fields").querySelectorAll("input, select").forEach(function (el) { values[el.id.slice(4)] = el.value; });
    const error = dialogDone ? dialogDone(values) : "";
    if (error) { $("dialog-error").textContent = error; return; }
    closeDialog();
  });

  $("dialog-cancel").addEventListener("click", closeDialog);
  dialog.addEventListener("keydown", function (e) { if (e.key === "Escape") closeDialog(); });

  function askText() {
    openDialog("Text at the selected note", [
      { id: "text", label: "Text (leave empty to take the text away)", max: 80 },
      { id: "place", label: "Place", type: "select", options: [["above", "above the staff"], ["below", "below the staff"]] }
    ], function (v) {
      run({ op: "text", text: v.text, place: v.place });
      return "";
    });
  }

  function askTempo() {
    openDialog("Tempo mark at the selected note", [
      { id: "bpm", label: "Quarter notes per minute (20 to 400, empty for text only)", type: "number", min: 20, maxValue: 400 },
      { id: "text", label: "Text, e.g. Andante (leave both empty to take the mark away)", max: 40 }
    ], function (v) {
      const bpm = v.bpm === "" ? 0 : parseInt(v.bpm, 10);
      if (isNaN(bpm) || (bpm !== 0 && (bpm < 20 || bpm > 400))) return "The tempo can be 20 to 400.";
      run({ op: "tempo", bpm: bpm, text: v.text });
      return "";
    });
  }

  // ---- the key list: all major and minor keys, by number of sharps or flats ----
  (function fillKeys() {
    const select = $("ed-key");
    for (let f = -7; f <= 7; f++) {
      [false, true].forEach(function (minor) {
        const o = document.createElement("option");
        o.value = f + (minor ? "m" : "M");
        o.textContent = keyName(f, minor) + " (" + (f === 0 ? "no sharps or flats" : Math.abs(f) + (f > 0 ? "#" : "b")) + ")";
        select.appendChild(o);
      });
    }

    select.addEventListener("change", function () {
      const v = select.value;
      send("editScore", { op: "key", fifths: parseInt(v, 10), minor: v.endsWith("m") });
      select.blur();
    });
  })();

  // ---- keys ----
  const durations = { "1": 64, "2": 32, "3": 16, "4": 8, "5": 4, "6": 2, "7": 1 };

  document.addEventListener("keydown", function (e) {
    const t = e.target;
    if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA" || t.isContentEditable)) return;

    const k = e.key;
    const ctrl = e.ctrlKey || e.metaKey;
    let request = null;

    if (ctrl && (k === "a" || k === "A") && score() && status && status.edit) {
      e.preventDefault();
      if (e.altKey) score().selectSame("name");           // Ctrl+Alt+A: the same note in every octave
      else if (e.shiftKey) score().selectSame("pitch");   // Ctrl+Shift+A: the same pitch (the same drum)
      else score().selectAll();                           // Ctrl+A: everything
      return;
    }

    if (ctrl && !e.altKey) {
      if (k === "z" || k === "Z") request = { op: e.shiftKey ? "redo" : "undo" };
      else if (k === "y" || k === "Y") request = { op: "redo" };
      else if (k === "ArrowUp") request = { op: "pitch", semitones: 12 };
      else if (k === "ArrowDown") request = { op: "pitch", semitones: -12 };
    } else if (!e.altKey) {
      if (k === "ArrowUp") request = { op: "pitch", semitones: 1 };
      else if (k === "ArrowDown") request = { op: "pitch", semitones: -1 };
      else if (k === "Delete" || k === "Backspace") request = { op: "delete" };
      else if (/^[a-gA-G]$/.test(k)) request = { op: "letter", letter: k.toUpperCase() };
      else if (durations[k]) request = { op: "duration", dur: durations[k], dots: 0 };
      else if (k === ".") request = { op: "dot" };
      else if (k === "t" || k === "T") request = { op: "tie" };
      else if (k === "j" || k === "J") request = { op: "respell" };
      else if (k === "x" || k === "X") request = { op: "stem", dir: "flip" };
      else if (k === "Escape" && selected() && score()) { score().select(""); e.preventDefault(); return; }
    }

    if (!request) return;
    if (request.op !== "undo" && request.op !== "redo" && !selected()) return;   // keys do nothing without a selection
    if (!status || !status.edit) return;
    e.preventDefault();
    run(request);
  });

  // ---- an edited score ----
  let confirming = false;
  const discard = $("discard-edits");
  discard.addEventListener("click", function () {
    if (!confirming) { confirming = true; discard.textContent = "Really discard?"; return; }
    confirming = false;
    discard.textContent = "Discard my edits";
    send("discardEdits");
  });

  on("capture", function (s) {
    const edited = !!(s.transcription && s.transcription.edited);
    $("edited-note").hidden = !edited;
    if (!edited && confirming) { confirming = false; discard.textContent = "Discard my edits"; }
  });

  update();
})();
