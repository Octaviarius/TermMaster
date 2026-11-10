#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <memory>
#include <type_traits>
#include <variant>
#include <vector>

class CompiledFormat;

/** Typed names for prepared byte formatting. Finish schema construction before sharing it. */
class FormatSchema
{
public:
    class Values;

    template <typename T>
    class Slot
    {
    public:
        Slot() = default;

    private:
        friend class FormatSchema;
        friend class Values;
        friend class CompiledFormat;

        std::shared_ptr<const void> _identity;
        qsizetype                   _index = -1;

        Slot(std::shared_ptr<const void> identity, qsizetype index) : _identity(std::move(identity)), _index(index)
        {
        }
    };

    /** Render-local values. Byte views borrow storage until the last synchronous render. */
    class Values
    {
    public:
        template <typename T>
        bool set(const Slot<T>& slot, std::type_identity_t<T> value, QString* error = nullptr)
        {
            return _set(slot._identity, slot._index, Value(std::move(value)), error);
        }

        /** Releases values, including borrowed byte views, after an invocation. */
        void clear();

    private:
        friend class FormatSchema;
        friend class CompiledFormat;

        using Value = std::variant<std::monostate, qint64, quint64, double, bool, QString, QByteArrayView>;

        std::shared_ptr<const void> _identity;
        std::vector<Value>          _values;
        std::vector<QByteArray>     _utf8;

        Values(std::shared_ptr<const void> identity, qsizetype size);
        bool _set(const std::shared_ptr<const void>& identity, qsizetype index, Value value, QString* error);
    };

    FormatSchema();
    ~FormatSchema();
    Q_DISABLE_COPY_MOVE(FormatSchema)

    /** Throws std::invalid_argument for invalid/duplicate names, limits or a sealed schema. */
    template <typename T>
    Slot<T> add(const QString& name)
    {
        return Slot<T>(_identity(), _add(name, _kind<T>()));
    }

    /** Seals the schema. Only slots used by a compiled plan need values. */
    Values createValues() const;

private:
    friend class CompiledFormat;

    enum class Kind
    {
        Signed,
        Unsigned,
        Floating,
        Boolean,
        Text,
        Bytes
    };
    struct Data;

    std::shared_ptr<Data> _data;

    template <typename T>
    static constexpr Kind _kind()
    {
        if constexpr (std::is_same_v<T, qint64>)
        {
            return Kind::Signed;
        }
        else if constexpr (std::is_same_v<T, quint64>)
        {
            return Kind::Unsigned;
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            return Kind::Floating;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return Kind::Boolean;
        }
        else if constexpr (std::is_same_v<T, QString>)
        {
            return Kind::Text;
        }
        else if constexpr (std::is_same_v<T, QByteArrayView>)
        {
            return Kind::Bytes;
        }
        else
        {
            static_assert(!std::is_same_v<T, T>, "Unsupported format slot type");
        }
    }

    std::shared_ptr<const void> _identity() const;
    qsizetype                   _add(const QString& name, Kind kind);
};
