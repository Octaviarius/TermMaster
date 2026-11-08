#include <QObject>
#include <Termaster/Core/plugin/Plugin.h>
#include <Termaster/PluginFixtureProvider/Api.h>

class ExternalConsumer final : public QObject, public Plugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.octaviarius.Termaster.Plugin/1.0" FILE "ExternalConsumer.json")
    Q_INTERFACES(Plugin)

private:
    State _state = State::Stopped;

public:
    bool startup(QString*) override
    {
        _state = termasterFixtureValue() == 42 ? State::Running : State::Failed;
        return _state == State::Running;
    }

    bool shutdown(QString*) override
    {
        _state = State::Stopped;
        return true;
    }

    Info info() const override
    {
        return {"externalConsumer", "External consumer", "1.0.0", {"provider"}};
    }

    State state() const override
    {
        return _state;
    }
};

#include "ExternalConsumer.moc"
