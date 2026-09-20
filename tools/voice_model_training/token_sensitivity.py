"""How much does changing one phone token move the predicted spectrum?

For a fixed phrase, replace a single phone's token with another symbol's token and
measure the mean absolute change over that phone's own frames. A model that uses
the token normally moves the prediction by roughly the between-symbol distance; a
model that has collapsed distinct unvoiced phones will barely move.

Diagnostic only. The comparison is only meaningful between arms measured with the
same phrase, alignment and swap distance, which the caller must supply.
"""
import numpy as np


def swapped_frames(alignment, phone_index):
    """Frame indices owned by a phone, from a one-based mel2ph alignment."""
    alignment = np.asarray(alignment)
    if (alignment.ndim != 1 or not 1 <= alignment.size <= 65536
            or not np.issubdtype(alignment.dtype, np.integer)
            or type(phone_index) is not int or phone_index <= 0):
        raise ValueError('Expected a bounded one-based integer alignment and a phone index')
    return np.flatnonzero(alignment == phone_index)


def sensitivity(original, swapped, alignment, phone_index):
    """Mean absolute prediction change over a phone's frames."""
    original, swapped = np.asarray(original, np.float64), np.asarray(swapped, np.float64)
    if original.shape != swapped.shape or original.ndim != 2:
        raise ValueError('Original and swapped predictions must share two-dimensional geometry')
    if not np.isfinite(original).all() or not np.isfinite(swapped).all():
        raise ValueError('Predictions must be finite')
    frames = swapped_frames(alignment, phone_index)
    if frames.size == 0:
        raise ValueError('Selected phone owns no aligned frames')
    selected_original, selected_swapped = original[frames], swapped[frames]
    return dict(phoneIndex=phone_index, frames=int(frames.size),
        meanAbsoluteChange=float(np.abs(selected_swapped - selected_original).mean()),
        # A token change that moves nothing is the collapse signature; report the
        # change relative to this phone's own prediction spread so the number is
        # not just a function of how loud the phone is.
        changeToSpreadRatio=(float(np.abs(selected_swapped - selected_original).mean()
                                   / selected_original.std()) if selected_original.std() else None))
