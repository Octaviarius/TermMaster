#include "Path.h"

static QStringList _splitPath(const QString& path, QChar sep);

Path::Path(const QString& path, QChar separator) : _sep(separator)
{
    *this = path;
}

Path Path::operator/(const QString& other) const
{
    Path newPath   = *this;
    newPath._parts += _splitPath(other, _sep);
    return newPath;
}

Path Path::operator|(const QString& other) const
{
    return operator/(other);
}

Path& Path::operator/=(const QString& other)
{
    _parts += _splitPath(other, _sep);
    return *this;
}

Path& Path::operator|=(const QString& other)
{
    return operator/=(other);
}

Path::operator QString() const
{
    return _parts.join(_sep);
}

Path& Path::operator=(const QString& path)
{
    _parts = _splitPath(path, _sep);
    return *this;
}

QStringList Path::parts()
{
    return _parts;
}

Path Path::parent() const
{
    Path parentPath = *this;
    if (!parentPath._parts.isEmpty())
    {
        parentPath._parts.removeLast();
    }
    return parentPath;
}

QString Path::filename() const
{
    return _parts.isEmpty() ? "" : _parts.last();
}

QString Path::extension() const
{
    QString file = filename();
    int     pos  = file.lastIndexOf('.');
    return (pos != -1) ? file.mid(pos) : "";
}

QString Path::stem() const
{
    QString file = filename();
    int     pos  = file.lastIndexOf('.');
    return (pos != -1) ? file.left(pos) : file;
}

void Path::goUp()
{
    _parts.pop_back();
}

void Path::goDown(QString part)
{
    _parts.push_back(part);
}

Path Path::cwd()
{
    return Path(QDir::currentPath());
}

Path Path::home()
{
    return Path(QDir::homePath());
}

UnixPath::UnixPath(const QString& path) : Path(path, '/')
{
}

WindowsPath::WindowsPath(const QString& path) : Path(path, '\\')
{
}

static QStringList _splitPath(const QString& path, QChar sep)
{
    QStringList res;

    if (path.length() > 0)
    {
        auto d = path.data();

        int s = 0;
        int e;

        for (e = 0; e < path.length(); e++)
        {
            if (d[e] == QChar('/') || d[e] == QChar('\\') || d[e] == sep)
            {
                if (e - s > 1)
                {
                    res += path.sliced(s, e - s);
                }

                s = ++e;
            }
        }

        if (e - s > 1)
        {
            res += path.sliced(s, e - s);
        }
    }

    return res;
}
