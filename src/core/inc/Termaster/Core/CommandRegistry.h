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

    /** Create a command; throws std::invalid_argument for an invalid ID and
     * std::logic_error for duplicate IDs or destruction in progress. */
    Command*        create(const QString&      id,
                           const QString&      text            = {},
                           const QIcon&        icon            = {},
                           const QKeySequence& defaultShortcut = {});
    Command*        find(const QString& id) const;
    QList<Command*> commands() const;
    bool            remove(const QString& id);

private:
    QMap<QString, QPointer<Command>> _commands;
    bool                             _destroying = false;
};
