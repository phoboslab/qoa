![QOA Logo](https://qoaformat.org/qoa-logo-new.svg)

# QOA - The “Quite OK Audio Format” for fast, lossy audio compression

Single-file MIT licensed library for C/C++

See [qoa.h](https://github.com/phoboslab/qoa/blob/master/qoa.h) for
the documentation and format specification.

More info at: https://qoaformat.org

Audio samples in WAV & QOA format can be found at: https://qoaformat.org/samples/


⚠️ This implementation has not yet been fuzzed. Don't use it with untrusted input.

⚠️ If you are experimenting with QOA, please be _very_ careful, especially when
wearing headphones. You may unexpectedly produce garbage output that can damage
your ears. I had more than a few close calls.

## Building

Use `cmake -S . -B build` in the source directory containing qoaconv.c, qoaplay.c
This creates an isolated build folder called `build` to prevent clutter in the source tree.
To enable FLAC/MP3 support, add `-DINCLUDE_FLAC` / `-DINCLUDE_MP3` respectively to the
beginning of the aforementioned command. Then, run `cmake --build build` for the binaries.

## Alternative Implementations of QOA

- [pfusik/qoa-fu](https://github.com/pfusik/qoa-fu) - Fusion, transpiling to
[C](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.c),
[C++](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.cpp),
[C#](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.cs),
[D](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.d),
[Java](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOADecoder.java),
[JavaScript](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.js),
[Python](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.py),
[Swift](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.swift)
and [TypeScript](https://github.com/pfusik/qoa-fu/blob/master/transpiled/QOA.ts)
- [qoa-format](https://github.com/mattdesl/qoa-format) - JavaScript encoder/decoder
- [SerenityOS](https://github.com/SerenityOS/serenity) supports QOA system wide through [QOALoader.h](https://github.com/SerenityOS/serenity/blob/master/Userland/Libraries/LibAudio/QOALoader.h)
- [JohannesFriedrich/qoa4R](https://github.com/JohannesFriedrich/qoa4R) - R
- [rafaelcaricio/qoaudio](https://github.com/rafaelcaricio/qoaudio) - Pure Rust zero-dependency decoder implementation
- [AuburnSounds/audio-formats](https://github.com/AuburnSounds/audio-formats) - D library, supports QOA
- [braheezy/goqoa](https://github.com/braheezy/goqoa) - Go library and CLI tool
