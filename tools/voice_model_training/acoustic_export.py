"""Read a verified acoustic export and predict mel without a vocoder session.

Diagnostic-only: no training, no bundle admission, no qualification.
"""
from .prepare_bundle import ACOUSTIC_FORMAT, read_report


def load_acoustic_graph(directory):
    """Return the verified acoustic graph bytes for a captured export."""
    report, graph = read_report(directory, ACOUSTIC_FORMAT,
                                'acousticPath', 'acousticSha256', 'acousticBytes')
    return graph
