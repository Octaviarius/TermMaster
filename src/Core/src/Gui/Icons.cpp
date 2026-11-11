#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmapCache>
#include <QSvgRenderer>
#include <Termaster/Core/Gui/Icons.h>

static void initializeActionIcons()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(actionIcons);
        Q_INIT_RESOURCE(processorIcons);
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
        _cacheKey = QStringLiteral("Termaster.Icon.")
                  + QString::fromLatin1(QCryptographicHash::hash(_svg, QCryptographicHash::Sha256).toHex());
    }

    QIconEngine* clone() const override
    {
        return new PaletteIconEngine(_svg, _color);
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const auto target  = painter->deviceTransform().mapRect(QRectF(rect));
        const auto aligned = [](qreal value) { return qAbs(value - qRound(value)) < 0.001; };
        if (painter->worldTransform().type() > QTransform::TxTranslate || !aligned(target.x()) || !aligned(target.y())
            || !aligned(target.width()) || !aligned(target.height()))
        {
            _paintVector(painter, rect, mode);
            return;
        }
        const auto image = scaledPixmap(rect.size(), mode, state, painter->device()->devicePixelRatioF());
        painter->drawPixmap(rect, image);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override
    {
        const auto color = _color.isValid() ?
                               _color :
                               QApplication::palette().color(QPalette::Active,
                                                             mode == QIcon::Selected ? QPalette::HighlightedText :
                                                                                       QPalette::ButtonText);
        const auto key   = _cacheKey
                         + QString(".%1.%2x%3.%4.%5")
                               .arg(color.name(QColor::HexRgb))
                               .arg(size.width())
                               .arg(size.height())
                               .arg(scale, 0, 'g', 17)
                               .arg(int(mode));
        QPixmap    result;
        if (QPixmapCache::find(key, &result))
        {
            return result;
        }
        result = QPixmap(size * scale);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        {
            QPainter painter(&result);
            _paintVector(&painter, QRect(QPoint(), size), mode);
        }
        QPixmapCache::insert(key, result);
        return result;
    }

private:
    QByteArray _svg;
    QColor     _color;
    QString    _cacheKey;

    void _paintVector(QPainter* painter, const QRect& rect, QIcon::Mode mode)
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

QIcon Icons::processorBlock(bool composite, bool duplex)
{
    return icon(QString("Processor%1%2").arg(composite ? "Composite" : "Elementary").arg(duplex ? 4 : 2));
}
