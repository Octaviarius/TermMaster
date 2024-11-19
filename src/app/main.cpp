#include <QApplication>
#include <QMap>
#include <QStyle>
#include <core/RandomNames.h>
#include <logic/GeneralLogic.h>
#include <managers/CommandManager.h>
#include <managers/SettingsManager.h>
#include <managers/TerminalManager.h>
#include <managers/WindowManager.h>
#include <terminal/Emulator.h>
#include <widgets/TermWidget/TermWidget.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Terminal Master");

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
    emu->inputData(
        EMU_CSI_REND(EMU_FCLR_BR_GREEN ";" EMU_ATTR_SET_BOLD) "#5 green and bold text\x15\r\n" EMU_CSI_REND_RESET_ATTRS);
    emu->inputData(EMU_CSI_REND(EMU_ATTR_SET_STRIKE) "#6 striked out" EMU_CSI_REND(
        EMU_ATTR_CLR_STRIKE) ", and after good text\r\n" EMU_CSI_REND_RESET_ATTRS);

    emu->inputData(
        EMU_CSI_REND(EMU_FCLR_BR_GREEN
                     ";" EMU_BCLR_BR_RED) "#7 Bright yellow text on bright red background\x01\x02\x03\r\n" EMU_CSI_REND_RESET_ATTRS);

    emu->inputData(EMU_CSI_REND(
        EMU_FCLR_CYAN ";" EMU_BCLR_BR_MAGENTA) "#8 Cyan text on bright magenta background\x01\r\n" EMU_CSI_REND_RESET_ATTRS);

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
