#pragma once

#include <QtGlobal>
#include <Termaster/Core/Logging/Logger.h>
#include <Termaster/Core/Path.h>
#include <memory>

/** Application-owned Qt message bridge for the shared asynchronous logger.
 * Exactly one bridge may be active. Configure on the owner thread; message
 * delivery is thread-safe and never accesses Widgets. Destruction restores the
 * previous Qt handler/filter and drains the owned output worker.
 */
class Logging final
{
public:
    explicit Logging(const Path& userDirectory);
    ~Logging();
    Q_DISABLE_COPY_MOVE(Logging)

    /** Switches the whole family atomically after preparing the requested sinks.
     * A failed switch retains the old policy/directory. Passing disabled outputs
     * does not create directories. Configuration changes drain prior records.
     */
    bool    configure(const Logger::Settings& settings, const Path& directory, QString* error = nullptr);
    bool    flush(QString* error = nullptr) const;
    Path    directory() const;
    QString error() const;

private:
    struct Data;
    std::unique_ptr<Data> _data;

    static void _message(QtMsgType type, const QMessageLogContext& context, const QString& message);
};
