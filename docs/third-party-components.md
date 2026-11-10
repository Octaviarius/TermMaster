# Third-party components and offline notices

The application embeds the component inventory and complete copyright and license
texts from `src/App/Res/ThirdParty`. The same files are copied to `third-party/`
beside the executable during a build. The Help interface reads embedded resources,
so it remains available without the adjacent directory or an internet connection.
The adjacent directory is intended to accompany future distribution packages.

## Inventory

The version 1 `termaster.third-party-components` JSON manifest contains seven
entries: Qt and six directly incorporated dependencies. The Qt entry includes
49 notice links covering its own licenses, 42 bundled library/source attributions
and its logo. Lucide and its Feather-derived icons share an entry that includes
both upstream notices.

| Direct component | Incorporated revision/version | Notice source |
| --- | --- | --- |
| Qt | 6.12.0, MSVC 2022 64-bit kit | Installed runtime SBOM, installed attribution documentation and pinned upstream license files |
| {fmt} | 12.2.1, `208219daff58f2a7438f6931cc2ddc358acf33ef` | `components/fmt/LICENSE` |
| PCRE2 | 10.49, `6f9d7c1373262c541324a16a358785b33ef116cf` | `components/pcre2/LICENCE.md` |
| SLJIT | `de0259c7aaf36aa40cba8014f3fad3edde9307f9` | `components/pcre2/deps/sljit/LICENSE` |
| Valijson | `af2383509277f8e8051acbb3e4b3060f4779aa10` | `components/valijson/LICENSE` |
| yaml-cpp | 0.8.0 plus 31 commits, `1d8ca1f35eb3a9c9142462b28282a848e5d29a91` | `components/yaml-cpp/LICENSE` |
| Lucide / Feather icons | `53fab88ff9d1b48f4704b5b5b560d6b4d560bf4c` | `components/lucide/LICENSE` and `SOURCE.md` |

The Qt inventory follows the installed Qt Base, Qt Serial Port and Qt SVG SBOM
dependency graph from Core, Gui, Widgets, Network, SerialPort, Svg and ZlibPrivate,
plus the Windows platform/style, SVG image/icon and native TLS runtime plugins.
It also includes OpenGL where the selected Qt graph requires it. Runtime package
names, selected roots, bundled component metadata, source hashes and notice hashes
are recorded in `provenance.json`.

Qt build-system metadata and tools are excluded. The inventory does not enumerate
unused submodule test frameworks, Qt Quick/Declarative, SQL drivers, optional
image-format modules or other installed Qt modules. In particular, the application's
PCRE2 10.49 has its own entry; Qt's bundled PCRE2 10.48 is acknowledged within the
Qt entry. Qt's bundled zlib 1.3.2 notice also covers the selected compression
dependency.

The original Qt logo comes from the selected installed documentation at
`global/template/images/Qt-logo.png`. Its documentation license and copyright and
trademark attribution accompany the image. No product logos are present in the
other incorporated dependency source trees. The interface uses its generic
component icon when an original product icon is unavailable.

## Snapshot maintenance

`cmake/ThirdParty.cmake` directly embeds and copies the checked-in bundle. Builds
do not invoke Python, inspect dependency revisions or compare kit hashes. The
notices are a maintained distribution snapshot; refresh them when incorporated
dependencies, Qt kits or deployed plugins change.

The current snapshot describes Qt 6.12.0 `msvc2022_64` with Qt bundled zlib.
`tools/licenses/CollectThirdParty.py` is an explicit maintenance tool using local
dependency sources, installed Qt SBOM files and installed Qt documentation. It
records source revisions and hashes as provenance. To refresh the snapshot:

```powershell
python tools/licenses/CollectThirdParty.py `
    --qt-prefix C:/Qt/6.12.0/msvc2022_64 `
    --qt-version 6.12.0 `
    --qt-docs C:/Qt/Docs/Qt-6.12.0 `
    --qtbase-revision 025bdad1
```

The collector preserves copyright and license text from preformatted
documentation blocks, decodes HTML entities and removes presentation markup. It
does not summarize or rewrite license terms. Direct dependency license files are
copied byte for byte. Prepare the matching Qt GPL 2, GPL 3, LGPL 3 and XSVG files
in the source bundle before refreshing and pass their Qt Base revision. The
collector performs no downloads. The initial copies came from Qt Base revision
`025bdad1`, identified by the selected kit SBOM. Content hashes and upstream paths
are recorded in provenance. Those four initial downloads used physical Ethernet.
The remaining inputs came from local source trees and installed Qt documentation.

When dependencies change, update the direct component versions and relevant
runtime roots in the maintenance script, prepare matching upstream license files,
refresh the snapshot and retain all required notices in the distribution. The
current roots describe the Windows desktop deployment; another deployment may
require a different set of modules, plugins and bundled attributions.

## Distribution scope

The Qt license expression records alternatives reported by the kit and does not
select a commercial or open-source license for a distributor. The texts include
the upstream commercial licensing notice and complete GPL 2, GPL 3 and LGPL 3
licenses. Bundled components retain their own attribution and license expressions.

The notices page and adjacent files are acknowledgements and license materials.
They do not provide corresponding source, relinking materials, an offer for source,
or other distribution arrangements required by an applicable license. Those must
be addressed by the eventual packaging and distribution process. There is no
installer or general deployment workflow in this change. A future package that
adds Qt plugins or other dependencies must update the runtime roots and notices;
any Microsoft runtime redistributable must be handled by that package's actual
redistribution terms and materials.

The source inventory and notice bundle were prepared mechanically from the
sources above. No automated tests or visual acceptance runs were performed for
this change; the requested execution scope is compilation only.

The separate About Qt menu action is omitted; Qt information and full notices
are available through Third-party components.
