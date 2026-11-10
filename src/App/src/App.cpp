#include "Gui/Help/ThirdPartyDialog.h"
#include "Gui/Main/EditActions.h"
#include "Gui/Main/MainWindow.h"
#include "Gui/Processing/LineflowEditorDialog.h"
#include "Gui/Processing/ProcessorFavorites.h"
#include "Gui/Session/SessionPicker.h"
#include "Gui/Settings/FavoritesEditor.h"
#include "Gui/Settings/SettingsDialog.h"
#include "Gui/Settings/ShortcutEditor.h"
#include "Gui/Terminal/TerminalController.h"
#include "Processing/WorkspaceProcessingEdit.h"

#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QUuid>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/CommandShortcuts.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/Plugin/PluginManagerGui.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Plugin/PluginManager.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/RandomNames.h>
#include <Termaster/Core/Session.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <Termaster/Core/SettingsSchema.h>
#include <Termaster/Core/Version.h>
#include <Termaster/Core/Workspace.h>
#include <array>
#include <exception>

struct App::Data
{
    ApplicationPaths                                paths;
    Settings                                        defaults {QString {}};
    Settings                                        settings;
    Settings                                        pluginSettings;
    std::unique_ptr<Workspace>                      workspace;
    std::unique_ptr<Session>                        session;
    CommandRegistry                                 commands;
    CommandShortcuts                                shortcuts {&commands};
    SettingsWidgetRegistry                          settingsWidgets;
    std::unique_ptr<ProcessorFavorites>             processorFavorites;
    std::unique_ptr<MainWindow>                     window;
    std::unique_ptr<TerminalController>             terminals;
    std::unique_ptr<EditActions>                    editing;
    QPointer<QDialog>                               pluginWindow;
    QPointer<ThirdPartyDialog>                      thirdPartyWindow;
    QPointer<LineflowEditorDialog>                  lineflowWindow;
    QPointer<QAction>                               lineflowAction;
    QPointer<SettingsDialog>                        settingsWindow;
    QMap<SettingsContext, QPointer<SettingsDialog>> settingsWindows;
    QPointer<QAction>                               createSessionAction;
    QPointer<QAction>                               openSessionAction;
    QPointer<QAction>                               saveSessionAction;
    QPointer<QAction>                               closeSessionAction;
    QPointer<QAction>                               renameSessionAction;
    QPointer<QAction>                               workspaceSettingsAction;
    QPointer<QAction>                               sessionSettingsAction;
    QPointer<QAction>                               saveAction;
    QPointer<QAction>                               closeAction;
    QPointer<QAction>                               toolbarAction;
    QPointer<QAction>                               statusAction;
    QPointer<QAction>                               fullScreenAction;
    std::array<QPointer<QAction>, 10>               recentWorkspaces;
    std::array<QPointer<QAction>, 10>               recentSessions;
    bool                                            settingsLoaded        = false;
    bool                                            applyingConfiguration = false;
    // Shutdown may still use commands, views, workspace and settings.
    PluginManager plugins;

    explicit Data(const Options& options) :
        paths(_paths(options)),
        settings(paths.configFile("application"), {"termaster.application", "Termaster application settings"}),
        pluginSettings(paths.configFile("plugins"), {"termaster.plugins", "Plugin activation preferences"}),
        window(std::make_unique<MainWindow>()), plugins(pluginSettings, paths.pluginsDirectory().toString())
    {

        defaults.applyValues(
            {
                {     "appearance/fontFamily", QFontDatabase::systemFont(QFontDatabase::FixedFont).family()},
                {       "appearance/fontSize",                                                           12},
                {     "appearance/foreground",                                                    "#e5e7eb"},
                {     "appearance/background",                                                    "#15191f"},
                {"appearance/scrollbackLines",                                                        10000},
                {           "input/localEcho",                                                        false},
                {         "ui/toolbarVisible",                                                         true},
                {       "ui/statusBarVisible",                                                         true}
        },
            {});
    }

    SettingsOverlay applicationSettings()
    {
        return SettingsOverlay(defaults.node(""), settings.node("settings"));
    }

private:
    static ApplicationPaths _paths(const Options& options)
    {
        ApplicationPaths result;
        if (!options.dataDirectory.isEmpty())
        {
            result.setDataDirectory(Path(options.dataDirectory));
        }
        if (!options.pluginsDirectory.isEmpty())
        {
            result.setPluginsDirectory(Path(options.pluginsDirectory));
        }
        return result;
    }
};

App::App(int& argc, char** argv) : App(argc, argv, Options {})
{
}

App::App(int& argc, char** argv, const Options& options) : QApplication(argc, argv)
{
    setApplicationName("Termaster");
    setApplicationVersion(Version::shortVersion);
    Q_INIT_RESOURCE(appIcons);
    Q_INIT_RESOURCE(connectionStatusIcons);
    Q_INIT_RESOURCE(appSettingsSchema);
    Q_INIT_RESOURCE(thirdPartyNotices);
    setWindowIcon(QIcon(":/Termaster/App/IconOrange.png"));

    _data = std::make_unique<Data>(options);
    registerNativeProcessors(_data->plugins.processors(), "core.processing");
    _data->settingsWidgets.registerWidget("commandShortcuts", [this](const SettingsSchema::Field&, QWidget* parent) {
        return new ShortcutEditor(_data->commands, _data->shortcuts, parent);
    });
    _data->settingsWidgets.registerWidget("commandFavorites", [](const SettingsSchema::Field&, QWidget* parent) {
        return new FavoritesEditor(parent);
    });
    connect(&_data->shortcuts, &CommandShortcuts::diagnostic, _data->window.get(), [this](const QString& error) {
        _data->window->statusBar()->showMessage(error);
    });
    _setupActions();
    _data->terminals = std::make_unique<TerminalController>(
        *_data->window,
        commands(),
        settingsWidgets(),
        [this] { return _data->workspace.get(); },
        [this] {
            if (_data->lineflowWindow)
            {
                _data->lineflowWindow->releaseEditors();
            }
            const auto dialogs = _data->settingsWindows.values();
            for (const auto& dialog : dialogs)
            {
                delete dialog.data();
            }
        },
        [this] {
            _showSettings(SettingsContext::Terminal);
            if (_data->settingsWindow)
            {
                _data->settingsWindow->selectPage("terminal.connection");
            }
        },
        this);
    _data->terminals->setProcessorRegistry(_data->plugins.processors());
    _data->plugins.setHost(_data->terminals.get());
    connect(_data->terminals.get(), &TerminalController::selectionChanged, this, [this] {
        const auto dialog = _data->settingsWindows.value(SettingsContext::Terminal);
        if (dialog && dialog->property("terminalId").toString() != _data->terminals->selectedTerminal())
        {
            _closeSettings(SettingsContext::Terminal);
        }
    });
    _data->window->installEventFilter(this);
    _data->window->statusBar()->installEventFilter(this);
    connect(_data->window.get(), &MainWindow::openWorkspaceRequested, this, &App::_chooseWorkspace);
    connect(_data->window.get(), &MainWindow::createWorkspaceRequested, this, &App::_chooseNewWorkspace);
    connect(&_data->plugins,
            &PluginManager::errorOccurred,
            _data->window.get(),
            [window = _data->window.get()](const QString& error) { window->statusBar()->showMessage(error); });
    connect(_data->window.get(), &MainWindow::openSessionRequested, this, &App::_chooseSession);
    connect(_data->window.get(), &MainWindow::createSessionRequested, this, &App::_chooseNewSession);
    _updateWorkspace();
}

App::~App()
{
    delete _data->lineflowWindow.data();
    QObject::disconnect(_data->terminals.get(), nullptr, this, nullptr);
    _data->window->removeEventFilter(this);
    _data->window->statusBar()->removeEventFilter(this);
    // Stop callbacks into Data before its members begin destruction.
    for (auto* sender : _data->window->findChildren<QObject*>())
    {
        QObject::disconnect(sender, nullptr, this, nullptr);
    }
    QObject::disconnect(_data->window.get(), nullptr, this, nullptr);
}

int App::run()
{
    QString error;
    if (!_ensureApplicationSettings(&error))
    {
        _data->window->statusBar()->showMessage(error);
    }
    if (!_data->plugins.start(&error))
    {
        _data->window->statusBar()->showMessage(error);
    }
    for (const auto& plugin : _data->plugins.plugins())
    {
        if ((plugin.info.id == "SerialTerminal" || plugin.info.id == "ConsoleTerminal"
             || plugin.info.id == "SocketTerminal")
            && !_data->pluginSettings.contains("enabled/" + plugin.info.id))
        {
            if (!_data->plugins.setEnabled(plugin.info.id, true, &error))
            {
                _data->window->statusBar()->showMessage(error);
            }
        }
    }
    if (!restoreLastWorkspace(&error))
    {
        _data->window->showWorkspaceChoice(error);
        _data->window->statusBar()->showMessage(error);
    }
    _applyConfiguration();
    _updateRecent();
    _data->window->show();
    return exec();
}

const ApplicationPaths& App::paths() const
{
    return _data->paths;
}
Settings& App::settings()
{
    return _data->settings;
}
SettingsWidgetRegistry& App::settingsWidgets()
{
    return _data->settingsWidgets;
}

CommandRegistry& App::commands()
{
    return _data->commands;
}
QMainWindow& App::mainWindow()
{
    return *_data->window;
}
MenubarActionContainer& App::mainMenu()
{
    return _data->window->mainMenu();
}
ToolbarActionContainer& App::toolbar()
{
    return _data->window->toolbar();
}
Workspace* App::workspace() const
{
    return _data->workspace.get();
}

QStringList App::recentWorkspaces() const
{
    QStringList result;
    if (!_data->settingsLoaded)
    {
        return result;
    }
    for (const auto& value : _data->settings.value("state/recentWorkspaces").toList())
    {
        result.append(value.toString());
    }
    return result;
}

SettingsOverlay App::configuration()
{
    if (_data->session)
    {
        return _data->session->settings();
    }
    if (_data->workspace)
    {
        return _data->workspace->settings();
    }
    return _data->applicationSettings();
}

bool App::restoreLastWorkspace(QString* error)
{
    if (!_ensureApplicationSettings(error))
    {
        return false;
    }
    if (!_data->settings.contains("state/lastWorkspace"))
    {
        return true;
    }
    const QVariant value = _data->settings.value("state/lastWorkspace");
    if (value.typeId() != QMetaType::QString || value.toString().isEmpty())
    {
        if (error)
        {
            *error = tr("Last workspace must be a nonempty file path.");
        }
        return false;
    }
    try
    {
        const Path file(value.toString());
        if (!file.isAbsolute())
        {
            if (error)
            {
                *error = tr("Last workspace path must be absolute.");
            }
            return false;
        }
        const auto canonical = file.canonical();
        if (_data->workspace && canonical && _data->workspace->filePath() == *canonical)
        {
            return true;
        }
        if (!WorkspaceProcessingEdit::recover(file, error))
        {
            return false;
        }
        auto candidate = std::make_unique<Workspace>(file, _data->applicationSettings());
        if (!candidate->load(error))
        {
            return false;
        }
        if (!_prepareWorkspaceProcessors(*candidate, error))
        {
            return false;
        }
        if (_data->workspace && !saveWorkspace(error))
        {
            return false;
        }
        const bool hadSession = bool(_data->session);
        _data->terminals->setSession(nullptr);
        _closeSettings(SettingsContext::Session);
        _closeSettings(SettingsContext::Terminal);
        _data->session.reset();
        _closeSettings(SettingsContext::Workspace);
        delete _data->lineflowWindow.data();
        _data->workspace = std::move(candidate);
        _bindWorkspaceProcessors();
        _updateWorkspace();
        if (hadSession)
        {
            emit sessionChanged();
        }
        emit workspaceChanged();
        _restoreLastSession();
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString::fromUtf8(exception.what());
        }
        return false;
    }
}

bool App::openWorkspace(const Path& file, QString* error)
{
    if (!_ensureApplicationSettings(error))
    {
        return false;
    }
    const auto canonical = file.canonical();
    if (_data->workspace && canonical && _data->workspace->filePath() == *canonical)
    {
        return _rememberWorkspace(*canonical, error);
    }
    if (!WorkspaceProcessingEdit::recover(file, error))
    {
        return false;
    }
    auto candidate = std::make_unique<Workspace>(file, _data->applicationSettings());
    return candidate->load(error) && _activateWorkspace(std::move(candidate), error);
}

bool App::createWorkspace(const Path& file, const QString& name, QString* error)
{
    if (!_ensureApplicationSettings(error))
    {
        return false;
    }
    auto candidate = std::make_unique<Workspace>(file, _data->applicationSettings());
    return candidate->create(name, error) && _activateWorkspace(std::move(candidate), error);
}

bool App::saveWorkspace(QString* error)
{
    if (!_data->workspace)
    {
        if (error)
        {
            *error = tr("No workspace is open.");
        }
        return false;
    }
    if ((_data->session && !_data->session->sync(error)) || !_data->workspace->sync(error))
    {
        return false;
    }
    _updateSession();
    return true;
}

bool App::closeWorkspace(QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_data->workspace)
    {
        return true;
    }
    if (!saveWorkspace(error))
    {
        return false;
    }
    const bool hadSession = bool(_data->session);
    _data->terminals->setSession(nullptr);
    _closeSettings(SettingsContext::Session);
    _closeSettings(SettingsContext::Terminal);
    _data->session.reset();
    _closeSettings(SettingsContext::Workspace);
    if (!_data->plugins.processors().publishDefinitions("workspace", {}, error))
    {
        return false;
    }
    delete _data->lineflowWindow.data();
    _data->processorFavorites.reset();
    _data->workspace.reset();
    _updateWorkspace();
    if (hadSession)
    {
        emit sessionChanged();
    }
    emit workspaceChanged();
    return true;
}

bool App::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _data->window.get() && event->type() == QEvent::Close)
    {
        QString error;
        if (!_saveChanges(&error))
        {
            static_cast<QCloseEvent*>(event)->ignore();
            _showError(error);
            return true;
        }

        // Finish the close event before leaving the application event loop.
        // Auxiliary windows must not keep a closed application shell alive.
        QTimer::singleShot(0, this, [this] {
            if (_data->window->isHidden())
            {
                quit();
            }
        });
    }
    if (watched == _data->window->statusBar() && (event->type() == QEvent::Show || event->type() == QEvent::Hide)
        && _data->statusAction)
    {
        const bool           visible = !_data->window->statusBar()->isHidden();
        const bool           changed = _data->statusAction->isChecked() != visible;
        const QSignalBlocker blocker(_data->statusAction);
        _data->statusAction->setChecked(visible);
        if (changed)
        {
            _storeViewSetting("ui/statusBarVisible", visible);
        }
    }
    if (watched == _data->window.get() && event->type() == QEvent::WindowStateChange && _data->fullScreenAction)
    {
        const QSignalBlocker blocker(_data->fullScreenAction);
        _data->fullScreenAction->setChecked(_data->window->isFullScreen());
    }
    return QApplication::eventFilter(watched, event);
}

void App::_setupActions()
{
    auto* create = _addAction("Workspace.New",
                              tr("&New workspace…"),
                              Icons::icon("folder-plus"),
                              QKeySequence::New,
                              "file",
                              "workspace");
    create->setToolTip(tr("Create a workspace"));
    connect(create, &QAction::triggered, this, &App::_chooseNewWorkspace);
    auto* open = _addAction("Workspace.Open",
                            tr("&Open workspace…"),
                            Icons::icon("folder-open"),
                            QKeySequence::Open,
                            "file",
                            "workspace");
    open->setToolTip(tr("Open a workspace"));
    connect(open, &QAction::triggered, this, &App::_chooseWorkspace);
    _data->saveAction = _addAction("Workspace.Save",
                                   tr("&Save workspace"),
                                   Icons::icon("save"),
                                   QKeySequence::Save,
                                   "file",
                                   "workspace");
    _data->saveAction->setToolTip(tr("Save the current workspace"));
    connect(_data->saveAction, &QAction::triggered, this, [this]() {
        QString error;
        if (!saveWorkspace(&error))
        {
            _showError(error);
        }
    });
    _data->closeAction = _addAction("Workspace.Close",
                                    tr("&Close workspace"),
                                    Icons::icon("x"),
                                    QKeySequence::Close,
                                    "file",
                                    "workspace");
    _data->closeAction->setToolTip(tr("Close the current workspace"));
    connect(_data->closeAction, &QAction::triggered, this, [this]() {
        QString error;
        if (!closeWorkspace(&error))
        {
            _showError(error);
        }
    });
    for (std::size_t index = 0; index < _data->recentWorkspaces.size(); ++index)
    {
        auto* action                   = _addAction(QString("Workspace.Recent.%1").arg(index + 1),
                                                    {},
                                                    {},
                                                    {},
                                                    "file/recentWorkspaces",
                                                    "entries");
        _data->recentWorkspaces[index] = action;
        connect(action, &QAction::triggered, this, [this, action] {
            QString error;
            if (!openWorkspace(Path(action->data().toString()), &error))
            {
                _showError(error);
            }
        });
        _data->recentSessions[index] = _addAction(QString("Session.Recent.%1").arg(index + 1),
                                                  {},
                                                  {},
                                                  {},
                                                  "session/recentSessions",
                                                  "entries");
    }
    for (const auto& action : _data->recentSessions)
    {
        connect(action, &QAction::triggered, this, [this, action] {
            QString error;
            if (!openSession(action->data().toString(), &error))
            {
                _showError(error);
            }
        });
    }
    _data->createSessionAction = _addAction("Session.New",
                                            tr("New &session…"),
                                            Icons::icon("file-plus"),
                                            {},
                                            "session",
                                            "session");
    _data->createSessionAction->setToolTip(tr("Create a session"));
    connect(_data->createSessionAction, &QAction::triggered, this, &App::_chooseNewSession);
    _data->openSessionAction = _addAction("Session.Open",
                                          tr("Open session…"),
                                          Icons::icon("folder-open"),
                                          {},
                                          "session",
                                          "session");
    _data->openSessionAction->setToolTip(tr("Open a session from this workspace"));
    connect(_data->openSessionAction, &QAction::triggered, this, &App::_chooseSession);
    _data->saveSessionAction = _addAction("Session.Save",
                                          tr("Save session"),
                                          Icons::icon("save"),
                                          {},
                                          "session",
                                          "session");
    _data->saveSessionAction->setToolTip(tr("Save the current session"));
    connect(_data->saveSessionAction, &QAction::triggered, this, [this] {
        QString error;
        if (!saveSession(&error))
        {
            _showError(error);
        }
    });
    _data->closeSessionAction = _addAction("Session.Close",
                                           tr("Close session"),
                                           Icons::icon("x"),
                                           {},
                                           "session",
                                           "session");
    _data->closeSessionAction->setToolTip(tr("Close the current session"));
    connect(_data->closeSessionAction, &QAction::triggered, this, [this] {
        QString error;
        if (!closeSession(&error))
        {
            _showError(error);
        }
    });
    _data->renameSessionAction = _addAction("Session.Rename",
                                            tr("Rename session…"),
                                            Icons::icon("pencil"),
                                            {},
                                            "session",
                                            "session");
    connect(_data->renameSessionAction, &QAction::triggered, this, [this] {
        if (!_data->session)
        {
            return;
        }
        bool       accepted = false;
        const auto name     = QInputDialog::getText(_data->window.get(),
                                                    tr("Rename session"),
                                                    tr("Name:"),
                                                    QLineEdit::Normal,
                                                    _data->session->name(),
                                                    &accepted);
        if (!accepted)
        {
            return;
        }
        QString error;
        if (!renameSession(name, &error))
        {
            _showError(error);
        }
    });
    _data->workspaceSettingsAction = _addAction("Workspace.Settings",
                                                tr("Workspace settings…"),
                                                Icons::icon("sliders-horizontal"),
                                                {},
                                                "file",
                                                "settings");
    connect(_data->workspaceSettingsAction, &QAction::triggered, this, [this] {
        _showSettings(SettingsContext::Workspace);
    });
    _data->sessionSettingsAction = _addAction("Session.Settings",
                                              tr("Session settings…"),
                                              Icons::icon("sliders-horizontal"),
                                              {},
                                              "session",
                                              "settings");
    connect(_data->sessionSettingsAction, &QAction::triggered, this, [this] {
        _showSettings(SettingsContext::Session);
    });
    for (const auto& path : {"/file/recentWorkspaces", "/session/recentSessions"})
    {
        connect(static_cast<MenuActionContainer*>(mainMenu().findMenu(path))->menu(),
                &QMenu::aboutToShow,
                this,
                &App::_updateRecent);
    }
    auto* quit = _addAction("Application.Exit", tr("E&xit"), {}, QKeySequence::Quit, "file", "application");
    quit->setMenuRole(QAction::QuitRole);
    connect(quit, &QAction::triggered, _data->window.get(), &QWidget::close);

    _data->editing       = std::make_unique<EditActions>(*_data->window, commands(), *mainMenu().findMenu("/edit"));
    _data->toolbarAction = _addAction("View.Toolbar", tr("&Toolbar"), Icons::icon("panel-top"), {}, "view", "panels");
    _data->statusAction  = _addAction("View.StatusBar",
                                      tr("&Status bar"),
                                      Icons::icon("panel-bottom"),
                                      {},
                                      "view",
                                      "panels");
    for (auto* action : {_data->toolbarAction.data(), _data->statusAction.data()})
    {
        action->setCheckable(true);
        action->setChecked(true);
    }
    connect(_data->toolbarAction, &QAction::toggled, this, [this](bool visible) {
        toolbar().toolbar()->setVisible(visible);
        _storeViewSetting("ui/toolbarVisible", visible);
    });
    connect(toolbar().toolbar(), &QToolBar::visibilityChanged, this, [this](bool) {
        if (!_data->toolbarAction)
        {
            return;
        }
        const bool           visible = !toolbar().toolbar()->isHidden();
        const bool           changed = _data->toolbarAction->isChecked() != visible;
        const QSignalBlocker blocker(_data->toolbarAction);
        _data->toolbarAction->setChecked(visible);
        if (changed)
        {
            _storeViewSetting("ui/toolbarVisible", visible);
        }
    });
    connect(_data->statusAction, &QAction::toggled, this, [this](bool visible) {
        _data->window->statusBar()->setVisible(visible);
        _storeViewSetting("ui/statusBarVisible", visible);
    });
    _data->fullScreenAction = _addAction("View.FullScreen",
                                         tr("&Full screen"),
                                         {},
                                         QKeySequence(Qt::Key_F11),
                                         "view",
                                         "window");
    _data->fullScreenAction->setCheckable(true);
    connect(_data->fullScreenAction, &QAction::toggled, this, [this](bool full) {
        if (full)
        {
            _data->window->showFullScreen();
        }
        else
        {
            _data->window->showNormal();
        }
    });
    auto* preferences = _addAction("Application.Settings",
                                   tr("&Settings…"),
                                   Icons::icon("sliders-horizontal"),
                                   QKeySequence::Preferences,
                                   "tools",
                                   "management");
    preferences->setMenuRole(QAction::PreferencesRole);
    connect(preferences, &QAction::triggered, this, [this] { _showSettings(); });
    auto* plugins = _addAction("Application.Plugins",
                               tr("&Plugins…"),
                               Icons::icon("puzzle"),
                               {},
                               "tools",
                               "management");
    plugins->setToolTip(tr("Manage plugins"));
    connect(plugins, &QAction::triggered, this, &App::_showPluginManager);
    _data->lineflowAction = _addAction("Workspace.Processing",
                                       tr("Lineflow editor…"),
                                       Icons::icon("Lineflow"),
                                       {},
                                       "tools",
                                       "management");
    _data->lineflowAction->setToolTip(tr("Edit reusable workspace processor blocks"));
    connect(_data->lineflowAction, &QAction::triggered, this, &App::_showLineflowEditor);
    auto* about = _addAction("Help.About", tr("&About Termaster"), windowIcon(), {}, "help", "about");
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(_data->window.get(), tr("About Termaster"), tr("Termaster %1").arg(Version::shortVersion));
    });
    auto* thirdParty = _addAction("Help.ThirdParty",
                                  tr("Third-party &components…"),
                                  Icons::icon("blocks"),
                                  {},
                                  "help",
                                  "about");
    connect(thirdParty, &QAction::triggered, this, [this] {
        if (!_data->thirdPartyWindow)
        {
            _data->thirdPartyWindow = new ThirdPartyDialog(_data->window.get());
        }
        _data->thirdPartyWindow->show();
        _data->thirdPartyWindow->raise();
        _data->thirdPartyWindow->activateWindow();
    });
}

void App::_showPluginManager()
{
    if (!_data->pluginWindow)
    {
        _data->pluginWindow = createPluginManagerWindow(_data->plugins, _data->window.get());
        _data->pluginWindow->setAttribute(Qt::WA_DeleteOnClose);
    }
    _data->pluginWindow->show();
    _data->pluginWindow->raise();
    _data->pluginWindow->activateWindow();
}

void App::_showLineflowEditor()
{
    if (!_data->workspace)
    {
        return;
    }
    if (!_data->lineflowWindow)
    {
        QPointer<Workspace> workspace(_data->workspace.get());
        _data->lineflowWindow = new LineflowEditorDialog(
            workspace->processorDefinitions().values(),
            _data->plugins.processors(),
            _data->settingsWidgets,
            [this, workspace](const QVariantMap&                         baseline,
                              const QVariantMap&                         draft,
                              const QList<ProcessorRefactoring::Rename>& renames,
                              QString*                                   error) -> LineflowEditorDialog::ApplyResult {
                if (!workspace || _data->workspace.get() != workspace.data())
                {
                    if (error)
                    {
                        *error = tr("The editor workspace is no longer open.");
                    }
                    return false;
                }
                if (!WorkspaceProcessingEdit::apply(*workspace,
                                                    _data->session.get(),
                                                    _data->plugins.processors(),
                                                    baseline,
                                                    draft,
                                                    renames,
                                                    error))
                {
                    return false;
                }
                return LineflowEditorDialog::ApplyResult(true, workspace->processorDefinitions().values());
            },
            _data->window.get(),
            _data->processorFavorites.get());
        _data->lineflowWindow->restoreDockLayout(
            QByteArray::fromBase64(_data->settings.value("settings/ui/lineflow/docks").toString().toLatin1()));
        connect(_data->lineflowWindow, &LineflowEditorDialog::dockLayoutChanged, this, [this](const QByteArray& state) {
            QString error;
            if (!_data->settings.setValue("settings/ui/lineflow/docks", QString::fromLatin1(state.toBase64()), &error))
            {
                _data->window->statusBar()->showMessage(error);
            }
        });
        _data->lineflowWindow->setAttribute(Qt::WA_DeleteOnClose);
    }
    _data->lineflowWindow->show();
    _data->lineflowWindow->raise();
    _data->lineflowWindow->activateWindow();
}

bool App::_ensureApplicationSettings(QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (_data->settingsLoaded)
    {
        return true;
    }
    if (!_data->settings.load(error))
    {
        return false;
    }
    for (const auto& key : {"settings", "state"})
    {
        if (_data->settings.contains(key) && _data->settings.value(key).typeId() != QMetaType::QVariantMap)
        {
            if (error)
            {
                *error = tr("Application field '%1' must be a mapping.").arg(key);
            }
            return false;
        }
    }
    if (_data->settings.contains("state/recentWorkspaces"))
    {
        const auto recent = _data->settings.value("state/recentWorkspaces");
        if (recent.typeId() != QMetaType::QVariantList || recent.toList().size() > 10)
        {
            if (error)
            {
                *error = tr("Recent workspaces must be a list of at most 10 file paths.");
            }
            return false;
        }
        QSet<QString> seen;
        for (const auto& value : recent.toList())
        {
            const QString path = value.toString();
            if (value.typeId() != QMetaType::QString || path.isEmpty() || path.contains(QChar::Null)
                || !Path(path).isAbsolute() || seen.contains(path))
            {
                if (error)
                {
                    *error = tr("Recent workspace entries must be unique absolute file paths.");
                }
                return false;
            }
            seen.insert(path);
        }
    }
    const auto assignments = _data->settings.value("settings/shortcuts", QVariantMap {});
    if (assignments.typeId() != QMetaType::QVariantMap)
    {
        if (error)
        {
            *error = tr("Application shortcuts must be a mapping.");
        }
        return false;
    }
    if (!_data->shortcuts.setAssignments(assignments.toMap(), error))
    {
        return false;
    }
    _data->settingsLoaded = true;
    return true;
}

bool App::_rememberWorkspace(const Path& file, QString* error)
{
    const bool existed   = _data->settings.contains("state/lastWorkspace");
    const auto old       = _data->settings.value("state/lastWorkspace");
    const bool hadRecent = _data->settings.contains("state/recentWorkspaces");
    const auto oldRecent = _data->settings.value("state/recentWorkspaces");
    auto       recent    = recentWorkspaces();
    recent.removeAll(file.toString());
    recent.prepend(file.toString());
    while (recent.size() > 10)
    {
        recent.removeLast();
    }
    QVariantList entries;
    for (const auto& path : recent)
    {
        entries.append(path);
    }
    const auto rollback = [&] {
        if (existed)
        {
            _data->settings.setValue("state/lastWorkspace", old);
        }
        else
        {
            _data->settings.remove("state/lastWorkspace");
        }
        if (hadRecent)
        {
            _data->settings.setValue("state/recentWorkspaces", oldRecent);
        }
        else
        {
            _data->settings.remove("state/recentWorkspaces");
        }
    };
    if (!_data->settings.setValue("state/lastWorkspace", file.toString(), error))
    {
        return false;
    }
    if (!_data->settings.setValue("state/recentWorkspaces", entries, error) || !_data->settings.sync(error))
    {
        rollback();
        return false;
    }
    _updateRecent();
    return true;
}

bool App::_activateWorkspace(std::unique_ptr<Workspace> candidate, QString* error)
{
    if (!_prepareWorkspaceProcessors(*candidate, error))
    {
        return false;
    }

    if (_data->workspace && !saveWorkspace(error))
    {
        return false;
    }
    if (!_rememberWorkspace(candidate->filePath(), error))
    {
        return false;
    }
    const bool hadSession = bool(_data->session);
    _data->terminals->setSession(nullptr);
    _closeSettings(SettingsContext::Session);
    _closeSettings(SettingsContext::Terminal);
    _data->session.reset();
    _closeSettings(SettingsContext::Workspace);
    delete _data->lineflowWindow.data();
    _data->workspace = std::move(candidate);
    _bindWorkspaceProcessors();
    _updateWorkspace();
    if (hadSession)
    {
        emit sessionChanged();
    }
    emit workspaceChanged();
    _restoreLastSession();
    return true;
}

bool App::_saveChanges(QString* error)
{
    if (_data->lineflowWindow
        && !_data->settings.setValue("settings/ui/lineflow/docks",
                                     QString::fromLatin1(_data->lineflowWindow->dockLayout().toBase64()),
                                     error))
    {
        return false;
    }

    if (_data->workspace && !saveWorkspace(error))
    {
        return false;
    }
    return !_data->settings.isDirty() || _data->settings.sync(error);
}

bool App::_prepareWorkspaceProcessors(Workspace& workspace, QString* error)
{
    const auto validate = [this](const QVariantMap& values, QString* error) {
        const auto definitions = ProcessorRegistry::parseDefinitions(values, error);
        return definitions && _data->plugins.processors().validatePublication("workspace", *definitions, error);
    };
    if (!validate(workspace.processorDefinitions().values(), error))
    {
        return false;
    }
    workspace.setProcessorValidator(validate);
    return true;
}

void App::_bindWorkspaceProcessors()
{
    const QPointer<Workspace> workspace(_data->workspace.get());
    _data->processorFavorites = std::make_unique<
        ProcessorFavorites>(workspace->state().node("processing"), [this, workspace](QString* error) {
        if (!workspace || _data->workspace.get() != workspace)
        {
            if (error)
            {
                *error = tr("The favorites workspace is no longer open.");
            }
            return false;
        }
        return workspace->sync(error);
    });
    _data->terminals->setProcessorFavorites(*_data->processorFavorites);

    const auto publish = [this] {
        QString    error;
        const auto definitions = ProcessorRegistry::parseDefinitions(_data->workspace->processorDefinitions().values(),
                                                                     &error);
        if (!definitions || !_data->plugins.processors().publishDefinitions("workspace", *definitions, &error))
        {
            _data->window->statusBar()->showMessage(tr("Cannot publish workspace processors: %1").arg(error));
        }
    };
    connect(_data->workspace.get(), &Workspace::processorsChanged, this, publish);
    publish();
}

void App::_updateWorkspace()
{
    const bool open = bool(_data->workspace);
    if (_data->lineflowAction)
    {
        _data->lineflowAction->setEnabled(open);
    }
    if (_data->workspaceSettingsAction)
    {
        _data->workspaceSettingsAction->setEnabled(open);
    }
    if (_data->saveAction)
    {
        _data->saveAction->setEnabled(open);
    }
    if (_data->closeAction)
    {
        _data->closeAction->setEnabled(open);
    }
    if (open)
    {
        _data->window->setWorkspace(_data->workspace->name(), _data->workspace->filePath().toString());
    }
    else
    {
        _data->window->showWorkspaceChoice();
    }
    _updateSession();
}

void App::_applyConfiguration()
{
    const QScopedValueRollback guard(_data->applyingConfiguration, true);
    const bool                 toolbarVisible = _data->applicationSettings().value("ui/toolbarVisible", true).toBool();
    const bool                 statusVisible = _data->applicationSettings().value("ui/statusBarVisible", true).toBool();
    if (_data->toolbarAction)
    {
        _data->toolbarAction->setChecked(toolbarVisible);
    }
    if (_data->statusAction)
    {
        _data->statusAction->setChecked(statusVisible);
    }
    toolbar().toolbar()->setVisible(toolbarVisible);
    _data->window->statusBar()->setVisible(statusVisible);
}

void App::_updateRecent()
{
    const auto workspaces = recentWorkspaces();
    const auto sessions   = _data->workspace ? _data->workspace->recentSessions() : QStringList {};
    for (std::size_t index = 0; index < _data->recentWorkspaces.size(); ++index)
    {
        auto*      action  = _data->recentWorkspaces[index].data();
        const bool present = index < static_cast<std::size_t>(workspaces.size());
        action->setVisible(present);
        if (present)
        {
            const auto path = workspaces.at(index);
            action->setText(QString("&%1 %2").arg((index + 1) % 10).arg(Path(path).name().replace('&', "&&")));
            action->setToolTip(path);
            action->setData(path);
        }
        auto*      session    = _data->recentSessions[index].data();
        const bool hasSession = index < static_cast<std::size_t>(sessions.size());
        session->setVisible(hasSession);
        session->setEnabled(hasSession);
        if (hasSession)
        {
            session->setData(sessions.at(index));
        }
        session->setToolTip(tr("Open this session configuration"));
        if (hasSession)
        {
            const auto id        = sessions.at(index);
            QString    label     = id;
            const auto directory = _data->workspace->sessionDirectory(id);
            if (_data->session && _data->session->id() == id)
            {
                label = _data->session->name();
            }
            else if (directory)
            {
                Session saved(*directory, _data->workspace->settings());
                if (saved.load() && saved.id() == id)
                {
                    label = saved.name();
                }
            }
            session->setText(QString("&%1 %2").arg((index + 1) % 10).arg(label.replace('&', "&&")));
            session->setToolTip(directory ? directory->join("session.yaml").toString() : id);
        }
    }
    static_cast<MenuActionContainer*>(mainMenu().findMenu("/file/recentWorkspaces"))
        ->menu()
        ->setEnabled(!workspaces.isEmpty());
    static_cast<MenuActionContainer*>(mainMenu().findMenu("/session/recentSessions"))
        ->menu()
        ->setEnabled(!sessions.isEmpty());
}

void App::_chooseWorkspace()
{
    const QString file = QFileDialog::
        getOpenFileName(_data->window.get(),
                        tr("Open workspace"),
                        _data->workspace ? _data->workspace->filePath().parent().toString() : Path::home().toString(),
                        tr("Termaster workspaces (*.termaster);;YAML files (*.yaml *.yml);;All files (*)"));
    if (file.isEmpty())
    {
        return;
    }
    QString error;
    if (!openWorkspace(Path(file), &error))
    {
        _showError(error);
    }
}

void App::_chooseNewWorkspace()
{
    QFileDialog dialog(_data->window.get(), tr("Create workspace"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(tr("Termaster workspaces (*.termaster)"));
    dialog.setDefaultSuffix("termaster");
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    dialog.setDirectory(_data->workspace ? _data->workspace->filePath().parent().toString() : Path::home().toString());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const QString file = dialog.selectedFiles().value(0);
    if (file.isEmpty())
    {
        return;
    }
    const Path path(file);
    QString    error;
    if (!createWorkspace(path, path.stem(), &error))
    {
        _showError(error);
    }
}

void App::_showError(const QString& error)
{
    _data->window->statusBar()->showMessage(error);
    QMessageBox::warning(_data->window.get(), tr("Termaster"), error);
}

void App::_storeViewSetting(const QString& key, bool value)
{
    if (_data->applyingConfiguration || !_data->settingsLoaded)
    {
        return;
    }
    QString error;
    if (!_data->applicationSettings().setValue(key, value, &error))
    {
        _data->window->statusBar()->showMessage(error);
    }
}

QAction* App::_addAction(const QString&      id,
                         const QString&      text,
                         const QIcon&        icon,
                         const QKeySequence& shortcut,
                         const QString&      menu,
                         const QString&      group)
{
    auto* command = commands().create(id, text, icon, shortcut);
    auto* action  = command->newAction(_data->window.get());
    mainMenu().findMenu('/' + menu)->addAction(action, group);
    return action;
}

Session* App::session() const
{
    return _data->session.get();
}

bool App::createSession(const QString& requestedName, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_data->workspace)
    {
        if (error)
        {
            *error = tr("No workspace is open.");
        }
        return false;
    }
    if (_data->session && !_data->session->sync(error))
    {
        return false;
    }
    const auto root = _data->workspace->sessionsDirectory();
    if (!root.createDirectories(error))
    {
        return false;
    }
    const auto id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto directory = root.join(id);
    const auto name      = requestedName.isEmpty() ? RandomNames::generate().toString() : requestedName;
    auto       candidate = std::make_unique<Session>(directory, _data->workspace->settings());
    if (!candidate->create(id, name, error))
    {
        return false;
    }
    if (!_data->workspace->registerSession(id, directory, error))
    {
        QFile::remove(candidate->filePath().toString());
        QDir().rmdir(directory.toString());
        return false;
    }
    if (_activateSession(std::move(candidate), error))
    {
        return true;
    }
    _data->workspace->unregisterSession(id);
    QFile::remove(directory.join("session.yaml").toString());
    QDir().rmdir(directory.toString());
    return false;
}

bool App::openSession(const QString& id, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_data->workspace)
    {
        if (error)
        {
            *error = tr("No workspace is open.");
        }
        return false;
    }
    const auto directory = _data->workspace->sessionDirectory(id, error);
    if (!directory)
    {
        return false;
    }
    if (_data->session && _data->session->id() == id)
    {
        return true;
    }
    auto candidate = std::make_unique<Session>(*directory, _data->workspace->settings());
    if (!candidate->load(error))
    {
        return false;
    }
    if (candidate->id() != id)
    {
        if (error)
        {
            *error = tr("Session ID does not match its workspace catalog entry.");
        }
        return false;
    }
    return _activateSession(std::move(candidate), error);
}

bool App::saveSession(QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_data->session)
    {
        if (error)
        {
            *error = tr("No session is open.");
        }
        return false;
    }
    if (!_data->session->sync(error))
    {
        return false;
    }
    _updateSession();
    return true;
}

bool App::renameSession(const QString& name, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!_data->session)
    {
        if (error)
        {
            *error = tr("No session is open.");
        }
        return false;
    }
    const auto previous = _data->session->name();
    if (!_data->session->setName(name, error))
    {
        return false;
    }
    if (!saveSession(error))
    {
        _data->session->setName(previous);
        return false;
    }
    emit sessionChanged();
    return true;
}

bool App::closeSession(QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (_data->session && !_data->session->sync(error))
    {
        return false;
    }
    if (_data->workspace)
    {
        auto       state    = _data->workspace->state();
        const bool hadLast  = state.contains("lastSession");
        const auto previous = state.value("lastSession");
        if (hadLast && (!state.remove("lastSession", error) || !_data->workspace->sync(error)))
        {
            state.setValue("lastSession", previous);
            return false;
        }
    }
    if (!_data->session)
    {
        return true;
    }
    _data->terminals->setSession(nullptr);
    _closeSettings(SettingsContext::Session);
    _closeSettings(SettingsContext::Terminal);
    _data->session.reset();
    _updateSession();
    emit sessionChanged();
    return true;
}

void App::_restoreLastSession()
{
    if (!_data->workspace || _data->session)
    {
        return;
    }
    const auto state = _data->workspace->state();
    if (!state.contains("lastSession"))
    {
        return;
    }
    const auto value = state.value("lastSession");
    QString    error;
    if (value.typeId() != QMetaType::QString || value.toString().isEmpty() || value.toString().size() > 128
        || value.toString().contains(QChar::Null))
    {
        error = tr("Last session must be a nonempty catalog ID of at most 128 characters.");
    }
    else if (openSession(value.toString(), &error))
    {
        return;
    }
    _data->window->statusBar()->showMessage(tr("Cannot restore last session: %1").arg(error));
}

bool App::_activateSession(std::unique_ptr<Session> candidate, QString* error)
{
    if (_data->session && !_data->session->sync(error))
    {
        return false;
    }
    auto       state        = _data->workspace->state();
    const bool hadRecent    = state.contains("recentSessions");
    const auto previous     = state.value("recentSessions");
    const bool hadLast      = state.contains("lastSession");
    const auto previousLast = state.value("lastSession");
    if (!_data->workspace->rememberSession(candidate->id(), error))
    {
        return false;
    }
    if (!state.setValue("lastSession", candidate->id(), error) || !_data->workspace->sync(error))
    {
        if (hadLast)
        {
            state.setValue("lastSession", previousLast);
        }
        else
        {
            state.remove("lastSession");
        }
        if (hadRecent)
        {
            state.setValue("recentSessions", previous);
        }
        else
        {
            state.remove("recentSessions");
        }
        return false;
    }
    _data->terminals->setSession(nullptr);
    _closeSettings(SettingsContext::Session);
    _data->session = std::move(candidate);
    _data->terminals->setSession(_data->session.get());
    _updateSession();
    emit sessionChanged();
    return true;
}

void App::_updateSession()
{
    const bool workspace = bool(_data->workspace);
    const bool session   = bool(_data->session);
    _data->createSessionAction->setEnabled(workspace);
    _data->openSessionAction->setEnabled(workspace);
    for (auto* action : {_data->saveSessionAction.data(),
                         _data->closeSessionAction.data(),
                         _data->renameSessionAction.data(),
                         _data->sessionSettingsAction.data()})
    {
        action->setEnabled(session);
    }
    _data->window->setSession(session ? _data->session->name() : QString {},
                              session ? _data->session->filePath().toString() : QString {},
                              session ? _data->session->objects().values().size() : 0,
                              session ? _data->session->links().values().size() : 0);
    _applyConfiguration();
    _data->terminals->applySettings();
    _updateRecent();
}

void App::_chooseNewSession()
{
    if (!_data->workspace)
    {
        return;
    }
    bool       accepted = false;
    const auto name     = QInputDialog::getText(_data->window.get(),
                                                tr("Create session"),
                                                tr("Name:"),
                                                QLineEdit::Normal,
                                                RandomNames::generate().toString(),
                                                &accepted);
    if (!accepted)
    {
        return;
    }
    QString error;
    if (!createSession(name, &error))
    {
        _showError(error);
    }
}

void App::_chooseSession()
{
    if (!_data->workspace)
    {
        return;
    }
    const auto ids = _data->workspace->sessionIds();
    if (ids.isEmpty())
    {
        _data->window->statusBar()->showMessage(tr("This workspace has no saved sessions."));
        return;
    }
    QList<SessionPicker::Entry> entries;
    for (const auto& id : ids)
    {
        QString name = tr("Unavailable session");
        if (const auto directory = _data->workspace->sessionDirectory(id))
        {
            Session candidate(*directory, _data->workspace->settings());
            QString error;
            if (candidate.load(&error))
            {
                name = candidate.name();
            }
        }
        entries.append({name, id});
    }
    SessionPicker picker(entries, _data->window.get());
    if (picker.exec() != QDialog::Accepted)
    {
        return;
    }

    QString error;
    if (!openSession(picker.selectedSession(), &error))
    {
        _showError(error);
    }
}

void App::_showSettings(SettingsContext context)
{
    if ((context == SettingsContext::Workspace && !_data->workspace)
        || (context == SettingsContext::Session && !_data->session)
        || (context == SettingsContext::Terminal && _data->terminals->selectedTerminal().isEmpty()))
    {
        return;
    }
    _data->settingsWindow = _data->settingsWindows.value(context);
    if (!_data->settingsWindow)
    {
        auto* workspace = _data->workspace.get();
        auto* session   = _data->session.get();
        auto* dialog    = new SettingsDialog(
            settingsWidgets(),
            [this, context](QString* error) {
                if (context == SettingsContext::Application)
                {
                    const auto assignments = _data->settings.value("settings/shortcuts", QVariantMap {}).toMap();
                    const auto validation  = _data->shortcuts.validateAssignments(assignments);
                    if (!validation.isEmpty())
                    {
                        if (error)
                        {
                            *error = validation;
                        }
                        return false;
                    }
                    if (!_data->settings.sync(error))
                    {
                        return false;
                    }
                    return _data->shortcuts.setAssignments(assignments, error);
                }
                if (context == SettingsContext::Workspace)
                {
                    return _data->workspace && _data->workspace->sync(error);
                }
                return _data->session && _data->session->sync(error);
            },
            _data->window.get());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setProperty("settingsContext", int(context));
        _data->settingsWindow = dialog;
        _data->settingsWindows.insert(context, dialog);
        const auto schema = [](const QString& resource) {
            QFile file(":/Termaster/App/" + resource);
            if (!file.open(QIODevice::ReadOnly))
            {
                throw std::runtime_error(file.errorString().toStdString());
            }
            return std::make_shared<SettingsSchema>(file.readAll());
        };
        if (context == SettingsContext::Terminal)
        {
            dialog->setWindowTitle(tr("Terminal settings"));
            dialog->setProperty("terminalId", _data->terminals->selectedTerminal());
            _data->terminals->addSettingsPages(*dialog);
            dialog->selectPage("terminal.connection");
        }
        else
        {
            const auto scope  = context == SettingsContext::Application ? QString("application") :
                                context == SettingsContext::Workspace   ? QString("workspace") :
                                                                          QString("session");
            auto       target = context == SettingsContext::Application ? _data->applicationSettings() :
                                context == SettingsContext::Workspace   ? workspace->settings() :
                                                                          session->settings();
            dialog->setWindowTitle(context == SettingsContext::Application ? tr("Application settings") :
                                   context == SettingsContext::Workspace   ? tr("Workspace settings") :
                                                                             tr("Session settings"));
            if (context == SettingsContext::Application)
            {
                dialog->addPage("application.shortcuts",
                                "Application",
                                "Keyboard shortcuts",
                                "Global command shortcuts. Changes take effect after Apply.",
                                schema("Shortcuts.yaml"),
                                _data->settings.node("settings"));
                dialog->addPage("application.appearance",
                                "Application",
                                "Interface",
                                "Application interface options.",
                                schema("SettingsSchema.yaml"),
                                target);
            }
            dialog->addPage(scope + ".terminal",
                            "Terminals",
                            "Text and screen",
                            "Default terminal appearance and input settings.",
                            schema("TerminalAppearance.yaml"),
                            target);
            _data->terminals->addDefaultSettingsPages(*dialog, scope, target);
            dialog->selectPage(context == SettingsContext::Application ? "application.appearance" :
                                                                         scope + ".terminal");
        }
        connect(dialog, &SettingsDialog::applied, this, &App::_applyConfiguration);
        connect(dialog, &SettingsDialog::applied, this, [this, dialog] {
            for (const auto& other : _data->settingsWindows)
            {
                if (other && other != dialog)
                {
                    other->refreshInheritedValues();
                }
            }
        });
        connect(dialog, &SettingsDialog::applied, _data->terminals.get(), &TerminalController::applySettings);
        const auto closeOnScopeChange = [this, dialog, workspace, session, context] {
            if ((context != SettingsContext::Application && _data->workspace.get() != workspace)
                || ((context == SettingsContext::Session || context == SettingsContext::Terminal)
                    && _data->session.get() != session))
            {
                delete dialog;
            }
        };
        connect(this, &App::workspaceChanged, dialog, closeOnScopeChange);
        connect(this, &App::sessionChanged, dialog, closeOnScopeChange);
    }
    _data->settingsWindow->show();
    _data->settingsWindow->raise();
    _data->settingsWindow->activateWindow();
}

PluginHost& App::pluginHost()
{
    return *_data->terminals;
}
PluginManager& App::plugins()
{
    return _data->plugins;
}
bool App::createTerminal(const QString& type, const QVariantMap& settings, const QString& name, QString* error)
{
    return _data->terminals->createTerminal(type, settings, name, error);
}
QStringList App::terminalIds() const
{
    return _data->terminals->terminalIds();
}
QString App::selectedTerminal() const
{
    return _data->terminals->selectedTerminal();
}

void App::_closeSettings(SettingsContext context)
{
    delete _data->settingsWindows.value(context).data();
    _data->settingsWindows.remove(context);
}
