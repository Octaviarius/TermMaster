#pragma once

#include <QMap>
#include <QPointer>
#include <QWidget>
#include <Termaster/Core/ActionContainer.h>

/** Shared Qt projection. Only managed actions are removed from the target widget. */
class ActionContainerAdapter : public ActionContainer
{
    Q_OBJECT

public:
    ~ActionContainerAdapter() override;
    QWidget* widget() const;

protected:
    enum class WidgetOwnership
    {
        Borrowed,
        Owned
    };

    ActionContainerAdapter(QString name, QString title, QWidget* widget, WidgetOwnership ownership, QObject* owner);
    bool             _isAvailable() const final;
    bool             _acceptsAction(const QAction* action) const final;
    bool             _acceptsMenu(const ActionContainer* menu) const final;
    virtual QAction* _menuAction() const = 0;
    virtual void     _configureAction(QAction* action);

private:
    QPointer<QWidget>                _widget;
    WidgetOwnership                  _ownership;
    QMap<QString, QPointer<QAction>> _separators;
    QList<QPointer<QAction>>         _rendered;
    QMetaObject::Connection          _changedConnection;
    QMetaObject::Connection          _widgetConnection;
    bool                             _projecting  = false;
    bool                             _dirty       = false;
    bool                             _tearingDown = false;

    void                     _synchronize();
    QList<QPointer<QAction>> _desiredActions();
    void                     _clearProjection();
};
