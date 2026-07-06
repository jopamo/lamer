# Sample corpus

This directory is the local corpus workspace for LAME quality, regression, and perf runs. Large media stays out of git; only the tooling and selection policy live here.

## Default corpus order

1. **EBU SQAM FLAC package** — primary quality corpus.  
   Official sound-quality assessment material. Use for testing/evaluation only; not for commercial use.
2. **MIT SQAM WAV selected clips** — compact “killer-ish” WAV set.  
   Selected transient, tonal, solo-instrument, and speech clips. Same testing/evaluation-only restriction.
3. **Xiph.org test media** — real-program lossless FLAC soundtracks.
4. **Mini LibriSpeech** — speech regression set.
5. **Generated synthetic WAVs** — silence, sine, sweep, impulse, clipping, odd-length fixtures.

Optional, not pulled by default:

6. **Full LibriSpeech dev/test subsets** — only when you want a larger speech corpus.

## Layout

- `samples/sources/` — downloaded source assets and extracted archives
- `samples/wav/` — fixed PCM inputs for LAME

## Quick start

Minimal useful corpus:

```sh
python3 samples/manage_corpus.py prepare
python3 samples/manage_corpus.py status
```

Include the optional larger LibriSpeech speech set:

```sh
python3 samples/manage_corpus.py prepare --with-full-librispeech
```

Only fetch downloads, without WAV staging:

```sh
python3 samples/manage_corpus.py fetch
```

Refresh the WAV tree from already-downloaded sources:

```sh
python3 samples/manage_corpus.py prepare-wav
```

Synthetic fixtures only:

```sh
python3 samples/manage_corpus.py generate
```

## Notes

- `prepare` fetches the default corpus in the order above, stages/creates WAV inputs, and leaves the big blobs under `samples/sources/`.
- `prepare-wav` copies existing WAV sources and converts every FLAC under `samples/sources/` into a matching path under `samples/wav/`.
- FLAC conversion uses `ffmpeg` with metadata stripped so the WAV tree is clean fixed PCM input.
- MIT SQAM clip downloads are FTP-backed; the script prefers `wget` for them and reports failures without hiding them.
- The selected MIT clips are:
  - `gspi35_1.wav` — glockenspiel
  - `harp40_1.wav` — harpsichord
  - `trpt21_2.wav` — trumpet
  - `sopr44_1.wav` — soprano
  - `bass47_1.wav` — bass
  - `quar48_1.wav` — quartet
  - `spme50_1.wav` — male speech
  - `spfe49_1.wav` — female speech
