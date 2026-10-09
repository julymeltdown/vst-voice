"""The refusal half of the full-product capability matrix, derived from code.

The contract (`docs/product/full-product-beta-contract.json`, `scope.resourceCoverage`) allows
language subsets and backend incompatibilities only inside a frozen matrix that carries a
supported tested combination for every mandatory feature. `scope.matrixStatus` is
`UNRESOLVED` and `scope.releasedResources` is empty.

This module deliberately does not produce that matrix, and the reason is recorded here so the
next reader does not mistake the absence for an oversight. The contract's rule turns on a
POSITIVE claim: a supported, tested combination. Nothing in this repository can support one.
There is no model or vocoder byte anywhere (`find . -name '*.onnx'` is empty), the only voicebank
is an eight-unit public-domain fixture whose own manifest declares `official: false` and whose
README calls it unsuitable for release-quality singing, and the one COMPLETE coverage report
(`assets/pilots/seam-pilot-01/coverage-report.json`) carries a hardcoded
`releaseEligible: false`. Writing a positive cell would mean asserting acoustic qualification
that no artifact here supports, which is the misrepresentation this project has been closing
one unit at a time.

What IS factual, and what this module therefore provides, is the NEGATIVE half: which
combinations the code already refuses, with the refusal named. That half is not invented, it is
read out of the refusal paths, and it is the half a later matrix can be seeded from once real
resources exist. Every entry below cites the file and line that refuses it.

Nothing here asserts that an unlisted combination works. An absent cell means "not refused by
that path", never "supported".
"""
from __future__ import annotations

from typing import Final

#: The three resource kinds the code actually models (`domain::SingerResourceKind`). The
#: contract names six; `dictionary-original` and `character-original` have no representation
#: here at all, and `recipe-original` / `sample-procedural` share one C++ variant.
MODELLED_RESOURCE_KINDS: Final = ("sample", "procedural", "neural")

#: Contract resource kinds with no modelled counterpart. Recorded so the gap is visible rather
#: than silently absent from a generated matrix.
UNMODELLED_CONTRACT_KINDS: Final = ("dictionary-original", "character-original")


class Refusal:
    """One refusal the code already raises, with the capability it is about.

    Two citations, because they prove different things. `refused_at` is the line that
    raises, which proves this carrier refuses. `worded_at` is where the message text is
    defined, which proves the refusal says what this report claims it says. When they are the
    same line the code raises a literal message directly.
    """

    __slots__ = ("capability", "carrier", "refusal", "refused_at", "worded_at")

    def __init__(self, capability: str, carrier: str, refusal: str,
                 refused_at: str, worded_at: str) -> None:
        self.capability = capability
        self.carrier = carrier
        self.refusal = refusal
        self.refused_at = refused_at
        self.worded_at = worded_at

    def __repr__(self) -> str:
        return (f"Refusal(capability={self.capability!r}, carrier={self.carrier!r}, ",
                f"refused_at={self.refused_at!r})")

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Refusal):
            return NotImplemented
        return (self.capability, self.carrier, self.refusal,
                self.refused_at, self.worded_at) == (
            other.capability, other.carrier, other.refusal,
            other.refused_at, other.worded_at)

    def __hash__(self) -> int:
        return hash((self.capability, self.carrier, self.refusal,
                     self.refused_at, self.worded_at))


_REFUSAL_ROWS: Final = (
    # The six expression channels the neural carrier refuses. Each refusal site raises a
    # helper whose text names the capability, so an entry cites both the raise and the
    # definition: the raise proves the carrier refuses, the definition proves what it says.
    ("formant", "neural", "cannot apply the project's formant curve",
     "libs/seam-rendering/src/render_snapshot.cpp:815",
     "libs/seam-rendering/src/render_snapshot.cpp:59"),
    ("breathiness", "neural", "cannot apply the project's breathiness curve",
     "libs/seam-rendering/src/render_snapshot.cpp:819",
     "libs/seam-rendering/src/render_snapshot.cpp:77"),
    ("tension", "neural", "cannot apply the project's tension curve",
     "libs/seam-rendering/src/render_snapshot.cpp:824",
     "libs/seam-rendering/src/render_snapshot.cpp:95"),
    ("airiness", "neural", "cannot apply the project's airiness curve",
     "libs/seam-rendering/src/render_snapshot.cpp:827",
     "libs/seam-rendering/src/render_snapshot.cpp:113"),
    ("gender", "neural", "cannot apply the project's gender curve",
     "libs/seam-rendering/src/render_snapshot.cpp:830",
     "libs/seam-rendering/src/render_snapshot.cpp:131"),
    ("growl", "neural", "cannot apply the project's growl curve",
     "libs/seam-rendering/src/render_snapshot.cpp:833",
     "libs/seam-rendering/src/render_snapshot.cpp:149"),
    # StyleBlend and the backend refusals raise a literal message at the cited line, so the
    # message site and the refusal site are the same place.
    ("style-blend", "neural", "Neural rendering does not admit a StyleBlend PCM pair",
     "libs/seam-rendering/src/render_snapshot.cpp:811",
     "libs/seam-rendering/src/render_snapshot.cpp:811"),
    ("style-blend", "procedural", "Procedural rendering does not admit a StyleBlend PCM pair",
     "libs/seam-rendering/src/render_snapshot.cpp:735",
     "libs/seam-rendering/src/render_snapshot.cpp:735"),
    ("style-blend", "sample", "StyleBlend requires an explicit sample-bank style pair",
     "libs/seam-rendering/src/render_snapshot.cpp:1040",
     "libs/seam-rendering/src/render_snapshot.cpp:1040"),
    ("backend", "any", "Snapshot scheduler has no backend for this singer resource",
     "libs/seam-rendering/src/render_scheduler.cpp:117",
     "libs/seam-rendering/src/render_scheduler.cpp:117"),
    ("formant", "raw-renderer", "Raw cannot apply the required formant control",
     "libs/seam-synthesis/src/raw_renderer.cpp:65",
     "libs/seam-synthesis/src/raw_renderer.cpp:65"),
)

#: Every refusal the code raises, as inspectable values.
REFUSALS: Final = tuple(Refusal(*row) for row in _REFUSAL_ROWS)

#: Capabilities named by a refusal, in stable order.
REFUSED_CAPABILITIES: Final = tuple(sorted({row[0] for row in _REFUSAL_ROWS}))


def refusals_for(carrier: str) -> tuple[Refusal, ...]:
    """The refusals raised for one carrier, or every refusal for "any" and "all"."""
    if carrier in ("any", "all"):
        return REFUSALS
    return tuple(row for row in REFUSALS if row.carrier == carrier)


def capability_report() -> dict[str, object]:
    """A description of what this module does and does not claim.

    The report is deliberately asymmetric. It states the refusals as fact and states the
    positive half as absent, because the contract's rule turns on a positive claim that no
    artifact in this repository can support.
    """
    return {
        "schemaVersion": 1,
        "matrixStatus": "REFUSALS_ONLY",
        "positiveCoverageClaimed": False,
        "why": (
            "scope.resourceCoverage requires a supported tested combination for every mandatory "
            "feature. scope.releasedResources is empty, there is no model or vocoder byte in the "
            "repository, and the only voicebank is a non-official public-domain fixture, so no "
            "positive cell can be asserted without claiming qualification no artifact supports."
        ),
        "modelledResourceKinds": list(MODELLED_RESOURCE_KINDS),
        "unmodelledContractKinds": list(UNMODELLED_CONTRACT_KINDS),
        "refusedCapabilities": list(REFUSED_CAPABILITIES),
        "refusals": [
            {"capability": row.capability, "carrier": row.carrier,
             "refusal": row.refusal, "refusedAt": row.refused_at, "wordedAt": row.worded_at}
            for row in REFUSALS
        ],
        "absentMeans": (
            "A combination absent from this report is not claimed to work. It means only that no "
            "refusal path was found for it, which is not evidence of support."
        ),
    }
