#pragma once

#include <QObject>
#include <QPointer>

class PluginManager;
class PluginManagerWindow;

/** Routes window commands to the authoritative manager on the GUI thread. */
class PluginManagerController final : public QObject
{
    Q_OBJECT

private:
    QPointer<PluginManager>       _manager;
    QPointer<PluginManagerWindow> _window;
    QString                       _operationError;
    bool                          _busy = false;

    void _setEnabled(const QString& id, bool enabled);
    void _reload(const QString& id);
    void _refresh();
    void _openFolder();
    bool _begin();
    void _finish(bool success, const QString& error);

public:
    explicit PluginManagerController(PluginManager& manager, PluginManagerWindow& window);
};
