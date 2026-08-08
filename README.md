# QSA - Quantised Sliced Audio

QSA is a tiny, lossy audio codec for machines that cannot afford much of a
codec at all. It is mono, any sample rate, and spends a fixed 2 bits per
residual plus a 4-bit quantiser scale per 64-sample slice — about 2.1 bits
per sample, or 19.6 kbit/s at the default 9360 hz. The decoder needs no
division, no floating point and no per-sample branching, which is what it was
built for: it fits 90 minutes of film audio in 13 MB and plays it on a Game
Boy Advance next to a software video decoder.

Single-file MIT licensed library for C/C++. See [qsa.h](qsa.h) for the
documentation and format specification.

This is a fork of [phoboslab/qoa](https://github.com/phoboslab/qoa), the
"Quite OK Audio Format", and QSA is a direct descendant of it: the 4-tap
sign-sign LMS predictor, the scale table and the slice idea are all QOA's.

⚠️ This implementation has not yet been fuzzed. Don't use it with untrusted input.


## How QSA relates to QOA

QOA already quantises in slices: 20 samples share a 4-bit scalefactor, with
3-bit residuals, for ~3.2 bits per sample at any sample rate. QSA is what
happens when you need to halve that. An earlier revision of this fork (QAV1)
tried variable bitrate — one scale per 2048-sample chunk, with 1, 2 or 3-bit
residuals chosen per chunk. It worked, but a single step size is wrong for
most of a 160 ms chunk, and the bit-depth switching audibly moved the noise
floor about twice a second.

QSA instead returns to QOA's per-slice scaling and pins the residual at 2
bits. Measured on film dialogue, dropping the scale period from 2048 to 64
samples was worth about +3.6 dB segmental SNR for 3% more bytes, where a whole
extra residual bit costs 50% more bytes for +4.6 dB.

On top of QOA's design, QSA adds:

- **A leak on the LMS weights** — every fourth sample each weight loses
  `weight >> 7`. At 2 bits the residuals are too coarse to keep the filter
  honest without it.
- **A wrapping predictor by specification** — the LMS dot product is defined
  as wrapping 32-bit arithmetic, so a fixed-point ARM decoder (`mul`/`mla`)
  and this reference implementation are bit-identical on any input.
- **Noise-shaped encoding** — the encoder picks codes and scales by minimising
  `e² + λ·(e[n]−e[n−1])²` rather than plain MSE. Penalising the error slope
  tilts the quantisation noise down to follow the signal spectrum, where the
  ear masks it far better than broadband hiss. This is encoder-only; the
  bitstream and decoder know nothing of it.
- **A chunk index** — chunks (default 2048 samples) are indexed by byte
  offset, so a player or a ROM streamer can locate any chunk in O(1). Note
  that the LMS state is not stored in the file, so seeking still requires
  decoding from the start; decoding is far faster than real time.


## Tools

`qsaconv` converts WAV (and optionally MP3/FLAC) to QSA and back. Sources
must be mono 16-bit; the source's sample rate is carried in the QSA header.
Resample with e.g. `ffmpeg -i in.flac -ac 1 -ar 9360 -c:a pcm_s16le in.wav`
first.

```bash
./qsaconv in.wav out.qsa
./qsaconv out.qsa decoded.wav
```

`--chunk-samples` (default 2048) sets the chunk length, `--slice-samples`
(default 64) the scale period, and `--shape` (default 0.5) the noise shaping
strength λ; `--shape 0` gives plain MSE.

`qsaplay` plays QSA files:

```bash
./qsaplay file.qsa
```


## Compiling

Call `make` to build `qsaconv` and `qsaplay`. By default `qsaconv` is compiled
without MP3 and FLAC support.

To compile `qsaconv` with MP3 and FLAC support, download the
[dr_*.h files](https://github.com/mackron/dr_libs) and pass `HAS_DRLIBS=true`
to make:

```bash
curl https://raw.githubusercontent.com/mackron/dr_libs/refs/heads/master/dr_mp3.h -o dr_mp3.h
curl https://raw.githubusercontent.com/mackron/dr_libs/refs/heads/master/dr_flac.h -o dr_flac.h
make HAS_DRLIBS=true
```

`qsaplay` requires
[sokol_audio.h](https://github.com/floooh/sokol/blob/master/sokol_audio.h).

Call `make test` to build and run `qsatest`, which round trips a fixed signal
against golden hashes taken from the reference encoder.
