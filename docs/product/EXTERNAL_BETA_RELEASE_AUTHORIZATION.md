# External Beta release authorization V-1

V-1 is the hard authorization boundary before a candidate ID can be issued.
The authorization identifies one source commit, the rights-cleared bank, trust
policy, documentation, SBOM, signed predecessor, and restored archive. It must
include A3, A4, A5, and A6 approvals, exclude signing credentials from the
freeze environment, and bind two matching unsigned builds for each target
platform.

`tools/external_beta/freeze_candidate.py` consumes this shape and refuses to
issue a candidate ID when any prerequisite is missing, unsigned, version-drifted,
or independently inconsistent. The schemas are
[external-beta-release-authorization.schema.json](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/external-beta-release-authorization.schema.json)
and [external-beta-build-manifest.schema.json](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/external-beta-build-manifest.schema.json).

The checked-in repository contains no GO authorization or signed predecessor;
those remain `NOT_RUN`/`BLOCKED` until real release roles, package bytes, and
archive restoration exist.
