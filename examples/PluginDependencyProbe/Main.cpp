#include "ProbeInterface.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QTextStream>
#include <QtCore/qt_windows.h>

int main(int argc, char** argv)
{
    QCoreApplication   app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"plugin-dir", "Directory containing the two plugins", "path"});
    parser.addOption({"provider-first", "Instantiate the provider before loading the consumer"});
    parser.process(app);

    const QDir    directory(parser.isSet("plugin-dir") ? parser.value("plugin-dir") :
                                                         QCoreApplication::applicationDirPath() + "/plugins");
    const QString providerPath = directory.absoluteFilePath("Provider.dll");
    QPluginLoader provider(providerPath);
    QPluginLoader consumer(directory.absoluteFilePath("Consumer.dll"));

    auto        providerMapped = []() { return GetModuleHandleW(L"Provider.dll") != nullptr; };
    QJsonObject result;
    result["qt"]                              = qVersion();
    result["provider_mapped_before_metadata"] = providerMapped();
    result["provider_metadata"]               = provider.metaData().value("MetaData");
    result["consumer_metadata"]               = consumer.metaData().value("MetaData");
    result["provider_mapped_after_metadata"]  = providerMapped();
    result["consumer_loaded_after_metadata"]  = consumer.isLoaded();
    result["provider_first"]                  = parser.isSet("provider-first");

    auto finish = [&result](int exitCode) {
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        return exitCode;
    };

    if (parser.isSet("provider-first"))
    {
        const auto* api = qobject_cast<IProbePlugin*>(provider.instance());
        if (!api)
        {
            result["error"] = provider.errorString();
            return finish(1);
        }
        result["provider_result"] = api->calculate();
    }

    const auto* api                          = qobject_cast<IProbePlugin*>(consumer.instance());
    result["consumer_loaded"]                = consumer.isLoaded();
    result["provider_mapped_after_consumer"] = providerMapped();
    if (!api)
    {
        result["error"] = consumer.errorString();
        return finish(1);
    }

    result["consumer_id"]     = api->id();
    result["consumer_result"] = api->calculate();
    return finish(api->calculate() == 84 ? 0 : 2);
}
