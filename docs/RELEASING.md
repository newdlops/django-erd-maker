# Release Django ERD Maker

This guide prepares a reviewable VSIX and then describes publication as a separate
maintainer action. No script in this repository uploads a package or creates a
public release. Current candidate: **0.0.1068, pre-release, darwin-arm64**.

## Decisions before public publication

The prepared legal files use **GPL-3.0-only as a draft project license**. The
project owner must confirm that choice and the right to license contributed code
before publishing. OGDF retains its GPL v2-or-v3 terms and exceptions; this draft
uses v3 for the combined wrapper. Do not replace the notices with an MIT-only
package while keeping the GPL-linked executable.

`newdlops` is taken from the public repository owner and existing development
extension identity. Verify ownership of the matching Marketplace publisher in
the [publisher management page](https://marketplace.visualstudio.com/manage).
The presence of this field does not establish that the publisher is registered
or that a Marketplace version has already been published.

Only **Apple Silicon macOS 26+ and VS Code 1.100+** are covered by this candidate.
Do not advertise Windows, Linux, Intel Mac, older macOS, remote Linux hosts, or
web support based on this package. Build and exercise matching binaries before
adding another `vsce --target` package.

## Prepare the candidate

1. Read the [build guide](BUILDING.md) and install its tools.
2. Set the intended version in both `package.json` and `package-lock.json` and
   update `CHANGELOG.md`. A VSIX version is immutable after Marketplace upload;
   inspect existing versions before selecting the next one.
3. Run the guarded prepare, integration-test, and package commands in the build
   guide. Inspect `dist/verification.json` and `dist/SHA256SUMS`.
4. Review `README.md` and `README.ko.md`, the icon, privacy/support/security
   documents, `LICENSE`, `NOTICE.md`, and generated `THIRD_PARTY_NOTICES.md`.
5. Review the source archive. It must contain the actual distributed TypeScript,
   Rust and C++ source, build scripts, original dependency notices, and the locked
   crate sources. Keep these archives available with the executable release.

`private: true` in package.json prevents accidental npm publication. It does not
prevent VSIX packaging. `--no-dependencies` is intentional: emitted extension
JavaScript imports only VS Code and Node built-ins. `.vscodeignore` is an allowlist
excluding development dependencies, workspace exports, training data, caches,
and research results. The package script first copies explicit release files into
a temporary staging folder so vsce does not scan local research/build trees. It
removes development scripts and devDependencies from the installed manifest,
retaining the uninstall hook. The source archive keeps the original build manifest.
A second allowlist checks the generated ZIP.

Preparation outputs include:

| File in `dist/` | Purpose |
| --- | --- |
| `django-erd-maker-0.0.1068-darwin-arm64.vsix` | Installable pre-release extension |
| `project-source.tar.gz` | Exact project source snapshot and build instructions |
| `rust-dependencies.tar.gz` | Locked Rust dependency source and vendor config |
| `ogdf-foxglove-202510.tar.gz` | Original OGDF source distribution |
| `source-manifest.json` | Tool versions, source/executable hashes, build options |
| `SHA256SUMS` | Artifact integrity checks |
| `verification.json` | Automated package inspection and runtime smoke results |

## Manual acceptance on the candidate VSIX

Install the candidate into a temporary VS Code profile using **Install from
VSIX…**, keeping the normal development profile separate. These manual checks
are required before public publication; automated native smoke tests do not
establish UI acceptance.

- The extension details show the correct name, publisher, icon, readme and license.
- Open a trusted synthetic Django project and run **Django ERD: Open Diagram**.
  Verify fields, two directions of a foreign key, and discovery diagnostics.
- Search, select a model, open Related diagram, follow a peer, use Back and Full
  diagram, and refresh after editing the fixture.
- Inspect a large local project for leaf-card connections, readable selection,
  pagination and resize behavior. Do not upload its screenshots or source unless
  you have permission; public examples must be synthetic or explicitly public.
- Verify the restricted-workspace explanation and missing-project behavior.
- Check **Django ERD: Show Logs** for missing executables or host-only libraries.

Record the tested VS Code/macOS versions and outcomes with the release. The
automated report explicitly distinguishes an extracted-runtime check from an
installation or a manual UI test. See CHANGELOG for known preview limitations.

## Publish the reviewed file

After the license and publisher are confirmed and the manual checks pass, upload
the already reviewed VSIX through Marketplace publisher management, or use an
authorized `vsce publish --packagePath <reviewed-file.vsix>` invocation. Do not
use an inline version argument that silently changes package metadata during
publication. Follow the current [official publishing guide](https://code.visualstudio.com/api/working-with-extensions/publishing-extension)
for authentication; prefer its supported Microsoft Entra workflow for automation.
Never commit tokens or put them in this document.

Commit the reviewed source and release documents, then tag that source revision
and create a GitHub pre-release with the VSIX, corresponding source archives,
source manifest and checksums. Ensure README links resolve at the published
repository revision. Describe this version as a preview and include its platform
restriction and known issues.

After publication, download the uploaded VSIX, compare its checksum to the
reviewed file, and repeat installation in a clean profile. If a regression is
found, publish a higher patch version and direct affected users to **Install
Another Version** or a known-good VSIX. Do not reuse the previous version number.

The packaging requirements, PNG icon rule, platform target and pre-release
metadata follow the [VS Code extension manifest reference](https://code.visualstudio.com/api/references/extension-manifest)
and [publishing guide](https://code.visualstudio.com/api/working-with-extensions/publishing-extension).
