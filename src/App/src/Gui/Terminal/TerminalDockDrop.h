#pragma once

#include <QPointer>
#include <QRect>

class MainWindow;
class QDockWidget;
class QRubberBand;
class QLabel;
class QWidget;

/** GUI-thread drop geometry and native, input-transparent preview. No domain
 * state is transferred here. MainWindow outlives this presentation helper.
 */
class TerminalDockDrop final
{
public:
    enum class Placement
    {
        None,
        Tabs,
        Left,
        Right,
        Above,
        Below,
        Tabify
    };
    struct Target
    {
        Placement             placement = Placement::None;
        QPointer<QDockWidget> dock;
        QRect                 rectangle;
        int                   tabIndex = -1;
    };

    explicit TerminalDockDrop(MainWindow& window);
    ~TerminalDockDrop();
    Q_DISABLE_COPY_MOVE(TerminalDockDrop)

    Target targetAt(const QPoint& globalPosition) const;
    bool   mainUnderCursor(QWidget* source) const;
    void   show(const Target& target);
    void   hide();

private:
    MainWindow&           _window;
    QPointer<QRubberBand> _preview;
    QPointer<QLabel>      _caption;

    static Target _edgeTarget(const QRect& rectangle, const QPoint& point, int band, QDockWidget* dock);
};
