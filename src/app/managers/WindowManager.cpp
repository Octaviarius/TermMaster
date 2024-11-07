#include "WindowManager.h"

#include "managers/SettingsManager.h"

WindowManager::WindowManager()
{
    _idCounter = SettingsManager::instance().latestSessionId() + 1;

    auto sessions = SettingsManager::instance().generalSettings("sessions");

    _recentSessions.setMaxItems(sessions->value("max_count", 10));
    _recentSessions = sessions->values<uint>("recent");

    delete sessions;
}

MainWindow* WindowManager::currentWindow()
{
    return _currentWindow;
}

QList<MainWindow*> WindowManager::windows()
{
    return _windows;
}

MainWindow* WindowManager::newWindow(int id, bool show)
{
    auto onWindowClosed = [this](QObject* obj) {
        auto window = dynamic_cast<MainWindow*>(obj);
        _windows.removeOne(window);

        _recentSessions.enqueue(window->id());

        emit windowTerminated(window);
    };

    auto onFocusEvent = [this](bool activeState) {
        if (activeState)
        {
            _currentWindow = dynamic_cast<MainWindow*>(QObject::sender());
            emit currentWindowChanged(_currentWindow);
            qInfo() << QString("Current window '%1'").arg(_currentWindow->windowTitle());
        }
        else
        {
            _currentWindow = nullptr;
            emit currentWindowChanged(_currentWindow);
        }
    };

    if (id < 0 || id > _idCounter)
    {
        id = _idCounter++;
    }

    auto window  = new MainWindow(id);
    _windows    += window;

    connect(window, &QMainWindow::destroyed, this, onWindowClosed);
    connect(window, &MainWindow::focusChanged, this, onFocusEvent);

    if (show)
    {
        window->show();
    }

    emit windowCreated(window);

    return window;
}

QList<uint> WindowManager::recentSessions()
{
    return _recentSessions;
}
