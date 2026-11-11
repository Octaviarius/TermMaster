#include <QDir>
#include <QLoggingCategory>
#include <Termaster/Core/Logging/Logging.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace
{
std::mutex                                    routingMutex;
Logging*                                      activeLogging = nullptr;
std::atomic<bool>                             debugEnabled {false};
std::atomic<bool>                             traceEnabled {false};
std::atomic<QLoggingCategory::CategoryFilter> previousFilter {nullptr};

void categoryFilter(QLoggingCategory* category)
{
    if (const auto filter = previousFilter.load())
    {
        filter(category);
    }
    const QByteArray name(category->categoryName());
    if (name.startsWith("termaster."))
    {
        category->setEnabled(QtDebugMsg, name.endsWith(".trace") ? traceEnabled.load() : debugEnabled.load());
    }
}

bool sameSettings(const Logger::Settings& a, const Logger::Settings& b)
{
    return a.minimumLevel == b.minimumLevel && a.tagLevels == b.tagLevels && a.console == b.console && a.file == b.file
        && a.gui == b.gui && a.historyLimit == b.historyLimit && a.maximumFileBytes == b.maximumFileBytes
        && a.retainedFiles == b.retainedFiles;
}

std::filesystem::path nativePath(const Path& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toString().toStdWString());
#else
    return std::filesystem::path(path.toString().toStdString());
#endif
}

void updateCategories(const Logger::Settings& settings)
{
    const bool enabled = settings.file || settings.console || settings.gui;
    bool       debug   = settings.minimumLevel <= Logger::Level::debug;
    bool       trace   = settings.minimumLevel == Logger::Level::trace;
    for (const auto& [tag, level] : settings.tagLevels)
    {
        debug = debug || level <= Logger::Level::debug;
        trace = trace || level == Logger::Level::trace;
    }
    debugEnabled.store(enabled && debug);
    traceEnabled.store(enabled && trace);
    QLoggingCategory::installFilter(categoryFilter);
}
} // namespace

struct Logging::Data
{
    Path                    directory;
    Logger::Settings        settings;
    std::shared_ptr<Logger> family;
    QtMessageHandler        previousHandler = nullptr;

    explicit Data(const Path& path) :
        directory(path.absolute()), family(std::make_shared<Logger>("termaster", nativePath(directory)))
    {
        settings.gui = false;
        family->configure(settings);
    }
};

Logging::Logging(const Path& userDirectory) : _data(std::make_unique<Data>(userDirectory))
{
    {
        const std::lock_guard lock(routingMutex);
        if (activeLogging)
        {
            throw std::logic_error("Only one application logging bridge may be active");
        }
        activeLogging          = this;
        _data->previousHandler = qInstallMessageHandler(_message);
    }
    previousFilter.store(QLoggingCategory::installFilter(nullptr));
    updateCategories(_data->settings);
}

Logging::~Logging()
{
    {
        const std::lock_guard lock(routingMutex);
        if (activeLogging == this)
        {
            qInstallMessageHandler(_data->previousHandler);
            activeLogging = nullptr;
        }
    }
    QLoggingCategory::installFilter(previousFilter.exchange(nullptr));
    _data->family->flush();
}

bool Logging::configure(const Logger::Settings& settings, const Path& directory, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto destination = directory.absolute();
    {
        const std::lock_guard lock(routingMutex);
        if (_data->directory == destination && sameSettings(_data->settings, settings))
        {
            const auto failure = _data->family->error();
            if (error)
            {
                *error = QString::fromStdString(failure);
            }
            if (failure.empty())
            {
                return true;
            }
        }
    }

    if (settings.file && !QDir().mkpath(destination.toString()))
    {
        if (error)
        {
            *error = QStringLiteral("Cannot create log directory: %1").arg(destination.toString());
        }
        return false;
    }
    std::shared_ptr<Logger> current;
    {
        const std::lock_guard lock(routingMutex);
        if (_data->directory == destination)
        {
            current = _data->family;
        }
    }
    if (current)
    {
        const auto failure = current->configure(settings);
        if (!failure.empty())
        {
            if (error)
            {
                *error = QString::fromStdString(failure);
            }
            return false;
        }
        {
            const std::lock_guard lock(routingMutex);
            _data->settings = settings;
        }
        updateCategories(settings);
        if (error)
        {
            error->clear();
        }
        return true;
    }

    auto       candidate = std::make_shared<Logger>("termaster", nativePath(destination));
    const auto failure   = candidate->configure(settings);
    if (!failure.empty())
    {
        if (error)
        {
            *error = QStringLiteral("%1: %2").arg(QString::fromStdString(failure), destination.toString());
        }
        return false;
    }

    std::shared_ptr<Logger> previous;
    {
        const std::lock_guard lock(routingMutex);
        previous         = std::exchange(_data->family, std::move(candidate));
        _data->directory = destination;
        _data->settings  = settings;
    }
    updateCategories(settings);
    const auto drainError = previous->flush();
    if (!drainError.empty() && error)
    {
        *error = QStringLiteral("Previous log output failed: %1").arg(QString::fromStdString(drainError));
    }
    return true;
}

bool Logging::flush(QString* error) const
{
    std::shared_ptr<Logger> family;
    {
        const std::lock_guard lock(routingMutex);
        family = _data->family;
    }
    const auto failure = family->flush();
    if (error)
    {
        *error = QString::fromStdString(failure);
    }
    return failure.empty();
}

Path Logging::directory() const
{
    const std::lock_guard lock(routingMutex);
    return _data->directory;
}

QString Logging::error() const
{
    const std::lock_guard lock(routingMutex);
    return QString::fromStdString(_data->family->error());
}

void Logging::_message(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    try
    {
        const std::lock_guard lock(routingMutex);
        if (!activeLogging)
        {
            return;
        }
        auto category = QByteArray(context.category ? context.category : "default");
        auto level    = type == QtDebugMsg    ? Logger::Level::debug :
                        type == QtInfoMsg     ? Logger::Level::info :
                        type == QtWarningMsg  ? Logger::Level::warning :
                        type == QtCriticalMsg ? Logger::Level::error :
                                                Logger::Level::critical;
        if (category.startsWith("termaster."))
        {
            category.remove(0, 10);
            if (type == QtDebugMsg && category.endsWith(".trace"))
            {
                category.chop(6);
                level = Logger::Level::trace;
            }
        }
        else if (category == "default")
        {
            category = "qt";
        }
        else if (!category.startsWith("qt."))
        {
            category.prepend("qt.");
        }
        category.replace('.', ':');
        auto& family = activeLogging->_data->family;
        family->child(category.toStdString()).write(level, message.toUtf8().toStdString());
        if (type == QtFatalMsg)
        {
            family->flush();
            // Fatal Qt diagnostics must remain visible even when all sinks are disabled.
            std::fprintf(stderr, "Termaster fatal: %s\n", message.toUtf8().constData());
            std::fflush(stderr);
        }
    }
    catch (...)
    {
        // The Qt handler must never throw or recursively log a sink failure.
        std::fputs("Termaster logging delivery failed\n", stderr);
    }
}
