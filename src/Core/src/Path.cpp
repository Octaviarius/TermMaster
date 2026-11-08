#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <Termaster/Core/Path.h>
#include <stdexcept>
#include <utility>

Path::Path(QString path) : _path(std::move(path))
{
}

Path::Path(const PurePath& path) : _path(path)
{
    if (path.syntax() != PurePath::nativeSyntax())
    {
        throw std::invalid_argument("Filesystem paths require native syntax");
    }
}

Path Path::current()
{
    return Path(QDir::currentPath());
}

Path Path::home()
{
    return Path(QDir::homePath());
}

Path Path::appData()
{
    return writable(QStandardPaths::AppDataLocation);
}

Path Path::writable(QStandardPaths::StandardLocation location)
{
    const QString value = QStandardPaths::writableLocation(location);
    if (value.isEmpty())
    {
        throw std::runtime_error("No writable location is available for the requested standard path");
    }
    return Path(value);
}

PurePath Path::pure() const
{
    return _path;
}

QString Path::toString() const
{
    return _path.toString();
}

QStringList Path::parts() const
{
    return _path.parts();
}

QString Path::name() const
{
    return _path.name();
}

QString Path::stem() const
{
    return _path.stem();
}

QString Path::suffix() const
{
    return _path.suffix();
}

QStringList Path::suffixes() const
{
    return _path.suffixes();
}

bool Path::isAbsolute() const
{
    return _path.isAbsolute();
}

Path Path::parent() const
{
    return Path(_path.parent());
}

Path Path::join(const QString& segment) const
{
    return Path(_path.join(segment));
}

Path Path::join(const Path& segment) const
{
    return Path(_path.join(segment._path));
}

Path Path::absolute() const
{
    return Path(QFileInfo(toString()).absoluteFilePath());
}

std::optional<Path> Path::canonical(QString* error) const
{
    if (error)
    {
        error->clear();
    }
    const QString result = QFileInfo(toString()).canonicalFilePath();
    if (result.isEmpty())
    {
        if (error)
        {
            *error = QString("Path does not exist or cannot be resolved: %1").arg(toString());
        }
        return std::nullopt;
    }
    return Path(result);
}

std::optional<PurePath> Path::relativeTo(const Path& base) const
{
    return _path.relativeTo(base._path);
}

bool Path::exists() const
{
    return QFileInfo::exists(toString());
}

bool Path::isFile() const
{
    return QFileInfo(toString()).isFile();
}

bool Path::isDirectory() const
{
    return QFileInfo(toString()).isDir();
}

bool Path::createDirectories(QString* error) const
{
    if (error)
    {
        error->clear();
    }
    if (QDir().mkpath(toString()))
    {
        return true;
    }
    if (error)
    {
        // QDir::mkpath has no errorString; identify a blocking entry when possible.
        Path ancestor = *this;
        while (true)
        {
            if (ancestor.exists() && !ancestor.isDirectory())
            {
                *error = QString("Not a directory: %1").arg(ancestor.toString());
                break;
            }
            const Path parent = ancestor.parent();
            if (parent == ancestor)
            {
                *error = QString("Cannot create directory: %1").arg(toString());
                break;
            }
            ancestor = parent;
        }
    }
    return false;
}

bool Path::createFile(QString* error) const
{
    if (error)
    {
        error->clear();
    }
    QFile file(toString());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
    {
        if (error)
        {
            *error = QString("Cannot create file %1: %2").arg(toString(), file.errorString());
        }
        return false;
    }
    file.close();
    if (file.error() != QFileDevice::NoError)
    {
        if (error)
        {
            *error = QString("Cannot close file %1: %2").arg(toString(), file.errorString());
        }
        return false;
    }
    return true;
}

Path Path::operator/(const QString& segment) const
{
    return join(segment);
}

Path Path::operator/(const Path& segment) const
{
    return join(segment);
}

Path& Path::operator/=(const QString& segment)
{
    *this = join(segment);
    return *this;
}

Path& Path::operator/=(const Path& segment)
{
    *this = join(segment);
    return *this;
}

bool Path::operator==(const Path& other) const
{
    return _path == other._path;
}
