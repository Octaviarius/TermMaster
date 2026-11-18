#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QLoggingCategory>
#include <QPointer>
#include <Termaster/Core/Processing/LineflowEndpoint.h>
#include <Termaster/Core/SettingsSchema.h>
#include <array>

namespace
{
Q_LOGGING_CATEGORY(recordingLog, "termaster.processing.recording")
bool fail(QString* error, const QString& text)
{
    if (error)
    {
        *error = text;
    }
    return false;
}

QVariantMap merge(QVariantMap base, const QVariantMap& extra)
{
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
    {
        if (it.value().typeId() == QMetaType::QVariantMap && base.value(it.key()).typeId() == QMetaType::QVariantMap)
        {
            base.insert(it.key(), merge(base.value(it.key()).toMap(), it.value().toMap()));
        }
        else
        {
            base.insert(it.key(), it.value());
        }
    }
    return base;
}

class Recorder final : public Processor
{
public:
    Recorder(QVariantMap configuration, int channels, Limits limits) :
        _configuration(std::move(configuration)), _channels(channels), _limits(limits)
    {
    }

    QVariantMap configuration() const override
    {
        return _configuration;
    }
    int inputCount() const override
    {
        return _channels;
    }
    int outputCount() const override
    {
        return _channels;
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        if (_stopped || input.port < 0 || input.port >= _channels || input.bytes.size() > _limits.maxBytes)
        {
            return AcceptResult::Rejected;
        }
        QString    error;
        const auto selection = _configuration.value("direction", "both").toString();
        const bool selected  = _channels == 1 || selection == "both" || (selection == "rx" && input.port == 0)
                            || (selection == "tx" && input.port == 1);
        if (_configuration.value("enabled").toBool() && selected)
        {
            if (!_record(input, &error))
            {
                _stopped = true;
                _close();
                if (callbacks.completed)
                {
                    callbacks.completed(error);
                }
                return AcceptResult::Accepted;
            }
        }

        QPointer<Recorder> guard(this);
        if (callbacks.output)
        {
            input.bytes = QByteArray(input.bytes.constData(), input.bytes.size());
            callbacks.output(std::move(input));
        }
        if (guard && !_stopped && callbacks.completed)
        {
            callbacks.completed({});
        }
        return AcceptResult::Accepted;
    }
    bool flush(QString* error)
    {
        for (auto& state : _files)
        {
            if (state.file.isOpen() && !state.file.flush())
            {
                return fail(error, "Cannot flush recording file: " + state.file.errorString());
            }
            state.pending = 0;
        }
        return true;
    }
    void cancel(std::function<void()> quiescent) override
    {
        _stopped = true;
        QString error;
        if (!flush(&error))
        {
            qCWarning(recordingLog).noquote() << error;
        }
        _close();
        setOutputHandler({});
        setErrorHandler({});
        if (quiescent)
        {
            quiescent();
        }
    }

private:
    struct File
    {
        QFile                      file;
        std::unique_ptr<QLockFile> lock;
        int                        pending = 0;
    };

    QVariantMap         _configuration;
    int                 _channels;
    Limits              _limits;
    std::array<File, 2> _files;
    quint64             _index   = 0;
    bool                _stopped = false;

    void _close()
    {
        for (auto& state : _files)
        {
            state.file.close();
            state.lock.reset();
        }
    }
    bool _open(File& state, const QString& path, QString* error)
    {
        if (state.file.isOpen())
        {
            return true;
        }
        // Resolve parent aliases before acquiring the writer lease; lock follows the
        // destination while rotations rename the data file itself.
        const QFileInfo info(path);
        const auto      parent = info.dir().canonicalPath();
        if (parent.isEmpty())
        {
            return fail(error, "Recording parent directory does not exist");
        }
        const auto destination = QFileInfo(path).exists() ? QFileInfo(path).canonicalFilePath() :
                                                            QDir(parent).filePath(info.fileName());
        state.lock             = std::make_unique<QLockFile>(destination + ".termaster-recording.lock");
        state.lock->setStaleLockTime(0);
        if (!state.lock->tryLock(0))
        {
            state.lock.reset();
            return fail(error, "Recording file is already in use or its writer lock cannot be created");
        }
        state.file.setFileName(destination);
        const auto mode = QIODevice::WriteOnly
                        | (_configuration.value("append").toBool() ? QIODevice::Append : QIODevice::Truncate);
        if (!state.file.open(mode))
        {
            const auto reason = state.file.errorString();
            state.lock.reset();
            return fail(error, "Cannot open recording file: " + reason);
        }
        return true;
    }
    bool _rotate(File& state, QString* error)
    {
        if (!state.file.flush())
        {
            return fail(error, "Cannot flush recording file: " + state.file.errorString());
        }
        const auto path = state.file.fileName();
        state.file.close();
        const int backups = _configuration.value("backups").toInt();
        if (backups == 0)
        {
            if (!state.file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                return fail(error, "Cannot truncate recording file: " + state.file.errorString());
            }
            return true;
        }
        for (int index = backups; index >= 1; --index)
        {
            const auto destination = path + '.' + QString::number(index);
            const auto source      = index == 1 ? path : path + '.' + QString::number(index - 1);
            if (QFile::exists(destination) && !QFile::remove(destination))
            {
                return fail(error, "Cannot remove recording backup");
            }
            if (QFile::exists(source) && !QFile::rename(source, destination))
            {
                return fail(error, "Cannot rotate recording file");
            }
        }
        if (!state.file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return fail(error, "Cannot create recording file after rotation: " + state.file.errorString());
        }
        state.pending = 0;
        return true;
    }
    bool _record(const Chunk& input, QString* error)
    {
        const auto direction = _channels == 1  ? QStringLiteral("stream") :
                               input.port == 0 ? QStringLiteral("rx") :
                                                 QStringLiteral("tx");
        const auto pattern   = _configuration.value("file").toString();
        const bool separate  = pattern.contains("{direction}");
        auto&      state     = _files[separate ? size_t(input.port) : 0];
        auto       path      = pattern;
        path.replace("{direction}", direction);
        if (!_open(state, path, error))
        {
            return false;
        }
        QByteArray record;
        const auto format = _configuration.value("format").toString();
        if (format == "binary")
        {
            record = input.bytes;
        }
        else if (format == "hex")
        {
            record = input.bytes.toHex(' ') + '\n';
        }
        else
        {
            const QJsonObject object {
                {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {    "index",                                 QString::number(_index + 1)},
                {"direction",                                                   direction},
                {   "length",                                  qint64(input.bytes.size())},
                {    "bytes",                 QString::fromLatin1(input.bytes.toBase64())}
            };
            record = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        }
        const auto maximum = _configuration.value("maxFileBytes").toLongLong();
        if (record.size() > maximum)
        {
            return fail(error, "Recording packet exceeds the configured file size limit");
        }
        if (state.file.size() > maximum - record.size() && !_rotate(state, error))
        {
            return false;
        }
        qsizetype offset = 0;
        while (offset < record.size())
        {
            const auto written = state.file.write(record.constData() + offset, record.size() - offset);
            if (written <= 0)
            {
                return fail(error, "Cannot write recording file: " + state.file.errorString());
            }
            offset += written;
        }
        ++_index;
        if (++state.pending >= _configuration.value("flushEvery").toInt())
        {
            state.pending = 0;
            if (!state.file.flush())
            {
                return fail(error, "Cannot flush recording file: " + state.file.errorString());
            }
        }
        return true;
    }
};
} // namespace

bool LineflowEndpoint::flush(Processor& recorder, QString* error)
{
    auto* instance = dynamic_cast<Recorder*>(&recorder);
    return instance ? instance->flush(error) : fail(error, "Processor is not a Lineflow boundary recorder");
}

QByteArray LineflowEndpoint::settingsSchema(int channels, bool allowLines)
{
    QByteArray result           = R"(version: 1
type: termaster.settings-schema
description: Lineflow boundary recording
content:
  type: object
  properties:
    enabled:
      type: boolean
      title: Recording enabled
      default: false
      x-order: 1
    recordingMode:
      type: string
      title: Record
      description: 'Rendered lines require the terminal root final endpoint and always record Rx.'
      default: packets
      x-order: 2
    file:
      type: string
      title: File
      description: 'Absolute path; {direction} expands to rx, tx or stream. The parent directory must exist.'
      maxLength: 32768
      default: ''
      x-order: 3
    append:
      type: boolean
      title: Append
      default: true
      x-order: 5
    maxFileBytes:
      type: integer
      title: Maximum file bytes
      minimum: 1
      maximum: 1073741824
      default: 16777216
      x-order: 6
    backups:
      type: integer
      title: Backups
      minimum: 0
      maximum: 20
      default: 3
      x-order: 7
    flushEvery:
      type: integer
      title: Flush interval (records)
      minimum: 1
      maximum: 1024
      default: 1
      x-order: 8
)";
    QByteArray packetProperties = R"(    format:
      type: string
      title: Format
      enum: [jsonl, binary, hex]
      x-widget-options:
        variantTitles: {jsonl: JSONL, binary: Binary, hex: HEX}
      default: jsonl
      x-order: 4
)";
    if (channels == 2)
    {
        packetProperties += R"(    direction:
      type: string
      title: Direction
      enum: [both, rx, tx]
      x-widget-options:
        variantTitles: {both: Both, rx: Rx, tx: Tx}
      default: both
      x-order: 9
)";
    }
    if (!allowLines)
    {
        result.replace(
            "      default: packets",
            "      enum: [packets]\n      x-widget-options:\n        variantTitles: {packets: Packets}\n      default: packets");
        return result + packetProperties;
    }

    result += R"(  oneOf:
    - title: Packets
      required: [recordingMode]
      properties:
        recordingMode: {type: string, const: packets}
)";
    for (const auto& line : packetProperties.split('\n'))
    {
        if (!line.isEmpty())
        {
            result += "    " + line + '\n';
        }
    }
    result += R"(    - title: Terminal lines
      required: [recordingMode]
      properties:
        recordingMode: {type: string, const: lines}
        lineEnding:
          type: string
          title: Line ending
          enum: [lf, crlf]
          x-widget-options:
            variantTitles: {lf: LF, crlf: CRLF}
          default: lf
          x-order: 10
        trimTrailingSpaces:
          type: boolean
          title: Trim trailing spaces
          default: true
          x-order: 11
        flushScreen:
          type: boolean
          title: Flush active screen
          description: Write the current screen when recording stops; otherwise record complete history lines only.
          default: true
          x-order: 12
)";
    return result;
}

QVariantMap LineflowEndpoint::defaults(int channels)
{
    QVariantMap result {
        {     "recordingMode",  "packets"},
        {        "lineEnding",       "lf"},
        {"trimTrailingSpaces",       true},
        {       "flushScreen",       true},
        {           "enabled",      false},
        {              "file", QString {}},
        {            "format",    "jsonl"},
        {            "append",       true},
        {      "maxFileBytes",   16777216},
        {           "backups",          3},
        {        "flushEvery",          1}
    };
    if (channels == 2)
    {
        result.insert("direction", "both");
    }
    return result;
}

std::optional<QVariantMap> LineflowEndpoint::resolveConfiguration(const QVariantMap& stored,
                                                                  const QVariantMap& overrides,
                                                                  int                channels,
                                                                  QString*           error)
{
    if (channels != 1 && channels != 2)
    {
        fail(error, "Lineflow recording channels must be 1 or 2");
        return {};
    }
    auto result = merge(merge(defaults(channels), stored), overrides);
    if (channels == 2 && result.value("recordingMode").toString() == "lines")
    {
        // Packet direction is inactive in line mode; rendered terminal lines are Rx.
        result.insert("direction", QStringLiteral("rx"));
    }
    static const SettingsSchema unary(settingsSchema(1));
    static const SettingsSchema duplex(settingsSchema(2));
    const auto                  errors = (channels == 1 ? unary : duplex).validate(result);
    if (!errors.isEmpty())
    {
        fail(error, "Invalid recording configuration: " + errors.first().path + ": " + errors.first().message);
        return {};
    }
    if (result.value("enabled").toBool())
    {
        const auto path = result.value("file").toString();
        if (path.isEmpty() || path.contains(QChar::Null) || !QDir::isAbsolutePath(path))
        {
            fail(error, "Recording requires an absolute nonempty file path without NUL");
            return {};
        }
        if (channels == 2 && result.value("format").toString() == "binary"
            && result.value("direction").toString() == "both" && !path.contains("{direction}"))
        {
            fail(error, "Bidirectional binary recording requires {direction} in the file path");
            return {};
        }
    }
    return result;
}

std::unique_ptr<Processor> LineflowEndpoint::create(const QVariantMap& configuration,
                                                    int                channels,
                                                    Processor::Limits  limits,
                                                    QString*           error)
{
    const auto resolved = resolveConfiguration(configuration, {}, channels, error);
    if (!resolved)
    {
        return {};
    }
    if (resolved->value("enabled").toBool() && resolved->value("recordingMode").toString() == "lines")
    {
        fail(error, "Rendered line recording requires the terminal root final endpoint");
        return {};
    }
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxDepth <= 0 || limits.maxInstances <= 0
        || limits.eventBudget <= 0 || limits.maxSteps <= 0)
    {
        fail(error, "Invalid recording processing limits");
        return {};
    }
    return std::make_unique<Recorder>(*resolved, channels, limits);
}
