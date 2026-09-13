"""Single owner of the platform identifiers used across SEAM.

Two supported targets are named in three different namespaces, and the Windows
spelling is deliberately different in two of them:

* Host, standalone and install evidence carry a platform name plus a separate
  architecture: ``("macos", "arm64")`` and ``("windows", "x86_64")``.
* Payload, update and neural-deployment descriptors carry one identifier:
  ``macos-arm64`` and ``windows-x64``.
* The full-product contract carries one identifier whose Windows spelling is
  the architecture name: ``macos-arm64`` and ``windows-x86_64``.

``windows-x64`` and ``windows-x86_64`` are not interchangeable strings, so every
translation goes through the table below. Callers must not derive one spelling
from another with string surgery, and a platform that is not in the table is
refused instead of being passed through. Anything the host cannot run, such as
``linux-x64``, is deployment-only and has no full-product contract identity.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Final


class PlatformIdentityError(ValueError):
    """Raised when a platform is unknown or belongs to a different namespace."""


@dataclass(frozen=True, slots=True)
class PlatformIdentity:
    """One supported target under every spelling the product uses."""

    name: str
    architecture: str
    deployment: str
    product_contract: str | None


IDENTITIES: Final = (
    PlatformIdentity("macos", "arm64", "macos-arm64", "macos-arm64"),
    PlatformIdentity("windows", "x86_64", "windows-x64", "windows-x86_64"),
    PlatformIdentity("linux", "x86_64", "linux-x64", None),
)


def _text(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise PlatformIdentityError(f"{label} must be a non-empty string")
    return value


def host_platforms() -> dict[str, str]:
    """Return the host-evidence platform-name to architecture table.

    Only rows with a full-product contract identity are certifiable host targets;
    a deployment-only row such as ``linux-x64`` cannot appear in host evidence.
    """
    return {
        identity.name: identity.architecture
        for identity in IDENTITIES
        if identity.product_contract is not None
    }


def deployment_platforms() -> frozenset[str]:
    """Return every platform a payload or update descriptor may declare."""
    return frozenset(identity.deployment for identity in IDENTITIES)


def product_contract_platforms() -> tuple[str, ...]:
    """Return the full-product contract platforms in declaration order."""
    return tuple(
        identity.product_contract
        for identity in IDENTITIES
        if identity.product_contract is not None
    )


def product_contract_platform(deployment_platform: str) -> str:
    """Translate a payload/update platform into the contract's spelling.

    ``windows-x64`` becomes ``windows-x86_64``. A deployment-only platform such
    as ``linux-x64``, a contract spelling such as ``windows-x86_64``, and an
    unknown value are all refused.
    """

    value = _text(deployment_platform, "deployment platform")
    for identity in IDENTITIES:
        if identity.deployment != value:
            continue
        if identity.product_contract is None:
            raise PlatformIdentityError(
                f"{value} is a deployment-only platform with no contract identity"
            )
        return identity.product_contract
    raise PlatformIdentityError(f"{value} is not a known deployment platform")


def deployment_platform(product_contract: str) -> str:
    """Translate a full-product contract platform into the deployment spelling."""

    value = _text(product_contract, "product contract platform")
    for identity in IDENTITIES:
        if identity.product_contract == value:
            return identity.deployment
    raise PlatformIdentityError(f"{value} is not a known full-product platform")


def deployment_platform_for_host(name: str, architecture: str) -> str:
    """Translate an evidence platform name and architecture into a payload name.

    The pair must match one row exactly. Architectures are never inferred from the
    platform name, so ``windows``/``arm64`` and ``macos``/``x86_64`` are refused.
    """

    platform_name = _text(name, "host platform")
    platform_architecture = _text(architecture, "host architecture")
    for identity in IDENTITIES:
        if identity.name == platform_name and identity.architecture == platform_architecture:
            return identity.deployment
    raise PlatformIdentityError(
        f"{platform_name}/{platform_architecture} is not a known supported host target"
    )


def identity_for_deployment(deployment_platform: str) -> PlatformIdentity:
    """Return the full identity row for one deployment platform name."""

    value = _text(deployment_platform, "deployment platform")
    for identity in IDENTITIES:
        if identity.deployment == value:
            return identity
    raise PlatformIdentityError(f"{value} is not a known deployment platform")


__all__ = [
    "IDENTITIES",
    "PlatformIdentity",
    "PlatformIdentityError",
    "deployment_platform",
    "deployment_platform_for_host",
    "deployment_platforms",
    "host_platforms",
    "identity_for_deployment",
    "product_contract_platform",
    "product_contract_platforms",
]
