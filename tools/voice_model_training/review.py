"""Verify a supplied training review against an independently trusted policy."""
from tools.public_release.crypto_validation import signed_record_errors
from tools.public_release.contracts import sha256_json


def verify_training_review(review: dict, *, policy: dict, trusted_policy_sha256: str,
                           configuration_sha256: str, now: int) -> dict:
    """No keys or approvals are generated. Caller owns the policy trust anchor.

    The configuration digest must identify the exact source/evidence configuration
    independently re-inspected by admission. This verifies a review, not training
    execution readiness or correctness of a reviewer's legal interpretation.
    """
    if type(now) is not int or now < 0:
        raise ValueError("Review verification needs explicit current Unix time")
    for digest in (trusted_policy_sha256, configuration_sha256):
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError("Invalid trusted review binding")
    fields = {"policyVersion", "algorithm", "requiredRoles", "trustedKeys"}
    if (not isinstance(policy, dict) or set(policy) != fields
            or policy["policyVersion"] != "seam-training-review-1"
            or policy["algorithm"] != "Ed25519" or policy["requiredRoles"] != ["training-rights-reviewer"]
            or not isinstance(policy["trustedKeys"], list) or not 1 <= len(policy["trustedKeys"]) <= 64):
        raise ValueError("Invalid training reviewer policy")
    for key in policy["trustedKeys"]:
        if (not isinstance(key, dict) or set(key) != {"keyId", "role", "signerId", "publicKey"}
                or any(not isinstance(v, str) or not 1 <= len(v.encode()) <= 256 for v in key.values())):
            raise ValueError("Invalid training reviewer key")
    if sha256_json(policy) != trusted_policy_sha256:
        raise ValueError("Reviewer policy differs from independent trust anchor")
    fields = {"formatId", "schemaVersion", "policyVersion", "policySha256", "algorithm", "keyId", "signerId",
              "configurationSha256", "decision", "issuedAt", "expiresAt", "signature", "recordSha256"}
    if (not isinstance(review, dict) or set(review) != fields
            or review["formatId"] != "com.project-seam.training-rights-review"
            or type(review["schemaVersion"]) is not int or review["schemaVersion"] != 1
            or review["policySha256"] != trusted_policy_sha256
            or review["configurationSha256"] != configuration_sha256
            or review["decision"] != "APPROVE_TRAINING_SCOPES"):
        raise ValueError("Review does not approve the captured training configuration")
    if (type(review["issuedAt"]) is not int or type(review["expiresAt"]) is not int
            or not 0 <= review["issuedAt"] <= now < review["expiresAt"]):
        raise ValueError("Training review is future-dated or expired")
    for key in fields - {"schemaVersion", "issuedAt", "expiresAt"}:
        if not isinstance(review[key], str) or not 1 <= len(review[key].encode()) <= 256:
            raise ValueError("Invalid review text")
    errors = signed_record_errors(review, policy, "training-rights-reviewer", "recordSha256", "signerId")
    if errors:
        raise ValueError("Training review signature or signer binding failed")
    return dict(formatId="com.project-seam.verified-training-review", schemaVersion=1,
                configurationSha256=configuration_sha256, policySha256=trusted_policy_sha256,
                reviewSha256=review["recordSha256"], signerId=review["signerId"],
                reviewAuthenticated=True, trainingAdmitted=False, releaseEligible=False)
