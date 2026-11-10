#include "TerminalProcessing.h"

QString TerminalProcessing::mode(const QVariantMap& configuration)
{
    return configuration.value("mode", "bypass").toString();
}

bool TerminalProcessing::validateMode(const QVariantMap& configuration, QString* error)
{
    const auto value = configuration.value("mode", QStringLiteral("bypass"));
    if (value.typeId() != QMetaType::QString || !QStringList {"bypass", "simplex", "duplex"}.contains(value.toString()))
    {
        if (error)
        {
            *error = QStringLiteral("Processing mode must be Bypass, Simplex or Duplex");
        }
        return false;
    }
    return true;
}

QVariantMap TerminalProcessing::active(const QVariantMap& configuration)
{
    QVariantMap result;
    const auto  selected = mode(configuration);
    if (selected == "simplex")
    {
        for (const auto& key : {QStringLiteral("rx"), QStringLiteral("tx")})
        {
            if (configuration.contains(key))
            {
                result.insert(key, configuration.value(key));
            }
        }
    }
    else if (selected == "duplex")
    {
        result.insert("duplex", configuration.value("duplex", QVariantMap {}));
    }
    return result;
}

QString TerminalProcessing::rootId(const QString& direction)
{
    return "terminal.inplace." + direction;
}

bool TerminalProcessing::sameActive(const QVariantMap& left, const QVariantMap& right)
{
    const auto selections = active(left);
    if (selections != active(right))
    {
        return false;
    }
    for (auto it = selections.cbegin(); it != selections.cend(); ++it)
    {
        if (it.value().toMap().value("type") == "@inplace" && diagram(left, it.key()) != diagram(right, it.key()))
        {
            return false;
        }
    }
    return true;
}

QVariantMap TerminalProcessing::diagram(const QVariantMap& configuration, const QString& direction)
{
    const auto diagrams = configuration.value("inplace").toMap();
    if (diagrams.contains(direction))
    {
        return diagrams.value(direction).toMap();
    }
    const auto        id = rootId(direction);
    const QVariantMap definition {
        {    "id",                                                                                    id},
        {  "name",                                                             QStringLiteral("Inplace")},
        {  "kind",                                                            QStringLiteral("lineflow")},
        {"source", QVariantMap {{"channels", direction == "duplex" ? 2 : 1}, {"nodes", QVariantList {}}}}
    };
    return {
        {id, definition}
    };
}
