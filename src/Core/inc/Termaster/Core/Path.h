#pragma once

#include <QStandardPaths>
#include <Termaster/Core/PurePath.h>

/** Native filesystem path. File operations delegate to Qt; lexical state is PurePath. */
class Path
{
public:
    explicit Path(QString path = {});
    /** A foreign syntax is rejected with std::invalid_argument. */
    explicit Path(const PurePath& path);

    static Path current();
    static Path home();
    static Path appData();
    /** Throws std::runtime_error if Qt cannot provide the requested location. */
    static Path writable(QStandardPaths::StandardLocation location);

    PurePath    pure() const;
    QString     toString() const;
    QStringList parts() const;
    QString     name() const;
    QString     stem() const;
    QString     suffix() const;
    QStringList suffixes() const;
    bool        isAbsolute() const;

    Path parent() const;
    Path join(const QString& segment) const;
    Path join(const Path& segment) const;
    Path absolute() const;
    /** Resolves existing paths, including symlinks. Failure returns nullopt and a cause. */
    std::optional<Path>     canonical(QString* error = nullptr) const;
    std::optional<PurePath> relativeTo(const Path& base) const;

    bool exists() const;
    bool isFile() const;
    bool isDirectory() const;
    bool createDirectories(QString* error = nullptr) const;
    /** Creates a file without truncating existing contents; the parent must already exist. */
    bool createFile(QString* error = nullptr) const;

    Path  operator/(const QString& segment) const;
    Path  operator/(const Path& segment) const;
    Path& operator/=(const QString& segment);
    Path& operator/=(const Path& segment);
    bool  operator==(const Path& other) const;

private:
    PurePath _path;
};
