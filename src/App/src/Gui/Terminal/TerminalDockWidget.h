#pragma once

#include <QDockWidget>

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

protected:
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* event) override;

private:
    bool _allowClose = false;
};
