#include "WindowManager.h"

WindowManager::WindowManager() : _currentId(0)
{
}

MainWindow* WindowManager::currentWindow()
{
    return _currentWindow;
}

QList<MainWindow*> WindowManager::windows()
{
    return _windows;
}

MainWindow* WindowManager::newWindow(bool show)
{
    auto onWindowClosed = [this](QObject* obj) {
        auto window = dynamic_cast<MainWindow*>(obj);
        _windows.removeOne(window);
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

    auto window  = new MainWindow(RandomNames::instance().getId(_currentId++));
    _windows    += window;

    connect(window, &QMainWindow::destroyed, this, onWindowClosed);
    connect(window, &MainWindow::focusChanged, this, onFocusEvent);

    emit windowCreated(window);

    if (show)
    {
        window->show();
    }
    return window;
}
