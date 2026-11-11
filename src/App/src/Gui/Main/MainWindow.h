#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <Termaster/Core/Gui/Action/MenubarActionContainer.h>
#include <Termaster/Core/Gui/Action/ToolbarActionContainer.h>

class WorkspacePage;
class QIcon;
class QTabWidget;
class QStackedWidget;

/** Application shell. Commands and domain operations are wired by App. */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    MenubarActionContainer& mainMenu();
    ToolbarActionContainer& toolbar();
    QTabWidget&             terminalTabs();
    QMainWindow&            terminalDockHost();
    void                    showTerminals(bool visible);
    void                    setTerminalTab(int            index,
                                           const QString& title,
                                           const QString& detail,
                                           const QIcon&   typeIcon,
                                           const QIcon&   statusIcon);
    void                    setWorkspace(const QString& name, const QString& file);
    void                    showWorkspaceChoice(const QString& error = {});
    void                    setSession(const QString& name, const QString& file, int objects, int links);
    Q_SIGNAL void           openSessionRequested();
    Q_SIGNAL void           createSessionRequested();
    Q_SIGNAL void           openWorkspaceRequested();
    Q_SIGNAL void           createWorkspaceRequested();
    Q_SIGNAL void           terminalDetachRequested(const QString& terminalId);
    Q_SIGNAL void           terminalTearOffRequested(const QString& terminalId,
                                                     const QPoint&  globalPosition,
                                                     const QPoint&  grabOffset);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    MenubarActionContainer _mainMenu;
    ToolbarActionContainer _toolbar;
    WorkspacePage*         _workspacePage;
    QTabWidget*            _terminalTabs;
    QStackedWidget*        _central;
    QMainWindow*           _terminalDockHost;
    QString                _dragTerminalId;
    QPointer<QWidget>      _dragPanel;
    QPoint                 _dragStart;
    QPoint                 _dragGrabOffset;
    quint64                _dragGeneration = 0;

    void _createMenus();
};
