# Phase 8d tests in Ableton Live 11: grace notes

New in the Edit bar (the Dynamic / Articulation row): **Grace note** with *With slash*, *No slash* and *Normal note*; and, for drums, **Add as grace note** (a flam) in the Drums row.
A grace note is a small note in front of the note after it. It takes no time in the bar: when a note becomes a grace note, its time becomes a rest (make the note before it longer if
you want that), and when a grace note becomes a normal note again it takes a 16th from the rest in front of its note.

## 8d.1 Piano (`t42-edit-layout`, piano, As played: bar 1 is eight eighth notes C D E F G A B C)
- Click the 2nd note (D) and press **With slash**: D is small with a slash in front of E; where it was there is an eighth rest; the bar is still full. The message says "Grace note."; the sentence under the score says "grace note (with a slash)".
- Press **No slash** on it: the slash goes (an accented grace note). **Pitch ▲** on it: it goes up a half step. **1/4** on it: "A grace note has no length: ...". **Slur** on it, **Tie** on it, **Voice 2** on it: refused with that message.
- Press **Normal note** (or *With slash* again): it is a 16th again and takes a 16th from the rest in front of E. **Delete** on a grace note takes it away. Ctrl+Z undoes each step.
- Make the note after a grace note a rest (Delete): the grace note goes with it. The last note of a bar cannot become a grace note ("A grace note stands in front of a note or chord ... there is none right after this one.").
- Ctrl+click two notes (not next to each other) and press *With slash*: both become grace notes in one step; Ctrl+Z puts both back.
- Ctrl+S, restart Live: the grace notes are still there; Export PDF…: they are in the PDF, small, with the slash.

## 8d.2 Guitar (`t44-edit-guitar`, Guitar)
- Make a note of the standard staff a grace note: it is small in the staff; the tab does **not** show it (Verovio cannot draw a grace note in a tab), and the tab notes of the other notes stay where they were. Click the tab note after it and press Pitch ▲: it works.

## 8d.3 Drums (`t31-drum-groove`, Drums)
- Click a snare hit, choose *Snare* in the Drums list, press **Add as grace note**: a small snare with a slash in front of the hit (a flam), nothing else moves.
- Click a hi-hat hit and press **With slash**: the hi-hat becomes the grace note of the next hit (in the same voice).

## Not built
- Detecting grace notes while transcribing (a very short note right before a longer one): you make them by hand. Grace notes are not played back, and there are no ornament signs (trill, mordent).
