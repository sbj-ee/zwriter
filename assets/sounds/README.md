# Typewriter key sounds

Optional sample pack for the key-click toggle (**default off** in the app).

| File | Role |
|---|---|
| `key-1.wav` … `key-4.wav` | Type-bar strike variants (tick + metallic clack + body thump + bottom-out), rotated per keystroke |
| `return.wav` | Return / carriage return (ratchet slide, end-stop thunk, bell) |

These are **synthesized in-repo** by `tools/gen_typewriter_sounds.py` (pure
Python, deterministic; filtered noise + damped inharmonic partials) —
royalty-free, not recorded from a physical machine. Re-run the script to
regenerate, or swap in recorded WAVs with the same names; keep levels modest.

Playback uses `QSoundEffect` when Qt6 Multimedia is available at build time
(`ZWRITER_HAS_MULTIMEDIA`). Without Multimedia the Keys toggle remains a
preference stub with no audio.
