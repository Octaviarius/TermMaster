#pragma once
#include <QByteArray>
#include <QString>
#include <Qt>

class QKeyEvent;
class TerminalScreen;

/** Stateless input encoding using the active screen modes; no transport ownership. */
class TerminalInput
{
public:
    enum class MouseEventKind
    {
        Press,
        Release,
        Move,
        WheelUp,
        WheelDown
    };
    static QByteArray key(const QKeyEvent& event, const TerminalScreen& screen, bool release = false);
    /** Rejects payloads exceeding 4 MiB in UTF-8, returning an empty array. */
    static QByteArray paste(const QString& text, const TerminalScreen& screen);
    static QByteArray focus(bool focused, const TerminalScreen& screen);
    /** Coordinates are zero based. Mode 1016 requires explicit pixel coordinates. */
    static QByteArray mouse(MouseEventKind        kind,
                            Qt::MouseButton       button,
                            Qt::MouseButtons      buttons,
                            int                   column,
                            int                   row,
                            Qt::KeyboardModifiers modifiers,
                            const TerminalScreen& screen,
                            bool                  pixelCoordinates = false);
};
