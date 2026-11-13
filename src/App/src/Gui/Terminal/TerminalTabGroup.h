#pragma once

#include <QFrame>
#include <QHash>
#include <QPointer>

class QAction;
class QIcon;
class QLabel;
class QMimeData;
class QMenu;
class QRubberBand;
class QStatusBar;
class QTabWidget;
class QToolBar;
class QToolButton;

/** GUI-thread presentation group. Terminal pages and actions remain controller-owned. */
class TerminalTabGroup final : public QFrame
{
    Q_OBJECT
public:
    explicit TerminalTabGroup(QString id = {}, QWidget* parent = nullptr);
    ~TerminalTabGroup() override;

    QString     id() const;
    QTabWidget& tabs();
    QToolBar&   toolbar();
    QStatusBar& statusBar();
    void        setTabPresentation(int            index,
                                   const QString& title,
                                   const QString& detail,
                                   const QIcon&   typeIcon,
                                   const QIcon&   statusIcon);
    void        clearControls();
    QAction*    action(const QString& id) const;
    void        setAction(const QString& id, QAction* action);

    Q_SIGNAL void groupTransferRequested(TerminalTabGroup* source, TerminalTabGroup* target, Qt::Edge edge);
    Q_SIGNAL void groupDetachRequested(TerminalTabGroup* source, const QPoint& globalPosition);
    Q_SIGNAL void activated();
    Q_SIGNAL void closeRequested(const QString& id);
    Q_SIGNAL void contextMenuRequested(const QString& id, const QPoint& globalPosition);
    Q_SIGNAL void detachRequested(const QString& id, const QPoint& globalPosition);
    Q_SIGNAL void transferRequested(const QString& id, TerminalTabGroup* target, Qt::Edge edge, int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString                           _id;
    bool                              _dragWholeGroup  = false;
    bool                              _activeGroupDrag = false;
    QToolButton*                      _groupDragHandle = nullptr;
    QList<QPointer<QWidget>>          _activeGroupPages;
    QStringList                       _activeGroupIds;
    QToolButton*                      _hiddenTabsButton = nullptr;
    QMenu*                            _hiddenTabsMenu   = nullptr;
    QTabWidget*                       _tabs             = nullptr;
    QToolBar*                         _toolbar          = nullptr;
    QStatusBar*                       _statusBar        = nullptr;
    QHash<QString, QPointer<QAction>> _actions;
    QRubberBand*                      _preview        = nullptr;
    QLabel*                           _previewCaption = nullptr;
    QPointer<QWidget>                 _dragPage;
    QPointer<QWidget>                 _activeDragPage;
    QString                           _activeDragId;
    QString                           _dragId;
    QPoint                            _dragStart;
    QPoint                            _dragOffset;
    Qt::Edge                          _dropEdge  = Qt::Edge(0);
    int                               _dropIndex = -1;

    void              _refreshHiddenTabs();
    QString           _terminalId(int index) const;
    QString           _mimeId(const QMimeData* mime, QObject* source) const;
    TerminalTabGroup* _groupSource(const QMimeData* mime, QObject* source) const;
    void              _showPreview(const QPoint& globalPosition, bool wholeGroup = false);
    void              _hidePreview();
    void              _startDrag(bool wholeGroup = false);
};
