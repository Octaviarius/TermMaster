#pragma once

#include <QApplication>
#include <memory>

class ApplicationPaths;
class CommandRegistry;
class MenubarActionContainer;
class ToolbarActionContainer;
class Settings;
class QMainWindow;

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

private:
    struct Data;
    std::unique_ptr<Data> _data;

    void _setupActions();
    void _showPluginManager();
};
