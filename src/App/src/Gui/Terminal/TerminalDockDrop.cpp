#include "TerminalDockDrop.h"

#include "../Main/MainWindow.h"
#include "TerminalDockWidget.h"

#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QLabel>
#include <QRubberBand>
#include <QTabBar>
#include <QTabWidget>
#include <QToolTip>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

TerminalDockDrop::TerminalDockDrop(MainWindow& window) : _window(window)
{
}

TerminalDockDrop::~TerminalDockDrop()
{
    delete _caption.data();
    delete _preview.data();
}

TerminalDockDrop::Target TerminalDockDrop::targetAt(const QPoint& globalPosition) const
{
    auto& host = _window.terminalDockHost();
    if (!_window.isVisible() || _window.isMinimized() || !host.isVisible())
    {
        return {};
    }
    const QPoint point  = host.mapFromGlobal(globalPosition);
    const QRect  bounds = host.rect();
    if (!bounds.contains(point))
    {
        return {};
    }
    const int outerBand = std::clamp(std::min(bounds.width(), bounds.height()) / 8, 24, 64);
    auto      result    = _edgeTarget(bounds, point, outerBand, nullptr);
    if (result.placement != Placement::None)
    {
        return result;
    }
    for (auto* dock : host.findChildren<TerminalDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (!dock->isVisible() || dock->isFloating())
        {
            continue;
        }
        const QRect rectangle(dock->mapTo(&host, QPoint()), dock->size());
        if (!rectangle.contains(point))
        {
            continue;
        }
        const int innerBand = std::clamp(std::min(rectangle.width(), rectangle.height()) / 4, 16, 64);
        result              = _edgeTarget(rectangle, point, innerBand, dock);
        if (result.placement == Placement::None)
        {
            result.placement = Placement::Tabify;
            result.dock      = dock;
            result.rectangle = rectangle;
        }
        return result;
    }
    auto* center = host.centralWidget();
    if (center && center->isVisible())
    {
        const QRect rectangle(center->mapTo(&host, QPoint()), center->size());
        if (rectangle.contains(point))
        {
            result.placement      = Placement::Tabs;
            result.rectangle      = rectangle;
            auto&        tabs     = _window.terminalTabs();
            auto*        bar      = tabs.tabBar();
            const QPoint tabPoint = bar->mapFromGlobal(globalPosition);
            const int    index    = bar->tabAt(tabPoint);
            result.tabIndex       = index >= 0 ? index + int(tabPoint.x() >= bar->tabRect(index).center().x()) :
                                                 tabs.currentIndex() + 1;
            result.tabIndex       = std::clamp(result.tabIndex, 0, tabs.count());
        }
    }
    return result;
}

bool TerminalDockDrop::mainUnderCursor(QWidget* source) const
{
    if (!source || !_window.isVisible() || _window.isMinimized() || QApplication::activeModalWidget())
    {
        return false;
    }
#ifdef Q_OS_WIN
    POINT point;
    if (!GetCursorPos(&point))
    {
        return false;
    }
    const auto sourceId  = reinterpret_cast<HWND>(source->winId());
    const auto mainId    = reinterpret_cast<HWND>(_window.winId());
    const auto previewId = _preview ? reinterpret_cast<HWND>(_preview->winId()) : nullptr;
    const auto captionId = _caption ? reinterpret_cast<HWND>(_caption->winId()) : nullptr;
    for (auto current = GetTopWindow(nullptr); current; current = GetWindow(current, GW_HWNDNEXT))
    {
        if (current == sourceId || current == previewId || current == captionId
            || GetWindow(current, GW_OWNER) == sourceId || !IsWindowVisible(current) || IsIconic(current)
            || (GetWindowLongPtr(current, GWL_EXSTYLE) & WS_EX_TRANSPARENT))
        {
            continue;
        }
        RECT rectangle;
        if (GetWindowRect(current, &rectangle) && PtInRect(&rectangle, point))
        {
            return current == mainId;
        }
    }
#endif
    return false;
}

void TerminalDockDrop::show(const Target& target)
{
    if (target.placement == Placement::None || target.rectangle.isEmpty())
    {
        hide();
        return;
    }
    if (!_preview)
    {
        _preview = new QRubberBand(QRubberBand::Rectangle, &_window);
        _preview->setObjectName("terminalDockPreview");
        _preview->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput
                                 | Qt::WindowDoesNotAcceptFocus);
        _preview->setAttribute(Qt::WA_ShowWithoutActivating);
        _preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    if (!_caption)
    {
        _caption = new QLabel(&_window);
        _caption->setObjectName("terminalDockPreviewCaption");
        _caption->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput
                                 | Qt::WindowDoesNotAcceptFocus);
        _caption->setAttribute(Qt::WA_ShowWithoutActivating);
        _caption->setAttribute(Qt::WA_TransparentForMouseEvents);
        _caption->setTextFormat(Qt::PlainText);
        _caption->setPalette(QToolTip::palette());
        _caption->setAutoFillBackground(true);
        _caption->setFrameStyle(QFrame::StyledPanel);
        _caption->setContentsMargins(8, 4, 8, 4);
    }
    const auto text = target.placement == Placement::Tabs || target.placement == Placement::Tabify ?
                          QCoreApplication::translate("TerminalDockDrop", "Add as tab") :
                      target.placement == Placement::Left ?
                          QCoreApplication::translate("TerminalDockDrop", "Dock left") :
                      target.placement == Placement::Right ?
                          QCoreApplication::translate("TerminalDockDrop", "Dock right") :
                      target.placement == Placement::Above ?
                          QCoreApplication::translate("TerminalDockDrop", "Dock above") :
                          QCoreApplication::translate("TerminalDockDrop", "Dock below");
    if (_caption->text() != text)
    {
        _caption->setText(text);
        _caption->adjustSize();
    }
    const QRect rectangle(_window.terminalDockHost().mapToGlobal(target.rectangle.topLeft()), target.rectangle.size());
    if (_preview->geometry() != rectangle)
    {
        _preview->setGeometry(rectangle);
    }
    if (!_preview->isVisible())
    {
        _preview->show();
        _preview->raise();
    }
    _caption->move(rectangle.center().x() - _caption->width() / 2, rectangle.top() + 8);
    if (!_caption->isVisible())
    {
        _caption->show();
        _caption->raise();
    }
}

void TerminalDockDrop::hide()
{
    if (_caption)
    {
        _caption->hide();
    }
    if (_preview)
    {
        _preview->hide();
    }
}

TerminalDockDrop::Target TerminalDockDrop::_edgeTarget(const QRect&  rectangle,
                                                       const QPoint& point,
                                                       int           band,
                                                       QDockWidget*  dock)
{
    const int left    = point.x() - rectangle.left();
    const int right   = rectangle.right() - point.x();
    const int top     = point.y() - rectangle.top();
    const int bottom  = rectangle.bottom() - point.y();
    const int nearest = std::min({left, right, top, bottom});
    Target    result;
    if (nearest >= band)
    {
        return result;
    }
    result.dock      = dock;
    result.rectangle = rectangle;
    if (nearest == left)
    {
        result.placement = Placement::Left;
        result.rectangle.setWidth(rectangle.width() / 2);
    }
    else if (nearest == right)
    {
        result.placement = Placement::Right;
        result.rectangle.setLeft(rectangle.center().x() + 1);
    }
    else if (nearest == top)
    {
        result.placement = Placement::Above;
        result.rectangle.setHeight(rectangle.height() / 2);
    }
    else
    {
        result.placement = Placement::Below;
        result.rectangle.setTop(rectangle.center().y() + 1);
    }
    return result;
}
