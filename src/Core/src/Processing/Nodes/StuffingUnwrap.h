#pragma once

#include "StuffingSettings.h"

#include <QByteArrayView>
#include <Termaster/Core/Processing/Processor.h>
#include <memory>

/** Strict packet decoding with optional bounded delimiter stream framing. */
class StuffingUnwrap final : public Processor
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);

    QVariantMap  configuration() const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    QVariantMap      _configuration;
    Limits           _limits;
    StuffingSettings _settings;
    QByteArray       _buffer;
    bool             _framing;
    bool             _awaitOpening;
    bool             _busy    = false;
    bool             _stopped = false;

    StuffingUnwrap(QVariantMap configuration, Limits limits);
    static bool _fail(QString* error, const QString& message);
    static bool _charge(qsizetype count, qsizetype& remaining, QString& error);
    bool        _decode(QByteArrayView body,
                        QList<Chunk>&  outputs,
                        qsizetype&     outputBytes,
                        qsizetype&     remaining,
                        QString&       error) const;
};
