#include "TerminalDockWidget.h"

#include <QCloseEvent>
#include <QEvent>
#include <QPointer>

TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent) : QDockWidget(title, parent)
{
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void TerminalDockWidget::allowClose()
{
    _allowClose = true;
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
    if (event->type() == QEvent::MouseButtonPress)
    {
        const QPointer<TerminalDockWidget> self(this);
        emit                               activated();
        if (!self)
        {
            return true;
        }
    }
    return QDockWidget::event(event);
}
