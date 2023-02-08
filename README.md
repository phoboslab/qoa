# QOA - The “Quite OK Audio Format” for fast, lossy audio compression

Single-file MIT licensed library for C/C++

See [qoa.h](https://github.com/phoboslab/qoa/blob/master/qoa.h) for
the documentation and format specification.

More info at: https://phoboslab.org/log/2023/02/qoa-time-domain-audio-compression

Audio samples in WAV & QOA format can be found at: https://phoboslab.org/files/qoa-samples/


⚠️ This implementation has not yet been fuzzed. Don't use it with untrusted input.

⚠️ The format is likely to change in the coming weeks. Once it is finalized, 
a detailed specification will be published. Note that files encoded with a current
development versions will likely not work (or produce horrible sounds) when 
decoded in a slightly older or newer version.

⚠️ If you are experimenting with QOA, please be _very_ careful, especially when
wearing headphones. You may unexpectedly produce garbage output that can damage
your ears. I had more than a few close calls.

## Alternative Implementations of QOA

- [pfusik/qoa-ci](https://github.com/pfusik/qoa-ci) - Ć, transpiling to
[C](https://github.com/pfusik/qoa-ci/blob/master/transpiled/QOA.c),
[C++](https://github.com/pfusik/qoa-ci/blob/master/transpiled/QOA.cpp),
[C#](https://github.com/pfusik/qoa-ci/blob/master/transpiled/QOA.cs),
[Java](https://github.com/pfusik/qoa-ci/blob/master/transpiled/QOADecoder.java),
[JavaScript](https://github.com/pfusik/qoa-ci/blob/master/transpiled/QOA.js),
[Python](https://github.com/pfusik/qoa-ci/blob/master/transpiled/QOA.py)
and [Swift](https://github.com/pfusik/qoa-ci/blob/master/transpiled/QOA.swift)
- [qoa-format](https://github.com/mattdesl/qoa-format) - JavaScript encoder/decoder
