#include <Termaster/Core/PurePath.h>
#include <stdexcept>
#include <utility>

PurePath::PurePath(QString path, Syntax syntax) : _syntax(syntax)
{
    if (syntax != Syntax::Posix && syntax != Syntax::Windows)
    {
        throw std::invalid_argument("Invalid path syntax");
    }
    _parse(std::move(path));
}

PurePath::Syntax PurePath::nativeSyntax()
{
#ifdef Q_OS_WIN
    return Syntax::Windows;
#else
    return Syntax::Posix;
#endif
}

PurePath PurePath::posix(const QString& path)
{
    return PurePath(path, Syntax::Posix);
}

PurePath PurePath::windows(const QString& path)
{
    return PurePath(path, Syntax::Windows);
}

PurePath::Syntax PurePath::syntax() const
{
    return _syntax;
}

QString PurePath::toString() const
{
    QString result = _drive + _root + _parts.join('/');
    // Preserve a relative first component such as "c:notes" when serializing.
    if (_syntax == Syntax::Windows && _drive.isEmpty() && _root.isEmpty() && !_parts.isEmpty()
        && _parts.first().size() >= 2 && _parts.first()[0].isLetter() && _parts.first()[0].unicode() < 128
        && _parts.first()[1] == ':')
    {
        result.prepend("./");
    }
    return result.isEmpty() ? QString(".") : _format(result);
}

QString PurePath::drive() const
{
    return _format(_drive);
}

QString PurePath::root() const
{
    return _format(_root);
}

QString PurePath::anchor() const
{
    return _format(_drive + _root);
}

QStringList PurePath::parts() const
{
    QStringList result = _parts;
    if (!anchor().isEmpty())
    {
        result.prepend(anchor());
    }
    return result;
}

QString PurePath::name() const
{
    return _parts.isEmpty() ? QString() : _parts.last();
}

QString PurePath::stem() const
{
    const QString value = name();
    return value.first(value.size() - suffix().size());
}

QString PurePath::suffix() const
{
    const QString value   = name();
    qsizetype     leading = 0;
    while (leading < value.size() && value[leading] == '.')
    {
        ++leading;
    }
    const qsizetype dot = value.lastIndexOf('.');
    return dot >= leading ? value.mid(dot) : QString();
}

QStringList PurePath::suffixes() const
{
    QString   value   = name();
    qsizetype leading = 0;
    while (leading < value.size() && value[leading] == '.')
    {
        ++leading;
    }
    value                    = value.mid(leading);
    const QStringList fields = value.split('.');
    QStringList       result;
    for (qsizetype index = 1; index < fields.size(); ++index)
    {
        result.append('.' + fields[index]);
    }
    return result;
}

bool PurePath::isAbsolute() const
{
    return !_root.isEmpty() && (_syntax == Syntax::Posix || !_drive.isEmpty());
}

PurePath PurePath::parent() const
{
    PurePath result = *this;
    if (!result._parts.isEmpty())
    {
        result._parts.removeLast();
    }
    return result;
}

PurePath PurePath::join(const QString& segment) const
{
    return join(PurePath(segment, _syntax));
}

PurePath PurePath::join(const PurePath& segment) const
{
    if (_syntax != segment._syntax)
    {
        throw std::invalid_argument("Cannot join paths with different syntaxes");
    }

    PurePath result = *this;
    if (!segment._drive.isEmpty())
    {
        if (!_equal(_drive, segment._drive) || !segment._root.isEmpty())
        {
            return segment;
        }
        result._drive = segment._drive;
    }
    if (!segment._root.isEmpty())
    {
        result._root  = segment._root;
        result._parts = segment._parts;
    }
    else
    {
        result._parts.append(segment._parts);
    }
    return result;
}

std::optional<PurePath> PurePath::relativeTo(const PurePath& base) const
{
    if (_syntax != base._syntax || !_equal(_drive, base._drive) || !_equal(_root, base._root)
        || _parts.size() < base._parts.size())
    {
        return std::nullopt;
    }
    for (qsizetype index = 0; index < base._parts.size(); ++index)
    {
        if (!_equal(_parts[index], base._parts[index]))
        {
            return std::nullopt;
        }
    }

    PurePath result({}, _syntax);
    result._parts = _parts.mid(base._parts.size());
    return result;
}

PurePath PurePath::operator/(const QString& segment) const
{
    return join(segment);
}

PurePath PurePath::operator/(const PurePath& segment) const
{
    return join(segment);
}

PurePath& PurePath::operator/=(const QString& segment)
{
    *this = join(segment);
    return *this;
}

PurePath& PurePath::operator/=(const PurePath& segment)
{
    *this = join(segment);
    return *this;
}

bool PurePath::operator==(const PurePath& other) const
{
    if (_syntax != other._syntax || !_equal(_drive, other._drive) || !_equal(_root, other._root)
        || _parts.size() != other._parts.size())
    {
        return false;
    }
    for (qsizetype index = 0; index < _parts.size(); ++index)
    {
        if (!_equal(_parts[index], other._parts[index]))
        {
            return false;
        }
    }
    return true;
}

void PurePath::_parse(QString path)
{
    if (path.contains(QChar::Null))
    {
        throw std::invalid_argument("A path cannot contain NUL");
    }
    if (_syntax == Syntax::Windows)
    {
        path.replace(QChar(92), '/');
        // A UNC share is an indivisible drive. Extended UNC shares use the same rule.
        if (path.startsWith("//") && !path.startsWith("///"))
        {
            const qsizetype start  = path.startsWith("//?/UNC/", Qt::CaseInsensitive) ? 8 : 2;
            const auto      fields = path.mid(start).split('/');
            if (fields.size() >= 2 && !fields[0].isEmpty() && !fields[1].isEmpty())
            {
                _drive = path.first(start) + fields[0] + '/' + fields[1];
                _root  = "/";
                path   = fields.mid(2).join('/');
            }
        }
        if (_drive.isEmpty() && path.size() >= 2 && path[0].isLetter() && path[0].unicode() < 128 && path[1] == ':')
        {
            _drive = path.first(2);
            path.remove(0, 2);
        }
    }
    if (path.startsWith('/'))
    {
        _root = _syntax == Syntax::Posix && path.startsWith("//") && !path.startsWith("///") ? "//" : "/";
    }
    for (const QString& part : path.split('/', Qt::SkipEmptyParts))
    {
        if (part != ".")
        {
            _parts.append(part);
        }
    }
}

QString PurePath::_format(QString path) const
{
    if (_syntax == Syntax::Windows)
    {
        path.replace('/', QChar(92));
    }
    return path;
}

bool PurePath::_equal(const QString& left, const QString& right) const
{
    return left.compare(right, _syntax == Syntax::Windows ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0;
}

PurePosixPath::PurePosixPath(QString path) : PurePath(std::move(path), Syntax::Posix)
{
}

PureWindowsPath::PureWindowsPath(QString path) : PurePath(std::move(path), Syntax::Windows)
{
}
