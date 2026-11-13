#include "../SettingsVariantFixture.h"
#include "Gui/Help/HelpController.h"
#include "Gui/Main/MainWindow.h"
#include "Gui/Processing/LineflowEditorDialog.h"
#include "Gui/Processing/ProcessorCatalogTree.h"
#include "Gui/Session/SessionPicker.h"
#include "Gui/Settings/ActionLayoutEditor.h"
#include "Gui/Settings/FavoritesEditor.h"
#include "Gui/Settings/LoggingTagsEditor.h"
#include "Gui/Settings/SettingsDialog.h"
#include "Gui/Terminal/CommandEditor.h"
#include "Gui/Terminal/DetachedTerminalWindow.h"
#include "Gui/Terminal/TerminalController.h"
#include "Gui/Terminal/TerminalProcessingPanel.h"
#include "Gui/Terminal/TerminalTabGroup.h"
#include "Gui/Terminal/TerminalWorkspace.h"
#include "Processing/TerminalProcessing.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHelpContentModel>
#include <QHelpEngine>
#include <QHelpEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextDocumentFragment>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QUuid>
#include <QWheelEvent>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>
#include <Termaster/Core/Gui/Help/HelpBrowser.h>
#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>
#include <Termaster/Core/NumberedName.h>
#include <Termaster/Core/Plugin/PluginHost.h>
#include <Termaster/Core/Plugin/PluginManager.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <Termaster/Core/Workspace.h>
#include <atomic>
#include <cstdlib>
#include <stdexcept>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <chrono>
#include <thread>
#include <windows.h>

/** GUI-thread conversion. The input worker only receives copied native coordinates and handles. */
static POINT nativeGroupPoint(QWidget* widget, const QPoint& point)
{
    POINT origin {};
    ClientToScreen(reinterpret_cast<HWND>(widget->window()->winId()), &origin);
    const QPoint local = widget->mapTo(widget->window(), point);
    const qreal  ratio = widget->window()->devicePixelRatioF();
    return {origin.x + qRound(local.x() * ratio), origin.y + qRound(local.y() * ratio)};
}

/** Bounded opt-in input, restricted to verified fixture windows before any input is injected. */
static std::jthread nativeGroupDrag(POINT                              start,
                                    POINT                              via,
                                    POINT                              finish,
                                    HWND                               source,
                                    HWND                               target,
                                    bool                               cancel,
                                    std::shared_ptr<std::atomic_int>   result,
                                    std::shared_ptr<std::atomic<HWND>> finishRoot)
{
    return std::jthread([=] {
        const auto rootAt = [](POINT point) { return GetAncestor(WindowFromPoint(point), GA_ROOT); };
        finishRoot->store(rootAt(finish));
        if (!IsWindow(source) || !IsWindow(target) || rootAt(start) != source || finishRoot->load() != target)
        {
            result->store(-1);
            return;
        }
        bool       accepted = true;
        const int  left     = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int  top      = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int  width    = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int  height   = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        const auto move     = [&](POINT point, DWORD buttons = 0) {
            INPUT input {};
            input.type       = INPUT_MOUSE;
            input.mi.dx      = MulDiv(point.x - left, 65535, std::max(1, width - 1));
            input.mi.dy      = MulDiv(point.y - top, 65535, std::max(1, height - 1));
            input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE | buttons;
            accepted         = SendInput(1, &input, sizeof(INPUT)) == 1 && accepted;
        };
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        move(start);
        move(start, MOUSEEVENTF_LEFTDOWN);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        move(via);
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
        for (int step = 1; step <= 12; ++step)
        {
            move({via.x + (finish.x - via.x) * step / 12, via.y + (finish.y - via.y) * step / 12});
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(650));
        if (cancel)
        {
            INPUT keys[2] {};
            keys[0].type       = INPUT_KEYBOARD;
            keys[0].ki.wVk     = VK_ESCAPE;
            keys[1]            = keys[0];
            keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
            accepted           = SendInput(2, keys, sizeof(INPUT)) == 2 && accepted;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        finishRoot->store(rootAt(finish));
        const bool targetVisible = finishRoot->load() == target;
        move(finish, MOUSEEVENTF_LEFTUP);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        result->store(!targetVisible ? -3 : accepted ? 1 : -2);
    });
}
#endif

/** Synthetic backend: every connection attempt is in-memory, never a device open. */
class ReconnectDevice final : public QObject, public SerialDevice
{
public:
    struct Probe
    {
        std::atomic<bool>     available {false};
        std::atomic<int>      attempts {0};
        std::atomic<int>      closes {0};
        std::atomic<qint64>   allowance {2};
        std::atomic<State>    backendState {State::Closed};
        mutable QMutex        mutex;
        ReconnectDevice*      device = nullptr;
        QByteArray            recordedOutput;
        QVariantMap           configuration;
        std::function<void()> resizeNotification;

        QByteArray output() const
        {
            QMutexLocker lock(&mutex);
            return recordedOutput;
        }
        void clearOutput()
        {
            QMutexLocker lock(&mutex);
            recordedOutput.clear();
        }
        QVariantMap openConfiguration() const
        {
            QMutexLocker lock(&mutex);
            return configuration;
        }
        bool hasDevice() const
        {
            QMutexLocker lock(&mutex);
            return device != nullptr;
        }
        void injectReceive(const QByteArray& bytes)
        {
            QMutexLocker lock(&mutex);
            if (device)
            {
                QMetaObject::invokeMethod(
                    device,
                    [target = device, bytes] { target->injectReceive(bytes); },
                    Qt::QueuedConnection);
            }
        }
        void drop(const QString& error)
        {
            QMutexLocker lock(&mutex);
            if (device)
            {
                QMetaObject::
                    invokeMethod(device, [target = device, error] { target->drop(error); }, Qt::QueuedConnection);
            }
        }
        void setResizeNotification(std::function<void()> callback)
        {
            QMutexLocker lock(&mutex);
            resizeNotification = std::move(callback);
        }
    };
    explicit ReconnectDevice(std::shared_ptr<Probe> probe) : _probe(std::move(probe))
    {
        QMutexLocker lock(&_probe->mutex);
        _probe->device = this;
    }
    ~ReconnectDevice() override
    {
        QMutexLocker lock(&_probe->mutex);
        _probe->device = nullptr;
    }
    QObject* object() override
    {
        return this;
    }
    State state() const override
    {
        return _state;
    }
    void setCallbacks(Callbacks callbacks) override
    {
        _callbacks = std::move(callbacks);
    }
    bool open(const QVariantMap& configuration, QString* error) override
    {
        ++_probe->attempts;
        {
            QMutexLocker lock(&_probe->mutex);
            _probe->configuration = configuration;
        }
        _setState(State::Opening);
        if (!_probe->available)
        {
            if (error)
            {
                *error = "Synthetic port unavailable";
            }
            drop("Synthetic port unavailable");
            return false;
        }
        _setState(State::Open);
        return true;
    }
    void close() override
    {
        ++_probe->closes;
        _setState(State::Closing);
        _setState(State::Closed);
    }
    qint64 write(const QByteArray& bytes, QString*) override
    {
        if (_state != State::Open)
        {
            return -1;
        }
        const auto count = qMin<qint64>(bytes.size(), _probe->allowance.load());
        {
            QMutexLocker lock(&_probe->mutex);
            _probe->recordedOutput.append(bytes.first(count));
        }
        const auto callback = _callbacks.written;
        if (callback && count > 0)
        {
            callback(count);
        }
        return count;
    }
    qint64 pendingBytes() const override
    {
        return 0;
    }
    void resize(int, int) override
    {
        std::function<void()> callback;
        {
            QMutexLocker lock(&_probe->mutex);
            callback = _probe->resizeNotification;
        }
        if (callback)
        {
            callback();
        }
    }
    void injectReceive(const QByteArray& bytes)
    {
        const auto callback = _callbacks.received;
        if (callback)
        {
            callback(bytes);
        }
    }
    void drop(const QString& error)
    {
        _setState(State::Failed);
        const auto callback = _callbacks.error;
        if (callback)
        {
            callback(error);
        }
    }

private:
    std::shared_ptr<Probe> _probe;
    State                  _state = State::Closed;
    Callbacks              _callbacks;
    void                   _setState(State state)
    {
        _state = state;
        _probe->backendState.store(state);
        const auto callback = _callbacks.stateChanged;
        if (callback)
        {
            callback(state);
        }
    }
};

/** Observe the selected GUI facade, after worker notifications have reached the GUI. */
static SerialDevice::State selectedDeviceState(App& app)
{
    const auto type   = app.session()->objects().value(app.selectedTerminal()).toMap().value("type").toString();
    auto*      device = app.pluginHost().selectedDevice(type);
    return device ? device->state() : SerialDevice::State::Closed;
}

class ReentrantProcessor final : public Processor
{
public:
    QVariantMap configuration() const override
    {
        return {};
    }
    AcceptResult accept(Chunk input, Callbacks callbacks) override
    {
        callbacks.output(std::move(input));
        callbacks.completed({});
        return AcceptResult::Accepted;
    }
    void cancel(std::function<void()> done) override
    {
        done();
    }
};

class AppTests final : public QObject
{
    Q_OBJECT

public:
    explicit AppTests(App& app) : _app(app)
    {
    }

private:
    App& _app;

    static QAction* _menuAction(QMenu* menu, const QString& id)
    {
        for (auto* action : menu->actions())
        {
            if (action->objectName() == id)
            {
                return action;
            }
        }
        return nullptr;
    }

    static QWidget* _tabHeader(QTabWidget* tabs, int index)
    {
        return tabs->tabBar()->tabButton(index, QTabBar::LeftSide);
    }

    static QString _tabCaption(QTabWidget* tabs, int index)
    {
        auto* header = _tabHeader(tabs, index);
        auto* title  = header ? header->findChild<QLabel*>("terminalTabTitle") : nullptr;
        auto* detail = header ? header->findChild<QLabel*>("terminalTabDetail") : nullptr;
        return title && detail ? title->text() + '\n' + detail->text() : QString();
    }

    static QToolButton* _tabType(QTabWidget* tabs, int index)
    {
        auto* header = _tabHeader(tabs, index);
        return header ? header->findChild<QToolButton*>("terminalTypeIcon") : nullptr;
    }

    static QToolButton* _tabStatus(QTabWidget* tabs, int index)
    {
        auto* header = _tabHeader(tabs, index);
        return header ? header->findChild<QToolButton*>("terminalStatusIndicator") : nullptr;
    }

    static bool _tabHeadersAreCoherent(QTabWidget* tabs, bool diagnostics = false)
    {
        auto* bar      = tabs->tabBar();
        bool  coherent = true;
        for (int index = 0; index < tabs->count(); ++index)
        {
            auto* header = _tabHeader(tabs, index);
            if (!header)
            {
                return false;
            }
            const QRect bounds(header->mapTo(bar, QPoint()), header->size());
            bool        valid = header->isVisible() && bar->tabRect(index).adjusted(-1, -1, 1, 1).contains(bounds);
            for (auto* child : header->findChildren<QWidget*>())
            {
                const QRect childBounds(child->mapTo(header, QPoint()), child->size());
                valid = valid && child->isVisible() && header->rect().contains(childBounds);
            }
            auto* title  = header->findChild<QLabel*>("terminalTabTitle");
            auto* close  = header->findChild<QToolButton*>("terminalTabClose");
            auto* status = _tabStatus(tabs, index);
            if (!title || !close || !status)
            {
                return false;
            }
            const int titleCenter = title->mapTo(header, title->rect().center()).y();
            valid                 = valid && qAbs(close->mapTo(header, close->rect().center()).y() - titleCenter) <= 3
                                 && qAbs(status->mapTo(header, status->rect().center()).y() - titleCenter) <= 3;
            if (!valid && diagnostics)
            {
                qWarning() << "Tab header layout" << index << "tab" << bar->tabRect(index) << "header" << bounds
                           << "visible" << header->isVisible() << "hint" << header->sizeHint() << "minimum"
                           << header->minimumSize() << "parent" << header->parentWidget();
                for (auto* child : header->findChildren<QWidget*>())
                {
                    qWarning() << child->objectName() << QRect(child->mapTo(header, QPoint()), child->size())
                               << "visible" << child->isVisible() << "parent" << child->parentWidget();
                }
            }
            coherent = coherent && valid;
        }
        return coherent;
    }

    void _closeTemporaryWorkspace()
    {
        QString error;
        if (_app.closeWorkspace(&error))
        {
            return;
        }
        // Isolated fixtures may deliberately write documents from another process.
        // Reload those documents before releasing their temporary directories.
        if (_app.workspace())
        {
            QVERIFY2(_app.workspace()->load(&error), qPrintable(error));
        }
        if (_app.session())
        {
            QVERIFY2(_app.session()->load(&error), qPrintable(error));
        }
        QVERIFY2(_app.closeWorkspace(&error), qPrintable(error));
    }

    Q_SLOT void mainWindowCloseExitsProcess_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const auto& scenario : {"empty",
                                     "auxiliary",
                                     "detached",
                                     "splitGroups",
                                     "quitDisabled",
                                     "saveFailure",
                                     "console",
                                     "consoleInplace",
                                     "nestedProcessing"})
        {
            QTest::newRow(scenario) << QString::fromLatin1(scenario);
        }
    }

    Q_SLOT void mainWindowCloseExitsProcess()
    {
        QFETCH(QString, scenario);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.remove("TERMASTER_APP_RESTART_DATA");
        environment.insert("TERMASTER_APP_CLOSE_CHILD", temporary.path());
        environment.insert("TERMASTER_APP_CLOSE_SCENARIO", scenario);
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(child.waitForStarted(5000));
        const bool finished = child.waitForFinished(10000);
        if (!finished)
        {
            child.kill();
            child.waitForFinished(2000);
        }
        QFile markers(temporary.filePath("close-markers.txt"));
        QVERIFY(markers.open(QIODevice::ReadOnly));
        const auto observed   = markers.readAll();
        const auto diagnostic = "Scenario " + scenario.toUtf8() + ": " + observed + child.readAllStandardError();
        QVERIFY2(finished, diagnostic.constData());
        QVERIFY2(child.exitStatus() == QProcess::NormalExit, diagnostic.constData());
        QVERIFY2(child.exitCode() == 0, diagnostic.constData());
        QVERIFY2(observed.contains("mainClosed\n"), diagnostic.constData());
        QVERIFY2(observed.contains("loopExited\n"), diagnostic.constData());
        QVERIFY2(observed.contains("destructorFinished\n"), diagnostic.constData());
        if (scenario == "saveFailure")
        {
            QVERIFY2(observed.contains("saveVetoed\n"), diagnostic.constData());
        }
        if (scenario == "detached")
        {
            QVERIFY2(observed.contains("detachedReady\n"), diagnostic.constData());
            Settings  base(temporary.filePath("verification.yaml"));
            Workspace workspace(Path(temporary.filePath("Close.termaster")), base.node(""));
            QString   error;
            QVERIFY2(workspace.load(&error), qPrintable(error));
            const auto directory = workspace.sessionDirectory(workspace.state().value("lastSession").toString(),
                                                              &error);
            QVERIFY2(directory.has_value(), qPrintable(error));
            Session session(*directory, workspace.settings());
            QVERIFY2(session.load(&error), qPrintable(error));
            int connected = 0;
            for (const auto& value : session.objects().values())
            {
                const auto state = value.toMap().value("connected");
                QCOMPARE(state.typeId(), QMetaType::Bool);
                connected += state.toBool() ? 1 : 0;
            }
            QCOMPARE(session.objects().values().size(), 2);
            QCOMPARE(connected, 1);
        }
        if (scenario == "splitGroups")
        {
            QVERIFY2(observed.contains("splitGroupsReady\n"), diagnostic.constData());
        }
    }

    Q_SLOT void shellAndCommands()
    {
        QCOMPARE(QCoreApplication::applicationName(), QString("Termaster"));
        QCOMPARE(_app.mainWindow().windowTitle(), QString("Termaster"));
        QVERIFY(_app.mainWindow().isVisible());
        QVERIFY(!QApplication::windowIcon().isNull());
        QVERIFY(!_app.mainWindow().windowIcon().isNull());
        QVERIFY(!QApplication::windowIcon().pixmap(32, 32).isNull());
        for (const auto& menu : {"/file", "/edit", "/view", "/session", "/terminal", "/tools", "/help"})
        {
            QVERIFY(_app.mainMenu().findMenu(menu));
        }
        QVERIFY(!_app.workspace());
        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Welcome.png")));
        }
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(_app.mainWindow().findChild<QPushButton*>("createWorkspace")->isVisible());
        QVERIFY(_app.mainWindow().findChild<QPushButton*>("openWorkspace")->isVisible());
        QVERIFY(!_app.commands().find("Workspace.Save")->actions().first()->isEnabled());
        QVERIFY(!_app.commands().find("Workspace.Close")->actions().first()->isEnabled());
        auto* tools = _app.mainMenu().findMenu("/tools");
        QVERIFY(tools);
        auto* plugins = _app.commands().find("Application.Plugins");
        auto* quit    = _app.commands().find("Application.Exit");
        QVERIFY(plugins);
        QVERIFY(quit);
        QCOMPARE(plugins->actions().size(), 1);
        QCOMPARE(quit->shortcut(), QKeySequence("Ctrl+Shift+Q"));
        QVERIFY(tools->actions("management").contains(plugins->actions().first()));
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(plugins->actions().first()));
        QCOMPARE(_app.toolbar().groups(), QStringList({"terminalCreation"}));
        QCOMPARE(_app.toolbar().toolbar()->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(_app.toolbar().toolbar()->actions().size(), 0);
        QVERIFY(!_app.commands().find("Terminal.LocalEcho")->actions().first()->isEnabled());
        for (auto* action : _app.toolbar().toolbar()->actions())
        {
            if (!action->isSeparator())
            {
                QVERIFY(!action->icon().isNull());
                QVERIFY(!action->toolTip().isEmpty());
            }
        }
        for (const auto* id : {"Workspace.New",
                               "Workspace.Open",
                               "Workspace.Save",
                               "Workspace.Close",
                               "Session.New",
                               "Session.Open",
                               "Session.Save",
                               "Session.Close"})
        {
            QVERIFY(!_app.toolbar().toolbar()->actions().contains(_app.commands().find(id)->actions().first()));
        }
        QVERIFY(!QFileInfo::exists(_app.paths().configFile("plugins").toString()));
    }

    Q_SLOT void terminalGroupFrameAndOverflowNavigation()
    {
        TerminalWorkspace workspace;
        workspace.resize(680, 420);
        auto* group = workspace.initialGroup();
        for (int index = 0; index < 12; ++index)
        {
            auto* page = new QWidget;
            page->setProperty("terminalId", QString::number(index));
            group->tabs().addTab(page, QString());
            group->setTabPresentation(index, QString("Terminal %1").arg(index), "Endpoint", {}, {});
        }
        group->toolbar().addAction("Start");
        group->statusBar().showMessage("Terminal 0: Disconnected");
        workspace.show();
        QTest::qWait(60);
        QVERIFY(qobject_cast<QFrame*>(group));
        QVERIFY(group->contentsRect().left() >= 2);
        QVERIFY(
            group->contentsRect().contains(QRect(group->toolbar().mapTo(group, QPoint()), group->toolbar().size())));
        QVERIFY(group->contentsRect().contains(group->statusBar().geometry()));
        QVERIFY(group->toolbar().geometry().bottom() < group->tabs().geometry().top());
        QCOMPARE(group->toolbar().iconSize(), QSize(16, 16));
        auto*       bar         = group->tabs().tabBar();
        auto*       currentPage = group->tabs().currentWidget();
        const auto  firstTab    = bar->tabRect(0);
        const auto  point       = bar->rect().center();
        QWheelEvent wheel(point,
                          bar->mapToGlobal(point),
                          QPoint(),
                          QPoint(0, -120),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(bar, &wheel);
        QTest::qWait(40);
        QVERIFY(bar->tabRect(0).left() < firstTab.left());
        QCOMPARE(group->tabs().currentWidget(), currentPage);
        QWheelEvent back(point,
                         bar->mapToGlobal(point),
                         QPoint(),
                         QPoint(0, 120),
                         Qt::NoButton,
                         Qt::NoModifier,
                         Qt::NoScrollPhase,
                         false);
        QCoreApplication::sendEvent(bar, &back);
        QTest::qWait(40);
        QCOMPARE(bar->tabRect(0), firstTab);
        QCOMPARE(group->tabs().currentWidget(), currentPage);

        auto* button = group->findChild<QToolButton*>("terminalHiddenTabsButton");
        QVERIFY(button && button->menu());
        // Fusion's offscreen tab base adds a one-pixel baseline outside the corner slot.
        QVERIFY(qAbs(button->height() - bar->height()) <= (QGuiApplication::platformName() == "windows" ? 0 : 1));
        QVERIFY(qAbs(2 * button->width() - button->height()) <= 2);
        QVERIFY(qAbs(button->mapTo(group, QPoint()).y() - bar->mapTo(group, QPoint()).y())
                <= (QGuiApplication::platformName() == "windows" ? 0 : 1));
        const auto originalFont = group->tabs().font();
        auto       largerFont   = originalFont;
        largerFont.setPointSize(originalFont.pointSize() + 3);
        group->tabs().setFont(largerFont);
        QTest::qWait(40);
        // Fusion's offscreen tab base adds a one-pixel baseline outside the corner slot.
        QVERIFY(qAbs(button->height() - bar->height()) <= (QGuiApplication::platformName() == "windows" ? 0 : 1));
        QVERIFY(qAbs(2 * button->width() - button->height()) <= 2);
        group->tabs().setFont(originalFont);
        QTest::qWait(40);
        auto*      menu    = button->menu();
        const auto capture = qEnvironmentVariable("TERMASTER_GROUP_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(workspace.grab().save(QDir(capture).filePath("OverflowStrip.png")));
            menu->popup(button->mapToGlobal(QPoint(0, button->height())));
            QTest::qWait(80);
            QVERIFY(menu->grab().save(QDir(capture).filePath("OverflowMenu.png")));
            menu->hide();
        }
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
        QVERIFY(!menu->actions().isEmpty());
        auto* target = group->tabs().widget(11);
        auto* action = menu->actions().last();
        QVERIFY(action->text().contains("Terminal 11"));
        // A queued menu selection must resolve the page after tab reordering.
        group->tabs().tabBar()->moveTab(11, 2);
        action->trigger();
        QCOMPARE(group->tabs().currentWidget(), target);
        group->tabs().removeTab(group->tabs().indexOf(target));
        auto* selected = group->tabs().currentWidget();
        action->trigger();
        QCOMPARE(group->tabs().currentWidget(), selected);
        delete target;
        action->trigger();
        QCOMPARE(group->tabs().currentWidget(), selected);

        workspace.resize(2600, 420);
        QTest::qWait(40);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
        const auto visibleActions = menu->actions();
        QVERIFY(std::none_of(visibleActions.cbegin(), visibleActions.cend(), [](const auto* item) {
            return item->isEnabled();
        }));
    }

    Q_SLOT void loggingTagEditorPreservesInvalidDrafts()
    {
        LoggingTagsEditor editor;
        const QVariantMap rules {
            {"termaster:a",  "info"},
            {"termaster:b", "trace"}
        };
        editor.setValue(rules);
        QVERIFY(editor.hasAcceptableInput());
        QCOMPARE(editor.value().toMap(), rules);
        auto* table = editor.findChild<QTableView*>("loggingTagsTable");
        QVERIFY(table);
        QVERIFY(table->model()->setData(table->model()->index(1, 0), "termaster:a"));
        QVERIFY(!editor.hasAcceptableInput());
        QVERIFY(editor.inputError().contains("unique"));

        editor.setValue(QVariantMap {
            {"termaster:a", 42}
        });
        QVERIFY(!editor.hasAcceptableInput());
        QCOMPARE(editor.value().toMap().value("termaster:a").typeId(), int(QMetaType::Int));
        editor.setValue(QVariantMap {
            {QString::fromUtf8("termaster:雪"), "info"}
        });
        QVERIFY(!editor.hasAcceptableInput());
        editor.setValue(QString("Preserved invalid input"));
        QVERIFY(!editor.hasAcceptableInput());
        QCOMPARE(editor.value().toString(), QString("Preserved invalid input"));

        QVariantMap oversized;
        for (int index = 0; index < 200; ++index)
        {
            oversized.insert(QString("termaster:tag%1").arg(index), "info");
        }
        editor.setValue(oversized);
        QCOMPARE(table->model()->rowCount(), 65);
        QCOMPARE(editor.value().toMap(), oversized);
        QVERIFY(!editor.hasAcceptableInput());
        QVERIFY(!editor.findChild<QToolButton*>("loggingTagAdd")->isEnabled());
        editor.setValue(QVariantMap {});
        QVERIFY(editor.hasAcceptableInput());
        QVERIFY(editor.value().toMap().isEmpty());
        QVERIFY(editor.findChild<QToolButton*>("loggingTagAdd")->isEnabled());
    }

    Q_SLOT void loggingSettingsAndWorkspaceDestinations()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const ApplicationPaths paths(Path(temporary.filePath("data")));
        Settings saved(paths.configFile("application"), {"termaster.application", "Termaster application settings"});
        QVERIFY(saved.setValue("settings/logging/file", false));
        QVERIFY(saved.sync());

        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_LOGGING_CHILD", temporary.path());
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0,
                 qPrintable(QString("Logging child exit %1: %2")
                                .arg(child.exitCode())
                                .arg(QString::fromUtf8(child.readAllStandardError()))));

        const auto contents = [](const QString& name) {
            QFile file(name);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
        };
        const auto user      = contents(paths.logsDirectory().join("application.log").toString());
        const auto workspace = contents(temporary.filePath("workspace/logs/application.log"));
        QVERIFY(user.contains("logging-user-before"));
        QVERIFY(user.contains("logging-user-after"));
        QVERIFY(!user.contains("logging-workspace"));
        QVERIFY(workspace.contains("logging-workspace"));
        QVERIFY(workspace.contains("logging-failed-switch"));
        QVERIFY(!workspace.contains("logging-user-after"));
        QVERIFY(!QFileInfo::exists(temporary.filePath("workspace/example.termaster.d/logs")));

        QTemporaryDir invalid;
        QVERIFY(invalid.isValid());
        const ApplicationPaths invalidPaths(Path(invalid.filePath("data")));
        Settings               invalidSaved(invalidPaths.configFile("application"),
                                            {"termaster.application", "Termaster application settings"});
        QVERIFY(invalidSaved.setValue("settings/logging/file", false));
        QVERIFY(invalidSaved.setValue("state/recentWorkspaces", 42));
        QVERIFY(invalidSaved.sync());
        environment.insert("TERMASTER_APP_LOGGING_CHILD", invalid.path());
        environment.insert("TERMASTER_APP_LOGGING_STARTUP_ONLY", "1");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QVERIFY(!invalidPaths.logsDirectory().exists());
    }

    Q_SLOT void workspaceLifecycleAndRestart()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const Path file(temporary.filePath("example.termaster"));
        QString    error;
        QSignalSpy changed(&_app, &App::workspaceChanged);
        QVERIFY(_app.settings().setValue("settings/appearance/fontSize", 12));
        QVERIFY(_app.settings().setValue("settings/appearance/theme", "dark"));
        QVERIFY2(_app.createWorkspace(file, "Example", &error), qPrintable(error));
        QCOMPARE(changed.count(), 1);
        QVERIFY(_app.workspace());
        QCOMPARE(_app.workspace()->name(), QString("Example"));
        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Workspace.png")));
        }
        QVERIFY(_app.mainWindow().windowTitle().contains("Example"));
        QVERIFY(_app.commands().find("Workspace.Save")->actions().first()->isEnabled());
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 12);
        QVERIFY(_app.configuration().setValue("appearance/fontSize", 18));
        auto* dirtyWorkspace = _app.workspace();
        QVERIFY(_app.restoreLastWorkspace(&error));
        QCOMPARE(_app.workspace(), dirtyWorkspace);
        QVERIFY(_app.workspace()->isDirty());
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 18);
        QCOMPARE(_app.configuration().value("appearance/theme").toString(), QString("dark"));
        QCOMPARE(_app.settings().value("settings/appearance/fontSize").toInt(), 12);
        QVERIFY2(_app.saveWorkspace(&error), qPrintable(error));
        QVERIFY(_app.settings().sync(&error));

        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_RESTART_DATA", QFileInfo(_app.settings().filePath()).dir().absolutePath());
        environment.insert("TERMASTER_APP_RESTART_WORKSPACE", _app.workspace()->filePath().toString());
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), QStringList());
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
        Settings savedApplication(_app.paths().configFile("application"),
                                  {"termaster.application", "Termaster application settings"});
        QVERIFY(savedApplication.load(&error));
        QCOMPARE(savedApplication.value("settings/child/autosave").toInt(), 41);
        Workspace savedWorkspace(file, savedApplication.node("settings"));
        QVERIFY(savedWorkspace.load(&error));
        QCOMPARE(savedWorkspace.settings().value("child/autosave").toInt(), 42);
        // Adopt the independent child process save before making further parent edits.
        QVERIFY2(_app.workspace()->load(&error), qPrintable(error));

        auto*      active     = _app.workspace();
        const auto remembered = _app.settings().value("state/lastWorkspace");
        QVERIFY(!_app.openWorkspace(Path(temporary.filePath("missing.termaster")), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(_app.workspace(), active);
        QCOMPARE(_app.settings().value("state/lastWorkspace"), remembered);
        QFile malformed(temporary.filePath("malformed.termaster"));
        QVERIFY(malformed.open(QIODevice::WriteOnly));
        QVERIFY(malformed.write("version: [invalid") > 0);
        malformed.close();
        QVERIFY(!_app.openWorkspace(Path(malformed.fileName()), &error));
        QCOMPARE(_app.workspace(), active);
        QVERIFY(_app.configuration().setValue("appearance/fontSize", 19));
        const QString backup = file.toString() + ".backup";
        QVERIFY(QFile::rename(file.toString(), backup));
        QVERIFY(QDir().mkdir(file.toString()));
        QVERIFY(!_app.closeWorkspace(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(_app.workspace(), active);
        QVERIFY(_app.workspace()->isDirty());
        QVERIFY(QDir().rmdir(file.toString()));
        QVERIFY(QFile::rename(backup, file.toString()));
        QVERIFY(_app.configuration().setValue("appearance/fontSize", 18));
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!_app.workspace());
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 12);
        QVERIFY(!_app.commands().find("Workspace.Save")->actions().first()->isEnabled());
        QVERIFY2(_app.openWorkspace(file, &error), qPrintable(error));
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 18);
        QVERIFY(_app.configuration().remove("appearance/fontSize"));
        QCOMPARE(_app.configuration().value("appearance/fontSize").toInt(), 12);
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void missingLastWorkspaceRecovery()
    {
        QTemporaryDir          temporary;
        const ApplicationPaths paths(Path(temporary.filePath("data")));
        const QString          missing = temporary.filePath("missing.termaster");
        Settings settings(paths.configFile("application"), {"termaster.application", "Termaster application settings"});
        QVERIFY(settings.setValue("state/lastWorkspace", missing));
        QVERIFY(settings.sync());
        QFile file(paths.configFile("application").toString());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        file.close();
        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_RESTART_DATA", paths.dataDirectory().toString());
        environment.insert("TERMASTER_APP_RECOVERY", "1");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), QStringList());
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
        QVERIFY(!QFileInfo::exists(missing));
    }

    Q_SLOT void editingAndViewCommands()
    {
        auto* clipboard      = QApplication::clipboard();
        auto  savedClipboard = std::make_unique<QMimeData>();
        if (const auto* source = clipboard->mimeData())
        {
            for (const auto& format : source->formats())
            {
                savedClipboard->setData(format, source->data(format));
            }
        }
        const auto restoreClipboard = qScopeGuard([&] { clipboard->setMimeData(savedClipboard.release()); });
        clipboard->setText("replacement");
        const auto    action = [this](const char* id) { return _app.commands().find(id)->actions().first(); };
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("editing.termaster")), "Editing", &error));
        auto* file = _app.mainWindow().findChild<QLineEdit*>("workspaceFile");
        QVERIFY(file);
        _app.mainWindow().activateWindow();
        file->setFocus();
        file->selectAll();
        QTRY_VERIFY(action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.Cut")->isEnabled());
        QVERIFY(!action("Edit.Paste")->isEnabled());
        action("Edit.Copy")->trigger();
        QCOMPARE(clipboard->text(), file->text());
        file->deselect();
        QTRY_VERIFY(!action("Edit.Copy")->isEnabled());

        QPlainTextEdit editor(&_app.mainWindow());
        editor.setGeometry(50, 100, 400, 100);
        editor.show();
        editor.setFocus();
        editor.setPlainText("alpha");
        editor.selectAll();
        QTRY_VERIFY(action("Edit.Cut")->isEnabled());
        action("Edit.Cut")->trigger();
        QCOMPARE(editor.toPlainText(), QString());
        QCOMPARE(clipboard->text(), QString("alpha"));
        action("Edit.Paste")->trigger();
        QCOMPARE(editor.toPlainText(), QString("alpha"));
        QTRY_VERIFY(action("Edit.Undo")->isEnabled());
        action("Edit.Undo")->trigger();
        QCOMPARE(editor.toPlainText(), QString());
        QTRY_VERIFY(action("Edit.Redo")->isEnabled());
        action("Edit.Redo")->trigger();
        QCOMPARE(editor.toPlainText(), QString("alpha"));
        editor.selectAll();
        editor.setReadOnly(true);
        QTRY_VERIFY(!action("Edit.Cut")->isEnabled());
        QVERIFY(action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.Paste")->isEnabled());
        editor.setReadOnly(false);
        QTRY_VERIFY(action("Edit.Cut")->isEnabled());

        QLineEdit line(&_app.mainWindow());
        line.setGeometry(50, 220, 400, 30);
        line.show();
        line.setText("beta");
        line.setFocus();
        line.selectAll();
        QTRY_VERIFY(action("Edit.Cut")->isEnabled());
        action("Edit.Cut")->trigger();
        QCOMPARE(line.text(), QString());
        action("Edit.Paste")->trigger();
        QCOMPARE(line.text(), QString("beta"));
        line.setReadOnly(true);
        line.selectAll();
        QTRY_VERIFY(!action("Edit.Cut")->isEnabled());
        QVERIFY(action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.Paste")->isEnabled());
        _app.mainWindow().findChild<QPushButton*>("openWorkspace")->setFocus();
        QTRY_VERIFY(!action("Edit.Copy")->isEnabled());
        QVERIFY(!action("Edit.SelectAll")->isEnabled());

        QVERIFY(_app.settings().setValue("settings/ui/toolbarVisible", true));
        QVERIFY(_app.settings().setValue("settings/ui/statusBarVisible", true));
        action("View.Toolbar")->trigger();
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        QVERIFY(!_app.workspace()->settings().containsOverride("ui/toolbarVisible"));
        QCOMPARE(_app.settings().value("settings/ui/toolbarVisible").toBool(), true);
        action("View.StatusBar")->trigger();
        QVERIFY(_app.mainWindow().statusBar()->isHidden());
        QVERIFY(!_app.workspace()->settings().containsOverride("ui/statusBarVisible"));
        QCOMPARE(_app.settings().value("settings/ui/statusBarVisible").toBool(), false);
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        action("View.Toolbar")->trigger();
        action("View.StatusBar")->trigger();
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        QVERIFY(!_app.mainWindow().statusBar()->isHidden());
        QVERIFY(!action("View.Toolbar")->isChecked());
        QVERIFY(!action("View.Toolbar")->isVisible());
        QVERIFY(!action("View.Toolbar")->isEnabled());
        QVERIFY(action("View.StatusBar")->isChecked());
    }

    Q_SLOT void recentWorkspaceHistory()
    {
        QTemporaryDir temporary;
        QString       error;
        QStringList   files;
        for (int index = 0; index < 12; ++index)
        {
            const Path file(temporary.filePath(QString::number(index) + ".termaster"));
            QVERIFY2(_app.createWorkspace(file, QString::number(index), &error), qPrintable(error));
            files.prepend(_app.workspace()->filePath().toString());
        }
        files = files.mid(0, 10);
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(_app.openWorkspace(Path(files.at(3)), &error));
        files.prepend(files.takeAt(3));
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(_app.openWorkspace(Path(files.first()), &error));
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(!_app.openWorkspace(Path(temporary.filePath("missing.termaster")), &error));
        QCOMPARE(_app.recentWorkspaces(), files);
        QVERIFY(_app.mainMenu().findMenu("/file/recentWorkspaces"));
        QVERIFY(_app.mainMenu().findMenu("/session/recentSessions"));
        auto* recentMenu        = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/file/recentWorkspaces"))
                                      ->menu();
        int   visibleWorkspaces = 0;
        for (auto* action : recentMenu->actions())
        {
            if (action->isVisible())
            {
                ++visibleWorkspaces;
            }
        }
        QCOMPARE(visibleWorkspaces, 10);
        QCOMPARE(recentMenu->actions().first()->data().toString(), files.first());
        QVERIFY(_app.createSession("Recorded", &error));
        const auto recordedId = _app.session()->id();
        QVERIFY(_app.closeSession(&error));
        auto* sessionMenu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/session/recentSessions"))
                                ->menu();
        QVERIFY(QMetaObject::invokeMethod(sessionMenu, "aboutToShow", Qt::DirectConnection));
        int visibleSessions = 0;
        for (auto* action : sessionMenu->actions())
        {
            if (action->isVisible())
            {
                ++visibleSessions;
                QVERIFY(action->isEnabled());
                QVERIFY(action->text().contains("Recorded"));
                QCOMPARE(action->data().toString(), recordedId);
                action->trigger();
                QVERIFY(_app.session());
                QCOMPARE(_app.session()->name(), QString("Recorded"));
            }
        }
        QCOMPARE(visibleSessions, 1);
        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            recentMenu->popup(_app.mainWindow().mapToGlobal(QPoint(20, 60)));
            QTRY_VERIFY(recentMenu->isVisible());
            QApplication::processEvents();
            QVERIFY(recentMenu->grab().save(QDir(capture).filePath("RecentWorkspaces.png")));
            recentMenu->close();
            sessionMenu->popup(_app.mainWindow().mapToGlobal(QPoint(20, 60)));
            QTRY_VERIFY(sessionMenu->isVisible());
            QApplication::processEvents();
            QVERIFY(sessionMenu->grab().save(QDir(capture).filePath("RecentSessions.png")));
            sessionMenu->close();
        }
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void groupedTerminalTransfersRetainRuntimeAndIndependentControls()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Groups.termaster")), "Groups", &error));
        QVERIFY(_app.createSession("Groups", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        PluginHost::TerminalType type;
        type.id             = "groupFixture";
        type.title          = "Group fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic group fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        const auto cleanup  = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        host.registerTerminalType(type);
        QStringList                     ids;
        QList<QWidget*>                 panels;
        QList<TerminalDisplay*>         displays;
        QList<QPlainTextEdit*>          drafts;
        QList<TerminalProcessingPanel*> processing;
        SerialDevice*                   openDevice = nullptr;
        for (int index = 0; index < 3; ++index)
        {
            QVERIFY(_app.createTerminal(type.id,
                                        {
                                            {"input", QVariantMap {{"mode", "buffered"}, {"localEcho", false}}}
            },
                                        QString("Terminal %1").arg(index),
                                        &error));
            ids.append(_app.selectedTerminal());
            auto* group = controller.terminalGroup(ids.last());
            QVERIFY(group);
            auto* panel = group->tabs().currentWidget();
            QVERIFY(panel);
            auto* display         = panel->findChild<TerminalDisplay*>("terminalDisplay");
            auto* draft           = panel->findChild<QPlainTextEdit*>("commandDraft");
            auto* processingPanel = panel->findChild<TerminalProcessingPanel*>();
            QVERIFY(display && draft && processingPanel);
            panels.append(panel);
            displays.append(display);
            drafts.append(draft);
            processing.append(processingPanel);
            display->receive(QByteArray("owned-text-") + QByteArray::number(index));
            draft->setPlainText(QString("draft-%1").arg(index));
            if (index == 0)
            {
                probe->available = true;
                _app.mainWindow().activateWindow();
                QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
                openDevice = host.selectedDevice(type.id);
                QVERIFY(openDevice);
                host.connectSelectedTerminal();
                QTRY_COMPARE(openDevice->state(), SerialDevice::State::Open);
            }
        }
        const int closes = probe->closes.load();
        displays[0]->selectAll();
        QVERIFY(displays[0]->hasSelection());
        displays[0]->copy();
        const auto selection = QApplication::clipboard()->text();
        QVERIFY(selection.startsWith("owned-text-0"));
        const auto presentation = _app.session()->presentation().values();
        auto*      initial      = controller.terminalGroup(ids[2]);
        QVERIFY(controller.moveTerminal(ids[0], initial, Qt::RightEdge, -1, &error));
        auto* splitGroup = controller.terminalGroup(ids[0]);
        QVERIFY(splitGroup && splitGroup != initial);
        QCOMPARE(static_cast<MainWindow&>(_app.mainWindow()).terminalWorkspace().groups().size(), 2);
        QCOMPARE(splitGroup->tabs().currentWidget(), panels[0]);
        QCOMPARE(panels[0]->window(), static_cast<QWidget*>(&_app.mainWindow()));
        QTRY_VERIFY(initial->statusBar().currentMessage().contains("Terminal 2"));
        QTRY_VERIFY(splitGroup->statusBar().currentMessage().contains("Terminal 0"));

        QVERIFY2(controller.detachTerminal(ids[0], &error), qPrintable(error));
        QVERIFY2(controller.detachTerminal(ids[1], &error), qPrintable(error));
        QPointer<DetachedTerminalWindow> first(
            qobject_cast<DetachedTerminalWindow*>(controller.detachedWindow(ids[0])));
        QPointer<DetachedTerminalWindow> second(
            qobject_cast<DetachedTerminalWindow*>(controller.detachedWindow(ids[1])));
        QVERIFY(first && second && first != second);
        QVERIFY(controller.moveTerminal(ids[0], controller.terminalGroup(ids[1]), Qt::RightEdge, -1, &error));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(first.isNull());
        QCOMPARE(controller.detachedWindow(ids[0]), static_cast<QMainWindow*>(second.data()));
        auto* firstGroup  = controller.terminalGroup(ids[0]);
        auto* secondGroup = controller.terminalGroup(ids[1]);
        QVERIFY(firstGroup && secondGroup && firstGroup != secondGroup);
        QCOMPARE(second->terminalWorkspace().groups().size(), 2);
        QCOMPARE(firstGroup->tabs().currentWidget(), panels[0]);
        QCOMPARE(secondGroup->tabs().currentWidget(), panels[1]);
        QTRY_VERIFY(initial->statusBar().currentMessage().contains("Terminal 2"));
        QTRY_VERIFY(firstGroup->statusBar().currentMessage().contains("Terminal 0"));
        QTRY_VERIFY(secondGroup->statusBar().currentMessage().contains("Terminal 1"));
        QCOMPARE(panels[0]->window(), static_cast<QWidget*>(second.data()));
        QVERIFY(!panels[0]->isWindow());
        for (auto* group : {initial, firstGroup, secondGroup})
        {
            QCOMPARE(group->toolbar().toolButtonStyle(), Qt::ToolButtonIconOnly);
            for (const auto* name : {"statusBufferedInput", "statusSendFile", "statusProcessing"})
            {
                QVERIFY(group->statusBar().findChild<QToolButton*>(name));
            }
        }
        for (auto* window : {static_cast<QMainWindow*>(&_app.mainWindow()), static_cast<QMainWindow*>(second.data())})
        {
            for (auto* button : window->findChildren<QToolButton*>())
            {
                if (!button->objectName().startsWith("status"))
                {
                    continue;
                }
                auto* owner = button->parentWidget();
                while (owner && !qobject_cast<TerminalTabGroup*>(owner))
                {
                    owner = owner->parentWidget();
                }
                QVERIFY2(owner, qPrintable(button->objectName()));
                auto* group = qobject_cast<TerminalTabGroup*>(owner);
                QVERIFY(group->statusBar().isAncestorOf(button));
            }
        }
        auto* wrap      = firstGroup->action("View.WrapLines");
        auto* otherWrap = secondGroup->action("View.WrapLines");
        auto* echo      = firstGroup->action("Terminal.LocalEcho");
        auto* copy      = second->findChild<QAction*>("Edit.Copy", Qt::FindDirectChildrenOnly);
        QVERIFY(wrap && otherWrap && echo && copy);
        QVERIFY(wrap != otherWrap);
        QVERIFY(wrap->isChecked() && otherWrap->isChecked());
        second->activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), second.data());
        auto* buffered = firstGroup->action("Terminal.BufferedInput");
        QVERIFY(buffered && buffered->isChecked());
        buffered->trigger();
        QVERIFY(!buffered->isChecked());
        QVERIFY(!drafts[0]->isVisible());
        QVERIFY(!displays[0]->focusProxy());
        displays[0]->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), displays[0]);
        QTRY_COMPARE(_app.selectedTerminal(), ids[0]);
        QCOMPARE(firstGroup->styleSheet(), secondGroup->styleSheet());
        wrap->trigger();
        echo->trigger();
        QVERIFY(!wrap->isChecked());
        QVERIFY(otherWrap->isChecked());
        QVERIFY(echo->isChecked());
        const auto settings = _app.session()->objects().value(ids[0]).toMap().value("settings").toMap();
        QCOMPARE(settings.value("appearance").toMap().value("wrapLines").toBool(), false);
        QCOMPARE(settings.value("input").toMap().value("localEcho").toBool(), true);
        host.refreshTerminalActions();
        QVERIFY(copy->isEnabled());
        QApplication::clipboard()->setText("unrelated");
        copy->trigger();
        // Viewport-only blank rows can change when split/window dimensions change.
        QCOMPARE(QApplication::clipboard()->text().trimmed(), selection.trimmed());
        QCOMPARE(controller.terminalGroup(ids[1])->tabs().currentWidget(), panels[1]);

        QVERIFY(controller.moveTerminal(ids[2], firstGroup, Qt::Edge(0), 0, &error));
        QCOMPARE(firstGroup->tabs().widget(0), panels[2]);
        QCOMPARE(firstGroup->tabs().widget(1), panels[0]);
        const auto firstGroupId         = firstGroup->id();
        const auto secondGroupId        = secondGroup->id();
        const auto selectedBeforeReturn = _app.selectedTerminal();
        QTRY_VERIFY(firstGroup->statusBar().currentMessage().contains("Terminal 2"));
        QTRY_VERIFY(secondGroup->statusBar().currentMessage().contains("Terminal 1"));
        QVERIFY2(controller.returnWindow(second, &error), qPrintable(error));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(second.isNull());
        QVERIFY(!controller.detachedWindow(ids[0]));
        QCOMPARE(_app.selectedTerminal(), selectedBeforeReturn);
        QCOMPARE(controller.terminalGroup(ids[0])->id(), firstGroupId);
        QCOMPARE(controller.terminalGroup(ids[1])->id(), secondGroupId);
        QCOMPARE(controller.terminalGroup(ids[2]), controller.terminalGroup(ids[0]));
        QVERIFY(static_cast<MainWindow&>(_app.mainWindow()).terminalWorkspace().groups().size() >= 2);
        for (int index = 0; index < ids.size(); ++index)
        {
            auto* group = controller.terminalGroup(ids[index]);
            QVERIFY(group);
            QCOMPARE(group->tabs().widget(group->tabs().indexOf(panels[index])), panels[index]);
            QCOMPARE(panels[index]->window(), static_cast<QWidget*>(&_app.mainWindow()));
            QCOMPARE(panels[index]->findChild<TerminalDisplay*>("terminalDisplay"), displays[index]);
            QCOMPARE(panels[index]->findChild<QPlainTextEdit*>("commandDraft"), drafts[index]);
            QCOMPARE(panels[index]->findChild<TerminalProcessingPanel*>(), processing[index]);
            QCOMPARE(drafts[index]->toPlainText(), QString("draft-%1").arg(index));
        }
        QVERIFY(displays[0]->hasSelection());
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().widget(0), panels[2]);
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().widget(1), panels[0]);
        QVariantList visibleOrder;
        for (auto* group : static_cast<MainWindow&>(_app.mainWindow()).terminalWorkspace().groups())
        {
            for (int index = 0; index < group->tabs().count(); ++index)
            {
                visibleOrder.append(group->tabs().widget(index)->property("terminalId"));
            }
        }
        QCOMPARE(visibleOrder.size(), ids.size());
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), visibleOrder);
        QCOMPARE(_app.session()->presentation().value("terminalLayout"), presentation.value("terminalLayout"));
        QTRY_COMPARE(probe->attempts.load(), 1);
        QCOMPARE(probe->closes.load(), closes);
        QTRY_COMPARE(openDevice->state(), SerialDevice::State::Open);
        QVERIFY(controller.detachTerminal(ids[0], &error));
        QVERIFY(controller.attachTerminal(ids[0], &error));
        QVERIFY(!controller.detachedWindow(ids[0]));
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().currentWidget(), panels[0]);
        QTRY_COMPARE(openDevice->state(), SerialDevice::State::Open);
        QTRY_COMPARE(probe->attempts.load(), 1);
        QCOMPARE(probe->closes.load(), closes);

        const auto capture = qEnvironmentVariable("TERMASTER_DETACHED_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("TerminalGroups.png")));
        }
        QVERIFY(controller.detachTerminal(ids[0], &error));
        QPointer<QWidget>         closedPanel(panels[0]);
        QPointer<TerminalDisplay> closedDisplay(displays[0]);
        QPointer<QMainWindow>     closedWindow(controller.detachedWindow(ids[0]));
        closedWindow->activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), closedWindow.data());
        displays[0]->setFocus();
        QTRY_COMPARE(_app.selectedTerminal(), ids[0]);
        auto* close = controller.terminalGroup(ids[0])->action("Terminal.CloseView");
        QVERIFY(close);
        close->trigger();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(closedWindow.isNull());
        QVERIFY(closedPanel.isNull());
        QVERIFY(closedDisplay.isNull());
        QVERIFY(!controller.terminalIds().contains(ids[0]));
        QVERIFY(_app.session()->objects().values().contains(ids[0]));
    }

    Q_SLOT void nativeGroupDragTransfersAndCancellation()
    {
#ifndef Q_OS_WIN
        QSKIP("Native group dragging acceptance requires Windows");
#else
        if (!qEnvironmentVariableIsSet("TERMASTER_GROUP_DRAG_GESTURE") || QGuiApplication::platformName() != "windows")
        {
            QSKIP("Set TERMASTER_GROUP_DRAG_GESTURE=1 with the Windows platform for native input acceptance");
        }
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("GroupDrag.termaster")), "Group drag", &error));
        QVERIFY(_app.createSession("Group drag", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        PluginHost::TerminalType type;
        type.id             = "groupDragFixture";
        type.title          = "Drag fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Native drag fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        const auto cleanup  = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        host.registerTerminalType(type);
        QStringList     ids;
        QList<QWidget*> panels;
        for (int index = 0; index < 3; ++index)
        {
            QVERIFY(_app.createTerminal(type.id, {}, QString("Native %1").arg(index), &error));
            ids.append(_app.selectedTerminal());
            panels.append(controller.terminalGroup(ids.last())->tabs().currentWidget());
        }
        auto&                                 main             = _app.mainWindow();
        const QRect                           originalGeometry = main.geometry();
        QList<QPair<QPointer<QWidget>, bool>> raised;
        const auto                            restoreWindows = qScopeGuard([&] {
            for (const auto& entry : raised)
            {
                if (entry.first && !entry.second)
                {
                    SetWindowPos(reinterpret_cast<HWND>(entry.first->winId()),
                                 HWND_NOTOPMOST,
                                 0,
                                 0,
                                 0,
                                 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            main.setGeometry(originalGeometry);
        });
        const QRect                           screen = main.screen()->availableGeometry().adjusted(20, 30, -20, -30);
        QVERIFY(screen.width() >= 800 && screen.height() >= 400);
        const int   half = (screen.width() - 30) / 2;
        const QRect left(screen.topLeft(), QSize(half, screen.height()));
        const QRect right(screen.topLeft() + QPoint(half + 30, 0), QSize(half, screen.height()));
        const auto  raiseOwn = [&](QWidget* window, const QRect& geometry) {
            const HWND handle   = reinterpret_cast<HWND>(window->winId());
            bool       recorded = false;
            for (const auto& entry : raised)
            {
                recorded = recorded || entry.first == window;
            }
            if (!recorded)
            {
                raised.append({window, bool(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST)});
            }
            window->setGeometry(geometry);
            window->show();
            SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            window->raise();
            window->activateWindow();
            QTest::qWait(150);
        };
        const QString capture = qEnvironmentVariable("TERMASTER_GROUP_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
        }
        QStringList previews;
        const auto  drag = [&](const QString& id,
                               QWidget*       destination,
                               const QPoint&  end,
                               const QString& name,
                               bool           cancel     = false,
                               bool           horizontal = false,
                               bool           wholeGroup = false) {
            auto* group = controller.terminalGroup(id);
            if (!group)
            {
                return false;
            }
            auto* bar   = group->tabs().tabBar();
            int   index = -1;
            for (int candidate = 0; candidate < group->tabs().count(); ++candidate)
            {
                if (group->tabs().widget(candidate)->property("terminalId").toString() == id)
                {
                    index = candidate;
                    break;
                }
            }
            if (index < 0)
            {
                return false;
            }
            QWidget* surface = wholeGroup ? group->findChild<QToolButton*>("terminalGroupDragHandle") :
                                            static_cast<QWidget*>(bar);
            if (!surface)
            {
                return false;
            }
            const QPoint press  = wholeGroup ? surface->rect().center() : bar->tabRect(index).center();
            const QPoint beyond = horizontal ?
                                      QPoint(press.x() + 3 * QApplication::startDragDistance(), press.y()) :
                                      QPoint(press.x(), surface->height() + 3 * QApplication::startDragDistance());
            const POINT  start  = nativeGroupPoint(surface, press);
            const POINT  via    = nativeGroupPoint(surface, beyond);
            const POINT  finish = nativeGroupPoint(destination, end);
            const HWND   sourceHandle = reinterpret_cast<HWND>(bar->window()->winId());
            const HWND   targetHandle = reinterpret_cast<HWND>(destination->window()->winId());
            if (GetAncestor(WindowFromPoint(start), GA_ROOT) != sourceHandle
                || GetAncestor(WindowFromPoint(finish), GA_ROOT) != targetHandle)
            {
                return false;
            }
            const QPointer<QWidget>          sourceWindow(bar->window());
            const QPointer<QWidget>          targetWindow(destination->window());
            const QPointer<TerminalTabGroup> sourceGroup(group);
            bool                             captured      = false;
            bool                             captureOk     = true;
            bool                             ghostCaptured = false;
            bool                             ghostOk       = false;
            QElapsedTimer                    elapsed;
            elapsed.start();
            QTimer sampler;
            sampler.setInterval(50);
            QObject::connect(&sampler, &QTimer::timeout, &sampler, [&] {
                if (!ghostCaptured && sourceGroup)
                {
                    for (auto* active : sourceGroup->findChildren<QDrag*>(QString(), Qt::FindDirectChildrenOnly))
                    {
                        const QImage ghost  = active->pixmap().toImage();
                        const qint64 pixels = qint64(ghost.width()) * ghost.height();
                        if (ghost.isNull())
                        {
                            continue;
                        }
                        ghostCaptured = true;
                        if (pixels > 0 && pixels <= 1024 * 1024)
                        {
                            int maximumAlpha = 0;
                            for (int y = 0; y < ghost.height(); ++y)
                            {
                                for (int x = 0; x < ghost.width(); ++x)
                                {
                                    maximumAlpha = std::max(maximumAlpha, ghost.pixelColor(x, y).alpha());
                                }
                            }
                            ghostOk = maximumAlpha > 0 && maximumAlpha <= 170;
                            if (!capture.isEmpty())
                            {
                                captureOk = ghost.save(QDir(capture).filePath(name + "DragGhost.png")) && captureOk;
                            }
                        }
                        break;
                    }
                }
                for (auto* window : {sourceWindow.data(), targetWindow.data()})
                {
                    if (!window)
                    {
                        continue;
                    }
                    for (auto* label : window->findChildren<QLabel*>("terminalDropCaption"))
                    {
                        if (label->isVisible() && !previews.contains(label->text()))
                        {
                            previews.append(label->text());
                        }
                    }
                }
                if (!captured && !capture.isEmpty() && elapsed.elapsed() > 1050 && targetWindow)
                {
                    const QRect area = targetWindow->frameGeometry();
                    captureOk        = targetWindow->screen()
                                           ->grabWindow(0, area.x(), area.y(), area.width(), area.height())
                                           .save(QDir(capture).filePath(name + "Ghost.png"))
                                    && captureOk;
                    captured         = true;
                }
            });
            sampler.start();
            auto result     = std::make_shared<std::atomic_int>(0);
            auto finishRoot = std::make_shared<std::atomic<HWND>>(nullptr);
            auto driver = nativeGroupDrag(start, via, finish, sourceHandle, targetHandle, cancel, result, finishRoot);
            while (result->load() == 0 && elapsed.elapsed() < 6000)
            {
                QTest::qWait(20);
            }
            sampler.stop();
            driver.join();
            QCoreApplication::processEvents();
            QStringList sourceTabs;
            if (sourceGroup)
            {
                for (int current = 0; current < sourceGroup->tabs().count(); ++current)
                {
                    sourceTabs.append(sourceGroup->tabs().widget(current)->property("terminalId").toString());
                }
            }
            qInfo() << "Native group drag" << name << "terminal" << id << "input status" << result->load()
                    << "expected finish root" << reinterpret_cast<quintptr>(targetHandle) << "actual finish root"
                    << reinterpret_cast<quintptr>(finishRoot->load()) << "source tabs" << sourceTabs << "previews"
                    << previews;
            return result->load() == 1 && captureOk && ghostCaptured && ghostOk;
        };

        raiseOwn(&main, left);
        auto*       initial    = controller.terminalGroup(ids[0]);
        const auto  initialId  = initial->id();
        auto*       initialBar = initial->tabs().tabBar();
        const QRect lastTab    = initialBar->tabRect(2);
        QVERIFY2(drag(ids[0], initialBar, QPoint(lastTab.right() - 2, lastTab.center().y()), "Reorder", false, true),
                 "Native in-strip reorder input, ghost alpha or fixture-window hit verification failed");
        QCOMPARE(controller.terminalGroup(ids[0])->id(), initialId);
        QCOMPARE(initial->tabs().widget(0), panels[1]);
        QCOMPARE(initial->tabs().widget(1), panels[2]);
        QCOMPARE(initial->tabs().widget(2), panels[0]);
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), QVariantList({ids[1], ids[2], ids[0]}));
        QVERIFY(previews.contains("Insert terminal tab"));
        QVERIFY2(drag(ids[0], initial, QPoint(initial->width() - 8, initial->height() / 2), "Split"),
                 "Native drag input or fixture-window hit verification failed");
        QTRY_VERIFY(controller.terminalGroup(ids[0])->id() != initialId);
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().currentWidget(), panels[0]);
        QVERIFY(previews.contains("Split right"));
        QVERIFY(controller.detachTerminal(ids[0], &error));
        QVERIFY(controller.detachTerminal(ids[1], &error));
        QPointer<DetachedTerminalWindow> source(controller.detachedWindow(ids[0]));
        QPointer<DetachedTerminalWindow> target(controller.detachedWindow(ids[1]));
        QVERIFY(source && target);
        raiseOwn(source, left);
        raiseOwn(target, right);
        auto* targetGroup = controller.terminalGroup(ids[1]);
        QVERIFY2(drag(ids[0], targetGroup, targetGroup->rect().center(), "CrossWindow"),
                 "Native cross-window drag input or fixture-window hit verification failed");
        QTRY_COMPARE(controller.detachedWindow(ids[0]), target.data());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(source.isNull());
        QCOMPARE(controller.terminalGroup(ids[0]), controller.terminalGroup(ids[1]));
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().count(), 2);
        QVERIFY(previews.contains("Add to tab group"));

        raiseOwn(&main, left);
        raiseOwn(target, right);
        auto*        menu = main.menuBar();
        const QPoint outside(menu->width() - 15, menu->height() / 2);
        QVERIFY2(drag(ids[0], menu, outside, "Outside"),
                 "Native outside-workspace drag input or fixture-window hit verification failed");
        QTRY_VERIFY(controller.detachedWindow(ids[0]) && controller.detachedWindow(ids[0]) != target);
        QPointer<DetachedTerminalWindow> torn(controller.detachedWindow(ids[0]));
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().currentWidget(), panels[0]);
        QCOMPARE(controller.terminalGroup(ids[1])->tabs().currentWidget(), panels[1]);
        raiseOwn(&main, left);
        raiseOwn(torn, right);
        auto*      beforeCancel  = controller.terminalGroup(ids[0]);
        const auto cancelGroupId = beforeCancel->id();
        QVERIFY2(drag(ids[0], menu, outside, "Cancelled", true),
                 "Native cancelled drag input or fixture-window hit verification failed");
        QTest::qWait(250);
        QCOMPARE(controller.detachedWindow(ids[0]), torn.data());
        QCOMPARE(controller.terminalGroup(ids[0])->id(), cancelGroupId);
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().currentWidget(), panels[0]);
        QCOMPARE(controller.terminalIds().size(), 3);
        auto* complete = controller.terminalGroup(ids[2]);
        QVERIFY(controller.moveTerminal(ids[1], complete, Qt::Edge(0), -1, &error));
        const auto completeId   = complete->id();
        auto*      completeTabs = &complete->tabs();
        raiseOwn(&main, left);
        raiseOwn(torn, right);
        auto* destination = controller.terminalGroup(ids[0]);
        QVERIFY(drag(ids[2], destination, QPoint(8, destination->height() / 2), "WholeGroupSplit", false, false, true));
        QCOMPARE(controller.terminalGroup(ids[2]), complete);
        QCOMPARE(controller.terminalGroup(ids[1]), complete);
        QCOMPARE(&complete->tabs(), completeTabs);
        QCOMPARE(controller.detachedWindow(ids[2]), torn.data());
        QCOMPARE(complete->tabs().count(), 2);
        raiseOwn(&main, left);
        raiseOwn(torn, right);
        QVERIFY(drag(ids[2], menu, outside, "WholeGroupCancelled", true, false, true));
        QCOMPARE(controller.detachedWindow(ids[2]), torn.data());
        QCOMPARE(complete->id(), completeId);
        QVERIFY(drag(ids[2], menu, outside, "WholeGroupOutside", false, false, true));
        QPointer<DetachedTerminalWindow> wholeWindow(controller.detachedWindow(ids[2]));
        QVERIFY(wholeWindow && wholeWindow != torn);
        QCOMPARE(controller.detachedWindow(ids[1]), wholeWindow.data());
        QCOMPARE(controller.terminalGroup(ids[2]), complete);
        raiseOwn(&main, left);
        raiseOwn(wholeWindow, right);
        auto* empty = static_cast<MainWindow&>(main).terminalWorkspace().initialGroup();
        QVERIFY(drag(ids[2], empty, empty->rect().center(), "WholeGroupReturn", false, false, true));
        QVERIFY(!controller.detachedWindow(ids[2]));
        QCOMPARE(controller.terminalGroup(ids[2]), complete);
        QCOMPARE(complete->id(), completeId);
        QCOMPARE(complete->tabs().count(), 2);
        QCOMPARE(probe->attempts.load(), 0);
        if (!capture.isEmpty())
        {
            QVERIFY(main.grab().save(QDir(capture).filePath("NativeGroupsMain.png")));
            QVERIFY(torn->grab().save(QDir(capture).filePath("NativeGroupsSecondary.png")));
        }
#endif
    }

    Q_SLOT void windowTitlesAndNumbersPersist()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("lab.termaster")), "lab", &error));
        QVERIFY(_app.createSession("Debug session", &error));
        QCOMPARE(_app.mainWindow().windowTitle(), QString("lab / Debug session"));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        PluginHost::TerminalType type;
        type.id     = "windowNamesFixture";
        type.title  = "Fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Window names fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [] {
            return std::make_unique<ReconnectDevice>(std::make_shared<ReconnectDevice::Probe>());
        };
        const auto cleanup = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        host.registerTerminalType(type);
        QStringList ids;
        for (int index = 0; index < 3; ++index)
        {
            const auto requestedName = index < 2 ? QString() : QString("Readable name %1").arg(index);
            QVERIFY(_app.createTerminal(type.id, {}, requestedName, &error));
            ids.append(_app.selectedTerminal());
            const auto storedName = _app.session()->objects().value(ids.last()).toMap().value("name").toString();
            if (requestedName.isEmpty())
            {
                QCOMPARE(storedName, QString("Terminal %1").arg(index + 1));
            }
            else
            {
                QCOMPARE(storedName, requestedName);
            }
        }
        QVERIFY(controller.detachTerminal(ids[0], &error));
        QVERIFY(controller.detachTerminal(ids[1], &error));
        auto* first  = controller.detachedWindow(ids[0]);
        auto* second = controller.detachedWindow(ids[1]);
        QCOMPARE(first->number(), 2);
        QCOMPARE(second->number(), 3);
        second->setNumber(5);
        QCOMPARE(first->windowTitle(), QString("lab / Debug session / 2"));
        QCOMPARE(second->windowTitle(), QString("lab / Debug session / 5"));
        QVERIFY(controller.moveTerminal(ids[2], controller.terminalGroup(ids[0]), Qt::Edge(0), -1, &error));
        QCOMPARE(first->windowTitle(), QString("lab / Debug session / 2"));
        controller.terminalGroup(ids[0])->tabs().setCurrentIndex(0);
        QCOMPARE(first->windowTitle(), QString("lab / Debug session / 2"));
        QVERIFY(_app.saveSession(&error));
        const auto windows = _app.session()->presentation().value("terminalLayout").toMap().value("windows").toList();
        QCOMPARE(windows.size(), 2);
        QCOMPARE(windows[0].toMap().value("number").toInt(), 2);
        QCOMPARE(windows[1].toMap().value("number").toInt(), 5);
        const auto sessionId = _app.session()->id();
        QVERIFY(_app.closeSession(&error));
        QVERIFY(_app.openSession(sessionId, &error));
        QCOMPARE(controller.detachedWindow(ids[0])->number(), 2);
        QCOMPARE(controller.detachedWindow(ids[1])->number(), 5);
        QCOMPARE(controller.detachedWindow(ids[1])->windowTitle(), QString("lab / Debug session / 5"));
        QVERIFY(controller.detachTerminal(ids[2], &error));
        QCOMPARE(controller.detachedWindow(ids[2])->number(), 3);
        QCOMPARE(controller.detachedWindow(ids[0])->number(), 2);
        QCOMPARE(controller.detachedWindow(ids[1])->number(), 5);
    }

    Q_SLOT void wholeGroupMovesPreserveRuntimeAndPresentation()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("WholeGroup.termaster")), "Whole group", &error));
        QVERIFY(_app.createSession("Whole group", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        PluginHost::TerminalType type;
        type.id     = "wholeGroupFixture";
        type.title  = "Whole group fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Group fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice  = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        const auto cleanup = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        host.registerTerminalType(type);
        QStringList     ids;
        QList<QWidget*> pages;
        QList<QWidget*> headers;
        for (int index = 0; index < 3; ++index)
        {
            QVERIFY(_app.createTerminal(type.id, {}, QString("Member %1").arg(index), &error));
            ids.append(_app.selectedTerminal());
            auto* group = controller.terminalGroup(ids.last());
            pages.append(group->tabs().currentWidget());
            headers.append(group->tabs().tabBar()->tabButton(index, QTabBar::LeftSide));
        }
        auto* group = controller.terminalGroup(ids[0]);
        QVERIFY(controller.moveTerminal(ids[2], group, Qt::RightEdge, -1, &error));
        auto*      other   = controller.terminalGroup(ids[2]);
        auto*      tabs    = &group->tabs();
        auto*      toolbar = &group->toolbar();
        auto*      status  = &group->statusBar();
        const auto groupId = group->id();
        group->tabs().setCurrentIndex(0);
        auto* selected = group->tabs().currentWidget();
        auto* draft    = selected->findChild<QPlainTextEdit*>("commandDraft");
        QVERIFY(draft);
        draft->setPlainText("keep input");
        probe->available = true;
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* device = host.selectedDevice(type.id);
        QVERIFY(device);
        host.connectSelectedTerminal();
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        const auto closes = probe->closes.load();
        QVERIFY(controller.moveGroup(group, other, Qt::BottomEdge, &error));
        QCOMPARE(controller.terminalGroup(ids[0]), group);
        QCOMPARE(controller.terminalGroup(ids[1]), group);
        QCOMPARE(group->tabs().widget(0), pages[0]);
        QCOMPARE(group->tabs().widget(1), pages[1]);
        QCOMPARE(group->tabs().tabBar()->tabButton(0, QTabBar::LeftSide), headers[0]);
        QCOMPARE(group->tabs().tabBar()->tabButton(1, QTabBar::LeftSide), headers[1]);
        QVERIFY(controller.detachGroup(group, &error));
        QPointer<DetachedTerminalWindow> first(controller.detachedWindow(ids[0]));
        QVERIFY(first);
        QCOMPARE(controller.detachedWindow(ids[1]), first.data());
        QCOMPARE(&group->tabs(), tabs);
        QCOMPARE(&group->toolbar(), toolbar);
        QCOMPARE(&group->statusBar(), status);
        QCOMPARE(group->tabs().currentWidget(), selected);
        QCOMPARE(group->id(), groupId);
        QCOMPARE(draft->toPlainText(), QString("keep input"));
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        QCOMPARE(probe->closes.load(), closes);
        QTRY_COMPARE(probe->attempts.load(), 1);
        QVERIFY(controller.detachGroup(other, &error));
        auto* second = controller.detachedWindow(ids[2]);
        QVERIFY(second && second != first);
        QVERIFY(controller.moveGroup(group, other, Qt::LeftEdge, &error));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(first.isNull());
        QCOMPARE(controller.detachedWindow(ids[0]), second);
        QCOMPARE(controller.detachedWindow(ids[1]), second);
        QCOMPARE(second->terminalWorkspace().groups().size(), 2);
        QVERIFY(controller.returnWindow(second, &error));
        QCOMPARE(controller.terminalGroup(ids[0]), group);
        QVERIFY(!controller.detachedWindow(ids[0]));
        QCOMPARE(group->tabs().currentWidget(), selected);
        QCOMPARE(draft->toPlainText(), QString("keep input"));
        QTRY_COMPARE(probe->attempts.load(), 1);
        QCOMPARE(probe->closes.load(), closes);
        QVERIFY(_app.saveSession(&error));
        const auto saved = _app.session()->presentation().value("terminalLayout").toMap();
        QVERIFY(!saved.isEmpty());
        const auto sessionId = _app.session()->id();
        QVERIFY(_app.closeSession(&error));
        QVERIFY(_app.openSession(sessionId, &error));
        QCOMPARE(controller.terminalGroup(ids[0])->id(), groupId);
        QCOMPARE(controller.terminalGroup(ids[1]), controller.terminalGroup(ids[0]));
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().widget(0)->property("terminalId").toString(), ids[0]);
        QCOMPARE(controller.terminalGroup(ids[0])->tabs().widget(1)->property("terminalId").toString(), ids[1]);
        QTRY_COMPARE(probe->attempts.load(), 2);
    }

    Q_SLOT void groupedTransitionsQueueGuiSessionClosureAfterWorkerResize()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Reentry.termaster")), "Reentry", &error));
        QVERIFY(_app.createSession("Reentry", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        PluginHost::TerminalType type;
        type.id             = "groupReentryFixture";
        type.title          = "Reentry fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Reentry fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        const auto cleanup  = qScopeGuard([&] {
            probe->setResizeNotification({});
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        host.registerTerminalType(type);
        for (const int mode : {0, 1, 2})
        {
            if (!_app.session())
            {
                QVERIFY(_app.createSession("Split reentry", &error));
            }
            QVERIFY(_app.createTerminal(type.id, {}, "Reentry", &error));
            const auto id           = _app.selectedTerminal();
            auto*      target       = controller.terminalGroup(id);
            auto       active       = std::make_shared<std::atomic<bool>>(true);
            auto       workerResize = std::make_shared<std::atomic<bool>>(false);
            auto*      guiThread    = QThread::currentThread();
            bool       closed       = false;
            const auto deactivate   = qScopeGuard([&] {
                active->store(false);
                probe->setResizeNotification({});
            });
            probe->setResizeNotification([&, active, workerResize, guiThread] {
                workerResize->store(QThread::currentThread() != guiThread);
                QMetaObject::invokeMethod(
                    &_app,
                    [&, active] {
                        if (!active->exchange(false))
                        {
                            return;
                        }
                        closed = _app.closeSession(&error);
                    },
                    Qt::QueuedConnection);
            });
            const bool moved = mode == 0 ? controller.detachTerminal(id, &error) :
                               mode == 1 ? controller.moveTerminal(id, target, Qt::RightEdge, -1, &error) :
                                           controller.detachGroup(target, &error);
            QVERIFY2(moved, qPrintable(error));
            QTRY_VERIFY(closed);
            QVERIFY(workerResize->load());
            QVERIFY(!_app.session());
            QVERIFY(controller.terminalIds().isEmpty());
            QVERIFY(!controller.detachedWindow(id));
            QVERIFY(!controller.terminalGroup(id));
            QCOMPARE(probe->attempts.load(), 0);
        }
    }

    Q_SLOT void groupedPresentationPersistsAndProviderRecoveryStaysDisconnected()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("GroupSave.termaster")), "Group save", &error));
        QVERIFY(_app.createSession("Group save", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        PluginHost::TerminalType type;
        type.id             = "groupSaveFixture";
        type.title          = "Group save fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Group persistence fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        const auto cleanup  = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        host.registerTerminalType(type);
        QStringList ids;
        for (int index = 0; index < 4; ++index)
        {
            QVERIFY(_app.createTerminal(type.id, {}, QString::number(index), &error));
            ids.append(_app.selectedTerminal());
        }
        const auto sessionId = _app.session()->id();
        const auto path      = _app.session()->filePath();
        QVERIFY(controller.moveTerminal(ids[0], controller.terminalGroup(ids[1]), Qt::RightEdge, -1, &error));
        QVERIFY(controller.detachTerminal(ids[2], &error));
        QVERIFY(controller.moveTerminal(ids[3], controller.terminalGroup(ids[2]), Qt::BottomEdge, -1, &error));
        QStringList              groupIds;
        QList<QPointer<QWidget>> panels;
        for (const auto& id : ids)
        {
            auto* group = controller.terminalGroup(id);
            QVERIFY(group);
            groupIds.append(group->id());
            panels.append(group->tabs().currentWidget());
        }
        controller.terminalGroup(ids[0])->toolbar().hide();
        controller.terminalGroup(ids[3])->statusBar().hide();
        auto* shell = controller.detachedWindow(ids[2]);
        QVERIFY(shell);
        QCOMPARE(controller.detachedWindow(ids[3]), shell);
        const auto readDisk = [&] {
            QFile file(path.toString());
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
        };
        const auto baseline = readDisk();
        QVERIFY(!baseline.isEmpty());
        const QString unknownMetadata(256 * 1024, QLatin1Char('x'));
        QVERIFY(_app.session()->presentation().setValue("resizeFixtureMetadata", unknownMetadata, &error));
        const auto beforeResize = _app.session()->presentation().values();
        const auto origin       = shell->pos();
        for (int index = 0; index < 40; ++index)
        {
            shell->resize(720 + index % 8, 450 + index % 6);
            shell->move(origin + QPoint(index % 8, index % 6));
            QCoreApplication::processEvents();
        }
        QCOMPARE(_app.session()->presentation().values(), beforeResize);
        QCOMPARE(readDisk(), baseline);
        QVERIFY(controller.capturePresentation(&error));
        const auto layout = _app.session()->presentation().value("terminalLayout").toMap();
        QCOMPARE(layout.value("main").toMap().value("type").toString(), QString("split"));
        const auto windows = layout.value("windows").toList();
        QCOMPARE(windows.size(), 1);
        QCOMPARE(windows.first().toMap().value("number").toInt(), shell->number());
        QCOMPARE(windows.first().toMap().value("tree").toMap().value("type").toString(), QString("split"));
        const auto geometry = windows.first().toMap().value("geometry").toString().toLatin1();
        QVERIFY(!geometry.isEmpty());
        QCOMPARE(QByteArray::fromBase64(geometry).toBase64(), geometry);
        QCOMPARE(_app.session()->presentation().value("resizeFixtureMetadata").toString(), unknownMetadata);
        QCOMPARE(readDisk(), baseline);
        QVERIFY2(_app.saveSession(&error), qPrintable(error));
        const auto saved = _app.session()->presentation().values();
        Settings   persisted(path, {"termaster.session", "Termaster session"});
        QVERIFY(persisted.load(&error));
        QCOMPARE(persisted.value("presentation").toMap(), saved);

        const auto savedPath = path.toString() + ".saved";
        QVERIFY(QFile::rename(path.toString(), savedPath));
        QVERIFY(QDir().mkdir(path.toString()));
        auto                  restoreFile = qScopeGuard([&] {
            QDir().rmdir(path.toString());
            QFile::rename(savedPath, path.toString());
        });
        QPointer<QMainWindow> liveShell(shell);
        QVERIFY(!_app.saveSession(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(liveShell);
        QCOMPARE(controller.detachedWindow(ids[2]), liveShell.data());
        QCOMPARE(_app.session()->presentation().value("terminalLayout").toMap(), saved.value("terminalLayout").toMap());
        QVERIFY(QDir().rmdir(path.toString()));
        QVERIFY(QFile::rename(savedPath, path.toString()));
        restoreFile.dismiss();

        QVERIFY(host.unregisterTerminalType(type.id, &error));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (const auto& panel : panels)
        {
            QVERIFY(panel.isNull());
        }
        QCOMPARE(_app.session()->presentation().values(), saved);
        QVERIFY2(_app.saveSession(&error), qPrintable(error));
        QCOMPARE(_app.session()->presentation().value("terminalLayout").toMap(), saved.value("terminalLayout").toMap());
        host.registerTerminalType(type);
        for (int index = 0; index < ids.size(); ++index)
        {
            auto* group = controller.terminalGroup(ids[index]);
            QVERIFY(group);
            QCOMPARE(group->id(), groupIds[index]);
            QCOMPARE(group->toolbar().isHidden(), index == 0);
            QCOMPARE(group->statusBar().isHidden(), index == 3);
            QCOMPARE(group->tabs().currentWidget()->findChild<TerminalDisplay*>("terminalDisplay")->text(), QString {});
        }
        QVERIFY(controller.detachedWindow(ids[2]));
        QCOMPARE(controller.detachedWindow(ids[2]), controller.detachedWindow(ids[3]));
        QVERIFY(_app.saveSession(&error));
        QPointer<QMainWindow> beforeClose(controller.detachedWindow(ids[2]));
        QVERIFY(_app.closeSession(&error));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(beforeClose.isNull());
        QVERIFY(_app.openSession(sessionId, &error));
        for (int index = 0; index < ids.size(); ++index)
        {
            auto* group = controller.terminalGroup(ids[index]);
            QVERIFY(group);
            QCOMPARE(group->id(), groupIds[index]);
            QCOMPARE(group->toolbar().isHidden(), index == 0);
            QCOMPARE(group->statusBar().isHidden(), index == 3);
        }
        QVERIFY(controller.detachedWindow(ids[2]));
        QCOMPARE(controller.detachedWindow(ids[2]), controller.detachedWindow(ids[3]));
        QCOMPARE(probe->attempts.load(), 0);
    }

    Q_SLOT void terminalTabOrderPersistsWithoutReorderWrites()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Order.termaster")), "Order", &error));
        QVERIFY(_app.createSession("First", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        PluginHost::TerminalType type;
        type.id             = "orderFixture";
        type.title          = "Order fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic presentation fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        const auto cleanup  = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("orderFixture");
        });
        host.registerTerminalType(type);
        QVariantList ids;
        for (const auto& name : {"First", "Second", "Third"})
        {
            QVERIFY2(_app.createTerminal(type.id, {}, name, &error), qPrintable(error));
            ids.append(_app.selectedTerminal());
        }
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), ids);
        const auto firstSession = _app.session()->id();
        QVERIFY(_app.session()->presentation().setValue("future",
                                                        QVariantMap {
                                                            {"layout", 7}
        }));
        QVERIFY(_app.saveSession(&error));
        const auto file     = _app.session()->filePath();
        const auto readDisk = [&] {
            QFile input(file.toString());
            if (!input.open(QIODevice::ReadOnly))
            {
                return QByteArray {};
            }
            return input.readAll();
        };
        const auto baseline = readDisk();
        QVERIFY(!baseline.isEmpty());
        auto* group = controller.terminalGroup(_app.selectedTerminal());
        QVERIFY(group);
        QVERIFY(group->styleSheet().isEmpty());
        QCOMPARE(group->frameWidth(), 2);
        QVERIFY(!_app.session()->isDirty());
        QVERIFY(controller.moveTerminal(_app.selectedTerminal(), group, Qt::Edge(0), -1, &error));
        QVERIFY(!_app.session()->isDirty());
        QCOMPARE(readDisk(), baseline);

        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        const auto visibleOrder = [&] {
            QVariantList order;
            const int    current = tabs->currentIndex();
            for (int index = 0; index < tabs->count(); ++index)
            {
                tabs->setCurrentIndex(index);
                order.append(_app.selectedTerminal());
            }
            tabs->setCurrentIndex(current);
            return order;
        };
        tabs->tabBar()->moveTab(2, 0);
        tabs->tabBar()->moveTab(2, 1);
        const QVariantList reordered {ids[2], ids[1], ids[0]};
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), reordered);
        QVERIFY(_app.session()->isDirty());
        QCOMPARE(readDisk(), baseline);
        const auto selectedBeforeSave = _app.selectedTerminal();
        QVERIFY(_app.saveSession(&error));
        QVERIFY(_app.closeSession(&error));
        QVERIFY(_app.openSession(firstSession, &error));
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QCOMPARE(visibleOrder(), reordered);
        QCOMPARE(_app.selectedTerminal(), selectedBeforeSave);
        QCOMPARE(_app.session()->presentation().value("future").toMap(),
                 QVariantMap({
                     {"layout", 7}
        }));
        QVERIFY(_app.createTerminal(type.id, {}, "Fourth", &error));
        const auto fourth     = _app.selectedTerminal();
        auto       withFourth = reordered;
        withFourth.append(fourth);
        Settings persisted(file, {"termaster.session", "Termaster session"});
        QVERIFY(persisted.load(&error));
        QCOMPARE(persisted.value("presentation/tabOrder").toList(), withFourth);
        QVERIFY(_app.closeSession(&error));
        QVERIFY(_app.createSession("Second session", &error));
        const auto secondSession = _app.session()->id();
        QVERIFY(_app.createTerminal(type.id, {}, "Independent", &error));
        const QVariantList secondOrder {_app.selectedTerminal()};
        QVERIFY(_app.openSession(firstSession, &error));
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), withFourth);
        QVERIFY(_app.openSession(secondSession, &error));
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), secondOrder);

        // An unavailable provider keeps its saved slot while the other tabs move.
        QVERIFY(_app.openSession(firstSession, &error));
        tabs             = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        auto objects     = _app.session()->objects().values();
        auto unavailable = objects.value(ids[1].toString()).toMap();
        unavailable.insert("type", "lateOrderFixture");
        QVERIFY(_app.session()->objects().setValue(ids[1].toString(), unavailable));
        QVariantList withStale {ids[2], ids[1], "stale-object", ids[0], fourth};
        QVERIFY(_app.session()->presentation().setValue("tabOrder", withStale));
        QVERIFY(_app.session()->presentation().remove("terminalLayout"));
        controller.setSession(nullptr);
        controller.setSession(_app.session());
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QCOMPARE(tabs->count(), 3);
        tabs->tabBar()->moveTab(2, 0);
        const QVariantList missingOrder {fourth, ids[1], "stale-object", ids[2], ids[0]};
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), missingOrder);
        type.id = "lateOrderFixture";
        host.registerTerminalType(type);
        const auto lateCleanup = qScopeGuard([&] { host.unregisterTerminalType("lateOrderFixture"); });
        QCOMPARE(tabs->count(), 4);
        QCOMPARE(visibleOrder(), QVariantList({fourth, ids[1], ids[2], ids[0]}));
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), missingOrder);
        QVERIFY(_app.session()->objects().setValue("z-unlisted", objects.value(ids[0].toString())));
        QVERIFY(_app.session()->objects().setValue("a-unlisted", objects.value(ids[0].toString())));
        controller.setSession(nullptr);
        controller.setSession(_app.session());
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QCOMPARE(visibleOrder(), QVariantList({fourth, ids[1], ids[2], ids[0], "a-unlisted", "z-unlisted"}));
        QCOMPARE(_app.session()->presentation().value("tabOrder").toList(), missingOrder);
        QCOMPARE(probe->attempts.load(), 0);
    }

    Q_SLOT void wrapLinesCommandTracksSelectedTerminalAndSession()
    {
        auto* command = _app.commands().find("View.WrapLines");
        QVERIFY(command);
        auto* action = command->actions().first();
        QVERIFY(action->isCheckable());
        QVERIFY(_app.mainMenu().findMenu("/view")->actions("terminal").contains(action));
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(action));
        for (const auto& group : _app.mainMenu().findMenu("/terminal")->groups())
        {
            QVERIFY(!_app.mainMenu().findMenu("/terminal")->actions(group).contains(action));
        }
        QVERIFY(!action->isEnabled());
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Wrap.termaster")), "Wrap", &error));
        QVERIFY(_app.createSession("Wrap", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        PluginHost::TerminalType type;
        type.id             = "wrapFixture";
        type.title          = "Wrap fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic presentation fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        const auto cleanup  = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("wrapFixture");
        });
        host.registerTerminalType(type);
        QVERIFY(_app.createTerminal(type.id, {}, "First", &error));
        const auto first = _app.selectedTerminal();
        QVERIFY(_app.createTerminal(type.id, {}, "Second", &error));
        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.refreshTerminalActions();
        QVERIFY(action->isEnabled());
        QVERIFY(action->isChecked());
        auto* group = controller.terminalGroup(_app.selectedTerminal());
        QVERIFY(group);
        QVERIFY(group->toolbar().actions().contains(group->action("View.WrapLines")));
        auto* display = tabs->currentWidget()->findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        display->receive(QByteArray("word ").repeated(120));
        QCoreApplication::processEvents();
        QCOMPARE(display->horizontalScrollBar()->maximum(), 0);
        action->trigger();
        QCoreApplication::processEvents();
        QVERIFY(!action->isChecked());
        QVERIFY(display->horizontalScrollBar()->maximum() > 0);
        QCOMPARE(controller.selectedSettings().value("appearance/wrapLines").toBool(), false);
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("WrapToolbar.png")));
            auto* view = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/view"))->menu();
            view->popup(_app.mainWindow().mapToGlobal(QPoint(20, 70)));
            QTest::qWait(80);
            QVERIFY(view->grab().save(QDir(capture).filePath("WrapMenu.png")));
            view->hide();
        }
        tabs->setCurrentIndex(0);
        QCOMPARE(_app.selectedTerminal(), first);
        QVERIFY(action->isChecked());
        QVERIFY(!controller.selectedSettings().containsOverride("appearance/wrapLines"));
        QVERIFY(controller.selectedSettings().setValue("appearance/wrapLines", false));
        controller.applySettings();
        QVERIFY(!action->isChecked());
        action->trigger();
        QVERIFY(action->isChecked());
        const auto session = _app.session()->id();
        QVERIFY(_app.saveSession(&error));
        QVERIFY(_app.closeSession(&error));
        QVERIFY(!action->isEnabled());
        QVERIFY(_app.openSession(session, &error));
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        tabs->setCurrentIndex(0);
        QVERIFY(action->isChecked());
        tabs->setCurrentIndex(1);
        QVERIFY(!action->isChecked());
        QCOMPARE(controller.selectedSettings().value("appearance/wrapLines").toBool(), false);
        QCOMPARE(probe->attempts.load(), 0);
    }

    Q_SLOT void bulkTerminalRestoreAndClosePublishFinalState()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Batch.termaster")), "Batch", &error));
        QVERIFY(_app.createSession("Batch", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        int                      refreshes  = 0;
        const auto               cleanup    = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterAction("Batch.Probe");
            host.unregisterTerminalType("batchFixture");
        });
        PluginHost::TerminalType type;
        type.id     = "batchFixture";
        type.title  = "Batch fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Batch fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [] {
            return std::make_unique<ReconnectDevice>(std::make_shared<ReconnectDevice::Probe>());
        };
        host.registerTerminalType(std::move(type));
        PluginHost::Action action;
        action.id       = "Batch.Probe";
        action.title    = "Batch probe";
        action.group    = "settings";
        action.iconName = "sliders-horizontal";
        action.trigger  = [] { };
        action.visible  = [&] {
            ++refreshes;
            return !controller.terminalIds().isEmpty();
        };
        host.registerAction(std::move(action));
        for (int index = 0; index < 4; ++index)
        {
            QVERIFY2(_app.createTerminal("batchFixture", {}, QString::number(index), &error), qPrintable(error));
        }
        QVERIFY(_app.saveSession(&error));
        auto*      session = _app.session();
        const auto ids     = controller.terminalIds();
        auto*      tabs    = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        QSignalSpy selection(&controller, &TerminalController::selectionChanged);
        QSignalSpy tabSelection(tabs, &QTabWidget::currentChanged);
        refreshes = 0;
        controller.setSession(nullptr);
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QCOMPARE(selection.count(), 1);
        QCOMPARE(tabSelection.count(), 0);
        QCOMPARE(refreshes, 1);
        QVERIFY(controller.terminalIds().isEmpty());
        QCOMPARE(tabs->count(), 0);
        QVERIFY(controller.selectedTerminal().isEmpty());
        QVERIFY(!_app.commands().find("Batch.Probe")->actions().first()->isVisible());
        selection.clear();
        refreshes = 0;
        controller.setSession(session);
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QCOMPARE(selection.count(), 1);
        QCOMPARE(refreshes, 2); // One window menu and one populated group's controls.
        QCOMPARE(controller.terminalIds(), ids);
        QCOMPARE(controller.selectedTerminal(), session->presentation().value("tabOrder").toList().last().toString());
        QCOMPARE(tabs->count(), 4);
        QVERIFY(!tabs->signalsBlocked());
        QVERIFY(tabs->updatesEnabled());
        QVERIFY(_app.commands().find("Batch.Probe")->actions().first()->isVisible());
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().repaint();
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("TerminalBatch.png")));
        }
        selection.clear();
        tabs->blockSignals(true);
        tabs->setUpdatesEnabled(false);
        controller.closeAll();
        QCOMPARE(selection.count(), 1);
        QVERIFY(tabs->signalsBlocked());
        QVERIFY(!tabs->updatesEnabled());
        tabs->blockSignals(false);
        tabs->setUpdatesEnabled(true);
        selection.clear();
        controller.closeAll();
        QCOMPARE(selection.count(), 0);
        controller.setSession(nullptr);
        const auto reentry           = connect(&controller, &TerminalController::selectionChanged, this, [&] {
            if (!controller.terminalIds().isEmpty())
            {
                controller.closeAll();
            }
        });
        const auto disconnectReentry = qScopeGuard([&] { disconnect(reentry); });
        controller.setSession(session);
        tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        disconnect(reentry);
        QCOMPARE(selection.count(), 2);
        QVERIFY(controller.terminalIds().isEmpty());
        QVERIFY(controller.selectedTerminal().isEmpty());
        QVERIFY(!tabs->signalsBlocked());
        QVERIFY(tabs->updatesEnabled());
    }

    Q_SLOT void savingDoesNotApplyPendingRuntimeConfiguration()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Save.termaster")), "Save", &error));
        QVERIFY(_app.createSession("Save", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        auto                     probe      = std::make_shared<ReconnectDevice::Probe>();
        const auto               cleanup    = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("saveFixture");
        });
        PluginHost::TerminalType type;
        type.id     = "saveFixture";
        type.title  = "Save fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Save fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        QVERIFY(_app.createTerminal("saveFixture", {}, "Saved", &error));
        auto* editor = _app.mainWindow().findChild<CommandEditor*>();
        QVERIFY(editor);
        QVERIFY(!editor->buffered());
        QVERIFY(controller.selectedSettings().setValue("input/mode", "buffered", &error));

        QVERIFY(_app.saveSession(&error));
        QVERIFY(!editor->buffered());
        QVERIFY(_app.saveWorkspace(&error));
        QVERIFY(!editor->buffered());
        Settings persisted(_app.session()->filePath(), {"termaster.session", "Termaster session"});
        QVERIFY(persisted.load(&error));
        QCOMPARE(persisted.value("objects/" + _app.selectedTerminal() + "/settings/input/mode").toString(),
                 QString("buffered"));
        QVERIFY(_app.renameSession("Save renamed", &error));
        QVERIFY(_app.mainWindow().findChild<QLabel*>("sessionTitle")->text().contains("Save renamed"));
        QVERIFY(!editor->buffered());

        controller.applySettings();
        QVERIFY(editor->buffered());
    }

    Q_SLOT void numberedNamesUseFirstAvailablePositiveSuffix()
    {
        QCOMPARE(numberedName("Session", {}), QString("Session 1"));
        QCOMPARE(numberedName("Session", {"Session 1", "Session 3", "Session 01", "Session 0", "Other 2"}),
                 QString("Session 2"));
        QCOMPARE(numberedName("Terminal", {"Terminal 1", "Terminal 2", "Terminal 4"}), QString("Terminal 3"));
    }

    Q_SLOT void automaticSessionNamesFollowCatalogAndPreserveNames()
    {
        QTemporaryDir temporary;
        QString       error;
        const auto    cleanup = qScopeGuard([&] { _closeTemporaryWorkspace(); });
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Workspace 1.termaster")), {}, &error));
        QCOMPARE(_app.workspace()->name(), QString("Workspace 1"));
        QVERIFY(_app.createSession("Session 2", &error));
        const auto secondId = _app.session()->id();
        QVERIFY(_app.createSession({}, &error));
        QCOMPARE(_app.session()->name(), QString("Session 1"));
        const auto firstId = _app.session()->id();
        QVERIFY(_app.createSession({}, &error));
        QCOMPARE(_app.session()->name(), QString("Session 3"));
        QVERIFY(_app.openSession(firstId, &error));
        QVERIFY(_app.renameSession("Custom name", &error));
        QVERIFY(_app.createSession({}, &error));
        QCOMPARE(_app.session()->name(), QString("Session 1"));
        QVERIFY(_app.openSession(secondId, &error));
        QCOMPARE(_app.session()->name(), QString("Session 2"));
        const auto directory = _app.session()->directory();
        QVERIFY(_app.closeSession(&error));
        QVERIFY(QFile::remove(directory.join("session.yaml").toString()));
        QVERIFY(!_app.createSession({}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!_app.session());
    }

    Q_SLOT void sessionLifecycleAndPersistence()
    {
        QTemporaryDir temporary;
        QString       error;
        const auto    cleanup = qScopeGuard([&] { _closeTemporaryWorkspace(); });
        QVERIFY(_app.settings().setValue("settings/sessionTest/value", 10));
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("session.termaster")), "Sessions", &error));
        QVERIFY(_app.workspace()->settings().setValue("sessionTest/value", 20));
        QVERIFY2(_app.createSession({}, &error), qPrintable(error));
        QVERIFY(_app.session());
        const auto generated = _app.session()->name();
        QCOMPARE(generated, QString("Session 1"));
        const auto generatedNamesCapture = qEnvironmentVariable("TERMASTER_GENERATED_NAMES_CAPTURE");
        if (!generatedNamesCapture.isEmpty())
        {
            QVERIFY(QDir().mkpath(generatedNamesCapture));
            QCoreApplication::processEvents();
            QVERIFY(_app.mainWindow().grab().save(QDir(generatedNamesCapture).filePath("GeneratedSession.png")));
        }
        const auto id = _app.session()->id();
        QVERIFY(!QUuid(id).isNull());
        QVERIFY(_app.session()->setName("Saved through workspace"));
        QVERIFY(_app.saveWorkspace(&error));
        QCOMPARE(_app.mainWindow().findChild<QLabel*>("sessionTitle")->text(), QString("Saved through workspace"));
        const auto directory = _app.session()->directory();
        const auto file      = _app.session()->filePath();
        QCOMPARE(directory.name(), id);
        QCOMPARE(directory.parent().toString(), _app.workspace()->sessionsDirectory().toString());
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 20);
        QVERIFY(_app.configuration().setValue("sessionTest/value", 30));
        QCOMPARE(_app.workspace()->settings().value("sessionTest/value").toInt(), 20);
        QCOMPARE(_app.settings().value("settings/sessionTest/value").toInt(), 10);
        QVERIFY(_app.configuration().remove("sessionTest/value"));
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 20);
        QVERIFY(_app.workspace()->settings().remove("sessionTest/value"));
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 10);
        QVERIFY(_app.configuration().setValue("sessionTest/value", 31));
        const QVariantMap object {
            {    "type",                          "terminal"},
            {  "module",                           "fixture"},
            {"settings", QVariantMap {{"port", "synthetic"}}}
        };
        QVERIFY(_app.session()->objects().setValue("source", object));
        QVERIFY(_app.session()->objects().setValue("target", object));
        QVERIFY(
            _app.session()->links().setValue("connection",
                                             QVariantMap {
                                                 {"source", QVariantMap {{"object", "source"}, {"port", "output"}}},
                                                 {"target",  QVariantMap {{"object", "target"}, {"port", "input"}}}
        }));
        QVERIFY(_app.session()->resources().setValue("script", "resources/example.txt"));
        QVERIFY(_app.session()->presentation().setValue("layout", "horizontal"));
        QVERIFY(_app.renameSession("Renamed session", &error));
        QCOMPARE(_app.session()->id(), id);
        QCOMPARE(_app.session()->filePath().toString(), file.toString());
        auto* title = _app.mainWindow().findChild<QLabel*>("sessionTitle");
        QVERIFY(title);
        QVERIFY(title->text().contains("Renamed session"));
        auto* sessionFile = _app.mainWindow().findChild<QLineEdit*>("sessionFile");
        QVERIFY(sessionFile);
        QCOMPARE(sessionFile->text(), file.toString());
        QVERIFY(_app.mainWindow().findChild<QLabel*>("sessionSummary"));
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Session.png")));
        }
        QVERIFY(_app.saveSession(&error));
        QVERIFY(_app.saveWorkspace(&error));
        QVERIFY(_app.settings().sync(&error));
        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_RESTART_DATA", QFileInfo(_app.settings().filePath()).dir().absolutePath());
        environment.insert("TERMASTER_APP_RESTART_SESSION", id);
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), QStringList());
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
        Settings childSaved(file, {"termaster.session", "Termaster session"});
        QVERIFY(childSaved.load(&error));
        QCOMPARE(childSaved.value("settings/sessionTest/childAutosave").toInt(), 99);
        // Startup restoration updates the child's workspace history as well as the session.
        QVERIFY2(_app.workspace()->load(&error), qPrintable(error));
        QVERIFY2(_app.session()->load(&error), qPrintable(error));
        auto borrowed = _app.session()->objects();
        QVERIFY2(_app.closeSession(&error), qPrintable(error));
        QVERIFY(!borrowed.isValid());
        QVERIFY(!_app.session());
        QVERIFY2(_app.workspace()->load(&error), qPrintable(error));
        QVERIFY(_app.openSession(id, &error));
        QCOMPARE(_app.session()->name(), QString("Renamed session"));
        QCOMPARE(_app.configuration().value("sessionTest/value").toInt(), 31);
        QCOMPARE(_app.session()->objects().values().size(), 2);
        QCOMPARE(_app.session()->links().values().size(), 1);
        QCOMPARE(_app.session()->resources().value("script").toString(), QString("resources/example.txt"));
        QCOMPARE(_app.session()->presentation().value("layout").toString(), QString("horizontal"));
        QVERIFY(_app.configuration().setValue("sessionTest/switchSaved", 7));
        auto oldView = _app.session()->settings();
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("other.termaster")), "Other", &error));
        QVERIFY(!_app.session());
        QVERIFY(!oldView.isValid());
        Settings saved(file, {"termaster.session", "Termaster session"});
        QVERIFY(saved.load(&error));
        QCOMPARE(saved.value("settings/sessionTest/switchSaved").toInt(), 7);
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void workspaceRestoresItsLastSession()
    {
        QTemporaryDir temporary;
        QString       error;
        const auto    cleanup = qScopeGuard([&] { _closeTemporaryWorkspace(); });
        const Path    first(temporary.filePath("first.termaster"));
        const Path    second(temporary.filePath("second.termaster"));
        QVERIFY(_app.createWorkspace(first, "First", &error));
        QVERIFY(_app.createSession("First session", &error));
        const auto firstId = _app.session()->id();
        QTRY_COMPARE(_app.workspace()->state().value("lastSession").toString(), firstId);
        QVERIFY(_app.createWorkspace(second, "Second", &error));
        QVERIFY(!_app.session());
        QVERIFY(_app.createSession("Second session", &error));
        const auto secondId = _app.session()->id();
        QVERIFY(_app.openWorkspace(first, &error));
        QVERIFY(_app.session());
        QCOMPARE(_app.session()->id(), firstId);
        QVERIFY(!_app.openSession("missing", &error));
        QTRY_COMPARE(_app.workspace()->state().value("lastSession").toString(), firstId);
        QVERIFY(_app.openWorkspace(second, &error));
        QVERIFY(_app.session());
        QCOMPARE(_app.session()->id(), secondId);
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(_app.openWorkspace(second, &error));
        QVERIFY(_app.session());
        QCOMPARE(_app.session()->id(), secondId);
        QVERIFY(_app.closeSession(&error));
        QVERIFY(!_app.workspace()->state().contains("lastSession"));
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(_app.openWorkspace(second, &error));
        QVERIFY(!_app.session());
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void unavailableLastSessionLeavesWorkspaceUsable()
    {
        QTemporaryDir temporary;
        QString       error;
        const auto    cleanup = qScopeGuard([&] { _closeTemporaryWorkspace(); });
        const Path    file(temporary.filePath("restore.termaster"));
        QVERIFY(_app.createWorkspace(file, "Restore", &error));
        QVERIFY(_app.createSession("Saved session", &error));
        const auto id          = _app.session()->id();
        const auto sessionFile = _app.session()->filePath().toString();
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(QFile::remove(sessionFile));
        QVERIFY(_app.openWorkspace(file, &error));
        QVERIFY(_app.workspace());
        QVERIFY(!_app.session());
        QTRY_COMPARE(_app.workspace()->state().value("lastSession").toString(), id);
        QVERIFY(_app.mainWindow().statusBar()->currentMessage().contains("Cannot restore last session"));
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(_app.closeWorkspace(&error));
        QFile invalid(sessionFile);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QVERIFY(invalid.write("version: [invalid") > 0);
        invalid.close();
        QVERIFY(_app.restoreLastWorkspace(&error));
        QVERIFY(_app.workspace());
        QVERIFY(!_app.session());
        QVERIFY(_app.mainWindow().statusBar()->currentMessage().contains("Cannot restore last session"));
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(_app.closeSession(&error));
        QVERIFY(!_app.workspace()->state().contains("lastSession"));
        QVERIFY(_app.createSession("Replacement", &error));
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void sessionFailureRetainsOwners()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("failures.termaster")), "Failures", &error));
        QVERIFY(_app.createSession("Original", &error));
        auto*      original  = _app.session();
        auto*      workspace = _app.workspace();
        const auto history   = workspace->recentSessions();
        QVERIFY(!_app.openSession("missing", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(_app.session(), original);
        QVERIFY(workspace->sessions().setValue("missing",
                                               QVariantMap {
                                                   {"directory", "sessions/missing"}
        }));
        QVERIFY(!_app.openSession("missing", &error));
        QCOMPARE(_app.session(), original);
        const auto badDirectory = workspace->sessionsDirectory().join("bad");
        QVERIFY(badDirectory.createDirectories());
        QVERIFY(workspace->registerSession("bad", badDirectory));
        QFile badFile(badDirectory.join("session.yaml").toString());
        QVERIFY(badFile.open(QIODevice::WriteOnly));
        QVERIFY(badFile.write("version: [invalid") > 0);
        badFile.close();
        QVERIFY(!_app.openSession("bad", &error));
        QCOMPARE(_app.session(), original);
        QVERIFY(QFile::remove(badFile.fileName()));
        QVERIFY(QDir().rmdir(badDirectory.toString()));
        Session mismatch(badDirectory, workspace->settings());
        QVERIFY(mismatch.create("different-id", "Mismatch", &error));
        QVERIFY(!_app.openSession("bad", &error));
        QCOMPARE(_app.session(), original);
        QCOMPARE(workspace->recentSessions(), history);
        const auto catalogBefore = workspace->sessionIds();
        const auto foldersBefore = QDir(workspace->sessionsDirectory().toString())
                                       .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        const auto workspaceFile = workspace->filePath().toString();
        QVERIFY(QFile::rename(workspaceFile, workspaceFile + ".backup"));
        QVERIFY(QDir().mkdir(workspaceFile));
        QVERIFY(!_app.createSession("Cannot persist catalog", &error));
        QCOMPARE(_app.session(), original);
        QTRY_COMPARE(workspace->state().value("lastSession").toString(), original->id());
        QVERIFY(!_app.closeSession(&error));
        QCOMPARE(_app.session(), original);
        QTRY_COMPARE(workspace->state().value("lastSession").toString(), original->id());
        QCOMPARE(workspace->sessionIds(), catalogBefore);
        QCOMPARE(workspace->recentSessions(), history);
        QCOMPARE(QDir(workspace->sessionsDirectory().toString()).entryList(QDir::Dirs | QDir::NoDotAndDotDot),
                 foldersBefore);
        QVERIFY(QDir().rmdir(workspaceFile));
        QVERIFY(QFile::rename(workspaceFile + ".backup", workspaceFile));
        QVERIFY(original->settings().setValue("pending", 1));
        const auto file   = original->filePath().toString();
        const auto backup = file + ".backup";
        QVERIFY(QFile::rename(file, backup));
        QVERIFY(QDir().mkdir(file));
        QVERIFY(!_app.closeSession(&error));
        QVERIFY(!_app.closeWorkspace(&error));
        QCOMPARE(_app.workspace(), workspace);
        QCOMPARE(_app.session(), original);
        QVERIFY(original->isDirty());
        QVERIFY(QDir().rmdir(file));
        QVERIFY(QFile::rename(backup, file));
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void globalShortcutDraftPersistence()
    {
        const auto original = _app.settings().value("settings/shortcuts", QVariantMap {});
        const auto cleanup  = qScopeGuard([&] {
            for (auto* dialog : _app.mainWindow().findChildren<SettingsDialog*>())
            {
                delete dialog;
            }
            _app.commands().remove("Fixture.Shortcut");
            _app.commands().remove("Fixture.Late");
            _app.settings().setValue("settings/shortcuts", original);
        });
        auto* command = _app.commands().create("Fixture.Shortcut", "Fixture command", {}, QKeySequence("Ctrl+Alt+F8"));
        auto* action  = command->newAction(&_app.mainWindow());
        _app.mainWindow().addAction(action);
        const auto open = [&]() {
            _app.commands().find("Application.Settings")->actions().first()->trigger();
            auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
            if (dialog)
            {
                dialog->selectPage("application.shortcuts");
            }
            return dialog;
        };
        QPointer<SettingsDialog> dialog(open());
        QVERIFY(dialog);
        auto* model = dialog->pageModel("application.shortcuts");
        QVERIFY(model);
        auto* search   = dialog->findChild<QLineEdit*>("shortcutSearch");
        auto* table    = dialog->findChild<QTableView*>("shortcutTable");
        auto* sequence = dialog->findChild<QKeySequenceEdit*>("shortcutSequence");
        QVERIFY(search && table && sequence);
        search->setText("Fixture.Shortcut");
        QCOMPARE(table->model()->rowCount(), 1);
        table->selectRow(0);
        sequence->setKeySequence(QKeySequence("Ctrl+Alt+F9"));
        QVERIFY(model->isDirty());
        search->setText("Fixture command");
        QCOMPARE(table->model()->rowCount(), 1);
        search->setText("Ctrl+Alt+F9");
        QCOMPARE(table->model()->rowCount(), 1);
        search->setText("Fixture.Shortcut");
        table->selectRow(0);
        sequence->setKeySequence(_app.commands().find("Workspace.New")->defaultShortcut());
        QVERIFY(model->isValid());
        auto conflicting = model->value("assignments").toMap();
        conflicting
            .insert("Workspace.New",
                    _app.commands().find("Workspace.New")->defaultShortcut().toString(QKeySequence::PortableText));
        model->setValue("assignments", conflicting);
        QVERIFY(!model->isValid());
        QVERIFY(model->errors().first().message.contains("conflict", Qt::CaseInsensitive));
        table->selectRow(0);
        dialog->findChild<QPushButton*>("shortcutReset")->click();
        QVERIFY(model->isValid());
        QCOMPARE(sequence->keySequence(), QKeySequence("Ctrl+Alt+F8"));
        dialog->findChild<QPushButton*>("shortcutClear")->click();
        QVERIFY(sequence->keySequence().isEmpty());
        QVERIFY(model->value("assignments").toMap().contains("Fixture.Shortcut"));
        dialog->findChild<QPushButton*>("shortcutResetAll")->click();
        QVERIFY(!model->value("assignments").toMap().contains("Fixture.Shortcut"));
        QCOMPARE(command->shortcut(), QKeySequence("Ctrl+Alt+F8"));
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(command->shortcut(), QKeySequence("Ctrl+Alt+F8"));
        dialog                  = open();
        model                   = dialog->pageModel("application.shortcuts");
        QVariantMap assignments = original.toMap();
        assignments.insert("Fixture.Shortcut", "Ctrl+Alt+F9");
        assignments.insert("Fixture.Late", "Ctrl+Alt+F10");
        model->setValue("assignments", assignments);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(command->shortcut(), QKeySequence("Ctrl+Alt+F9"));
        QCOMPARE(action->shortcut(), QKeySequence("Ctrl+Alt+F9"));
        Settings persisted(_app.settings().filePath(), {"termaster.application", "Termaster application settings"});
        QString  error;
        QVERIFY2(persisted.load(&error), qPrintable(error));
        QCOMPARE(persisted.value("settings/shortcuts").toMap(), assignments);
        auto* late = _app.commands().create("Fixture.Late", "Late plugin", {}, QKeySequence("Ctrl+Alt+F11"));
        QCOMPARE(late->shortcut(), QKeySequence("Ctrl+Alt+F10"));
        _app.commands().remove("Fixture.Late");
        late = _app.commands().create("Fixture.Late", "Reloaded plugin");
        QCOMPARE(late->shortcut(), QKeySequence("Ctrl+Alt+F10"));
        assignments.insert("Fixture.Shortcut", "");
        model->setValue("assignments", assignments);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QVERIFY(action->shortcut().isEmpty());
        auto capture = qEnvironmentVariable("TERMASTER_PREFERENCES_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            dialog->setFixedSize(1100, 760);
            auto* filter = dialog->findChild<QLineEdit*>("shortcutSearch");
            filter->setText("Workspace");
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(capture).filePath("Shortcuts.png")));
            dialog->selectPage("application.terminal");
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(capture).filePath("Appearance.png")));
            auto* family = dialog->findChild<QComboBox*>("settingsFontFamily");
            QVERIFY(family);
            family->showPopup();
            QTest::qWait(100);
            auto* popup = family->view()->window();
            QVERIFY(popup->isVisible());
            auto     image = dialog->grab();
            QPainter painter(&image);
            painter.drawPixmap(dialog->mapFromGlobal(popup->mapToGlobal(QPoint())), popup->grab());
            painter.end();
            QVERIFY(image.save(QDir(capture).filePath("FontPopup.png")));
            family->hidePopup();
            dialog->selectPage("application.appearance");
            dialog->pageModel("application.appearance")->setValue("statusBar", false);
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(capture).filePath("Reset.png")));
        }
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QProcess child;
        auto     environment = QProcessEnvironment::systemEnvironment();
        environment.insert("TERMASTER_APP_RESTART_DATA", QFileInfo(_app.settings().filePath()).dir().absolutePath());
        environment.insert("TERMASTER_SHORTCUT_RESTART", "1");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(child.waitForFinished(30000));
        QCOMPARE(child.exitCode(), 0);
    }

    Q_SLOT void actionLayoutSettingsDrafts()
    {
        const auto original = _app.settings().value("settings/ui/terminalGroups");
        const auto cleanup  = qScopeGuard([&] {
            for (auto* dialog : _app.mainWindow().findChildren<SettingsDialog*>())
            {
                delete dialog;
            }
            if (original.isValid())
            {
                _app.settings().setValue("settings/ui/terminalGroups", original);
            }
            else
            {
                _app.settings().remove("settings/ui/terminalGroups");
            }
        });
        const auto open     = [&] {
            _app.commands().find("Application.Settings")->actions().first()->trigger();
            auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
            if (dialog)
            {
                dialog->selectPage("application.appearance");
            }
            return dialog;
        };
        QPointer<SettingsDialog> dialog(open());
        QVERIFY(dialog);
        auto*      model   = dialog->pageModel("application.appearance");
        const auto editors = dialog->findChildren<ActionLayoutEditor*>();
        QCOMPARE(editors.size(), 4);
        auto*              editor = editors.first();
        const QVariantList selectors {"command:Unavailable.Plugin", "group:connection"};
        model->setValue("embeddedToolbar", selectors);
        QCOMPARE(editor->value().toList(), selectors);
        auto* list = editor->findChild<QListView*>("actionLayoutList");
        list->setCurrentIndex(list->model()->index(1, 0));
        editor->findChild<QPushButton*>("actionLayoutUp")->click();
        const QVariantList reordered {"group:connection", "command:Unavailable.Plugin"};
        QCOMPARE(model->value("embeddedToolbar").toList(), reordered);
        QVERIFY(model->isValid());
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(_app.settings().value("settings/ui/terminalGroups"), original);

        dialog = open();
        QVERIFY(dialog);
        model  = dialog->pageModel("application.appearance");
        editor = dialog->findChildren<ActionLayoutEditor*>().first();
        model->setValue("embeddedToolbar", selectors);
        auto* selector = editor->findChild<QComboBox*>("actionLayoutSelector");
        selector->setEditText("command:Future.Node");
        editor->findChild<QPushButton*>("actionLayoutAdd")->click();
        auto expected = selectors;
        expected.append("command:Future.Node");
        QCOMPARE(model->value("embeddedToolbar").toList(), expected);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(_app.settings().value("settings/ui/terminalGroups/embedded/toolbar").toList(), expected);
        list = editor->findChild<QListView*>("actionLayoutList");
        while (list->model()->rowCount())
        {
            list->setCurrentIndex(list->model()->index(0, 0));
            editor->findChild<QPushButton*>("actionLayoutRemove")->click();
        }
        QVERIFY(model->value("embeddedToolbar").toList().isEmpty());
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QVERIFY(_app.settings().value("settings/ui/terminalGroups/embedded/toolbar").toList().isEmpty());
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
    }

    Q_SLOT void disabledPluginHelpPackageDiscovery()
    {
#if defined(HELPER_PLUGIN_FILE) && defined(HELPER_QCH_FILE)
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = temporary.filePath("plugin");
        const QString bin  = root + "/bin";
        QVERIFY(QDir().mkpath(bin + "/help"));
        const QString library = bin + '/' + QFileInfo(HELPER_PLUGIN_FILE).fileName();
        const QString package = bin + "/help/ProcessorTutorial.qch";
        QVERIFY(QFile::copy(HELPER_PLUGIN_FILE, library));
        QVERIFY(QFile::copy(HELPER_QCH_FILE, package));
        Settings preferences(temporary.filePath("preferences.yaml"));
        QVERIFY(preferences.load());
        QVERIFY(preferences.setValue("enabled/ProcessorTutorialPlugin", false));
        QVERIFY(preferences.sync());
        PluginManager manager(preferences, root);
        QSignalSpy    started(&manager, &PluginManager::pluginStarted);
        QString       error;
        // start() activates discovery; persisted false prevents every plugin startup.
        QVERIFY2(manager.start(&error), qPrintable(error));
        QCOMPARE(manager.plugins().size(), 1);
        QCOMPARE(manager.plugins().first().status, PluginManager::Status::Disabled);
        QVERIFY(!manager.processors().type("sample.prefix"));
        QWidget         parent;
        CommandRegistry commands;
        HelpController  controller(commands, manager, &parent);
        controller.showTopic("example.prefix");
        auto* browser = parent.findChild<HelpBrowser*>("helpBrowser");
        QVERIFY(browser);
        QCOMPARE(browser->currentTopic(), QString("example.prefix"));
        QVERIFY(browser->namespaces().contains("org.termaster.example.prefix"));
        auto* text   = browser->findChild<QTextBrowser*>("helpText");
        auto* status = browser->findChild<QLabel*>("helpStatus");
        QVERIFY(text && status);
        QCOMPARE(text->source().scheme(), QString("qthelp"));
        QVERIFY(text->toPlainText().contains("sample.prefix"));
        const auto retained = text->toPlainText();
        QCOMPARE(started.count(), 0);
        QVERIFY(QFile::remove(library));
        QVERIFY(QFile::remove(package));
        QVERIFY2(manager.rescan(&error), qPrintable(error));
        QTRY_VERIFY(manager.plugins().isEmpty());
        QTRY_VERIFY(!browser->namespaces().contains("org.termaster.example.prefix"));
        QCOMPARE(text->toPlainText(), retained);
        QVERIFY(status->text().contains("unavailable", Qt::CaseInsensitive));
        QVERIFY(!manager.processors().type("sample.prefix"));
        QCOMPARE(started.count(), 0);
#else
        QSKIP("Build with the HELP tutorial fixture to verify disabled-plugin package discovery.");
#endif
    }

    Q_SLOT void contextualHelpPreservesDrafts()
    {
        int                            factoryCalls = 0;
        ProcessorRegistry              registry;
        QPointer<SettingsDialog>       settings;
        QPointer<LineflowEditorDialog> lineflow;
        const auto                     cleanup    = qScopeGuard([&] {
            delete _app.mainWindow().findChild<QDialog*>("helpDialog");
            delete settings.data();
            delete lineflow.data();
        });
        const auto                     helpDialog = [&] { return _app.mainWindow().findChild<QDialog*>("helpDialog"); };
        const auto                     capture    = [&](const QString& name) {
            const auto directory = qEnvironmentVariable("TERMASTER_HELP_CAPTURE");
            if (directory.isEmpty())
            {
                return true;
            }
            QApplication::processEvents();
            QTest::qWait(150);
            return helpDialog() && QDir().mkpath(directory)
                && helpDialog()->grab().save(QDir(directory).filePath(name + ".png"));
        };
        auto* contents = _app.commands().find("Help.Contents");
        QVERIFY(contents);
        QCOMPARE(contents->defaultShortcut(), QKeySequence(Qt::Key_F1));
        QCOMPARE(contents->shortcut(), QKeySequence(Qt::Key_F1));
        QVERIFY(_app.mainMenu().findMenu("/help"));
        QVERIFY(_app.mainMenu().findMenu("/help")->actions("documentation").contains(contents->actions().first()));
        QVERIFY(QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("help/Termaster.qch")));

        auto native = _app.plugins().processors().type("native.pattern-framer");
        QVERIFY(native.has_value());
        native->create = [&](const QVariantMap&, const Processor::Limits&, QString*) -> std::unique_ptr<Processor> {
            ++factoryCalls;
            return {};
        };
        registry.registerType("help-fixture", std::move(*native));
        const auto savedSettings  = _app.settings().values();
        const auto savedSession   = _app.session();
        const auto savedWorkspace = _app.workspace();
        _app.mainWindow().activateWindow();
        _app.mainWindow().setFocus();
        contents->actions().first()->trigger();
        QTRY_VERIFY(helpDialog() && helpDialog()->isVisible());
        auto* browser = helpDialog()->findChild<HelpBrowser*>("helpBrowser");
        QVERIFY(browser);
        QVERIFY(!browser->namespaces().isEmpty());
        QTRY_VERIFY(browser->engine().contentModel()->rowCount() > 0);
        QCOMPARE(browser->currentTopic(), QString("index"));
        auto* text = browser->findChild<QTextBrowser*>("helpText");
        QVERIFY(text);
        QCOMPARE(text->source().scheme(), QString("qthelp"));
        QVERIFY(text->toPlainText().contains("Workspaces and sessions"));
        QVERIFY(text->toPlainText().contains("Keyboard shortcuts"));
        QVERIFY(!text->toPlainText().contains("Help topic unavailable"));
        QVERIFY(capture("MainHelp"));
        helpDialog()->close();
        QTRY_VERIFY(!helpDialog());

        _app.commands().find("Application.Settings")->actions().first()->trigger();
        settings = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(settings);
        QVERIFY(settings->selectPage("application.appearance"));
        auto* model = settings->pageModel("application.appearance");
        QVERIFY(model);
        const bool draftValue = !model->value("statusBar").toBool();
        model->setValue("statusBar", draftValue);
        QVERIFY(model->isDirty());
        QSignalSpy applied(settings, &SettingsDialog::applied);
        auto*      reset = settings->findChild<QToolButton*>("reset_statusBar");
        QVERIFY(reset);
        auto* checkbox = reset->parentWidget()->findChild<QCheckBox*>();
        QVERIFY(checkbox);
        settings->activateWindow();
        checkbox->setFocus();
        QTRY_VERIFY(checkbox->hasFocus());
        QTest::keyClick(checkbox, Qt::Key_F1);
        QTRY_VERIFY(helpDialog() && helpDialog()->isVisible());
        browser = helpDialog()->findChild<HelpBrowser*>("helpBrowser");
        QVERIFY(browser);
        QVERIFY(browser->currentTopic().startsWith("settings.application.appearance"));
        text = browser->findChild<QTextBrowser*>("helpText");
        QVERIFY(text);
        QVERIFY(text->toPlainText().contains("Parameters"));
        QVERIFY(text->toPlainText().contains("Show status bar"));
        QVERIFY(capture("SettingsHelp"));
        QVERIFY(model->isDirty());
        QCOMPARE(model->value("statusBar").toBool(), draftValue);
        QCOMPARE(applied.count(), 0);
        QCOMPARE(_app.settings().values(), savedSettings);
        helpDialog()->close();
        QTRY_VERIFY(!helpDialog());
        auto* helpButton = settings->findChild<QPushButton*>("contextualHelp");
        QVERIFY(helpButton);
        helpButton->click();
        QTRY_VERIFY(helpDialog() && helpDialog()->isVisible());
        browser = helpDialog()->findChild<HelpBrowser*>("helpBrowser");
        QVERIFY(browser);
        QVERIFY(browser->currentTopic().startsWith("settings.application.appearance"));
        QCOMPARE(applied.count(), 0);
        QVERIFY(model->isDirty());
        helpDialog()->close();
        QTRY_VERIFY(!helpDialog());
        QVERIFY(settings->selectPage("application.shortcuts"));
        auto* shortcutSearch = settings->findChild<QLineEdit*>("shortcutSearch");
        shortcutSearch->setText("Help.Contents");
        auto* shortcutTable = settings->findChild<QTableView*>("shortcutTable");
        shortcutTable->selectRow(0);
        auto* recorder = settings->findChild<QKeySequenceEdit*>("shortcutSequence");
        settings->activateWindow();
        recorder->setFocus();
        QTRY_VERIFY(recorder->hasFocus());
        QTest::keyClick(recorder, Qt::Key_F1);
        QTRY_COMPARE(recorder->keySequence(), QKeySequence(Qt::Key_F1));
        QVERIFY(!helpDialog());

        settings->reject();
        QTRY_VERIFY(!settings);
        QCOMPARE(_app.settings().values(), savedSettings);

        const QVariantMap child {
            {    "id",                                       "shared"                      },
            {  "name",                                                     "Shared framing"},
            {  "kind",                                                           "lineflow"},
            {"source",
             QVariantMap {{"nodes",
             QVariantList {QVariantMap {{"name", "frame"},
             {"type", "native.pattern-framer"},
             {"configuration", QVariantMap {}}}}}}                                         }
        };
        const QVariantMap root {
            {    "id",                                                            "root"                      },
            {  "name",                                                                           "Nested help"},
            {  "kind",                                                                              "lineflow"},
            {"source",
             QVariantMap {{"nodes", QVariantList {QVariantMap {{"name", "nested"}, {"reference", "shared"}}}}}}
        };
        int applyCalls = 0;
        lineflow       = new LineflowEditorDialog(
            {
                {  "root",  root},
                {"shared", child}
        },
            registry,
            _app.settingsWidgets(),
            [&](const auto&, const auto&, const auto&, QString*) {
                ++applyCalls;
                return LineflowEditorDialog::ApplyResult(false);
            },
            &_app.mainWindow());
        lineflow->show();
        lineflow->activateWindow();
        auto* canvas = lineflow->findChild<QAbstractItemView*>("lineflowCanvas");
        QVERIFY(canvas);
        const auto parent = lineflow->model().index(0, 0);
        const auto nested = lineflow->model().index(0, 0, parent);
        QVERIFY(nested.isValid());
        QVERIFY(nested.data(LineflowEditorModel::LinkedRole).toBool());
        QCOMPARE(nested.data(LineflowEditorModel::CatalogIdRole).toString(), QString("native.pattern-framer"));
        canvas->setCurrentIndex(nested);
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        QTest::keyClick(canvas, Qt::Key_F1);
        QTRY_VERIFY(helpDialog() && helpDialog()->isVisible());
        browser = helpDialog()->findChild<HelpBrowser*>("helpBrowser");
        QVERIFY(browser);
        QCOMPARE(browser->currentTopic(), QString("processor.native.pattern-framer"));
        QVERIFY(browser->openTopic("processor.native.pattern-framer")); // Packaged manual reference exists.
        text = browser->findChild<QTextBrowser*>("helpText");
        QVERIFY(text);
        QVERIFY(text->toPlainText().contains("Parameters"));
        QVERIFY(capture("NodeHelp"));
        QCOMPARE(factoryCalls, 0);
        QCOMPARE(applyCalls, 0);
        QCOMPARE(_app.settings().values(), savedSettings);
        QCOMPARE(_app.session(), savedSession);
        QCOMPARE(_app.workspace(), savedWorkspace);
    }

    Q_SLOT void generatedSettingsDialog()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.settings().setValue("settings/ui/statusBarVisible", true));
        QVERIFY(_app.settings().setValue("settings/ui/statusBarVisible", true));
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Settings.termaster")), "Settings", &error));
        const auto cleanup = qScopeGuard([&] {
            for (auto* dialog : _app.mainWindow().findChildren<SettingsDialog*>())
            {
                delete dialog;
            }
            _app.closeWorkspace();
            auto* action = _app.commands().find("View.StatusBar")->actions().first();
            if (!action->isChecked())
            {
                action->trigger();
            }
        });
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> dialog(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(dialog);
        QVERIFY(dialog->pageModel("application.appearance"));
        QVERIFY(!dialog->pageModel("workspace.terminal"));
        QVERIFY(!dialog->pageModel("terminal.connection"));
        auto* model = dialog->pageModel("application.appearance");
        model->setValue("statusBar", false);
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        auto* reset = dialog->findChild<QToolButton*>("reset_statusBar");
        QVERIFY(reset && !reset->isHidden());
        auto* booleanEditor = reset->parentWidget()->findChild<QCheckBox*>();
        QVERIFY(booleanEditor);
        QApplication::processEvents();
        const int gap = reset->mapTo(dialog, QPoint()).x()
                      - booleanEditor->mapTo(dialog, QPoint(booleanEditor->width(), 0)).x();
        QVERIFY2(gap >= 0 && gap <= 24, qPrintable(QString::number(gap)));
        const auto interfaceCapture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!interfaceCapture.isEmpty())
        {
            QVERIFY(QDir().mkpath(interfaceCapture));
            dialog->setFixedSize(1280, 800);
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(interfaceCapture).filePath("InterfaceReset.png")));
        }
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        model  = dialog->pageModel("application.appearance");
        model->setValue("statusBar", false);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QVERIFY(_app.toolbar().toolbar()->isHidden());
        QVERIFY(!_app.settings().value("settings/ui/statusBarVisible").toBool());
        QVERIFY(!_app.workspace()->settings().containsOverride("ui/statusBarVisible"));
        QVERIFY(_app.createSession("Settings session", &error));
        QVERIFY(dialog);
        auto* applicationDraft = dialog->pageModel("application.terminal");
        applicationDraft->setValue("fontSize", 18.0);
        _app.commands().find("Session.Settings")->actions().first()->trigger();
        QVERIFY(dialog);
        QVERIFY(applicationDraft->isDirty());
        for (auto* candidate : _app.mainWindow().findChildren<SettingsDialog*>("settingsDialog"))
        {
            if (candidate->pageModel("session.terminal"))
            {
                dialog = candidate;
            }
        }
        QVERIFY(dialog->pageModel("session.terminal"));
        QVERIFY(!dialog->pageModel("application.appearance"));
        QVERIFY(!dialog->pageModel("workspace.terminal"));
        auto*      font      = dialog->pageModel("session.terminal");
        const auto inherited = font->value("fontSize");
        font->setValue("fontSize", 19.0);
        QCOMPARE(_app.session()->settings().value("appearance/fontSize", inherited), inherited);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(_app.session()->settings().value("appearance/fontSize").toDouble(), 19.0);
        QVERIFY(!_app.workspace()->settings().containsOverride("appearance/fontSize"));
        QVERIFY(_app.closeSession(&error));
        QTRY_VERIFY(dialog.isNull());
    }

    Q_SLOT void preferencesNavigationSearchAndPreview()
    {
        QString error;
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> dialog(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(dialog);
        const auto cleanup = qScopeGuard([&] {
            if (dialog)
            {
                delete dialog.data();
            }
        });
        auto*      tree    = dialog->findChild<QTreeView*>("settingsTree");
        auto*      search  = dialog->findChild<QLineEdit*>("settingsSearch");
        QVERIFY(tree);
        QVERIFY(search);
        QCOMPARE(tree->model()->rowCount(), 2);
        QVERIFY(dialog->selectPage("application.terminal"));
        auto*      model         = dialog->pageModel("application.terminal");
        const auto inheritedSize = model->value("fontSize");
        auto*      preview       = dialog->findChild<TerminalDisplay*>("settingsAppearancePreview");
        QVERIFY(preview);
        model->setValue("fontSize", 21.0);
        QCOMPARE(preview->font().pointSizeF(), 21.0);
        model->setValue("fontSize", 72.0);
        QApplication::processEvents();
        const auto previewImage      = preview->viewport()->grab().toImage();
        const auto previewBackground = QColor(model->value("background").toString()).rgb();
        bool       visibleText       = false;
        for (int y = 0; y < previewImage.height() && !visibleText; ++y)
        {
            for (int x = 0; x < previewImage.width(); ++x)
            {
                if (previewImage.pixelColor(x, y).rgb() != previewBackground)
                {
                    visibleText = true;
                    break;
                }
            }
        }
        QVERIFY(visibleText);
        model->setValue("fontSize", 21.0);
        QVERIFY(!(_app.settings().value("settings/appearance/fontSize").toDouble() == 21.0));
        search->setText("background");
        QCOMPARE(tree->model()->rowCount(), 1);
        QCOMPARE(dialog->currentPage(), QString("application.terminal"));
        SettingsFormWidget* active = nullptr;
        for (auto* form : dialog->findChildren<SettingsFormWidget*>())
        {
            if (form->isVisible())
            {
                active = form;
            }
        }
        QVERIFY(active);
        QCOMPARE(active->visibleFields(), QStringList {"background"});
        search->setText("no-such-setting-5689");
        QCOMPARE(tree->model()->rowCount(), 0);
        search->clear();
        QCOMPARE(model->value("fontSize").toDouble(), 21.0);
        model->removeValue("fontSize");
        QCOMPARE(preview->font().pointSizeF(), inheritedSize.toDouble());
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            dialog->setFixedSize(1280, 800);
            QVERIFY(dialog->selectPage("application.terminal"));
            QApplication::processEvents();
            QVERIFY(dialog->grab().save(QDir(capture).filePath("ApplicationSettings.png")));
        }
    }

    Q_SLOT void sessionPickerKeepsIdentitySeparate()
    {
        const QString first("11111111-1111-1111-1111-111111111111");
        const QString second("22222222-2222-2222-2222-222222222222");
        SessionPicker picker(
            {
                {"Same readable name",  first},
                {"Same readable name", second}
        },
            &_app.mainWindow());
        picker.show();
        auto* combo = picker.findChild<QComboBox*>("sessionChoice");
        QVERIFY(combo);
        QCOMPARE(combo->currentText(), QString("Same readable name"));
        QCOMPARE(picker.selectedSession(), first);
        combo->setCurrentIndex(1);
        QCOMPARE(picker.selectedSession(), second);
        QVERIFY(combo->itemData(1, Qt::AccessibleTextRole).toString().contains(second));
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QApplication::processEvents();
            QPixmap canvas(1280, 800);
            canvas.fill(picker.palette().color(QPalette::Window));
            QPainter painter(&canvas);
            painter.drawPixmap(QPoint((1280 - picker.width()) / 2, (800 - picker.height()) / 2), picker.grab());
            painter.end();
            QVERIFY(canvas.save(QDir(capture).filePath("SessionPicker.png")));
        }
        picker.reject();
    }

    Q_SLOT void terminalTabTypeAndEndpoint()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("Tabs.termaster")), "Tab fixture", &error),
                 qPrintable(error));
        QVERIFY2(_app.createSession("Tab fixture", &error), qPrintable(error));
        auto&      host         = _app.pluginHost();
        auto&      controller   = static_cast<TerminalController&>(host);
        const auto probe        = std::make_shared<ReconnectDevice::Probe>();
        const auto registration = [probe](const QString& id) {
            PluginHost::TerminalType type;
            type.id             = id;
            type.title          = "Fixture " + id;
            type.plugin         = "TabFixture";
            type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic tab presentation fixture
content:
  type: object
  properties:
    host:
      type: string
      default: localhost
      x-settings-path: transport/host
    port:
      type: integer
      default: 5000
      x-settings-path: transport/port
    portName:
      type: string
      default: COM1
      x-settings-path: transport/portName
    program:
      type: string
      default: ''
      x-settings-path: transport/program
)";
            type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
            return type;
        };
        const auto cleanup = qScopeGuard([&] {
            _app.closeWorkspace();
            for (const auto& id : {"socket", "serial", "console"})
            {
                host.unregisterTerminalType(id);
            }
        });
        for (const auto& id : {"socket", "serial", "console"})
        {
            host.registerTerminalType(registration(id));
        }
        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        QVERIFY2(_app.createTerminal("socket", {}, "Saved <friendly> socket", &error), qPrintable(error));
        const auto socketId = _app.selectedTerminal();
        QCOMPARE(_tabCaption(tabs, 0), "Fixture socket\nlocalhost:5000");
        const auto socketGlyph = _tabType(tabs, 0)->icon().pixmap(16, 16).toImage();
        QVERIFY(!socketGlyph.isNull());
        QVERIFY(tabs->tabBar()->tabButton(0, QTabBar::LeftSide));
        QCOMPARE(_tabHeader(tabs, 0)->objectName(), "terminalTabHeader");
        QVERIFY(_tabStatus(tabs, 0));
        QVERIFY(tabs->tabBar()->tabText(0).isEmpty());
        QVERIFY(QTextDocumentFragment::fromHtml(tabs->tabToolTip(0)).toPlainText().contains("Saved <friendly> socket"));
        QVERIFY(tabs->tabBar()->accessibleTabName(0).contains("localhost:5000"));
        QVERIFY(tabs->tabBar()->accessibleTabName(0).contains("Disconnected"));

        host.showSelectedTerminalSettings();
        auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(dialog);
        auto* model = dialog->pageModel("terminal.connection");
        QVERIFY(model);
        QVERIFY(_app.workspace()->settings().setValue("transport/host", "inherited.example"));
        dialog->refreshInheritedValues();
        model->setValue("port", 5001);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(_tabCaption(tabs, 0), "Fixture socket\ninherited.example:5001");
        QVERIFY(!_app.session()->objects().node(socketId + "/settings").contains("transport/host"));
        model->setValue("host", "2001:db8::1");
        model->setValue("port", 6000);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(_tabCaption(tabs, 0), "Fixture socket\n[2001:db8::1]:6000");
        delete dialog;
        const auto objectBefore = _app.session()->objects().value(socketId);
        QVERIFY(host.unregisterTerminalType("socket", &error));
        host.registerTerminalType(registration("socket"));
        QCOMPARE(_app.selectedTerminal(), socketId);
        QCOMPARE(_tabCaption(tabs, 0), "Fixture socket\n[2001:db8::1]:6000");
        QCOMPARE(_app.session()->objects().value(socketId), objectBefore);
        QCOMPARE(_tabType(tabs, 0)->icon().pixmap(16, 16).toImage(), socketGlyph);
        QVERIFY(tabs->tabBar()->accessibleTabName(0).contains("Saved <friendly> socket"));

        QVERIFY2(_app.createTerminal("serial", {}, "Saved serial", &error), qPrintable(error));
        QCOMPARE(_tabCaption(tabs, 1), "Fixture serial\nCOM1");
        QVERIFY2(_app.createTerminal("console",
                                     {
                                         {"transport",
                                          QVariantMap {{"program", "sh"},
                                                       {"arguments", QStringList {"private-argument"}},
                                                       {"environment", QVariantMap {{"TOKEN", "private-token"}}}}}
        },
                                     "Saved console",
                                     &error),
                 qPrintable(error));
        QCOMPARE(_tabCaption(tabs, 2), "Fixture console\nsh");
        const auto serialGlyph  = _tabType(tabs, 1)->icon().pixmap(16, 16).toImage();
        const auto consoleGlyph = _tabType(tabs, 2)->icon().pixmap(16, 16).toImage();
        QVERIFY(!serialGlyph.isNull());
        QVERIFY(!consoleGlyph.isNull());
        QVERIFY(serialGlyph != socketGlyph);
        QVERIFY(consoleGlyph != socketGlyph);
        QVERIFY(consoleGlyph != serialGlyph);
        QVERIFY(!tabs->tabToolTip(2).contains("private-"));
        QVERIFY(!tabs->tabBar()->accessibleTabName(2).contains("private-"));
        tabs->tabBar()->moveTab(2, 0);
        QCOMPARE(_tabCaption(tabs, tabs->currentIndex()), "Fixture console\nsh");
        QCOMPARE(_tabType(tabs, 0)->icon().pixmap(16, 16).toImage(), consoleGlyph);
        QCOMPARE(_tabType(tabs, 1)->icon().pixmap(16, 16).toImage(), socketGlyph);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        QCoreApplication::processEvents();
        QVERIFY(tabs->tabBar()->tabRect(0).height() >= 2 * tabs->tabBar()->fontMetrics().height());
        QVERIFY(!tabs->tabsClosable());
        for (int index = 0; index < tabs->count(); ++index)
        {
            auto* header = _tabHeader(tabs, index);
            QVERIFY(header);
            QCOMPARE(header->objectName(), "terminalTabHeader");
            QVERIFY(header->layout());
            QVERIFY(!tabs->tabBar()->tabButton(index, QTabBar::RightSide));
            QVERIFY(tabs->tabBar()->tabText(index).isEmpty());
            auto* close  = header->findChild<QToolButton*>("terminalTabClose");
            auto* title  = header->findChild<QLabel*>("terminalTabTitle");
            auto* detail = header->findChild<QLabel*>("terminalTabDetail");
            auto* type   = header->findChild<QToolButton*>("terminalTypeIcon");
            auto* status = _tabStatus(tabs, index);
            QVERIFY(close);
            QVERIFY(title);
            QVERIFY(detail);
            QVERIFY(type);
            QVERIFY(status);
            QVERIFY(close->isVisible());
            QVERIFY(title->isVisible());
            QVERIFY(detail->isVisible());
            QVERIFY(type->isVisible());
            QVERIFY(!type->icon().isNull());
            const int titleCenter = title->mapTo(tabs->tabBar(), title->rect().center()).y();
            QVERIFY(qAbs(close->mapTo(tabs->tabBar(), close->rect().center()).y() - titleCenter) <= 3);
            QVERIFY(qAbs(status->mapTo(tabs->tabBar(), status->rect().center()).y() - titleCenter) <= 3);
            QVERIFY(!status->grab().isNull());
        }
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("TerminalTabs.png")));
        }

        const auto originalScheme  = QApplication::styleHints()->colorScheme();
        const auto originalPalette = QApplication::palette();
        const auto restoreScheme   = qScopeGuard([originalScheme, originalPalette] {
            QApplication::styleHints()->setColorScheme(originalScheme);
            QApplication::setPalette(originalPalette);
        });
        for (const auto scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark})
        {
            QApplication::styleHints()->setColorScheme(scheme);
            auto colors = originalPalette;
            if (scheme == Qt::ColorScheme::Dark)
            {
                colors.setColor(QPalette::Window, QColor("#202020"));
                colors.setColor(QPalette::WindowText, QColor("#eeeeee"));
                colors.setColor(QPalette::Base, QColor("#161616"));
                colors.setColor(QPalette::Button, QColor("#303030"));
                colors.setColor(QPalette::ButtonText, QColor("#eeeeee"));
                colors.setColor(QPalette::Highlight, QColor("#4aa3ff"));
                colors.setColor(QPalette::HighlightedText, QColor("#000000"));
            }
            QApplication::setPalette(colors);
            QCoreApplication::processEvents();
            for (const int selected : {0, 1})
            {
                tabs->setCurrentIndex(selected);
                QCoreApplication::processEvents();
                const auto image  = tabs->tabBar()->grab().toImage();
                const auto ratio  = image.devicePixelRatio();
                const auto stripe = tabs->tabBar()->tabRect(selected).bottomLeft() + QPoint(8, -1);
                QCOMPARE(image.pixelColor(qRound(stripe.x() * ratio), qRound(stripe.y() * ratio)),
                         tabs->tabBar()->palette().color(QPalette::Highlight));
                const auto selectedBackground = tabs->tabBar()->tabRect(selected).bottomLeft() + QPoint(3, -5);
                const auto inactiveBackground = tabs->tabBar()->tabRect(1 - selected).bottomLeft() + QPoint(3, -5);
                QCOMPARE(image.pixelColor(qRound(inactiveBackground.x() * ratio),
                                          qRound(inactiveBackground.y() * ratio)),
                         QApplication::palette().color(QPalette::Window));
                QVERIFY(image.pixelColor(qRound(selectedBackground.x() * ratio), qRound(selectedBackground.y() * ratio))
                        != QApplication::palette().color(QPalette::Window));
                const auto inactiveStripe = tabs->tabBar()->tabRect(1 - selected).bottomLeft() + QPoint(8, -1);
                QVERIFY(image.pixelColor(qRound(inactiveStripe.x() * ratio), qRound(inactiveStripe.y() * ratio))
                        != tabs->tabBar()->palette().color(QPalette::Highlight));
            }
            if (!capture.isEmpty())
            {
                const auto name = scheme == Qt::ColorScheme::Dark ? "TerminalTabsSelectedDark.png" :
                                                                    "TerminalTabsSelectedLight.png";
                QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath(name)));
            }
        }
        QApplication::styleHints()->setColorScheme(originalScheme);
        QApplication::setPalette(originalPalette);
        QCoreApplication::processEvents();
        tabs->setCurrentIndex(1);
        host.showSelectedTerminalSettings();
        dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(dialog);
        model               = dialog->pageModel("terminal.connection");
        const auto longHost = QString(180, 'a') + ".example";
        model->setValue("host", longHost);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        _tabHeadersAreCoherent(tabs, true);
        QTRY_VERIFY(_tabHeadersAreCoherent(tabs));
        const auto label = _tabCaption(tabs, 1);
        QVERIFY(label.startsWith("Fixture socket\n"));
        QVERIFY(label.size() < longHost.size());
        QVERIFY(tabs->tabToolTip(1).contains(longHost));
        QVERIFY(tabs->tabBar()->accessibleTabName(1).contains(longHost));
        delete dialog;
        if (!capture.isEmpty())
        {
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("TerminalTabsLongEndpoint.png")));
        }
        struct HeaderSnapshot
        {
            QPointer<QWidget> panel;
            QWidget*          header = nullptr;
            QToolButton*      status = nullptr;
            QToolButton*      type   = nullptr;
            QToolButton*      close  = nullptr;
            QString           caption;
            QImage            typeImage;
            QImage            statusImage;
            QString           terminalId;
        };
        QList<HeaderSnapshot> snapshots;
        int                   minimumWidth = tabs->tabBar()->tabRect(0).width();
        int                   maximumWidth = minimumWidth;
        for (int index = 0; index < tabs->count(); ++index)
        {
            tabs->setCurrentIndex(index);
            auto* header = _tabHeader(tabs, index);
            auto* type   = header->findChild<QToolButton*>("terminalTypeIcon");
            auto* status = _tabStatus(tabs, index);
            snapshots.append({tabs->widget(index),
                              header,
                              status,
                              type,
                              header->findChild<QToolButton*>("terminalTabClose"),
                              _tabCaption(tabs, index),
                              type->icon().pixmap(16, 16).toImage(),
                              status->icon().pixmap(16, 16).toImage(),
                              _app.selectedTerminal()});
            minimumWidth = std::min(minimumWidth, tabs->tabBar()->tabRect(index).width());
            maximumWidth = std::max(maximumWidth, tabs->tabBar()->tabRect(index).width());
        }
        QVERIFY(minimumWidth < maximumWidth);
        auto*      bar = tabs->tabBar();
        QSignalSpy moved(bar, &QTabBar::tabMoved);
        for (const auto& move : {
                 QPair {0, 2},
                 QPair {1, 0},
                 QPair {2, 1}
        })
        {
            auto*      panel = tabs->widget(move.first);
            const auto id    = panel->property("terminalId").toString();
            auto*      group = controller.terminalGroup(id);
            QVERIFY(group);
            if (!capture.isEmpty())
            {
                QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath(
                    QString("TerminalTabsBeforeReorder-%1-%2.png").arg(move.first).arg(move.second))));
            }
            const int insertionIndex = move.second > move.first ? move.second + 1 : move.second;
            QVERIFY2(controller.moveTerminal(id, group, Qt::Edge(0), insertionIndex, &error), qPrintable(error));
            QTRY_COMPARE(tabs->indexOf(panel), move.second);
            QTest::qWait(200);
            _tabHeadersAreCoherent(tabs, true);
            QTRY_VERIFY(_tabHeadersAreCoherent(tabs));
            if (!capture.isEmpty())
            {
                QVERIFY(_app.mainWindow().grab().save(
                    QDir(capture).filePath(QString("TerminalTabsDropped-%1-%2.png").arg(move.first).arg(move.second))));
            }
            for (const auto& snapshot : snapshots)
            {
                const int index = tabs->indexOf(snapshot.panel);
                QVERIFY(index >= 0);
                QCOMPARE(_tabHeader(tabs, index), snapshot.header);
                QCOMPARE(_tabStatus(tabs, index), snapshot.status);
                QCOMPARE(snapshot.header->findChild<QToolButton*>("terminalTypeIcon"), snapshot.type);
                QCOMPARE(snapshot.header->findChild<QToolButton*>("terminalTabClose"), snapshot.close);
                QCOMPARE(_tabCaption(tabs, index), snapshot.caption);
                QCOMPARE(snapshot.type->icon().pixmap(16, 16).toImage(), snapshot.typeImage);
                QCOMPARE(snapshot.status->icon().pixmap(16, 16).toImage(), snapshot.statusImage);
                tabs->setCurrentIndex(index);
                QCOMPARE(_app.selectedTerminal(), snapshot.terminalId);
            }
        }
        QVERIFY(moved.size() >= 3);
        const auto& closed      = snapshots.front();
        const int   closedIndex = tabs->indexOf(closed.panel);
        QVERIFY(closedIndex >= 0);
        QSignalSpy closeRequested(tabs, &QTabWidget::tabCloseRequested);
        QTest::mouseClick(closed.close, Qt::LeftButton);
        QCOMPARE(closeRequested.size(), 1);
        QCOMPARE(closeRequested.front().front().toInt(), closedIndex);
        QTRY_VERIFY(closed.panel.isNull());
        QCOMPARE(tabs->count(), 2);
        for (int index = 1; index < snapshots.size(); ++index)
        {
            const auto& snapshot = snapshots[index];
            QVERIFY(snapshot.panel);
            const int tab = tabs->indexOf(snapshot.panel);
            QVERIFY(tab >= 0);
            QCOMPARE(_tabHeader(tabs, tab), snapshot.header);
            QCOMPARE(_tabCaption(tabs, tab), snapshot.caption);
        }
        QCOMPARE(probe->attempts.load(), 0);
    }
    Q_SLOT void socketPluginTerminalLifecycle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const auto pluginFile = QDir(_app.plugins().directory())
                                    .filePath(QFileInfo(TERMASTER_SOCKET_PLUGIN_FILE).fileName());
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        QVERIFY(QFile::copy(TERMASTER_SOCKET_PLUGIN_FILE, pluginFile));
        const auto cleanup = qScopeGuard([&] {
            _app.plugins().setEnabled("SocketTerminal", false);
            _app.closeWorkspace();
            QFile::remove(pluginFile);
            _app.plugins().rescan();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        QVERIFY2(_app.plugins().setEnabled("SocketTerminal", true, &error), qPrintable(error));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("Socket.termaster")), "Socket fixture", &error),
                 qPrintable(error));
        QVERIFY2(_app.createSession("Socket fixture", &error), qPrintable(error));
        QVERIFY2(_app.createTerminal("socket",
                                     {
                                         {"transport",
                                          QVariantMap {{"protocol", "TCP"},
                                                       {"host", "127.0.0.1"},
                                                       {"port", int(server.serverPort())}}}
        },
                                     "Socket acceptance",
                                     &error),
                 qPrintable(error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto& host   = _app.pluginHost();
        auto* device = host.selectedDevice("socket");
        QVERIFY(device);
        QVERIFY(device->autoReconnect());
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        host.connectSelectedTerminal();
        QVERIFY(host.connectionRequested("socket"));
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QTcpSocket> peer(server.nextPendingConnection());
        peer->write(QByteArray::fromHex("1b5b324a1b5b48536f636b6574201b5b33326d5443501b5b306d0d0a"));
        QTRY_VERIFY(display->text().contains("Socket TCP"));
        QByteArray outbound;
        connect(peer.get(), &QIODevice::readyRead, this, [&] { outbound += peer->readAll(); });
        QTest::keyClicks(display, "ping");
        QTest::keyClick(display, Qt::Key_Return);
        QTRY_VERIFY(outbound.contains("ping\r"));
        peer->disconnectFromHost();
        QTRY_VERIFY(device->state() != SerialDevice::State::Open);
        QVERIFY(host.connectionRequested("socket"));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
        peer.reset(server.nextPendingConnection());
        QTRY_COMPARE(device->state(), SerialDevice::State::Open);
        host.disconnectSelectedTerminal();
        QVERIFY(!host.connectionRequested("socket"));
        QTRY_COMPARE(device->state(), SerialDevice::State::Closed);
        QTest::qWait(1200);
        QVERIFY(!server.hasPendingConnections());
        host.showSelectedTerminalSettings();
        auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(dialog);
        auto* model = dialog->pageModel("terminal.connection");
        QVERIFY(model && model->isValid());
        QCOMPARE(model->value("protocol").toString(), "TCP");
        model->setValue("protocol", "UDP");
        QVERIFY2(model->apply(&error), qPrintable(error));
        QPointer<SettingsDialog> liveDialog(dialog);
        QVERIFY2(_app.plugins().setEnabled("SocketTerminal", false, &error), qPrintable(error));
        QVERIFY(liveDialog.isNull());
        QVERIFY(_app.terminalIds().isEmpty());
        QVERIFY(!_app.commands().find("Socket.New"));
        // Persisted objects retain their type/settings and reconstruct disconnected.
        QVERIFY2(_app.plugins().setEnabled("SocketTerminal", true, &error), qPrintable(error));
        QCOMPARE(_app.terminalIds().size(), 1);
        device = host.selectedDevice("socket");
        QVERIFY(device);
        QTRY_COMPARE(device->state(), SerialDevice::State::Closed);
        QVERIFY(!host.connectionRequested("socket"));
    }
    Q_SLOT void consistentTerminalControls()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString     error;
        QStringList files;
        const auto  originalSize = _app.mainWindow().size();
        const auto  cleanup      = qScopeGuard([&] {
            for (const auto* id : {"SerialTerminal", "ConsoleTerminal", "SocketTerminal"})
            {
                _app.plugins().setEnabled(id, false);
            }
            _app.closeWorkspace();
            for (const auto& file : files)
            {
                QFile::remove(file);
            }
            _app.plugins().rescan();
            _app.mainWindow().resize(originalSize);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        for (const auto* source :
             {TERMASTER_SOCKET_PLUGIN_FILE, TERMASTER_SERIAL_PLUGIN_FILE, TERMASTER_CONSOLE_PLUGIN_FILE})
        {
            const auto file = QDir(_app.plugins().directory()).filePath(QFileInfo(source).fileName());
            QVERIFY(QFile::copy(source, file));
            files.append(file);
        }
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        // Deliberately load in a different order from the toolbar's semantic ordering.
        for (const auto* id : {"SocketTerminal", "SerialTerminal", "ConsoleTerminal"})
        {
            QVERIFY2(_app.plugins().setEnabled(id, true, &error), qPrintable(error));
        }
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("controls.termaster")), "Controls", &error));
        QVERIFY(_app.createSession("Controls", &error));
        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        _app.mainWindow().resize(1280, 800);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        QStringList terminalIds;
        for (const auto* type : {"serial", "socket", "console"})
        {
            QVariantMap transport;
            if (QString(type) == "serial")
            {
                transport.insert("portName", "COM_TERMASTER_TEST_UNUSED");
            }
            else if (QString(type) == "socket")
            {
                transport.insert("host", "127.0.0.1");
            }
            else
            {
                transport.insert("program", "cmd.exe");
            }
            QVERIFY2(_app.createTerminal(type,
                                         {
                                             {"transport", transport}
            },
                                         QString(type),
                                         &error),
                     qPrintable(error));
            terminalIds.append(_app.selectedTerminal());
            QVERIFY(tabs->currentWidget()->findChild<TerminalDisplay*>());
            auto* display = tabs->currentWidget()->findChild<TerminalDisplay*>();
            display->receive(QByteArray("KEEP ") + type);
            _app.mainWindow().activateWindow();
            QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
            _app.pluginHost().refreshTerminalActions();
            QStringList visible;
            for (const auto* action : _app.toolbar().toolbar()->actions())
            {
                if (action->isVisible() && !action->isSeparator())
                {
                    visible.append(action->text());
                }
            }
            QCOMPARE(visible, QStringList({"New console", "New serial", "New socket"}));
            auto* group = static_cast<TerminalController&>(_app.pluginHost()).terminalGroup(_app.selectedTerminal());
            QVERIFY(group);
            QStringList local;
            for (const auto* action : group->toolbar().actions())
            {
                if (action->isVisible() && !action->isSeparator())
                {
                    local.append(action->text());
                }
            }
            const bool console = QString(type) == "console";
            QCOMPARE(_tabCaption(tabs, tabs->currentIndex()).section('\n', 0, 0),
                     QString(console                   ? "CMD" :
                             QString(type) == "serial" ? "Serial" :
                                                         "TCP"));
            if (QString(type) == "socket")
            {
                auto settings = static_cast<TerminalController&>(_app.pluginHost()).selectedSettings();
                QVERIFY(settings.setValue("transport/protocol", "udp"));
                static_cast<TerminalController&>(_app.pluginHost()).applySettings();
                QCOMPARE(_tabCaption(tabs, tabs->currentIndex()).section('\n', 0, 0), QString("UDP"));
                QVERIFY(settings.setValue("transport/protocol", "tcp"));
                static_cast<TerminalController&>(_app.pluginHost()).applySettings();
            }
            if (console)
            {
                auto settings = static_cast<TerminalController&>(_app.pluginHost()).selectedSettings();
                for (const auto& shell : QStringList {"powershell.exe", "pwsh.exe", "C:/Tools/PowerShell.EXE"})
                {
                    QVERIFY(settings.setValue("transport/program", shell));
                    static_cast<TerminalController&>(_app.pluginHost()).applySettings();
                    QCOMPARE(_tabCaption(tabs, tabs->currentIndex()).section('\n', 0, 0), QString("PowerShell"));
                }
                QVERIFY(settings.setValue("transport/program", "cmd.exe"));
                static_cast<TerminalController&>(_app.pluginHost()).applySettings();
            }
            QCOMPARE(local,
                     QStringList({console ? "Start" : "Connect",
                                  console ? "Stop" : "Disconnect",
                                  console ? "Restart" : "Reconnect",
                                  "Local echo",
                                  "Wrap lines",
                                  "Settings..."}));
            QVERIFY(!visible.contains("Close"));
            QTRY_COMPARE(_app.pluginHost().selectedDevice(type)->state(), SerialDevice::State::Closed);
            const auto capture = qEnvironmentVariable("TERMASTER_CONTROLS_CAPTURE");
            if (!capture.isEmpty())
            {
                QVERIFY(QDir().mkpath(capture));
                QCoreApplication::processEvents();
                const auto originalMinimum = _app.mainWindow().minimumSize();
                const auto originalMaximum = _app.mainWindow().maximumSize();
                const auto restoreSize     = qScopeGuard([&] {
                    _app.mainWindow().setMinimumSize(originalMinimum);
                    _app.mainWindow().setMaximumSize(originalMaximum);
                });
                _app.mainWindow().setFixedSize(1280, 800);
                QCoreApplication::processEvents();
                QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath(QString(type) + ".png")));
                const auto originalScheme  = QApplication::styleHints()->colorScheme();
                const auto originalPalette = QApplication::palette();
                const auto restorePalette  = qScopeGuard([&] {
                    QApplication::styleHints()->setColorScheme(originalScheme);
                    QApplication::setPalette(originalPalette);
                });
                for (const auto scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark})
                {
                    QApplication::styleHints()->setColorScheme(scheme);
                    auto palette = originalPalette;
                    if (scheme == Qt::ColorScheme::Dark)
                    {
                        palette.setColor(QPalette::Window, QColor("#202020"));
                        palette.setColor(QPalette::WindowText, QColor("#eeeeee"));
                        palette.setColor(QPalette::Base, QColor("#161616"));
                        palette.setColor(QPalette::Button, QColor("#303030"));
                        palette.setColor(QPalette::ButtonText, QColor("#eeeeee"));
                        palette.setColor(QPalette::Highlight, QColor("#4aa3ff"));
                        palette.setColor(QPalette::HighlightedText, QColor("#000000"));
                    }
                    QApplication::setPalette(palette);
                    QCoreApplication::processEvents();
                    const auto suffix = scheme == Qt::ColorScheme::Dark ? "Dark.png" : "Light.png";
                    QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath(QString(type) + suffix)));
                }
            }
        }
        const auto menuCapture = qEnvironmentVariable("TERMASTER_CONTROLS_CAPTURE");
        if (!menuCapture.isEmpty())
        {
            auto* terminalMenu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/terminal"))->menu();
            terminalMenu->popup(_app.mainWindow().mapToGlobal(QPoint(250, 60)));
            QTest::qWait(100);
            auto     canvas = _app.mainWindow().grab();
            QPainter painter(&canvas);
            painter.drawPixmap(_app.mainWindow().mapFromGlobal(terminalMenu->pos()), terminalMenu->grab());
            painter.end();
            QVERIFY(canvas.save(QDir(menuCapture).filePath("TerminalMenu.png")));
            terminalMenu->close();
        }
        auto* bar      = tabs->tabBar();
        auto  openMenu = [&](int index) {
            const auto point = bar->tabRect(index).center();
            QMetaObject::invokeMethod(bar, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point));
            return _app.mainWindow().findChild<QMenu*>("terminalTabMenu");
        };
        auto* menu = openMenu(0); // Right-click an inactive serial tab.
        QVERIFY(menu);
        QCOMPARE(_app.selectedTerminal(), terminalIds.at(0));
        QVERIFY(_menuAction(menu, "Serial.Close"));
        QVERIFY(!_menuAction(menu, "Serial.New"));
        QVERIFY(!_menuAction(menu, "Console.Environment.Refresh"));
        QVERIFY(_menuAction(menu, "Terminal.Start")->isEnabled());
        QVERIFY(!_menuAction(menu, "Terminal.Stop")->isEnabled());
        QVERIFY(!_menuAction(menu, "Terminal.Restart")->isEnabled());
        const auto capture = qEnvironmentVariable("TERMASTER_CONTROLS_CAPTURE");
        if (!capture.isEmpty())
        {
            QTest::qWait(100);
            auto     canvas = _app.mainWindow().grab();
            QPainter painter(&canvas);
            painter.drawPixmap(_app.mainWindow().mapFromGlobal(menu->pos()), menu->grab());
            painter.end();
            QVERIFY(canvas.save(QDir(capture).filePath("TabContextMenu.png")));
        }
        // Moving tabs must not redirect an already offered command to another terminal.
        bar->moveTab(0, 2);
        _menuAction(menu, "Serial.Clear")->trigger();
        QVERIFY(tabs->currentWidget()->findChild<TerminalDisplay*>()->text().trimmed().isEmpty());
        QVERIFY(tabs->widget(0)->findChild<TerminalDisplay*>()->text().contains("KEEP socket"));
        menu->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        menu = openMenu(2);
        QVERIFY(menu);
        QPointer<QAction> close = _menuAction(menu, "Serial.Close");
        QVERIFY(close);
        _app.pluginHost().closeSelectedTerminal();
        close->trigger(); // A stale menu cannot close the newly selected terminal.
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(_app.terminalIds().size(), 2);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        menu = openMenu(0);
        QVERIFY(menu);
        QPointer<QAction> stale = _menuAction(menu, "Socket.Close");
        QVERIFY(stale);
        QVERIFY(_app.plugins().setEnabled("SocketTerminal", false, &error));
        QVERIFY(!stale->isEnabled());
        stale->trigger();
        QCOMPARE(tabs->count(), 1);
        QVERIFY(_app.pluginHost().hasSelectedTerminal("console"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto count = _app.mainWindow().findChildren<QMenu*>("terminalTabMenu").size();
        QMetaObject::invokeMethod(bar,
                                  "customContextMenuRequested",
                                  Qt::DirectConnection,
                                  Q_ARG(QPoint, QPoint(bar->width() - 1, bar->height() - 1)));
        QCOMPARE(_app.mainWindow().findChildren<QMenu*>("terminalTabMenu").size(), count);
        menu = openMenu(0);
        QVERIFY(menu);
        auto* closeConsole = _menuAction(menu, "Console.Close");
        QVERIFY(closeConsole && closeConsole->isEnabled());
        closeConsole->trigger();
        QCOMPARE(tabs->count(), 0);
        QVERIFY(_app.terminalIds().isEmpty());
    }

    Q_SLOT void consolePluginTerminalLifecycle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const auto pluginFile = QDir(_app.plugins().directory())
                                    .filePath(QFileInfo(TERMASTER_CONSOLE_PLUGIN_FILE).fileName());
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        QVERIFY(QFile::copy(TERMASTER_CONSOLE_PLUGIN_FILE, pluginFile));
        const auto cleanup = qScopeGuard([&] {
            _app.plugins().setEnabled("ConsoleTerminal", false);
            _app.closeWorkspace();
            QFile::remove(pluginFile);
            _app.plugins().rescan();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        QVERIFY2(_app.plugins().setEnabled("ConsoleTerminal", true, &error), qPrintable(error));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("console.termaster")), "Console test", &error),
                 qPrintable(error));
        QVERIFY2(_app.createSession("Local console", &error), qPrintable(error));
        QVariantMap transport;
#ifdef Q_OS_WIN
        transport.insert("program",
                         qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe");
        transport.insert("arguments",
                         QStringList {"-NoProfile",
                                      "-NonInteractive",
                                      "-Command",
                                      "Write-Output 'TERMASTER_CONSOLE_ACCEPTANCE'; exit 7"});
#else
        transport.insert("program", "/bin/sh");
        transport.insert("arguments", QStringList {"-c", "printf 'TERMASTER_CONSOLE_ACCEPTANCE\\n'; exit 7"});
#endif
        QVERIFY2(_app.createTerminal("console",
                                     {
                                         {"transport", transport}
        },
                                     "Console acceptance",
                                     &error),
                 qPrintable(error));
        const auto menuCapture = qEnvironmentVariable("TERMASTER_TERMINAL_MENU_CAPTURE");
        if (!menuCapture.isEmpty())
        {
            for (const auto* source : {TERMASTER_SERIAL_PLUGIN_FILE, TERMASTER_SOCKET_PLUGIN_FILE})
            {
                QVERIFY(QFile::copy(source, QDir(_app.plugins().directory()).filePath(QFileInfo(source).fileName())));
            }
            QVERIFY(_app.plugins().rescan(&error));
            QVERIFY(_app.plugins().setEnabled("SerialTerminal", true, &error));
            QVERIFY(_app.plugins().setEnabled("SocketTerminal", true, &error));
            _app.mainWindow().hide();
            _app.mainWindow().show();
            _app.mainWindow().activateWindow();
            QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
            _app.pluginHost().refreshTerminalActions();
            auto* menu = static_cast<MenuActionContainer*>(_app.mainMenu().findMenu("/terminal"))->menu();
            menu->popup(QPoint(20, 20));
            QTest::qWait(100);
            QPixmap canvas(1280, 800);
            canvas.fill(_app.mainWindow().palette().color(QPalette::Window));
            QPainter painter(&canvas);
            painter.drawPixmap(20, 20, menu->grab());
            painter.end();
            QVERIFY(QDir().mkpath(menuCapture));
            QVERIFY(canvas.save(QDir(menuCapture).filePath("TerminalMenu.png")));
            _app.mainWindow().resize(1280, 800);
            QVERIFY(_app.mainWindow().grab().save(QDir(menuCapture).filePath("TerminalToolbar.png")));
            menu->close();
            return;
        }
        const auto launchCapture = qEnvironmentVariable("TERMASTER_SHELL_GUI_CAPTURE");
        if (!launchCapture.isEmpty())
        {
            // Native rendering preview only: editing elevation must not launch UAC.
            _app.pluginHost().showSelectedTerminalSettings();
            auto* dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
            QVERIFY(dialog && dialog->selectPage("terminal.connection"));
            auto* model = dialog->pageModel("terminal.connection");
            model->setValue("program", "pwsh.exe");
            model->setValue("arguments", QVariantList {});
            model->setValue("runAsAdministrator", true);
            QVERIFY(model->isValid());
            auto* search = dialog->findChild<QLineEdit*>("settingsSearch");
            QVERIFY(search);
            search->setText("Launch");
            auto*      form  = dialog->findChild<SettingsFormWidget*>();
            const auto forms = dialog->findChildren<SettingsFormWidget*>();
            for (auto* candidate : forms)
            {
                if (candidate->editor("program"))
                {
                    form = candidate;
                }
            }
            QVERIFY(form && form->visibleFields().contains("runAsAdministrator"));
            QVERIFY(!form->visibleFields().contains("environment"));
            dialog->resize(1280, 800);
            QTest::qWait(100);
            auto* combo = dialog->findChild<QComboBox*>("consoleShellCombo");
            QVERIFY(combo && combo->isEditable());
            combo->showPopup();
            QTest::qWait(100);
            auto     image = dialog->grab();
            QPainter painter(&image);
            auto*    popup = combo->view()->window();
            painter.drawPixmap(popup->mapToGlobal(QPoint()) - dialog->mapToGlobal(QPoint()), popup->grab());
            painter.end();
            QVERIFY(QDir().mkpath(launchCapture));
            QVERIFY(image.save(QDir(launchCapture).filePath("ShellSelector.png")));
            combo->hidePopup();
            return;
        }
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* device = _app.pluginHost().selectedDevice("console");
        QVERIFY(device);
        QVERIFY(!device->autoReconnect());
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        _app.pluginHost().connectSelectedTerminal();
        QTRY_VERIFY_WITH_TIMEOUT(display->text().contains("TERMASTER_CONSOLE_ACCEPTANCE"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!_app.pluginHost().connectionRequested("console"), 15000);
        QTRY_COMPARE(device->state(), SerialDevice::State::Closed);
        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        QVERIFY(QTextDocumentFragment::fromHtml(tabs->tabToolTip(tabs->currentIndex()))
                    .toPlainText()
                    .contains("Process exited with code 7"));
        _app.pluginHost().showSelectedTerminalSettings();
        QTRY_VERIFY(_app.mainWindow().findChild<SettingsDialog*>());
        auto* settings = _app.mainWindow().findChild<SettingsDialog*>();
        QVERIFY(settings->pageModel("terminal.connection"));
        auto* shell = settings->findChild<QComboBox*>("consoleShellCombo");
        QVERIFY(shell && shell->isEditable());
        QCOMPARE(shell->currentText(), transport.value("program").toString());
        auto* consoleModel = settings->pageModel("terminal.connection");
        QCOMPARE(consoleModel->value("runAsAdministrator").toBool(), false);
        consoleModel->setValue("runAsAdministrator", true);
        QVERIFY(consoleModel->isValid());
        consoleModel->setValue("runAsAdministrator", false);
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().resize(1280, 800);
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("ConsoleTerminal.png")));
        }
        QVERIFY2(_app.plugins().setEnabled("ConsoleTerminal", false, &error), qPrintable(error));
        QVERIFY(_app.terminalIds().isEmpty());
        QVERIFY(!_app.commands().find("Console.New"));
    }
    Q_SLOT void serialPluginTerminalLifecycle()
    {
        auto* clipboard      = QApplication::clipboard();
        auto  savedClipboard = std::make_unique<QMimeData>();
        if (const auto* source = clipboard->mimeData())
        {
            for (const auto& format : source->formats())
            {
                savedClipboard->setData(format, source->data(format));
            }
        }
        const auto    restoreClipboard = qScopeGuard([&] { clipboard->setMimeData(savedClipboard.release()); });
        const auto    editAction       = [this](const char* id) { return _app.commands().find(id)->actions().first(); };
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString    error;
        const auto pluginFile = QDir(_app.plugins().directory())
                                    .filePath(QFileInfo(TERMASTER_SERIAL_PLUGIN_FILE).fileName());
        QVERIFY(QDir().mkpath(_app.plugins().directory()));
        QVERIFY2(QFile::copy(TERMASTER_SERIAL_PLUGIN_FILE, pluginFile), qPrintable(pluginFile));
        const auto oldFontSize   = _app.settings().value("settings/appearance/fontSize");
        const auto oldWindowSize = _app.mainWindow().size();
        const auto cleanup       = qScopeGuard([&] {
            _app.plugins().setEnabled("SerialTerminal", false);
            _app.closeWorkspace();
            _app.mainWindow().resize(oldWindowSize);
            _app.settings().setValue("settings/appearance/fontSize", oldFontSize);
            QFile::remove(pluginFile);
            _app.plugins().rescan();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY2(_app.plugins().rescan(&error), qPrintable(error));
        QVERIFY2(_app.plugins().setEnabled("SerialTerminal", true, &error), qPrintable(error));
        auto* terminalMenu = _app.mainMenu().findMenu("/terminal");
        QVERIFY(terminalMenu);
        const QMap<QString, QString> groups {
            {       "Serial.New",     "create"},
            {   "Serial.Connect", "connection"},
            {"Serial.Disconnect", "connection"},
            {     "Serial.Close", "management"},
            {     "Serial.Clear",     "output"},
            {  "Serial.Settings",   "settings"}
        };
        QList<QPointer<QAction>> actions;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        {
            auto* command = _app.commands().find(it.key());
            QVERIFY(command);
            auto* action = command->actions().first();
            actions.append(action);
            QVERIFY(!action->icon().isNull());
            QVERIFY(!action->toolTip().isEmpty());
            QCOMPARE(terminalMenu->actions(it.value()).contains(action), it.value() != "connection");
            QCOMPARE(_app.toolbar().toolbar()->actions().contains(action), it.value() == "create");
        }
        QVERIFY(!_app.commands().find("Serial.New")->actions().first()->isEnabled());
        QVERIFY(_app.settings().setValue("settings/appearance/fontSize", 15));
        QVERIFY2(_app.createWorkspace(Path(temporary.filePath("serial.termaster")), "Serial test", &error),
                 qPrintable(error));
        QVERIFY(_app.workspace()->settings().setValue("appearance/fontSize", 17));
        QVERIFY(_app.workspace()->settings().setValue("input/pastePolicy", "immediate"));
        QVERIFY(_app.workspace()->settings().setValue("serial/baudRates", QVariantList {123456, 250000}));
        QVERIFY2(_app.createSession("Disconnected serial", &error), qPrintable(error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        _app.pluginHost().refreshTerminalActions();
        QVERIFY(_app.commands().find("Serial.New")->actions().first()->isEnabled());
        QVERIFY2(_app.createTerminal("serial",
                                     {
                                         {"transport", QVariantMap {{"portName", "COM_TERMASTER_TEST_UNUSED"}}}
        },
                                     "Synthetic serial",
                                     &error),
                 qPrintable(error));
        QCOMPARE(_app.terminalIds().size(), 1);
        const auto id = _app.selectedTerminal();
        QVERIFY(!id.isEmpty());
        auto* device = _app.pluginHost().selectedDevice("serial");
        QVERIFY(device);
        QTRY_COMPARE(device->state(), SerialDevice::State::Closed);
        QPointer<TerminalDisplay> display(_app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay"));
        QVERIFY(display);
        QCOMPARE(display->font().pointSizeF(), 17.0);
        QVERIFY(_app.commands().find("Serial.Connect")->actions().first()->isEnabled());
        QVERIFY(!_app.commands().find("Serial.Disconnect")->actions().first()->isEnabled());
        display->receive("synthetic output");
        QVERIFY(display->text().contains("synthetic output"));
        display->setFocus();
        QTRY_VERIFY(display->hasFocus());
        QTRY_VERIFY(editAction("Edit.SelectAll")->isEnabled());
        QVERIFY(!editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.Cut")->isEnabled());
        QVERIFY(!editAction("Edit.Undo")->isEnabled());
        QVERIFY(!editAction("Edit.Redo")->isEnabled());
        editAction("Edit.SelectAll")->trigger();
        QTRY_VERIFY(editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.Cut")->isEnabled());
        QVERIFY(!editAction("Edit.Undo")->isEnabled());
        QVERIFY(!editAction("Edit.Redo")->isEnabled());
        clipboard->setText("unrelated");
        QTest::keyClick(display, Qt::Key_C, Qt::AltModifier);
        QVERIFY(clipboard->text().contains("synthetic output"));
        clipboard->setText("synthetic paste");
        QTRY_VERIFY(editAction("Edit.Paste")->isEnabled());
        QSignalSpy terminalOutput(display.data(), &TerminalDisplay::outputBytes);
        const auto clipboardBeforeControl = clipboard->text();
        QTest::keyClick(display, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(terminalOutput.size(), 1);
        QCOMPARE(terminalOutput.first().at(0).toByteArray(), QByteArray(1, '\3'));
        QCOMPARE(clipboard->text(), clipboardBeforeControl);
        terminalOutput.clear();
        QTest::keyClick(display, Qt::Key_Tab);
        QCOMPARE(terminalOutput.size(), 1);
        QCOMPARE(terminalOutput.first().at(0).toByteArray(), QByteArray("\t"));
        QVERIFY(display->hasFocus());
        terminalOutput.clear();

        QTest::keyClick(display, Qt::Key_V, Qt::AltModifier);
        QCOMPARE(terminalOutput.size(), 1);
        QCOMPARE(terminalOutput.first().at(0).toByteArray(), QByteArray("synthetic paste"));
        QCOMPARE(terminalOutput.first().at(1).value<TerminalDisplay::OutputKind>(), TerminalDisplay::OutputKind::Paste);
        QTRY_COMPARE(device->state(), SerialDevice::State::Closed);
        clipboard->clear();
        QTRY_VERIFY(!editAction("Edit.Paste")->isEnabled());
        auto* workspaceFile = _app.mainWindow().findChild<QLineEdit*>("workspaceFile");
        QVERIFY(workspaceFile);
        workspaceFile->setFocus();
        workspaceFile->deselect();
        QTRY_VERIFY(!editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.Paste")->isEnabled());
        display->setFocus();
        QTRY_VERIFY(editAction("Edit.Copy")->isEnabled());
        _app.commands().find("Serial.Clear")->actions().first()->trigger();
        QVERIFY(!display->text().contains("synthetic output"));
        QTRY_VERIFY(!editAction("Edit.Copy")->isEnabled());
        QVERIFY(!editAction("Edit.SelectAll")->isEnabled());
        const auto definition = _app.session()->objects().value(id);
        QVERIFY(!definition.toMap().value("settings").toMap().contains("appearance"));
        _app.commands().find("Workspace.Settings")->actions().first()->trigger();
        auto* workspaceDialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(workspaceDialog);
        auto* workspaceModel = workspaceDialog->pageModel("workspace.serial.suggestions");
        QVERIFY(workspaceModel);
        workspaceModel->setValue("baudRates", QVariantList {123456, 123456});
        QVERIFY(!workspaceModel->isValid());
        workspaceModel->setValue("baudRates", QVariantList {123456, 250000, 654321});
        QVERIFY(workspaceModel->isValid());
        QVERIFY(workspaceModel->apply(&error));
        QVERIFY(_app.saveWorkspace(&error));
        QVERIFY(!workspaceDialog->pageModel("terminal.connection"));
        delete workspaceDialog;
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        auto* applicationDialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(applicationDialog->pageModel("application.serial"));
        QVERIFY(!applicationDialog->pageModel("workspace.serial.suggestions"));
        QVERIFY(!applicationDialog->pageModel("terminal.connection"));
        auto* defaultPort = applicationDialog->findChild<QComboBox*>("serialPortCombo");
        QVERIFY(defaultPort && defaultPort->isEditable());
        defaultPort->setEditText("COM_GLOBAL_DRAFT");
        QCOMPARE(_app.settings().value("settings/transport/portName", QString()).toString(), QString());
        delete applicationDialog;
        _app.commands().find("Serial.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> settings(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(settings);
        QVERIFY(!settings->pageModel("workspace.serial"));
        QVERIFY(!settings->pageModel("application.appearance"));
        SettingsFormWidget* workspaceForm  = nullptr;
        SettingsFormWidget* connectionForm = nullptr;
        for (auto* form : settings->findChildren<SettingsFormWidget*>())
        {
            if (form->editor("baudRates"))
            {
                workspaceForm = form;
            }
            if (form->findChild<QComboBox*>("baudRateSelector"))
            {
                connectionForm = form;
            }
        }
        QVERIFY(!workspaceForm);
        QVERIFY(connectionForm);
        auto* port = connectionForm->findChild<QComboBox*>("serialPortCombo");
        QVERIFY(port && port->isEditable());
        QCOMPARE(port->currentText(), QString("COM_TERMASTER_TEST_UNUSED"));
        auto* terminalTree = settings->findChild<QTreeView*>("settingsTree");
        QCOMPARE(terminalTree->model()->rowCount(), 1);
        QCOMPARE(terminalTree->model()->index(0, 0).data().toString(), QString("Terminal"));
        auto* baud = connectionForm->editor("baudRate")->findChild<QComboBox*>("baudRateSelector");
        QVERIFY(baud);
        QVERIFY(baud->isEditable());
        QVERIFY(baud->findData(123456) >= 0);
        QVERIFY(baud->findData(250000) >= 0);
        QCOMPARE(baud->currentText(), "115200");
        baud->setEditText("234567");
        auto* connectionModel = settings->pageModel("terminal.connection");
        QVERIFY(connectionModel->isValid());
        QCOMPARE(connectionModel->value("baudRate").toInt(), 234567);
        QVERIFY(connectionModel->canReset("baudRate"));
        connectionModel->removeValue("baudRate");
        QCOMPARE(baud->currentText(), "115200");

        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        QVERIFY(tabs->tabBar()->tabButton(tabs->currentIndex(), QTabBar::LeftSide));
        QVERIFY(!tabs->tabBar()->tabButton(tabs->currentIndex(), QTabBar::LeftSide)->grab().isNull());
        QCOMPARE(_tabCaption(tabs, tabs->currentIndex()), QString("Serial\nCOM_TERMASTER_TEST_UNUSED"));
        QVERIFY(settings->selectPage("terminal.connection"));
        if (QGuiApplication::platformName() == "windows")
        {
            QTRY_VERIFY(settings->screen()->availableGeometry().contains(settings->frameGeometry()));
            QWidget* lastField = nullptr;
            for (auto* form : settings->findChildren<SettingsFormWidget*>())
            {
                if (auto* editor = form->editor("rts"))
                {
                    lastField = editor;
                    break;
                }
            }
            QVERIFY(lastField);
            auto* scroll = lastField->parentWidget();
            while (scroll && !qobject_cast<QScrollArea*>(scroll))
            {
                scroll = scroll->parentWidget();
            }
            auto* area = qobject_cast<QScrollArea*>(scroll);
            QVERIFY(area);
            area->ensureWidgetVisible(lastField);
            QTRY_VERIFY(
                area->viewport()->rect().contains(lastField->mapTo(area->viewport(), lastField->rect().center())));
            const auto nativeCapture = qEnvironmentVariable("TERMASTER_APP_NATIVE_CAPTURE");
            if (!nativeCapture.isEmpty())
            {
                QVERIFY(QDir().mkpath(nativeCapture));
                settings->raise();
                settings->activateWindow();
                QTRY_COMPARE(QApplication::activeWindow(), settings.data());
                QApplication::processEvents();
                QVERIFY(settings->grab().save(QDir(nativeCapture).filePath("SettingsNativeBottom.png")));
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->minimum());
                QApplication::processEvents();
                QVERIFY(settings->grab().save(QDir(nativeCapture).filePath("SettingsNativeTop.png")));
            }
        }
        QList<QPointer<SettingsFieldEditor>> editors;
        for (auto* editor : settings->findChildren<SettingsFieldEditor*>())
        {
            editors.append(editor);
        }
        QVERIFY(!editors.isEmpty());
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().resize(1280, 800);
            settings->setFixedSize(1280, 800);
            QApplication::processEvents();
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("SerialTerminal.png")));
            QVERIFY(settings->grab().save(QDir(capture).filePath("SerialTerminalSettings.png")));
        }
        const auto sessionFile = _app.session()->filePath();
        QVERIFY2(_app.plugins().setEnabled("SerialTerminal", false, &error), qPrintable(error));
        QVERIFY(display.isNull());
        QVERIFY(settings.isNull());
        for (const auto& editor : editors)
        {
            QVERIFY(editor.isNull());
        }
        QVERIFY(_app.terminalIds().isEmpty());
        QVERIFY(_app.selectedTerminal().isEmpty());
        for (const auto& action : actions)
        {
            QVERIFY(action.isNull());
        }
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        {
            QVERIFY(!_app.commands().find(it.key()));
        }
        SettingsSchema::Field field;
        field.widget = "serialPortSelector";
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, _app.settingsWidgets().create(field, nullptr));
        QCOMPARE(_app.session()->objects().value(id), definition);
        Settings persisted(sessionFile, {"termaster.session", "Termaster session"});
        QVERIFY2(persisted.load(&error), qPrintable(error));
        QCOMPARE(persisted.value("objects/" + id), definition);
        QVERIFY2(_app.plugins().setEnabled("SerialTerminal", true, &error), qPrintable(error));
        QCOMPARE(_app.terminalIds(), QStringList {id});
        QCOMPARE(_app.selectedTerminal(), id);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        device = _app.pluginHost().selectedDevice("serial");
        QVERIFY(device);
        QTRY_COMPARE(device->state(), SerialDevice::State::Closed);
        display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        QCOMPARE(display->font().pointSizeF(), 17.0);
        QCOMPARE(_app.session()->objects().value(id), definition);
    }

    Q_SLOT void bufferedCommandSubmission()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Input.termaster")), "Input fixture", &error));
        QVERIFY(_app.createSession("Input fixture", &error));
        const auto probe                         = std::make_shared<ReconnectDevice::Probe>();
        probe->available                         = true;
        auto&                    host            = _app.pluginHost();
        const auto               originalSize    = _app.mainWindow().size();
        const auto               originalMinimum = _app.mainWindow().minimumSize();
        const auto               cleanup         = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("inputFixture");
            _app.mainWindow().setMinimumSize(originalMinimum);
            _app.mainWindow().resize(originalSize);
        });
        PluginHost::TerminalType type;
        type.id             = "inputFixture";
        type.title          = "Command fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic command input fixture
content:
  type: object
  properties: {}
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        QVariantMap input {
            {      "mode", "buffered"},
            {"terminator",      "nul"},
            { "localEcho",       true}
        };
        QVERIFY(_app.createTerminal("inputFixture",
                                    {
                                        {"input", input}
        },
                                    "Commands",
                                    &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* editor  = _app.mainWindow().findChild<CommandEditor*>("commandEditor");
        auto* draft   = editor->findChild<QPlainTextEdit*>("commandDraft");
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(editor && draft && display);
        QVERIFY(editor->buffered());
        host.connectSelectedTerminal();
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        display->setFocus();
        QTest::keyClicks(QApplication::focusWidget(), "AT");
        QCOMPARE(draft->toPlainText(), "AT");
        QCOMPARE(probe->output(), QByteArray());
        QTest::keyClick(draft, Qt::Key_Return);
        QTRY_COMPARE(probe->output(), QByteArray("AT\0", 3));
        QVERIFY(draft->toPlainText().isEmpty());
        auto* history = editor->findChild<QComboBox*>("commandHistory");
        QCOMPARE(history->count(), 1);
        probe->allowance = 0;
        draft->setPlainText("must not replay");
        QTest::keyClick(draft, Qt::Key_Return);
        QCoreApplication::processEvents();
        host.disconnectSelectedTerminal();
        host.connectSelectedTerminal();
        probe->allowance = 2;
        QTest::qWait(40);
        QCOMPARE(probe->output(), QByteArray("AT\0", 3));
        const auto beforeReply = display->text();
        display->receive("\x1b[6n");
        QTRY_VERIFY(probe->output().mid(3).endsWith('R'));
        QVERIFY(probe->output().mid(3).startsWith("\x1b["));
        QCOMPARE(display->text(), beforeReply);
        QApplication::clipboard()->setText("PASTE");
        display->paste();
        QCOMPARE(draft->toPlainText(), "PASTE");
        const auto sentBeforePaste = probe->output();
        auto*      pastePolicy     = editor->findChild<QComboBox*>("commandPastePolicy");
        pastePolicy->setCurrentIndex(pastePolicy->findData("immediate"));
        QVERIFY(QMetaObject::invokeMethod(pastePolicy, "activated", Q_ARG(int, pastePolicy->currentIndex())));
        display->paste();
        QTRY_COMPARE(probe->output(), sentBeforePaste + QByteArray("PASTE\0", 6));
        QCOMPARE(draft->toPlainText(), "PASTE");
        const auto capture = qEnvironmentVariable("TERMASTER_COMMAND_CAPTURE");
        if (!capture.isEmpty())
        {
            _app.mainWindow().setMinimumSize(1100, 760);
            _app.mainWindow().resize(1100, 760);
            _app.mainWindow().move(QPoint(20, 40));
            display->clear();
            display->receive("Synthetic command fixture: exact byte output and bounded queued send.\r\n");
            draft->setPlainText("AT+GMR");
            draft->setFocus();
            QTest::qWait(100);
            QCOMPARE(_app.mainWindow().size(), QSize(1100, 760));
            QVERIFY(_app.mainWindow().grab().save(capture));
            const QFileInfo captureFile(capture);
            auto&           controller = static_cast<TerminalController&>(host);
            QVERIFY(controller.selectedSettings().setValue("input/favorites",
                                                           QVariantList {
                                                               QVariantMap {{"name", "Firmware version"},
                                                                            {"text", "AT+GMR"},
                                                                            {"terminator", "crlf"},
                                                                            {"customTerminator", ""}}
            }));
            controller.applySettings();
            auto* advancedToggle = editor->findChild<QToolButton*>("commandAdvancedToggle");
            advancedToggle->setChecked(true);
            editor->findChild<QPushButton*>("commandFavoriteLoad")->click();
            QCOMPARE(editor->findChild<QComboBox*>("commandTerminator")->currentData().toString(), QString("crlf"));
            editor->findChild<QPushButton*>("commandPeriodicStart")->click();
            QVERIFY(editor->findChild<QToolButton*>("commandStop")->isHidden());
            QTest::qWait(100);
            QCOMPARE(_app.mainWindow().size(), QSize(1100, 760));
            const auto advancedName = QString(captureFile.completeBaseName()).replace("Buffered", "Advanced") + ".png";
            advancedToggle->setChecked(true);
            QTest::qWait(30);
            auto  toolsCapture = _app.mainWindow().grab();
            auto* tools        = editor->findChild<QWidget*>("commandAdvancedPanel");
            QVERIFY(tools && tools->isVisible());
            {
                QPainter painter(&toolsCapture);
                tools->render(&painter, tools->mapToGlobal(QPoint()) - _app.mainWindow().mapToGlobal(QPoint()));
            }
            QVERIFY(toolsCapture.save(QDir(captureFile.absolutePath()).filePath(advancedName)));
            editor->findChild<QToolButton*>("commandStop")->click();
            advancedToggle->setChecked(false);
            editor->setBuffered(false);
            QTest::qWait(30);
            const auto interactiveName = QString(captureFile.completeBaseName()).replace("Buffered", "Interactive")
                                       + ".png";
            QVERIFY(_app.mainWindow().grab().save(QDir(captureFile.absolutePath()).filePath(interactiveName)));
            _app.commands().find("Application.Settings")->actions().first()->trigger();
            auto* settings = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
            QVERIFY(settings && settings->selectPage("application.terminal"));
            settings->setFixedSize(1100, 760);
            QTest::qWait(100);
            for (auto* area : settings->findChildren<QScrollArea*>())
            {
                if (area->isVisible())
                {
                    area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
                }
            }
            QApplication::processEvents();
            auto* favoritesEditor = settings->findChild<FavoritesEditor*>("commandFavoritesEditor");
            QVERIFY(favoritesEditor);
            favoritesEditor->setValue(controller.selectedSettings().value("input/favorites"));
            QApplication::processEvents();
            const auto settingsName = QString(captureFile.completeBaseName()).replace("Buffered", "Settings") + ".png";
            QVERIFY(settings->grab().save(QDir(captureFile.absolutePath()).filePath(settingsName)));
            const auto favoritesName = QString(captureFile.completeBaseName()).replace("Buffered", "Favorites")
                                     + ".png";
            QVERIFY(settings->grab().save(QDir(captureFile.absolutePath()).filePath(favoritesName)));
            settings->reject();
        }
    }

    Q_SLOT void retainedCommandCancelledWhenTargetChanges()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Retained.termaster")), "Retention fixture", &error));
        QVERIFY(_app.createSession("Retention fixture", &error));
        auto probe                       = std::make_shared<ReconnectDevice::Probe>();
        probe->available                 = true;
        auto&                    host    = _app.pluginHost();
        const auto               cleanup = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("retentionFixture");
        });
        PluginHost::TerminalType type;
        type.id             = "retentionFixture";
        type.title          = "Retention fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic retention fixture
content:
  type: object
  properties:
    target:
      type: string
      default: old-target
      x-settings-path: transport/target
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        QVERIFY(_app.createTerminal("retentionFixture",
                                    {
                                        {"input",
                                         QVariantMap {{"mode", "buffered"},
                                                      {"disconnectedPolicy", "retain"},
                                                      {"terminator", "cr"}}}
        },
                                    "Retention",
                                    &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* editor = _app.mainWindow().findChild<CommandEditor*>("commandEditor");
        QVERIFY(editor);
        auto* draft = editor->findChild<QPlainTextEdit*>("commandDraft");
        draft->setPlainText("offline for old target");
        QTest::keyClick(draft, Qt::Key_Return);
        QVERIFY(draft->toPlainText().isEmpty());
        QCOMPARE(editor->findChild<QComboBox*>("commandHistory")->count(), 1);
        QCOMPARE(probe->output(), QByteArray());
        QVERIFY(editor->findChild<QToolButton*>("commandStop")->isHidden());
        auto& controller = static_cast<TerminalController&>(host);
        QVERIFY(controller.selectedSettings().setValue("transport/target", "new-target"));
        controller.applySettings();
        QTRY_VERIFY(editor->findChild<QToolButton*>("commandStop")->isHidden());
        host.connectSelectedTerminal();
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        QCOMPARE(probe->openConfiguration().value("target").toString(), QString("new-target"));
        QTest::qWait(60);
        QCOMPARE(probe->output(), QByteArray());
        draft->setPlainText("fresh command");
        QTest::keyClick(draft, Qt::Key_Return);
        QTRY_COMPARE(probe->output(), QByteArray("fresh command\r"));
    }

    Q_SLOT void workspaceScopedProcessorFavorites()
    {
        QTemporaryDir temporary;
        QString       error;
        const Path    first(temporary.filePath("FavoriteOne.termaster"));
        const Path    second(temporary.filePath("FavoriteTwo.termaster"));
        QVERIFY2(_app.createWorkspace(first, "One", &error), qPrintable(error));
        _app.commands().find("Workspace.Processing")->actions().first()->trigger();
        auto* dialog = _app.mainWindow().findChild<LineflowEditorDialog*>("lineflowEditor");
        QVERIFY(dialog);
        auto* catalog = dialog->findChild<ProcessorCatalogTree*>("processorCatalog");
        QVERIFY(catalog);
        auto* stub = catalog->findItems("Stub", Qt::MatchExactly | Qt::MatchRecursive).first();
        catalog->expandAll();
        catalog->scrollToItem(stub);
        QTimer::singleShot(0, [] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QTest::mouseClick(menu,
                              Qt::LeftButton,
                              Qt::NoModifier,
                              menu->actionGeometry(menu->actions().first()).center());
        });
        emit catalog->customContextMenuRequested(catalog->visualItemRect(stub).center());
        QCOMPARE(_app.workspace()->state().value("processing/favorites").toStringList(),
                 QStringList({"native.bypass", "native.stub"}));
        Settings  base(QString {});
        Workspace persisted(first, base.node(""));
        QVERIFY2(persisted.load(&error), qPrintable(error));
        QCOMPARE(persisted.state().value("processing/favorites").toStringList(),
                 QStringList({"native.bypass", "native.stub"}));
        QVERIFY(!_app.settings().contains("settings/processing/favorites"));
        QVERIFY2(_app.createWorkspace(second, "Two", &error), qPrintable(error));
        QCOMPARE(_app.workspace()->state().value("processing/favorites").toStringList(),
                 QStringList({"native.bypass"}));
        QVERIFY2(_app.openWorkspace(first, &error), qPrintable(error));
        QCOMPARE(_app.workspace()->state().value("processing/favorites").toStringList(),
                 QStringList({"native.bypass", "native.stub"}));
        QVERIFY(_app.closeWorkspace(&error));
    }

    Q_SLOT void workspaceLineflowEditorIntegration()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Editor.termaster")), "Editor", &error));
        bool       removedFavorites = false;
        const auto cleanup          = qScopeGuard([&] {
            if (removedFavorites)
            {
                _app.settingsWidgets().registerWidget("commandFavorites",
                                                      [](const SettingsSchema::Field&, QWidget* parent) {
                                                          return new FavoritesEditor(parent);
                                                      });
            }
            _app.closeWorkspace();
        });
        auto*      action           = _app.commands().find("Workspace.Processing")->actions().first();
        QVERIFY(action->isEnabled());
        QVERIFY(!action->icon().isNull());
        action->trigger();
        QPointer<LineflowEditorDialog> editor = _app.mainWindow().findChild<LineflowEditorDialog*>("lineflowEditor");
        QVERIFY(editor);
        const auto block = editor->model().createBlock("Receive", &error);
        QVERIFY2(!block.isEmpty(), qPrintable(error));
        const auto name = editor->model().insertNode(block, 0, "native.gate", &error);
        QVERIFY2(!name.isEmpty(), qPrintable(error));
        QVERIFY(editor->model().renameNode(block, name, "Pass through", &error));
        QVERIFY(_app.workspace()->processorDefinitions().values().isEmpty());
        QVERIFY2(editor->applyDraft(&error), qPrintable(error));
        QVERIFY(_app.workspace()->processorDefinitions().contains(block));
        QVERIFY(_app.plugins().processors().type(block));
        QVERIFY(editor->model().renameBlock(block, "Discarded name", &error));
        auto* dockHost    = editor->findChild<QMainWindow*>("lineflowDockHost");
        auto* paletteDock = editor->findChild<QDockWidget*>("lineflowPaletteDock");
        QVERIFY(dockHost && paletteDock);
        dockHost->addDockWidget(Qt::RightDockWidgetArea, paletteDock);
        const auto savedLayout = editor->dockLayout();
        editor->reject();
        QTRY_VERIFY(!editor);
        QCOMPARE(_app.workspace()->processorDefinitions().value(block).toMap().value("name").toString(),
                 QString("Receive"));

        action->trigger();
        editor = _app.mainWindow().findChild<LineflowEditorDialog*>("lineflowEditor");
        QVERIFY(editor);
        QCOMPARE(QByteArray::fromBase64(_app.settings().value("settings/ui/lineflow/docks").toString().toLatin1()),
                 savedLayout);
        dockHost    = editor->findChild<QMainWindow*>("lineflowDockHost");
        paletteDock = editor->findChild<QDockWidget*>("lineflowPaletteDock");
        QCOMPARE(dockHost->dockWidgetArea(paletteDock), Qt::RightDockWidgetArea);
        // Widget factory removal releases consumers while retaining the Lineflow draft.
        _app.pluginHost().unregisterEditor("commandFavorites");
        removedFavorites = true;
        QVERIFY(editor);
        QVERIFY(editor->model().definitions().contains(block));
        action->trigger();
        editor = _app.mainWindow().findChild<LineflowEditorDialog*>("lineflowEditor");
        QVERIFY(editor);
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!editor);
        QVERIFY(!action->isEnabled());
    }

    Q_SLOT void workspaceProcessorPublication()
    {
        QTemporaryDir temporary;
        QString       error;
        const Path    first(temporary.filePath("Catalog.termaster"));
        QVERIFY(_app.createWorkspace(first, "Catalog", &error));
        const auto        cleanup  = qScopeGuard([&] { _app.closeWorkspace(); });
        auto&             registry = _app.plugins().processors();
        const QVariantMap definition {
            {    "id",                     "workspace.pipeline"},
            {  "name",                               "Pipeline"},
            {  "kind",                               "lineflow"},
            {"source", QVariantMap {{"nodes", QVariantList {}}}}
        };
        QVERIFY(_app.workspace()->setProcessorDefinition("workspace.pipeline", definition, &error));
        QVERIFY(registry.type("workspace.pipeline").has_value());
        QCOMPARE(registry.type("workspace.pipeline")->title, QString("Pipeline"));
        const QVariantMap script {
            {    "id",                                       "workspace.script"},
            {  "name",                                                 "Script"},
            {  "kind",                                                 "python"},
            {"source", QVariantMap {{"code", "def process(data): return data"}}},
            {"editor",                     QVariantMap {{"language", "python"}}}
        };
        QVERIFY(_app.workspace()->setProcessorDefinition("workspace.script", script, &error));
        QVERIFY(registry.type("workspace.script"));
        QVERIFY(!registry.type("workspace.script")->available);
        QVERIFY(registry.type("workspace.script")->unavailableReason.contains("python"));
        QVERIFY(_app.saveWorkspace(&error));
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!registry.type("workspace.pipeline"));
        QVERIFY(_app.openWorkspace(first, &error));
        QVERIFY(registry.type("workspace.pipeline"));
        QVERIFY(registry.type("workspace.script"));
        QCOMPARE(_app.workspace()->processorDefinitions().value("workspace.script").toMap(), script);

        auto renamed = definition;
        renamed.insert("name", "Updated pipeline");
        QVERIFY(_app.workspace()->setProcessorDefinition("workspace.pipeline", renamed, &error));
        QCOMPARE(registry.type("workspace.pipeline")->title, QString("Updated pipeline"));

        // Publication preflight rejects collisions before changing the saved draft.
        auto collision = definition;
        collision.insert("id", "native.gate");
        const auto original = _app.workspace()->processorDefinitions().values();
        QVERIFY(!_app.workspace()->setProcessorDefinition("native.gate", collision, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(_app.workspace()->processorDefinitions().values(), original);
        QVERIFY(_app.createSession("Publication state", &error));
        auto*     originalSession = _app.session();
        Settings  emptyBase(QString {});
        Workspace conflicting(Path(temporary.filePath("Conflict.termaster")), emptyBase.node(""));
        QVERIFY(conflicting.create("Conflict", &error));
        QVERIFY(conflicting.setProcessorDefinition("native.gate", collision, &error));
        QVERIFY(conflicting.sync(&error));
        QVERIFY(!_app.openWorkspace(conflicting.filePath(), &error));
        QCOMPARE(_app.workspace()->filePath(), first.absolute());
        QCOMPARE(_app.session(), originalSession);
        QCOMPARE(registry.type("workspace.pipeline")->title, QString("Updated pipeline"));

        // Switching workspaces drops the previous catalog and restores it on reopening.
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Other.termaster")), "Other", &error));
        QVERIFY(!registry.type("workspace.pipeline"));
        QVERIFY(_app.openWorkspace(first, &error));
        QCOMPARE(registry.type("workspace.pipeline")->title, QString("Updated pipeline"));
        QVERIFY(_app.workspace()->removeProcessorDefinition("workspace.pipeline", &error));
        QVERIFY(!registry.type("workspace.pipeline"));
    }

    Q_SLOT void terminalProcessingFaultPreservesConnection_data()
    {
        QTest::addColumn<bool>("receiveFault");
        QTest::newRow("rx") << true;
        QTest::newRow("tx") << false;
    }

    Q_SLOT void terminalProcessingFaultPreservesConnection()
    {
        QFETCH(bool, receiveFault);
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Fault.termaster")), "Fault", &error));
        QVERIFY(_app.createSession("Fault", &error));
        auto& host                       = _app.pluginHost();
        auto& controller                 = static_cast<TerminalController&>(host);
        auto  probe                      = std::make_shared<ReconnectDevice::Probe>();
        probe->available                 = true;
        probe->allowance                 = 1024 * 1024;
        const auto               cleanup = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("faultFixture");
        });
        PluginHost::TerminalType type;
        type.id     = "faultFixture";
        type.title  = "Fault fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Fault fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        const QVariantMap decoder {
            {"type", "native.hex-decoder"}
        };
        const QVariantMap gate {
            {"type", "native.gate"}
        };
        QVERIFY(_app.createTerminal("faultFixture",
                                    {
                                        {"processing",
                                         QVariantMap {{"mode", "simplex"},
                                                      {"rx", receiveFault ? decoder : gate},
                                                      {"tx", receiveFault ? gate : decoder}}}
        },
                                    "Fault",
                                    &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        const auto sendFaultLane = [&](const QByteArray& bytes) {
            if (receiveFault)
            {
                probe->injectReceive(bytes);
            }
            else
            {
                display->outputBytes(bytes, TerminalDisplay::OutputKind::Reply, {});
            }
        };
        // An odd HEX packet fails by default rather than borrowing the next input.
        sendFaultLane("4");
        QTest::qWait(100);
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        QVERIFY(host.connectionRequested("faultFixture"));

        // Valid bytes after failure must neither restart decoding nor bypass it.
        sendFaultLane("4142");
        if (receiveFault)
        {
            display->outputBytes("opposite", TerminalDisplay::OutputKind::Reply, {});
            QTRY_COMPARE(probe->output(), QByteArray("opposite"));
        }
        else
        {
            probe->injectReceive("opposite");
            QTRY_VERIFY(display->text().contains("opposite"));
        }
        QTest::qWait(1200);
        QTRY_COMPARE(probe->attempts.load(), 1);
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        QVERIFY(host.connectionRequested("faultFixture"));
        if (receiveFault)
        {
            QVERIFY(!display->text().contains("AB"));
            QVERIFY(!display->text().contains("4142"));
        }
        else
        {
            QVERIFY(probe->output().isEmpty());
        }

        auto* panel = _app.mainWindow().findChild<TerminalProcessingPanel*>("terminalProcessing");
        QVERIFY(panel);
        auto* mode = panel->findChild<QComboBox*>("processingMode");
        QVERIFY(mode);
        mode->setCurrentIndex(mode->findData("bypass"));
        emit mode->activated(mode->currentIndex());
        QVERIFY2(panel->applyDraft(&error), qPrintable(error));
        sendFaultLane("recovered");
        if (receiveFault)
        {
            QTRY_VERIFY(display->text().contains("recovered"));
        }
        else
        {
            QTRY_COMPARE(probe->output(), QByteArray("recovered"));
        }
        QCOMPARE(controller.selectedSettings().value("processing/mode").toString(), QString("bypass"));
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        QTRY_COMPARE(probe->attempts.load(), 1);
    }

    Q_SLOT void terminalVariantEffectiveConfiguration()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Variants.termaster")), "Variants", &error));
        QVERIFY(_app.createSession("Variants", &error));
        auto&      host       = _app.pluginHost();
        auto&      controller = static_cast<TerminalController&>(host);
        const auto cleanup    = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("variantFixture");
        });
        auto       probe      = std::make_shared<ReconnectDevice::Probe>();
        probe->available      = true;
        PluginHost::TerminalType type;
        type.id             = "variantFixture";
        type.title          = "Variant fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = settingsVariantFixture();
        type.settingsSchema.replace("title: Width,", "title: Width, x-settings-path: transport/packet/width,");
        type.settingsSchema.replace("title: Maximum bytes,",
                                    "title: Maximum bytes, x-settings-path: transport/packet/maxBytes,");
        type.settingsSchema.replace("const: fixed, title: Encoding,",
                                    "const: fixed, title: Encoding, x-settings-path: transport/packet/encoding,");
        type.settingsSchema.replace("title: Options,", "title: Options, x-settings-path: transport/options,");
        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        const QVariantMap transport {
            {"packet", QVariantMap {{"encoding", "varint"}, {"width", 7}, {"maxBytes", 8}, {"future", 99}}}
        };
        QVERIFY2(_app.createTerminal("variantFixture",
                                     {
                                         {"transport", transport}
        },
                                     "Variant",
                                     &error),
                 qPrintable(error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        QTRY_COMPARE(probe->attempts.load(), 1);
        const auto packet = probe->openConfiguration().value("packet").toMap();
        QCOMPARE(packet.value("encoding").toString(), QString("varint"));
        QCOMPARE(packet.value("maxBytes").toInt(), 8);
        QCOMPARE(packet.value("future").toInt(), 99);
        QVERIFY(!packet.contains("width"));
        QVERIFY(!probe->openConfiguration().contains("options"));
        QCOMPARE(controller.selectedSettings().value("transport/packet/width").toInt(), 7);
    }

    Q_SLOT void terminalLocalEchoActionTracksSelection()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Echo.termaster")), "Echo", &error));
        QVERIFY(_app.createSession("Echo", &error));
        auto&                    host       = _app.pluginHost();
        auto&                    controller = static_cast<TerminalController&>(host);
        const auto               cleanup    = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("echoFixture");
        });
        PluginHost::TerminalType type;
        type.id     = "echoFixture";
        type.title  = "Echo fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Echo fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [] {
            return std::make_unique<ReconnectDevice>(std::make_shared<ReconnectDevice::Probe>());
        };
        host.registerTerminalType(std::move(type));
        QVERIFY(_app.createTerminal("echoFixture",
                                    {
                                        {"input", QVariantMap {{"localEcho", false}}}
        },
                                    "First",
                                    &error));
        const auto firstId = _app.selectedTerminal();
        auto*      command = _app.commands().find("Terminal.LocalEcho");
        QVERIFY(command && !command->actions().isEmpty());
        auto* action = command->actions().first();
        QVERIFY(action->isCheckable());
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(action));
        QVERIFY(!action->icon().isNull());
        QVERIFY(!action->toolTip().isEmpty());
        QVERIFY(!action->isChecked());
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        action->trigger();
        QVERIFY(action->isChecked());
        QVERIFY(controller.selectedSettings().value("input/localEcho").toBool());
        _app.mainWindow().statusBar()->clearMessage();
        const auto capture = qEnvironmentVariable("TERMASTER_ECHO_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QApplication::processEvents();
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("LocalEcho.png")));
        }
        QVERIFY(_app.createTerminal("echoFixture",
                                    {
                                        {"input", QVariantMap {{"localEcho", false}}}
        },
                                    "Second",
                                    &error));
        QVERIFY(!action->isChecked());
        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        tabs->setCurrentIndex(0);
        QVERIFY(action->isChecked());
        action->trigger();
        QVERIFY(!controller.selectedSettings().value("input/localEcho").toBool());
        tabs->setCurrentIndex(1);
        QVERIFY(!action->isChecked());
        tabs->setCurrentIndex(0);
        QVERIFY(!action->isChecked());
        const auto persisted = _app.session()->objects().value(firstId).toMap().value("settings").toMap();
        QVERIFY(persisted.value("input").toMap().contains("localEcho"));
        QVERIFY(!persisted.value("input").toMap().value("localEcho").toBool());
    }

    Q_SLOT void terminalProcessingRouting()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Processing.termaster")), "Processing", &error));
        QVERIFY(_app.createSession("Processing", &error));
        auto& registry                   = _app.plugins().processors();
        auto  probe                      = std::make_shared<ReconnectDevice::Probe>();
        probe->available                 = true;
        probe->allowance                 = 1024 * 1024;
        auto&                    host    = _app.pluginHost();
        const auto               cleanup = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("processingFixture");
            QTest::qWait(30);
        });
        PluginHost::TerminalType type;
        type.id     = "processingFixture";
        type.title  = "Processing fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Routing fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        const QVariantMap decoder {
            {         "type",               "native.hex-decoder"},
            {"configuration", QVariantMap {{"accumulate", true}}}
        };
        QVERIFY(
            _app.createTerminal("processingFixture",
                                {
                                    {"processing", QVariantMap {{"mode", "simplex"}, {"rx", decoder}, {"tx", decoder}}},
                                    {     "input",          QVariantMap {{"mode", "buffered"}, {"terminator", "crlf"}}}
        },
                                "Processing",
                                &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        auto* interactionArea = _app.mainWindow().findChild<QWidget*>("terminalInteractionArea");
        QVERIFY(interactionArea);
        QVERIFY(interactionArea->isAncestorOf(_app.mainWindow().findChild<CommandEditor*>("commandEditor")));
        QVERIFY(
            interactionArea->isAncestorOf(_app.mainWindow().findChild<TerminalProcessingPanel*>("terminalProcessing")));
        // Split nibbles in opposite directions must not share decoder state.
        probe->injectReceive("4");
        display->outputBytes("4", TerminalDisplay::OutputKind::Reply, {});
        QTest::qWait(30);
        QVERIFY(probe->output().isEmpty());
        QVERIFY(!display->text().contains('A'));
        probe->injectReceive("1");
        display->outputBytes("2", TerminalDisplay::OutputKind::Reply, {});
        QTRY_VERIFY(display->text().contains('A'));
        QTRY_COMPARE(probe->output(), QByteArray("B"));

        // Rx-only edits preserve the Tx decoder's buffered nibble and running transport.
        auto* processingPanel = _app.mainWindow().findChild<TerminalProcessingPanel*>("terminalProcessing");
        QVERIFY(processingPanel);
        auto* rxSelection = processingPanel->findChild<QComboBox*>("rxProcessor");
        QVERIFY(rxSelection);
        probe->clearOutput();
        display->outputBytes("4", TerminalDisplay::OutputKind::Reply, {});
        QTest::qWait(30);
        QVERIFY(processingPanel->applyDraft(&error));
        rxSelection->setCurrentIndex(rxSelection->findData("native.gate"));
        emit rxSelection->activated(rxSelection->currentIndex());
        QVERIFY2(processingPanel->applyDraft(&error), qPrintable(error));
        display->outputBytes("2", TerminalDisplay::OutputKind::Reply, {});
        QTRY_COMPARE(probe->output(), QByteArray("B"));
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        rxSelection->setCurrentIndex(rxSelection->findData("native.hex-decoder"));
        emit rxSelection->activated(rxSelection->currentIndex());
        QVERIFY2(processingPanel->applyDraft(&error), qPrintable(error));
        QCOMPARE(_app.session()
                     ->objects()
                     .value(static_cast<TerminalController&>(host).selectedTerminal())
                     .toMap()
                     .value("settings")
                     .toMap()
                     .value("processing")
                     .toMap()
                     .value("rx")
                     .toMap()
                     .value("type")
                     .toString(),
                 QString("native.hex-decoder"));

        // A workspace block is selectable through the same controller boundary.
        QVERIFY(
            _app.workspace()->setProcessorDefinition("identity",
                                                     {
                                                         {    "id","identity"                                                                   },
                                                         {  "name",                                        "Identity"},
                                                         {  "kind",                                        "lineflow"},
                                                         {"source",
                                                          QVariantMap {
                                                          {"nodes",
                                                          QVariantList {QVariantMap {{"name", "pass"},
                                                          {"type", "native.gate"}}}}}                                }
        },
                                                     &error));
        auto&             controller = static_cast<TerminalController&>(host);
        const QVariantMap identity {
            {"type", "identity"}
        };
        QVERIFY(controller.selectedSettings().setValue("processing",
                                                       QVariantMap {
                                                           {"mode", "simplex"},
                                                           {  "rx",  identity},
                                                           {  "tx",  identity}
        }));
        controller.applySettings();
        QTest::qWait(30);
        probe->clearOutput();
        // Protocol replies bypass command terminators and display-only echo.
        probe->injectReceive("\033[5n");
        QTRY_COMPARE(probe->output(), QByteArray("\033[0n"));
        QVERIFY(!display->text().contains("[0n"));
        probe->injectReceive("FINAL");
        QTRY_VERIFY(display->text().contains("FINAL"));
        host.disconnectSelectedTerminal();
        host.connectSelectedTerminal();
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        probe->injectReceive("REOPEN");
        QTRY_VERIFY(display->text().contains("REOPEN"));
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);

        // A shared two-lane definition is selected once and drives both device directions.
        QVERIFY2(_app.workspace()
                     ->setProcessorDefinition("duplex.fixture",
                                              {
                                                  {    "id","duplex.fixture"                                                            },
                                                  {  "name",                                        "Duplex fixture"},
                                                  {  "kind",                                              "lineflow"},
                                                  {"source",
                                                   QVariantMap {{"channels", 2},
                                                   {"nodes",
                                                   QVariantList {QVariantMap {{"name", "decode"},
                                                   {"type",
                                                   "native.hex-decoder"},
                                                   {"lane", "rx"}},
                                                   QVariantMap {{"name", "encode"},
                                                   {"type",
                                                   "native.hex-encoder"},
                                                   {"lane", "tx"},
                                                   {"configuration",
                                                   QVariantMap {{"separator",
                                                   ""}}}}}}}                                                        }
        },
                                              &error),
                 qPrintable(error));
        auto* processingMode  = processingPanel->findChild<QComboBox*>("processingMode");
        auto* sharedSelection = processingPanel->findChild<QComboBox*>("duplexProcessor");
        QVERIFY(processingMode && sharedSelection);
        processingMode->setCurrentIndex(processingMode->findData("duplex"));
        emit processingMode->activated(processingMode->currentIndex());
        QVERIFY(sharedSelection->findData("duplex.fixture") >= 0);
        sharedSelection->setCurrentIndex(sharedSelection->findData("duplex.fixture"));
        emit sharedSelection->activated(sharedSelection->currentIndex());
        QVERIFY2(processingPanel->applyDraft(&error), qPrintable(error));
        QTest::qWait(30);
        probe->clearOutput();
        display->clear();
        probe->injectReceive("41");
        display->outputBytes("B", TerminalDisplay::OutputKind::Reply, {});
        QTRY_VERIFY(display->text().contains('A'));
        QTRY_COMPARE(probe->output(), QByteArray("42"));
        QCOMPARE(controller.selectedSettings().value("processing/duplex/type").toString(), QString("duplex.fixture"));
        QCOMPARE(controller.selectedSettings().value("processing/mode").toString(), QString("duplex"));
        QCOMPARE(controller.selectedSettings().value("processing/rx/type").toString(), QString("identity"));
        QCOMPARE(controller.selectedSettings().value("processing/tx/type").toString(), QString("identity"));
        host.disconnectSelectedTerminal();
        host.connectSelectedTerminal();
        QTest::qWait(30);
        probe->clearOutput();
        display->outputBytes("C", TerminalDisplay::OutputKind::Reply, {});
        QTRY_COMPARE(probe->output(), QByteArray("43"));

        // Stop must discard both router output and the full downstream transport queue.
        probe->clearOutput();
        probe->allowance = 0;
        for (int index = 0; index < 8; ++index)
        {
            display->outputBytes(QByteArray(512 * 1024, 'x'), TerminalDisplay::OutputKind::Reply, {});
            QTest::qWait(25);
        }
        display->outputBytes("stale", TerminalDisplay::OutputKind::Reply, {});
        QTest::qWait(40);
        auto* sendingEditor = _app.mainWindow().findChild<CommandEditor*>("commandEditor");
        QVERIFY(sendingEditor);
        auto* stop = sendingEditor->findChild<QToolButton*>("commandStop");
        QVERIFY(stop);
        QVERIFY(stop->isHidden());
        emit sendingEditor->stopRequested();
        probe->allowance = 1024 * 1024;
        QTest::qWait(60);
        QVERIFY(probe->output().isEmpty());

        // With no processing selected, replies cannot be overtaken by editor sends.
        QVERIFY(controller.selectedSettings().setValue("processing", QVariantMap {}));
        controller.applySettings();
        QTest::qWait(30);
        probe->clearOutput();
        auto* editor = _app.mainWindow().findChild<CommandEditor*>("commandEditor");
        QVERIFY(editor);
        auto* draft = editor->findChild<QPlainTextEdit*>("commandDraft");
        QVERIFY(draft);
        // Deliver input to the GUI emulator before immediately enqueueing an editor send.
        display->receive("\033[5n");
        draft->setPlainText("command");
        QTest::keyClick(draft, Qt::Key_Return);
        QTRY_COMPARE(probe->output(), QByteArray("\033[0ncommand\r\n"));
        const QVariantMap framed {
            {         "type","native.pattern-framer"                },
            {"configuration",
             QVariantMap {{"pattern", "<(?<header>[A-Z]):(?<payload>[^>]+)>"},
             {"outputTemplate", "${payload}abcdef${header}END"}}}
        };
        QVERIFY(controller.selectedSettings().setValue("processing",
                                                       QVariantMap {
                                                           {"mode", "simplex"},
                                                           {  "rx",    framed},
                                                           {  "tx",    framed}
        }));
        controller.applySettings();
        QTest::qWait(30);
        display->clear();
        probe->clearOutput();
        probe->injectReceive("noise<H:hello");
        QTest::qWait(30);
        QVERIFY(display->text().isEmpty());
        probe->injectReceive(">tail");
        QTRY_COMPARE(display->text(), QString("helloabcdefHEND"));
        display->outputBytes("noise<S:go", TerminalDisplay::OutputKind::Reply, {});
        QTest::qWait(30);
        QVERIFY(probe->output().isEmpty());
        display->outputBytes(">", TerminalDisplay::OutputKind::Reply, {});
        QTRY_COMPARE(probe->output(), QByteArray("goabcdefSEND"));
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        QVERIFY(host.connectionRequested("processingFixture"));
        QVERIFY(_app.closeWorkspace(&error));
        QTRY_VERIFY(registry.canRemoveOwner("core.processing"));
    }

    Q_SLOT void dormantProcessingEditsPreserveDuplexState()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Dormant.termaster")), "Dormant", &error));
        QVERIFY(_app.createSession("Dormant profiles", &error));
        auto& host                       = _app.pluginHost();
        auto& controller                 = static_cast<TerminalController&>(host);
        auto  probe                      = std::make_shared<ReconnectDevice::Probe>();
        probe->available                 = true;
        const auto               cleanup = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("dormantFixture");
        });
        PluginHost::TerminalType type;
        type.id     = "dormantFixture";
        type.title  = "Dormant fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Dormant fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        const QVariantMap codec {
            {    "id",                 "dormant.codec"                      },
            {  "name",                                       "Dormant codec"},
            {  "kind",                                            "lineflow"},
            {"source",
             QVariantMap {{"channels", 2},
             {"nodes",
             QVariantList {QVariantMap {{"name", "codec"},
             {"type", "native.hex-codec"},
             {"configuration", QVariantMap {{"decodeAccumulate", true}}},
             {"lane", "duplex"}}}}}                                         }
        };
        QVERIFY(_app.workspace()->setProcessorDefinition("dormant.codec", codec, &error));
        QVERIFY(_app.createTerminal("dormantFixture",
                                    {
                                        {"processing",
                                         QVariantMap {{"mode", "duplex"},
                                                      {"duplex", QVariantMap {{"type", "dormant.codec"}}},
                                                      {"rx", QVariantMap {{"type", "unavailable.dormant"}}}}}
        },
                                    "Dormant",
                                    &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        QVERIFY(probe->hasDevice());
        QTRY_COMPARE(probe->backendState.load(), SerialDevice::State::Open);
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        auto* panel   = _app.mainWindow().findChild<TerminalProcessingPanel*>("terminalProcessing");
        QVERIFY(display && panel);
        probe->injectReceive("6");
        QTest::qWait(30);
        QVERIFY(display->text().isEmpty());
        QVERIFY(_app.workspace()->removeProcessorDefinition("dormant.codec", &error));
        QVERIFY(!_app.plugins().processors().type("dormant.codec"));
        // The receive profile is dormant while the shared HEX codec accumulates packets.
        panel->findChild<QToolButton*>("rxInplace")->click();
        auto* editor = panel->findChild<LineflowEditorDialog*>("inplaceLineflowEditor");
        QVERIFY(editor);
        QVERIFY(!editor->model().insertNode("terminal.inplace.rx", 0, "native.gate", &error).isEmpty());
        QVERIFY2(editor->applyDraft(&error), qPrintable(error));
        editor->close();
        QCOMPARE(controller.selectedSettings().value("processing/mode").toString(), QString("duplex"));
        QCOMPARE(controller.selectedSettings().value("processing/rx/type").toString(), QString("@inplace"));
        QCOMPARE(controller.selectedSettings().value("processing/duplex/type").toString(), QString("dormant.codec"));
        probe->injectReceive("6");
        QTRY_COMPARE(display->text(), QString("f"));
        QTRY_COMPARE(probe->backendState.load(), SerialDevice::State::Open);

        QVERIFY(_app.workspace()->setProcessorDefinition("dormant.codec", codec, &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.disconnectSelectedTerminal();
        host.connectSelectedTerminal();
        display->clear();
        probe->injectReceive("6");
        QTest::qWait(30);
        QVERIFY(display->text().isEmpty());
        QVERIFY(controller.selectedSettings().setValue("processing/tx",
                                                       QVariantMap {
                                                           {"type", "another.unavailable.dormant"}
        }));
        controller.applySettings();
        probe->injectReceive("7");
        QTRY_COMPARE(display->text(), QString("g"));
        QTRY_COMPARE(probe->backendState.load(), SerialDevice::State::Open);
        QVERIFY(_app.saveSession(&error));
    }

    Q_SLOT void terminalProcessingWorkerFactoriesQueueGuiLifecycleChanges()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Reentrant.termaster")), "Reentrant", &error));
        QVERIFY(_app.createSession("Reentrant", &error));
        auto& host       = _app.pluginHost();
        auto& controller = static_cast<TerminalController&>(host);
        auto& registry   = _app.plugins().processors();
        auto  probe      = std::make_shared<ReconnectDevice::Probe>();
        probe->available = true;
        std::atomic<bool>        closeDuringFactory {false};
        std::atomic<bool>        replaceDuringFactory {false};
        std::atomic<bool>        factoryOnWorker {false};
        auto                     active      = std::make_shared<std::atomic<bool>>(true);
        auto*                    guiThread   = QThread::currentThread();
        bool                     nestedSaved = false;
        const auto               cleanup     = qScopeGuard([&] {
            active->store(false);
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("reentrantFixture");
            registry.removeOwner("reentrant-owner");
        });
        PluginHost::TerminalType type;
        type.id     = "reentrantFixture";
        type.title  = "Reentrant fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Reentrant fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        ProcessorCatalog::Type processor;
        processor.id    = "reentrant.processor";
        processor.title = "Reentrant processor";
        processor.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Reentrant processor\ncontent:\n  type: object\n  properties: {}\n";
        processor.create = [&, active, guiThread](const auto&, const auto&, QString*) {
            factoryOnWorker.store(QThread::currentThread() != guiThread);
            const bool close   = closeDuringFactory.exchange(false);
            const bool replace = replaceDuringFactory.exchange(false);
            if (close || replace)
            {
                QMetaObject::invokeMethod(
                    &_app,
                    [&, active, close, replace] {
                        if (!active->load())
                        {
                            return;
                        }
                        if (close)
                        {
                            host.closeSelectedTerminal();
                        }
                        if (replace)
                        {
                            nestedSaved = controller.selectedSettings().setValue("processing",
                                                                                 QVariantMap {
                                                                                     {  "mode", "bypass"},
                                                                                     {"marker", "nested"}
                            })
                                       && _app.saveSession(&error);
                            controller.applySettings();
                        }
                    },
                    Qt::QueuedConnection);
            }
            return std::make_unique<ReentrantProcessor>();
        };
        registry.registerType("reentrant-owner", std::move(processor));
        QVERIFY(_app.createTerminal("reentrantFixture",
                                    {
                                        {"processing",
                                         QVariantMap {{"mode", "simplex"},
                                                      {"rx", QVariantMap {{"type", "reentrant.processor"}}}}}
        },
                                    "Reentrant",
                                    &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        QVERIFY(probe->hasDevice());
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        host.disconnectSelectedTerminal();
        closeDuringFactory = true;
        host.connectSelectedTerminal();
        QTRY_VERIFY(_app.terminalIds().isEmpty());
        QVERIFY(factoryOnWorker.load());
        QVERIFY(!probe->hasDevice());
        QTRY_VERIFY(registry.canRemoveOwner("reentrant-owner"));

        QVERIFY(_app.createTerminal("reentrantFixture",
                                    {
                                        {"processing", QVariantMap {{"mode", "simplex"}}}
        },
                                    "Nested",
                                    &error));
        auto* panel = _app.mainWindow().findChild<TerminalProcessingPanel*>("terminalProcessing");
        QVERIFY(panel);
        auto* selection = panel->findChild<QComboBox*>("rxProcessor");
        QVERIFY(selection && selection->findData("reentrant.processor") >= 0);
        replaceDuringFactory = true;
        selection->setCurrentIndex(selection->findData("reentrant.processor"));
        emit selection->activated(selection->currentIndex());
        QTRY_VERIFY2(nestedSaved, qPrintable(error));
        QVERIFY(factoryOnWorker.load());
        QCOMPARE(controller.selectedSettings().value("processing/marker").toString(), QString("nested"));
        QCOMPARE(controller.selectedSettings().value("processing/mode").toString(), QString("bypass"));
        Settings persisted(_app.session()->filePath(), {"termaster.session", "Termaster session"});
        QVERIFY(persisted.load(&error));
        QCOMPARE(persisted.value("objects/" + _app.selectedTerminal() + "/settings/processing/marker").toString(),
                 QString("nested"));
        QCOMPARE(controller.selectedSettings().value("processing/mode").toString(), QString("bypass"));
    }

    Q_SLOT void liveInplaceNodeRemovalPreservesTransportAndRetriesAtomically()
    {
        QTemporaryDir temporary;
        QString       error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Removal.termaster")), "Removal", &error));
        QVERIFY(_app.createSession("Live removal", &error));
        auto& host                       = _app.pluginHost();
        auto& controller                 = static_cast<TerminalController&>(host);
        auto  probe                      = std::make_shared<ReconnectDevice::Probe>();
        probe->available                 = true;
        probe->allowance                 = 1024;
        const auto               cleanup = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("removalFixture");
        });
        PluginHost::TerminalType type;
        type.id     = "removalFixture";
        type.title  = "Removal fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Removal fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(std::move(type));
        const QString     root = "terminal.inplace.rx";
        const QVariantMap definitions {
            {root,
             QVariantMap {{"id", root},
                          {"name", "Inplace"},
                          {"kind", "lineflow"},
                          {"source",
                           QVariantMap {
                               {"nodes",
                                QVariantList {QVariantMap {{"name", "HEX Encoder"}, {"type", "native.hex-encoder"}},
                                              QVariantMap {{"name", "Gate"}, {"type", "native.gate"}}}}}}}}
        };
        const QVariantMap gateOverride {
            {"enabled",       true},
            {"unknown", "retained"}
        };
        const QVariantMap initial {
            {   "mode",          "simplex"                       },
            {     "rx",
             QVariantMap {{"type", "@inplace"},
             {"unknown", "selection metadata"},
             {"overrides",
             QVariantMap {{"HEX Encoder", QVariantMap {{"letterCase", "lower"}, {"separator", ""}}},
             {"Gate", gateOverride}}}}                           },
            {     "tx",     QVariantMap {{"type", "native.gate"}}},
            {"inplace",         QVariantMap {{"rx", definitions}}},
            {"unknown",                     "processing metadata"}
        };
        QVERIFY2(_app.createTerminal("removalFixture",
                                     {
                                         {"processing", initial}
        },
                                     "Live removal",
                                     &error),
                 qPrintable(error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        QVERIFY(probe->hasDevice());
        QTRY_COMPARE(probe->backendState.load(), SerialDevice::State::Open);
        const auto attempts = probe->attempts.load();
        const auto closes   = probe->closes.load();
        auto*      display  = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        auto*      panel    = _app.mainWindow().findChild<TerminalProcessingPanel*>("terminalProcessing");
        QVERIFY(display && panel);
        probe->injectReceive("A");
        QTRY_COMPARE(display->text(), QString("41"));
        panel->findChild<QToolButton*>("rxInplace")->click();
        auto* editor = panel->findChild<LineflowEditorDialog*>("inplaceLineflowEditor");
        QVERIFY(editor);
        QVERIFY(editor->model().removeNode(root, "HEX Encoder", &error));
        QVERIFY(editor->model().renameNode(root, "Gate", "Retained gate", &error));

        // An external writer rejects the whole edit before runtime replacement.
        QFile disk(_app.session()->filePath().toString());
        QVERIFY(disk.open(QIODevice::ReadOnly));
        const auto originalBytes = disk.readAll();
        disk.close();
        const auto externalBytes = originalBytes + "\n# concurrent removal writer\n";
        QVERIFY(disk.open(QIODevice::WriteOnly));
        QCOMPARE(disk.write(externalBytes), qint64(externalBytes.size()));
        disk.close();
        QVERIFY(!editor->applyDraft(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(controller.selectedSettings().value("processing").toMap(), initial);
        QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), externalBytes);
        disk.close();
        display->clear();
        probe->injectReceive("B");
        QTRY_COMPARE(display->text(), QString("42"));
        QTRY_COMPARE(probe->backendState.load(), SerialDevice::State::Open);
        QCOMPARE(probe->attempts.load(), attempts);
        QCOMPARE(probe->closes.load(), closes);

        QVERIFY(disk.open(QIODevice::WriteOnly));
        QCOMPARE(disk.write(originalBytes), qint64(originalBytes.size()));
        disk.close();
        QVERIFY2(editor->applyDraft(&error), qPrintable(error));
        const auto committed = controller.selectedSettings().value("processing").toMap();
        const auto selection = committed.value("rx").toMap();
        QCOMPARE(selection.value("overrides").toMap(),
                 QVariantMap({
                     {"Retained gate", gateOverride}
        }));
        QCOMPARE(selection.value("unknown"), initial.value("rx").toMap().value("unknown"));
        QCOMPARE(committed.value("tx"), initial.value("tx"));
        QCOMPARE(committed.value("unknown"), initial.value("unknown"));
        QCOMPARE(committed.value("inplace").toMap().value("rx").toMap(), editor->model().definitions());
        display->clear();
        probe->injectReceive("C");
        QTRY_COMPARE(display->text(), QString("C"));
        display->outputBytes("still open", TerminalDisplay::OutputKind::Reply, {});
        QTRY_COMPARE(probe->output(), QByteArray("still open"));
        QTRY_COMPARE(probe->backendState.load(), SerialDevice::State::Open);
        QCOMPARE(probe->attempts.load(), attempts);
        QCOMPARE(probe->closes.load(), closes);
        Settings persisted(_app.session()->filePath(), {"termaster.session", "Termaster session"});
        QVERIFY(persisted.load(&error));
        QCOMPARE(persisted.value("objects/" + _app.selectedTerminal() + "/settings/processing").toMap(), committed);
        const auto capture = qEnvironmentVariable("TERMASTER_INPLACE_REMOVAL_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            editor->setFixedSize(1100, 760);
            QApplication::processEvents();
            QVERIFY(editor->grab().save(QDir(capture).filePath("AppliedNodeRemoval.png")));
        }
        editor->close();
    }

    Q_SLOT void terminalInplaceProcessingPersistenceAndAtomicity()
    {
        QTemporaryDir temporary;
        QString       error;
        const Path    workspaceFile(temporary.filePath("Inplace.termaster"));
        QVERIFY(_app.createWorkspace(workspaceFile, "Inplace", &error));
        QVERIFY(_app.createSession("Local diagrams", &error));
        auto&                                          host       = _app.pluginHost();
        auto&                                          controller = static_cast<TerminalController&>(host);
        auto&                                          registry   = _app.plugins().processors();
        QList<std::shared_ptr<ReconnectDevice::Probe>> probes;
        const auto                                     originalMinimum = _app.mainWindow().minimumSize();
        const auto                                     originalMaximum = _app.mainWindow().maximumSize();
        const auto                                     originalSize    = _app.mainWindow().size();
        const auto                                     cleanup         = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType("inplaceFixture");
            _app.mainWindow().setMinimumSize(originalMinimum);
            _app.mainWindow().setMaximumSize(originalMaximum);
            _app.mainWindow().resize(originalSize);
        });
        PluginHost::TerminalType                       type;
        type.id     = "inplaceFixture";
        type.title  = "Inplace fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Inplace fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [&] {
            auto probe       = std::make_shared<ReconnectDevice::Probe>();
            probe->available = true;
            probe->allowance = 1024 * 1024;
            probes.append(probe);
            return std::make_unique<ReconnectDevice>(probe);
        };
        host.registerTerminalType(std::move(type));
        const auto diagram = [](const QString& direction, QVariantList nodes) {
            const auto root = "terminal.inplace." + direction;
            return QVariantMap {
                {root,
                 QVariantMap {{"id", root},
                              {"name", "Inplace"},
                              {"kind", "lineflow"},
                              {"source",
                               QVariantMap {{"channels", direction == "duplex" ? 2 : 1}, {"nodes", nodes}}}}}
            };
        };
        const auto        firstRx  = diagram("rx",
                                             {
                                                 QVariantMap {{"name", "decode"}, {"type", "native.hex-decoder"}}
        });
        const auto        secondRx = diagram("rx",
                                             {
                                                 QVariantMap {{"name", "encode"},
                                                              {"type", "native.hex-encoder"},
                                                              {"configuration", QVariantMap {{"separator", ""}}}}
        });
        const auto        shared   = diagram("duplex", {});
        const QVariantMap initial {
            {   "mode",                                         "simplex"},
            {     "rx",                QVariantMap {{"type", "@inplace"}}},
            {     "tx",             QVariantMap {{"type", "native.gate"}}},
            { "duplex",                QVariantMap {{"type", "@inplace"}}},
            {"inplace", QVariantMap {{"rx", firstRx}, {"duplex", shared}}}
        };
        QVERIFY2(_app.createTerminal("inplaceFixture",
                                     {
                                         {"processing", initial}
        },
                                     "First",
                                     &error),
                 qPrintable(error));
        const auto firstId = _app.selectedTerminal();
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        auto* display = tabs->widget(0)->findChild<TerminalDisplay*>("terminalDisplay");
        auto* panel   = tabs->widget(0)->findChild<TerminalProcessingPanel*>("terminalProcessing");
        QVERIFY(display && panel);
        probes.at(0)->injectReceive("41");
        QTRY_VERIFY(display->text().contains('A'));
        auto* commandEditor = tabs->widget(0)->findChild<CommandEditor*>("commandEditor");
        QVERIFY(commandEditor && commandEditor->isHidden());
        QVERIFY(panel->isHidden());
        auto* processingCommand = _app.commands().find("Terminal.Processing");
        auto* bufferedCommand   = _app.commands().find("Terminal.BufferedInput");
        auto* sendFileCommand   = _app.commands().find("Terminal.SendFile");
        QVERIFY(processingCommand && bufferedCommand && sendFileCommand);
        auto* processingAction = processingCommand->actions().first();
        auto* bufferedAction   = bufferedCommand->actions().first();
        QVERIFY(processingAction->isCheckable() && bufferedAction->isCheckable());
        processingAction->trigger();
        QVERIFY(processingAction->isChecked());
        QVERIFY(!panel->isHidden());
        bufferedAction->trigger();
        QVERIFY(commandEditor->buffered() && !commandEditor->isHidden());
        auto* draft = commandEditor->findChild<QPlainTextEdit*>("commandDraft");
        draft->setPlainText("Preserved command draft");
        const auto capture = qEnvironmentVariable("TERMASTER_INPLACE_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().setFixedSize(1100, 760);
            QApplication::processEvents();
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Buffer.png")));
        }
        bufferedAction->trigger();
        QVERIFY(!commandEditor->buffered() && commandEditor->isHidden());
        QCOMPARE(draft->toPlainText(), QString("Preserved command draft"));
        if (!capture.isEmpty())
        {
            QApplication::processEvents();
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Simplex.png")));
        }
        QVERIFY(!registry.type("terminal.inplace.rx"));
        QVERIFY(_app.workspace()->processorDefinitions().values().isEmpty());

        // Bypass ignores unavailable selections in all inactive directions.
        const QVariantMap unavailable {
            {"type", "missing.processor"}
        };
        QVERIFY2(_app.createTerminal("inplaceFixture",
                                     {
                                         {"processing",
                                          QVariantMap {{"mode", "bypass"},
                                                       {"rx", unavailable},
                                                       {"tx", unavailable},
                                                       {"duplex", unavailable}}}
        },
                                     "Second",
                                     &error),
                 qPrintable(error));
        const auto secondId = _app.selectedTerminal();
        host.connectSelectedTerminal();
        auto* secondDisplay = tabs->widget(1)->findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(secondDisplay);
        probes.at(1)->injectReceive("RAW");
        QTRY_VERIFY(secondDisplay->text().contains("RAW"));
        secondDisplay->outputBytes("raw", TerminalDisplay::OutputKind::Reply, {});
        QTRY_COMPARE(probes.at(1)->output(), QByteArray("raw"));
        auto secondConfiguration = initial;
        auto secondDiagrams      = initial.value("inplace").toMap();
        secondDiagrams.insert("rx", secondRx);
        secondConfiguration.insert("inplace", secondDiagrams);
        QVERIFY(controller.selectedSettings().setValue("processing", secondConfiguration));
        controller.applySettings();
        secondDisplay->clear();
        probes.at(1)->injectReceive("B");
        QTRY_VERIFY(secondDisplay->text().contains("42"));
        QVERIFY(_app.saveSession(&error));
        tabs->setCurrentIndex(0);
        QCOMPARE(_app.selectedTerminal(), firstId);
        QCOMPARE(controller.selectedSettings().value("processing/inplace/rx").toMap(), firstRx);

        panel->findChild<QToolButton*>("rxInplace")->click();
        auto* editor = panel->findChild<LineflowEditorDialog*>("inplaceLineflowEditor");
        QVERIFY(editor);
        QVERIFY(!editor->model().insertNode("terminal.inplace.rx", 1, "native.gate", &error).isEmpty());
        QVERIFY2(editor->applyDraft(&error), qPrintable(error));
        editor->close();
        const auto editedRx = controller.selectedSettings().value("processing/inplace/rx").toMap();
        QVERIFY(editedRx != firstRx);
        auto* mode = panel->findChild<QComboBox*>("processingMode");
        QVERIFY(mode);
        for (const auto& selected : {QString("duplex"), QString("simplex")})
        {
            mode->setCurrentIndex(mode->findData(selected));
            emit mode->activated(mode->currentIndex());
            QCOMPARE(controller.selectedSettings().value("processing/mode").toString(), selected);
            QCOMPARE(controller.selectedSettings().value("processing/inplace/rx").toMap(), editedRx);
            QCOMPARE(controller.selectedSettings().value("processing/inplace/duplex").toMap(), shared);
            QCOMPARE(controller.selectedSettings().value("processing/duplex/type").toString(), QString("@inplace"));
            if (selected == "duplex" && !capture.isEmpty())
            {
                QApplication::processEvents();
                QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("Duplex.png")));
                panel->findChild<QToolButton*>("duplexInplace")->click();
                auto* duplexEditor = panel->findChild<LineflowEditorDialog*>("inplaceLineflowEditor");
                QVERIFY(duplexEditor);
                QCOMPARE(duplexEditor->model().selectedBlock(), QString("terminal.inplace.duplex"));
                duplexEditor->setFixedSize(1100, 760);
                QApplication::processEvents();
                QVERIFY(duplexEditor->grab().save(QDir(capture).filePath("InplaceEditor.png")));
                duplexEditor->close();
                QApplication::processEvents();
            }
        }

        // A concurrent disk writer prevents both persistence and runtime replacement.
        QFile disk(_app.session()->filePath().toString());
        QVERIFY(disk.open(QIODevice::ReadOnly));
        const auto originalBytes = disk.readAll();
        disk.close();
        const auto externalBytes = originalBytes + "\n# concurrent writer\n";
        QVERIFY(disk.open(QIODevice::WriteOnly));
        QCOMPARE(disk.write(externalBytes), qint64(externalBytes.size()));
        disk.close();
        const auto beforeFailure = controller.selectedSettings().value("processing");
        mode->setCurrentIndex(mode->findData("bypass"));
        emit mode->activated(mode->currentIndex());
        QCOMPARE(controller.selectedSettings().value("processing"), beforeFailure);
        QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), externalBytes);
        disk.close();
        display->clear();
        probes.at(0)->injectReceive("44");
        QTRY_VERIFY(display->text().contains('D'));
        QVERIFY(disk.open(QIODevice::WriteOnly));
        QCOMPARE(disk.write(originalBytes), qint64(originalBytes.size()));
        disk.close();
        panel->propertiesWidget()->findChild<QPushButton*>("revertProcessing")->click();
        tabs->setCurrentIndex(1);
        QCOMPARE(_app.selectedTerminal(), secondId);
        QCOMPARE(controller.selectedSettings().value("processing/inplace/rx").toMap(), secondRx);
        QVERIFY(!registry.type("terminal.inplace.rx"));
        QVERIFY(!registry.type("terminal.inplace.duplex"));
        const auto sessionFile = _app.session()->filePath();
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(_app.openWorkspace(workspaceFile, &error));
        QVERIFY(_app.session());
        QCOMPARE(_app.terminalIds().size(), 2);
        Settings persisted(sessionFile, {"termaster.session", "Termaster session"});
        QVERIFY(persisted.load(&error));
        QCOMPARE(persisted.value("objects/" + firstId + "/settings/processing/inplace/rx").toMap(), editedRx);
        QCOMPARE(persisted.value("objects/" + secondId + "/settings/processing/inplace/rx").toMap(), secondRx);
        for (const auto& probe : probes)
        {
            if (probe->hasDevice())
            {
                QTRY_COMPARE(probe->backendState.load(), SerialDevice::State::Open);
            }
        }
        QVERIFY(!registry.type("terminal.inplace.rx"));
    }

    Q_SLOT void connectionShortcutsRespectFocusedTerminalAndRebinding()
    {
        const QVariantMap defaults {
            {   "Terminal.Start",     "Ctrl+Shift+S"},
            {    "Terminal.Stop",     "Ctrl+Shift+X"},
            { "Terminal.Restart",     "Ctrl+Shift+R"},
            {"Terminal.StartAll", "Ctrl+Alt+Shift+S"},
            { "Terminal.StopAll", "Ctrl+Alt+Shift+X"}
        };
        for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
        {
            const auto* command = _app.commands().find(it.key());
            QVERIFY(command);
            QCOMPARE(command->defaultShortcut(), QKeySequence(it.value().toString()));
            const auto group = it.key().endsWith("All") ? "allConnections" : "connection";
            QVERIFY(_app.mainMenu().findMenu("/terminal")->actions(group).contains(command->actions().first()));
            QCOMPARE(command->actions().first()->shortcut(), command->defaultShortcut());
        }
        const QVariantMap editingDefaults {
            {    "Terminal.LocalEcho",  "Ctrl+Shift+E"},
            {"Terminal.BufferedInput",  "Ctrl+Shift+B"},
            {             "Edit.Copy",         "Alt+C"},
            {            "Edit.Paste",         "Alt+V"},
            {              "Edit.Cut",         "Alt+X"},
            {        "Edit.SelectAll",  "Ctrl+Shift+A"},
            {             "Edit.Undo",  "Ctrl+Shift+Z"},
            {             "Edit.Redo",  "Ctrl+Shift+Y"},
            {         "Workspace.New",  "Ctrl+Shift+N"},
            {        "Workspace.Open",  "Ctrl+Shift+O"},
            {        "Workspace.Save", "Ctrl+Shift+F2"},
            {       "Workspace.Close",  "Ctrl+Shift+W"}
        };
        for (auto it = editingDefaults.cbegin(); it != editingDefaults.cend(); ++it)
        {
            QCOMPARE(_app.commands().find(it.key())->defaultShortcut(), QKeySequence(it.value().toString()));
        }
        for (const auto* id : {"Serial.Connect",
                               "Serial.Disconnect",
                               "Serial.Reconnect",
                               "Console.Connect",
                               "Console.Disconnect",
                               "Console.Restart"})
        {
            if (const auto* command = _app.commands().find(id))
            {
                QVERIFY(command->defaultShortcut().isEmpty());
            }
        }

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Shortcuts.termaster")), "Shortcuts", &error));
        QVERIFY(_app.createSession("Shortcuts", &error));
        auto&      host                = _app.pluginHost();
        auto&      controller          = static_cast<TerminalController&>(host);
        const auto originalAssignments = _app.settings().value("settings/shortcuts", QVariantMap {});
        QList<std::shared_ptr<ReconnectDevice::Probe>> probes;
        const auto                                     cleanup = qScopeGuard([&] {
            for (auto* dialog : _app.mainWindow().findChildren<SettingsDialog*>())
            {
                delete dialog;
            }
            _closeTemporaryWorkspace();
            for (const auto* id : {"ShortcutFixture.Connect", "ShortcutFixture.Disconnect", "ShortcutFixture.Restart"})
            {
                host.unregisterAction(id);
            }
            host.unregisterTerminalType("shortcutFixture");
            _app.commands().find("Application.Settings")->actions().first()->trigger();
            auto* restore = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
            if (restore)
            {
                restore->selectPage("application.shortcuts");
                auto* model = restore->pageModel("application.shortcuts");
                if (model)
                {
                    model->setValue("assignments", originalAssignments);
                    restore->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
                }
                delete restore;
            }
        });
        PluginHost::TerminalType                       type;
        type.id     = "shortcutFixture";
        type.title  = "Shortcut fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Shortcut fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [&] {
            auto probe       = std::make_shared<ReconnectDevice::Probe>();
            probe->available = true;
            probe->allowance = 1024;
            probes.append(probe);
            return std::make_unique<ReconnectDevice>(probe);
        };
        host.registerTerminalType(std::move(type));
        using Role = PluginHost::Action::Role;
        for (const auto role : {Role::Connect, Role::Disconnect, Role::Restart})
        {
            PluginHost::Action action;
            action.role         = role;
            action.terminalType = "shortcutFixture";
            action.id           = role == Role::Connect    ? "ShortcutFixture.Connect" :
                                  role == Role::Disconnect ? "ShortcutFixture.Disconnect" :
                                                             "ShortcutFixture.Restart";
            action.title        = action.id;
            action.group        = "connection";
            action.iconName     = "play";
            action.visible      = [&] { return host.hasSelectedTerminal("shortcutFixture"); };
            action.enabled      = [&, role] {
                return host.hasSelectedTerminal("shortcutFixture")
                    && (role == Role::Connect ? !host.connectionRequested("shortcutFixture") :
                                                host.connectionRequested("shortcutFixture"));
            };
            action.trigger = [&, role] {
                if (role != Role::Connect)
                {
                    host.disconnectSelectedTerminal();
                }
                if (role != Role::Disconnect)
                {
                    host.connectSelectedTerminal();
                }
            };
            host.registerAction(std::move(action));
        }
        QStringList             ids;
        QList<TerminalDisplay*> displays;
        for (int index = 0; index < 2; ++index)
        {
            QVERIFY(_app.createTerminal("shortcutFixture", {}, QString::number(index), &error));
            ids.append(_app.selectedTerminal());
            displays.append(controller.terminalGroup(ids.last())
                                ->tabs()
                                .currentWidget()
                                ->findChild<TerminalDisplay*>("terminalDisplay"));
            QVERIFY(displays.last());
        }
        QVERIFY(controller.moveTerminal(ids[0], controller.terminalGroup(ids[1]), Qt::RightEdge, -1, &error));
        auto* display = displays[0];
        _app.mainWindow().activateWindow();
        display->setFocus();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        QTRY_VERIFY(display->hasFocus());
        constexpr auto connectionModifiers = Qt::ControlModifier | Qt::ShiftModifier;
        QSignalSpy     output(display, &TerminalDisplay::outputBytes);
        const auto     toggleInput = [&] {
            for (const auto key : {Qt::Key_B, Qt::Key_E})
            {
                const auto command  = key == Qt::Key_B ? "Terminal.BufferedInput" : "Terminal.LocalEcho";
                auto*      selected = controller.terminalGroup(ids[0])->action(command);
                auto*      other    = controller.terminalGroup(ids[1])->action(command);
                QVERIFY(selected && other);
                const bool initial      = selected->isChecked();
                const bool otherInitial = other->isChecked();
                for (const bool expected : {!initial, initial})
                {
                    QTest::keyClick(QApplication::focusWidget(), key, connectionModifiers);
                    QTRY_COMPARE(selected->isChecked(), expected);
                    QCOMPARE(other->isChecked(), otherInitial);
                }
            }
            QCOMPARE(output.count(), 0);
        };
        toggleInput();
        QTest::keyClick(display, Qt::Key_S, connectionModifiers);
        QTRY_COMPARE(probes[0]->backendState.load(), SerialDevice::State::Open);
        QCOMPARE(probes[1]->attempts.load(), 0);
        display->receive("\x1b[=10u");
        QKeyEvent restartPress(QEvent::KeyPress, Qt::Key_R, connectionModifiers);
        QKeyEvent restartRelease(QEvent::KeyRelease, Qt::Key_R, connectionModifiers);
        QApplication::sendEvent(display, &restartPress);
        QApplication::sendEvent(display, &restartRelease);
        display->receive("\x1b[=0u");
        QTRY_COMPARE(probes[0]->attempts.load(), 2);
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        QCOMPARE(probes[1]->attempts.load(), 0);
        QCOMPARE(output.count(), 0);
        QVERIFY(probes[0]->output().isEmpty());
        for (const auto key : {Qt::Key_S, Qt::Key_C, Qt::Key_X, Qt::Key_R})
        {
            QTest::keyClick(display, key, Qt::ControlModifier);
        }
        QTRY_COMPARE(probes[0]->output(), QByteArray::fromHex("13031812"));
        output.clear();
        QTest::keyClick(display, Qt::Key_X, connectionModifiers);
        QTRY_COMPARE(probes[0]->backendState.load(), SerialDevice::State::Closed);
        QCOMPARE(output.count(), 0);

        QVERIFY(controller.detachTerminal(ids[0], &error));
        auto* secondary = controller.detachedWindow(ids[0]);
        QVERIFY(secondary);
        secondary->activateWindow();
        display->setFocus();
        QTRY_COMPARE(QApplication::activeWindow(), secondary);
        QTRY_VERIFY(display->hasFocus());
        toggleInput();
        QTest::keyClick(display, Qt::Key_S, connectionModifiers);
        QTRY_COMPARE(probes[0]->attempts.load(), 3);
        QTest::keyClick(display, Qt::Key_R, connectionModifiers);
        QTRY_COMPARE(probes[0]->attempts.load(), 4);
        QCOMPARE(probes[1]->attempts.load(), 0);
        QTest::keyClick(display, Qt::Key_X, connectionModifiers);
        QTRY_COMPARE(probes[0]->backendState.load(), SerialDevice::State::Closed);
        QTest::keyClick(display, Qt::Key_S, connectionModifiers | Qt::AltModifier);
        QTRY_COMPARE(probes[0]->backendState.load(), SerialDevice::State::Open);
        QTRY_COMPARE(probes[1]->backendState.load(), SerialDevice::State::Open);
        QTest::keyClick(display, Qt::Key_X, connectionModifiers | Qt::AltModifier);
        QTRY_COMPARE(probes[0]->backendState.load(), SerialDevice::State::Closed);
        QTRY_COMPARE(probes[1]->backendState.load(), SerialDevice::State::Closed);
        QCOMPARE(output.count(), 0);
        QCOMPARE(probes[0]->output(), QByteArray::fromHex("13031812"));
        QVERIFY(probes[1]->output().isEmpty());

        // Persist both a replacement and explicit removal through the real settings owner.
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        QPointer<SettingsDialog> dialog(_app.mainWindow().findChild<SettingsDialog*>("settingsDialog"));
        QVERIFY(dialog);
        dialog->selectPage("application.shortcuts");
        auto* model = dialog->pageModel("application.shortcuts");
        QVERIFY(model);
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            auto* search = dialog->findChild<QLineEdit*>("shortcutSearch");
            search->setText("Ctrl+Shift");
            QTest::qWait(100);
            QVERIFY(dialog->grab().save(QDir(capture).filePath("ConnectionShortcuts.png")));
            search->clear();
        }

        auto assignments = originalAssignments.toMap();
        assignments.insert("Terminal.Start", "Ctrl+Alt+F9");
        model->setValue("assignments", assignments);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(_app.commands().find("Terminal.Start")->shortcut(), QKeySequence("Ctrl+Alt+F9"));
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        secondary->activateWindow();
        display->setFocus();
        QTRY_COMPARE(QApplication::activeWindow(), secondary);
        QTRY_VERIFY(display->hasFocus());
        const auto attempts = probes[0]->attempts.load();
        QTest::keyClick(display, Qt::Key_S, connectionModifiers);
        QCOMPARE(probes[0]->attempts.load(), attempts);
        QTest::keyClick(display, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(probes[0]->attempts.load(), attempts + 1);
        QTest::keyClick(display, Qt::Key_X, connectionModifiers);
        QTRY_COMPARE(probes[0]->backendState.load(), SerialDevice::State::Closed);

        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        _app.commands().find("Application.Settings")->actions().first()->trigger();
        dialog = _app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
        QVERIFY(dialog);
        dialog->selectPage("application.shortcuts");
        model = dialog->pageModel("application.shortcuts");
        assignments.insert("Terminal.Start", "");
        model->setValue("assignments", assignments);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QVERIFY(_app.commands().find("Terminal.Start")->shortcut().isEmpty());
        Settings persisted(_app.settings().filePath(), {"termaster.application", "Termaster application settings"});
        QVERIFY2(persisted.load(&error), qPrintable(error));
        QCOMPARE(persisted.value("settings/shortcuts").toMap(), assignments);
        dialog->reject();
        QTRY_VERIFY(dialog.isNull());
        secondary->activateWindow();
        display->setFocus();
        QTRY_COMPARE(QApplication::activeWindow(), secondary);
        QTRY_VERIFY(display->hasFocus());
        QTest::keyClick(display, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
        QTest::keyClick(display, Qt::Key_S, connectionModifiers);
        QCOMPARE(probes[0]->attempts.load(), attempts + 1);
        QTRY_COMPARE(probes[0]->backendState.load(), SerialDevice::State::Closed);
    }

    Q_SLOT void bulkTerminalConnections()
    {
        auto* start = _app.commands().find("Terminal.StartAll")->actions().first();
        auto* stop  = _app.commands().find("Terminal.StopAll")->actions().first();
        QVERIFY(!start->isEnabled());
        QVERIFY(!stop->isEnabled());
        auto* menu = _app.mainMenu().findMenu("/terminal");
        QCOMPARE(menu->actions("allConnections"), QList<QAction*>({start, stop}));
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(start));
        QVERIFY(!_app.toolbar().toolbar()->actions().contains(stop));

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Bulk.termaster")), "Bulk fixture", &error));
        QVERIFY(_app.createSession("Bulk fixture", &error));
        auto available       = std::make_shared<ReconnectDevice::Probe>();
        auto unavailable     = std::make_shared<ReconnectDevice::Probe>();
        available->available = true;
        auto&      host      = _app.pluginHost();
        const auto cleanup   = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("bulkAvailable");
            host.unregisterTerminalType("bulkUnavailable");
        });
        for (const bool working : {true, false})
        {
            PluginHost::TerminalType type;
            type.id             = working ? "bulkAvailable" : "bulkUnavailable";
            type.title          = type.id;
            type.plugin         = "Synthetic";
            type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic bulk fixture
content:
  type: object
  properties: {}
)";
            type.createDevice   = [probe = working ? available : unavailable] {
                return std::make_unique<ReconnectDevice>(probe);
            };
            host.registerTerminalType(std::move(type));
        }
        QVERIFY(_app.createTerminal("bulkAvailable", {}, "Available", &error));
        QVERIFY(_app.createTerminal("bulkUnavailable", {}, "Unavailable", &error));
        const auto selected = _app.selectedTerminal();
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.refreshTerminalActions();
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        start->trigger();
        QTRY_COMPARE(available->backendState.load(), SerialDevice::State::Open);
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
        QCOMPARE(available->attempts.load(), 1);
        QCOMPARE(unavailable->attempts.load(), 1);
        QCOMPARE(_app.selectedTerminal(), selected);
        QVERIFY(host.connectionRequested("bulkUnavailable"));
        QVERIFY(!start->isEnabled());
        QVERIFY(stop->isEnabled());
        start->trigger();
        QCOMPARE(available->attempts.load(), 1);
        stop->trigger();
        QTRY_COMPARE(available->backendState.load(), SerialDevice::State::Closed);
        QTRY_COMPARE(unavailable->backendState.load(), SerialDevice::State::Closed);
        QVERIFY(!host.connectionRequested("bulkUnavailable"));
        QCOMPARE(_app.selectedTerminal(), selected);
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        QTest::qWait(1200);
        QCOMPARE(unavailable->attempts.load(), 1);
        unavailable->available = true;
        start->trigger();
        QTRY_COMPARE(available->attempts.load(), 2);
        QTRY_COMPARE(unavailable->attempts.load(), 2);
        QTRY_COMPARE(unavailable->backendState.load(), SerialDevice::State::Open);
        stop->trigger();

        // Close is still offered when a backend registers no presentation actions.
        auto*      tabs  = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        const auto point = tabs->tabBar()->tabRect(0).center();
        QMetaObject::invokeMethod(tabs->tabBar(),
                                  "customContextMenuRequested",
                                  Qt::DirectConnection,
                                  Q_ARG(QPoint, point));
        auto* popup = _app.mainWindow().findChild<QMenu*>("terminalTabMenu");
        QVERIFY(popup);
        QVERIFY(!_menuAction(popup, "Terminal.StartAll"));
        auto* close = _menuAction(popup, "Terminal.CloseView");
        QVERIFY(close);
        close->trigger();
        QCOMPARE(tabs->count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!start->isEnabled());
        QVERIFY(!stop->isEnabled());
    }

    Q_SLOT void unavailableConnectionRestoresRetriesUntilExplicitStop()
    {
        QTemporaryDir temporary;
        QString       error;
        const Path    workspacePath(temporary.filePath("RetryRestore.termaster"));
        QVERIFY(_app.createWorkspace(workspacePath, "Retry restore", &error));
        QVERIFY(_app.createSession("Retry restore", &error));
        auto&                                          host = _app.pluginHost();
        QList<std::shared_ptr<ReconnectDevice::Probe>> probes;
        PluginHost::TerminalType                       type;
        type.id     = "retryRestoreFixture";
        type.title  = "Retry restore fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Retry restore fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [&probes] {
            auto probe = std::make_shared<ReconnectDevice::Probe>();
            probes.append(probe);
            return std::make_unique<ReconnectDevice>(probe);
        };
        host.registerTerminalType(type);
        const auto cleanup = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        QVERIFY(_app.createTerminal(type.id, {}, "Unavailable", &error));
        const auto id = _app.selectedTerminal();
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        host.connectSelectedTerminal();
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
        QVERIFY(host.connectionRequested(type.id));
        QVERIFY(_app.closeWorkspace(&error));

        QVERIFY(_app.openWorkspace(workspacePath, &error));
        QCOMPARE(probes.size(), 2);
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        QVERIFY(host.connectionRequested(type.id));
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
        QVERIFY(_app.session()->objects().value(id + "/connected").toBool());
        const int attempts = probes[1]->attempts.load();
        QTRY_VERIFY_WITH_TIMEOUT(probes[1]->attempts.load() > attempts, 3500);
        probes[1]->available = true;
        QTRY_COMPARE_WITH_TIMEOUT(selectedDeviceState(_app), SerialDevice::State::Open, 3500);
        QVERIFY(probes[1]->output().isEmpty());

        host.disconnectSelectedTerminal();
        QVERIFY(!host.connectionRequested(type.id));
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(_app.openWorkspace(workspacePath, &error));
        QCOMPARE(probes.size(), 3);
        QVERIFY(!_app.session()->objects().value(id + "/connected").toBool());
        QVERIFY(!host.connectionRequested(type.id));
        QTest::qWait(1200);
        QCOMPARE(probes[2]->attempts.load(), 0);
    }

    Q_SLOT void terminalConnectionSnapshotRestoresRequestedConnections()
    {
        QTemporaryDir temporary;
        QString       error;
        const Path    workspacePath(temporary.filePath("Connections.termaster"));
        QVERIFY(_app.createWorkspace(workspacePath, "Connections", &error));
        QVERIFY(_app.createSession("Connections", &error));
        auto&                                          host = _app.pluginHost();
        QList<std::shared_ptr<ReconnectDevice::Probe>> probes;
        PluginHost::TerminalType                       type;
        type.id     = "connectionSnapshotFixture";
        type.title  = "Snapshot fixture";
        type.plugin = "Synthetic";
        type.settingsSchema
            = "version: 1\ntype: termaster.settings-schema\ndescription: Snapshot fixture\ncontent:\n  type: object\n  properties: {}\n";
        type.createDevice = [&probes] {
            auto probe       = std::make_shared<ReconnectDevice::Probe>();
            probe->available = true;
            probes.append(probe);
            return std::make_unique<ReconnectDevice>(probe);
        };
        host.registerTerminalType(type);
        const auto  cleanup = qScopeGuard([&] {
            _closeTemporaryWorkspace();
            host.unregisterTerminalType(type.id);
        });
        QStringList ids;
        for (int index = 0; index < 4; ++index)
        {
            QVERIFY(_app.createTerminal(type.id, {}, QString::number(index), &error));
            ids.append(_app.selectedTerminal());
            _app.mainWindow().activateWindow();
            QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
            if (index != 1)
            {
                host.connectSelectedTerminal();
                QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
            }
            if (index == 2)
            {
                probes[index]->drop("Synthetic failure");
                QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
            }
        }
        QVERIFY(_app.saveSession(&error));
        // A deliberately closed view must not retain an earlier connected snapshot.
        host.closeSelectedTerminal();
        const auto file = _app.session()->filePath();
        QVERIFY(_app.closeWorkspace(&error));
        Settings persisted(file, {"termaster.session", "Termaster session"});
        QVERIFY2(persisted.load(&error), qPrintable(error));
        for (int index = 0; index < ids.size(); ++index)
        {
            const auto value = persisted.value("objects/" + ids[index] + "/connected");
            QCOMPARE(value.typeId(), QMetaType::Bool);
            QCOMPARE(value.toBool(), index == 0 || index == 2);
        }
        QVERIFY(_app.openWorkspace(workspacePath, &error));
        QCOMPARE(probes.size(), 8);
        // Both the open terminal and the terminal awaiting retry retain connection intent.
        QTRY_VERIFY(_app.saveSession(&error) && _app.session()->objects().value(ids[0] + "/connected").toBool());
        QTRY_COMPARE(probes[4]->backendState.load(), SerialDevice::State::Open);
        QTRY_COMPARE(probes[6]->backendState.load(), SerialDevice::State::Open);
        int openCount = 0;
        int attempts  = 0;
        for (int index = 4; index < probes.size(); ++index)
        {
            attempts  += probes[index]->attempts.load();
            openCount += probes[index]->backendState.load() == SerialDevice::State::Open ? 1 : 0;
            QVERIFY(probes[index]->output().isEmpty());
        }
        QCOMPARE(attempts, 2);
        QCOMPARE(openCount, 2);

        // Missing providers retain snapshots; enabling the provider applies them once.
        QVERIFY(host.unregisterTerminalType(type.id, &error));
        QVERIFY(_app.saveSession(&error));
        host.registerTerminalType(type);
        QCOMPARE(probes.size(), 12);
        QTRY_VERIFY(_app.saveSession(&error) && _app.session()->objects().value(ids[0] + "/connected").toBool());
        QTRY_COMPARE(probes[8]->backendState.load(), SerialDevice::State::Open);
        QTRY_COMPARE(probes[10]->backendState.load(), SerialDevice::State::Open);
        attempts = 0;
        for (int index = 8; index < probes.size(); ++index)
        {
            attempts += probes[index]->attempts.load();
        }
        QCOMPARE(attempts, 2);
    }

    Q_SLOT void desiredConnectionSurvivesDeviceLoss()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY(_app.createWorkspace(Path(temporary.filePath("Retry.termaster")), "Retry fixture", &error));
        QVERIFY(_app.createSession("Retry fixture", &error));
        auto                     probe = std::make_shared<ReconnectDevice::Probe>();
        auto&                    host  = _app.pluginHost();
        PluginHost::TerminalType type;
        type.id             = "retryFixture";
        type.title          = "Retry fixture";
        type.plugin         = "Synthetic";
        type.settingsSchema = R"(version: 1
type: termaster.settings-schema
description: Synthetic retry fixture
content:
  type: object
  properties:
    portName:
      type: string
      default: synthetic
      x-settings-path: transport/portName
)";
        type.createDevice   = [probe] { return std::make_unique<ReconnectDevice>(probe); };
        host.registerTerminalType(type);
        const auto cleanup = qScopeGuard([&] {
            _app.closeWorkspace();
            host.unregisterTerminalType("retryFixture");
        });
        QVERIFY(_app.createTerminal("retryFixture", {}, "Retry terminal", &error));
        _app.mainWindow().activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &_app.mainWindow());
        auto* tabs = &static_cast<MainWindow&>(_app.mainWindow()).terminalTabs();
        QVERIFY(tabs);
        const auto tooltip = [tabs] {
            return QTextDocumentFragment::fromHtml(tabs->tabToolTip(tabs->currentIndex()))
                .toPlainText()
                .replace(QChar(0x00a0), ' ');
        };
        host.connectSelectedTerminal();
        QVERIFY(host.connectionRequested("retryFixture"));
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
        QVERIFY(tooltip().contains("Desired: Connected"));
        QVERIFY(tooltip().contains("Synthetic port unavailable"));
        const auto hoverPoint = tabs->tabBar()->tabRect(tabs->currentIndex()).center();
        QTest::mouseMove(tabs->tabBar(), hoverPoint);
        QApplication::processEvents();
        QHelpEvent hover(QEvent::ToolTip, hoverPoint, tabs->tabBar()->mapToGlobal(hoverPoint));
        QCoreApplication::sendEvent(tabs->tabBar(), &hover);
        QTRY_VERIFY(QToolTip::isVisible());
        QCOMPARE(QToolTip::text(), tabs->tabToolTip(tabs->currentIndex()));
        const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            _app.mainWindow().resize(1280, 800);
            QApplication::processEvents();
            auto     image = _app.mainWindow().grab();
            QPainter painter(&image);
            for (auto* widget : QApplication::topLevelWidgets())
            {
                if (widget->isVisible() && widget->windowType() == Qt::ToolTip)
                {
                    const auto point = widget->mapToGlobal(QPoint()) - _app.mainWindow().mapToGlobal(QPoint());
                    painter.drawPixmap(point, widget->grab());
                }
            }
            painter.end();
            QVERIFY(image.save(QDir(capture).filePath("ConnectionRetry.png")));
        }
        QToolTip::hideText();
        const auto failedIcon = _tabStatus(tabs, tabs->currentIndex())->icon().pixmap(16, 16).toImage();
        probe->available      = true;
        QTRY_COMPARE_WITH_TIMEOUT(selectedDeviceState(_app), SerialDevice::State::Open, 3500);
        QVERIFY(probe->attempts.load() >= 2);
        QVERIFY(tooltip().contains("Actual: Connected"));
        QVERIFY(!tooltip().contains("Synthetic port unavailable"));
        QVERIFY(_tabStatus(tabs, tabs->currentIndex())->icon().pixmap(16, 16).toImage() != failedIcon);
        auto* display = _app.mainWindow().findChild<TerminalDisplay*>("terminalDisplay");
        QVERIFY(display);
        display->receive("\033]2;Remote title\007");
        QVERIFY(tooltip().contains("Desired: Connected"));
        QVERIFY(tooltip().contains("Remote title"));

        probe->available = false;
        probe->drop("Removed <synthetic> port");
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
        QVERIFY(host.connectionRequested("retryFixture"));
        QVERIFY(tabs->tabToolTip(tabs->currentIndex()).contains("&lt;synthetic&gt;"));
        const int beforeRetry = probe->attempts.load();
        QTRY_VERIFY_WITH_TIMEOUT(probe->attempts.load() > beforeRetry, 3500);
        host.disconnectSelectedTerminal();
        QVERIFY(!host.connectionRequested("retryFixture"));
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Closed);
        const int stoppedAttempts = probe->attempts.load();
        probe->available          = true;
        QTest::qWait(1200);
        QCOMPARE(probe->attempts.load(), stoppedAttempts);
        host.connectSelectedTerminal();
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Open);
        host.connectSelectedTerminal();
        QCOMPARE(probe->attempts.load(), stoppedAttempts + 1);

        probe->available = false;
        probe->drop("Removed synthetic port again");
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
        const int unloadAttempts = probe->attempts.load();
        QVERIFY(host.unregisterTerminalType("retryFixture", &error));
        QVERIFY(!probe->hasDevice());
        QTest::qWait(1200);
        QCOMPARE(probe->attempts.load(), unloadAttempts);
        host.registerTerminalType(type);
        QVERIFY(probe->hasDevice());
        QTRY_COMPARE(selectedDeviceState(_app), SerialDevice::State::Failed);
        QTRY_VERIFY(probe->attempts.load() > unloadAttempts);
        QVERIFY(host.connectionRequested("retryFixture"));
        host.connectSelectedTerminal();
        QVERIFY(host.connectionRequested("retryFixture"));
        const int closedAttempts = probe->attempts.load();
        QVERIFY(_app.closeWorkspace(&error));
        QVERIFY(!probe->hasDevice());
        QTest::qWait(1200);
        QCOMPARE(probe->attempts.load(), closedAttempts);
    }

    Q_SLOT void pluginWindowLifecycle()
    {
        auto* action = _app.commands().find("Application.Plugins")->actions().first();
        action->trigger();
        auto dialogs = _app.mainWindow().findChildren<QDialog*>();
        QCOMPARE(dialogs.size(), 1);
        QPointer<QDialog> dialog(dialogs.first());
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(dialog->findChild<QTableView*>("pluginTable"));
        QVERIFY(!dialog->windowIcon().isNull());
        action->trigger();
        QCOMPARE(_app.mainWindow().findChildren<QDialog*>().size(), 1);

        const QString capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(_app.mainWindow().grab().save(QDir(capture).filePath("MainWindow.png")));
            QVERIFY(QApplication::windowIcon().pixmap(64, 64).save(QDir(capture).filePath("ApplicationIcon.png")));
            QVERIFY(dialog->grab().save(QDir(capture).filePath("PluginWindow.png")));
        }
        dialog->close();
        QTRY_VERIFY(dialog.isNull());
        action->trigger();
        dialogs = _app.mainWindow().findChildren<QDialog*>();
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(dialogs.first()->isVisible());
    }
};

int main(int argc, char* argv[])
{
    const auto closeData = qEnvironmentVariable("TERMASTER_APP_CLOSE_CHILD");
    if (!closeData.isEmpty())
    {
        const auto scenario = qEnvironmentVariable("TERMASTER_APP_CLOSE_SCENARIO");
        const auto marker   = [&](const QByteArray& value) {
            QFile file(QDir(closeData).filePath("close-markers.txt"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Append))
            {
                file.write(value + '\n');
            }
        };
        App::Options options;
        options.dataDirectory    = closeData + "/data";
        options.pluginsDirectory = closeData + "/plugins";
        if (scenario.startsWith("console"))
        {
            QDir().mkpath(options.pluginsDirectory);
            if (!QFile::copy(TERMASTER_CONSOLE_PLUGIN_FILE,
                             QDir(options.pluginsDirectory)
                                 .filePath(QFileInfo(TERMASTER_CONSOLE_PLUGIN_FILE).fileName())))
            {
                marker("consoleCopyFailed");
                return 87;
            }
        }
        int result = 1;
        {
            App                      app(argc, argv, options);
            std::unique_ptr<QDialog> auxiliary;
            QTimer::singleShot(4000, &app, [&] {
                marker("eventLoopWatchdog");
                std::_Exit(86);
            });
            QTimer::singleShot(0, &app, [&] {
                marker("started");
                QString error;
                if (scenario != "empty")
                {
                    if (!app.createWorkspace(Path(QDir(closeData).filePath("Close.termaster")), "Close fixture", &error)
                        || !app.createSession("Close fixture", &error))
                    {
                        marker("setupFailed: " + error.toUtf8());
                        std::_Exit(87);
                    }
                    QString     typeId = "console";
                    QVariantMap settings;
                    if (scenario.startsWith("console"))
                    {
#ifdef Q_OS_WIN
                        settings.insert("transport",
                                        QVariantMap {
                                            {  "program", qEnvironmentVariable("SystemRoot") + "/System32/cmd.exe"},
                                            {"arguments",                                 QStringList {"/D", "/Q"}}
                        });
#else
                        settings.insert("transport", QVariantMap {{"program", "/bin/sh"}});
#endif
                    }
                    else
                    {
                        auto probe       = std::make_shared<ReconnectDevice::Probe>();
                        probe->available = true;
                        PluginHost::TerminalType type;
                        type.id     = "closeFixture";
                        typeId      = type.id;
                        type.title  = "Close fixture";
                        type.plugin = "Synthetic";
                        type.settingsSchema
                            = "version: 1\ntype: termaster.settings-schema\ndescription: Close fixture\ncontent:\n  type: object\n  properties: {}\n";
                        type.createDevice = [probe] { return std::make_unique<ReconnectDevice>(probe); };
                        app.pluginHost().registerTerminalType(std::move(type));
                    }
                    if (scenario == "nestedProcessing")
                    {
                        const auto        root = TerminalProcessing::rootId("rx");
                        const QVariantMap gate {
                            {"name",        "Gate"},
                            {"type", "native.gate"}
                        };
                        const QVariantMap nestedNode {
                            {     "name",       "Inner"},
                            {"reference", "local.inner"}
                        };
                        const QVariantMap innerSource {
                            {"nodes", QVariantList {gate}}
                        };
                        const QVariantMap outerSource {
                            {"nodes", QVariantList {nestedNode}}
                        };
                        const QVariantMap innerDefinition {
                            {    "id", "local.inner"},
                            {  "name",       "Inner"},
                            {  "kind",    "lineflow"},
                            {"source",   innerSource}
                        };
                        const QVariantMap outerDefinition {
                            {    "id",        root},
                            {  "name",   "Inplace"},
                            {  "kind",  "lineflow"},
                            {"source", outerSource}
                        };
                        const QVariantMap definitions {
                            {         root, outerDefinition},
                            {"local.inner", innerDefinition}
                        };
                        QVariantMap processing;
                        processing.insert("mode", "simplex");
                        processing.insert("rx",
                                          QVariantMap {
                                              {"type", "@inplace"}
                        });
                        processing.insert("inplace",
                                          QVariantMap {
                                              {"rx", definitions}
                        });
                        settings.insert("processing", processing);
                        for (int index = 0; index < 5; ++index)
                        {
                            if (!app.createTerminal(typeId, settings, "Nested close fixture", &error))
                            {
                                marker("nestedSetupFailed: " + error.toUtf8());
                                std::_Exit(87);
                            }
                        }
                    }
                    if (!app.createTerminal(typeId, settings, "Close terminal", &error))
                    {
                        marker("terminalFailed: " + error.toUtf8());
                        std::_Exit(87);
                    }
                    app.mainWindow().activateWindow();
                    QApplication::processEvents();
                    app.pluginHost().connectSelectedTerminal();
                }
                if (scenario == "detached")
                {
                    auto&      controller = static_cast<TerminalController&>(app.pluginHost());
                    const auto first      = app.selectedTerminal();
                    if (!controller.detachTerminal(first, &error)
                        || !app.createTerminal("closeFixture", {}, "Second detached", &error)
                        || !controller.detachTerminal(app.selectedTerminal(), &error))
                    {
                        marker("detachFailed: " + error.toUtf8());
                        std::_Exit(87);
                    }
                    marker("detachedReady");
                }
                if (scenario == "splitGroups")
                {
                    auto&      controller = static_cast<TerminalController&>(app.pluginHost());
                    const auto first      = app.selectedTerminal();
                    if (!app.createTerminal("closeFixture", {}, "Second group", &error))
                    {
                        marker("splitGroupsFailed: " + error.toUtf8());
                        std::_Exit(87);
                    }
                    const auto second = app.selectedTerminal();
                    if (!controller.moveTerminal(first, controller.terminalGroup(second), Qt::RightEdge, -1, &error)
                        || !controller.terminalGroup(first) || !controller.terminalGroup(second)
                        || controller.terminalGroup(first) == controller.terminalGroup(second))
                    {
                        marker("splitGroupsFailed: " + error.toUtf8());
                        std::_Exit(87);
                    }
                    marker("splitGroupsReady");
                }
                if (scenario == "auxiliary")
                {
                    app.commands().find("Application.Settings")->actions().first()->trigger();
                    app.commands().find("Workspace.Processing")->actions().first()->trigger();
                    // A separately owned primary window makes the main-window lifetime
                    // contract independent of Qt's last-visible-window heuristic.
                    auxiliary = std::make_unique<QDialog>();
                    auxiliary->setWindowTitle("Auxiliary close fixture");
                    auxiliary->show();
                }
                if (scenario == "quitDisabled")
                {
                    app.setQuitOnLastWindowClosed(false);
                }
                auto* closeTimer = new QTimer(&app);
                closeTimer->setInterval(100);
                QObject::connect(closeTimer, &QTimer::timeout, &app, [&, closeTimer, error = QString {}]() mutable {
                    if (scenario.startsWith("console"))
                    {
                        app.pluginHost().connectSelectedTerminal();
                        auto* device = app.pluginHost().selectedDevice("console");
                        if (!device || device->state() != SerialDevice::State::Open)
                        {
                            return;
                        }
                        marker("consoleOpen");
                    }
                    if (scenario == "consoleInplace")
                    {
                        auto* panel = app.mainWindow().findChild<TerminalProcessingPanel*>();
                        panel->findChild<QComboBox*>("processingMode")->setCurrentIndex(2);
                        panel->findChild<QToolButton*>("duplexInplace")->click();
                        auto* editor = panel->findChild<LineflowEditorDialog*>("inplaceLineflowEditor");
                        if (!editor || !editor->isVisible())
                        {
                            marker("inplaceSetupFailed");
                            std::_Exit(87);
                        }
                        auto&      model = editor->model();
                        const auto node  = model.insertNodeAt(model.selectedBlock(),
                                                              0,
                                                              "native.pattern-framer",
                                                              "tx",
                                                              &error);
                        if (node.isEmpty())
                        {
                            marker("inplaceNodeFailed: " + error.toUtf8());
                            std::_Exit(87);
                        }
                        editor->findChild<QAbstractItemView*>("lineflowCanvas")->setCurrentIndex(model.index(0, 0));
                        marker("inplaceOpen");
                    }
                    closeTimer->stop();
                    if (scenario == "saveFailure")
                    {
                        const auto path = app.settings().filePath();
                        QDir().mkpath(QFileInfo(path).absolutePath());
                        QFile::remove(path);
                        if (!QDir().mkdir(path) || !app.settings().setValue("settings/closeFixture", true))
                        {
                            marker("saveFailureSetupFailed");
                            std::_Exit(87);
                        }
                        QTimer::singleShot(50, &app, [&] {
                            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                            {
                                dialog->accept();
                            }
                        });
                        if (app.mainWindow().close() || !app.mainWindow().isVisible())
                        {
                            marker("saveFailureDidNotVeto");
                            std::_Exit(88);
                        }
                        marker("saveVetoed");
                        QDir().rmdir(path);
                    }
                    if (!app.mainWindow().close())
                    {
                        marker("mainCloseRejected");
                        std::_Exit(89);
                    }
                    marker("mainClosed");
                    result = 0;
                });
                closeTimer->start();
            });
            const auto loopResult = app.run();
            marker("loopExited");
            if (loopResult != 0)
            {
                result = loopResult;
            }
            auxiliary.reset();
        }
        marker("destructorFinished");
        return result;
    }

    const QString loggingRoot = qEnvironmentVariable("TERMASTER_APP_LOGGING_CHILD");
    if (!loggingRoot.isEmpty())
    {
        App::Options options;
        options.dataDirectory    = loggingRoot + "/data";
        options.pluginsDirectory = loggingRoot + "/empty-plugins";
        int result               = 1;
        {
            App app(argc, argv, options);
            QTimer::singleShot(0, &app, [&] {
                if (app.paths().logsDirectory().exists())
                {
                    result = 101;
                    app.quit();
                    return;
                }
                if (qEnvironmentVariableIsSet("TERMASTER_APP_LOGGING_STARTUP_ONLY"))
                {
                    result = 0;
                    app.quit();
                    return;
                }
                app.commands().find("Application.Settings")->actions().first()->trigger();
                auto* dialog = app.mainWindow().findChild<SettingsDialog*>("settingsDialog");
                auto* model  = dialog ? dialog->pageModel("application.logging") : nullptr;
                if (!model)
                {
                    result = 102;
                    app.quit();
                    return;
                }
                model->setValue("tagLevels",
                                QVariantMap {
                                    {"termaster:transport", "invalid"}
                });
                if (model->isValid())
                {
                    result = 103;
                    app.quit();
                    return;
                }
                model->setValue("tagLevels",
                                QVariantMap {
                                    {"bad tag", "warning"}
                });
                if (model->isValid())
                {
                    result = 109;
                    app.quit();
                    return;
                }
                model->setValue("tagLevels",
                                QVariantMap {
                                    {"termaster:transport", "warning"}
                });
                model->setValue("file", true);
                dialog->selectPage("application.logging");
                const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
                if (!capture.isEmpty())
                {
                    QDir().mkpath(capture);
                    QApplication::processEvents();
                    if (!dialog->grab().save(QDir(capture).filePath("Logging.png")))
                    {
                        app.quit();
                        return;
                    }
                }
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
                if (!app.settings().value("settings/logging/file").toBool() || !app.paths().logsDirectory().exists())
                {
                    const auto errors = model->errors();
                    std::fprintf(stderr,
                                 "Logging apply saved=%d directory=%d valid=%d errors=%s\n",
                                 app.settings().value("settings/logging/file").toBool(),
                                 app.paths().logsDirectory().exists(),
                                 model->isValid(),
                                 errors.isEmpty() ? "none" :
                                                    qPrintable(errors.first().path + ": " + errors.first().message));
                }
                dialog->reject();
                if (!app.settings().value("settings/logging/file").toBool() || !app.paths().logsDirectory().exists())
                {
                    result = 104;
                    app.quit();
                    return;
                }
                qInfo("logging-user-before");
                QDir().mkpath(loggingRoot + "/workspace");
                QString error;
                if (!app.createWorkspace(Path(loggingRoot + "/workspace/example.termaster"), "Logging", &error))
                {
                    result = 105;
                    app.quit();
                    return;
                }
                qInfo("logging-workspace");
                QDir().mkpath(loggingRoot + "/blocked");
                QFile blocker(loggingRoot + "/blocked/logs");
                if (!blocker.open(QIODevice::WriteOnly))
                {
                    result = 106;
                    app.quit();
                    return;
                }
                blocker.close();
                if (!app.createWorkspace(Path(loggingRoot + "/blocked/example.termaster"), "Blocked logs", &error)
                    || !app.mainWindow().statusBar()->currentMessage().contains("Logging:"))
                {
                    result = 107;
                    app.quit();
                    return;
                }
                qInfo("logging-failed-switch");
                if (!app.closeWorkspace(&error))
                {
                    result = 108;
                    app.quit();
                    return;
                }
                qInfo("logging-user-after");
                result = 0;
                app.mainWindow().close();
            });
            if (app.run() != 0)
            {
                result = 1;
            }
        }
        return result;
    }

    const QString restartData = qEnvironmentVariable("TERMASTER_APP_RESTART_DATA");
    if (!restartData.isEmpty())
    {
        App::Options options;
        options.dataDirectory    = restartData;
        options.pluginsDirectory = restartData + "/empty-plugins";
        App app(argc, argv, options);
        int result = 1;
        QTimer::singleShot(0, &app, [&]() {
            if (qEnvironmentVariableIsSet("TERMASTER_SHORTCUT_RESTART"))
            {
                auto* command = app.commands().create("Fixture.Shortcut",
                                                      "Restart fixture",
                                                      {},
                                                      QKeySequence("Ctrl+Alt+F8"));
                auto* late    = app.commands().create("Fixture.Late", "Restart plugin");
                result = command->shortcut().isEmpty() && late->shortcut() == QKeySequence("Ctrl+Alt+F10") ? 0 : 1;
            }
            else if (qEnvironmentVariableIsSet("TERMASTER_APP_RESTART_SESSION"))
            {
                result = app.workspace() && app.session()
                              && app.session()->id() == qEnvironmentVariable("TERMASTER_APP_RESTART_SESSION")
                              && app.session()->name() == "Renamed session"
                              && app.configuration().value("sessionTest/value").toInt() == 31
                              && app.session()->objects().values().size() == 2
                              && app.session()->links().values().size() == 1
                              && app.configuration().setValue("sessionTest/childAutosave", 99) ?
                             0 :
                             1;
            }
            else if (qEnvironmentVariableIsSet("TERMASTER_APP_RECOVERY"))
            {
                result = !app.workspace() && app.mainWindow().isVisible()
                              && app.mainWindow().findChild<QPushButton*>("openWorkspace")->isVisible()
                              && !app.mainWindow().findChild<QLabel*>("workspaceMessage")->text().isEmpty()
                              && !QApplication::activeModalWidget() ?
                             0 :
                             1;
            }
            else
            {
                result = app.workspace()
                              && app.workspace()->filePath().toString()
                                     == qEnvironmentVariable("TERMASTER_APP_RESTART_WORKSPACE")
                              && app.configuration().value("appearance/fontSize").toInt() == 18
                              && app.configuration().value("appearance/theme").toString() == "dark"
                              && !QApplication::activeModalWidget() ?
                             0 :
                             1;
                if (result == 0)
                {
                    result = app.settings().setValue("settings/child/autosave", 41)
                                  && app.configuration().setValue("child/autosave", 42) ?
                                 0 :
                                 1;
                }
            }
            if (!app.mainWindow().close())
            {
                result = 1;
                app.quit();
            }
        });
        const int loopResult = app.run();
        return loopResult == 0 ? result : loopResult;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid())
    {
        return 1;
    }
    int                   result = 1;
    QPointer<QMainWindow> window;
    QPointer<QAction>     action;
    {
        App::Options options;
        options.dataDirectory    = temporary.path() + "/data";
        options.pluginsDirectory = temporary.path() + "/plugins";
        App      app(argc, argv, options);
        AppTests tests(app);
        window = &app.mainWindow();
        action = app.commands().find("Application.Plugins")->actions().first();
        QTimer::singleShot(0, &app, [&]() {
            result = QTest::qExec(&tests, argc, argv);
            app.quit();
        });
        const int loopResult = app.run();
        if (loopResult != 0)
        {
            result = loopResult;
        }
    }
    return window || action ? 1 : result;
}

#include "AppTests.moc"
