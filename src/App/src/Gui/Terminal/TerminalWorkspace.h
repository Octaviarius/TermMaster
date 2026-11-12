#pragma once

#include <QList>
#include <QVariantMap>
#include <QWidget>

class QVBoxLayout;
class TerminalTabGroup;

/** Native splitter tree of presentation groups, with no terminal-domain ownership. */
class TerminalWorkspace final : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWorkspace(QWidget* parent = nullptr);

    TerminalTabGroup*        initialGroup() const;
    QList<TerminalTabGroup*> groups() const;
    TerminalTabGroup*        groupFor(QWidget* page) const;
    TerminalTabGroup*        findGroup(const QString& id) const;
    TerminalTabGroup*        splitGroup(TerminalTabGroup* group, Qt::Edge edge);
    void                     removeEmptyGroups();
    QVariantMap              saveLayout() const;
    bool                     restoreLayout(const QVariantMap& layout, QString* error = nullptr);
    void                     clear();
    bool                     appendWorkspace(TerminalWorkspace& source, QString* error = nullptr);

    Q_SIGNAL void groupAdded(TerminalTabGroup* group);
    Q_SIGNAL void activated(TerminalTabGroup* group);
    Q_SIGNAL void transferRequested(const QString& id, TerminalTabGroup* target, Qt::Edge edge, int index);
    Q_SIGNAL void detachRequested(const QString& id, const QPoint& globalPosition);
    Q_SIGNAL void closeRequested(const QString& id);
    Q_SIGNAL void contextMenuRequested(const QString& id, const QPoint& globalPosition);

private:
    QVBoxLayout* _layout = nullptr;
    QWidget*     _root   = nullptr;

    TerminalTabGroup* _createGroup(const QString& id = {});
    void              _connectGroup(TerminalTabGroup* group);
    QWidget*          _restoreNode(const QVariantMap& node);
    QWidget*          _prune(QWidget* node, TerminalTabGroup* retained);
    QVariantMap       _saveNode(QWidget* node) const;
    void              _setRoot(QWidget* root);
};
