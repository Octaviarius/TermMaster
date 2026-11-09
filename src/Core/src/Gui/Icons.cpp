#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <Termaster/Core/Gui/Icons.h>

static void initializeActionIcons()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(actionIcons);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace
{
class PaletteIconEngine final : public QIconEngine
{
public:
    explicit PaletteIconEngine(QByteArray svg, QColor color) : _svg(std::move(svg)), _color(color)
    {
    }

    QIconEngine* clone() const override
    {
        return new PaletteIconEngine(_svg, _color);
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        const auto palette = QApplication::palette();
        const auto role    = mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText;
        auto       svg     = _svg;
        const auto color   = _color.isValid() ? _color : palette.color(QPalette::Active, role);
        svg.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
        QSvgRenderer renderer(svg);
        const auto   side = qMin(rect.width(), rect.height());
        const QRectF target(rect.x() + (rect.width() - side) / 2.0,
                            rect.y() + (rect.height() - side) / 2.0,
                            side,
                            side);
        painter->save();
        if (mode == QIcon::Disabled)
        {
            painter->setOpacity(painter->opacity() * 0.15);
        }
        painter->setRenderHint(QPainter::Antialiasing);
        renderer.render(painter, target);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap result(size * scale);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

private:
    QByteArray _svg;
    QColor     _color;
};
} // namespace

QIcon Icons::icon(const QString& name, const QColor& color)
{
    initializeActionIcons();
    QFile file(":/Termaster/Core/Icons/" + name + ".svg");
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QIcon(new PaletteIconEngine(file.readAll(), color));
}
