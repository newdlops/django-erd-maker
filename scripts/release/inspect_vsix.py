"""Inspect our generated VSIX before extracting it; preserve executable modes."""
import json
import re
import stat
import sys
import zipfile
from pathlib import Path, PurePosixPath

archive, destination = map(Path, sys.argv[1:])
root_docs = {
    "package.json", "readme.md", "readme.ko.md", "changelog.md", "license", "license.txt",
    "notice.md", "third_party_notices.md", "privacy.md", "security.md", "support.md",
}


def allowed(name):
    if name in {"[Content_Types].xml", "extension.vsixmanifest"}:
        return True
    if not name.startswith("extension/"):
        return False
    relative = name[len("extension/"):]
    return relative.lower() in root_docs or bool(re.fullmatch(
        r"(?:media/icon\.png|media/ml-preview/(?:layout\.json|manifest\.json|overview\.npz|individual\.npz)|media/source-layout/(?:model\.bin|manifest\.json)|scripts/vscode-uninstall\.mjs|"
        r"out/(?:extension|shared|webview)/.+\.js|"
        r"bin/analyzer/darwin-arm64/django-erd-maker-analyzer|"
        r"bin/ogdf/darwin-arm64/django-erd-ogdf-layout|"
        r"bin/ogdf/licenses/.+|licenses/.+|sources/[^/]+\.tar\.gz|sources/manifest\.json)",
        relative,
    ))


with zipfile.ZipFile(archive) as package:
    names = package.namelist()
    assert len(names) == len(set(names)), "Duplicate ZIP entry"
    for entry in package.infolist():
        parts = PurePosixPath(entry.filename).parts
        assert not entry.filename.startswith("/") and ".." not in parts, entry.filename
        assert "\\" not in entry.filename, entry.filename
        assert not stat.S_ISLNK(entry.external_attr >> 16), "Unexpected symlink"
        if entry.is_dir():
            continue
        assert allowed(entry.filename), f"Unexpected package file: {entry.filename}"
        target = destination.joinpath(*parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        with package.open(entry) as source, target.open("wb") as output:
            while chunk := source.read(1024 * 1024):
                output.write(chunk)
        mode = (entry.external_attr >> 16) & 0o777
        if mode:
            target.chmod(mode)
    manifest = package.read("extension.vsixmanifest").decode("utf-8")
    assert 'TargetPlatform="darwin-arm64"' in manifest, "VSIX target missing"
    assert 'Id="Microsoft.VisualStudio.Code.PreRelease" Value="true"' in manifest, "Preview flag missing"
    print(json.dumps({"files": len(names), "compressedBytes": archive.stat().st_size,
                      "uncompressedBytes": sum(item.file_size for item in package.infolist())}))
