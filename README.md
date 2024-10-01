![QOA Logo](https://qoaformat.org/qoa-logo-new.svg)

# QOA - The “Quite OK Audio Format” for fast, lossy audio compression

Single-file MIT licensed library for C/C++

See [qoa.h](https://github.com/phoboslab/qoa/blob/master/qoa.h) for
the documentation and format specification.

More info at: https://qoaformat.org

Audio samples in WAV & QOA format can be found at: https://qoaformat.org/samples/


⚠️ This implementation has not yet been fuzzed. Don't use it with untrusted input.


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
- [JohannesFriedrich/qoa4R](https://github.com/JohannesFriedrich/qoa4R) - R
- [rafaelcaricio/qoaudio](https://github.com/rafaelcaricio/qoaudio) - Pure Rust zero-dependency decoder implementation
- [AuburnSounds/audio-formats](https://github.com/AuburnSounds/audio-formats) - D library, supports QOA
- [braheezy/goqoa](https://github.com/braheezy/goqoa) - Go library and CLI tool
- [HaxelWorks/qoa-python](https://github.com/HaxelWorks/qoa-python) - Python wrapper using cffi
- [Cl Qoa](https://shinmera.github.io/cl-qoa/) - Common Lisp

## QOA Support in Other Software

- [Godot Engine](https://godotengine.org) - supports compressing WAV files into QOA since 4.3
- [raylib](https://github.com/raysan5/raylib) - supports decoding QOA samples through its [raudio module](https://github.com/raysan5/raylib/blob/master/src/raudio.c)
- [SerenityOS](https://github.com/SerenityOS/serenity) supports QOA system wide through [QOALoader.h](https://github.com/SerenityOS/serenity/blob/master/Userland/Libraries/LibAudio/QOALoader.h)
- [Qmmp](https://github.com/TTK-qmmp/qmmp-qoa) - supports decoding QOA samples
- [Visual Studio Code](https://github.com/microsoft/vscode): supports playing QOA files with the [QOA Preview extension](https://github.com/braheezy/vscode-qoa-preview)
