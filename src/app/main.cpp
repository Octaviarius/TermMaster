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
#include <widgets/TerminalWidget.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Terminal Master");

    // config others
    auto terminal = new TerminalWidget();
    auto emu      = new Emulator();
    emu->setTerminalWidget(terminal);

    terminal->show();

    terminal->setForeground(6);
    terminal->setBackground(0);
    emu->inputData(QString("Surprise, motherfucker!\r\n").toUtf8());

    terminal->setForeground(5);
    terminal->setBackground(0);
    emu->inputData(QString("The next line\r\n").toUtf8());

    terminal->setForeground(4);
    terminal->setBackground(2);
    terminal->setAttrs(TextAttribute::Italic);
    emu->inputData(QString("And the neeeeeeeeext\r\n").toUtf8());

    terminal->clrForeground();
    terminal->clrBackground();
    terminal->setAttrs(TextAttribute::Bold | TextAttribute::Underline);
    emu->inputData(QString("1\t2\t3\t4\r\n").toUtf8());

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
