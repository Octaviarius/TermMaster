#include "utils.h"
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

PathedValue::PathedValue() : _root(nullptr), _createdByRef(false)
{
}

PathedValue::PathedValue(const PathedValue& copy)
{
    _createdByRef = false;
    _root         = new QVariant();
    *_root        = *copy._root;
}

PathedValue::PathedValue(QVariant& root) : _root(&root), _createdByRef(false)
{
}

PathedValue::~PathedValue()
{
    if (!_createdByRef && _root != nullptr)
    {
        delete _root;
    }
}

QVariant PathedValue::get(const QString& path, QVariant defaultValue, bool autocreate)
{
    auto parts = path.split('/');
    bool ok;
    int idx;

    if (!_root)
    {
        _root = new QVariant();

        parts[0].toInt(&ok);
        if (ok)
        {
            *_root = QVariantList();
        }
        else
        {
            *_root = QVariantMap();
        }
    }

    QVariant* obj = _root;

    foreach (auto p, parts)
    {
        switch (obj->userType())
        {
            case QMetaType::QVariantMap:
            {
                QVariantMap& map = *reinterpret_cast<QVariantMap*>(obj);

                if (map.contains(p))
                {
                    obj = &map[p];
                }
                else
                {
                    if (autocreate)
                    {
                        set(defaultValue, parts);
                    }
                    return defaultValue;
                }
                break;
            }

            case QMetaType::QVariantList:
            {
                QVariantList& list = *reinterpret_cast<QVariantList*>(obj);
                idx                = p.toInt(&ok);

                if (ok && idx >= 0 && list.size() > idx)
                {
                    obj = &list[idx];
                }
                else
                {
                    if (autocreate)
                    {
                        set(defaultValue, parts);
                    }
                    return defaultValue;
                }

                break;
            }
        }

        idx++;
    }

    return *obj;
}

void PathedValue::set(const QVariant& value, const QStringList& path)
{
    bool ok;
    int idx;

    if (!_root)
    {
        _root = new QVariant();

        path[0].toInt(&ok);
        if (ok)
        {
            *_root = QVariantList();
        }
        else
        {
            *_root = QVariantMap();
        }
    }

    QVariant* obj = _root;

    for (auto p = path.constBegin(), nextP = path.constBegin() + 1; p != path.constEnd(); p++, nextP++)
    {
        const bool isLast = nextP == path.constEnd();

        switch (obj->userType())
        {
            case QMetaType::QVariantMap:
            {
                QVariantMap& map = *reinterpret_cast<QVariantMap*>(obj);

                if (isLast)
                {
                    map[*p] = value;
                    return;
                }
                else
                {
                    if (map.contains(*p))
                    {
                        obj = &map[*p];
                    }
                    else
                    {
                        idx = nextP->toInt(&ok);

                        if (ok)
                        {
                            map[*p]            = QVariantList();
                            obj                = &map[*p];
                            QVariantList& list = *reinterpret_cast<QVariantList*>(obj);
                            list.resize(idx + 1);
                        }
                        else
                        {
                            map[*p] = QVariantMap();
                            obj     = &map[*p];
                        }
                    }
                }
                break;
            }

            case QMetaType::QVariantList:
            {
                QVariantList& list = *reinterpret_cast<QVariantList*>(obj);
                idx                = p->toInt(&ok);

                if (ok && idx >= 0)
                {
                    if (isLast)
                    {
                        list[idx] = value;
                        return;
                    }
                    else
                    {
                        if (list.size() < idx)
                        {
                            list.resize(idx + 1);
                        }

                        obj = &list[idx];
                        if (obj->isNull())
                        {
                            idx = nextP->toInt(&ok);
                            if (ok)
                            {
                                obj->setValue(QVariantList());
                                reinterpret_cast<QVariantList*>(obj)->resize(idx + 1);
                            }
                            else
                            {
                                obj->setValue(QVariantMap());
                            }
                        }
                    }
                }
                else
                {
                }

                break;
            }
        }
    }
}

void PathedValue::set(const QVariant& value, const QString& path)
{
    set(value, path.split('/'));
}

bool PathedValue::merge(const PathedValue& other)
{
}

void PathedValue::clear()
{
    _root->clear();
}

QVariant& PathedValue::operator[](const QStringList& path)
{
    bool ok;
    int idx;

    if (!_root)
    {
        _root = new QVariant();

        path[0].toInt(&ok);
        if (ok)
        {
            *_root = QVariantList();
        }
        else
        {
            *_root = QVariantMap();
        }
    }

    QVariant* obj = _root;

    for (auto p = path.constBegin(), nextP = path.constBegin() + 1; p != path.constEnd(); p++, nextP++)
    {
        const bool isLast = nextP == path.constEnd();

        switch (obj->userType())
        {
            case QMetaType::QVariantMap:
            {
                QVariantMap& map = *reinterpret_cast<QVariantMap*>(obj);

                if (isLast)
                {
                    return map[*p];
                }
                else
                {
                    if (map.contains(*p))
                    {
                        obj = &map[*p];
                    }
                    else
                    {
                        idx = nextP->toInt(&ok);

                        if (ok)
                        {
                            map[*p]            = QVariantList();
                            obj                = &map[*p];
                            QVariantList& list = *reinterpret_cast<QVariantList*>(obj);
                            list.resize(idx + 1);
                        }
                        else
                        {
                            map[*p] = QVariantMap();
                            obj     = &map[*p];
                        }
                    }
                }
                break;
            }

            case QMetaType::QVariantList:
            {
                QVariantList& list = *reinterpret_cast<QVariantList*>(obj);
                idx                = p->toInt(&ok);

                if (ok && idx >= 0)
                {
                    if (isLast)
                    {
                        return list[idx];
                    }
                    else
                    {
                        if (list.size() < idx)
                        {
                            list.resize(idx + 1);
                        }

                        obj = &list[idx];
                        if (obj->isNull())
                        {
                            idx = nextP->toInt(&ok);
                            if (ok)
                            {
                                obj->setValue(QVariantList());
                                reinterpret_cast<QVariantList*>(obj)->resize(idx + 1);
                            }
                            else
                            {
                                obj->setValue(QVariantMap());
                            }
                        }
                    }
                }
                else
                {
                }

                break;
            }
        }
    }

    return *obj;
}

QVariant& PathedValue::operator[](const QString& path)
{
    return (*this)[path.split('/')];
}

const QVariant& PathedValue::operator[](const QStringList& path) const
{
    return (*this)[path];
}

const QVariant& PathedValue::operator[](const QString& path) const
{
    return (*this)[path];
}
