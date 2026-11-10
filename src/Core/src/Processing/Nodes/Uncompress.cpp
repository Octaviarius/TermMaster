#include "Uncompress.h"

#include "Compression.h"
#include "CompressionSettings.h"

#include <QScopeGuard>
#include <algorithm>
#include <array>
#include <limits>
#include <zlib.h>

struct Uncompress::Data
{
    z_stream stream {};
    bool     initialized = false;
    int      resetStatus = Z_OK;

    ~Data()
    {
        if (initialized)
        {
            inflateEnd(&stream);
        }
    }
};

Uncompress::Uncompress(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(configuration, limits), _data(std::make_unique<Data>()), _maxSteps(limits.maxSteps),
    _maxOutputBytes(std::min<qsizetype>(configuration.value("maxOutputBytes").toInt(), limits.maxBytes)),
    _concatenated(configuration.value("concatenated").toBool())
{
}

Uncompress::~Uncompress() = default;

QByteArray Uncompress::schema()
{
    return Compression::descriptor().schema(Codec::Mode::Inverse);
}

std::unique_ptr<Processor> Uncompress::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error) || limits.maxSteps <= 0)
    {
        _fail(error, QStringLiteral("Processing node limits must be positive"));
        return {};
    }

    auto configuration = input;
    if (!_stringOption(configuration, "format", "zlib", {"zlib", "gzip", "raw"}, error)
        || !_boolOption(configuration, "autoDetect", true, error)
        || !_boolOption(configuration, "concatenated", false, error)
        || !CompressionSettings::integerOption(configuration, "windowBits", 15, 9, 15, error)
        || !CompressionSettings::integerOption(configuration, "maxOutputBytes", 1048576, 1, 67108864, error))
    {
        return {};
    }

    auto       result = std::unique_ptr<Uncompress>(new Uncompress(configuration, limits));
    const auto status = inflateInit2(&result->_data->stream,
                                     CompressionSettings::wrappedWindowBits(configuration,
                                                                            configuration.value("autoDetect")
                                                                                .toBool()));
    if (status != Z_OK)
    {
        _fail(error, QStringLiteral("Cannot initialize packet decompression (zlib status %1)").arg(status));
        return {};
    }
    result->_data->initialized = true;
    return result;
}

void Uncompress::_process(const Chunk& input, QByteArray& output, QString& error)
{
    if (_data->resetStatus != Z_OK)
    {
        _fail(&error, QStringLiteral("Cannot reset packet decompression context"));
        return;
    }
    if (input.bytes.isEmpty())
    {
        _fail(&error, QStringLiteral("Compressed packet is empty"));
        return;
    }
    if (quint64(input.bytes.size()) > std::numeric_limits<uInt>::max())
    {
        _fail(&error, QStringLiteral("Decompression input exceeds zlib packet capacity"));
        return;
    }

    auto& stream    = _data->stream;
    stream.next_in  = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.bytes.constData()));
    stream.avail_in = uInt(input.bytes.size());
    std::array<char, 16384> buffer;
    const auto              clearPointers = qScopeGuard([&stream] {
        stream.next_in   = nullptr;
        stream.avail_in  = 0;
        stream.next_out  = nullptr;
        stream.avail_out = 0;
    });
    qsizetype               steps         = 0;
    while (steps < _maxSteps)
    {
        ++steps;
        const auto available = stream.avail_in;
        stream.next_out      = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out     = uInt(buffer.size());
        const auto status    = inflate(&stream, Z_NO_FLUSH);
        const auto produced  = qsizetype(buffer.size() - stream.avail_out);

        if (status == Z_NEED_DICT)
        {
            _fail(&error, QStringLiteral("Compressed packet requires an unsupported preset dictionary"));
            return;
        }
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
        {
            const auto reason = stream.msg ? QString::fromLatin1(stream.msg) :
                                             QStringLiteral("zlib status %1").arg(status);
            _fail(&error, QStringLiteral("Invalid compressed packet: %1").arg(reason));
            return;
        }
        if (produced > _maxOutputBytes - output.size())
        {
            _fail(&error, QStringLiteral("Decompressed packet exceeds output byte limit"));
            return;
        }
        if (!_append(output, QByteArray(buffer.data(), produced), error))
        {
            return;
        }
        if (status == Z_STREAM_END)
        {
            if (stream.avail_in == 0)
            {
                return;
            }
            if (!_concatenated)
            {
                _fail(&error, QStringLiteral("Compressed packet contains trailing bytes"));
                return;
            }

            const auto remaining   = stream.avail_in;
            auto*      next        = stream.next_in;
            const auto resetStatus = inflateReset(&stream);
            if (resetStatus != Z_OK)
            {
                _fail(&error, QStringLiteral("Cannot reset decompression for the next stream"));
                return;
            }
            stream.next_in  = next;
            stream.avail_in = remaining;
            continue;
        }
        if (available == stream.avail_in && produced == 0)
        {
            _fail(&error,
                  stream.avail_in == 0 ? QStringLiteral("Compressed packet is truncated") :
                                         QStringLiteral("Packet decompression made no progress"));
            return;
        }
    }

    _fail(&error, QStringLiteral("Packet decompression exceeds step limit"));
}

void Uncompress::_reset()
{
    auto& stream       = _data->stream;
    _data->resetStatus = inflateReset(&stream);
    stream.next_in     = nullptr;
    stream.avail_in    = 0;
    stream.next_out    = nullptr;
    stream.avail_out   = 0;
}
