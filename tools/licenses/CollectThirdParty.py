"""Refresh the checked-in third-party notices from local dependency sources.

This maintenance tool reads an installed Qt kit's SBOM and documentation.
It is not invoked by configuration or builds and never retrieves network files.
"""

import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
BUNDLE = ROOT / "src/App/Res/ThirdParty"
QT_SBOMS = ("qtbase", "qtserialport", "qtsvg")
QT_ROOTS = {
    "Core", "Gui", "Widgets", "Network", "SerialPort", "Svg", "ZlibPrivate",
    "QWindowsIntegrationPlugin", "QModernWindowsStylePlugin", "QSvgPlugin",
    "QSvgIconPlugin", "QSchannelBackendPlugin", "QTlsBackendCertOnlyPlugin",
}
# These are build-system metadata rather than runtime libraries.
BUILD_ONLY = {
    "Platform", "PlatformModuleInternal", "PlatformPluginInternal",
    "GlobalConfigPrivate", "GlobalConfig", "WrapAtomic",
}
LOCAL = (
    ("fmt", "{fmt}", "components/fmt", "12.2.1", "LICENSE", "MIT",
     "Victor Zverovich and {fmt} contributors", "https://fmt.dev/"),
    ("pcre2", "PCRE2", "components/pcre2", "10.49", "LICENCE.md",
     "BSD-3-Clause WITH PCRE2-exception", "Philip Hazel, University of Cambridge and contributors",
     "https://www.pcre.org/"),
    ("sljit", "SLJIT", "components/pcre2/deps/sljit", "", "LICENSE", "BSD-2-Clause",
     "Zoltan Herczeg and contributors", "https://github.com/zherczeg/sljit"),
    ("valijson", "Valijson", "components/valijson", "", "LICENSE", "BSD-2-Clause",
     "Tristan Penman, Akamai Technologies and contributors", "https://github.com/tristanpenman/valijson"),
    ("yaml-cpp", "yaml-cpp", "components/yaml-cpp", "0.8.0 + 31 commits", "LICENSE", "MIT",
     "Jesse Beder and contributors", "https://github.com/jbeder/yaml-cpp"),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def revision(path):
    return subprocess.check_output(
        ["git", "-c", "safe.directory=*", "-C", str(ROOT / path), "rev-parse", "HEAD"],
        text=True,
    ).strip()


def plain_pre(page):
    """Decode formatting tags while preserving every upstream preformatted character."""
    blocks = re.findall(r"<pre\b[^>]*>(.*?)</pre>", page, re.S)
    return [html.unescape(re.sub(r"<[^>]+>", "", block)) for block in blocks]


def notice(path, title="Copyright and license"):
    return {"title": title, "path": path}


def component(id_, name, version, authors, homepage, license_, notices):
    return dict(id=id_, name=name, version=version, authors=authors,
                homepage=homepage, license=license_, notices=notices)


def refresh(args):
    provenance = {
        "version": 1,
        "qtVersion": args.qt_version,
        "qtKit": args.qt_prefix.name,
        "zlibMode": "qt",
        "sbomFiles": {},
        "localSources": [],
        "noticeSources": {},
        "runtimeRoots": sorted(QT_ROOTS),
        "excludedBuildMetadata": sorted(BUILD_ONLY),
    }
    components = []
    for id_, name, path, version, license_file, license_, authors, homepage in LOCAL:
        source = ROOT / path / license_file
        rev = revision(path)
        relative = f"Licenses/{id_}.txt"
        (BUNDLE / relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, BUNDLE / relative)
        components.append(component(id_, name, version or rev, authors, homepage,
                                    license_, [notice(relative)]))
        provenance["localSources"].append({"path": path, "revision": rev,
                                           "licenseFile": license_file,
                                           "sha256": digest(source)})
    lucide_revision = re.search(r"Pinned revision: `([0-9a-f]+)`", (ROOT / "components/lucide/SOURCE.md").read_text(encoding="utf-8"))[1]
    shutil.copyfile(ROOT / "components/lucide/LICENSE", BUNDLE / "Licenses/lucide.txt")
    components.append(component("lucide", "Lucide / Feather icons", lucide_revision,
                                "Lucide Icons contributors; Cole Bemis (Feather)",
                                "https://lucide.dev/", "ISC AND MIT",
                                [notice("Licenses/lucide.txt", "Lucide and Feather copyright and licenses")]))
    provenance["lucide"] = {"revision": lucide_revision,
                             "licenseSha256": digest(ROOT / "components/lucide/LICENSE")}

    packages = {}
    dependencies = {}
    for name in QT_SBOMS:
        filename = f"{name}-{args.qt_version}.cdx.json"
        source = args.qt_prefix / "sbom" / filename
        cdx = json.loads(source.read_text(encoding="utf-8"))
        for package in cdx["components"]:
            packages.setdefault(package["bom-ref"], package)
        for package in cdx["dependencies"]:
            if package.get("dependsOn"):
                dependencies.setdefault(package["ref"], package["dependsOn"])
        provenance["sbomFiles"][filename] = digest(source)
    spdx_packages = {}
    for name in QT_SBOMS:
        filename = f"{name}-{args.qt_version}.spdx.json"
        source = args.qt_prefix / "sbom" / filename
        spdx = json.loads(source.read_text(encoding="utf-8"))
        for package in spdx["packages"]:
            spdx_packages.setdefault(package["SPDXID"], package)
        provenance["sbomFiles"][filename] = digest(source)

    pending = [ref for ref, p in packages.items() if p["name"] in QT_ROOTS]
    seen = set()
    while pending:
        ref = pending.pop()
        if ref in seen:
            continue
        if ref not in packages:
            raise ValueError(f"Unresolved runtime SBOM dependency: {ref}")
        if packages[ref]["name"] in BUILD_ONLY:
            continue
        seen.add(ref)
        pending.extend(dependencies.get(ref, []))

    qt_packages = []
    qt_bundled_components = []
    qt_bundled_notices = []
    documents = set()
    bundled_docs = {"BundledFreetype": "freetype", "BundledHarfbuzz": "harfbuzz-ng",
                    "BundledLibpng": "libpng", "BundledPcre2": "pcre2",
                    "BundledZLIB": "zlib", "ZlibPrivate": "zlib"}
    for ref in sorted(seen):
        p = spdx_packages.get(ref)
        if p is None:
            raise ValueError(f"SPDX metadata missing: {ref}")
        name = p["name"]
        match = re.search(r"\n    Id: (.+)\n", p.get("comment", ""))
        if not match and name not in bundled_docs:
            qt_packages.append(p)
            continue

        id_ = match[1] if match else bundled_docs[name]
        if id_ in documents:
            continue
        documents.add(id_)
        relative = f"Licenses/Qt/{id_}.txt"
        if id_ == "xsvg":
            relative = "Licenses/Qt/XSVG.txt"
            if not (BUNDLE / relative).is_file():
                raise ValueError("Missing pinned upstream Qt XSVG license")
            title = "XSVG"
            homepage = "https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/util"
            source_description = f"https://raw.githubusercontent.com/qt/qtbase/{args.qtbase_revision}/src/gui/util/LICENSE.XSVG.txt"
        else:
            pages = sorted(args.qt_docs.rglob(f"*-attribution-{id_.replace('_', '-')}.html"))
            if len(pages) != 1:
                raise ValueError(f"Expected one installed Qt attribution for {id_}, found {len(pages)}")
            page_path = pages[0]
            page = page_path.read_text(encoding="utf-8")
            blocks = plain_pre(page)
            if len(blocks) < 2:
                raise ValueError(f"Incomplete copyright/license in {page_path}")
            (BUNDLE / relative).write_text("\n\n".join(blocks), encoding="utf-8")
            title = html.unescape(re.sub(r"<[^>]+>", "", re.search(r'<h1 class="title">(.*?)</h1>', page, re.S)[1]))
            title = re.sub(r", version .*", "", title)
            homepage_match = re.search(r'<a href="([^"]+)">Project Homepage</a>', page)
            homepage = html.unescape(homepage_match[1]) if homepage_match else f"https://doc.qt.io/qt-6/{page_path.name}"
            source_description = str(page_path.relative_to(args.qt_docs)).replace("\\", "/")
            provenance["noticeSources"][relative] = {"qtDocumentation": source_description,
                                                       "sourceSha256": digest(page_path)}
        if id_ == "xsvg":
            provenance["noticeSources"][relative] = {"upstream": source_description}
        version = p.get("versionInfo", "Not specified")
        if version == "unknown":
            version = "Not specified"
        qt_bundled_components.append(component(f"qt-{id_}", title, version,
                                               p.get("copyrightText", "See copyright notice"),
                                               homepage, p["licenseConcluded"], [notice(relative)]))
        notice_title = f"{title} — {version} — {p['licenseConcluded']}"
        qt_bundled_notices.append(notice(relative, notice_title))

    qt_copyright = "\n".join(sorted({p.get("copyrightText", "") for p in qt_packages}))
    (BUNDLE / "Licenses/Qt/Copyright.txt").write_text(qt_copyright + "\n", encoding="utf-8")
    commercial = next(lic["license"]["text"]["content"] for p in packages.values()
                      if p["name"] == "Core" for lic in p["licenses"]
                      if lic.get("license", {}).get("name") == "LicenseRef-Qt-Commercial")
    (BUNDLE / "Licenses/Qt/Commercial.txt").write_text(commercial, encoding="utf-8")
    qt_notices = [notice("Licenses/Qt/Copyright.txt", "Qt copyright notices")]
    for license_ in ("GPL-2.0-only", "GPL-3.0-only", "LGPL-3.0-only"):
        relative = f"Licenses/Qt/{license_}.txt"
        if not (BUNDLE / relative).is_file():
            raise ValueError(f"Missing pinned upstream Qt license: {relative}")
        provenance["noticeSources"][relative] = {"upstream": f"https://raw.githubusercontent.com/qt/qtbase/{args.qtbase_revision}/LICENSES/{license_}.txt"}
        qt_notices.append(notice(relative, license_))
    qt_notices.append(notice("Licenses/Qt/Commercial.txt", "Qt commercial licensing notice"))
    qt_notices.extend(qt_bundled_notices)
    icon_source = args.qt_docs / "global/template/images/Qt-logo.png"
    icon_path = "Icons/Qt.png"
    (BUNDLE / "Icons").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(icon_source, BUNDLE / icon_path)
    # Preserve the documentation license and attribution for this original Qt image.
    shutil.copyfile(args.qt_prefix.parents[1] / "Licenses/LICENSE.FDL",
                    BUNDLE / "Licenses/Qt/Documentation-GFDL-1.3.txt")
    (BUNDLE / "Licenses/Qt/Logo.txt").write_text(
        "Qt documentation image: global/template/images/Qt-logo.png\n"
        "Copyright (C) The Qt Company Ltd. and other contributors.\n"
        "The documentation provided herein is licensed under the terms of the\n"
        "GNU Free Documentation License version 1.3 as published by the\n"
        "Free Software Foundation.\n"
        "Qt and respective logos are trademarks of The Qt Company Ltd.\n"
        "in Finland and/or other countries.\n"
        "https://doc.qt.io/qt/trademarks.html\n", encoding="utf-8")
    qt_notices.extend([notice("Licenses/Qt/Logo.txt", "Qt logo attribution"),
                       notice("Licenses/Qt/Documentation-GFDL-1.3.txt", "Qt documentation image license")])
    components.insert(0, component("qt", "Qt", args.qt_version,
                                   "The Qt Company Ltd. and contributors", "https://www.qt.io/",
                                   "GPL-2.0-only OR GPL-3.0-only OR LGPL-3.0-only OR LicenseRef-Qt-Commercial",
                                   qt_notices))
    components[0]["icon"] = icon_path
    provenance["assetSources"] = {icon_path: {
        "qtDocumentation": "global/template/images/Qt-logo.png",
        "sha256": digest(icon_source),
        "copyrightAndTrademarkNotice": "Licenses/Qt/Logo.txt",
        "documentationLicense": "Licenses/Qt/Documentation-GFDL-1.3.txt",
    }}
    provenance["qtRuntimePackages"] = sorted({p["name"] for p in qt_packages})
    provenance["qtBundledComponents"] = qt_bundled_components
    provenance["licenseHashes"] = {str(p.relative_to(BUNDLE)).replace("\\", "/"): digest(p)
                                    for p in sorted((BUNDLE / "Licenses").rglob("*.txt"))}
    manifest = {"version": 1, "type": "termaster.third-party-components",
                "description": "Third-party components and offline copyright and license notices.",
                "content": {"components": components}}
    write_json(BUNDLE / "manifest.json", manifest)
    write_json(BUNDLE / "provenance.json", provenance)
    print(f"Prepared {len(components)} component entries, including "
          f"{len(qt_bundled_notices)} bundled notices under Qt")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--qt-version", required=True)
    parser.add_argument("--qt-docs", type=Path, required=True)
    parser.add_argument("--qtbase-revision", required=True,
                        help="Qt Base revision of the separately prepared GPL/LGPL and XSVG files")
    args = parser.parse_args()
    try:
        refresh(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Third-party notices: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
