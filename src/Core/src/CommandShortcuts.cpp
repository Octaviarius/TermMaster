#include <QRegularExpression>
#include <Termaster/Core/CommandShortcuts.h>

CommandShortcuts::CommandShortcuts(CommandRegistry* registry, QObject* parent) : QObject(parent), _registry(registry)
{
    if (_registry)
    {
        connect(_registry, &CommandRegistry::commandAdded, this, &CommandShortcuts::_applyLate);
        connect(_registry, &CommandRegistry::commandRemoved, this, &CommandShortcuts::_retrySuppressed);
    }
}

QVariantMap CommandShortcuts::assignments() const
{
    return _assignments;
}

QString CommandShortcuts::validateAssignments(const QVariantMap& assignments) const
{
    static const QRegularExpression validId(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\\z"));
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it)
    {
        if (!validId.match(it.key()).hasMatch())
        {
            return tr("Invalid command ID '%1'.").arg(it.key());
        }
        if (it.value().metaType().id() != QMetaType::QString)
        {
            return tr("Shortcut for '%1' must be a PortableText string.").arg(it.key());
        }
        const auto text     = it.value().toString();
        const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (!text.isEmpty() && (sequence.isEmpty() || sequence.toString(QKeySequence::PortableText) != text))
        {
            return tr("Shortcut '%1' for '%2' is invalid PortableText.").arg(text, it.key());
        }
        for (int index = 0; index < sequence.count(); ++index)
        {
            if (sequence[index].key() == Qt::Key_unknown || sequence[index].key() == 0)
            {
                return tr("Shortcut '%1' for '%2' has an invalid key.").arg(text, it.key());
            }
        }
    }

    QMap<QString, QKeySequence> effective;
    if (_registry)
    {
        for (auto* command : _registry->commands())
        {
            const auto id = command->name();
            effective.insert(id,
                             assignments.contains(id) ? QKeySequence::fromString(assignments.value(id).toString(),
                                                                                 QKeySequence::PortableText) :
                                                        command->defaultShortcut());
        }
    }
    for (auto first = effective.cbegin(); first != effective.cend(); ++first)
    {
        if (first.value().isEmpty())
        {
            continue;
        }
        auto second = first;
        for (++second; second != effective.cend(); ++second)
        {
            if (!second.value().isEmpty()
                && (first.value().matches(second.value()) != QKeySequence::NoMatch
                    || second.value().matches(first.value()) != QKeySequence::NoMatch))
            {
                return tr("Shortcut conflict between '%1' (%2) and '%3' (%4).")
                    .arg(first.key(),
                         first.value().toString(QKeySequence::PortableText),
                         second.key(),
                         second.value().toString(QKeySequence::PortableText));
            }
        }
    }
    return {};
}

bool CommandShortcuts::setAssignments(const QVariantMap& assignments, QString* error)
{
    const auto validation = validateAssignments(assignments);
    if (error)
    {
        *error = validation;
    }
    if (!validation.isEmpty())
    {
        return false;
    }

    _assignments = assignments;
    _suppressed.clear();
    if (_registry)
    {
        QStringList ids;
        for (auto* command : _registry->commands())
        {
            ids.append(command->name());
        }
        for (const auto& id : ids)
        {
            _apply(id);
        }
    }
    emit assignmentsChanged();
    return true;
}

void CommandShortcuts::_apply(const QString& id)
{
    auto* command = _registry ? _registry->find(id) : nullptr;
    if (command)
    {
        command->setShortcut(_assignments.contains(id) ? QKeySequence::fromString(_assignments.value(id).toString(),
                                                                                  QKeySequence::PortableText) :
                                                         command->defaultShortcut());
    }
}

void CommandShortcuts::_applyLate(const QString& id)
{
    QPointer<Command> command = _registry ? _registry->find(id) : nullptr;
    if (!command)
    {
        return;
    }
    const auto desired = _assignments.contains(id) ?
                             QKeySequence::fromString(_assignments.value(id).toString(), QKeySequence::PortableText) :
                             command->defaultShortcut();
    QString    conflictId;
    if (!desired.isEmpty())
    {
        for (auto* other : _registry->commands())
        {
            const auto active = other->shortcut();
            if (other != command && !active.isEmpty()
                && (desired.matches(active) != QKeySequence::NoMatch
                    || active.matches(desired) != QKeySequence::NoMatch))
            {
                conflictId = other->name();
                break;
            }
        }
    }
    if (!conflictId.isEmpty())
    {
        const bool newlySuppressed = !_suppressed.contains(id);
        _suppressed.insert(id);
        command->setShortcut({});
        if (newlySuppressed)
        {
            emit diagnostic(tr("Shortcut '%1' for '%2' was disabled because it conflicts with '%3'.")
                                .arg(desired.toString(QKeySequence::PortableText), id, conflictId));
        }
        return;
    }
    _suppressed.remove(id);
    command->setShortcut(desired);
}

void CommandShortcuts::_retrySuppressed(const QString& removedId)
{
    _suppressed.remove(removedId);
    auto ids = _suppressed.values();
    ids.sort();
    for (const auto& id : ids)
    {
        _applyLate(id);
    }
}
