#pragma once

#include <QApplication>
#include <QStringList>
#include <QVariant>
#include <memory>

class ApplicationPaths;
class CommandRegistry;
class MenubarActionContainer;
class ToolbarActionContainer;
class Settings;
class PluginHost;
class PluginManager;
class SettingsWidgetRegistry;
class QMainWindow;
class Path;
class Workspace;
class Session;
class SettingsOverlay;
class QAction;
class QIcon;
class QKeySequence;

/** Owns application services and windows. Construct once and use on the GUI thread. */
class App final : public QApplication
{
    Q_OBJECT

public:
    struct Options
    {
        QString dataDirectory;
        QString pluginsDirectory;
    };

    App(int& argc, char** argv);
    App(int& argc, char** argv, const Options& options);
    ~App() override;
    Q_DISABLE_COPY_MOVE(App)

    int run();

    const ApplicationPaths& paths() const;
    Settings&               settings();
    CommandRegistry&        commands();
    SettingsWidgetRegistry& settingsWidgets();
    PluginHost&             pluginHost();
    PluginManager&          plugins();
    bool                    createTerminal(const QString&     type,
                                           const QVariantMap& settings,
                                           const QString&     name  = {},
                                           QString*           error = nullptr);
    QStringList             terminalIds() const;
    QString                 selectedTerminal() const;
    QMainWindow&            mainWindow();
    MenubarActionContainer& mainMenu();
    ToolbarActionContainer& toolbar();
    Workspace*              workspace() const;
    QStringList             recentWorkspaces() const;
    SettingsOverlay         configuration();
    bool                    restoreLastWorkspace(QString* error = nullptr);
    bool                    openWorkspace(const Path& file, QString* error = nullptr);
    bool                    createWorkspace(const Path& file, const QString& name, QString* error = nullptr);
    /** Persists workspace/session state without reapplying runtime configuration. */
    bool          saveWorkspace(QString* error = nullptr);
    bool          closeWorkspace(QString* error = nullptr);
    Q_SIGNAL void workspaceChanged();

    Session* session() const;
    bool     createSession(const QString& name, QString* error = nullptr);
    bool     openSession(const QString& id, QString* error = nullptr);
    /** Persists session state without reapplying runtime configuration. */
    bool          saveSession(QString* error = nullptr);
    bool          renameSession(const QString& name, QString* error = nullptr);
    bool          closeSession(QString* error = nullptr);
    Q_SIGNAL void sessionChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class SettingsContext
    {
        Application,
        Workspace,
        Session,
        Terminal
    };
    struct Data;
    std::unique_ptr<Data> _data;

    void     _setupActions();
    void     _showPluginManager();
    void     _showLineflowEditor();
    void     _showSettings(SettingsContext context = SettingsContext::Application);
    void     _closeSettings(SettingsContext context);
    bool     _ensureApplicationSettings(QString* error);
    bool     _rememberWorkspace(const Path& file, QString* error);
    bool     _activateWorkspace(std::unique_ptr<Workspace> workspace, QString* error);
    bool     _saveChanges(QString* error);
    void     _updateWorkspace();
    bool     _prepareWorkspaceProcessors(Workspace& workspace, QString* error);
    void     _bindWorkspaceProcessors();
    void     _applyConfiguration();
    void     _updateRecent();
    void     _updateSessionPresentation();
    bool     _activateSession(std::unique_ptr<Session> candidate, QString* error);
    void     _updateSession();
    void     _restoreLastSession();
    void     _chooseNewSession();
    void     _chooseSession();
    void     _chooseWorkspace();
    void     _chooseNewWorkspace();
    void     _showError(const QString& error);
    void     _storeViewSetting(const QString& key, bool value);
    QAction* _addAction(const QString&      id,
                        const QString&      text,
                        const QIcon&        icon,
                        const QKeySequence& shortcut,
                        const QString&      menu,
                        const QString&      group);
};
