#include "TerminalTabGroup.h"

#include "TerminalWorkspace.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegion>
#include <QRubberBand>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <Termaster/Core/Gui/Icons.h>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
/** Native tab geometry and dragging; the complete caption lives in its layout. */
class TerminalTabBar final : public QTabBar
{
public:
    TerminalTabBar()
    {
        qApp->installEventFilter(this);
        _refreshStyle();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        {
            _refreshStyle();
        }
        return QTabBar::eventFilter(watched, event);
    }

private:
    void _refreshStyle()
    {
        const auto   colors     = QApplication::palette();
        const auto   background = colors.color(QPalette::Window);
        const auto   accent     = colors.color(QPalette::Highlight);
        const QColor selected((4 * background.red() + accent.red()) / 5,
                              (4 * background.green() + accent.green()) / 5,
                              (4 * background.blue() + accent.blue()) / 5);
        const auto   sheet = QString("QTabBar::tab { background-color: %1; color: %2; "
                                     "border-right: 1px solid %5; border-bottom: 3px solid transparent; } "
                                     "QTabBar::tab:selected { background-color: %3; border-bottom-color: %4; } "
                                     "QTabBar::scroller { width: 44px; } "
                                     "QTabBar > QToolButton { background-color: %1; color: %2; border: 1px solid %5; } "
                                     "QTabBar > QToolButton:disabled { color: %5; }")
                                 .arg(background.name(),
                                      colors.color(QPalette::WindowText).name(),
                                      selected.name(),
                                      accent.name(),
                                      colors.color(QPalette::Mid).name());
        if (styleSheet() != sheet)
        {
            setStyleSheet(sheet);
        }
    }
};

/** Borrow the sibling bar's size hint so Qt lays out a full-height square corner. */
class HiddenTabsButton final : public QToolButton
{
public:
    explicit HiddenTabsButton(QTabWidget* tabs) : QToolButton(tabs), _tabBar(tabs->tabBar())
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override
    {
        const int side = std::max(QToolButton::sizeHint().height(), _tabBar->sizeHint().height());
        return QSize(side, side);
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

private:
    // The owning tab widget keeps its bar alive for the button's lifetime.
    QTabBar* _tabBar;
};

class TerminalTabs final : public QTabWidget
{
public:
    explicit TerminalTabs(QWidget* parent) : QTabWidget(parent)
    {
        setTabBar(new TerminalTabBar);
    }
};
} // namespace

TerminalTabGroup::TerminalTabGroup(QString id, QWidget* parent) :
    QFrame(parent), _id(QUuid(id).isNull() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id)
{
    setObjectName("terminalTabGroup");
    setAcceptDrops(true);
    _refreshFrame();
    _tabs = new TerminalTabs(this);
    _tabs->setObjectName("terminalTabs");
    _tabs->setDocumentMode(true);
    // QDrag owns reordering and cross-window transfer as one gesture.
    // Keep QTabBar's separate native drag state machine disabled.
    _tabs->setMovable(false);
    _tabs->setTabsClosable(false);
    _tabs->tabBar()->setElideMode(Qt::ElideNone);
    _tabs->tabBar()->setExpanding(false);
    _tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    _toolbar = new QToolBar(tr("Terminal"), this);
    _toolbar->setObjectName("terminalGroupToolbar");
    _toolbar->setMovable(false);
    _toolbar->setFloatable(false);
    _toolbar->setIconSize(QSize(16, 16));
    _toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _toolbar->setStyleSheet("QToolBar#terminalGroupToolbar { spacing: 1px; padding: 0px; border: 0px; }");
    _hiddenTabsButton = new HiddenTabsButton(_tabs);
    _hiddenTabsButton->setObjectName("terminalHiddenTabsButton");
    _hiddenTabsButton->setAutoRaise(true);
    _hiddenTabsButton->setIcon(Icons::icon("chevron-down"));
    _hiddenTabsButton->setIconSize(QSize(16, 16));
    _hiddenTabsButton->setStyleSheet("QToolButton::menu-indicator { image: none; }");
    _hiddenTabsButton->setToolTip(tr("Show hidden terminal tabs"));
    _hiddenTabsButton->setAccessibleName(tr("Show hidden terminal tabs"));
    _hiddenTabsButton->setPopupMode(QToolButton::InstantPopup);
    _hiddenTabsMenu = new QMenu(_hiddenTabsButton);
    _hiddenTabsMenu->setObjectName("terminalHiddenTabsMenu");
    _hiddenTabsButton->setMenu(_hiddenTabsMenu);
    _tabs->setCornerWidget(_hiddenTabsButton, Qt::TopRightCorner);
    connect(_hiddenTabsMenu, &QMenu::aboutToShow, this, &TerminalTabGroup::_refreshHiddenTabs);
    _statusBar = new QStatusBar(this);
    _statusBar->setObjectName("terminalGroupStatusBar");
    _statusBar->setSizeGripEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new QWidget(this);
    header->setObjectName("terminalGroupHeader");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(1);
    _groupDragHandle = new QToolButton(header);
    _groupDragHandle->setObjectName("terminalGroupDragHandle");
    _groupDragHandle->setIcon(Icons::icon("panels-top-left"));
    _groupDragHandle->setIconSize(QSize(16, 16));
    _groupDragHandle->setAutoRaise(true);
    _groupDragHandle->setCursor(Qt::OpenHandCursor);
    _groupDragHandle->setToolTip(tr("Drag the entire terminal group; release outside to open a new window"));
    _groupDragHandle->setAccessibleName(tr("Move terminal group"));
    headerLayout->addWidget(_groupDragHandle);
    headerLayout->addWidget(_toolbar, 1);
    layout->addWidget(header);
    layout->addWidget(_tabs, 1);
    layout->addWidget(_statusBar);
    _preview = new QRubberBand(QRubberBand::Rectangle, this);
    _preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    _previewCaption = new QLabel(this);
    _previewCaption->setObjectName("terminalDropCaption");
    _previewCaption->setAttribute(Qt::WA_TransparentForMouseEvents);
    _previewCaption->setAutoFillBackground(true);
    _previewCaption->setMargin(6);
    _previewCaption->setFrameStyle(QFrame::StyledPanel);
    _previewCaption->hide();

    connect(_tabs, &QTabWidget::currentChanged, this, [this] { emit activated(); });
    connect(_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        emit closeRequested(_terminalId(index));
    });
    connect(_tabs->tabBar(), &QTabBar::customContextMenuRequested, this, [this](const QPoint& position) {
        const QString id = _terminalId(_tabs->tabBar()->tabAt(position));
        if (!id.isEmpty())
        {
            emit contextMenuRequested(id, _tabs->tabBar()->mapToGlobal(position));
        }
    });
    // Observe descendants without changing terminal panels' own input handling.
    qApp->installEventFilter(this);
}

TerminalTabGroup::~TerminalTabGroup()
{
    qApp->removeEventFilter(this);
}

QString TerminalTabGroup::id() const
{
    return _id;
}

QTabWidget& TerminalTabGroup::tabs()
{
    return *_tabs;
}

QToolBar& TerminalTabGroup::toolbar()
{
    return *_toolbar;
}

QStatusBar& TerminalTabGroup::statusBar()
{
    return *_statusBar;
}

void TerminalTabGroup::setTabPresentation(int            index,
                                          const QString& title,
                                          const QString& detail,
                                          const QIcon&   typeIcon,
                                          const QIcon&   statusIcon)
{
    auto* bar = _tabs->tabBar();
    if (index < 0 || index >= bar->count())
    {
        return;
    }

    auto*       header       = bar->tabButton(index, QTabBar::LeftSide);
    const bool  created      = !header;
    const QSize previousSize = header ? header->size() : QSize();
    if (!header)
    {
        header = new QWidget(bar);
        header->setObjectName("terminalTabHeader");
        auto* row = new QHBoxLayout(header);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        row->setSizeConstraint(QLayout::SetFixedSize);

        const auto iconButton = [header](const QString& name) {
            auto* button = new QToolButton(header);
            button->setObjectName(name);
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            button->setIconSize(QSize(16, 16));
            button->setFixedSize(16, 16);
            return button;
        };
        auto* status = iconButton("terminalStatusIndicator");
        status->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(status, 0, Qt::AlignTop);

        auto* caption = new QVBoxLayout;
        caption->setContentsMargins(0, 0, 0, 0);
        caption->setSpacing(0);
        auto* titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(4);
        titleRow->addStretch();
        auto* glyph = iconButton("terminalTypeIcon");
        glyph->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(glyph);
        auto* titleLabel = new QLabel(header);
        titleLabel->setObjectName("terminalTabTitle");
        titleLabel->setTextFormat(Qt::PlainText);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(titleLabel);
        titleRow->addStretch();
        caption->addLayout(titleRow);
        auto* detailLabel = new QLabel(header);
        detailLabel->setObjectName("terminalTabDetail");
        detailLabel->setTextFormat(Qt::PlainText);
        detailLabel->setAlignment(Qt::AlignCenter);
        detailLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        caption->addWidget(detailLabel);
        row->addLayout(caption);

        auto* close = iconButton("terminalTabClose");
        close->setIcon(Icons::icon("x"));
        close->setToolTip(tr("Close terminal"));
        close->setAccessibleName(tr("Close terminal"));
        row->addWidget(close, 0, Qt::AlignTop);
        connect(close, &QToolButton::clicked, header, [this, header] {
            auto* currentBar = _tabs->tabBar();
            for (int current = 0; current < currentBar->count(); ++current)
            {
                if (currentBar->tabButton(current, QTabBar::LeftSide) == header)
                {
                    emit _tabs->tabCloseRequested(current);
                    return;
                }
            }
        });
    }

    header->findChild<QToolButton*>("terminalStatusIndicator")->setIcon(statusIcon);
    header->findChild<QToolButton*>("terminalTypeIcon")->setIcon(typeIcon);
    header->findChild<QLabel*>("terminalTabTitle")->setText(title);
    auto* detailLabel = header->findChild<QLabel*>("terminalTabDetail");
    detailLabel->setText(detail);
    detailLabel->setVisible(!detail.isEmpty());
    header->layout()->activate();
    header->adjustSize();

    // A single standard tab button owns all visible content. Qt moves and sizes
    // it as one unit during dragging, scrolling and animation.
    _tabs->setTabText(index, {});
    if (created || header->size() != previousSize)
    {
        if (!created)
        {
            bar->setTabButton(index, QTabBar::LeftSide, nullptr);
        }
        bar->setTabButton(index, QTabBar::LeftSide, header);
    }
}

void TerminalTabGroup::clearControls()
{
    _toolbar->clear();
    _statusBar->clearMessage();
    const auto widgets = _statusBar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* widget : widgets)
    {
        _statusBar->removeWidget(widget);
        widget->hide();
        widget->deleteLater();
    }
    _actions.clear();
}

QAction* TerminalTabGroup::action(const QString& id) const
{
    return _actions.value(id).data();
}

void TerminalTabGroup::setAction(const QString& id, QAction* action)
{
    if (action)
    {
        _actions.insert(id, action);
    }
    else
    {
        _actions.remove(id);
    }
}

bool TerminalTabGroup::eventFilter(QObject* watched, QEvent* event)
{
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || (widget != this && !isAncestorOf(widget)))
    {
        return QFrame::eventFilter(watched, event);
    }

    auto* bar = _tabs->tabBar();
    if (watched == bar && event->type() == QEvent::Wheel)
    {
        auto*      wheel = static_cast<QWheelEvent*>(event);
        const auto delta = wheel->angleDelta().isNull() ? wheel->pixelDelta() : wheel->angleDelta();
        if (!delta.isNull())
        {
            const bool   backwards = delta.y() != 0 ? delta.y() > 0 : delta.x() > 0;
            const bool   right     = backwards == (bar->layoutDirection() == Qt::RightToLeft);
            QToolButton* scroll    = nullptr;
            for (auto* button : bar->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly))
            {
                if (button->isVisible()
                    && (!scroll
                        || (right ? button->geometry().center().x() > scroll->geometry().center().x() :
                                    button->geometry().center().x() < scroll->geometry().center().x())))
                {
                    scroll = button;
                }
            }
            if (scroll)
            {
                if (scroll->isEnabled())
                {
                    scroll->click();
                }
                wheel->accept();
                return true;
            }
        }
    }

    const bool groupHandle = watched == _groupDragHandle || watched == _toolbar;
    if (groupHandle && event->type() == QEvent::MouseButtonPress)
    {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            _dragWholeGroup = true;
            _dragStart      = mouse->globalPosition().toPoint();
            _dragOffset     = mapFromGlobal(_dragStart);
        }
    }
    else if (groupHandle && event->type() == QEvent::MouseMove)
    {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (_dragWholeGroup && (mouse->buttons() & Qt::LeftButton)
            && (mouse->globalPosition().toPoint() - _dragStart).manhattanLength() >= QApplication::startDragDistance())
        {
            _groupDragHandle->setDown(false);
            _startDrag(true);
            return true;
        }
    }
    else if (groupHandle && event->type() == QEvent::MouseButtonRelease)
    {
        _dragWholeGroup = false;
    }

    if (watched == bar && event->type() == QEvent::MouseButtonPress)
    {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const int index = bar->tabAt(mouse->position().toPoint());
            _dragPage       = _tabs->widget(index);
            _dragId         = _terminalId(index);
            _dragStart      = mouse->globalPosition().toPoint();
            _dragOffset     = mouse->position().toPoint() - bar->tabRect(index).topLeft();
        }
    }
    else if (watched == bar && event->type() == QEvent::MouseMove)
    {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if ((mouse->buttons() & Qt::LeftButton) && _dragPage && !_dragId.isEmpty()
            && (mouse->globalPosition().toPoint() - _dragStart).manhattanLength() >= QApplication::startDragDistance())
        {
            _startDrag();
            return true;
        }
    }
    else if (watched == bar && event->type() == QEvent::MouseButtonRelease)
    {
        _dragPage.clear();
        _dragId.clear();
    }

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)
    {
        auto* drag        = static_cast<QDragMoveEvent*>(event);
        auto* sourceGroup = _groupSource(drag->mimeData(), drag->source());
        if ((sourceGroup || !_mimeId(drag->mimeData(), drag->source()).isEmpty())
            && (drag->possibleActions() & Qt::MoveAction))
        {
            _showPreview(widget->mapToGlobal(drag->position().toPoint()), sourceGroup != nullptr);
            drag->setDropAction(Qt::MoveAction);
            drag->accept();
            return true;
        }
        _hidePreview();
    }
    else if (event->type() == QEvent::Drop)
    {
        auto*         drop        = static_cast<QDropEvent*>(event);
        const QString id          = _mimeId(drop->mimeData(), drop->source());
        auto*         sourceGroup = _groupSource(drop->mimeData(), drop->source());
        if ((sourceGroup || !id.isEmpty()) && (drop->possibleActions() & Qt::MoveAction))
        {
            _showPreview(widget->mapToGlobal(drop->position().toPoint()), sourceGroup != nullptr);
            const Qt::Edge edge  = _dropEdge;
            const int      index = _dropIndex;
            _hidePreview();
            drop->setDropAction(Qt::MoveAction);
            drop->accept();
            if (sourceGroup)
            {
                emit groupTransferRequested(sourceGroup, this, edge);
            }
            else
            {
                emit transferRequested(id, this, edge, index);
            }
            return true;
        }
        _hidePreview();
    }
    else if (event->type() == QEvent::DragLeave)
    {
        _hidePreview();
    }

    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress)
    {
        const QPointer<QWidget> guard(widget);
        emit                    activated();
        return guard.isNull();
    }
    return QFrame::eventFilter(watched, event);
}

void TerminalTabGroup::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        _refreshFrame();
    }
}

void TerminalTabGroup::_refreshFrame()
{
    const QString sheet = QString("QFrame#terminalTabGroup { border: 2px solid %1; }")
                              .arg(palette().color(QPalette::Mid).name());
    if (styleSheet() != sheet)
    {
        setStyleSheet(sheet);
    }
}

void TerminalTabGroup::_refreshHiddenTabs()
{
    _hiddenTabsMenu->clear();
    auto*   bar = _tabs->tabBar();
    QRegion available(bar->rect());
    // Native scroll buttons obscure the ends of the bar even though those
    // tab rectangles still intersect its bounds.
    const auto buttons = bar->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* button : buttons)
    {
        if (button->isVisible())
        {
            available -= button->geometry();
        }
    }

    for (int index = 0; index < _tabs->count(); ++index)
    {
        if (!_tabs->isTabVisible(index) || QRegion(bar->tabRect(index)).subtracted(available).isEmpty())
        {
            continue;
        }

        auto*   header  = bar->tabButton(index, QTabBar::LeftSide);
        auto*   title   = header ? header->findChild<QLabel*>("terminalTabTitle") : nullptr;
        auto*   detail  = header ? header->findChild<QLabel*>("terminalTabDetail") : nullptr;
        auto*   glyph   = header ? header->findChild<QToolButton*>("terminalTypeIcon") : nullptr;
        QString caption = title ? title->text() : _tabs->tabText(index);
        if (detail && !detail->text().isEmpty())
        {
            caption += QStringLiteral(" — ") + detail->text();
        }
        // QAction labels use ampersands for mnemonics; terminal names are literal.
        caption.replace("&", "&&");
        auto* action = _hiddenTabsMenu->addAction(glyph ? glyph->icon() : _tabs->tabIcon(index), caption);
        action->setEnabled(_tabs->isTabEnabled(index));
        action->setCheckable(true);
        action->setChecked(index == _tabs->currentIndex());
        const QPointer<QWidget> page(_tabs->widget(index));
        connect(action, &QAction::triggered, this, [this, page] {
            const int currentIndex = page ? _tabs->indexOf(page) : -1;
            if (currentIndex < 0 || !_tabs->isTabVisible(currentIndex) || !_tabs->isTabEnabled(currentIndex))
            {
                return;
            }

            const QPointer<TerminalTabGroup> guard(this);
            _tabs->setCurrentWidget(page);
            if (guard && page && _tabs->currentWidget() == page)
            {
                page->setFocus(Qt::OtherFocusReason);
            }
        });
    }

    if (_hiddenTabsMenu->isEmpty())
    {
        _hiddenTabsMenu->addAction(tr("All terminal tabs are visible"))->setEnabled(false);
    }
}

QString TerminalTabGroup::_terminalId(int index) const
{
    auto* page = _tabs->widget(index);
    return page ? page->property("terminalId").toString() : QString();
}

QString TerminalTabGroup::_mimeId(const QMimeData* mime, QObject* source) const
{
    if (!mime || !mime->hasFormat("application/x-termaster-terminal-tab"))
    {
        return {};
    }
    const QByteArray bytes = mime->data("application/x-termaster-terminal-tab");
    if (bytes.isEmpty() || bytes.size() > 128 || bytes.contains('\0'))
    {
        return {};
    }
    const QString id          = QString::fromUtf8(bytes);
    auto*         sourceGroup = qobject_cast<TerminalTabGroup*>(source);
    if (id.toUtf8() != bytes || !sourceGroup || sourceGroup->_activeDragId != id || !sourceGroup->_activeDragPage
        || sourceGroup->_tabs->indexOf(sourceGroup->_activeDragPage) < 0
        || sourceGroup->_activeDragPage->property("terminalId").toString() != id)
    {
        return {};
    }
    // Every shell belongs to the same root QWidget. A different root denotes
    // another controller/session even when saved terminal UUIDs coincide.
    QWidget* sourceRoot = sourceGroup;
    while (sourceRoot->parentWidget())
    {
        sourceRoot = sourceRoot->parentWidget();
    }
    const QWidget* targetRoot = this;
    while (targetRoot->parentWidget())
    {
        targetRoot = targetRoot->parentWidget();
    }
    return sourceRoot == targetRoot ? id : QString();
}

TerminalTabGroup* TerminalTabGroup::_groupSource(const QMimeData* mime, QObject* source) const
{
    auto* group = qobject_cast<TerminalTabGroup*>(source);
    if (!mime || !group || !group->_activeGroupDrag
        || mime->data("application/x-termaster-terminal-group") != group->_id.toUtf8()
        || group->_tabs->count() != group->_activeGroupPages.size())
    {
        return nullptr;
    }
    for (int index = 0; index < group->_tabs->count(); ++index)
    {
        if (!group->_activeGroupPages[index] || group->_tabs->widget(index) != group->_activeGroupPages[index]
            || group->_terminalId(index) != group->_activeGroupIds[index])
        {
            return nullptr;
        }
    }
    QWidget* sourceRoot = group;
    while (sourceRoot->parentWidget())
    {
        sourceRoot = sourceRoot->parentWidget();
    }
    const QWidget* targetRoot = this;
    while (targetRoot->parentWidget())
    {
        targetRoot = targetRoot->parentWidget();
    }
    return sourceRoot == targetRoot ? group : nullptr;
}

void TerminalTabGroup::_showPreview(const QPoint& globalPosition, bool wholeGroup)
{
    const QPoint position    = mapFromGlobal(globalPosition);
    auto*        bar         = _tabs->tabBar();
    const QPoint barPosition = bar->mapFromGlobal(globalPosition);
    _dropEdge                = Qt::Edge(0);
    _dropIndex               = _tabs->count();
    QRect   area             = rect();
    QString caption          = tr("Add to tab group");
    if (!wholeGroup && bar->rect().contains(barPosition))
    {
        for (int index = 0; index < bar->count(); ++index)
        {
            const QRect tab    = bar->tabRect(index);
            const bool  before = bar->layoutDirection() == Qt::RightToLeft ? barPosition.x() > tab.center().x() :
                                                                             barPosition.x() < tab.center().x();
            if (before)
            {
                _dropIndex = index;
                break;
            }
        }
        const bool rtl = bar->layoutDirection() == Qt::RightToLeft;
        const int  x = _dropIndex < bar->count() ?
                           (rtl ? bar->tabRect(_dropIndex).right() : bar->tabRect(_dropIndex).left()) :
                           (bar->count() ?
                                (rtl ? bar->tabRect(bar->count() - 1).left() : bar->tabRect(bar->count() - 1).right()) :
                                0);
        area         = QRect(bar->mapTo(this, QPoint(x, 0)), QSize(4, bar->height()));
        caption      = tr("Insert terminal tab");
    }
    else if (width() > 0 && height() > 0)
    {
        const double left    = double(position.x()) / width();
        const double right   = double(width() - position.x()) / width();
        const double top     = double(position.y()) / height();
        const double bottom  = double(height() - position.y()) / height();
        const double nearest = std::min({left, right, top, bottom});
        if (wholeGroup || nearest < 0.25)
        {
            if (nearest == left)
            {
                _dropEdge = Qt::LeftEdge;
                area.setWidth(width() / 2);
                caption = tr("Split left");
            }
            else if (nearest == right)
            {
                _dropEdge = Qt::RightEdge;
                area.setLeft(width() / 2);
                caption = tr("Split right");
            }
            else if (nearest == top)
            {
                _dropEdge = Qt::TopEdge;
                area.setHeight(height() / 2);
                caption = tr("Split above");
            }
            else
            {
                _dropEdge = Qt::BottomEdge;
                area.setTop(height() / 2);
                caption = tr("Split below");
            }
        }
    }
    if (wholeGroup)
    {
        caption = tr("Move terminal group: %1").arg(caption);
    }
    _preview->setGeometry(area);
    _preview->show();
    _preview->raise();
    _previewCaption->setText(caption);
    _previewCaption->adjustSize();
    _previewCaption->move(std::max(0, (width() - _previewCaption->width()) / 2),
                          std::max(0, (height() - _previewCaption->height()) / 2));
    _previewCaption->show();
    _previewCaption->raise();
}

void TerminalTabGroup::_hidePreview()
{
    _preview->hide();
    _previewCaption->hide();
}

void TerminalTabGroup::_startDrag(bool wholeGroup)
{
    const QString           id   = wholeGroup ? _id : _dragId;
    const QPointer<QWidget> page = _dragPage;
    _dragId.clear();
    _dragPage.clear();
    _dragWholeGroup        = false;
    const int        index = _tabs->indexOf(page);
    const QByteArray bytes = id.toUtf8();
    if ((!wholeGroup && index < 0) || (wholeGroup && !_tabs->count() && !property("retainedPlacement").toBool())
        || bytes.isEmpty() || bytes.size() > 128 || bytes.contains('\0'))
    {
        return;
    }

    auto* bar  = _tabs->tabBar();
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(wholeGroup ? "application/x-termaster-terminal-group" : "application/x-termaster-terminal-tab",
                  bytes);
    drag->setMimeData(mime);
    const QPixmap original = wholeGroup ? grab(QRect(0, 0, std::min(width(), 600), _tabs->y() + bar->height())) :
                                          bar->grab(bar->tabRect(index));
    QPixmap       ghost(original.size());
    ghost.setDevicePixelRatio(original.devicePixelRatio());
    ghost.fill(Qt::transparent);
    {
        QPainter painter(&ghost);
        painter.setOpacity(0.65);
        painter.drawPixmap(QPoint(), original);
    }
    drag->setPixmap(ghost);
    drag->setHotSpot(wholeGroup ? QPoint(std::clamp(_dragOffset.x(), 0, std::min(width(), 600) - 1), _dragOffset.y()) :
                                  _dragOffset);
    _activeGroupDrag = wholeGroup;
    if (wholeGroup)
    {
        for (int tab = 0; tab < _tabs->count(); ++tab)
        {
            _activeGroupPages.append(_tabs->widget(tab));
            _activeGroupIds.append(_terminalId(tab));
        }
    }
    _activeDragPage = page;
    _activeDragId   = id;
    const QPointer<TerminalTabGroup> guard(this);
    const QPointer<QDrag>            dragGuard(drag);
    const Qt::DropAction             result = drag->exec(Qt::MoveAction);
    if (!guard)
    {
        return;
    }
    const bool groupStillValid = wholeGroup && dragGuard && _groupSource(dragGuard->mimeData(), this) == this;
    _activeGroupDrag           = false;
    _activeGroupPages.clear();
    _activeGroupIds.clear();
    _activeDragPage.clear();
    _activeDragId.clear();
    if (dragGuard)
    {
        dragGuard->deleteLater();
    }
    _hidePreview();

    const QPoint globalPosition = QCursor::pos();
    const int    threshold      = std::max(QApplication::startDragDistance() * 2, bar->height() / 2);
    const bool   outside        = !bar->rect()
                                       .adjusted(-threshold, -threshold, threshold, threshold)
                                       .contains(bar->mapFromGlobal(globalPosition));
    bool         overWorkspace  = false;
    for (auto* hit = QApplication::widgetAt(globalPosition); hit; hit = hit->parentWidget())
    {
        if (qobject_cast<TerminalWorkspace*>(hit))
        {
            overWorkspace = true;
            break;
        }
    }
    bool buttonHeld = QApplication::mouseButtons() & Qt::LeftButton;
#ifdef Q_OS_WIN
    // OLE resets Qt's cached mouse buttons when Escape cancels the native loop.
    // An actual release outside targets must be distinguished from that cancel.
    if (QGuiApplication::platformName() == "windows")
    {
        const int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
        buttonHeld       = GetAsyncKeyState(button) & 0x8000;
    }
#endif
    if (result == Qt::IgnoreAction && !overWorkspace && !buttonHeld && outside)
    {
        if (groupStillValid)
        {
            emit groupDetachRequested(this, globalPosition);
        }
        else if (!wholeGroup && page && _tabs->indexOf(page) >= 0 && page->property("terminalId").toString() == id)
        {
            emit detachRequested(id, globalPosition);
        }
    }
}
