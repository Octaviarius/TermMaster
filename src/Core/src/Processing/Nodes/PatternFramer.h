#pragma once

#include <Termaster/Core/Processing/Processor.h>
#include <memory>

/** Bounded binary PCRE2 stream framing. Calls and callbacks use the owning thread. */
class PatternFramer final : public Processor
{
public:
    static QByteArray                 schema();
    static std::unique_ptr<Processor> create(const QVariantMap& configuration,
                                             const Limits&      limits,
                                             QString*           error = nullptr);
    ~PatternFramer() override;
    Q_DISABLE_COPY_MOVE(PatternFramer)

    QVariantMap  configuration() const override;
    QString      inputName(int port) const override;
    QString      outputName(int port) const override;
    AcceptResult accept(Chunk input, Callbacks callbacks) override;
    void         cancel(std::function<void()> quiescent) override;

private:
    struct Data;
    QVariantMap           _configuration;
    Limits                _limits;
    std::unique_ptr<Data> _data;
    QByteArray            _buffer;
    qsizetype             _searchOffset = 0;
    bool                  _busy         = false;
    bool                  _stopped      = false;

    PatternFramer(QVariantMap configuration, Limits limits, std::unique_ptr<Data> data);
};
