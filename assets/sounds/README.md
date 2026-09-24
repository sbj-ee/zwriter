# Typewriter key sounds

Optional sample pack for the key-click toggle (**default off** in the app).

| File | Role |
|---|---|
| `key-1.wav` … `key-6.wav` | A typed character on an old manual typewriter, six variants picked at random per keystroke |
| `space.wav` | Space bar / Backspace: no type bar fires, so a softer, duller thup plus the escapement ticks |
| `return.wav` | Return / carriage return (rack ratchet, end-stop slam, bell) |

Each key strike is modelled on the measured shape of a well-liked reference
typewriter recording (nothing from it is copied — it was only used to compare
levels and spectra): a faint lever tick, one very bright and sharp type-bar
impact with some steel/wood body under it, a smoothly decaying bright wash, an
escapement "ka-chick" plus softer settling clacks around 90–170 ms, and a faint
low thump at the end. There is deliberately **no sustained narrow-band
ringing** (that is what makes synthetic clicks sound like a tin can) and no
dead digital silence between the parts of the sound.

These are **synthesized in-repo** by `tools/gen_typewriter_sounds.py` (pure
Python, deterministic) — royalty-free, not recorded from a physical machine.
Re-run the script to regenerate, or swap in recorded WAVs with the same names;
keep levels modest.

The samples are embedded in the binary (`src/sounds.qrc`), so sounds work from
any install location; WAVs found on disk under `assets/sounds/` next to the
binary or in the working directory take precedence. Re-run the generator and
rebuild to update the embedded copies.

Playback (`src/TypewriterSounds.cpp`) keeps **one persistent audio stream**
(`QAudioSink`, push mode, on a dedicated audio thread) fed by a small software
mixer: a keypress adds a voice to the running stream, so there is no
per-keystroke stream setup, fast typing never drops clicks, overlapping
strikes mix, and key variants are picked at random with slight level jitter.
It needs Qt6 Multimedia at build time (`ZWRITER_HAS_MULTIMEDIA`); without it
the Key Sounds toggle is disabled.
