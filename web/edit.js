"use strict";

// ---- Editing the score: the edit bar and the keyboard ------------------------------------------
// Every button and key sends one "editScore" request for the selected note or rest; the plugin
// does it (as one undo step) and answers with "editResult". Uses $, send and on from app.js and
// window.transcriberScore from score.js.

(function () {
  const bar = $("edit-bar");
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

    bar.querySelectorAll("button[data-op]").forEach(function (b) {
      const op = b.dataset.op;
      b.disabled = op === "undo" ? !(hasScore && edit.canUndo)
                 : op === "redo" ? !(hasScore && edit.canRedo)
                 : !can;
    });

    $("ed-interval").disabled = !can;
    $("ed-undo").title = hasScore && edit.canUndo ? "Undo: " + edit.undoName + " (Ctrl+Z)" : "Nothing to undo";
    $("ed-redo").title = hasScore && edit.canRedo ? "Redo: " + edit.redoName + " (Ctrl+Y)" : "Nothing to redo";

    if (hasScore && edit.blocked) say(edit.blocked, false);
    else if (message.textContent && message.textContent.indexOf("Editing is for") === 0) say("");
  }

  function run(request) {
    if (blocked()) { say(blocked(), true); return; }

    if (request.op !== "undo" && request.op !== "redo") {
      const id = selected();
      if (!id) { say("Click a note or a rest first.", true); return; }
      request.id = id;
    }

    send("editScore", request);
  }

  on("editResult", function (r) {
    say(r.message, !r.ok);
    if (!score()) return;
    if (r.ok && r.select) score().select(r.select);
    else if (selected()) score().select(selected());   // the text of the selected note is asked for again
  });

  on("capture", function (s) { status = s; update(); });
  window.addEventListener("transcriber-selection", update);

  // ---- buttons ----
  bar.addEventListener("click", function (e) {
    const b = e.target.closest ? e.target.closest("button[data-op]") : null;
    if (!b || b.disabled) return;

    const request = { op: b.dataset.op };
    if (b.dataset.semitones) request.semitones = parseInt(b.dataset.semitones, 10);
    if (b.dataset.dur) { request.dur = parseInt(b.dataset.dur, 10); request.dots = 0; }
    if (request.op === "interval") request.interval = parseInt($("ed-interval").value, 10);
    run(request);
    b.blur();
  });

  // ---- keys ----
  const durations = { "2": 32, "3": 16, "4": 8, "5": 4, "6": 2, "7": 1 };

  document.addEventListener("keydown", function (e) {
    const t = e.target;
    if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA" || t.isContentEditable)) return;

    const k = e.key;
    const ctrl = e.ctrlKey || e.metaKey;
    let request = null;

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
