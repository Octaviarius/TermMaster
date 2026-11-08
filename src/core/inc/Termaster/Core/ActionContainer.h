#pragma once

#include <QAction>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <optional>

/** Ordered action/menu hierarchy, independent of concrete widgets.
 * Actions are borrowed. Attached menus are owned and must be heap allocated.
 * Use on the QObject owner thread; observers must defer deleting the sender.
 */
class ActionContainer : public QObject
{
    Q_OBJECT

public:
    enum class Kind
    {
        Action,
        Menu
    };

    struct Item
    {
        Kind                      kind = Kind::Action;
        QPointer<QAction>         action;
        QPointer<ActionContainer> menu;
    };

    using List = QList<ActionContainer*>;

    explicit ActionContainer(QString name, QString title = {}, QObject* owner = nullptr);
    ~ActionContainer() override;
    Q_DISABLE_COPY_MOVE(ActionContainer)

    QString          name() const;
    QString          title() const;
    void             setTitle(const QString& title);
    ActionContainer* parentContainer() const;
    ActionContainer* root() const;
    QString          path() const;
    ActionContainer* findMenu(const QString& path) const;
    bool             isAvailable() const;

    QStringList groups() const;
    qsizetype   groupIndex(const QString& name) const;
    bool        addGroup(const QString& name);
    bool        insertGroup(const QString& name, qsizetype index);
    bool        insertGroupBefore(const QString& name, const QString& reference);
    bool        insertGroupAfter(const QString& name, const QString& reference);
    bool        moveGroup(const QString& name, qsizetype index);
    bool        moveGroupBefore(const QString& name, const QString& reference);
    bool        moveGroupAfter(const QString& name, const QString& reference);
    bool        renameGroup(const QString& name, const QString& newName);
    bool        clearGroup(const QString& name);
    bool        removeGroup(const QString& name);
    bool        clear();

    qsizetype       itemCount(const QString& group) const;
    QList<Item>     items(const QString& group) const;
    QList<QAction*> actions(const QString& group) const;
    List            menus(const QString& group) const;

    bool addAction(QAction* action, const QString& group);
    bool insertAction(QAction* action, const QString& group, qsizetype index);
    bool moveAction(QAction* action, const QString& group, qsizetype index);
    bool removeAction(QAction* action);

    /** Successful insertion transfers QObject ownership of the entire subtree. */
    bool addMenu(ActionContainer* menu, const QString& group);
    bool insertMenu(ActionContainer* menu, const QString& group, qsizetype index);
    bool moveMenu(ActionContainer* menu, const QString& group, qsizetype index);
    bool transferMenu(ActionContainer* menu, ActionContainer& target, const QString& group, qsizetype index);
    /** Detach without destruction; ownership passes to the caller. */
    ActionContainer* takeMenu(ActionContainer* menu);
    /** Remove and destroy the owned subtree. Borrowed actions survive. */
    bool removeMenu(ActionContainer* menu);

    Q_SIGNAL void changed();
    Q_SIGNAL void titleChanged(const QString& title);

protected:
    virtual bool _isAvailable() const                            = 0;
    virtual bool _acceptsAction(const QAction* action) const     = 0;
    virtual bool _acceptsMenu(const ActionContainer* menu) const = 0;

private:
    struct Record
    {
        Item                    item;
        quint64                 id = 0;
        QMetaObject::Connection destroyedConnection;
    };

    struct Group
    {
        QString       name;
        QList<Record> items;
    };

    struct Location
    {
        qsizetype group;
        qsizetype item;
    };

    QString                   _name;
    QString                   _title;
    QPointer<ActionContainer> _parentContainer;
    QList<Group>              _groups;
    quint64                   _nextId     = 1;
    bool                      _destroying = false;

    bool                    _canMutate() const;
    bool                    _validMenu(const ActionContainer* menu) const;
    std::optional<Location> _actionLocation(const QAction* action) const;
    std::optional<Location> _menuLocation(const ActionContainer* menu) const;
    bool                    _moveItem(Location from, const QString& group, qsizetype index);
    void                    _actionDestroyed(quint64 id);
    void                    _forgetMenu(ActionContainer* menu);
    void                    _release(QList<Record>& records);
    void                    _notify();
    static void             _deleteMenus(const QList<Record>& records);
};
