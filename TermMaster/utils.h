#ifndef UTILS_H
#define UTILS_H

#include <QList>
#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QVariant>

class PathedValue
{
public:
    PathedValue();
    PathedValue(const PathedValue& copy);
    PathedValue(QVariant& root);
    ~PathedValue();

    QVariant get(const QString& path, QVariant defaultValue = QVariant(), bool autocreate = false);
    void set(const QVariant& value, const QStringList& path);
    void set(const QVariant& value, const QString& path);

    bool merge(const PathedValue& other);
    void clear();

    QVariant& operator[](const QStringList& path);
    QVariant& operator[](const QString& path);

    const QVariant& operator[](const QStringList& path) const;
    const QVariant& operator[](const QString& path) const;

protected:
    QVariant* _root;
    bool _createdByRef;
};

#endif // UTILS_H
