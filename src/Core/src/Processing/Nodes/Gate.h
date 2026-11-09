#pragma once

#include <Termaster/Core/Processing/Processor.h>
#include <memory>

/** Synchronous byte gate. Disabled input completes successfully with no output. */
class Gate final : public Processor
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);
    QVariantMap                       configuration() const override;
    AcceptResult                      accept(Chunk input, Callbacks callbacks) override;
    void                              cancel(std::function<void()> quiescent) override;

private:
    QVariantMap _configuration;
    Limits      _limits;
    bool        _enabled = true;
    bool        _busy    = false;
    bool        _stopped = false;

    Gate(QVariantMap configuration, Limits limits, bool enabled);
};
