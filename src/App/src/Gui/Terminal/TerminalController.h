#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <Termaster/Core/Plugin/PluginHost.h>
#include <Termaster/Core/Processing/Processor.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <array>
#include <map>
#include <optional>

class MainWindow;
class ActionContainer;
class QMainWindow;
class DetachedTerminalWindow;
class ProcessorRegistry;
class ProcessorFavorites;
class CommandRegistry;
class SettingsWidgetRegistry;
class SettingsSchema;
class Session;
class Workspace;
class SettingsDialog;
class CommandEditor;
class TerminalDisplay;
class QAction;
class QToolButton;
class QTabWidget;
class TerminalTabGroup;
class TerminalWorkspace;

/** GUI-thread runtime owner. Session definitions survive closed views and module unload. */
class TerminalController final : public QObject, public PluginHost
{
    Q_OBJECT
public:
    TerminalController(MainWindow&                 window,
                       CommandRegistry&            commands,
                       SettingsWidgetRegistry&     editors,
                       std::function<Workspace*()> workspace,
                       std::function<void()>       closeSettings,
                       std::function<void()>       showSettings,
                       QObject*                    parent = nullptr);
    ~TerminalController() override;
    Q_DISABLE_COPY_MOVE(TerminalController)

    void                    setProcessorRegistry(ProcessorRegistry& registry);
    void                    setProcessorFavorites(ProcessorFavorites& favorites);
    void                    setSession(Session* session);
    void                    closeAll();
    TerminalTabGroup*       terminalGroup(const QString& id) const;
    bool                    moveTerminal(const QString&    id,
                                         TerminalTabGroup* target,
                                         Qt::Edge          edge  = Qt::Edge(0),
                                         int               index = -1,
                                         QString*          error = nullptr);
    bool                    moveGroup(TerminalTabGroup* source,
                                      TerminalTabGroup* destination,
                                      Qt::Edge          edge  = Qt::Edge(0),
                                      QString*          error = nullptr);
    bool                    detachGroup(TerminalTabGroup* group, QString* error = nullptr);
    bool                    returnWindow(DetachedTerminalWindow* window, QString* error = nullptr);
    bool                    detachTerminal(const QString& id, QString* error = nullptr);
    bool                    attachTerminal(const QString& id, QString* error = nullptr);
    DetachedTerminalWindow* detachedWindow(const QString& id) const;
    QWidget*                selectedWindow() const;
    bool                    capturePresentation(QString* error = nullptr);
    bool                    createTerminal(const QString&     type,
                                           const QVariantMap& overrides,
                                           const QString&     name,
                                           QString*           error = nullptr);
    QString                 selectedTerminal() const;
    QStringList             terminalIds() const;
    SettingsOverlay         selectedSettings();
    void                    addSettingsPages(SettingsDialog& dialog);
    bool                    saveSettings(SettingsDialog& dialog, QString* error);
    void addDefaultSettingsPages(SettingsDialog& dialog, const QString& scope, SettingsOverlay settings);
    void applySettings();

    QVariantMap   workspaceSettings() const override;
    void          showMessage(const QString& message) override;
    QObject*      context() override;
    void          registerTerminalType(TerminalType type) override;
    bool          unregisterTerminalType(const QString& type, QString* error = nullptr) override;
    void          registerAction(Action action) override;
    void          unregisterAction(const QString& id) override;
    void          registerEditor(const QString& id, EditorFactory factory) override;
    void          unregisterEditor(const QString& id) override;
    void          showCreateTerminal(const QString& type) override;
    SerialDevice* selectedDevice(const QString& type) override;
    bool          canCreateTerminal() const override;
    bool          connectionRequested(const QString& type) const override;
    bool          hasSelectedTerminal(const QString& type) const override;
    void          connectSelectedTerminal() override;
    void          disconnectSelectedTerminal() override;
    void          closeSelectedTerminal() override;
    void          clearSelectedTerminal() override;
    void          showSelectedTerminalSettings() override;
    void          refreshTerminalActions() override;

    Q_SIGNAL void selectionChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Type
    {
        TerminalType                    registration;
        std::shared_ptr<SettingsSchema> schema;
        std::shared_ptr<SettingsSchema> workspaceSchema;
        std::shared_ptr<SettingsSchema> defaultSchema;
    };
    struct Runtime;
    struct ConnectionDraft
    {
        std::shared_ptr<Settings> values;
        QVariantMap               baseline;
    };
    struct RegisteredAction
    {
        Action            registration;
        QPointer<QAction> action;
        ActionContainer*  menu = nullptr;
    };
    MainWindow&                                 _window;
    CommandRegistry&                            _commands;
    SettingsWidgetRegistry&                     _editors;
    std::function<Workspace*()>                 _workspace;
    std::function<void()>                       _closeSettings;
    std::function<void()>                       _showSettings;
    ProcessorRegistry*                          _processors = nullptr;
    QPointer<ProcessorFavorites>                _processorFavorites;
    Session*                                    _session = nullptr;
    std::shared_ptr<SettingsSchema>             _appearance;
    std::map<QString, Type>                     _types;
    std::map<QString, std::unique_ptr<Runtime>> _terminals;
    std::map<QString, RegisteredAction>         _actions;
    QHash<QObject*, QSet<int>>                  _shortcutReleases;
    QPointer<TerminalDisplay>                   _shortcutCandidate;
    int                                         _shortcutCandidateKey = 0;
    QPointer<SettingsDialog>                    _creation;
    QHash<SettingsDialog*, ConnectionDraft>     _connectionDrafts;

    int                                      _batchDepth            = 0;
    bool                                     _batchSelectionChanged = false;
    QPointer<QWidget>                        _batchSelected;
    QList<QPair<QPointer<QTabWidget>, bool>> _batchTabs;

    std::optional<QString>                      _commandTarget;
    bool                                        _movingHost        = false;
    bool                                        _refreshingActions = false;
    QString                                     _focusedTerminal;
    bool                                        _layoutRestored = false;
    QList<QPointer<DetachedTerminalWindow>>     _windows;
    QHash<QString, QString>                     _placements;
    QHash<QWidget*, QPointer<TerminalTabGroup>> _focusedGroups;

    void                      _wireWorkspace(TerminalWorkspace& workspace);
    void                      _wireGroup(TerminalTabGroup* group);
    void                      _refreshGroupControls(TerminalTabGroup* group);
    void                      _refreshWindowActions(DetachedTerminalWindow* window);
    DetachedTerminalWindow*   _createWindow(const QString& id = {}, int number = 0);
    void                      _restoreLayout();
    void                      _cleanupGroups();
    TerminalTabGroup*         _findGroup(const QString& id) const;
    QList<TerminalWorkspace*> _workspaces() const;
    QStringList               _surfaceCommands(const QString& surface, bool secondary) const;
    void                      _showGroupMenu(const QString& id, const QPoint& position);

    QString  _connectionAction(Action::Role role) const;
    Runtime* _current() const;
    QString  _mainTerminalId() const;
    void     _focusChanged();
    void     _triggerAction(const QString& command, const QString& target, QWidget* window);
    void     _refreshHostActions(const QString& id);
    bool     _actionChecked(const QString& command, const Runtime* terminal) const;
    bool     _activeWindow() const;
    bool     _materialize(const QString&   id,
                          QString*         error,
                          TerminalDisplay* display = nullptr,
                          CommandEditor*   editor  = nullptr);
    void     _restore();
    bool     _rememberTabOrder(QString* error = nullptr);
    void     _registerAction(Action action, ActionContainer* menu);
    void     _beginBatch();
    void     _endBatch();
    std::optional<std::array<std::shared_ptr<Processor>, 2>> _prepareProcessing(const QVariantMap& configuration,
                                                                                QString*           error,
                                                                                bool               txOnly = false);
    bool                                                     _applyProcessing(const QString&     id,
                                                                              const QVariantMap& baseline,
                                                                              const QVariantMap& configuration,
                                                                              QString*           error);
    bool                            _configureProcessing(Runtime& terminal, QString* error, bool txOnly = false);
    void                            _close(const QString& id, bool preservePlacement = false);
    bool                            _canSetAllConnections(bool requested) const;
    void                            _setAllConnections(bool requested);
    void                            _setConnectionRequested(QString id, bool requested);
    void                            _changed(const QString& id);
    void                            _showTerminalMessage(const QString& id, const QString& message);
    QString                         _mainParameter(const Runtime& terminal, const QVariantMap& transport) const;
    void                            _attemptConnection(const QString& id);
    QVariantMap                     _effective(const Type& type, SettingsOverlay settings) const;
    bool                            _validate(const Type& type, SettingsOverlay settings, QString* error) const;
    std::shared_ptr<SettingsSchema> _appearanceSchema();
};
