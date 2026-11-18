#include "TerminalProcessing.h"

QString TerminalProcessing::mode(const QVariantMap& configuration)
{
    return configuration.value("mode", "bypass").toString();
}

bool TerminalProcessing::validateMode(const QVariantMap& configuration, QString* error)
{
    const auto value = configuration.value("mode", QStringLiteral("bypass"));
    if (value.typeId() != QMetaType::QString || !QStringList {"bypass", "lineflow"}.contains(value.toString()))
    {
        if (error)
        {
            *error = QStringLiteral("Processing mode must be Bypass or Lineflow");
        }
        return false;
    }
    return true;
}

QVariantMap TerminalProcessing::active(const QVariantMap& configuration)
{
    QVariantMap result;
    const auto  selected = mode(configuration);
    if (selected == "lineflow")
    {
        result.insert("lineflow", configuration.value("lineflow", QVariantMap {}));
    }
    return result;
}

QString TerminalProcessing::rootId()
{
    return QStringLiteral("terminal.inplace.lineflow");
}

bool TerminalProcessing::sameActive(const QVariantMap& left, const QVariantMap& right)
{
    const auto selections = active(left);
    if (selections != active(right))
    {
        return false;
    }
    return selections.value("lineflow").toMap().value("type") != "@inplace" || diagram(left) == diagram(right);
}

QVariantMap TerminalProcessing::diagram(const QVariantMap& configuration)
{
    const auto diagrams = configuration.value("inplace").toMap();
    if (diagrams.contains("lineflow"))
    {
        return diagrams.value("lineflow").toMap();
    }
    const auto        id = rootId();
    const QVariantMap definition {
        {    "id",                                                        id},
        {  "name",                                 QStringLiteral("Inplace")},
        {  "kind",                                QStringLiteral("lineflow")},
        {"source", QVariantMap {{"channels", 2}, {"nodes", QVariantList {}}}}
    };
    return {
        {id, definition}
    };
}
