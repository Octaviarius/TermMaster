#include "ProfileTrace.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSerialPort>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <Termaster/App/App.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/Workspace.h>
#include <algorithm>
#include <cstdio>
#include <functional>
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

namespace
{
#ifdef QT_NO_DEBUG
constexpr auto BuildLabel = "Windows MSVC Qt6 Release";
#else
constexpr auto BuildLabel = "Windows MSVC Qt6 Debug";
#endif
QJsonArray measurements;
QString    reportPath;

void saveReport()
{
    QFile report(reportPath);
    if (report.open(QIODevice::WriteOnly))
    {
        report.write(QJsonDocument(QJsonObject {
                                       {"measurements", measurements},
                                       {    "platform",   BuildLabel}
        })
                         .toJson());
    }
}

quint64 cpuTime()
{
    FILETIME created {};
    FILETIME exited {};
    FILETIME kernel {};
    FILETIME user {};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    return (quint64(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime)
         + (quint64(user.dwHighDateTime) << 32 | user.dwLowDateTime);
}

void measure(const QString& name, const std::function<bool(QString&)>& action)
{
    QString       error;
    const auto    cpu = cpuTime();
    QElapsedTimer timer;
    timer.start();
    const bool                 ok = action(error);
    const double               ms = timer.nsecsElapsed() / 1e6;
    PROCESS_MEMORY_COUNTERS_EX memory {};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    measurements.append(QJsonObject {
        {     "stage",                            name},
        {    "wallMs",                              ms},
        {     "cpuMs",         (cpuTime() - cpu) / 1e4},
        {"privateMiB", memory.PrivateUsage / 1048576.0},
        {        "ok",                              ok},
        {     "error",                           error}
    });
    fprintf(stderr, "%s: %.3f ms %s\n", qPrintable(name), ms, ok ? "ok" : qPrintable(error));
    fflush(stderr);
    saveReport();
}

void measureResize(const QString& name, QWidget& window, const QSize& size)
{
    double geometryMs = 0;
    double eventsMs   = 0;
    measure(name, [&](QString&) {
        QElapsedTimer timer;
        timer.start();
        window.resize(size);
        geometryMs = timer.nsecsElapsed() / 1e6;
        timer.restart();
        QApplication::processEvents();
        eventsMs = timer.nsecsElapsed() / 1e6;
        return true;
    });
    auto record = measurements.last().toObject();
    record.insert("geometryMs", geometryMs);
    record.insert("eventsMs", eventsMs);
    measurements[measurements.size() - 1] = record;
}

void serialProbe()
{
    QSerialPort port("COM3");
    port.setBaudRate(115200);
    measure("serial.open.115200", [&](QString& error) {
        if (!port.open(QIODevice::ReadWrite))
        {
            error = port.errorString();
            return false;
        }
        return true;
    });
    if (!port.isOpen())
    {
        return;
    }
    port.clear();
    for (int size : {1, 16, 256, 4096})
    {
        for (int repetition = 0; repetition < 5; ++repetition)
        {
            QByteArray data(size, '\0');
            for (int i = 0; i < size; ++i)
            {
                data[i] = char((i * 37 + repetition * 11) & 255);
            }
            measure(QString("serial.loopback.%1").arg(size), [&](QString& error) {
                if (port.write(data) != size)
                {
                    error = port.errorString();
                    return false;
                }
                QByteArray    received;
                QElapsedTimer deadline;
                deadline.start();
                while (received.size() < size && deadline.elapsed() < 3000)
                {
                    port.waitForReadyRead(20);
                    received += port.readAll();
                }
                if (received != data)
                {
                    error = "Loopback byte mismatch or timeout";
                    return false;
                }
                return true;
            });
        }
    }
    measure("serial.close", [&](QString&) {
        port.close();
        return true;
    });
}

void terminalProbe()
{
    if (qEnvironmentVariableIsSet("TERMASTER_PROFILE_RESIZE_QUICK"))
    {
        TerminalDisplay display;
        display.resize(1200, 600);
        display.show();
        QApplication::processEvents();
        const QFontMetricsF fontMetrics(display.font(), display.viewport());
        measurements.append(QJsonObject {
            {       "stage",                                  "display.fixture"},
            {        "font",                 QFontInfo(display.font()).family()},
            {   "pointSize",                        display.font().pointSizeF()},
            {         "dpr",                        display.devicePixelRatioF()},
            {    "mAdvance", fontMetrics.horizontalAdvance(QStringLiteral("M"))},
            {"spaceAdvance", fontMetrics.horizontalAdvance(QStringLiteral(" "))},
            {    "xAdvance", fontMetrics.horizontalAdvance(QStringLiteral("x"))},
            {     "columns",                     display.terminalSize().width()},
            {        "rows",                    display.terminalSize().height()},
            {          "ok",                                               true}
        });
        for (int repeat = 0; repeat < 5; ++repeat)
        {
            measure("font.metrics.advance.10000", [&](QString&) {
                double        sum  = 0;
                const QString text = QStringLiteral("x");
                for (int index = 0; index < 10000; ++index)
                {
                    sum += fontMetrics.horizontalAdvance(text);
                }
                return sum > 0;
            });
        }
        for (int content = 0; content < 3; ++content)
        {
            display.clear();
            if (content > 0)
            {
                QByteArray input;
                const auto grid = display.terminalSize();
                for (int row = 0; row < grid.height(); ++row)
                {
                    input += QByteArray(grid.width(), content == 1 ? ' ' : 'x');
                    if (row + 1 < grid.height())
                    {
                        input += "\r\n";
                    }
                }
                display.receive(input);
            }
            QApplication::processEvents();
            for (int repeat = 0; repeat < 10; ++repeat)
            {
                measure(QString("display.warm.repaint.content%1").arg(content), [&](QString&) {
                    display.viewport()->repaint();
                    return true;
                });
                const QVariantMap appearance {
                    {"foreground", repeat % 2 == 0 ? "#e5e7ea" : "#e5e7eb"}
                };
                display.setAppearance(appearance);
                measure(QString("display.changedAppearance.repaint.content%1").arg(content), [&](QString&) {
                    display.viewport()->repaint();
                    return true;
                });
                display.setAppearance(appearance);
                measure(QString("display.unchangedAppearance.repaint.content%1").arg(content), [&](QString&) {
                    display.viewport()->repaint();
                    return true;
                });
            }
        }
        display.hide();
        for (int history : {0, 1000, 10000})
        {
            TerminalScreen screen;
            screen.setLimits(120, 30, history);
            screen.receive(QByteArray("x\r\n").repeated(history + 40));
            for (int change = 0; change < 4; ++change)
            {
                for (int repeat = 0; repeat < 5; ++repeat)
                {
                    screen.setLimits(120, 30, history);
                    const int columns = change == 2 ? 119 : change == 3 ? 121 : 120;
                    const int rows    = change == 1 ? 31 : 30;
                    measure(QString("grid.warmresize.history%1.change%2").arg(history).arg(change), [&](QString&) {
                        screen.setLimits(columns, rows, history);
                        return true;
                    });
                }
            }
        }
        return;
    }

    for (int history : {100, 10000})
    {
        TerminalScreen screen;
        screen.setLimits(120, 30, history);
        const QByteArray line = QByteArray(100, 'a') + "\r\n";
        screen.receive(line.repeated(history + 40));
        for (int chunk : {1, 64, 4096})
        {
            const auto input = line.repeated(1000);
            measure(QString("screen.history%1.chunk%2").arg(history).arg(chunk), [&](QString&) {
                for (qsizetype offset = 0; offset < input.size(); offset += chunk)
                {
                    screen.receive(input.mid(offset, chunk));
                }
                return true;
            });
        }
    }
    for (int history : {0, 1000, 10000})
    {
        for (int change = 0; change < 4; ++change)
        {
            for (int repeat = 0; repeat < 5; ++repeat)
            {
                TerminalScreen screen;
                screen.setLimits(120, 30, history);
                screen.receive((QByteArray(100, 'x') + "\r\n").repeated(history + 40));
                const int columns = change == 2 ? 119 : change == 3 ? 121 : 120;
                const int rows    = change == 1 ? 31 : 30;
                measure(QString("grid.resize.history%1.change%2").arg(history).arg(change), [&](QString&) {
                    screen.setLimits(columns, rows, history);
                    return true;
                });
            }
        }
    }
    TerminalDisplay display;
    display.resize(1200, 600);
    display.show();
    QApplication::processEvents();
    display.receive((QByteArray(100, 'b') + "\r\n").repeated(1000));
    QApplication::processEvents();
    for (int i = 0; i < 10; ++i)
    {
        measure("display.static.repaint", [&](QString&) {
            display.viewport()->repaint();
            return true;
        });
        measure("display.appearance.unchanged", [&](QString&) {
            display.setAppearance({});
            return true;
        });
        measure("display.appearance.repaint", [&](QString&) {
            display.viewport()->repaint();
            return true;
        });
    }
    for (int history : {0, 1000, 10000})
    {
        display.clear();
        display.setAppearance({
            {"scrollbackLines", std::max(1, history)}
        });
        display.receive((QByteArray(100, 'c') + "\r\n").repeated(history + 40));
        QApplication::processEvents();
        for (int i = 0; i < 20; ++i)
        {
            measure(QString("display.resize.history%1").arg(history), [&](QString&) {
                display.resize(1000 + (i % 2) * 160, 600 + (i % 2) * 80);
                QApplication::processEvents();
                return true;
            });
        }
    }
    display.hide();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        fprintf(stderr, "Usage: ApplicationProfile workspace data plugins output [--serial]\n");
        return 2;
    }
    const QString workspace = QString::fromLocal8Bit(argv[1]);
    const QString output    = QString::fromLocal8Bit(argv[4]);
    const bool    serial    = argc > 5 && QByteArray(argv[5]) == "--serial";
    reportPath              = output;
    QElapsedTimer startup;
    startup.start();
    int result = 0;
    {
        App::Options options;
        options.dataDirectory    = QString::fromLocal8Bit(argv[2]);
        options.pluginsDirectory = QString::fromLocal8Bit(argv[3]);
        App app(argc, argv, options);
        measurements.append(QJsonObject {
            { "stage",              "app.construct"},
            {"wallMs", startup.nsecsElapsed() / 1e6},
            {    "ok",                         true}
        });
        QTimer::singleShot(0, &app, [&] {
            measurements.append(QJsonObject {
                { "stage",     "app.start.to.eventloop"},
                {"wallMs", startup.nsecsElapsed() / 1e6},
                {    "ok",                         true}
            });
            QString sessionId;
            measure("workspace.open.restore", [&](QString& error) {
                const bool ok = app.openWorkspace(Path(workspace), &error);
                if (app.session())
                {
                    sessionId = app.session()->id();
                }
                return ok && app.session();
            });
            measurements.append(QJsonObject {
                {        "stage",                "fixture"},
                {"terminalCount", app.terminalIds().size()},
                {           "ok",     !sessionId.isEmpty()}
            });
            if (qEnvironmentVariableIsSet("TERMASTER_PROFILE_CLOSE_ONLY"))
            {
                measure("window.close.loaded", [&](QString&) { return app.mainWindow().close(); });
                return;
            }
            for (int i = 0; i < 20; ++i)
            {
                measureResize("window.resize.session.empty",
                              app.mainWindow(),
                              QSize(1000 + (i % 2) * 160, 700 + (i % 2) * 90));
            }
            if (qEnvironmentVariableIsSet("TERMASTER_PROFILE_RESIZE_ONLY"))
            {
                for (auto* display : app.mainWindow().findChildren<TerminalDisplay*>())
                {
                    if (!display->isVisible())
                    {
                        continue;
                    }
                    display->receive((QByteArray(100, 'x') + "\r\n").repeated(10040));
                    QApplication::processEvents();
                    measurements.append(QJsonObject {
                        {        "stage",                                             "resize.loaded.fixture"},
                        {      "columns",                                     display->terminalSize().width()},
                        {         "rows",                                    display->terminalSize().height()},
                        {"retainedLines",                               int(display->screen().lines().size())},
                        {         "font",                                            display->font().family()},
                        {          "dpr",                                        display->devicePixelRatioF()},
                        {           "ok", display->screen().lines().size() > display->terminalSize().height()}
                    });
                    for (int change = 0; change < 3; ++change)
                    {
                        for (int i = 0; i < 30; ++i)
                        {
                            measureResize(QString("window.resize.loaded.change%1").arg(change),
                                          app.mainWindow(),
                                          QSize(1000
                                                    + (change == 0 ? i :
                                                       change == 1 ? 0 :
                                                                     (i % 2) * 160),
                                                700
                                                    + (change == 1 ? i :
                                                       change == 2 ? (i % 2) * 90 :
                                                                     0)));
                        }
                    }
                    break;
                }
                measure("window.close.loaded", [&](QString&) { return app.mainWindow().close(); });
                ApplicationProfileTrace::store().dump();
                return;
            }
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                measure("lineflow.editor.open", [&](QString& error) {
                    auto* command = app.commands().find("Workspace.Processing");
                    if (!command || command->actions().isEmpty())
                    {
                        error = "Missing editor action";
                        return false;
                    }
                    command->actions().first()->trigger();
                    QApplication::processEvents();
                    return true;
                });
                measure("lineflow.editor.close", [&](QString&) {
                    for (auto* window : QApplication::topLevelWidgets())
                    {
                        if (QByteArray(window->metaObject()->className()) == "LineflowEditorDialog")
                        {
                            window->close();
                        }
                    }
                    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    QApplication::processEvents();
                    return true;
                });
            }
            measure("app.idle.3s", [&](QString&) {
                QEventLoop idle;
                QTimer::singleShot(3000, &idle, &QEventLoop::quit);
                idle.exec();
                return true;
            });
            for (int i = 0; i < (qEnvironmentVariableIsSet("TERMASTER_PROFILE_ONE_CYCLE") ? 1 : 5) && app.session();
                 ++i)
            {
                measure("session.sync.clean", [&](QString& e) { return app.session()->sync(&e); });
                measure("session.save.clean", [&](QString& e) { return app.saveSession(&e); });
                measure("workspace.sync.clean", [&](QString& e) { return app.workspace()->sync(&e); });
                const auto originalName = app.session()->name();
                app.session()->setName(originalName + " profile");
                measure("session.save.dirty", [&](QString& e) { return app.saveSession(&e); });
                app.session()->setName(originalName);
                measure("terminal.add.serial", [&](QString& e) {
                    return app.createTerminal("serial",
                                              {
                                                  {"transport", QVariantMap {{"portName", "COM999"}}}
                    },
                                              "Profile disconnected",
                                              &e);
                });
                measure("terminal.close.serial", [&](QString& error) {
                    auto* tabs = app.mainWindow().findChild<QTabWidget*>();
                    if (!tabs)
                    {
                        error = "Missing terminal tabs";
                        return false;
                    }
                    const auto count = app.terminalIds().size();
                    QMetaObject::invokeMethod(tabs,
                                              "tabCloseRequested",
                                              Qt::DirectConnection,
                                              Q_ARG(int, tabs->currentIndex()));
                    if (app.terminalIds().size() != count - 1)
                    {
                        error = "Terminal close did not remove exactly one tab";
                        return false;
                    }
                    return true;
                });
                measure("session.close", [&](QString& e) { return app.closeSession(&e); });
                measure("session.open", [&](QString& e) { return app.openSession(sessionId, &e); });
                QApplication::processEvents();
            }
            if (!qEnvironmentVariableIsSet("TERMASTER_PROFILE_SKIP_TERMINAL"))
            {
                terminalProbe();
            }
            if (serial)
            {
                serialProbe();
            }
            measure("workspace.close", [&](QString& e) { return app.closeWorkspace(&e); });
            measure("window.close", [&](QString& e) {
                if (!app.mainWindow().close())
                {
                    e = "close rejected";
                    return false;
                }
                return true;
            });
            ApplicationProfileTrace::store().dump();
        });
        result = app.run();
        ApplicationProfileTrace::store().dump();
        startup.restart();
    }
    measurements.append(QJsonObject {
        { "stage",               "app.destruct"},
        {"wallMs", startup.nsecsElapsed() / 1e6},
        {    "ok",                         true}
    });
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly))
    {
        return 3;
    }
    file.write(QJsonDocument(QJsonObject {
                                 {"measurements", measurements},
                                 {    "platform",   BuildLabel}
    })
                   .toJson());
    return result;
}
