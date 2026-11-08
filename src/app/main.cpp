#include <QApplication>
#include <QDir>
#include <QMap>
#include <QStyle>
#include <core/RandomNames.h>
#include <core/plugin/PluginManager.h>
#include <logic/GeneralLogic.h>
#include <managers/CommandManager.h>
#include <managers/SettingsManager.h>
#include <managers/TerminalManager.h>
#include <managers/WindowManager.h>
#include <terminal/Emulator.h>
#include <widgets/TermWidget/TermWidget.h>

/////////////////////////////////////////////////////////////////////////////////

template <size_t I, typename... Types>
struct index_of__impl
{
    static_assert(sizeof...(Types) > I, "Index out of range");
};

template <size_t I, typename A, typename B, typename... Rest>
struct index_of__impl<I, A, B, Rest...> : index_of__impl<I + 1, A, Rest...>
{
};

template <size_t I, typename A, typename... Rest>
struct index_of__impl<I, A, A, Rest...>
{
    static constexpr size_t value = I;
};

template <typename T, typename... Types>
using index_of = index_of__impl<0, T, Types...>;

/////////////////////////////////////////////////////////////////////////////////

template <size_t I, typename... Types>
class type_at_impl
{
    static_assert(sizeof...(Types) > 0, "Index out of range in types set");
};

template <size_t I, typename T, typename... Types>
struct type_at_impl<I, T, Types...> : type_at_impl<I - 1, Types...>
{
};

template <typename T, typename... Types>
struct type_at_impl<0, T, Types...>
{
    using type = T;
};

template <size_t N, typename... Types>
using type_at = type_at_impl<N, Types...>;

/////////////////////////////////////////////////////////////////////////////////

template <typename T1, typename T2>
struct type_greaterequal : std::integral_constant<bool, (sizeof(T1) >= sizeof(T2))>
{
};

/////////////////////////////////////////////////////////////////////////////////

template <typename E, E Ok>
class Result
{
public:
    using EnumType = E;

    static constexpr E okValue()
    {
        return Ok;
    }

    template <typename ValueType>
    static constexpr ValueType adapt(E value)
    {
        static_assert(sizeof(ValueType) >= sizeof(E));
        return static_cast<ValueType>(value);
    }
};

template <typename V, typename C, typename... Enums>
class Results
{
public:
    using ValueType = V;
    using CatType   = C;

    static_assert(std::conjunction_v<type_greaterequal<V, typename Enums::EnumType>...>,
                  "All EnumTypes must be smaller than ValueType");

    Results() : _value(0), _okValue(0), _category(CatType(-1))
    {
    }

    template <typename E,
              typename = std::enable_if_t<std::disjunction_v<std::is_same<E, typename Enums::EnumType>...>, E>>
    Results(E value)
    {
        *this = value;
    }

    constexpr ValueType value() const
    {
        return _value;
    }

    constexpr CatType category() const
    {
        return _category;
    }

    constexpr bool isOk() const
    {
        return _okValue == _value && isValid();
    }

    constexpr bool isValid() const
    {
        return _category != CatType(-1);
    }

    constexpr ValueType okValue() const
    {
        return _okValue;
    }

    constexpr operator ValueType() const
    {
        return _value;
    }

    template <typename E,
              typename = std::enable_if_t<std::disjunction_v<std::is_same<E, typename Enums::EnumType>...>, E>>
    Results& operator=(E value)
    {
        using Enum = type_at<index_of<E, typename Enums::EnumType...>::value, Enums...>;

        _value    = ValueType(value);
        _category = index_of<E, typename Enums::EnumType...>::value;
        _okValue  = ValueType(Enum::type::okValue());
        return *this;
    }

    template <typename E,
              typename = std::enable_if_t<std::disjunction_v<std::is_same<E, typename Enums::EnumType>...>, E>>
    bool operator==(E value) const
    {
        using Enum = type_at<index_of<E, typename Enums::EnumType...>::value, Enums...>;
        return (_value == ValueType(value)) && (_category == index_of<E, typename Enums::EnumType...>::value);
    }

    template <typename E,
              typename = std::enable_if_t<std::disjunction_v<std::is_same<E, typename Enums::EnumType>...>, E>>
    bool operator!=(E value) const
    {
        using Enum = type_at<index_of<E, typename Enums::EnumType...>::value, Enums...>;
        return (_value != ValueType(value)) || (_category != index_of<E, typename Enums::EnumType...>::value);
    }

protected:
    ValueType _value;
    ValueType _okValue;
    CatType   _category;
};

/////////////////////////////////////////////////////////////////////////////////

enum class E1
{
    Ok,
    Fail,
};

enum class E2
{
    Timeout,
    Failed,
    Ok,
};

enum class E3
{
    Line1,
    Line2,
    Line3,
};

using AppResult = Results<int, int8_t, Result<E1, E1::Ok>, Result<E2, E2::Ok>>;

int main(int argc, char* argv[])
{
    type_at<1, char, short, long, void*, const char*>::type typeat;

    auto a = index_of<short, char, int, short>::value;

    AppResult res = E1::Fail;
    qDebug() << QString("Valid=%1, ok=%2").arg(res.isValid()).arg(res.isOk());

    res = E1::Ok;
    qDebug() << QString("Valid=%1, ok=%2").arg(res.isValid()).arg(res.isOk());

    res = E2::Timeout;
    qDebug() << QString("Valid=%1, ok=%2").arg(res.isValid()).arg(res.isOk());
    qDebug() << QString("%1 equal %2 = %3").arg(res.value()).arg((int)E2::Timeout).arg(res == E2::Timeout);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Termaster");

    SettingsManager pluginSettings;
    PluginManager   pluginManager(pluginSettings, QDir(app.applicationDirPath()).filePath("plugin"));
    QString         pluginError;
    if (!pluginManager.start(&pluginError))
    {
        qWarning().noquote() << "Plugin startup:" << pluginError;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &pluginManager, [&pluginManager]() {
        pluginManager.stop();
    });

    // config others
    auto terminal = new TermWidget();
    auto emu      = new Emulator();
    emu->setEchoMode(true);
    emu->setTermWidget(terminal);

    terminal->show();

    emu->inputData(EMU_CSI_REND(EMU_FCLR_BR_RED) "#1 Red line\r\n" EMU_CSI_REND_RESET_ATTRS);
    emu->inputData(EMU_CSI_REND(EMU_FCLR_BR_YELLOW) "#2 Bright yellow line\x04\x15\r\n" EMU_CSI_REND_RESET_ATTRS);
    emu->inputData(EMU_CSI_REND(EMU_FCLR_BR_CYAN
                                ";" EMU_ATTR_SET_UNDERLINE) "#3 cyan and underlined text\r\n" EMU_CSI_REND_RESET_ATTRS);
    emu->inputData("#4 Neeeeeeeext normal line\r\n");
    emu->inputData(EMU_CSI_REND(EMU_FCLR_BR_GREEN
                                ";" EMU_ATTR_SET_BOLD) "#5 green and bold text\x15\r\n" EMU_CSI_REND_RESET_ATTRS);
    emu->inputData(EMU_CSI_REND(EMU_ATTR_SET_STRIKE) "#6 striked out" EMU_CSI_REND(
        EMU_ATTR_CLR_STRIKE) ", and after good text\r\n" EMU_CSI_REND_RESET_ATTRS);

    emu->inputData(EMU_CSI_REND(
        EMU_FCLR_BR_GREEN
        ";" EMU_BCLR_BR_RED) "#7 Bright yellow text on bright red background\x01\x02\x03\r\n" EMU_CSI_REND_RESET_ATTRS);

    emu->inputData(EMU_CSI_REND(
        EMU_FCLR_CYAN
        ";" EMU_BCLR_BR_MAGENTA) "#8 Cyan text on bright magenta background\x01\r\n" EMU_CSI_REND_RESET_ATTRS);

    emu->inputData(
        EMU_CSI_REND(EMU_FCLR_GRAY ";" EMU_BCLR_BR_WHITE ";" EMU_ATTR_SET_INVERSE) "#9 Inversed gray text" EMU_CSI_REND(
            EMU_ATTR_CLR_INVERSE) " on white background\r\n" EMU_CSI_REND_RESET_ATTRS);

    return app.exec();

    SettingsManager::initialize();
    RandomNames::initialize();
    CommandManager::initialize();
    WindowManager::initialize();
    TerminalManager::initialize();

    GeneralLogic::initialize();

    auto recentSessions = WindowManager::instance().recentSessions();

    if (recentSessions.count() == 0)
    {
        WindowManager::instance().newWindow();
    }

    return app.exec();
}
