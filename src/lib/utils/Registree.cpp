#include "Registree.h"

Registree::Registree()
{
    _map  = QSharedPointer<QVariant>::create();
    *_map = QVariantMap();
}

Registree::Registree(QString relPath, QSharedPointer<QVariant> map)
{
    _relPath = relPath.split('/');
    _map     = map;
}

void Registree::setValue(QString path, const QVariant& value)
{
    auto resPath = _relPath + path.split('/');

    QVariant* m = &map();

    for (const auto& p : resPath)
    {
        const bool isLast = (p == resPath.last());

        if (!isLast && m->typeId() != QMetaType::Type::QVariantMap)
        {
            *m = QVariantMap();
        }

        QVariantMap& vm = reinterpret_cast<QVariantMap&>(*m);

        if (isLast)
        {
            vm[p] = value;
        }
        else
        {
            if (!vm.contains(p))
            {
                vm[p] = QVariantMap();
            }
            m = &vm[p];
        }
    }
}

QVariant& Registree::value(QString path, const QVariant& defaultValue)
{
    auto resPath = _relPath + path.split('/');

    QVariant* m = &map();

    for (const auto& p : resPath)
    {
        const bool isLast = (p == resPath.last());

        if (!isLast && m->typeId() != QMetaType::Type::QVariantMap)
        {
            *m = QVariantMap();
        }

        QVariantMap& vm = reinterpret_cast<QVariantMap&>(*m);

        if (isLast)
        {
            if (!vm.contains(p))
            {
                vm[p] = defaultValue;
            }

            return vm[p];
        }
        else
        {
            if (!vm.contains(p))
            {
                vm[p] = QVariantMap();
            }
            m = &vm[p];
        }
    }

    return *m;
}

QVariant& Registree::value(QString path)
{
    return value(path, QVariant());
}

QVariant Registree::constValue(QString path, const QVariant& defaultValue) const
{
    auto resPath = _relPath + path.split('/');

    const QVariant* m = &map();

    for (const auto& p : resPath)
    {
        const bool isLast = (p == resPath.last());

        if (!isLast && m->typeId() != QMetaType::Type::QVariantMap)
        {
            return defaultValue;
        }

        // dirty hack
        QVariantMap& vm = **((QVariantMap**)&m);

        if (isLast)
        {
            if (!vm.contains(p))
            {
                return defaultValue;
            }

            return vm[p];
        }
        else
        {
            if (!vm.contains(p))
            {
                return defaultValue;
            }

            m = &vm[p];
        }
    }

    return defaultValue;
}

QVariant Registree::constValue(QString path) const
{
    return constValue(path, QVariant());
}

Registree Registree::node(QString relPath)
{
    return Registree(relPath, _map);
}

Registree Registree::root()
{
    return Registree("", _map);
}

Registree Registree::detachNode(QString relPath)
{
    return Registree("", QSharedPointer<QVariant>::create(value(relPath)));
}

QVariant& Registree::rootMap()
{
    return map();
}

QVariant Registree::operator[](QString path) const
{
    return constValue(path);
}

QVariant& Registree::operator[](QString path)
{
    return value(path);
}
