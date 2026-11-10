#pragma once

#include <QPointer>
#include <Termaster/Core/Processing/Processor.h>
#include <array>
#include <memory>

/** Independent inverse/direct processing endpoints. Port zero receives wire data;
 * port one sends payload data. Owns both children on its QObject thread and waits
 * for both child cancellation acknowledgements before reporting quiescence.
 */
class Codec final : public Processor
{
public:
    enum class Mode
    {
        Direct,
        Inverse,
        Duplex
    };
    /** Immutable family-owned descriptors and factories; assembled before form creation. */
    class Definition final
    {
    public:
        using Factory = std::function<
            std::unique_ptr<Processor>(const QVariantMap&, const Processor::Limits&, QString*)>;
        Definition(QByteArray commonSchema,
                   QByteArray directProperties,
                   QByteArray inverseProperties,
                   Factory    direct,
                   Factory    inverse,
                   QString    wireName,
                   QString    payloadName,
                   QString    directPrefix  = "encode",
                   QString    inversePrefix = "decode");
        QByteArray                 schema(Mode mode) const;
        std::unique_ptr<Processor> create(Mode                     mode,
                                          const QVariantMap&       configuration,
                                          const Processor::Limits& limits,
                                          QString*                 error = nullptr) const;

    private:
        QByteArray _commonSchema;
        QByteArray _directProperties;
        QByteArray _inverseProperties;
        Factory    _direct;
        Factory    _inverse;
        QString    _wireName;
        QString    _payloadName;
        QString    _directPrefix;
        QString    _inversePrefix;
    };
    Codec(QVariantMap                configuration,
          QString                    wireName,
          QString                    payloadName,
          std::unique_ptr<Processor> inverse,
          std::unique_ptr<Processor> direct);
    QVariantMap  configuration() const override;
    int          inputCount() const override;
    int          outputCount() const override;
    QString      inputName(int port) const override;
    QString      outputName(int port) const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    QVariantMap                               _configuration;
    QString                                   _wireName;
    QString                                   _payloadName;
    std::array<std::unique_ptr<Processor>, 2> _codecs;
    bool                                      _stopped = false;
    QString                                   _failure;
    int                                       _pendingCancellation = -1;
    QList<std::function<void()>>              _quiescent;
    void                                      _cancelled();
};
