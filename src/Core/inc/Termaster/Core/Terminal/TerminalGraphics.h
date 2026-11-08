#pragma once
#include <QHash>
#include <QImage>
#include <QRect>
#include <QSize>
#include <Termaster/Core/Terminal/VtParser.h>

/** Bounded, single-thread-owned Sixel and direct-data Kitty image state.
 * No filesystem, shared memory, animation or virtual placements.
 * Direct image data optionally uses bounded zlib decompression.
 */
class TerminalGraphics
{
public:
    using ReplyHandler = std::function<void(const QByteArray&)>;
    struct Placement
    {
        QImage image;
        int    column      = 0;
        int    row         = 0;
        int    columns     = 0;
        int    rows        = 0;
        bool   alternate   = false;
        int    id          = 0;
        int    placementId = 0;
        QRect  source;
        /** Natural cropped pixels when valid; otherwise size follows the cell grid. */
        QSize pixelSize;
        int   z      = 0;
        bool  scroll = true;
    };
    void                      receive(const VtParser::Sequence& sequence, int column, int absoluteRow, bool alternate);
    const QVector<Placement>& placements() const;
    /** Handler receives a borrowed bounded reply synchronously; receive must not reenter. */
    void       setReplyHandler(ReplyHandler handler);
    QByteArray takeOutput();
    QSize      takeCursorAdvance();
    void       clear();
    void       trimHistory(int count);
    void       setCellSize(QSize size);

private:
    static constexpr qint64 _pixelLimit   = 4 * 1024 * 1024;
    static constexpr qint64 _storageLimit = 32 * 1024 * 1024;
    QVector<Placement>      _placements;
    QHash<int, QImage>      _images;
    QHash<char, QByteArray> _chunkOptions;
    QByteArray              _chunks;
    QByteArray              _output;
    ReplyHandler            _replyHandler;
    QSize                   _cellSize {8, 16};
    QSize                   _advance;
    int                     _nextId = 1;
    void                    _sixel(const VtParser::Sequence& sequence, int column, int row, bool alternate);
    void                    _kitty(const QByteArray& data, int column, int row, bool alternate);
    void                    _reply(const QHash<char, QByteArray>& options, const QByteArray& result);
    bool                    _store(int id, const QImage& image);
    bool                    _place(int id, const QHash<char, QByteArray>& options, int column, int row, bool alternate);
};
