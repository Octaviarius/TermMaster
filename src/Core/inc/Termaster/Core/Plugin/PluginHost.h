#pragma once

#include <QByteArray>
#include <QIcon>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVariant>
#include <Termaster/Core/Terminal/SerialDevice.h>
#include <functional>
#include <memory>

class QWidget;

/** Pure virtual host ABI: plugins link Qt and the SDK, never a second static Core.
 * All methods execute on the GUI thread. Host outlives plugin shutdown.
 */
class PluginHost
{
public:
    class FieldEditor
    {
    public:
        virtual ~FieldEditor()                                              = default;
        virtual QWidget* widget()                                           = 0;
        virtual QVariant value() const                                      = 0;
        virtual void     setValue(const QVariant& value)                    = 0;
        virtual void     setChangedCallback(std::function<void()> callback) = 0;
        virtual QString  inputError() const
        {
            return {};
        }
    };
    using EditorFactory = std::function<std::unique_ptr<FieldEditor>(const QVariantMap&, QWidget*)>;
    struct TerminalType
    {
        QString                                        id;
        QString                                        title;
        QString                                        plugin;
        QByteArray                                     settingsSchema;
        std::function<std::unique_ptr<SerialDevice>()> createDevice;
        /** Optional plugin-owned panel; reparents and owns the supplied common display. */
        std::function<QWidget*(QWidget*, const QVariantMap&, QWidget*)> createPanel;
    };
    struct Action
    {
        QString               id;
        QString               title;
        QIcon                 icon;
        QKeySequence          shortcut;
        QString               group;
        std::function<void()> trigger;
        std::function<bool()> enabled;
        std::function<bool()> visible;
    };

    virtual ~PluginHost()                                                                       = default;
    virtual QObject* context()                                                                  = 0;
    virtual void     registerTerminalType(TerminalType type)                                    = 0;
    /** Closes dependent terminals and settings views before removing their factories. */
    virtual bool          unregisterTerminalType(const QString& type, QString* error = nullptr) = 0;
    virtual void          registerAction(Action action)                                         = 0;
    virtual void          unregisterAction(const QString& id)                                   = 0;
    virtual void          registerEditor(const QString& id, EditorFactory factory)              = 0;
    virtual void          unregisterEditor(const QString& id)                                   = 0;
    virtual void          showCreateTerminal(const QString& type)                               = 0;
    virtual SerialDevice* selectedDevice(const QString& type)                                   = 0;
    virtual bool          canCreateTerminal() const                                             = 0;
    virtual bool          hasSelectedTerminal(const QString& type) const                        = 0;
    virtual void          connectSelectedTerminal()                                             = 0;
    virtual void          disconnectSelectedTerminal()                                          = 0;
    virtual void          closeSelectedTerminal()                                               = 0;
    virtual void          clearSelectedTerminal()                                               = 0;
    virtual void          showSelectedTerminalSettings()                                        = 0;
    virtual void          refreshTerminalActions()                                              = 0;
};
