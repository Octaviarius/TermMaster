#ifndef REGISTREE_H
#define REGISTREE_H

#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QVariant>

class Registree
{
public:
    Registree();

    void      setValue(QString path, const QVariant& value);
    QVariant& value(QString path, const QVariant& defaultValue);
    QVariant& value(QString path);

    QVariant constValue(QString path, const QVariant& defaultValue) const;
    QVariant constValue(QString path) const;

    Registree root();
    Registree node(QString relPath);
    Registree detachNode(QString relPath);

    QVariant& rootMap();

    QVariant  operator[](QString path) const;
    QVariant& operator[](QString path);

private:
    QStringList              _relPath;
    QSharedPointer<QVariant> _map;

    inline QVariant& map()
    {
        return *_map;
    }

    inline const QVariant& map() const
    {
        return *_map;
    }

    Registree(QString relPath, QSharedPointer<QVariant> map);
};

#endif // REGISTREE_H
