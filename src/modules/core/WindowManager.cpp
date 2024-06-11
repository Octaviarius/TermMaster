#include "WindowManager.h"

#include "widgets/MainWindow.h"

WindowContainer::WindowContainer(QMainWindow* window)
{
    _window = window;
    connect(window, &QMainWindow::destroyed, this, &WindowContainer::deleteLater);
}

QMainWindow* WindowContainer::window()
{
    return _window;
}

/******************************************************************************/

WindowManager::WindowManager()
{
}

WindowContainer* WindowManager::currentWindow()
{
    return _currentWindow;
}
DocumentContainer* WindowManager::currentDocument()
{
}

QList<DocumentContainer*> WindowManager::documents()
{
}
QList<WindowContainer*> WindowManager::windows()
{
    return _windows;
}

WindowContainer* WindowManager::newWindow()
{
    auto w         = new MainWindow();
    auto container = new WindowContainer(w);

    auto onWindowClosed = [this](QObject* obj) {
        this->_windows.removeOne(obj);
        emit this->windowClosed(qobject_cast<WindowContainer*>(obj));
    };
    connect(container, &WindowContainer::destroyed, onWindowClosed);

    auto onWindowFocused = [this]() {};
    connect(container, &WindowContainer::focused, onWindowFocused);

    _windows += container;

    return container;
}
