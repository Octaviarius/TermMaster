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
    // auto emu      = new Emulator();
    // emu->setTermWidget(terminal);

    terminal->show();

    auto cursor = terminal->termModel()->cursor();

    cursor->setForeground(6);
    cursor->setBackground(0);
    cursor->addString(QString("Surprise, motherfucker!"));
    cursor->lineFeed();
    cursor->carriageReturn();

    cursor->setForeground(9);
    cursor->setBackground(0);
    cursor->addString(QString("123456789ABCDEF"));
    cursor->lineFeed();
    cursor->carriageReturn();

    cursor->setForeground(14);
    cursor->setBackground(2);
    cursor->setAttrs(TermAttribute::Attribute::Italic);
    cursor->addString(QString("And the neeeeeeeeext"));
    cursor->lineFeed();
    cursor->carriageReturn();

    cursor->clrForeground();
    cursor->clrBackground();
    cursor->setAttrs(TermAttribute::Attribute::Bold | TermAttribute::Attribute::Underline);
    cursor->addString(QString("1"));
    cursor->tabulate();
    cursor->addString(QString("2"));
    cursor->tabulate();
    cursor->addString(QString("3"));
    cursor->tabulate();
    cursor->addString(QString("4"));
    cursor->tabulate();
    cursor->addString(QString("tab"));
    cursor->tabulate();
    cursor->addString(QString("as"));
    cursor->tabulate();

    cursor->lineFeed();
    cursor->carriageReturn();

    cursor->update();

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
