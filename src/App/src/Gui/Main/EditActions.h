#pragma once

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <array>
#include <cstddef>

class QAction;
class QMainWindow;
class QWidget;
class CommandRegistry;
class ActionContainer;

/** Routes standard editing commands to supported editors in the main window.
 * All objects are used on the GUI thread. The window and registry outlive this controller.
 */
class EditActions final : public QObject
{
public:
    explicit EditActions(QMainWindow&     window,
                         CommandRegistry& commands,
                         ActionContainer& menu,
                         QObject*         owner = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Operation
    {
        Undo,
        Redo,
        Cut,
        Copy,
        Paste,
        SelectAll,
        Count
    };

    QPointer<QMainWindow>                                                     _window;
    QPointer<QWidget>                                                         _target;
    std::array<QPointer<QAction>, static_cast<std::size_t>(Operation::Count)> _actions;
    QList<QMetaObject::Connection>                                            _targetConnections;

    QWidget* _currentEditor() const;
    void     _observe(QWidget* target);
    void     _refresh();
    void     _execute(Operation operation);
};
