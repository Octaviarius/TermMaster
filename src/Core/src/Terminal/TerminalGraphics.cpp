#include <QBuffer>
#include <QColor>
#include <QImageReader>
#include <QSet>
#include <Termaster/Core/Terminal/TerminalGraphics.h>
#include <algorithm>
#include <limits>
#include <zlib.h>

namespace
{
int number(const QHash<char, QByteArray>& options, char key, int fallback = 0)
{
    if (!options.contains(key))
    {
        return fallback;
    }
    bool      ok    = false;
    const int value = options.value(key).toInt(&ok);
    return ok ? value : -1;
}
bool validSize(int width, int height)
{
    return width > 0 && height > 0 && width <= 4096 && height <= 4096 && qint64(width) * height <= 4 * 1024 * 1024;
}
bool inflateImage(const QByteArray& input, qsizetype limit, QByteArray& output)
{
    if (limit <= 0 || limit > 16 * 1024 * 1024 || input.size() > 8 * 1024 * 1024)
    {
        return false;
    }
    z_stream stream {};
    if (inflateInit(&stream) != Z_OK)
    {
        return false;
    }
    struct Cleanup
    {
        z_stream* stream;
        ~Cleanup()
        {
            inflateEnd(stream);
        }
    } cleanup {&stream};
    output.resize(limit + 1);
    stream.next_in   = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in  = static_cast<uInt>(input.size());
    stream.next_out  = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int status = inflate(&stream, Z_FINISH);
    if (status != Z_STREAM_END || stream.total_out > static_cast<uLong>(limit) || stream.avail_in != 0)
    {
        output.clear();
        return false;
    }
    output.resize(static_cast<qsizetype>(stream.total_out));
    return true;
}
} // namespace

void TerminalGraphics::receive(const VtParser::Sequence& sequence, int column, int row, bool alternate)
{
    if (sequence.kind == VtParser::Kind::Dcs && sequence.command == 'q' && sequence.intermediates.isEmpty()
        && sequence.privateMarkers.isEmpty())
    {
        _sixel(sequence, column, row, alternate);
    }
    else if (sequence.kind == VtParser::Kind::Apc && sequence.data.startsWith('G'))
    {
        _kitty(sequence.data.mid(1), column, row, alternate);
    }
}
const QVector<TerminalGraphics::Placement>& TerminalGraphics::placements() const
{
    return _placements;
}
void TerminalGraphics::setReplyHandler(ReplyHandler handler)
{
    _replyHandler = std::move(handler);
}
QByteArray TerminalGraphics::takeOutput()
{
    QByteArray result;
    result.swap(_output);
    return result;
}
QSize TerminalGraphics::takeCursorAdvance()
{
    const QSize result = _advance;
    _advance           = {};
    return result;
}
void TerminalGraphics::clear()
{
    _placements.clear();
    _images.clear();
    _chunks.clear();
    _chunkOptions.clear();
    _output.clear();
    _advance = {};
    _nextId  = 1;
}
void TerminalGraphics::trimHistory(int count)
{
    if (count <= 0)
    {
        return;
    }
    for (auto& placement : _placements)
    {
        if (!placement.alternate && placement.scroll)
        {
            placement.row -= count;
        }
    }
    _placements.removeIf([](const Placement& p) { return !p.alternate && p.row + p.rows <= 0; });
    for (auto it = _images.begin(); it != _images.end();)
    {
        const int id = it.key();
        if (id < 0
            && std::none_of(_placements.cbegin(), _placements.cend(), [id](const Placement& p) { return p.id == id; }))
        {
            it = _images.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
void TerminalGraphics::setCellSize(QSize size)
{
    if (size.width() > 0 && size.height() > 0)
    {
        _cellSize = size;
    }
}
void TerminalGraphics::_reply(const QHash<char, QByteArray>& options, const QByteArray& result)
{
    const int quiet = number(options, 'q');
    if (quiet == 2 || (quiet == 1 && result == "OK"))
    {
        return;
    }
    QByteArray reply = "\x1b_Gi=" + QByteArray::number(std::max(0, number(options, 'i')));
    if (options.contains('p'))
    {
        reply += ",p=" + QByteArray::number(std::max(0, number(options, 'p')));
    }
    reply += ';' + result.left(256) + "\x1b\\";
    if (_replyHandler)
    {
        _replyHandler(reply);
        return;
    }
    if (_output.size() + reply.size() <= 65536)
    {
        _output += reply;
    }
}
bool TerminalGraphics::_store(int id, const QImage& image)
{
    if (image.isNull() || (!_images.contains(id) && _images.size() >= 64))
    {
        return false;
    }
    qint64 bytes = image.sizeInBytes();
    // Placements may retain a replaced image, so account for all unique buffers.
    QSet<qint64> keys;
    keys.insert(image.cacheKey());
    for (auto it = _images.cbegin(); it != _images.cend(); ++it)
    {
        if (it.key() != id && !keys.contains(it.value().cacheKey()))
        {
            bytes += it.value().sizeInBytes();
            keys.insert(it.value().cacheKey());
        }
    }
    for (const auto& p : _placements)
    {
        if (p.id != id && !keys.contains(p.image.cacheKey()))
        {
            bytes += p.image.sizeInBytes();
            keys.insert(p.image.cacheKey());
        }
    }
    if (bytes > _storageLimit)
    {
        return false;
    }
    _images.insert(id, image);
    for (auto& p : _placements)
    {
        if (p.id == id)
        {
            p.image  = image;
            p.source = image.rect();
        }
    }
    return true;
}
bool TerminalGraphics::_place(int id, const QHash<char, QByteArray>& options, int column, int row, bool alternate)
{
    if (!_images.contains(id))
    {
        return false;
    }
    const int    pid     = number(options, 'p');
    const int    columns = number(options, 'c');
    const int    rows    = number(options, 'r');
    const int    x       = number(options, 'x');
    const int    y       = number(options, 'y');
    const QImage image   = _images.value(id);
    const int    width   = number(options, 'w', image.width() - x);
    const int    height  = number(options, 'h', image.height() - y);
    if (pid < 0 || columns < 0 || rows < 0 || columns > 4096 || rows > 4096 || x < 0 || y < 0 || width <= 0
        || height <= 0 || qint64(x) + width > image.width() || qint64(y) + height > image.height())
    {
        return false;
    }
    int displayColumns = columns ? columns : (width + _cellSize.width() - 1) / _cellSize.width();
    int displayRows    = rows ? rows : (height + _cellSize.height() - 1) / _cellSize.height();
    if (columns && !rows)
    {
        const qint64 numerator   = qint64(columns) * _cellSize.width() * height;
        const qint64 denominator = qint64(width) * _cellSize.height();
        const qint64 derived     = (numerator + denominator - 1) / denominator;
        if (derived > 4096)
        {
            return false;
        }
        displayRows = std::max(1, int(derived));
    }
    if (rows && !columns)
    {
        const qint64 numerator   = qint64(rows) * _cellSize.height() * width;
        const qint64 denominator = qint64(height) * _cellSize.width();
        const qint64 derived     = (numerator + denominator - 1) / denominator;
        if (derived > 4096)
        {
            return false;
        }
        displayColumns = std::max(1, int(derived));
    }
    if (pid)
    {
        _placements.removeIf([=](const Placement& p) { return p.id == id && p.placementId == pid; });
    }
    if (_placements.size() >= 128)
    {
        return false;
    }
    Placement p;
    p.image       = image;
    p.id          = id;
    p.placementId = pid;
    p.column      = column;
    p.row         = row;
    p.alternate   = alternate;
    p.columns     = displayColumns;
    p.rows        = displayRows;
    if (!columns && !rows)
    {
        p.pixelSize = QSize(width, height);
    }
    p.source = QRect(x, y, width, height);
    p.z      = number(options, 'z');
    _placements.append(p);
    if (number(options, 'C') != 1)
    {
        _advance = QSize(p.columns, p.rows);
    }
    return true;
}
void TerminalGraphics::_kitty(const QByteArray& data, int column, int row, bool alternate)
{
    const qsizetype         separator = data.indexOf(';');
    QHash<char, QByteArray> options;
    if ((separator < 0 ? data.size() : separator) > 512)
    {
        _chunks.clear();
        _chunkOptions.clear();
        _reply(options, "EINVAL:control header limit");
        return;
    }
    const QByteArray header = separator < 0 ? data : data.left(separator);
    for (const QByteArray& pair : header.split(','))
    {
        if (pair.isEmpty())
        {
            continue;
        }
        if (pair.size() < 3 || pair.size() > 18 || pair[1] != '=' || options.contains(pair[0]) || options.size() >= 32)
        {
            _chunks.clear();
            _chunkOptions.clear();
            _reply(options, "EINVAL:invalid control data");
            return;
        }
        const QByteArray value = pair.mid(2);
        const bool       flag  = QByteArray("atod").contains(pair[0]);
        bool             valid = flag ? value.size() == 1 : true;
        if (!flag)
        {
            for (qsizetype i = 0; i < value.size(); ++i)
            {
                if ((value[i] < '0' || value[i] > '9') && !(i == 0 && value[i] == '-'))
                {
                    valid = false;
                }
            }
            bool converted = false;
            value.toInt(&converted);
            valid = valid && converted;
        }
        if (!valid)
        {
            _chunks.clear();
            _chunkOptions.clear();
            _reply(options, "EINVAL:control value");
            return;
        }
        options.insert(pair[0], value);
    }
    const QByteArray payload = separator < 0 ? QByteArray() : data.mid(separator + 1);
    if (!_chunkOptions.isEmpty())
    {
        if (options.contains('i') && options.value('i') != _chunkOptions.value('i'))
        {
            _chunks.clear();
            _chunkOptions.clear();
            _reply(options, "EINVAL:interleaved transfer");
            return;
        }
        for (auto it = _chunkOptions.cbegin(); it != _chunkOptions.cend(); ++it)
        {
            if (!options.contains(it.key()))
            {
                options.insert(it.key(), it.value());
            }
        }
    }
    const char action = options.value('a', "t").isEmpty() ? 0 : options.value('a', "t").at(0);
    if (action == 'd')
    {
        const char what = options.value('d', "a").isEmpty() ? 0 : options.value('d', "a").at(0);
        if (what != 'a' && what != 'A' && what != 'i' && what != 'I')
        {
            _reply(options, "ENOTSUPPORTED:delete selector");
            return;
        }
        const int id  = number(options, 'i');
        const int pid = number(options, 'p');
        _placements.removeIf([=](const Placement& p) {
            return p.alternate == alternate
                && ((what == 'a' || what == 'A') || (p.id == id && (!pid || p.placementId == pid)));
        });
        if (what == 'A')
        {
            _images.clear();
        }
        if (what == 'I')
        {
            _images.remove(id);
        }
        return;
    }
    if (options.value('t', "d") != "d" || (options.contains('o') && options.value('o') != "z")
        || number(options, 'U') != 0 || options.contains('I')
        || (action != 't' && action != 'T' && action != 'p' && action != 'q'))
    {
        _chunks.clear();
        _chunkOptions.clear();
        _reply(options, "ENOTSUPPORTED:transport or operation");
        return;
    }
    if (action == 'p')
    {
        _reply(options,
               _place(number(options, 'i'), options, column, row, alternate) ? "OK" :
                                                                               "ENOENT:invalid image or placement");
        return;
    }
    const int more = number(options, 'm');
    if ((more != 0 && more != 1) || _chunks.size() + payload.size() > 8 * 1024 * 1024)
    {
        _chunks.clear();
        _chunkOptions.clear();
        _reply(options, "E2BIG:transfer limit");
        return;
    }
    _chunks += payload;
    if (more)
    {
        _chunkOptions = options;
        _chunkOptions.remove('m');
        return;
    }
    const auto decoded = QByteArray::fromBase64Encoding(_chunks, QByteArray::AbortOnBase64DecodingErrors);
    _chunks.clear();
    _chunkOptions.clear();
    if (!decoded)
    {
        _reply(options, "EINVAL:base64");
        return;
    }
    QByteArray bytes  = decoded.decoded;
    const int  format = number(options, 'f', 32);
    if (options.value('o') == "z")
    {
        qsizetype limit = 0;
        if (format == 100)
        {
            limit = 8 * 1024 * 1024;
        }
        else if (format == 24 || format == 32)
        {
            const int width  = number(options, 's');
            const int height = number(options, 'v');
            if (validSize(width, height))
            {
                limit = qsizetype(width) * height * (format / 8);
            }
        }
        else
        {
            _reply(options, "ENOTSUPPORTED:pixel format");
            return;
        }
        QByteArray inflated;
        if (!inflateImage(bytes, limit, inflated))
        {
            _reply(options, "EINVAL:compressed image or decoded size");
            return;
        }
        bytes = std::move(inflated);
    }
    QImage image;
    if (format == 100)
    {
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "png");
        const QSize  size = reader.size();
        if (validSize(size.width(), size.height()))
        {
            image = reader.read().convertToFormat(QImage::Format_ARGB32);
        }
    }
    else if (format == 24 || format == 32)
    {
        const int width    = number(options, 's');
        const int height   = number(options, 'v');
        const int channels = format / 8;
        if (validSize(width, height) && bytes.size() == qint64(width) * height * channels)
        {
            image = QImage(width, height, QImage::Format_ARGB32);
            if (image.isNull())
            {
                _reply(options, "ENOSPC:image allocation");
                return;
            }
            for (int y = 0; y < height; ++y)
            {
                auto*       line  = reinterpret_cast<QRgb*>(image.scanLine(y));
                const auto* input = reinterpret_cast<const uchar*>(bytes.constData()) + qint64(y) * width * channels;
                for (int x = 0; x < width; ++x)
                {
                    line[x]  = qRgba(input[0], input[1], input[2], channels == 4 ? input[3] : 255);
                    input   += channels;
                }
            }
        }
    }
    else
    {
        _reply(options, "ENOTSUPPORTED:pixel format");
        return;
    }
    if (image.isNull())
    {
        _reply(options, "EINVAL:image dimensions or data");
        return;
    }
    if (action == 'q')
    {
        _reply(options, "OK");
        return;
    }
    int id = number(options, 'i');
    if (id < 0)
    {
        _reply(options, "EINVAL:image id");
        return;
    }
    if (!id)
    {
        while (_images.contains(_nextId) && _nextId < std::numeric_limits<int>::max())
        {
            ++_nextId;
        }
        id = _nextId;
        options.insert('i', QByteArray::number(id));
    }
    if (!_store(id, image))
    {
        _reply(options, "ENOSPC:image storage limit");
        return;
    }
    if (action == 'T' && !_place(id, options, column, row, alternate))
    {
        _reply(options, "EINVAL:placement limit or dimensions");
        return;
    }
    _reply(options, "OK");
}

void TerminalGraphics::_sixel(const VtParser::Sequence& sequence, int column, int row, bool alternate)
{
    const QByteArray& data = sequence.data;
    if (data.size() > 4 * 1024 * 1024)
    {
        return;
    }
    QVector<QRgb> palette(256, qRgb(255, 255, 255));
    palette[0]                         = qRgb(0, 0, 0);
    palette[1]                         = qRgb(51, 51, 204);
    palette[2]                         = qRgb(204, 33, 33);
    palette[3]                         = qRgb(51, 204, 51);
    const QVector<QRgb> initialPalette = palette;
    int                 width          = 0;
    int                 height         = 0;
    QImage              image;
    // First pass validates geometry before allocating any pixel storage.
    for (int pass = 0; pass < 2; ++pass)
    {
        palette             = initialPalette;
        int       x         = 0;
        int       y         = 0;
        int       color     = 0;
        qsizetype pos       = 0;
        qint64    pixelWork = 0;
        auto      integer   = [&]() {
            int result = 0;
            while (pos < data.size() && data[pos] >= '0' && data[pos] <= '9')
            {
                if (result > 1000000)
                {
                    return -1;
                }
                result = result * 10 + data[pos++] - '0';
            }
            return result;
        };
        while (pos < data.size())
        {
            const uchar ch = data[pos++];
            if (ch == '$')
            {
                x = 0;
                continue;
            }
            if (ch == '-')
            {
                x  = 0;
                y += 6;
                if (y >= 4096)
                {
                    return;
                }
                continue;
            }
            if (ch == '#' || ch == '"')
            {
                QVector<int> params;
                params.append(integer());
                while (pos < data.size() && data[pos] == ';')
                {
                    ++pos;
                    params.append(integer());
                    if (params.size() > 5)
                    {
                        return;
                    }
                }
                if (std::any_of(params.cbegin(), params.cend(), [](int n) { return n < 0; }))
                {
                    return;
                }
                if (ch == '"')
                {
                    if (params.size() >= 4)
                    {
                        if (!validSize(params[2], params[3]))
                        {
                            return;
                        }
                        width  = std::max(width, params[2]);
                        height = std::max(height, params[3]);
                    }
                }
                else
                {
                    if (params[0] > 255)
                    {
                        return;
                    }
                    color = params[0];
                    if (params.size() == 5)
                    {
                        if (params[1] == 2 && params[2] <= 100 && params[3] <= 100 && params[4] <= 100)
                        {
                            palette[color] = qRgb(params[2] * 255 / 100, params[3] * 255 / 100, params[4] * 255 / 100);
                        }
                        else if (params[1] == 1 && params[2] <= 360 && params[3] <= 100 && params[4] <= 100)
                        {
                            palette[color] = QColor::fromHsl((params[2] + 240) % 360,
                                                             params[4] * 255 / 100,
                                                             params[3] * 255 / 100)
                                                 .rgb();
                        }
                        else
                        {
                            return;
                        }
                    }
                }
                continue;
            }
            int   repeat = 1;
            uchar sixel  = ch;
            if (ch == '!')
            {
                repeat = integer();
                if (repeat <= 0 || repeat > 4096 || pos >= data.size())
                {
                    return;
                }
                sixel = data[pos++];
            }
            if (sixel >= '?' && sixel <= '~')
            {
                pixelWork += qint64(repeat) * 6;
                if (pixelWork > 32 * 1024 * 1024)
                {
                    return;
                }
                if (x > 4096 - repeat || y > 4090)
                {
                    return;
                }
                width  = std::max(width, x + repeat);
                height = std::max(height, y + 6);
                if (!validSize(width, height))
                {
                    return;
                }
                if (pass)
                {
                    for (int bit = 0; bit < 6; ++bit)
                    {
                        if ((sixel - '?') & (1 << bit))
                        {
                            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y + bit));
                            std::fill(line + x, line + x + repeat, palette[color]);
                        }
                    }
                }
                x += repeat;
            }
            else if (ch == '!' || (ch >= 0x20 && ch != 0x7f))
            {
                return;
            }
        }
        if (!pass)
        {
            if (!validSize(width, height))
            {
                return;
            }
            image = QImage(width, height, QImage::Format_ARGB32);
            if (image.isNull())
            {
                return;
            }
            image.fill(Qt::transparent);
        }
    }
    int id = -1;
    while (_images.contains(id))
    {
        --id;
    }
    if (!_store(id, image))
    {
        return;
    }
    QHash<char, QByteArray> options;
    if (!_place(id, options, column, row, alternate))
    {
        _images.remove(id);
    }
}
