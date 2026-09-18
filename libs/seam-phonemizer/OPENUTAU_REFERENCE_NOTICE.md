# OpenUtau pronunciation reference cases

SEAM's pronunciation regression tests use selected lyric and explicit-hint examples from
[OpenUtau](https://github.com/openutau/OpenUtau) at commit
`83e02c7e4a4d9ea5fca72806b2aa27c5382be015`:

- `OpenUtau.Test/Plugins/EnArpaTest.cs`: `HintTest` examples with `r iy d`.
- `OpenUtau.Test/Plugins/EnArpaPlusTest.cs`: `SyllableCCVTest` example `more`, `m ao r`.
- `OpenUtau.Test/Plugins/JaVcvTest.cs`, `JaCvvcTest.cs`, and `JaPresampTest.cs`: selected kana and composed/decomposed voiced-kana inputs.
- `OpenUtau.Core/KoreanPhonemizerUtil.cs`: the `Variate` source-comment examples `많다`, `끓다`, `축하`, and `칼날`.

The expected phones, roles, and source-identity assertions are written for SEAM's inventory.
OpenUtau oto aliases, suffixes, colors, and bank-dependent fallback behavior are not interpreted
as SEAM phonemes. The tests do not claim full OpenUtau conformance, native-speaker acceptance,
or rendered singing quality. No dictionary, voicebank, or audio was copied.

The upstream repository's license is reproduced below for the referenced test inputs.

## OpenUtau license

The MIT License (MIT)

Copyright (c) 2014 StAkira

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
