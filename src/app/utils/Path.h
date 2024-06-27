#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

class PathBase
{
protected:
    QStringList _parts;
};

template <char S, typename Pathlike>
class TPathBase : public PathBase
{
public:
    using PathBase::_parts;

    TPathBase()
    {
    }

    TPathBase(const QString& path)
    {
        _parts = path.split(S);
    }

    TPathBase(const QStringList& path)
    {
        _parts = path;
    }

    Pathlike operator/(const QString& path) const
    {
        Pathlike newPath;
        newPath._parts = _parts + path.split(S);
        return newPath;
    }

    template <char _S, typename _Pathlike>
    Pathlike operator/(const TPathBase<_S, _Pathlike>& path)
    {
        Pathlike newPath;
        newPath._parts = _parts + path._parts;
        return newPath;
    }

    Pathlike& operator/=(const QString& path)
    {
        _parts += path.split(S);
        return self();
    }

    template <char _S, typename _Pathlike>
    Pathlike& operator/=(const TPathBase<_S, _Pathlike>& path)
    {
        _parts += path._parts;
        return self();
    }

    Pathlike& goUp(int steps = 1)
    {
        _parts.resize(_parts.count() - steps);
        return self();
    }

    Pathlike& goDown(const QString& path)
    {
        _parts += path.split(S);
        return self();
    }

    Pathlike& goDown(const QStringList& path)
    {
        _parts += path;
        return self();
    }

    template <char _S, typename _Pathlike>
    Pathlike& goDown(const TPathBase<_S, _Pathlike>& path)
    {
        _parts += path._parts;
        return self();
    }

    QStringList parts() const
    {
        return _parts;
    }

    Pathlike parent() const
    {
        return Pathlike(_parts.sliced(0, std::max<int>(0, _parts.count() - 1)));
    }

    operator QString() const
    {
        return _parts.join(S);
    }

    operator QStringList() const
    {
        return _parts;
    }

    template <char _S, typename _Pathlike>
    operator TPathBase<_S, _Pathlike>() const
    {
        return TPathBase<_S, _Pathlike>(_parts);
    }

    Pathlike& operator=(const QString& path)
    {
        _parts = path.split(S);
        return self();
    }

    template <char _S, typename _Pathlike>
    Pathlike relativeTo(const TPathBase<_S, _Pathlike>& base)
    {
        Pathlike newPath;

        int i;

        // go for a while parts are equal
        auto minCount = std::min(_parts.count(), base._parts.count());
        for (i = 0; i < minCount; i++)
        {
            if (_parts[i] != base._parts[i])
            {
                break;
            }
        }

        for (int j = i; j < base._parts.count(); j++)
        {
            newPath._parts += "..";
        }

        for (; i < _parts.count(); i++)
        {
            newPath._parts += _parts[i];
        }

        return newPath;
    }

    template <char _S, typename _Pathlike>
    Pathlike absoluteFrom(const TPathBase<_S, _Pathlike>& base)
    {
        Pathlike newPath(base);

        for (const auto& p : _parts)
        {
            if (p == "..")
            {
                newPath._parts.removeLast();
            }
            else
            {
                newPath._parts += p;
            }
        }
        return newPath;
    }

    QString suffix() const
    {
        auto nm = name();

        auto firstDot = nm.indexOf('.');
        if (firstDot < 0)
        {
            return "";
        }
        else
        {
            return nm.sliced(firstDot + 1);
        }
    }

    QStringList suffixes() const
    {
        return name().split(".").sliced(1);
    }

    QString stem() const
    {
        auto sufs = suffixes();
        if (sufs.count() == 0)
        {
            return "";
        }
        else
        {
            return sufs.last();
        }
    }

    QString name() const
    {
        if (_parts.count() == 0)
        {
            return "";
        }
        else
        {
            return _parts.last();
        }
    }

protected:
    constexpr Pathlike& self()
    {
        return static_cast<Pathlike&>(*this);
    }
};

template <char S, typename Pathlike>
TPathBase<S, Pathlike> operator/(const QString& left, const TPathBase<S, Pathlike>& right)
{
    return TPathBase<S, Pathlike>(left) / right;
}

/*******************************************************************************************/

template <char S>
class TPurePath : public TPathBase<S, TPurePath<S>>
{
public:
    using TPathBase<S, TPurePath<S>>::TPathBase;
    using TPathBase<S, TPurePath<S>>::_parts;

    static constexpr char separator = S;

    template <char _S>
    TPurePath(const TPurePath<_S>& path) : TPurePath(path._parts)
    {
    }
};

using PurePosixPath = TPurePath<'/'>;
using PureWinPath   = TPurePath<'\\'>;

#ifdef WIN32
using PurePath = PureWinPath;
#else
using PurePath = PurePosixPath;
#endif

/*******************************************************************************************/

template <char S>
class TPath : public TPathBase<S, TPath<S>>
{
public:
    using TPathBase<S, TPath<S>>::TPathBase;
    using TPathBase<S, TPath<S>>::_parts;

    template <char _S>
    TPath(const TPath<_S>& path) : TPath(path._parts)
    {
    }

    template <char _S>
    TPath(const TPurePath<_S>& path) : TPath(path._parts)
    {
    }

    operator TPurePath<S>() const
    {
        return TPurePath<S>(_parts);
    }

    TPath& operator=(const QString& path)
    {
        _parts = path.split(S);
        return *this;
    }

    TPurePath<S> purePath() const
    {
        return static_cast<TPurePath<S>>(*this);
    }

    static TPath currentDir()
    {
        return TPath(PurePath(QDir::currentPath()));
    }

    static TPath homeDir()
    {
        return TPath(PurePath(QDir::homePath()));
    }

    static TPath appdataDir()
    {
        return TPath(PurePath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));
    }

    static TPath writableDir(QStandardPaths::StandardLocation location)
    {
        return TPath(PurePath(QStandardPaths::writableLocation(location)));
    }
};

using PosixPath = TPath<'/'>;
using WinPath   = TPath<'\\'>;

#ifdef WIN32
using Path = WinPath;
#else
using Path     = PosixPath;
#endif
