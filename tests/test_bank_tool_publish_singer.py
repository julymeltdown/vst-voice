"""Integration coverage for the bank tool's singer publication, driven through the real binary."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def run(executable, *arguments, expect=0):
    result = subprocess.run([executable, *arguments], capture_output=True, text=True, timeout=60)
    assert result.returncode == expect, (arguments, result.returncode, result.stdout, result.stderr)
    return result


def fields(output):
    return dict(line.split("=", 1) for line in output.strip().splitlines() if "=" in line)


def main():
    executable = sys.argv[1]
    recipe_source = Path(__file__).resolve().parents[1] / "assets/pilots/seam-song-01/recipe.json"
    assert recipe_source.is_file(), recipe_source
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        private_key, public_key = root / "private.json", root / "public.json"
        run(executable, "keygen", str(private_key), str(public_key))

        # Publication derives the declared identity from the recipe, so the reported counts must be
        # the recipe's own and no approval or review field may appear in the output.
        package = root / "song-01.seamsinger"
        published = fields(run(executable, "publish-singer", str(recipe_source),
                               str(root / "staging"), str(package), str(private_key),
                               "--version", "1.0.0", "--language", "ja",
                               "--display-name", "Song 01 Original").stdout)
        assert published["singer"] == "song-01-original", published
        assert published["version"] == "1.0.0", published
        assert published["language"] == "ja", published
        assert int(published["phones"]) > 0, published
        assert int(published["styles"]) > 0, published
        assert len(published["digest"]) == 64, published
        assert "approv" not in published and "review" not in published, published
        assert package.is_file(), package
        # The staging directory keeps the exact canonical recipe the manifest digest binds.
        staged = (root / "staging" / "recipe.json").read_bytes()
        manifest = json.loads((root / "staging" / "manifest.json").read_text())
        import hashlib
        assert hashlib.sha256(staged).hexdigest() == manifest["recipeSha256"], manifest
        assert manifest["id"] == "song-01-original", manifest
        assert manifest["phones"], manifest

        # The published package verifies, and installing it is a separate, trusted step.
        verified = fields(run(executable, "verify-singer", str(package),
                              "--public-key", str(public_key)).stdout)
        assert verified["signatureValid"] == "true", verified
        assert verified["signerTrusted"] == "true", verified
        install_root = root / "singers"
        installed = fields(run(executable, "publish-singer", str(recipe_source),
                               str(root / "staging2"), str(root / "installed.seamsinger"),
                               str(private_key), "--version", "1.0.0", "--language", "ja",
                               "--install-root", str(install_root),
                               "--public-key", str(public_key)).stdout)
        assert installed["rendered"] == "song-01-original", installed
        assert len(installed["contentHash"]) == 64, installed
        installed_directory = Path(installed["installed"])
        assert installed_directory.is_dir(), installed_directory
        assert (installed_directory / "manifest.json").is_file()
        assert (installed_directory / "recipe.json").is_file()

        # A style the recipe does not declare is refused rather than written into a manifest as if
        # the singer supported it, and the refusal publishes nothing.
        refused = run(executable, "publish-singer", str(recipe_source), str(root / "bad-staging"),
                      str(root / "bad.seamsinger"), str(private_key),
                      "--version", "1.0.0", "--style", "operatic", expect=5)
        assert "style" in refused.stderr, refused.stderr
        assert not (root / "bad.seamsinger").exists()
        # A version is a required distribution decision, never derived.
        missing_version = run(executable, "publish-singer", str(recipe_source),
                              str(root / "no-version-staging"), str(root / "no-version.seamsinger"),
                              str(private_key), expect=5)
        assert "version" in missing_version.stderr, missing_version.stderr
        # Installing without a trust anchor is refused rather than trusting the package by default.
        untrusted = run(executable, "publish-singer", str(recipe_source), str(root / "untrusted-staging"),
                        str(root / "untrusted.seamsinger"), str(private_key),
                        "--version", "1.0.0", "--install-root", str(root / "untrusted-singers"),
                        expect=6)
        assert "public-key" in untrusted.stderr, untrusted.stderr
        assert not (root / "untrusted-singers").exists()


if __name__ == "__main__":
    main()
