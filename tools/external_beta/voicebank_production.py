from ._production_candidate import validate_candidate_export, validate_retake_closure
from ._production_common import ProductionResult, canonical_json, sha256_file, sha256_json
from ._production_lock import create_beta_lock, validate_beta_lock
from ._production_session import validate_recording_session
from ._production_workspace import initialize_production_workspace, validate_production_workspace
from ._source_admission import validate_source_strategy_document

__all__ = [
    "ProductionResult",
    "canonical_json",
    "create_beta_lock",
    "initialize_production_workspace",
    "sha256_file",
    "sha256_json",
    "validate_beta_lock",
    "validate_candidate_export",
    "validate_recording_session",
    "validate_retake_closure",
    "validate_production_workspace",
    "validate_source_strategy_document",
]


def main(argv: list[str] | None = None) -> int:
    from ._production_cli import main as run

    return run(argv)


if __name__ == "__main__":
    raise SystemExit(main())
