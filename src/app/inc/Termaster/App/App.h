#pragma once

#include <QApplication>
#include <QStringList>
#include <memory>

class ApplicationPaths;
class CommandRegistry;
class MenubarActionContainer;
class ToolbarActionContainer;
class Settings;
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
    QMainWindow&            mainWindow();
    MenubarActionContainer& mainMenu();
    ToolbarActionContainer& toolbar();
    Workspace*              workspace() const;
    QStringList             recentWorkspaces() const;
    SettingsOverlay         configuration();
    bool                    restoreLastWorkspace(QString* error = nullptr);
    bool                    openWorkspace(const Path& file, QString* error = nullptr);
    bool                    createWorkspace(const Path& file, const QString& name, QString* error = nullptr);
    bool                    saveWorkspace(QString* error = nullptr);
    bool                    closeWorkspace(QString* error = nullptr);
    Q_SIGNAL void           workspaceChanged();

    Session*      session() const;
    bool          createSession(const QString& name, QString* error = nullptr);
    bool          openSession(const QString& id, QString* error = nullptr);
    bool          saveSession(QString* error = nullptr);
    bool          renameSession(const QString& name, QString* error = nullptr);
    bool          closeSession(QString* error = nullptr);
    Q_SIGNAL void sessionChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Data;
    std::unique_ptr<Data> _data;

    void     _setupActions();
    void     _showPluginManager();
    bool     _ensureApplicationSettings(QString* error);
    bool     _rememberWorkspace(const Path& file, QString* error);
    bool     _activateWorkspace(std::unique_ptr<Workspace> workspace, QString* error);
    bool     _saveChanges(QString* error);
    void     _updateWorkspace();
    void     _applyConfiguration();
    void     _updateRecent();
    bool     _activateSession(std::unique_ptr<Session> candidate, QString* error);
    void     _updateSession();
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
                        const QString&      group,
                        const QString&      toolbarGroup = {});
};
