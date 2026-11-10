#pragma once

#include <QPointer>
#include <Termaster/Core/Processing/Processor.h>
#include <array>
#include <memory>

/** Two independent synchronous codec streams. Port zero decodes, port one encodes.
 * Owns unary codec instances on the same QObject thread; no workers or idle output.
 */
class DuplexCodecNode : public Processor
{
public:
    QVariantMap  configuration() const final;
    int          inputCount() const final;
    int          outputCount() const final;
    QString      inputName(int port) const final;
    QString      outputName(int port) const final;
    AcceptResult accept(Chunk input, Callbacks callbacks) final;
    void         cancel(std::function<void()> quiescent) final;

protected:
    DuplexCodecNode(QVariantMap                configuration,
                    QString                    encoding,
                    std::unique_ptr<Processor> decoder,
                    std::unique_ptr<Processor> encoder);
    static QVariantMap _directionConfiguration(const QVariantMap& configuration, const QString& prefix);

private:
    QVariantMap                               _configuration;
    QString                                   _encoding;
    std::array<std::unique_ptr<Processor>, 2> _codecs;
    bool                                      _stopped = false;
    QString                                   _failure;
    void                                      _stop();
};
