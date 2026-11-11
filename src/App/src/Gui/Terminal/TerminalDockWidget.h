#pragma once

#include <QDockWidget>
#include <QPointer>
#include <QTimer>

class QCloseEvent;

/** Native GUI-thread docking shell. Its controller supplies and transfers the
 * terminal panel, assigns stable identity and owns persistence and commands.
 */
class TerminalDockWidget final : public QDockWidget
{
    Q_OBJECT
public:
    explicit TerminalDockWidget(const QString& title, QWidget* parent = nullptr);
    void          allowClose();
    Q_SIGNAL void returnRequested();
    Q_SIGNAL void activated();
    Q_SIGNAL void floatingReleased(const QRect& frameGeometry);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* event) override;

private:
    QTimer            _floatingReleaseTimer;
    QPointer<QWidget> _ownerWindow;
    bool              _allowClose      = false;
    bool              _releaseObserved = false;

    bool _floatingPresentation() const;
    void _updateFloatingRelease();
    void _checkFloatingRelease();
};
