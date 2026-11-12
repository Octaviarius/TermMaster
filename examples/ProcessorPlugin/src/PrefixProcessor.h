#pragma once

#include <QList>
#include <Termaster/Core/Processing/Processor.h>

/** Synchronous, owning-thread example. No transport, timer or worker is involved. */
class PrefixProcessor final : public Processor
{
public:
    PrefixProcessor(QVariantMap configuration, Limits limits);

    QVariantMap  configuration() const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    QVariantMap                  _configuration;
    QByteArray                   _prefix;
    Limits                       _limits;
    QList<std::function<void()>> _quiescent;
    bool                         _cancelled = false;
    bool                         _busy      = false;
};
