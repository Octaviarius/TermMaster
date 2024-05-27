#include "Registree.h"

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

static QVariant& makeNodeInMap(QVariantMap& map, const QString& index, const QString& nextIndex);
static QVariant& makeNodeInList(QVariantList& list, int index, const QString& nextIndex);
static QVariant& makeNodeInUnknown(QVariant& var, const QString& index);

Registree::Registree() : _root(new QVariant())
{
}

Registree::Registree(const Registree& copy)
{
    _relPath = copy._relPath;
    _root    = copy._root;
}

Registree::~Registree()
{
}

Registree Registree::node(QString path)
{
    Registree res(*this);
    res._relPath += path.split('/');
    return res;
}

QVariant Registree::get(const QString& path, QVariant defaultValue) const
{
    auto parts = _relPath + path.split('/');
    bool ok;
    int  idx;

    QVariant* obj = _root.data();

    if (obj->isNull())
    {
        return defaultValue;
    }

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
                    return defaultValue;
                }

                break;
            }
        }

        idx++;
    }

    return *obj;
}

QVariant& Registree::get(const QString& path, const QVariant& defaultValue)
{
    auto& ret = (*this)[path];

    if (ret.isNull())
    {
        ret = defaultValue;
    }

    return ret;
}

void Registree::set(const QVariant& value, QStringList path)
{
    bool ok;
    int  idx;

    if (_relPath.size() > 0)
    {
        path = _relPath + path;
    }

    QVariant* obj = _root.data();

    if (obj->isNull())
    {
    }

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

void Registree::set(const QVariant& value, const QString& path)
{
    set(value, path.split('/'));
}

bool Registree::merge(const Registree& other)
{
}

void Registree::clear()
{
    _root->clear();
}

QVariant& Registree::operator[](QStringList path)
{
    bool ok;
    int  idx;

    if (_relPath.size() > 0)
    {
        path = _relPath + path;
    }

    QVariant* obj = _root.data();

    if (obj->userType() == QMetaType::UnknownType)
    {
        obj = &makeNodeInUnknown(*obj, path[0]);
    }

    for (auto p = path.constBegin(), nextP = path.constBegin() + 1; p != path.constEnd(); p++, nextP++)
    {
        const bool isLast = nextP == path.constEnd();

        switch (obj->userType())
        {
            case QMetaType::QVariantMap:
            {
                QVariantMap& map = reinterpret_cast<QVariantMap&>(*obj);

                if (isLast)
                {
                    if (!map.contains(*p))
                    {
                        map[*p] = QVariant();
                    }

                    obj = &map[*p];
                }
                else
                {
                    obj = &makeNodeInMap(map, *p, *nextP);
                }
                break;
            }

            case QMetaType::QVariantList:
            {
                QVariantList& list = reinterpret_cast<QVariantList&>(*obj);
                idx                = p->toInt(&ok);

                if (ok && idx >= 0)
                {
                    if (isLast)
                    {
                        obj = &list[idx];
                    }
                    else
                    {
                        obj = &makeNodeInList(list, idx, *nextP);
                    }
                }
                else
                {
                    auto mp = QVariantMap();

                    for (int i = 0; i < list.size(); i++)
                    {
                        mp[QString("%1").arg(i)] = list[i];
                    }

                    obj->setValue(mp);

                    QVariantMap& map = reinterpret_cast<QVariantMap&>(*obj);
                    if (isLast)
                    {
                        if (!map.contains(*p))
                        {
                            map[*p] = QVariant();
                        }

                        obj = &map[*p];
                    }
                    else
                    {
                        obj = &makeNodeInMap(map, *p, *nextP);
                    }
                }

                break;
            }
        }
    }

    return *obj;
}

QVariant& Registree::operator[](const QString& path)
{
    return (*this)[path.split('/')];
}

static QVariant& makeNodeInMap(QVariantMap& map, const QString& index, const QString& nextIndex)
{
    if (!map.contains(index))
    {
        map[index] = QVariant();
    }

    return makeNodeInUnknown(map[index], nextIndex);
}

static QVariant& makeNodeInList(QVariantList& list, int index, const QString& nextIndex)
{
    if (list.size() <= index)
    {
        list.resize(index + 1);
    }

    return makeNodeInUnknown(list[index], nextIndex);
}

static QVariant& makeNodeInUnknown(QVariant& var, const QString& index)
{
    if (var.isNull())
    {
        bool ok;
        auto idx = index.toInt(&ok);

        if (ok)
        {
            var = QVariantList();
            reinterpret_cast<QVariantList&>(var).resize(idx + 1);
        }
        else
        {
            var                                        = QVariantMap();
            reinterpret_cast<QVariantMap&>(var)[index] = QVariant();
        }
    }

    return var;
}
