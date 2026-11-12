#include "TerminalWorkspace.h"

#include "TerminalTabGroup.h"

#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <limits>

TerminalWorkspace::TerminalWorkspace(QWidget* parent) : QWidget(parent)
{
    setObjectName("terminalWorkspace");
    _layout = new QVBoxLayout(this);
    _layout->setContentsMargins(0, 0, 0, 0);
    _layout->setSpacing(0);
    _setRoot(_createGroup());
}

TerminalTabGroup* TerminalWorkspace::initialGroup() const
{
    const auto all = groups();
    return all.isEmpty() ? nullptr : all.first();
}

QList<TerminalTabGroup*> TerminalWorkspace::groups() const
{
    QList<TerminalTabGroup*>            result;
    const std::function<void(QWidget*)> visit = [&](QWidget* node) {
        if (auto* group = qobject_cast<TerminalTabGroup*>(node))
        {
            result.append(group);
        }
        else if (auto* splitter = qobject_cast<QSplitter*>(node))
        {
            for (int index = 0; index < splitter->count(); ++index)
            {
                visit(splitter->widget(index));
            }
        }
    };
    visit(_root);
    return result;
}

TerminalTabGroup* TerminalWorkspace::groupFor(QWidget* page) const
{
    if (!page)
    {
        return nullptr;
    }
    for (auto* group : groups())
    {
        if (group->tabs().indexOf(page) >= 0)
        {
            return group;
        }
    }
    return nullptr;
}

TerminalTabGroup* TerminalWorkspace::findGroup(const QString& id) const
{
    for (auto* group : groups())
    {
        if (group->id() == id)
        {
            return group;
        }
    }
    return nullptr;
}

TerminalTabGroup* TerminalWorkspace::splitGroup(TerminalTabGroup* group, Qt::Edge edge)
{
    if (!group || !groups().contains(group) || groups().size() >= 256
        || (edge != Qt::LeftEdge && edge != Qt::RightEdge && edge != Qt::TopEdge && edge != Qt::BottomEdge))
    {
        return nullptr;
    }

    const Qt::Orientation orientation = edge == Qt::LeftEdge || edge == Qt::RightEdge ? Qt::Horizontal : Qt::Vertical;
    const bool            before      = edge == Qt::LeftEdge || edge == Qt::TopEdge;
    auto*                 parent      = qobject_cast<QSplitter*>(group->parentWidget());
    int                   depth       = 0;
    for (auto* ancestor = group->parentWidget(); ancestor && ancestor != this; ancestor = ancestor->parentWidget())
    {
        if (qobject_cast<QSplitter*>(ancestor))
        {
            ++depth;
        }
    }
    if ((!parent || parent->orientation() != orientation) && depth >= 15)
    {
        return nullptr;
    }

    auto* created = _createGroup();
    if (parent && parent->orientation() == orientation)
    {
        const int index        = parent->indexOf(group);
        auto      sizes        = parent->sizes();
        const int originalSize = sizes.value(index, 2);
        sizes[index]           = std::max(1, originalSize / 2);
        sizes.insert(index + (before ? 0 : 1), std::max(1, originalSize - sizes[index]));
        parent->insertWidget(index + (before ? 0 : 1), created);
        parent->setSizes(sizes);
    }
    else
    {
        auto* splitter = new QSplitter(orientation, this);
        splitter->setChildrenCollapsible(false);
        QList<int> parentSizes;
        if (parent)
        {
            parentSizes = parent->sizes();
            parent->insertWidget(parent->indexOf(group), splitter);
        }
        else
        {
            _layout->removeWidget(group);
            _setRoot(splitter);
        }
        splitter->addWidget(before ? created : group);
        splitter->addWidget(before ? group : created);
        splitter->setSizes({1, 1});
        if (parent)
        {
            parent->setSizes(parentSizes);
        }
    }
    group->show();
    created->show();
    emit groupAdded(created);
    return created;
}

void TerminalWorkspace::removeEmptyGroups()
{
    const auto        all          = groups();
    TerminalTabGroup* retained     = nullptr;
    bool              hasPlacement = false;
    for (auto* group : all)
    {
        if (group->tabs().count() > 0 || group->property("retainedPlacement").toBool())
        {
            hasPlacement = true;
            break;
        }
    }
    if (!hasPlacement && !all.isEmpty())
    {
        retained = all.first();
    }

    QWidget* root = _prune(_root, retained);
    if (!root)
    {
        root = _createGroup();
    }
    if (root != _root)
    {
        _setRoot(root);
    }
    root->show();
}

QVariantMap TerminalWorkspace::saveLayout() const
{
    return _saveNode(_root);
}

bool TerminalWorkspace::restoreLayout(const QVariantMap& layout, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto fail = [error](const QString& message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    for (auto* group : groups())
    {
        if (group->tabs().count() != 0)
        {
            return fail(tr("Terminal layout can only be restored into empty groups."));
        }
    }

    int                      groupCount    = 0;
    int                      terminalCount = 0;
    QSet<QString>            groupIds;
    QSet<QString>            terminalIds;
    QString                  validationError;
    const QRegularExpression groupPattern("\\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z");
    const QRegularExpression terminalPattern("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z");
    const auto isString = [](const QVariant& value) { return value.metaType().id() == QMetaType::QString; };
    const std::function<bool(const QVariantMap&, int)> validate = [&](const QVariantMap& node, int depth) {
        if (depth > 16)
        {
            validationError = tr("Terminal layout exceeds the maximum depth of 16.");
            return false;
        }
        const QString type = node.value("type").toString();
        if (!isString(node.value("type")))
        {
            validationError = tr("Terminal layout node type must be a string.");
            return false;
        }
        if (type == "group")
        {
            const QVariant idValue = node.value("id");
            const QString  id      = idValue.toString();
            if (++groupCount > 256 || !isString(idValue) || !groupPattern.match(id).hasMatch() || QUuid(id).isNull()
                || groupIds.contains(id))
            {
                validationError = tr("Terminal layout has an invalid or duplicate group ID, or too many groups.");
                return false;
            }
            groupIds.insert(id);
            for (const auto& field : {"toolbarVisible", "statusBarVisible"})
            {
                if (node.contains(field) && node.value(field).typeId() != QMetaType::Bool)
                {
                    validationError = tr("Terminal group visibility must be boolean.");
                    return false;
                }
            }
            const QVariant tabs = node.value("tabs");
            if (tabs.metaType().id() != QMetaType::QVariantList)
            {
                validationError = tr("Terminal group tabs must be a list.");
                return false;
            }
            const QVariantList ids = tabs.toList();
            if (ids.size() > 100000 - terminalCount)
            {
                validationError = tr("Terminal layout exceeds the maximum terminal count.");
                return false;
            }
            terminalCount += int(ids.size());
            QSet<QString> localIds;
            for (const auto& value : ids)
            {
                const QString terminalId = value.toString();
                if (!isString(value) || !terminalPattern.match(terminalId).hasMatch()
                    || terminalIds.contains(terminalId))
                {
                    validationError = tr("Terminal layout has an invalid or duplicate terminal ID.");
                    return false;
                }
                terminalIds.insert(terminalId);
                localIds.insert(terminalId);
            }
            const QVariant active = node.value("active");
            if (node.contains("active")
                && (!isString(active) || (!active.toString().isEmpty() && !localIds.contains(active.toString()))))
            {
                validationError = tr("Terminal group active ID must identify one of its tabs.");
                return false;
            }
            return true;
        }
        if (type != "split")
        {
            validationError = tr("Terminal layout contains an unknown node type.");
            return false;
        }
        const QVariant orientation = node.value("orientation");
        if (!isString(orientation) || (orientation.toString() != "horizontal" && orientation.toString() != "vertical"))
        {
            validationError = tr("Terminal splitter orientation must be horizontal or vertical.");
            return false;
        }
        const QVariant children = node.value("children");
        const QVariant sizes    = node.value("sizes");
        if (children.metaType().id() != QMetaType::QVariantList || sizes.metaType().id() != QMetaType::QVariantList)
        {
            validationError = tr("Terminal splitter children and sizes must be lists.");
            return false;
        }
        const QVariantList childList = children.toList();
        const QVariantList sizeList  = sizes.toList();
        if (childList.size() < 2 || childList.size() > 256 || childList.size() != sizeList.size())
        {
            validationError = tr("Terminal splitter needs matching children and sizes, with at least two children.");
            return false;
        }
        for (qsizetype index = 0; index < childList.size(); ++index)
        {
            const auto&     size      = sizeList.at(index);
            bool            converted = false;
            const qlonglong number    = size.toLongLong(&converted);
            const int       sizeType  = size.metaType().id();
            const bool      integer   = sizeType == QMetaType::Int || sizeType == QMetaType::UInt
                                     || sizeType == QMetaType::LongLong || sizeType == QMetaType::ULongLong;
            if (!integer || !converted || number < 0 || number > std::numeric_limits<int>::max()
                || childList.at(index).metaType().id() != QMetaType::QVariantMap)
            {
                validationError = tr("Terminal splitter has an invalid size or child.");
                return false;
            }
            if (!validate(childList.at(index).toMap(), depth + 1))
            {
                return false;
            }
        }
        return true;
    };
    if (!validate(layout, 1))
    {
        return fail(validationError);
    }

    QWidget* oldRoot  = _root;
    QWidget* restored = _restoreNode(layout);
    _layout->removeWidget(oldRoot);
    oldRoot->hide();
    oldRoot->setParent(this);
    oldRoot->deleteLater();
    _setRoot(restored);
    for (auto* group : groups())
    {
        emit groupAdded(group);
    }
    return true;
}

void TerminalWorkspace::clear()
{
    QWidget* oldRoot = _root;
    _layout->removeWidget(oldRoot);
    oldRoot->hide();
    oldRoot->setParent(this);
    oldRoot->deleteLater();
    auto* group = _createGroup();
    _setRoot(group);
    emit groupAdded(group);
}

bool TerminalWorkspace::appendWorkspace(TerminalWorkspace& source, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (&source == this)
    {
        return true;
    }
    const auto                         existing     = groups();
    const auto                         transferred  = source.groups();
    const bool                         replaceEmpty = existing.size() == 1 && existing.first()->tabs().count() == 0
                                                   && !existing.first()->property("retainedPlacement").toBool();
    const std::function<int(QWidget*)> depth        = [&](QWidget* node) {
        auto* splitter = qobject_cast<QSplitter*>(node);
        int   maximum  = 0;
        if (splitter)
        {
            for (int index = 0; index < splitter->count(); ++index)
            {
                maximum = std::max(maximum, 1 + depth(splitter->widget(index)));
            }
        }
        return maximum;
    };
    if (existing.size() + transferred.size() - (replaceEmpty ? 1 : 0) > 256
        || (!replaceEmpty && std::max(depth(_root), depth(source._root)) >= 15))
    {
        if (error)
        {
            *error = tr("Combined terminal layout exceeds its group or depth limit.");
        }
        return false;
    }
    QSet<QString> ids;
    if (!replaceEmpty)
    {
        for (auto* group : existing)
        {
            ids.insert(group->id());
        }
    }
    for (auto* group : transferred)
    {
        if (ids.contains(group->id()))
        {
            if (error)
            {
                *error = tr("Combined terminal layout has duplicate group IDs.");
            }
            return false;
        }
        ids.insert(group->id());
    }

    QWidget* transferredRoot = source._root;
    source._layout->removeWidget(transferredRoot);
    source._root      = nullptr;
    auto* replacement = source._createGroup();
    source._setRoot(replacement);
    if (replaceEmpty)
    {
        QWidget* oldRoot = _root;
        _layout->removeWidget(oldRoot);
        oldRoot->hide();
        oldRoot->deleteLater();
        _setRoot(transferredRoot);
    }
    else
    {
        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);
        _layout->removeWidget(_root);
        splitter->addWidget(_root);
        splitter->addWidget(transferredRoot);
        splitter->setSizes({1, 1});
        _setRoot(splitter);
    }
    for (auto* group : transferred)
    {
        disconnect(group, nullptr, &source, nullptr);
        _connectGroup(group);
        emit groupAdded(group);
    }
    emit source.groupAdded(replacement);
    return true;
}

TerminalTabGroup* TerminalWorkspace::_createGroup(const QString& id)
{
    auto* group = new TerminalTabGroup(id, this);
    _connectGroup(group);
    return group;
}

void TerminalWorkspace::_connectGroup(TerminalTabGroup* group)
{
    connect(group, &TerminalTabGroup::activated, this, [this, group] { emit activated(group); });
    connect(group, &TerminalTabGroup::closeRequested, this, &TerminalWorkspace::closeRequested);
    connect(group, &TerminalTabGroup::contextMenuRequested, this, &TerminalWorkspace::contextMenuRequested);
    connect(group, &TerminalTabGroup::detachRequested, this, &TerminalWorkspace::detachRequested);
    connect(group, &TerminalTabGroup::transferRequested, this, &TerminalWorkspace::transferRequested);
}

QWidget* TerminalWorkspace::_restoreNode(const QVariantMap& node)
{
    if (node.value("type").toString() == "group")
    {
        auto* group = _createGroup(node.value("id").toString());
        group->setProperty("layoutMetadata", node);
        group->setProperty("restoredTabs", node.value("tabs").toStringList());
        group->setProperty("restoredActive", node.value("active").toString());
        group->toolbar().setVisible(node.value("toolbarVisible", true).toBool());
        group->statusBar().setVisible(node.value("statusBarVisible", true).toBool());
        return group;
    }
    auto* splitter = new QSplitter(node.value("orientation").toString() == "horizontal" ? Qt::Horizontal : Qt::Vertical,
                                   this);
    splitter->setChildrenCollapsible(false);
    splitter->setProperty("layoutMetadata", node);
    const auto children = node.value("children").toList();
    for (const auto& child : children)
    {
        splitter->addWidget(_restoreNode(child.toMap()));
    }
    QList<int> sizes;
    const auto values = node.value("sizes").toList();
    for (const auto& size : values)
    {
        sizes.append(size.toInt());
    }
    splitter->setSizes(sizes);
    return splitter;
}

QWidget* TerminalWorkspace::_prune(QWidget* node, TerminalTabGroup* retained)
{
    if (auto* group = qobject_cast<TerminalTabGroup*>(node))
    {
        if (group == retained || group->tabs().count() > 0 || group->property("retainedPlacement").toBool())
        {
            return group;
        }
        group->hide();
        group->setParent(this);
        group->deleteLater();
        return nullptr;
    }
    auto* splitter = qobject_cast<QSplitter*>(node);
    if (!splitter)
    {
        return nullptr;
    }
    QList<QWidget*> children;
    for (int index = 0; index < splitter->count(); ++index)
    {
        children.append(splitter->widget(index));
    }
    QList<QWidget*> remaining;
    for (auto* child : children)
    {
        if (auto* kept = _prune(child, retained))
        {
            if (kept != child)
            {
                splitter->insertWidget(std::min(int(remaining.size()), splitter->count()), kept);
            }
            remaining.append(kept);
        }
    }
    if (remaining.size() > 1)
    {
        return splitter;
    }
    QWidget* result = remaining.isEmpty() ? nullptr : remaining.first();
    if (result)
    {
        result->setParent(this);
    }
    splitter->hide();
    splitter->setParent(this);
    splitter->deleteLater();
    return result;
}

QVariantMap TerminalWorkspace::_saveNode(QWidget* node) const
{
    if (auto* group = qobject_cast<TerminalTabGroup*>(node))
    {
        QVariantList ids;
        auto&        tabs = group->tabs();
        for (int index = 0; index < tabs.count(); ++index)
        {
            const QString id = tabs.widget(index)->property("terminalId").toString();
            if (!id.isEmpty())
            {
                ids.append(id);
            }
        }
        auto*       current = tabs.currentWidget();
        QVariantMap result  = group->property("layoutMetadata").toMap();
        result.insert("type", "group");
        result.insert("id", group->id());
        result.insert("tabs", ids);
        result.insert("active", current ? current->property("terminalId").toString() : QString());
        result.insert("toolbarVisible", !group->toolbar().isHidden());
        result.insert("statusBarVisible", !group->statusBar().isHidden());
        return result;
    }
    auto* splitter = qobject_cast<QSplitter*>(node);
    if (!splitter)
    {
        return {};
    }
    QVariantList children;
    QVariantList sizes;
    for (int index = 0; index < splitter->count(); ++index)
    {
        children.append(_saveNode(splitter->widget(index)));
    }
    for (const int size : splitter->sizes())
    {
        sizes.append(std::max(size, 0));
    }
    QVariantMap result = splitter->property("layoutMetadata").toMap();
    result.insert("type", "split");
    result.insert("orientation", splitter->orientation() == Qt::Horizontal ? "horizontal" : "vertical");
    result.insert("sizes", sizes);
    result.insert("children", children);
    return result;
}

void TerminalWorkspace::_setRoot(QWidget* root)
{
    if (_root && _root != root)
    {
        _layout->removeWidget(_root);
    }
    _root = root;
    root->setParent(this);
    _layout->addWidget(root);
    root->show();
}
