#ifndef REGISTREE_H
#define REGISTREE_H

#include <QList>
#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QVariant>

class Registree
{
public:
    Registree();
    Registree(const Registree& copy);
    ~Registree();

    Registree node(QString path);

    QVariant  get(const QString& path, QVariant defaultValue = QVariant()) const;
    QVariant& get(const QString& path, const QVariant& defaultValue = QVariant());
    void      set(const QVariant& value, QStringList path);
    void      set(const QVariant& value, const QString& path);

    bool merge(const Registree& other);
    void clear();

    QVariant& operator[](QStringList path);
    QVariant& operator[](const QString& path);

protected:
    QSharedPointer<QVariant> _root;
    QStringList              _relPath;
};

#endif // REGISTREE_H
