#pragma once

#include <QVariantMap>
#include <atomic>
#include <memory>

class ProcessorRegistry;

/** Python source inspection and bounded isolated Codeflow execution. */
class Codeflow
{
public:
    static QString defaultPython();
    static QString hostScript();
    static QString templateSource(int channels = 2);
    /** Blocking inspection for a worker thread; never call on the GUI thread. */
    static QVariantMap inspect(const QVariantMap&                source,
                               QString*                          error     = nullptr,
                               std::shared_ptr<std::atomic_bool> cancelled = {});
    /** Isolated packet fixture. Result contains outputs [{port, data: QByteArray}] and diagnostics. */
    static QVariantMap test(const QVariantMap&                source,
                            const QVariantMap&                parameters,
                            int                               port,
                            const QByteArray&                 data,
                            QString*                          error     = nullptr,
                            std::shared_ptr<std::atomic_bool> cancelled = {});
    static QVariantMap definition(const QString&     id,
                                  const QString&     title,
                                  const QVariantMap& source,
                                  const QVariantMap& description);
    static void        registerProvider(ProcessorRegistry& registry);
};
