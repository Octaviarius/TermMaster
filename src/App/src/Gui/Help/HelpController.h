#pragma once

#include <QMap>
#include <QObject>
#include <QPointer>

class CommandRegistry;
class PluginManager;
class QWidget;
class QDialog;
class HelpBrowser;

/** Application-owned help windows and metadata-only context resolution. */
class HelpController final : public QObject
{
    Q_OBJECT
public:
    HelpController(CommandRegistry& commands, PluginManager& plugins, QWidget* window, QObject* parent = nullptr);
    ~HelpController() override;

    void showContext(QWidget* context);
    void showTopic(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    CommandRegistry&       _commands;
    PluginManager&         _plugins;
    QPointer<QWidget>      _window;
    QPointer<QDialog>      _dialog;
    QPointer<HelpBrowser>  _browser;
    QMap<QString, QString> _packages;
    QMap<QString, QString> _packageVersions;
    bool                   _refreshPending = false;
    int                    _releaseKey     = 0;

    void _ensureWindow();
    void _syncPackages();
    void _scheduleRefresh();
};
