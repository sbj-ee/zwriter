# Typewriter key sounds

Optional sample pack for the key-click toggle (**default off** in the app).

| File | Role |
|---|---|
| `key.wav` | Per-character mechanical typewriter click |
| `key-soft.wav` | Softer click variant (optional / unused by default) |
| `return.wav` | End-of-line / Return / carriage-return (soft bell + slide) |

These are **short procedural placeholders** generated in-repo (synthetic noise +
damped tones via a small Python script) — royalty-free, not recorded from a
physical machine. Swap in FocusWriter-style recorded WAVs anytime; keep levels
modest.

Playback uses `QSoundEffect` when Qt6 Multimedia is available at build time
(`ZWRITER_HAS_MULTIMEDIA`). Without Multimedia the Keys toggle remains a
preference stub with no audio.
