#ifndef PATH_H
#define PATH_H

#include <QDir>
#include <QString>

class Path
{
public:
    Path(const QString& path = "", QChar separator = QDir::separator());

    Path  operator/(const QString& other) const;
    Path  operator|(const QString& other) const;
    Path& operator/=(const QString& other);
    Path& operator|=(const QString& other);
          operator QString() const;

    QStringList parts();

    Path    parent() const;
    QString filename() const;
    QString extension() const;
    QString stem() const;

    void goUp();
    void goDown(QString part);

    static Path cwd();
    static Path home();

private:
    QStringList _parts;
    QChar       _sep;
};

class UnixPath : public Path
{
public:
    UnixPath(const QString& path = "");
};

class WindowsPath : public Path
{
public:
    WindowsPath(const QString& path = "");
};

#endif // PATH_H
