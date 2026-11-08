#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QVector>
#include <functional>

/** Streaming byte tokenizer. The owner and synchronous handler use one thread.
 * Handler sequence references are borrowed for the callback only. UTF-8 decoding
 * belongs to the screen. No parameter bytes means an empty parameters vector;
 * omitted semicolon/colon values are -1. Oversized strings are discarded through
 * their terminator. reset() clears partial input and diagnostic counters.
 */
class VtParser
{
public:
    enum class Kind
    {
        Print,
        Execute,
        Escape,
        Csi,
        Osc,
        Dcs,
        Apc
    };
    struct Sequence
    {
        Kind                  kind = Kind::Print;
        QByteArray            data;
        QByteArray            intermediates;
        QByteArray            privateMarkers;
        QVector<QVector<int>> parameters;
        char                  command = 0;
    };
    using Handler = std::function<void(const Sequence&)>;

    void receive(QByteArrayView bytes, const Handler& handler);
    void reset();
    void setEightBitControls(bool enabled);
    /** Buffered payload/header bytes; introducers and terminators are excluded. */
    qsizetype incompleteSequenceBytes() const;
    quint64   discardedSequences() const;

private:
    enum class State
    {
        Ground,
        Escape,
        Csi,
        DcsHeader,
        String,
        StringEscape,
        Ignore
    };
    static constexpr qsizetype _headerLimit = 256;
    static constexpr qsizetype _oscLimit    = 64 * 1024;
    static constexpr qsizetype _dcsLimit    = 4 * 1024 * 1024;
    State                      _state       = State::Ground;
    Sequence                   _sequence;
    QByteArray                 _parameters;
    qsizetype                  _headerBytes      = 0;
    quint64                    _discarded        = 0;
    bool                       _eightBitControls = false;
    bool                       _overflow         = false;

    void _start(State state, Kind kind);
    void _abort();
    void _discard();
    bool _parseParameters();
    void _emit(const Handler& handler);
    void _appendString(char byte);
};
