#include "PrefixPlugin.h"

#include "PrefixProcessor.h"

#include <QFile>
#include <QThread>
#include <Termaster/Core/Plugin/PluginHost.h>
#include <exception>

void PrefixPlugin::setHost(PluginHost* host)
{
    _host = host;
}

bool PrefixPlugin::startup(QString* error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (_registered)
    {
        return true;
    }
    _state = State::Starting;

    QFile schema(":/ProcessorTutorial/Prefix.yaml");
    if (!_host || !schema.open(QIODevice::ReadOnly))
    {
        if (error)
        {
            *error = QString::fromUtf8("Prefix example needs a host and its embedded settings schema");
        }
        _state = State::Failed;
        return false;
    }

    ProcessorCatalog::Type type;
    type.id    = "sample.prefix";
    type.title = "Prefix packet (example)";
    type.group = "Examples";
    type.editor.insert("helpTopic", "example.prefix");
    type.settingsSchema = schema.readAll();
    type.inputCount     = 1;
    type.outputCount    = 1;
    type.inputNames     = {"Packet"};
    type.outputNames    = {"Prefixed packet"};
    type.create         = [](const QVariantMap&       configuration,
                             const Processor::Limits& limits,
                             QString*                 cause) -> std::unique_ptr<Processor> {
        QVariantMap effective = configuration;
        if (!effective.contains("prefix"))
        {
            effective.insert("prefix", "[sample] ");
        }
        if (effective.value("prefix").typeId() != QMetaType::QString
            || effective.value("prefix").toString().size() > 256)
        {
            if (cause)
            {
                *cause = QString::fromUtf8("Prefix must be a string of at most 256 UTF-16 code units");
            }
            return {};
        }
        if (limits.maxChunks < 1 || limits.maxBytes < effective.value("prefix").toString().toUtf8().size())
        {
            if (cause)
            {
                *cause = QString::fromUtf8("Processor limits cannot hold one prefix packet");
            }
            return {};
        }
        return std::make_unique<PrefixProcessor>(std::move(effective), limits);
    };

    try
    {
        _host->registerProcessorType(std::move(type));
        _registered = true;
        _state      = State::Running;
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString::fromUtf8(exception.what());
        }
        _state = State::Failed;
        return false;
    }
}

bool PrefixPlugin::shutdown(QString* error)
{
    Q_ASSERT(QThread::currentThread() == thread());
    _state = State::Stopping;
    if (_registered && (!_host || !_host->unregisterProcessorType("sample.prefix", error)))
    {
        // Retain registration and the host when prepared factories or instances pin code.
        _state = State::Failed;
        return false;
    }

    _registered = false;
    _state      = State::Stopped;
    return true;
}

Plugin::Info PrefixPlugin::info() const
{
    return {"ProcessorTutorialPlugin", "Prefix processor tutorial", "1.0.0", {}};
}

Plugin::State PrefixPlugin::state() const
{
    return _state;
}
