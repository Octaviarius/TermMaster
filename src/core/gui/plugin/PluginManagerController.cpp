#include "PluginManagerController.h"

#include "PluginManagerWindow.h"

#include <QDesktopServices>
#include <QTimer>
#include <QUrl>
#include <Termaster/Core/gui/PluginManagerGui.h>
#include <Termaster/Core/plugin/PluginManager.h>

PluginManagerController::PluginManagerController(PluginManager& manager, PluginManagerWindow& window) :
    QObject(&window), _manager(&manager), _window(&window)
{
    connect(&window,
            &PluginManagerWindow::enabledChangeRequested,
            this,
            &PluginManagerController::_setEnabled,
            Qt::QueuedConnection);
    connect(&window,
            &PluginManagerWindow::reloadRequested,
            this,
            &PluginManagerController::_reload,
            Qt::QueuedConnection);
    connect(&window, &PluginManagerWindow::refreshRequested, this, &PluginManagerController::_refresh);
    connect(&window, &PluginManagerWindow::openFolderRequested, this, &PluginManagerController::_openFolder);
    connect(&manager, &PluginManager::errorOccurred, this, [this](const QString& error) {
        if (_busy && _operationError.isEmpty())
        {
            _operationError = error;
        }
        if (_window)
        {
            _window->showError(error);
        }
    });
    QTimer::singleShot(0, this, &PluginManagerController::_refresh);
}

void PluginManagerController::_setEnabled(const QString& id, bool enabled)
{
    if (!_begin())
    {
        return;
    }
    QString    error;
    const bool success = _manager->setEnabled(id, enabled, &error);
    _finish(success, error);
}

void PluginManagerController::_reload(const QString& id)
{
    if (!_begin())
    {
        return;
    }
    QString    error;
    const bool success = _manager->reload(id, &error);
    _finish(success, error);
}

void PluginManagerController::_refresh()
{
    if (!_begin())
    {
        return;
    }
    QString    error;
    const bool success = _manager->isActive() ? _manager->rescan(&error) : _manager->start(&error);
    _finish(success, error);
}

void PluginManagerController::_openFolder()
{
    if (!_manager || !_window)
    {
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(_manager->directory())))
    {
        _window->showError(tr("Cannot open the plugin folder."));
    }
}

bool PluginManagerController::_begin()
{
    if (_busy || !_manager || !_window)
    {
        return false;
    }
    _busy = true;
    _operationError.clear();
    _window->setBusy(true);
    _window->showResult({});
    return true;
}

void PluginManagerController::_finish(bool success, const QString& error)
{
    _busy = false;
    if (!_window)
    {
        return;
    }
    _window->setBusy(false);
    if (success)
    {
        _window->showResult({});
    }
    else
    {
        const QString cause = _operationError.isEmpty() ? error : _operationError;
        _window->showError(cause.isEmpty() ? tr("The plugin operation did not complete.") : cause);
    }
}

QDialog* createPluginManagerWindow(PluginManager& manager, QWidget* parent)
{
    auto window = new PluginManagerWindow(manager, parent);
    new PluginManagerController(manager, *window);
    return window;
}
