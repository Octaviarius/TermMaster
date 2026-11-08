#pragma once

#include <Termaster/Core/Command.h>

/** GUI-thread command owner with stable case-sensitive IDs. Returned commands are
 * borrowed until removal or registry destruction. Callbacks must use deleteLater()
 * when destroying their emitting QObject.
 */
class CommandRegistry final : public QObject
{
    Q_OBJECT
public:
    explicit CommandRegistry(QObject* parent = nullptr);
    ~CommandRegistry() override;

    Command*        create(const QString&      id,
                           const QString&      text            = {},
                           const QIcon&        icon            = {},
                           const QKeySequence& defaultShortcut = {},
                           QString*            error           = nullptr);
    Command*        find(const QString& id) const;
    QList<Command*> commands() const;
    bool            remove(const QString& id);

private:
    QMap<QString, QPointer<Command>> _commands;
    bool                             _destroying = false;
};
