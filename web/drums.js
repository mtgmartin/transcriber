"use strict";

// ---- Drum map: choosing, editing, learning, import and export ----------------------------
// Shown when the instrument is Drums. Uses $, send, on and noteName from app.js.

(function () {
  const panel = $("drum-panel");
  const select = $("drum-map");
  const editor = $("drum-editor");
  const tbody = $("drum-table").querySelector("tbody");
  const message = $("drum-msg");

  const places = [
    [-2, "Ledger line below"], [-1, "Space below (hi-hat pedal)"], [0, "Bottom line"], [1, "1st space (kick)"], [2, "2nd line"],
    [3, "2nd space"], [4, "3rd line"], [5, "3rd space (snare)"], [6, "4th line"], [7, "4th space"], [8, "Top line (ride)"],
    [9, "Space above (hi-hat)"], [10, "Ledger line above (crash)"], [11, "Space above the ledger line"], [12, "2nd ledger line above"]
  ];
  const heads = [["normal", "Normal"], ["x", "x (cymbals)"], ["open-x", "x with o (open hi-hat)"], ["diamond", "Diamond"]];

  let maps = [];            // [{id, name, builtIn}]
  let currentId = "gm";
  let instrument = "piano";
  let revision = -1;        // drumMapRevision last seen
  let editing = null;       // the map being edited: {id, name, entries: [...]}
  let editingId = "";
  let learning = -1;        // the row waiting for a note, or -1
  let learnFrom = 0;        // the note count when learning started
  let lastCount = 0;
  let lastPitch = -1;
  let confirmingDelete = false;

  // Live names MIDI note 60 "C3", the usual convention names it "C4": show both so a pad can be found by the name Live shows.
  function both(n) { return noteName(n) + (n >= 12 ? " · Live: " + noteName(n - 12) : ""); }

  function currentMap() { return maps.find(function (m) { return m.id === currentId; }) || null; }

  function say(text, bad) {
    message.className = "result" + (bad ? " bad" : "");
    message.textContent = text || "";
  }

  function option(value, label) {
    const o = document.createElement("option");
    o.value = value;
    o.textContent = label;
    return o;
  }

  function fillSelect() {
    select.textContent = "";
    maps.forEach(function (m) { select.appendChild(option(m.id, m.name + (m.builtIn ? "" : " (yours)"))); });
    select.value = currentId;
    const cur = currentMap();
    $("drum-delete").hidden = !cur || cur.builtIn;
    $("drum-delete").textContent = confirmingDelete ? "Really delete?" : "Delete";
  }

  // ---- the editor ----
  function entryRow(entry, index) {
    const tr = document.createElement("tr");
    if (index === learning) tr.className = "learning";

    function cell(child) { const td = document.createElement("td"); td.appendChild(child); tr.appendChild(td); return td; }

    const note = document.createElement("input");
    note.type = "number"; note.min = "0"; note.max = "127"; note.value = entry.note;
    note.addEventListener("change", function () {
      const v = parseInt(note.value, 10);
      if (v >= 0 && v <= 127) entry.note = v;
      renderEditor();
    });
    const noteCell = cell(note);
    const label = document.createElement("span");
    label.className = "note-name";
    label.textContent = both(entry.note);
    noteCell.appendChild(label);

    const name = document.createElement("input");
    name.type = "text"; name.maxLength = 60; name.value = entry.name;
    name.addEventListener("input", function () { entry.name = name.value; });
    cell(name);

    const place = document.createElement("select");
    places.forEach(function (p) { place.appendChild(option(String(p[0]), p[1])); });
    place.value = String(entry.loc);
    place.addEventListener("change", function () { entry.loc = parseInt(place.value, 10); });
    cell(place);

    const head = document.createElement("select");
    heads.forEach(function (h) { head.appendChild(option(h[0], h[1])); });
    head.value = entry.head;
    head.addEventListener("change", function () { entry.head = head.value; });
    cell(head);

    const voice = document.createElement("select");
    voice.appendChild(option("1", "Voice 1 (stems up; hands)"));
    voice.appendChild(option("2", "Voice 2 (stems down; feet)"));
    voice.appendChild(option("3", "Voice 3 (stems up)"));
    voice.appendChild(option("4", "Voice 4 (stems down)"));
    voice.value = String(entry.voice);
    voice.addEventListener("change", function () { entry.voice = parseInt(voice.value, 10); });
    cell(voice);

    const ghost = document.createElement("input");
    ghost.type = "number"; ghost.min = "0"; ghost.max = "127"; ghost.value = entry.ghostBelow;
    ghost.title = "Hits softer than this velocity are written as ghost notes in brackets. 0 = never.";
    ghost.addEventListener("change", function () { entry.ghostBelow = Math.max(0, Math.min(127, parseInt(ghost.value, 10) || 0)); });
    cell(ghost);

    const actions = document.createElement("div");
    const learn = document.createElement("button");
    learn.type = "button";
    learn.textContent = index === learning ? "Hit a pad…" : "Learn";
    learn.addEventListener("click", function () {
      learning = learning === index ? -1 : index;
      learnFrom = lastCount;
      renderEditor();
    });
    const remove = document.createElement("button");
    remove.type = "button";
    remove.textContent = "✕";
    remove.title = "Remove this line";
    remove.addEventListener("click", function () {
      editing.entries.splice(index, 1);
      learning = -1;
      renderEditor();
    });
    actions.appendChild(learn);
    actions.appendChild(remove);
    actions.className = "row";
    cell(actions);
    return tr;
  }

  function renderEditor() {
    if (!editing) return;
    $("drum-name").value = editing.name;
    tbody.textContent = "";
    editing.entries.forEach(function (entry, i) { tbody.appendChild(entryRow(entry, i)); });
    const builtIn = (maps.find(function (m) { return m.id === editingId; }) || {}).builtIn;
    $("drum-builtin-note").hidden = !builtIn;
  }

  function openEditor() {
    editing = null;
    editingId = currentId;
    editor.hidden = false;
    say("");
    send("drumMapNeed", { id: currentId });
  }

  function closeEditor() {
    editor.hidden = true;
    editing = null;
    learning = -1;
  }

  // the entries of the map in use, for the Drums row of the edit bar
  let mapEntries = [];
  let entriesFor = "";
  let entriesRevision = -1;
  window.transcriberDrums = { entries: function () { return mapEntries; } };

  on("drumMap", function (r) {
    if (r.id !== currentId) return;

    try {
      mapEntries = JSON.parse(r.json).entries || [];
      window.dispatchEvent(new Event("transcriber-drums"));
    } catch (err) {
      mapEntries = [];
    }
  });

  on("drumMap", function (r) {
    if (r.id !== editingId || editor.hidden) return;

    try {
      const map = JSON.parse(r.json);
      editing = { id: map.id, name: map.name, entries: map.entries.map(function (e) { return Object.assign({}, e); }) };
      renderEditor();
    } catch (err) {
      say("The drum map could not be read.", true);
    }
  });

  $("drum-name").addEventListener("input", function (e) { if (editing) editing.name = e.target.value; });

  $("drum-add").addEventListener("click", function () {
    if (!editing) return;
    const used = {};
    editing.entries.forEach(function (e) { used[e.note] = true; });
    let note = lastPitch >= 0 && !used[lastPitch] ? lastPitch : 36;
    while (used[note] && note < 127) note++;
    editing.entries.push({ note: note, name: "Drum " + note, loc: 5, head: "normal", voice: 1, ghostBelow: 0 });
    renderEditor();
  });

  $("drum-save").addEventListener("click", function () {
    if (!editing) return;
    const seen = {};

    for (const e of editing.entries) {
      if (seen[e.note]) { say("The note " + e.note + " (" + both(e.note) + ") is on two lines. Each note can be written one way only.", true); return; }
      seen[e.note] = true;
    }

    if (!editing.name.trim()) { say("Give the map a name.", true); return; }
    send("drumMapSave", { json: JSON.stringify(editing) });
  });

  $("drum-cancel").addEventListener("click", function () { closeEditor(); say(""); });
  $("drum-edit").addEventListener("click", function () { if (editor.hidden) openEditor(); else closeEditor(); });

  // ---- the map in use ----
  select.addEventListener("change", function () { confirmingDelete = false; send("drumMapSelect", { id: select.value }); });
  $("drum-import").addEventListener("click", function () { say(""); send("drumMapImport"); });
  $("drum-export").addEventListener("click", function () { say(""); send("drumMapExport", { id: currentId }); });
  $("drum-delete").addEventListener("click", function () {
    if (!confirmingDelete) { confirmingDelete = true; fillSelect(); return; }
    confirmingDelete = false;
    send("drumMapDelete", { id: currentId });
  });

  on("drumMapResult", function (r) {
    if (r.error) { say(r.error, true); return; }
    if (r.action === "save") { say("The drum map is saved and used for this take."); closeEditor(); }
    else if (r.action === "import") say("The drum map was imported and is in use.");
    else if (r.action === "export") say("Saved to " + r.path);
  });

  // ---- what the plugin tells us ----
  on("capture", function (s) {
    instrument = s.instrument || "piano";
    panel.hidden = instrument !== "drums";
    if (!s.drumMaps) return;

    const changed = JSON.stringify(s.drumMaps) !== JSON.stringify(maps) || s.drumMapId !== currentId;
    maps = s.drumMaps;
    currentId = s.drumMapId || "gm";
    if (changed && document.activeElement !== select) fillSelect();

    // a map that was saved or imported: the editor shows the new one
    if (s.drumMapRevision !== revision) {
      revision = s.drumMapRevision;
      confirmingDelete = false;
      fillSelect();
      if (!editor.hidden) { editingId = currentId; send("drumMapNeed", { id: currentId }); }
    }

    // the drums of the map in use are asked for when the map or its content changes
    if (instrument === "drums" && (entriesFor !== currentId || entriesRevision !== s.drumMapRevision)) {
      entriesFor = currentId;
      entriesRevision = s.drumMapRevision;
      send("drumMapNeed", { id: currentId });
    }

    // the unmapped notes of the shown take
    const warning = $("drum-warning");
    const t = s.transcription;
    const unmapped = t && instrument === "drums" ? t.warnings.filter(function (w) { return w.indexOf("not in the drum map") >= 0; }) : [];
    warning.hidden = unmapped.length === 0;
    warning.textContent = unmapped.join("\n");
  });

  on("status", function (s) {
    if (!s.lastNote) return;
    lastCount = s.lastNote.count;
    lastPitch = s.lastNote.pitch;
    $("drum-last").textContent = lastPitch >= 0 ? lastPitch + " (" + both(lastPitch) + ")" : "none yet";

    // learning: the first note played after pressing Learn is taken
    if (learning >= 0 && editing && lastCount > learnFrom && lastPitch >= 0 && learning < editing.entries.length) {
      editing.entries[learning].note = lastPitch;
      learning = -1;
      renderEditor();
    }
  });
})();
