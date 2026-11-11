#include "TerminalDockWidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QPointer>

TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent) :
    QDockWidget(title, parent), _ownerWindow(parent ? parent->window() : nullptr)
{
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setContextMenuPolicy(Qt::CustomContextMenu);

    _floatingReleaseTimer.setSingleShot(true);
    _floatingReleaseTimer.setInterval(80);
    _floatingReleaseTimer.setTimerType(Qt::PreciseTimer);
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool) { _updateFloatingRelease(); });

    const QPointer<TerminalDockWidget> self(this);
    connect(&_floatingReleaseTimer, &QTimer::timeout, this, [self] {
        if (self)
        {
            self->_checkFloatingRelease();
        }
    });
}

void TerminalDockWidget::allowClose()
{
    _allowClose = true;
    _floatingReleaseTimer.stop();
    _releaseObserved = false;
}

void TerminalDockWidget::closeEvent(QCloseEvent* event)
{
    if (_allowClose)
    {
        QDockWidget::closeEvent(event);
        return;
    }
    event->ignore();
    emit returnRequested();
}

bool TerminalDockWidget::event(QEvent* event)
{
    const QPointer<TerminalDockWidget> self(this);
    const auto                         type = event->type();
    if (type == QEvent::MouseButtonPress)
    {
        emit activated();
        if (!self)
        {
            return true;
        }
    }

    const bool handled = QDockWidget::event(event);
    if (self && (type == QEvent::ParentChange || type == QEvent::Show))
    {
        _updateFloatingRelease();
    }
    return handled;
}

bool TerminalDockWidget::_floatingPresentation() const
{
    // Native Qt tab tear-off may put this dock inside a separate top-level
    // window while the individual dock's isFloating() still reports false.
    return isFloating() || window() != _ownerWindow.data();
}

void TerminalDockWidget::_updateFloatingRelease()
{
    if (_allowClose || !_floatingPresentation())
    {
        _floatingReleaseTimer.stop();
        _releaseObserved = false;
    }
    else if (!_floatingReleaseTimer.isActive() && !_releaseObserved)
    {
        _floatingReleaseTimer.start();
    }
}

void TerminalDockWidget::_checkFloatingRelease()
{
    if (_allowClose || !_floatingPresentation())
    {
        _floatingReleaseTimer.stop();
        _releaseObserved = false;
        return;
    }
    if (QApplication::mouseButtons() != Qt::NoButton)
    {
        _releaseObserved = false;
        _floatingReleaseTimer.start();
        return;
    }
    if (!_releaseObserved)
    {
        // Native tab plugging is asynchronous; a second quiet sample gives Qt
        // time to finish placing the dock before the owner changes its shell.
        _releaseObserved = true;
        _floatingReleaseTimer.start();
        return;
    }

    _floatingReleaseTimer.stop();
    emit floatingReleased(window()->frameGeometry());
}
