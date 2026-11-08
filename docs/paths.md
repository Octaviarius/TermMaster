# Paths

Core provides two responsibilities: lexical paths and native filesystem paths.
The former template hierarchy, cross-flavour implicit conversions and public
component storage have been removed. No compatibility aliases are retained.

## Types and placement

- `<Termaster/Core/PurePath.h>` declares PurePath, PurePosixPath and
  PureWindowsPath together; implementation is `src/core/src/PurePath.cpp`.
- PurePath stores an explicit `Syntax::Posix` or `Syntax::Windows`. Its default
  syntax is native. `PurePath::posix()` and `PurePath::windows()` are factories.
  PurePosixPath and PureWindowsPath provide explicit named construction for their
  syntax on every host. Their inherited value operations return PurePath while
  preserving the syntax. They are value construction types, not polymorphic
  ownership interfaces.
- `<Termaster/Core/Path.h>` declares Path, implemented in `src/core/src/Path.cpp`.
  It contains a native-syntax PurePath and delegates filesystem access to Qt.
  Constructing Path from a foreign-syntax PurePath is rejected; explicit named
  pure types can still process foreign paths without I/O.

Representation is private. Values support copying, equality, `parent()`, `join()`
and `/`; `/=` is the explicit mutating convenience. `toString()` is required at
Qt string boundaries. There are no implicit QString or component-list conversions.
ApplicationPaths provides native Path locations; Settings accepts either a Path or an explicit QString.

```cpp
PurePosixPath menu("/application/menu");
auto settings = menu / "settings";
auto relative = settings.relativeTo(menu); // optional PurePath("settings")

PureWindowsPath remote("C:/Users/example");
auto config = remote / "config"; // Windows syntax even on Unix

Path file = Path::appData() / "sessions" / "session.yaml";
QString error;
if (!file.parent().createDirectories(&error))
    reportError(error);
else if (!file.createFile(&error))
    reportError(error);
```

## Lexical rules

Pure operations never inspect the filesystem. They support logical paths with
POSIX separators and filesystem names in either syntax. Empty paths format as
`.` and have no components. Repeated separators, trailing separators and `.`
components are removed; `..` components are preserved. Parent traversal removes
one lexical component and never escapes an anchor. No symbolic link resolution,
home expansion, environment expansion, globbing or URI parsing is performed.

POSIX uses `/`, preserves exactly two leading slashes, and compares components
case-sensitively. Backslashes are ordinary POSIX name characters. Windows accepts
both slash forms, emits backslashes, supports ASCII drive letters, drive-relative
and root-relative paths, standard UNC shares and extended UNC share anchors.
Windows equality and prefix comparison use Qt's case-insensitive QString
comparison, which is a lexical convention rather than a filesystem identity test.
Windows absolute paths require both drive/share and root. `drive()`, `root()`,
`anchor()` and `parts()` retain these distinctions. A relative first component
such as `c:notes` is serialized with a dot prefix to preserve parsing round trips.
Malformed UNC strings are processed lexically; no exhaustive Win32 name/device
validation or byte-for-byte pathlib compatibility is claimed.

An anchored joined segment replaces the prior path. A Windows root-relative
segment retains the existing drive; a same-drive relative segment appends, while
a different drive replaces the previous value. Joining different syntaxes throws
`std::invalid_argument`. Constructors reject embedded NUL and invalid Syntax
values. Failed `/=` leaves the original value unchanged.

`relativeTo()` uses whole-component prefix comparison and returns nullopt for
incompatible syntax, anchors or prefixes. Equal paths yield `.`. It does not
invent `..` to walk to siblings and does not resolve existing `..`; the result is
lexical and does not establish filesystem containment.

`name()`, `stem()`, `suffix()` and `suffixes()` follow the documented Python 3.14
convention: suffixes include their dots; only the final suffix is removed from
the stem. Leading dots alone do not establish an extension. A trailing single dot
is a suffix: `archive.tar.gz` has stem `archive.tar`, suffix `.gz` and suffixes
`.tar`, `.gz`; `.profile` has no suffix, while `file.` has suffix `.`.

## Filesystem operations

Path delegates existence/type checks to QFileInfo, directory creation to QDir,
file creation to QFile and standard locations to QStandardPaths. The API operates
on the host filesystem only. Standard locations do not create directories, and
`writable()` throws `std::runtime_error` when Qt returns no location.

`exists()` covers files and directories and follows Qt's symlink semantics.
`createDirectories()` creates missing parents. `createFile()` requires an existing
parent and opens without truncating existing contents; it does not update time
metadata deliberately or promise Python touch semantics. File/directory creation
returns bool, optionally populating a caller-owned error string. A successful
operation clears a previously supplied error. QFile errors retain Qt's cause;
QDir does not expose an errorString, so directory failures identify a blocking
non-directory entry when possible and otherwise report the requested path.

`absolute()` delegates host-relative interpretation to QFileInfo without requiring
existence. `canonical()` requires a resolvable existing entry, follows Qt's
canonical/symlink handling and returns an optional Path plus an optional error.
Path strings themselves are never canonicalized silently during ordinary joining.
These operations do not claim delivery guarantees, atomic creation, sandboxing
or full filesystem identity comparison.

## Qt and Python references

Qt's [QDir](https://doc.qt.io/qt-6/qdir.html),
[QFileInfo](https://doc.qt.io/qt-6/qfileinfo.html),
[QFile](https://doc.qt.io/qt-6/qfile.html) and
[QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html) implement the I/O side.
[Python pathlib](https://docs.python.org/3.14/library/pathlib.html) is the lexical
API reference; this is a scoped C++/Qt API rather than a complete pathlib port.

## Verification

Enable `TERMASTER_BUILD_PATH_TESTS` to build PathTests. QtTest checks remain active
in Release. Fixtures cover empty/root paths, POSIX and Windows lexical rules,
UNC shares, drive-relative joins, Unicode, NUL rejection, serialization round
trips, dotfiles and trailing-dot extensions, strict relative paths and native
filesystem operations. File fixtures use QTemporaryDir under the test working
directory, exercise genuinely relative names and never change the process CWD.
No real application settings or device data are modified by these fixtures.

Verified on 2026-11-08 with Windows x64, MSVC 19.44, Qt 6.12.0 and Ninja Release:
the application and all affected test executables built; all four CTest suites
passed. PathTests passed all 20 results without skips. Existing plugin integration
(including an independently compiled SDK consumer), plugin GUI and action suites
passed after the SettingsManager call sites changed. Both POSIX and Windows
lexical rules were tested on Windows; native Unix filesystem execution was not
performed. `clang-format` and `git diff --check` were applied.
