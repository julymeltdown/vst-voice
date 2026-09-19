"""Author a training review over an exact captured configuration.

This closes the last mechanical gap between a prepared corpus and dataset admission:
admission verifies a signed review, and nothing in this repository could produce one
for the operator's own material.

It deliberately does not claim independence. A review signed with the same seed that
declares the scopes is a self-authorization, which is a legitimate thing for an owner
to do with their own first-party renders, and is not an independent expert review. The
written record therefore states which of the two it is, and the review record itself
keeps the exact schema admission verifies so that no extra field can smuggle a claim
into the signed bytes.

No key material is generated, stored or transmitted by this module. The caller
supplies a private seed, and its file permissions are refused when they would expose
it to other accounts.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import time

from tools.phase13a.update_contract import ed25519_public_key, ed25519_sign
from tools.public_release.crypto_validation import canonical_signing_payload
from tools.public_release.contracts import sha256_json

from .__main__ import encode_report, load_config, publish_new

ROLES = {
    "rights": ("seam-training-review-1", "training-rights-reviewer",
               "com.project-seam.training-rights-review", "APPROVE_TRAINING_SCOPES"),
    "label": ("seam-training-label-review-1", "training-label-reviewer",
              "com.project-seam.training-label-review", "APPROVE_LABELS"),
}
MAXIMUM_REVIEW_SECONDS = 90 * 24 * 3600


def _seed(path):
    """Read the operator's private seed, refusing a file others can read."""
    path = Path(path)
    if path.is_symlink():
        raise ValueError("A private signing seed cannot be a symlink")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("The signing seed must be a regular file")
        # Group/other read would disclose the key that authorizes the material.
        if info.st_mode & (stat.S_IRGRP | stat.S_IROTH | stat.S_IWGRP | stat.S_IWOTH):
            raise ValueError("Restrict the signing seed to owner-only permissions before use")
        payload = stream.read(65)
        after = os.fstat(stream.fileno())
        if (info.st_size, info.st_mtime_ns, info.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise ValueError("The signing seed changed while it was read")
    if len(payload) != 32:
        raise ValueError("An Ed25519 signing seed is exactly 32 bytes")
    return payload


def author_review(*, kind, configuration, configuration_sha256, seed, output, key_id, signer_id,
                  valid_seconds, independent_reviewer_id=None):
    """Sign one configuration and write the policy, review and authoring record."""
    if kind not in ROLES:
        raise ValueError("Review kind must be rights or label")
    policy_version, role, format_id, decision = ROLES[kind]
    for value, name, limit in ((key_id, "key id", 256), (signer_id, "signer id", 256)):
        if (not isinstance(value, str) or not 1 <= len(value.encode()) <= limit
                or any(ord(c) < 32 or ord(c) == 127 for c in value)):
            raise ValueError(f"Review {name} must be bounded printable text")
    if (not isinstance(configuration_sha256, str) or len(configuration_sha256) != 64
            or any(c not in "0123456789abcdef" for c in configuration_sha256)):
        raise ValueError("Review target must be a lowercase SHA-256")
    if type(valid_seconds) is not int or not 0 < valid_seconds <= MAXIMUM_REVIEW_SECONDS:
        raise ValueError("Review validity must be a bounded positive number of seconds")
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Review output must be new with an existing parent")
    if independent_reviewer_id is not None:
        if (not isinstance(independent_reviewer_id, str) or not 1 <= len(independent_reviewer_id.encode()) <= 256
                or any(ord(c) < 32 or ord(c) == 127 for c in independent_reviewer_id)):
            raise ValueError("Independent reviewer identity must be bounded printable text")
        # Independence is the one thing a signer cannot assert about itself, so
        # naming a separate reviewer is required to record that kind of review.
        if independent_reviewer_id == signer_id:
            raise ValueError("An independent review must name a reviewer other than the signer")
    # Bind the review to the bytes actually present, so a review can never be
    # written against a configuration hash that does not match its file.
    captured = load_config(Path(configuration), configuration_sha256)
    if not isinstance(captured, dict):
        raise ValueError("A reviewed configuration must be a JSON object")
    seed_bytes = _seed(seed)
    policy = dict(policyVersion=policy_version, algorithm="Ed25519", requiredRoles=[role],
                  trustedKeys=[dict(keyId=key_id, role=role, signerId=signer_id,
                                    publicKey=base64.b64encode(ed25519_public_key(seed_bytes)).decode())])
    anchor = sha256_json(policy)
    now = int(time.time())
    review = dict(formatId=format_id, schemaVersion=1, policyVersion=policy_version,
                  policySha256=anchor, algorithm="Ed25519", keyId=key_id, signerId=signer_id,
                  configurationSha256=configuration_sha256, decision=decision,
                  issuedAt=now - 1, expiresAt=now + valid_seconds)
    review["signature"] = base64.b64encode(ed25519_sign(
        canonical_signing_payload(review, "recordSha256"), seed_bytes)).decode()
    review["recordSha256"] = sha256_json(review)
    output.mkdir(mode=0o700)
    publish_new(output / "policy.json", policy)
    publish_new(output / "review.json", review)
    authoring = dict(formatId="com.project-seam.training-review-authoring", schemaVersion=1,
        kind=kind, configurationPath=str(Path(configuration).resolve()),
        configurationSha256=configuration_sha256, policySha256=anchor,
        reviewSha256=review["recordSha256"], issuedAt=review["issuedAt"], expiresAt=review["expiresAt"],
        # The load-bearing distinction. A self-authored review authorizes the
        # owner's own first-party material; it is not an independent assessment of
        # legal scope, annotation quality or voice quality, and must not be reported
        # as one anywhere.
        selfAuthored=independent_reviewer_id is None,
        independentReviewerId=independent_reviewer_id,
        independenceClaimed=independent_reviewer_id is not None,
        signatureValid=True, sourceRightsAdmitted=False, labelsAdmitted=False,
        trainingAdmitted=False, singerQualified=False, releaseEligible=False)
    publish_new(output / "authoring.json", authoring)
    return dict(authoring=authoring, policy=policy, review=review)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=sorted(ROLES), required=True)
    parser.add_argument("--configuration", type=Path, required=True)
    parser.add_argument("--configuration-sha256", required=True)
    parser.add_argument("--seed", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--signer-id", required=True)
    parser.add_argument("--valid-seconds", type=int, default=7 * 24 * 3600)
    parser.add_argument("--independent-reviewer-id")
    args = parser.parse_args(argv)
    try:
        result = author_review(**vars(args))
        print(json.dumps({key: result["authoring"][key] for key in (
            "kind", "policySha256", "reviewSha256", "selfAuthored", "independenceClaimed",
            "trainingAdmitted", "releaseEligible")}, ensure_ascii=False))
        return 0
    except (ValueError, OSError) as error:
        print(f"Review authoring failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
