#pragma once

#include <QString>
#include <QStringList>
#include <optional>

/** Lexical path value. No operation accesses the filesystem or resolves symlinks. */
class PurePath
{
public:
    enum class Syntax
    {
        Posix,
        Windows
    };

    explicit PurePath(QString path = {}, Syntax syntax = nativeSyntax());

    static Syntax   nativeSyntax();
    static PurePath posix(const QString& path = {});
    static PurePath windows(const QString& path = {});

    Syntax      syntax() const;
    QString     toString() const;
    QString     drive() const;
    QString     root() const;
    QString     anchor() const;
    QStringList parts() const;
    QString     name() const;
    QString     stem() const;
    QString     suffix() const;
    QStringList suffixes() const;
    bool        isAbsolute() const;

    PurePath parent() const;
    PurePath join(const QString& segment) const;
    /** Mixing syntaxes is rejected with std::invalid_argument. */
    PurePath join(const PurePath& segment) const;
    /** Strict lexical descendant relation; incompatible anchors/prefixes return nullopt. */
    std::optional<PurePath> relativeTo(const PurePath& base) const;

    PurePath  operator/(const QString& segment) const;
    PurePath  operator/(const PurePath& segment) const;
    PurePath& operator/=(const QString& segment);
    PurePath& operator/=(const PurePath& segment);
    bool      operator==(const PurePath& other) const;

private:
    Syntax      _syntax;
    QString     _drive;
    QString     _root;
    QStringList _parts;

    void    _parse(QString path);
    QString _format(QString path) const;
    bool    _equal(const QString& left, const QString& right) const;
};

/** POSIX lexical paths, including logical application paths.
 * Value operations return PurePath and preserve this syntax.
 */
class PurePosixPath final : public PurePath
{
public:
    explicit PurePosixPath(QString path = {});
};

/** Windows lexical paths, including drives and UNC shares, on any host.
 * Value operations return PurePath and preserve this syntax.
 */
class PureWindowsPath final : public PurePath
{
public:
    explicit PureWindowsPath(QString path = {});
};
