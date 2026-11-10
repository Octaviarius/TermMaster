#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#ifdef TERMASTER_HAVE_VECTORSCAN
#include <hs.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Clock                         = std::chrono::steady_clock;
constexpr std::size_t   BufferLimit = 1024 * 1024;
constexpr std::uint64_t HashSeed    = 14695981039346656037ULL;
constexpr char          Pattern[]   = R"(\xA5\x5A(?<payload>[^\xFF]{32,})\xFF\x00)";

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

std::uint64_t hashByte(std::uint64_t hash, unsigned char value)
{
    return (hash ^ value) * 1099511628211ULL;
}

std::uint64_t hashOffset(std::uint64_t hash, std::uint64_t value)
{
    for (unsigned int shift = 0; shift < 64; shift += 8)
    {
        hash = hashByte(hash, static_cast<unsigned char>(value >> shift));
    }
    return hash;
}

struct Options
{
    std::string engine     = "pcre2";
    std::string scenario   = "frames";
    std::size_t chunk      = 64;
    std::size_t mib        = 16;
    std::size_t iterations = 1;
    std::size_t payload    = 64;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; index += 2)
    {
        require(index + 1 < argc, "Each option requires a value");
        const std::string key   = argv[index];
        const std::string value = argv[index + 1];
        if (key == "--engine")
        {
            options.engine = value;
        }
        else if (key == "--scenario")
        {
            options.scenario = value;
        }
        else
        {
            require(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos,
                    "Invalid positive integer: " + value);
            const auto number = std::stoull(value);
            require(number > 0 && number <= std::numeric_limits<std::size_t>::max(), "Integer outside supported range");
            if (key == "--chunk")
            {
                options.chunk = static_cast<std::size_t>(number);
            }
            else if (key == "--mib")
            {
                options.mib = static_cast<std::size_t>(number);
            }
            else if (key == "--payload")
            {
                options.payload = static_cast<std::size_t>(number);
            }
            else if (key == "--iterations")
            {
                options.iterations = static_cast<std::size_t>(number);
            }
            else
            {
                throw std::runtime_error("Unknown option: " + key);
            }
        }
    }
    require(options.engine == "pcre2" || options.engine == "pcre2-jit" || options.engine == "pcre2-dfa"
                || options.engine == "vectorscan" || options.engine == "vectorscan-pcre2",
            "Unknown engine");
    require(options.scenario == "frames" || options.scenario == "noise" || options.scenario == "near-miss",
            "Unknown scenario");
    require(options.chunk <= BufferLimit, "Chunk exceeds bounded input buffer");
    require(options.payload >= 32 && options.payload <= 65536, "Payload must be between 32 and 65536 bytes");
    require(options.mib <= 4096, "Corpus limit is 4096 MiB");
    require(options.iterations <= 10000, "Iteration limit is 10000");
    return options;
}

struct Corpus
{
    std::vector<unsigned char> bytes;
    std::vector<std::uint64_t> starts;
    std::size_t                payloadSize     = 64;
    std::uint64_t              rangeChecksum   = HashSeed;
    std::uint64_t              payloadChecksum = HashSeed;
};

Corpus makeCorpus(const Options& options)
{
    Corpus corpus;
    corpus.payloadSize = options.payload;
    const auto size    = options.mib * 1024 * 1024;
    corpus.bytes.reserve(size);
    std::uint32_t random   = 0x31415926;
    auto          nextByte = [&]() {
        random     ^= random << 13;
        random     ^= random >> 17;
        random     ^= random << 5;
        auto value  = static_cast<unsigned char>(random);
        // A5 is reserved for frame/candidate starts. FF remains in ordinary noise.
        return value == 0xA5 ? static_cast<unsigned char>(0xA4) : value;
    };
    while (corpus.bytes.size() < size)
    {
        if (options.scenario == "frames" && size - corpus.bytes.size() >= corpus.payloadSize + 4)
        {
            const auto start = corpus.bytes.size();
            corpus.starts.push_back(start);
            corpus.rangeChecksum = hashOffset(corpus.rangeChecksum, start);
            corpus.rangeChecksum = hashOffset(corpus.rangeChecksum, start + corpus.payloadSize + 4);
            corpus.bytes.push_back(0xA5);
            corpus.bytes.push_back(0x5A);
            for (std::size_t index = 0; index < corpus.payloadSize; ++index)
            {
                auto value = index == 0 ? static_cast<unsigned char>(0) : nextByte();
                if (value == 0xFF)
                {
                    value = 0xFE;
                }
                corpus.bytes.push_back(value);
                corpus.payloadChecksum = hashByte(corpus.payloadChecksum, value);
            }
            corpus.bytes.push_back(0xFF);
            corpus.bytes.push_back(0);
        }
        else if (options.scenario == "near-miss" && size - corpus.bytes.size() >= 100)
        {
            corpus.bytes.push_back(0xA5);
            corpus.bytes.push_back(0x5A);
            for (std::size_t index = 0; index < 96; ++index)
            {
                auto value = nextByte();
                corpus.bytes.push_back(value == 0xFF ? 0xFE : value);
            }
            corpus.bytes.push_back(0xFF);
            corpus.bytes.push_back(1);
        }
        const auto noise = std::min<std::size_t>(31 + random % 97, size - corpus.bytes.size());
        for (std::size_t index = 0; index < noise; ++index)
        {
            corpus.bytes.push_back(nextByte());
        }
    }
    return corpus;
}

struct Results
{
    std::uint64_t frames              = 0;
    std::uint64_t capturedBytes       = 0;
    std::uint64_t rangeChecksum       = HashSeed;
    std::uint64_t payloadChecksum     = HashSeed;
    std::uint64_t scannedSubjectBytes = 0;
    std::size_t   bufferPeak          = 0;
    std::size_t   bufferCapacity      = 0;
};

// Validation is part of each timed callback, equally for all capture engines.
void record(Results&             result,
            const Corpus&        corpus,
            std::uint64_t        start,
            std::uint64_t        end,
            const unsigned char* payload     = nullptr,
            std::size_t          payloadSize = 0)
{
    require(result.frames < corpus.starts.size() && start == corpus.starts[result.frames]
                && end == start + corpus.payloadSize + 4,
            "Detected frame range differs from generated oracle");
    ++result.frames;
    result.rangeChecksum = hashOffset(result.rangeChecksum, start);
    result.rangeChecksum = hashOffset(result.rangeChecksum, end);
    if (payload)
    {
        require(payloadSize == corpus.payloadSize
                    && std::memcmp(payload, corpus.bytes.data() + start + 2, payloadSize) == 0,
                "Captured payload differs from generated bytes");
        result.capturedBytes += payloadSize;
        for (std::size_t index = 0; index < payloadSize; ++index)
        {
            result.payloadChecksum = hashByte(result.payloadChecksum, payload[index]);
        }
    }
}

class Pcre
{
public:
    std::size_t codeBytes         = 0;
    std::size_t jitBytes          = 0;
    std::size_t matchDataBytes    = 0;
    std::size_t heapFramesBytes   = 0;
    std::size_t dfaWorkspaceBytes = 0;
    std::size_t dfaMatchDataBytes = 0;

    explicit Pcre(bool jit, bool dfa = false)
    {
        int        error  = 0;
        PCRE2_SIZE offset = 0;
        _code             = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(Pattern),
                                          PCRE2_ZERO_TERMINATED,
                                          0,
                                          &error,
                                          &offset,
                                          nullptr);
        require(_code != nullptr, "PCRE2 compilation failed: " + std::to_string(error));
        _match   = pcre2_match_data_create_from_pattern(_code, nullptr);
        _context = pcre2_match_context_create(nullptr);
        require(_match && _context, "PCRE2 allocation failed");
        pcre2_set_match_limit(_context, 1000000);
        pcre2_set_heap_limit(_context, 8192);
        if (jit)
        {
            require(pcre2_jit_compile(_code, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_HARD) == 0,
                    "PCRE2 JIT compilation unavailable");
        }
        pcre2_pattern_info(_code, PCRE2_INFO_SIZE, &codeBytes);
        pcre2_pattern_info(_code, PCRE2_INFO_JITSIZE, &jitBytes);
        matchDataBytes = pcre2_get_match_data_size(_match);
        if (dfa)
        {
            _dfaMatch = pcre2_match_data_create(32, nullptr);
            require(_dfaMatch != nullptr, "PCRE2 DFA allocation failed");
            _dfaWorkspace.resize(4096);
            dfaWorkspaceBytes = _dfaWorkspace.size() * sizeof(int);
            dfaMatchDataBytes = pcre2_get_match_data_size(_dfaMatch);
        }
    }

    ~Pcre()
    {
        pcre2_match_context_free(_context);
        pcre2_match_data_free(_dfaMatch);
        pcre2_match_data_free(_match);
        pcre2_code_free(_code);
    }

    Pcre(const Pcre&)            = delete;
    Pcre& operator=(const Pcre&) = delete;

    int match(const unsigned char* data, std::size_t size, std::size_t start, bool partial)
    {
        const auto status = pcre2_match(_code, data, size, start, partial ? PCRE2_PARTIAL_HARD : 0, _match, _context);
        if (status < 0 && status != PCRE2_ERROR_NOMATCH && status != PCRE2_ERROR_PARTIAL)
        {
            throw std::runtime_error("PCRE2 matching failed: " + std::to_string(status));
        }
        return status;
    }

    void updateMemoryUsage()
    {
        heapFramesBytes = pcre2_get_match_data_heapframes_size(_match);
    }

    PCRE2_SIZE* offsets()
    {
        return pcre2_get_ovector_pointer(_match);
    }

    int dfaMatch(const unsigned char* data, std::size_t size, std::size_t start, bool restart)
    {
        const auto status = pcre2_dfa_match(_code,
                                            data,
                                            size,
                                            start,
                                            PCRE2_PARTIAL_HARD | (restart ? PCRE2_DFA_RESTART : 0),
                                            _dfaMatch,
                                            _context,
                                            _dfaWorkspace.data(),
                                            _dfaWorkspace.size());
        if (status < 0 && status != PCRE2_ERROR_NOMATCH && status != PCRE2_ERROR_PARTIAL)
        {
            throw std::runtime_error("PCRE2 DFA matching failed: " + std::to_string(status));
        }
        return status;
    }

    PCRE2_SIZE* dfaOffsets()
    {
        return pcre2_get_ovector_pointer(_dfaMatch);
    }

private:
    pcre2_code*          _code     = nullptr;
    pcre2_match_data*    _match    = nullptr;
    pcre2_match_context* _context  = nullptr;
    pcre2_match_data*    _dfaMatch = nullptr;
    std::vector<int>     _dfaWorkspace;
};

Results runDfa(Pcre& pcre, const Corpus& corpus, std::size_t chunk)
{
    Results                    result;
    std::vector<unsigned char> buffer;
    buffer.reserve(chunk + 128);
    std::uint64_t base      = 0;
    std::size_t   candidate = 0;
    bool          partial   = false;
    auto          capture   = [&](std::size_t start, std::size_t end) {
        require(end > start && end <= buffer.size(), "Invalid DFA candidate range");
        const auto length           = end - start;
        result.scannedSubjectBytes += length;
        require(pcre.match(buffer.data() + start, length, 0, false) >= 2,
                "DFA candidate rejected by PCRE2 capture matcher");
        const auto offsets = pcre.offsets();
        require(offsets[0] == 0 && offsets[1] == length, "DFA capture boundaries differ");
        record(result, corpus, base + start, base + end, buffer.data() + start + offsets[2], offsets[3] - offsets[2]);
    };
    for (std::size_t position = 0; position < corpus.bytes.size(); position += chunk)
    {
        const auto length       = std::min(chunk, corpus.bytes.size() - position);
        const auto previousSize = buffer.size();
        require(previousSize + length <= BufferLimit + chunk, "PCRE2 DFA buffer limit exceeded");
        buffer.insert(buffer.end(), corpus.bytes.begin() + position, corpus.bytes.begin() + position + length);
        result.bufferPeak     = std::max(result.bufferPeak, buffer.size());
        result.bufferCapacity = std::max(result.bufferCapacity, buffer.capacity());
        std::size_t cursor    = 0;
        if (partial)
        {
            result.scannedSubjectBytes += length;
            const auto status           = pcre.dfaMatch(buffer.data() + previousSize, length, 0, true);
            if (status >= 0)
            {
                const auto end = previousSize + pcre.dfaOffsets()[1];
                capture(candidate, end);
                cursor  = end;
                partial = false;
            }
            else if (status == PCRE2_ERROR_NOMATCH)
            {
                // Restart state represents only one original start. Recover later
                // starts with a fresh bumpalong over all retained candidate bytes.
                cursor  = candidate + 1;
                partial = false;
            }
        }
        if (!partial)
        {
            while (cursor < buffer.size())
            {
                result.scannedSubjectBytes += buffer.size() - cursor;
                const auto status           = pcre.dfaMatch(buffer.data(), buffer.size(), cursor, false);
                const auto offsets          = pcre.dfaOffsets();
                if (status >= 0)
                {
                    const auto start = offsets[0];
                    const auto end   = offsets[1];
                    capture(start, end);
                    cursor = end;
                }
                else if (status == PCRE2_ERROR_PARTIAL)
                {
                    candidate = offsets[0];
                    partial   = true;
                    break;
                }
                else
                {
                    cursor = buffer.size();
                }
            }
        }
        const auto consumed = partial ? candidate : cursor;
        if (consumed)
        {
            buffer.erase(buffer.begin(), buffer.begin() + consumed);
            base += consumed;
            if (partial)
            {
                candidate -= consumed;
            }
        }
    }
    return result;
}

Results runPcre(Pcre& pcre, const Corpus& corpus, std::size_t chunk)
{
    Results                    result;
    std::vector<unsigned char> buffer;
    buffer.reserve(chunk + corpus.payloadSize + 4);
    std::uint64_t base = 0;
    for (std::size_t position = 0; position < corpus.bytes.size(); position += chunk)
    {
        const auto length = std::min(chunk, corpus.bytes.size() - position);
        require(buffer.size() + length <= BufferLimit + chunk, "PCRE2 buffer limit exceeded");
        buffer.insert(buffer.end(), corpus.bytes.begin() + position, corpus.bytes.begin() + position + length);
        result.bufferPeak     = std::max(result.bufferPeak, buffer.size());
        result.bufferCapacity = std::max(result.bufferCapacity, buffer.capacity());
        std::size_t consumed  = 0;
        while (consumed < buffer.size())
        {
            result.scannedSubjectBytes += buffer.size() - consumed;
            const auto status           = pcre.match(buffer.data(), buffer.size(), consumed, true);
            const auto offsets          = pcre.offsets();
            if (status >= 0)
            {
                require(status >= 2, "PCRE2 payload group missing");
                record(result,
                       corpus,
                       base + offsets[0],
                       base + offsets[1],
                       buffer.data() + offsets[2],
                       offsets[3] - offsets[2]);
                consumed = offsets[1];
            }
            else if (status == PCRE2_ERROR_PARTIAL)
            {
                consumed = offsets[0];
                break;
            }
            else
            {
                consumed = buffer.size();
            }
        }
        if (consumed)
        {
            buffer.erase(buffer.begin(), buffer.begin() + consumed);
            base += consumed;
        }
    }
    return result;
}

#ifdef TERMASTER_HAVE_VECTORSCAN
class Vectorscan
{
public:
    std::size_t databaseBytes = 0;
    std::size_t scratchBytes  = 0;
    std::size_t streamBytes   = 0;

    Vectorscan()
    {
        hs_compile_error_t* error   = nullptr;
        const auto          status  = hs_compile(R"(\xA5\x5A[^\xFF]{32,}\xFF\x00)",
                                                 HS_FLAG_SOM_LEFTMOST,
                                                 HS_MODE_STREAM | HS_MODE_SOM_HORIZON_LARGE,
                                                 nullptr,
                                                 &_database,
                                                 &error);
        const std::string   message = error ? error->message : "unknown error";
        hs_free_compile_error(error);
        require(status == HS_SUCCESS, "Vectorscan compilation failed: " + message);
        require(hs_alloc_scratch(_database, &_scratch) == HS_SUCCESS, "Vectorscan scratch allocation failed");
        hs_database_size(_database, &databaseBytes);
        hs_scratch_size(_scratch, &scratchBytes);
        hs_stream_size(_database, &streamBytes);
    }

    ~Vectorscan()
    {
        hs_free_scratch(_scratch);
        hs_free_database(_database);
    }

    Vectorscan(const Vectorscan&)            = delete;
    Vectorscan& operator=(const Vectorscan&) = delete;

    Results run(const Corpus& corpus, std::size_t chunk, Pcre* pcre)
    {
        Results  result;
        Callback context {&result, &corpus, pcre, {}, 0, 0, {}};
        if (pcre)
        {
            context.buffer.reserve(chunk + 128);
        }
        hs_stream_t* stream = nullptr;
        require(hs_open_stream(_database, 0, &stream) == HS_SUCCESS, "Vectorscan stream allocation failed");
        for (std::size_t position = 0; position < corpus.bytes.size(); position += chunk)
        {
            const auto length = std::min(chunk, corpus.bytes.size() - position);
            if (pcre)
            {
                require(context.buffer.size() + length <= BufferLimit + chunk,
                        "Vectorscan capture buffer limit exceeded");
                context.buffer.insert(context.buffer.end(),
                                      corpus.bytes.begin() + position,
                                      corpus.bytes.begin() + position + length);
                result.bufferPeak     = std::max(result.bufferPeak, context.buffer.size());
                result.bufferCapacity = std::max(result.bufferCapacity, context.buffer.capacity());
            }
            result.scannedSubjectBytes += length;
            const auto status           = hs_scan_stream(stream,
                                                         reinterpret_cast<const char*>(corpus.bytes.data() + position),
                                                         static_cast<unsigned int>(length),
                                                         0,
                                                         _scratch,
                                                         &_onMatch,
                                                         &context);
            if (status != HS_SUCCESS)
            {
                hs_close_stream(stream, _scratch, nullptr, nullptr);
                throw std::runtime_error(context.error.empty() ? "Vectorscan scan failed" : context.error);
            }
            // Retire completed frames once per input packet, never partial prefixes.
            if (pcre && context.retired > context.base)
            {
                const auto removed = static_cast<std::size_t>(context.retired - context.base);
                context.buffer.erase(context.buffer.begin(), context.buffer.begin() + removed);
                context.base = context.retired;
            }
            // This corpus protocol limits payloads to 64 KiB. Retain a full possible
            // candidate while discarding older unmatched noise in amortized batches.
            if (pcre && context.buffer.size() > BufferLimit)
            {
                const auto removed = context.buffer.size() - (65536 + 4);
                context.buffer.erase(context.buffer.begin(), context.buffer.begin() + removed);
                context.base += removed;
            }
        }
        const auto status = hs_close_stream(stream, _scratch, &_onMatch, &context);
        require(status == HS_SUCCESS && context.error.empty(), "Vectorscan close failed: " + context.error);
        return result;
    }

private:
    struct Callback
    {
        Results*                   result;
        const Corpus*              corpus;
        Pcre*                      pcre;
        std::vector<unsigned char> buffer;
        std::uint64_t              base    = 0;
        std::uint64_t              retired = 0;
        std::string                error;
    };

    hs_database_t* _database = nullptr;
    hs_scratch_t*  _scratch  = nullptr;

    static int _onMatch(unsigned int, unsigned long long start, unsigned long long end, unsigned int, void* data)
    {
        auto& context = *static_cast<Callback*>(data);
        try
        {
            if (context.pcre)
            {
                require(start >= context.base && end >= start && end <= context.base + context.buffer.size(),
                        "Vectorscan capture window unavailable");
                const auto subject                   = context.buffer.data() + (start - context.base);
                const auto length                    = static_cast<std::size_t>(end - start);
                context.result->scannedSubjectBytes += length;
                require(context.pcre->match(subject, length, 0, false) >= 2, "Vectorscan candidate rejected by PCRE2");
                const auto offsets = context.pcre->offsets();
                require(offsets[0] == 0 && offsets[1] == length, "Candidate boundaries differ");
                record(*context.result, *context.corpus, start, end, subject + offsets[2], offsets[3] - offsets[2]);
                context.retired = end;
            }
            else
            {
                record(*context.result, *context.corpus, start, end);
            }
            return 0;
        }
        catch (const std::exception& error)
        {
            context.error = error.what();
            return 1;
        }
    }
};
#endif

void validate(const Results& result, const Corpus& corpus, bool capture)
{
    require(result.frames == corpus.starts.size() && result.rangeChecksum == corpus.rangeChecksum,
            "Frame count/checksum validation failed");
    if (capture)
    {
        require(result.capturedBytes == corpus.starts.size() * corpus.payloadSize
                    && result.payloadChecksum == corpus.payloadChecksum,
                "Payload count/checksum validation failed");
    }
}

void verifyDfaRecovery(Pcre& pcre)
{
    Corpus corpus;
    corpus.payloadSize = 32;
    corpus.bytes       = {0xA5, 0x5A};
    corpus.bytes.insert(corpus.bytes.end(), 96, 0x33);
    corpus.bytes.push_back(0xFF);
    corpus.bytes.push_back(1);
    const auto start = corpus.bytes.size();
    corpus.starts.push_back(start);
    corpus.bytes.push_back(0xA5);
    corpus.bytes.push_back(0x5A);
    corpus.bytes.insert(corpus.bytes.end(), corpus.payloadSize, 0);
    corpus.bytes.push_back(0xFF);
    corpus.bytes.push_back(0);
    corpus.rangeChecksum = hashOffset(hashOffset(HashSeed, start), corpus.bytes.size());
    for (std::size_t index = 0; index < corpus.payloadSize; ++index)
    {
        corpus.payloadChecksum = hashByte(corpus.payloadChecksum, 0);
    }
    for (const std::size_t chunk : {1U, 64U, 128U})
    {
        validate(runDfa(pcre, corpus, chunk), corpus, true);
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto            options       = parseOptions(argc, argv);
        const auto            corpus        = makeCorpus(options);
        const bool            capture       = options.engine != "vectorscan";
        const bool            useVectorscan = options.engine.starts_with("vectorscan");
        const auto            compileStart  = Clock::now();
        std::unique_ptr<Pcre> pcre;
        if (capture)
        {
            pcre = std::make_unique<Pcre>(options.engine != "pcre2", options.engine == "pcre2-dfa");
        }
        std::size_t databaseBytes = 0;
        std::size_t scratchBytes  = 0;
        std::size_t streamBytes   = 0;
#ifdef TERMASTER_HAVE_VECTORSCAN
        std::unique_ptr<Vectorscan> vectorscan;
        if (useVectorscan)
        {
            vectorscan    = std::make_unique<Vectorscan>();
            databaseBytes = vectorscan->databaseBytes;
            scratchBytes  = vectorscan->scratchBytes;
            streamBytes   = vectorscan->streamBytes;
        }
#else
        require(!useVectorscan, "Vectorscan was not compiled into this executable");
#endif
        const auto compileMs = std::chrono::duration<double, std::milli>(Clock::now() - compileStart).count();
        if (options.engine == "pcre2-dfa")
        {
            verifyDfaRecovery(*pcre);
        }
        Results result;
        double  seconds = 0;
        for (std::size_t iteration = 0; iteration < options.iterations; ++iteration)
        {
            const auto start = Clock::now();
#ifdef TERMASTER_HAVE_VECTORSCAN
            auto current = useVectorscan ? vectorscan->run(corpus, options.chunk, pcre.get()) :
                                           (options.engine == "pcre2-dfa" ? runDfa(*pcre, corpus, options.chunk) :
                                                                            runPcre(*pcre, corpus, options.chunk));
#else
            auto current = (options.engine == "pcre2-dfa" ? runDfa(*pcre, corpus, options.chunk) :
                                                            runPcre(*pcre, corpus, options.chunk));
#endif
            seconds += std::chrono::duration<double>(Clock::now() - start).count();
            if (pcre)
            {
                pcre->updateMemoryUsage();
            }
            validate(current, corpus, capture);
            result.frames              += current.frames;
            result.capturedBytes       += current.capturedBytes;
            result.scannedSubjectBytes += current.scannedSubjectBytes;
            result.bufferPeak           = std::max(result.bufferPeak, current.bufferPeak);
            result.bufferCapacity       = std::max(result.bufferCapacity, current.bufferCapacity);
            result.rangeChecksum        = current.rangeChecksum;
            result.payloadChecksum      = current.payloadChecksum;
        }
        std::size_t privateBytes   = 0;
        std::size_t peakWorkingSet = 0;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX memory {};
        require(GetProcessMemoryInfo(GetCurrentProcess(),
                                     reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                     sizeof(memory))
                    != 0,
                "GetProcessMemoryInfo failed");
        privateBytes   = memory.PrivateUsage;
        peakWorkingSet = memory.PeakWorkingSetSize;
#endif
        const auto codeBytes = pcre ? pcre->codeBytes : 0;
        const auto jitBytes  = pcre ? pcre->jitBytes : 0;
        std::cout.precision(12);
        std::cout << "{\"engine\":\"" << options.engine << "\",\"scenario\":\"" << options.scenario
                  << "\",\"chunk\":" << options.chunk << ",\"payload\":" << options.payload
                  << ",\"iterations\":" << options.iterations
                  << ",\"bytes\":" << corpus.bytes.size() * options.iterations << ",\"frames\":" << result.frames
                  << ",\"captured_bytes\":" << result.capturedBytes
                  << ",\"checksum\":" << (capture ? result.payloadChecksum : result.rangeChecksum)
                  << ",\"range_checksum\":" << result.rangeChecksum << ",\"seconds\":" << seconds
                  << ",\"compile_ms\":" << compileMs
                  << ",\"engine_static_bytes\":" << codeBytes + jitBytes + databaseBytes
                  << ",\"pcre2_code_bytes\":" << codeBytes << ",\"pcre2_jit_bytes\":" << jitBytes
                  << ",\"pcre2_match_data_bytes\":" << (pcre ? pcre->matchDataBytes : 0)
                  << ",\"pcre2_heap_frames_bytes\":" << (pcre ? pcre->heapFramesBytes : 0)
                  << ",\"dfa_workspace_bytes\":" << (pcre ? pcre->dfaWorkspaceBytes : 0)
                  << ",\"dfa_match_data_bytes\":" << (pcre ? pcre->dfaMatchDataBytes : 0)
                  << ",\"vectorscan_database_bytes\":" << databaseBytes
                  << ",\"vectorscan_scratch_bytes\":" << scratchBytes << ",\"vectorscan_stream_bytes\":" << streamBytes
                  << ",\"buffer_peak\":" << result.bufferPeak << ",\"buffer_capacity_bytes\":" << result.bufferCapacity
                  << ",\"scanned_subject_bytes\":" << result.scannedSubjectBytes
                  << ",\"corpus_bytes\":" << corpus.bytes.size()
                  << ",\"oracle_capacity_bytes\":" << corpus.starts.capacity() * sizeof(std::uint64_t)
                  << ",\"process_private_bytes\":" << privateBytes << ",\"peak_working_set_bytes\":" << peakWorkingSet
                  << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RegexBenchmark: " << error.what() << '\n';
        return 1;
    }
}
