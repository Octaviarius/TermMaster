#include "Gui/Main/EditActions.h"
#include "Gui/Main/MainWindow.h"
#include "Gui/Settings/SettingsDialog.h"
#include "Gui/Settings/SettingsPreview.h"
#include "Gui/Terminal/TerminalController.h"

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
#include <QMessageBox>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QUuid>
#include <Termaster/App/App.h>
#include <Termaster/Core/ApplicationPaths.h>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Plugin/PluginManagerGui.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Plugin/PluginManager.h>
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
    ApplicationPaths                    paths;
    Settings                            defaults {QString {}};
    Settings                            settings;
    Settings                            pluginSettings;
    std::unique_ptr<Workspace>          workspace;
    std::unique_ptr<Session>            session;
    CommandRegistry                     commands;
    SettingsWidgetRegistry              settingsWidgets;
    std::unique_ptr<MainWindow>         window;
    std::unique_ptr<TerminalController> terminals;
    std::unique_ptr<EditActions>        editing;
    QPointer<QDialog>                   pluginWindow;
    QPointer<SettingsDialog>            settingsWindow;
    QPointer<QAction>                   createSessionAction;
    QPointer<QAction>                   openSessionAction;
    QPointer<QAction>                   saveSessionAction;
    QPointer<QAction>                   closeSessionAction;
    QPointer<QAction>                   renameSessionAction;
    QPointer<QAction>                   saveAction;
    QPointer<QAction>                   closeAction;
    QPointer<QAction>                   toolbarAction;
    QPointer<QAction>                   statusAction;
    QPointer<QAction>                   fullScreenAction;
    std::array<QPointer<QAction>, 10>   recentWorkspaces;
    std::array<QPointer<QAction>, 10>   recentSessions;
    bool                                settingsLoaded        = false;
    bool                                applyingConfiguration = false;
    // Shutdown may still use commands, views, workspace and settings.
    PluginManager plugins;

    explicit Data(const Options& options) :
        paths(_paths(options)),
        settings(paths.configFile("application"), {"termaster.application", "Termaster application settings"}),
        pluginSettings(paths.configFile("plugins"), {"termaster.plugins", "Plugin activation preferences"}),
        window(std::make_unique<MainWindow>()), plugins(pluginSettings, paths.pluginsDirectory().toString())
    {
        registerSettingsPreviewEditors(settingsWidgets);
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
    Q_INIT_RESOURCE(appSettingsSchema);
    setWindowIcon(QIcon(":/Termaster/App/IconOrange.png"));

    _data = std::make_unique<Data>(options);
    _setupActions();
    _data->terminals = std::make_unique<TerminalController>(
        *_data->window,
        commands(),
        settingsWidgets(),
        [this] { delete _data->settingsWindow.data(); },
        [this] {
            _showSettings();
            if (_data->settingsWindow)
            {
                _data->settingsWindow->selectPage("terminal.connection");
            }
        },
        this);
    _data->plugins.setHost(_data->terminals.get());
    connect(_data->terminals.get(), &TerminalController::selectionChanged, this, [this] {
        delete _data->settingsWindow.data();
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
    if (!restoreLastWorkspace(&error))
    {
        _data->window->showWorkspaceChoice(error);
        _data->window->statusBar()->showMessage(error);
    }
    _applyConfiguration();
    _updateRecent();
    _data->window->show();
    if (!_data->plugins.start(&error))
    {
        _data->window->statusBar()->showMessage(error);
    }
    for (const auto& plugin : _data->plugins.plugins())
    {
        if (plugin.info.id == "SerialTerminal" && !_data->pluginSettings.contains("enabled/SerialTerminal"))
        {
            if (!_data->plugins.setEnabled("SerialTerminal", true, &error))
            {
                _data->window->statusBar()->showMessage(error);
            }
        }
    }
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
        auto candidate = std::make_unique<Workspace>(file, _data->applicationSettings());
        if (!candidate->load(error))
        {
            return false;
        }
        if (_data->workspace && !saveWorkspace(error))
        {
            return false;
        }
        const bool hadSession = bool(_data->session);
        _data->terminals->setSession(nullptr);
        _data->session.reset();
        _data->workspace = std::move(candidate);
        _updateWorkspace();
        if (hadSession)
        {
            emit sessionChanged();
        }
        emit workspaceChanged();
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
    _data->session.reset();
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
    const auto icon = [this](const QString& name, QStyle::StandardPixmap fallback) {
        return QIcon::fromTheme(name, style()->standardIcon(fallback));
    };
    auto* create = _addAction("Workspace.New",
                              tr("&New workspace…"),
                              icon("document-new", QStyle::SP_FileIcon),
                              QKeySequence::New,
                              "file",
                              "workspace",
                              "application");
    create->setToolTip(tr("Create a workspace"));
    connect(create, &QAction::triggered, this, &App::_chooseNewWorkspace);
    auto* open = _addAction("Workspace.Open",
                            tr("&Open workspace…"),
                            icon("document-open", QStyle::SP_DirOpenIcon),
                            QKeySequence::Open,
                            "file",
                            "workspace",
                            "application");
    open->setToolTip(tr("Open a workspace"));
    connect(open, &QAction::triggered, this, &App::_chooseWorkspace);
    _data->saveAction = _addAction("Workspace.Save",
                                   tr("&Save workspace"),
                                   icon("document-save", QStyle::SP_DialogSaveButton),
                                   QKeySequence::Save,
                                   "file",
                                   "workspace",
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
                                    icon("document-close", QStyle::SP_DialogCloseButton),
                                    QKeySequence::Close,
                                    "file",
                                    "workspace",
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
                                                  "file/recentSessions",
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
                                            icon("document-new", QStyle::SP_FileIcon),
                                            {},
                                            "file",
                                            "session",
                                            "session");
    _data->createSessionAction->setToolTip(tr("Create a session"));
    connect(_data->createSessionAction, &QAction::triggered, this, &App::_chooseNewSession);
    _data->openSessionAction = _addAction("Session.Open",
                                          tr("Open session…"),
                                          icon("document-open", QStyle::SP_DirOpenIcon),
                                          {},
                                          "file",
                                          "session",
                                          "session");
    _data->openSessionAction->setToolTip(tr("Open a session from this workspace"));
    connect(_data->openSessionAction, &QAction::triggered, this, &App::_chooseSession);
    _data->saveSessionAction = _addAction("Session.Save",
                                          tr("Save session"),
                                          icon("document-save", QStyle::SP_DialogSaveButton),
                                          {},
                                          "file",
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
                                           icon("document-close", QStyle::SP_DialogCloseButton),
                                           {},
                                           "file",
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
    _data->renameSessionAction = _addAction("Session.Rename", tr("Rename session…"), {}, {}, "file", "session");
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
    for (const auto& path : {"/file/recentWorkspaces", "/file/recentSessions"})
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
    _data->toolbarAction = _addAction("View.Toolbar", tr("&Toolbar"), {}, {}, "view", "panels");
    _data->statusAction  = _addAction("View.StatusBar", tr("&Status bar"), {}, {}, "view", "panels");
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
                                   icon("preferences-system", QStyle::SP_FileDialogDetailedView),
                                   QKeySequence::Preferences,
                                   "tools",
                                   "management");
    preferences->setMenuRole(QAction::PreferencesRole);
    connect(preferences, &QAction::triggered, this, &App::_showSettings);
    auto* plugins = _addAction("Application.Plugins",
                               tr("&Plugins…"),
                               icon("application-x-addon", QStyle::SP_FileDialogDetailedView),
                               {},
                               "tools",
                               "management",
                               "application");
    plugins->setToolTip(tr("Manage plugins"));
    connect(plugins, &QAction::triggered, this, &App::_showPluginManager);
    auto* about = _addAction("Help.About", tr("&About Termaster"), windowIcon(), {}, "help", "about");
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(_data->window.get(), tr("About Termaster"), tr("Termaster %1").arg(Version::shortVersion));
    });
    auto* aboutQt = _addAction("Help.AboutQt", tr("About &Qt"), {}, {}, "help", "about");
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, this, [this]() { QMessageBox::aboutQt(_data->window.get()); });
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
    _data->session.reset();
    _data->workspace = std::move(candidate);
    _updateWorkspace();
    if (hadSession)
    {
        emit sessionChanged();
    }
    emit workspaceChanged();
    return true;
}

bool App::_saveChanges(QString* error)
{
    if (_data->workspace && !saveWorkspace(error))
    {
        return false;
    }
    return !_data->settings.isDirty() || _data->settings.sync(error);
}

void App::_updateWorkspace()
{
    const bool open = bool(_data->workspace);
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
    const bool                 toolbarVisible = configuration().value("ui/toolbarVisible", true).toBool();
    const bool                 statusVisible  = configuration().value("ui/statusBarVisible", true).toBool();
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
    static_cast<MenuActionContainer*>(mainMenu().findMenu("/file/recentSessions"))
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
    if (!configuration().setValue(key, value, &error))
    {
        _data->window->statusBar()->showMessage(error);
    }
}

QAction* App::_addAction(const QString&      id,
                         const QString&      text,
                         const QIcon&        icon,
                         const QKeySequence& shortcut,
                         const QString&      menu,
                         const QString&      group,
                         const QString&      toolbarGroup)
{
    auto* command = commands().create(id, text, icon, shortcut);
    auto* action  = command->newAction(_data->window.get());
    mainMenu().findMenu('/' + menu)->addAction(action, group);
    if (!toolbarGroup.isEmpty())
    {
        toolbar().addAction(action, toolbarGroup);
    }
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
    if (!_data->session)
    {
        return true;
    }
    if (!_data->session->sync(error))
    {
        return false;
    }
    _data->terminals->setSession(nullptr);
    _data->session.reset();
    _updateSession();
    emit sessionChanged();
    return true;
}

bool App::_activateSession(std::unique_ptr<Session> candidate, QString* error)
{
    if (_data->session && !_data->session->sync(error))
    {
        return false;
    }
    auto       state     = _data->workspace->state();
    const bool hadRecent = state.contains("recentSessions");
    const auto previous  = state.value("recentSessions");
    if (!_data->workspace->rememberSession(candidate->id(), error))
    {
        return false;
    }
    if (!_data->workspace->sync(error))
    {
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
    for (auto* action :
         {_data->saveSessionAction.data(), _data->closeSessionAction.data(), _data->renameSessionAction.data()})
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
    QStringList labels;
    for (const auto& id : ids)
    {
        const auto directory = _data->workspace->sessionDirectory(id);
        if (!directory)
        {
            labels.append(tr("Unavailable session") + " (" + id + ')');
            continue;
        }
        Session    candidate(*directory, _data->workspace->settings());
        QString    error;
        const auto label = candidate.load(&error) ? candidate.name() : tr("Unavailable session");
        labels.append(label + " (" + id + ')');
    }
    bool       accepted = false;
    const auto choice   = QInputDialog::getItem(_data->window.get(),
                                                tr("Open session"),
                                                tr("Session:"),
                                                labels,
                                                0,
                                                false,
                                                &accepted);
    if (!accepted)
    {
        return;
    }
    QString error;
    if (!openSession(ids.at(labels.indexOf(choice)), &error))
    {
        _showError(error);
    }
}

void App::_showSettings()
{
    if (!_data->settingsWindow)
    {
        QString error;
        if (!_ensureApplicationSettings(&error))
        {
            _showError(error);
            return;
        }
        QFile file(":/Termaster/App/SettingsSchema.yaml");
        if (!file.open(QIODevice::ReadOnly))
        {
            _showError(file.errorString());
            return;
        }
        auto  schema = std::make_shared<SettingsSchema>(file.readAll());
        auto* dialog = new SettingsDialog(
            settingsWidgets(),
            [this](QString* error) { return _saveChanges(error); },
            _data->window.get());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        _data->settingsWindow = dialog;
        dialog->addPage("application.appearance",
                        tr("Application"),
                        tr("Appearance"),
                        tr("Global display defaults for all workspaces."),
                        schema,
                        _data->settings.node("settings"));
        if (_data->workspace)
        {
            dialog->addPage("workspace.appearance",
                            tr("Workspace"),
                            tr("Appearance"),
                            tr("Display overrides for the current workspace."),
                            schema,
                            _data->workspace->settings());
        }
        if (_data->session)
        {
            dialog->addPage("session.appearance",
                            tr("Session"),
                            tr("Appearance"),
                            tr("Display overrides for the current session."),
                            schema,
                            _data->session->settings());
        }
        QFile appearanceFile(":/Termaster/App/TerminalAppearance.yaml");
        if (!appearanceFile.open(QIODevice::ReadOnly))
        {
            throw std::runtime_error("Terminal appearance schema is unavailable");
        }
        auto appearance = std::make_shared<SettingsSchema>(appearanceFile.readAll());
        dialog->addPage("application.terminal",
                        tr("Application"),
                        tr("Terminal defaults"),
                        tr("Default display and input settings for all terminals."),
                        appearance,
                        _data->applicationSettings());
        if (_data->workspace)
        {
            dialog->addPage("workspace.terminal",
                            tr("Workspace"),
                            tr("Terminal defaults"),
                            tr("Terminal defaults for this workspace."),
                            appearance,
                            _data->workspace->settings());
        }
        if (_data->session)
        {
            dialog->addPage("session.terminal",
                            tr("Session"),
                            tr("Terminal defaults"),
                            tr("Terminal defaults for this session."),
                            appearance,
                            _data->session->settings());
        }
        _data->terminals->addSettingsPages(*dialog);
        addSettingsPreviewPages(*dialog);
        dialog->selectPage(_data->session   ? "session.appearance" :
                           _data->workspace ? "workspace.appearance" :
                                              "application.appearance");
        connect(dialog, &SettingsDialog::applied, this, &App::_applyConfiguration);
        connect(dialog, &SettingsDialog::applied, _data->terminals.get(), &TerminalController::applySettings);
        const auto* workspace          = _data->workspace.get();
        const auto* session            = _data->session.get();
        const auto  closeOnScopeChange = [this, dialog, workspace, session] {
            if (_data->workspace.get() != workspace || _data->session.get() != session)
            {
                dialog->reject();
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
