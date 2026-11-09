#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>
#include <Termaster/Core/CommandRegistry.h>

/** GUI-thread assignment owner. Registry is borrowed and may be destroyed first.
 * Values are PortableText strings: absent uses the default, empty disables.
 * Unknown command IDs are retained for later plugin registration. No file I/O.
 */
class CommandShortcuts final : public QObject
{
    Q_OBJECT
public:
    explicit CommandShortcuts(CommandRegistry* registry, QObject* parent = nullptr);

    QVariantMap assignments() const;
    /** Empty result means valid; conflicts include sequence-prefix ambiguity. */
    QString validateAssignments(const QVariantMap& assignments) const;
    bool    setAssignments(const QVariantMap& assignments, QString* error = nullptr);

    Q_SIGNAL void assignmentsChanged();
    /** A late command shortcut was disabled because it conflicts with an active command. */
    Q_SIGNAL void diagnostic(const QString& message);

private:
    QPointer<CommandRegistry> _registry;
    QVariantMap               _assignments;

    QSet<QString> _suppressed;

    void _apply(const QString& id);
    void _applyLate(const QString& id);
    void _retrySuppressed(const QString& removedId);
};
