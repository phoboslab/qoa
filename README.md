# QOA - The “Quite OK Audio Format” for fast, lossy audio compression

Single-file MIT licensed library for C/C++

See [qoa.h](https://github.com/phoboslab/qoa/blob/master/qoa.h) for
the documentation and format specification.

More info at: https://phoboslab.org/log/2023/02/qoa-time-domain-audio-compression

Audio samples in WAV & QOA format can be found at: https://phoboslab.org/files/qoa-samples/


⚠️ This implementation has not yet been fuzzed. Don't use it with untrusted input.

⚠️ The format is likely to change in the coming weeks. Once it is finalized, 
a detailed specification will be published.

⚠️ If you are experimenting with QOA, please _very_ be careful, especially when
wearing headphones. You may unexpectedly produce garbage output that can damage
your ears. I had more than a few close calls.