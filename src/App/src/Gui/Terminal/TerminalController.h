#pragma once

#include <QObject>
#include <QPointer>
#include <Termaster/Core/Plugin/PluginHost.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <map>

class MainWindow;
class CommandRegistry;
class SettingsWidgetRegistry;
class SettingsSchema;
class Session;
class Workspace;
class SettingsDialog;
class TerminalDisplay;
class QAction;

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

    void            setSession(Session* session);
    void            closeAll();
    bool            createTerminal(const QString&     type,
                                   const QVariantMap& overrides,
                                   const QString&     name,
                                   QString*           error = nullptr);
    QString         selectedTerminal() const;
    QStringList     terminalIds() const;
    SettingsOverlay selectedSettings();
    void            addSettingsPages(SettingsDialog& dialog);
    void            addDefaultSettingsPages(SettingsDialog& dialog, const QString& scope, SettingsOverlay settings);
    void            applySettings();

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

private:
    struct Type
    {
        TerminalType                    registration;
        std::shared_ptr<SettingsSchema> schema;
        std::shared_ptr<SettingsSchema> workspaceSchema;
        std::shared_ptr<SettingsSchema> defaultSchema;
    };
    struct Runtime;
    struct RegisteredAction
    {
        Action            registration;
        QPointer<QAction> action;
    };
    MainWindow&                                 _window;
    CommandRegistry&                            _commands;
    SettingsWidgetRegistry&                     _editors;
    std::function<Workspace*()>                 _workspace;
    std::function<void()>                       _closeSettings;
    std::function<void()>                       _showSettings;
    Session*                                    _session = nullptr;
    std::map<QString, Type>                     _types;
    std::map<QString, std::unique_ptr<Runtime>> _terminals;
    std::map<QString, RegisteredAction>         _actions;
    QPointer<SettingsDialog>                    _creation;

    Runtime*                               _current() const;
    bool                                   _activeWindow() const;
    bool                                   _materialize(const QString& id, QString* error);
    void                                   _restore();
    void                                   _close(const QString& id);
    void                                   _showTabMenu(const QPoint& position);
    bool                                   _canSetAllConnections(bool requested) const;
    void                                   _setAllConnections(bool requested);
    void                                   _setConnectionRequested(QString id, bool requested);
    void                                   _changed(const QString& id);
    QString                                _mainParameter(const Runtime& terminal) const;
    void                                   _attemptConnection(const QString& id);
    QVariantMap                            _effective(const Type& type, SettingsOverlay settings) const;
    bool                                   _validate(const Type& type, SettingsOverlay settings, QString* error) const;
    static std::shared_ptr<SettingsSchema> _appearanceSchema();
};
