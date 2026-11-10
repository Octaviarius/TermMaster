#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

/** Bundled command icons rendered using the current application palette.
 * Use on the GUI thread, like the widgets that display them.
 */
class Icons final
{
public:
    /** Returns a null icon for an unknown name. An optional tint supports accent backgrounds. */
    static QIcon icon(const QString& name, const QColor& color = {});
    /** Processing topology: a plain or partitioned body with two or four leads. */
    static QIcon processorBlock(bool composite, bool duplex);
};
