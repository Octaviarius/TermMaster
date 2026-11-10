#include "PatternFramer.h"

#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <algorithm>
#include <bitset>
#include <optional>
#include <pcre2.h>
#include <vector>

struct PatternFramer::Data
{
    struct Part
    {
        QByteArray literal;
        int        capture = -1;
    };
    pcre2_code*                     code    = nullptr;
    pcre2_match_data*               match   = nullptr;
    pcre2_match_context*            context = nullptr;
    pcre2_jit_stack*                stack   = nullptr;
    std::vector<Part>               parts;
    bool                            resumePartial = true;
    std::optional<std::bitset<256>> completionBytes;

    ~Data()
    {
        pcre2_match_data_free(match);
        pcre2_match_context_free(context);
        pcre2_jit_stack_free(stack);
        pcre2_code_free(code);
    }
};

namespace
{
QString regexError(int code)
{
    PCRE2_UCHAR text[256];
    const auto  size = pcre2_get_error_message(code, text, sizeof(text));
    return size >= 0 ? QString::fromUtf8(reinterpret_cast<const char*>(text), size) :
                       QString("PCRE2 error %1").arg(code);
}
int hexDigit(QChar character)
{
    const auto value = character.unicode();
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    return -1;
}

std::optional<std::bitset<256>> atomBytes(const QByteArray& atom)
{
    std::bitset<256> bytes;
    if (atom.size() == 1 && atom[0] != '.')
    {
        bytes.set(static_cast<unsigned char>(atom[0]));
        return bytes;
    }
    int                                                     error  = 0;
    PCRE2_SIZE                                              offset = 0;
    std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)> code(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(
                                                                                   atom.constData()),
                                                                               size_t(atom.size()),
                                                                               PCRE2_NEVER_UTF | PCRE2_NEVER_UCP
                                                                                   | PCRE2_DOTALL,
                                                                               &error,
                                                                               &offset,
                                                                               nullptr),
                                                                 pcre2_code_free);
    std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)> match(pcre2_match_data_create(1, nullptr),
                                                                              pcre2_match_data_free);
    if (!code || !match)
    {
        return std::nullopt;
    }
    for (int value = 0; value < 256; ++value)
    {
        const auto byte = static_cast<PCRE2_UCHAR>(value);
        const int
            result = pcre2_match(code.get(), &byte, 1, 0, PCRE2_ANCHORED | PCRE2_ENDANCHORED, match.get(), nullptr);
        if (result >= 0)
        {
            const auto* span = pcre2_get_ovector_pointer(match.get());
            if (span[0] != 0 || span[1] != 1)
            {
                return std::nullopt;
            }
            bytes.set(value);
        }
        else if (result != PCRE2_ERROR_NOMATCH)
        {
            return std::nullopt;
        }
    }
    return bytes.any() ? std::optional(bytes) : std::nullopt;
}

/** Conservative consuming-concatenation recognizer, not a replacement regex parser.
 * Every quantified atom must stop at an unquantified suffix barrier. Without
 * that barrier, PARTIAL_HARD may expose an old match after later backtracking.
 * Assertions, alternation, group repetition and unknown constructs use PCRE2.
 */
std::optional<std::bitset<256>> completionBytes(const QByteArray& pattern)
{
    struct Atom
    {
        std::bitset<256> bytes;
        bool             quantified = false;
    };
    std::vector<Atom> atoms;
    int               depth       = 0;
    bool              canQuantify = false;
    qsizetype         index       = 0;
    while (index < pattern.size())
    {
        const char character = pattern[index];
        if (character == '(')
        {
            ++index;
            if (index < pattern.size() && pattern[index] == '?')
            {
                if (pattern.mid(index, 2) == "?:")
                {
                    index += 2;
                }
                else if (pattern.mid(index, 2) == "?<")
                {
                    const auto end  = pattern.indexOf('>', index + 2);
                    const auto name = pattern.mid(index + 2, end - index - 2);
                    if (end < 0 || name.isEmpty()
                        || !std::all_of(
                            name.cbegin(),
                            name.cend(),
                            [](char c) {
                                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                                    || c == '_';
                            }))
                    {
                        return std::nullopt;
                    }
                    index = end + 1;
                }
                else
                {
                    return std::nullopt;
                }
            }
            ++depth;
            canQuantify = false;
            continue;
        }
        if (character == ')')
        {
            if (--depth < 0)
            {
                return std::nullopt;
            }
            ++index;
            canQuantify = false;
            continue;
        }
        if (character == '*' || character == '+' || character == '?' || character == '{')
        {
            if (!canQuantify || atoms.empty())
            {
                return std::nullopt;
            }
            atoms.back().quantified = true;
            canQuantify             = false;
            if (character == '{')
            {
                const auto end = pattern.indexOf('}', index + 1);
                if (end < 0)
                {
                    return std::nullopt;
                }
                const auto range  = pattern.mid(index + 1, end - index - 1);
                const auto digits = [](const QByteArray& value) {
                    return !value.isEmpty()
                        && std::all_of(value.cbegin(), value.cend(), [](char c) { return c >= '0' && c <= '9'; });
                };
                const auto comma = range.indexOf(',');
                if ((comma < 0 && !digits(range))
                    || (comma >= 0
                        && (!digits(range.first(comma))
                            || (comma + 1 < range.size() && !digits(range.mid(comma + 1))))))
                {
                    return std::nullopt;
                }
                index = end + 1;
            }
            else
            {
                ++index;
            }
            if (index < pattern.size() && (pattern[index] == '?' || pattern[index] == '+'))
            {
                ++index;
            }
            continue;
        }
        if (QByteArrayLiteral("|^$]}").contains(character) || atoms.size() >= 64)
        {
            return std::nullopt;
        }

        const auto start = index;
        if (character == '[')
        {
            ++index;
            if (index < pattern.size() && pattern[index] == '^')
            {
                ++index;
            }
            if (index < pattern.size() && pattern[index] == ']')
            {
                ++index;
            }
            bool closed = false;
            while (index < pattern.size())
            {
                if (pattern[index] == '\\')
                {
                    index += 2;
                }
                else if (pattern[index++] == ']')
                {
                    closed = true;
                    break;
                }
            }
            const auto token = pattern.mid(start, index - start);
            if (!closed || token.contains("[:") || token.contains("[.") || token.contains("[="))
            {
                return std::nullopt;
            }
        }
        else if (character == '\\')
        {
            if (++index >= pattern.size())
            {
                return std::nullopt;
            }
            const char escaped = pattern[index++];
            if (escaped == 'x')
            {
                if (index + 1 >= pattern.size() || hexDigit(QChar::fromLatin1(pattern[index])) < 0
                    || hexDigit(QChar::fromLatin1(pattern[index + 1])) < 0)
                {
                    return std::nullopt;
                }
                index += 2;
            }
            else if (escaped == '0')
            {
                if (index < pattern.size() && pattern[index] >= '0' && pattern[index] <= '9')
                {
                    return std::nullopt;
                }
            }
            else if (!QByteArrayLiteral("dDsSwWhHvVnrtfae\\.^$*+?()[]{}|-/").contains(escaped))
            {
                return std::nullopt;
            }
        }
        else
        {
            ++index;
        }
        auto bytes = atomBytes(pattern.mid(start, index - start));
        if (!bytes)
        {
            return std::nullopt;
        }
        atoms.push_back({*bytes, false});
        canQuantify = true;
    }
    if (depth != 0 || atoms.empty() || atoms.back().quantified || atoms.back().bytes.count() > 16)
    {
        return std::nullopt;
    }

    std::bitset<256> repeated;
    size_t           suffix = 0;
    for (size_t atom = 0; atom < atoms.size(); ++atom)
    {
        if (atoms[atom].quantified)
        {
            repeated |= atoms[atom].bytes;
            suffix    = atom + 1;
        }
    }
    for (size_t atom = suffix; atom < atoms.size(); ++atom)
    {
        if ((atoms[atom].bytes & repeated).none())
        {
            return atoms.back().bytes;
        }
    }
    return std::nullopt;
}

} // namespace

PatternFramer::PatternFramer(QVariantMap configuration, Limits limits, std::unique_ptr<Data> data) :
    _configuration(std::move(configuration)), _limits(limits), _data(std::move(data))
{
}
PatternFramer::~PatternFramer() = default;
QVariantMap PatternFramer::configuration() const
{
    return _configuration;
}

QString PatternFramer::inputName(int port) const
{
    return port == 0 ? QStringLiteral("Stream") : QString {};
}

QString PatternFramer::outputName(int port) const
{
    return port == 0 ? QStringLiteral("Frames") : QString {};
}

std::unique_ptr<Processor> PatternFramer::create(const QVariantMap& configuration, const Limits& limits, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto reject = [&](QString message) -> std::unique_ptr<Processor> {
        if (error)
        {
            *error = std::move(message);
        }
        return {};
    };
    if (limits.maxBytes <= 0 || limits.maxChunks <= 0 || limits.maxSteps <= 0 || limits.maxDepth <= 0
        || limits.maxInstances <= 0 || limits.eventBudget <= 0)
    {
        return reject("Pattern framer runtime limits must be positive");
    }
    auto resolved = configuration;
    if (!resolved.contains("pattern"))
    {
        resolved.insert("pattern", QString("(?<payload>[^\\r\\n]+)\\r?\\n"));
    }
    if (!resolved.contains("outputTemplate"))
    {
        resolved.insert("outputTemplate", QString {});
    }
    if (resolved.value("pattern").typeId() != QMetaType::QString
        || resolved.value("outputTemplate").typeId() != QMetaType::QString)
    {
        return reject("Pattern and output template must be text");
    }
    const auto pattern = resolved.value("pattern").toString().toUtf8();
    const auto text    = resolved.value("outputTemplate").toString();
    if (pattern.isEmpty() || pattern.size() > 4096 || text.size() > 16384 || text.toUtf8().size() > 16384)
    {
        return reject("Pattern or output template exceeds its length limit");
    }
    auto data           = std::make_unique<Data>();
    // \G is relative to the supplied start offset, so preserve zero for such patterns.
    data->resumePartial = !pattern.contains("\\G");
    auto* compile       = pcre2_compile_context_create(nullptr);
    if (!compile)
    {
        return reject("Cannot allocate pattern compile context");
    }
    pcre2_set_parens_nest_limit(compile, 64);
    int        code   = 0;
    PCRE2_SIZE offset = 0;
    data->code        = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.constData()),
                                      size_t(pattern.size()),
                                      PCRE2_NEVER_UTF | PCRE2_NEVER_UCP | PCRE2_DOTALL,
                                      &code,
                                      &offset,
                                      compile);
    pcre2_compile_context_free(compile);
    if (!data->code)
    {
        return reject(QString("Invalid pattern at byte %1: %2").arg(offset).arg(regexError(code)));
    }
    data->completionBytes = completionBytes(pattern);
    uint32_t empty        = 0;
    pcre2_pattern_info(data->code, PCRE2_INFO_MATCHEMPTY, &empty);
    if (empty)
    {
        return reject("Pattern must not match an empty frame");
    }
    code = pcre2_jit_compile(data->code, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_HARD);
    if (code < 0)
    {
        return reject(QString("Pattern JIT compilation failed: %1").arg(regexError(code)));
    }
    PCRE2_SIZE jitSize = 0;
    pcre2_pattern_info(data->code, PCRE2_INFO_JITSIZE, &jitSize);
    if (!jitSize)
    {
        return reject("Pattern does not support JIT execution");
    }
    uint32_t captures = 0;
    pcre2_pattern_info(data->code, PCRE2_INFO_CAPTURECOUNT, &captures);
    QByteArray literal;
    const auto flush = [&] {
        if (!literal.isEmpty())
        {
            data->parts.push_back({std::move(literal), -1});
            literal.clear();
        }
    };
    if (text.isEmpty())
    {
        data->parts.push_back({{}, 0});
    }
    for (qsizetype index = 0; index < text.size();)
    {
        const auto character = text[index++];
        if (character == '$')
        {
            if (index < text.size() && text[index] == '$')
            {
                literal.append('$');
                ++index;
                continue;
            }
            if (index == text.size() || text[index++] != '{')
            {
                return reject("Output template dollar must use $$ or ${capture}");
            }
            const auto end = text.indexOf('}', index);
            if (end < 0 || end == index)
            {
                return reject("Output template has an incomplete capture reference");
            }
            const auto name = text.mid(index, end - index);
            if (name.contains(QChar(0)))
            {
                return reject("Output template capture name contains NUL");
            }
            index        = end + 1;
            bool numeric = true;
            for (const auto digit : name)
            {
                numeric = numeric && digit >= '0' && digit <= '9';
            }
            bool ok      = false;
            int  capture = numeric ? name.toInt(&ok) : -1;
            if (numeric && (!ok || capture < 0 || uint32_t(capture) > captures))
            {
                return reject(QString("Output template refers to unknown capture number: %1").arg(name));
            }
            if (!numeric)
            {
                const auto bytes = name.toUtf8();
                capture = pcre2_substring_number_from_name(data->code, reinterpret_cast<PCRE2_SPTR>(bytes.constData()));
                if (capture < 0)
                {
                    return reject(QString("Output template refers to unknown or ambiguous capture name: %1").arg(name));
                }
            }
            flush();
            data->parts.push_back({{}, capture});
        }
        else if (character == '\\')
        {
            if (index == text.size())
            {
                return reject("Output template has an incomplete escape");
            }
            const auto escaped = text[index++];
            if (escaped == '\\')
            {
                literal.append('\\');
            }
            else if (escaped == 'r')
            {
                literal.append('\r');
            }
            else if (escaped == 'n')
            {
                literal.append('\n');
            }
            else if (escaped == 't')
            {
                literal.append('\t');
            }
            else if (escaped == '0')
            {
                literal.append('\0');
            }
            else if (escaped == 'x')
            {
                if (index + 1 >= text.size() || hexDigit(text[index]) < 0 || hexDigit(text[index + 1]) < 0)
                {
                    return reject("Output template hexadecimal escape requires two digits");
                }
                literal.append(char((hexDigit(text[index]) << 4) | hexDigit(text[index + 1])));
                index += 2;
            }
            else
            {
                return reject("Output template contains an unsupported escape");
            }
        }
        else
        {
            const auto start = index - 1;
            while (index < text.size() && text[index] != '$' && text[index] != '\\')
            {
                ++index;
            }
            literal.append(text.mid(start, index - start).toUtf8());
        }
    }
    flush();
    data->match   = pcre2_match_data_create_from_pattern(data->code, nullptr);
    data->context = pcre2_match_context_create(nullptr);
    data->stack   = pcre2_jit_stack_create(32 * 1024, 128 * 1024, nullptr);
    if (!data->match || !data->context || !data->stack)
    {
        return reject("Cannot allocate pattern matching resources");
    }
    pcre2_set_match_limit(data->context, uint32_t(std::min<qsizetype>(limits.maxSteps, 100000)));
    pcre2_set_heap_limit(data->context, 8 * 1024);
    pcre2_jit_stack_assign(data->context, nullptr, data->stack);
    return std::unique_ptr<Processor>(new PatternFramer(std::move(resolved), limits, std::move(data)));
}

Processor::AcceptResult PatternFramer::accept(Chunk input, Callbacks callbacks)
{
    if (_stopped || input.port != 0)
    {
        return AcceptResult::Rejected;
    }
    if (_busy)
    {
        return AcceptResult::WouldBlock;
    }
    if (input.bytes.size() > _limits.maxBytes)
    {
        return AcceptResult::Rejected;
    }
    _busy = true;
    QString      error;
    QList<Chunk> outputs;
    qsizetype    outputBytes = 0;
    qsizetype    consumed    = 0;
    if (input.bytes.size() > _limits.maxBytes - _buffer.size())
    {
        error = "Pattern framer accumulated input exceeds its byte limit";
    }
    else
    {
        _buffer.append(input.bytes.constData(), input.bytes.size());
    }
    QElapsedTimer timer;
    timer.start();
    qsizetype  searches    = 0;
    const bool mayComplete = !_data->completionBytes
                          || std::any_of(input.bytes.cbegin(), input.bytes.cend(), [this](char byte) {
                                 return (*_data->completionBytes)[static_cast<unsigned char>(byte)];
                             });
    while (mayComplete && error.isEmpty() && consumed < _buffer.size())
    {
        if (++searches > _limits.maxSteps || timer.elapsed() >= 20)
        {
            error = "Pattern framer search budget exceeded";
            break;
        }
        const auto* subject = reinterpret_cast<PCRE2_SPTR>(_buffer.constData() + consumed);
        const auto  length  = _buffer.size() - consumed;
        const int   result  = pcre2_jit_match(_data->code,
                                              subject,
                                              size_t(length),
                                              consumed == 0 ? size_t(_searchOffset) : 0,
                                              PCRE2_PARTIAL_HARD,
                                              _data->match,
                                              _data->context);
        if (result == PCRE2_ERROR_PARTIAL)
        {
            // Keep the prefix for lookbehind, but resume at the first viable start.
            _searchOffset = _data->resumePartial ? qsizetype(pcre2_get_ovector_pointer(_data->match)[0]) : 0;
            break;
        }
        if (result == PCRE2_ERROR_NOMATCH)
        {
            _searchOffset = 0;
            break;
        }
        if (result < 0)
        {
            error = QString("Pattern matching failed: %1").arg(regexError(result));
            break;
        }
        const auto* offsets = pcre2_get_ovector_pointer(_data->match);
        if (offsets[1] <= offsets[0] || offsets[1] > size_t(length))
        {
            error = "Pattern produced an empty or invalid frame";
            break;
        }
        if (outputs.size() >= _limits.maxChunks)
        {
            error = "Pattern framer output exceeds runtime chunk limits";
            break;
        }
        QByteArray bytes;
        for (const auto& part : _data->parts)
        {
            const char* source = part.literal.constData();
            qsizetype   size   = part.literal.size();
            if (part.capture >= 0)
            {
                const auto start = offsets[2 * part.capture];
                const auto end   = offsets[2 * part.capture + 1];
                if (start == PCRE2_UNSET)
                {
                    continue;
                }
                source = reinterpret_cast<const char*>(subject) + start;
                size   = qsizetype(end - start);
            }
            if (size > _limits.maxBytes - outputBytes - bytes.size())
            {
                error = "Pattern framer output exceeds runtime byte limits";
                break;
            }
            bytes.append(source, size);
        }
        if (!error.isEmpty())
        {
            break;
        }
        _searchOffset  = 0;
        consumed      += qsizetype(offsets[1]);
        outputBytes   += bytes.size();
        outputs.append({std::move(bytes), 0});
    }
    if (error.isEmpty())
    {
        _buffer.remove(0, consumed);
        if (_buffer.size() > _limits.maxBytes - outputBytes)
        {
            error = "Pattern framer buffered input and output exceed runtime byte limits";
        }
    }
    if (!error.isEmpty())
    {
        _stopped = true;
        _buffer.clear();
        _searchOffset = 0;
    }
    QPointer<PatternFramer> guard(this);
    if (error.isEmpty() && callbacks.output)
    {
        for (auto& output : outputs)
        {
            callbacks.output(std::move(output));
            if (!guard || guard->_stopped)
            {
                return AcceptResult::Accepted;
            }
        }
    }
    _busy = false;
    if (callbacks.completed)
    {
        callbacks.completed(std::move(error));
    }
    return AcceptResult::Accepted;
}
void PatternFramer::cancel(std::function<void()> quiescent)
{
    _stopped = true;
    _busy    = false;
    _buffer.clear();
    _searchOffset = 0;
    if (quiescent)
    {
        quiescent();
    }
}
QByteArray PatternFramer::schema()
{
    return R"(version: 1
type: termaster.settings-schema
description: Binary regular expression stream framing settings
content:
  type: object
  x-groups:
    - {id: framing, title: Framing, order: 0}
  properties:
    pattern:
      type: string
      minLength: 1
      maxLength: 4096
      default: '(?<payload>[^\r\n]+)\r?\n'
      title: Pattern
      description: Binary PCRE2 pattern; a completed full match consumes the frame and preceding noise.
      x-group: framing
      x-order: 1
    outputTemplate:
      type: string
      maxLength: 16384
      default: ''
      title: Output
      description: 'Empty emits the full match. Captures: ${0}, ${1}, ${name}; $$ is a dollar. Escapes: \\, \r, \n, \t, \0, \xHH.'
      x-group: framing
      x-order: 2
)";
}
