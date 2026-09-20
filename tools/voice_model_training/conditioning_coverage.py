"""Audit which optional conditioning channels are actually populated.

A channel that is declared but never non-zero cannot be supervised, and a model
that declares it absent cannot receive it. This distinguishes "the model ignores
the channel" from "the pipeline never provides it". Descriptive only.
"""


def summarize(frames, phones):
    """Count populated values per symbol for the named channels."""
    if not isinstance(frames, list) or not frames or not isinstance(phones, list) or not phones:
        raise ValueError('Expected captured conditioning frames and phone intervals')
    keys = sorted({key for frame in frames for key in frame})
    if 'phoneIndex' not in keys or 'breathiness' not in keys:
        raise ValueError('Conditioning frames must carry phoneIndex and breathiness')
    counts, nonzero, maxima = {}, {}, {}
    for frame in frames:
        index = frame['phoneIndex']
        if type(index) is not int or not 0 <= index < len(phones):
            raise ValueError('Conditioning frame references an unknown phone')
        symbol = phones[index]['symbol']
        for key in keys:
            value = frame[key]
            if not isinstance(value, (int, float)) or value != value:
                raise ValueError('Conditioning values must be finite scalars')
            counts[key] = counts.get(key, 0) + 1
            if value != 0:
                nonzero[key] = nonzero.get(key, 0) + 1
                maxima[key] = max(maxima.get(key, 0.0), float(value))
        counts.setdefault('__bySymbol__', 0)
    return dict(channels={key: dict(frames=counts[key], nonzero=nonzero.get(key, 0),
                                    maximum=maxima.get(key, 0.0)) for key in keys},
        frameCount=len(frames), phoneCount=len(phones))


def channel_coverage(frames, phones, channel):
    """Per-symbol coverage of one channel, so a class-specific gap is visible."""
    if (not isinstance(frames, list) or not frames or not isinstance(phones, list) or not phones
            or not isinstance(channel, str) or not channel):
        raise ValueError('Expected captured frames, phones and a channel name')
    total, nonzero = {}, {}
    for frame in frames:
        index = frame['phoneIndex']
        if type(index) is not int or not 0 <= index < len(phones):
            raise ValueError('Conditioning frame references an unknown phone')
        if channel not in frame:
            raise ValueError('Selected channel is absent from the captured frames')
        symbol = phones[index]['symbol']
        total[symbol] = total.get(symbol, 0) + 1
        if frame[channel] != 0:
            nonzero[symbol] = nonzero.get(symbol, 0) + 1
    return {symbol: dict(frames=total[symbol], nonzero=nonzero.get(symbol, 0),
                         coverage=nonzero.get(symbol, 0) / total[symbol]) for symbol in sorted(total)}
