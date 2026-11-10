#include "Compress.h"

#include "Compression.h"
#include "CompressionSettings.h"

#include <QScopeGuard>
#include <array>
#include <limits>
#include <zlib.h>

struct Compress::Data
{
    z_stream stream {};
    bool     initialized = false;
    int      resetStatus = Z_OK;

    ~Data()
    {
        if (initialized)
        {
            deflateEnd(&stream);
        }
    }
};

Compress::Compress(QVariantMap configuration, Limits limits) :
    SyncProcessorNode(std::move(configuration), limits), _data(std::make_unique<Data>()), _maxSteps(limits.maxSteps)
{
}

Compress::~Compress() = default;

QByteArray Compress::schema()
{
    return Compression::descriptor().schema(Codec::Mode::Direct);
}

std::unique_ptr<Processor> Compress::create(const QVariantMap& input, const Limits& limits, QString* error)
{
    if (!_validLimits(limits, error) || limits.maxSteps <= 0)
    {
        _fail(error, QStringLiteral("Processing node limits must be positive"));
        return {};
    }

    auto configuration = input;
    if (!_stringOption(configuration, "format", "zlib", {"zlib", "gzip", "raw"}, error)
        || !_stringOption(configuration,
                          "strategy",
                          "default",
                          {"default", "filtered", "huffman", "rle", "fixed"},
                          error)
        || !CompressionSettings::integerOption(configuration, "level", -1, -1, 9, error)
        || !CompressionSettings::integerOption(configuration, "windowBits", 15, 9, 15, error)
        || !CompressionSettings::integerOption(configuration, "memLevel", 8, 1, 9, error))
    {
        return {};
    }

    const auto strategyName = configuration.value("strategy").toString();
    const int  strategy     = strategyName == "filtered" ? Z_FILTERED :
                              strategyName == "huffman"  ? Z_HUFFMAN_ONLY :
                              strategyName == "rle"      ? Z_RLE :
                              strategyName == "fixed"    ? Z_FIXED :
                                                           Z_DEFAULT_STRATEGY;
    auto       result       = std::unique_ptr<Compress>(new Compress(configuration, limits));
    const auto status       = deflateInit2(&result->_data->stream,
                                           configuration.value("level").toInt(),
                                           Z_DEFLATED,
                                           CompressionSettings::wrappedWindowBits(configuration),
                                           configuration.value("memLevel").toInt(),
                                           strategy);
    if (status != Z_OK)
    {
        _fail(error, QStringLiteral("Cannot initialize packet compression (zlib status %1)").arg(status));
        return {};
    }
    result->_data->initialized = true;
    return result;
}

void Compress::_process(const Chunk& input, QByteArray& output, QString& error)
{
    if (_data->resetStatus != Z_OK)
    {
        _fail(&error, QStringLiteral("Cannot reset packet compression context"));
        return;
    }
    if (quint64(input.bytes.size()) > std::numeric_limits<uInt>::max())
    {
        _fail(&error, QStringLiteral("Compression input exceeds zlib packet capacity"));
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
        const auto status    = deflate(&stream, Z_FINISH);
        const auto produced  = qsizetype(buffer.size() - stream.avail_out);

        if (status != Z_OK && status != Z_STREAM_END)
        {
            _fail(&error, QStringLiteral("Packet compression failed (zlib status %1)").arg(status));
            return;
        }
        if (!_append(output, QByteArray(buffer.data(), produced), error))
        {
            return;
        }
        if (status == Z_STREAM_END)
        {
            if (stream.avail_in != 0)
            {
                _fail(&error, QStringLiteral("Packet compression did not consume all input"));
            }
            return;
        }
        if (available == stream.avail_in && produced == 0)
        {
            _fail(&error, QStringLiteral("Packet compression made no progress"));
            return;
        }
    }

    _fail(&error, QStringLiteral("Packet compression exceeds step limit"));
}

void Compress::_reset()
{
    auto& stream       = _data->stream;
    _data->resetStatus = deflateReset(&stream);
    stream.next_in     = nullptr;
    stream.avail_in    = 0;
    stream.next_out    = nullptr;
    stream.avail_out   = 0;
}
