#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScopeGuard>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QTest>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/SelectionIconDelegate.h>
#include <cmath>
#include <memory>

namespace
{
class IconProbeDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::initStyleOption;
};
QImage renderRow(const QStyledItemDelegate&  delegate,
                 const QStyleOptionViewItem& option,
                 const QModelIndex&          index,
                 qreal                       dpr)
{
    QImage result(option.rect.size() * dpr, QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(dpr);
    result.fill(option.palette.color(QPalette::Base));
    QPainter painter(&result);
    delegate.paint(&painter, option, index);
    return result;
}
qreal iconLuminance(const QColor& color)
{
    const auto channel = [](qreal value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}
int contrastingIconPixels(const QImage& image, const QRect& decoration, qreal dpr)
{
    const auto  background          = image.pixelColor(image.width() - 5, image.height() / 2);
    const auto  backgroundLuminance = iconLuminance(background);
    int         result              = 0;
    const QRect pixels(qRound(decoration.x() * dpr),
                       qRound(decoration.y() * dpr),
                       qRound(decoration.width() * dpr),
                       qRound(decoration.height() * dpr));
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
    {
        for (int x = pixels.left(); x <= pixels.right(); ++x)
        {
            const auto value = iconLuminance(image.pixelColor(x, y));
            const auto ratio = (qMax(value, backgroundLuminance) + 0.05) / (qMin(value, backgroundLuminance) + 0.05);
            if (ratio >= 3.0)
            {
                ++result;
            }
        }
    }
    return result;
}
} // namespace

class IconTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void cachedPaintingMatchesVectorAtFractionalOrigins()
    {
        const auto icon = Icons::icon("square-terminal", QColor(Qt::black));
        QFile      source(":/Termaster/Core/Icons/square-terminal.svg");
        QVERIFY(source.open(QIODevice::ReadOnly));
        auto svg = source.readAll();
        svg.replace("currentColor", "#000000");
        QSvgRenderer renderer(svg);
        QVERIFY(renderer.isValid());
        for (const qreal dpr : {1.0, 1.25, 2.0})
        {
            for (const qreal shift : {0.0, 0.5})
            {
                QImage expected(QSize(48, 48) * dpr, QImage::Format_ARGB32_Premultiplied);
                expected.setDevicePixelRatio(dpr);
                expected.fill(Qt::transparent);
                auto        actual = expected;
                const QRect target(5, 7, 24, 24);
                {
                    QPainter painter(&expected);
                    painter.translate(shift, shift);
                    painter.setRenderHint(QPainter::Antialiasing);
                    renderer.render(&painter, target);
                }
                for (int repeat = 0; repeat < 2; ++repeat)
                {
                    actual.fill(Qt::transparent);
                    QPainter painter(&actual);
                    painter.translate(shift, shift);
                    icon.paint(&painter, target);
                    painter.end();
                    QCOMPARE(actual, expected);
                }
            }
        }
    }
    Q_SLOT void followsPaletteAndDeviceScale()
    {
        const auto original = QApplication::palette();
        const auto restore  = qScopeGuard([&] { QApplication::setPalette(original); });
        const auto icon     = Icons::icon("square-terminal");
        QVERIFY(!icon.isNull());
        QVERIFY(Icons::icon("missing-icon").isNull());
        for (const auto color : {QColor("#263245"), QColor("#e1e9f7")})
        {
            auto palette = original;
            palette.setColor(QPalette::Active, QPalette::ButtonText, color);
            palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#788493"));
            palette.setColor(QPalette::Active, QPalette::HighlightedText, QColor("#fefefe"));
            QApplication::setPalette(palette);
            for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Disabled, QIcon::Selected})
            {
                const auto expected = mode == QIcon::Selected ? QColor("#fefefe") : color;
                for (const auto dpr : {1.0, 1.5, 2.0})
                {
                    for (const auto size : {16, 24})
                    {
                        const auto pixmap = icon.pixmap(QSize(size, size), dpr, mode);
                        QCOMPARE(pixmap.size(), QSize(qRound(size * dpr), qRound(size * dpr)));
                        QCOMPARE(pixmap.devicePixelRatio(), dpr);
                        const auto image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
                        bool       found = false;
                        for (int y = 0; y < image.height(); ++y)
                        {
                            for (int x = 0; x < image.width(); ++x)
                            {
                                const auto pixel = image.pixelColor(x, y);
                                if (mode == QIcon::Disabled ?
                                        qAbs(pixel.alpha() - 38) <= 1 && qAbs(pixel.red() - expected.red()) <= 4
                                            && qAbs(pixel.green() - expected.green()) <= 4
                                            && qAbs(pixel.blue() - expected.blue()) <= 4 :
                                        pixel.alpha() > 250 && pixel.rgb() == expected.rgb())
                                {
                                    found = true;
                                }
                            }
                        }
                        QVERIFY(found);
                        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
                    }
                }
            }
        }
    }

    Q_SLOT void disabledOpacity()
    {
        const auto original = QApplication::palette();
        const auto restore  = qScopeGuard([&] { QApplication::setPalette(original); });
        for (const auto tint : {QColor(), QColor("#172333")})
        {
            const auto icon = Icons::icon("square-terminal", tint);
            for (const auto foreground : {QColor("#263245"), QColor("#e1e9f7")})
            {
                auto palette = original;
                palette.setColor(QPalette::Active, QPalette::ButtonText, foreground);
                palette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::red);
                QApplication::setPalette(palette);
                for (const auto dpr : {1.0, 1.5, 2.0})
                {
                    const auto normal   = icon.pixmap(QSize(24, 24), dpr, QIcon::Normal).toImage();
                    const auto disabled = icon.pixmap(QSize(24, 24), dpr, QIcon::Disabled).toImage();
                    int        checked  = 0;
                    for (int y = 0; y < normal.height(); ++y)
                    {
                        for (int x = 0; x < normal.width(); ++x)
                        {
                            const auto source = normal.pixelColor(x, y);
                            const auto faded  = disabled.pixelColor(x, y);
                            if (source.alpha() == 0)
                            {
                                QCOMPARE(faded.alpha(), 0);
                            }
                            if (source.alpha() == 255 && qAbs(faded.alpha() - 38) <= 1)
                            {
                                for (const auto background : {QColor("#ffffff"), QColor("#202733")})
                                {
                                    const auto composite = [&](const QColor& value) {
                                        QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
                                        image.fill(background);
                                        QPainter painter(&image);
                                        painter.fillRect(image.rect(), value);
                                        painter.end();
                                        return image.pixelColor(0, 0);
                                    };
                                    auto expected = source;
                                    expected.setAlphaF(0.15f);
                                    const auto actualComposite   = composite(faded);
                                    const auto expectedComposite = composite(expected);
                                    QVERIFY(qAbs(actualComposite.red() - expectedComposite.red()) <= 1);
                                    QVERIFY(qAbs(actualComposite.green() - expectedComposite.green()) <= 1);
                                    QVERIFY(qAbs(actualComposite.blue() - expectedComposite.blue()) <= 1);
                                }
                                ++checked;
                            }
                        }
                    }
                    QVERIFY(checked > 10);
                }
            }
        }
    }

    Q_SLOT void selectedItemIconsUseActualRowContrast()
    {
        const auto original           = QApplication::palette();
        const auto restore            = qScopeGuard([&] { QApplication::setPalette(original); });
        auto       applicationPalette = original;
        applicationPalette.setColor(QPalette::Active, QPalette::HighlightedText, Qt::white);
        QApplication::setPalette(applicationPalette);
        auto style = std::unique_ptr<QStyle>(QStyleFactory::create("Fusion"));
        QVERIFY(style);
        QStandardItemModel model;
        auto*              item = new QStandardItem(Icons::icon("square-terminal"), "Processor text");
        model.appendRow(item);
        QTreeView view;
        view.setStyle(style.get());
        view.setModel(&model);
        IconProbeDelegate     baseline;
        SelectionIconDelegate corrected;
        for (int state = 0; state < 3; ++state)
        {
            QStyleOptionViewItem option;
            option.initFrom(&view);
            option.widget                 = &view;
            option.rect                   = QRect(0, 0, 300, 36);
            option.decorationSize         = QSize(20, 20);
            option.showDecorationSelected = true;
            option.state                  = QStyle::State_Enabled | QStyle::State_Selected;
            if (state == 1)
            {
                option.state |= QStyle::State_Active | QStyle::State_HasFocus;
            }
            if (state == 2)
            {
                option.state |= QStyle::State_Active;
            }
            const bool dark = state == 1;
            option.palette.setColor(QPalette::Base, dark ? QColor("#202733") : QColor("#ffffff"));
            option.palette.setColor(QPalette::Text, dark ? QColor("#e2e8f1") : QColor("#172333"));
            option.palette.setColor(QPalette::Highlight, dark ? QColor("#354c69") : QColor("#dedede"));
            option.palette.setColor(QPalette::HighlightedText, state == 0 ? QColor("#172333") : QColor(Qt::white));
            auto initialized = option;
            baseline.initStyleOption(&initialized, model.index(0, 0));
            const auto decoration = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &initialized, &view);
            QVERIFY(!decoration.isEmpty());
            for (const auto dpr : {1.0, 2.0})
            {
                const auto before = renderRow(baseline, option, model.index(0, 0), dpr);
                const auto after  = renderRow(corrected, option, model.index(0, 0), dpr);
                QVERIFY(contrastingIconPixels(after, decoration, dpr) > 10);
                if (!dark)
                {
                    QVERIFY(contrastingIconPixels(after, decoration, dpr)
                            > contrastingIconPixels(before, decoration, dpr));
                }
                // The regular Qt style still renders every text/background/focus pixel.
                const auto excluded = decoration.adjusted(-2, -2, 2, 2);
                for (int y = 0; y < before.height(); ++y)
                {
                    for (int x = 0; x < before.width(); ++x)
                    {
                        if (!excluded.contains(QPoint(qFloor(x / dpr), qFloor(y / dpr))))
                        {
                            QCOMPARE(after.pixel(x, y), before.pixel(x, y));
                        }
                    }
                }
                for (const auto flags : {QStyle::State_Enabled, QStyle::State_Selected, QStyle::State_None})
                {
                    auto unchanged  = option;
                    unchanged.state = flags;
                    QCOMPARE(renderRow(corrected, unchanged, model.index(0, 0), dpr),
                             renderRow(baseline, unchanged, model.index(0, 0), dpr));
                }
            }
        }
    }

    Q_SLOT void nativeSelectedItemCapture()
    {
        const auto destination = qEnvironmentVariable("TERMASTER_SELECTION_CAPTURE");
        if (destination.isEmpty())
        {
            QSKIP("Selection capture is opt-in");
        }
        QVERIFY(QDir().mkpath(destination));
        QWidget window;
        window.setFixedSize(420, 180);
        auto* layout = new QVBoxLayout(&window);
        auto* tree   = new QTreeWidget(&window);
        tree->setHeaderHidden(true);
        tree->setIconSize(QSize(20, 20));
        tree->setItemDelegate(new SelectionIconDelegate(tree));
        auto* first = new QTreeWidgetItem(tree, QStringList {"Selected processor"});
        first->setIcon(0, Icons::icon("square-terminal"));
        auto* second = new QTreeWidgetItem(tree, QStringList {"Another processor"});
        second->setIcon(0, Icons::icon("sliders-horizontal"));
        auto* otherFocus = new QLineEdit("Focus target", &window);
        layout->addWidget(tree);
        layout->addWidget(otherFocus);
        auto palette = window.palette();
        palette.setColor(QPalette::Window, Qt::white);
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::Text, QColor("#172333"));
        palette.setColor(QPalette::Active, QPalette::Highlight, QColor("#3065a0"));
        palette.setColor(QPalette::Active, QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Inactive, QPalette::Highlight, QColor("#dedede"));
        palette.setColor(QPalette::Inactive, QPalette::HighlightedText, QColor("#172333"));
        window.setPalette(palette);
        tree->setCurrentItem(first);
        window.show();
        window.activateWindow();
        tree->setFocus();
        QTest::qWait(40);
        QVERIFY(window.grab().save(QDir(destination).filePath("SelectionActive.png")));
        otherFocus->setFocus();
        QTest::qWait(40);
        QVERIFY(window.grab().save(QDir(destination).filePath("SelectionUnfocused.png")));
    }

    Q_SLOT void nativeToolbarAndMenu()
    {
        const auto original = QApplication::palette();
        const auto restore  = qScopeGuard([&] { QApplication::setPalette(original); });
        const auto capture  = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
        for (const auto dark : {false, true})
        {
            auto palette = original;
            if (dark)
            {
                palette.setColor(QPalette::Window, QColor("#202733"));
                palette.setColor(QPalette::Button, QColor("#202733"));
                palette.setColor(QPalette::Base, QColor("#161b24"));
                palette.setColor(QPalette::WindowText, QColor("#e2e8f1"));
                palette.setColor(QPalette::Text, QColor("#e2e8f1"));
                palette.setColor(QPalette::ButtonText, QColor("#e2e8f1"));
                palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#758196"));
                palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#758196"));
                palette.setColor(QPalette::Highlight, QColor("#354c69"));
                palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
            }
            QApplication::setPalette(palette);
            QWidget window;
            auto*   layout  = new QVBoxLayout(&window);
            auto*   toolbar = new QToolBar(&window);
            toolbar->setIconSize(QSize(24, 24));
            toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
            auto* menu = new QMenu(&window);
            menu->setWindowFlags(Qt::Widget);
            const QList<QPair<QString, QString>> actions {
                {   "square-terminal",  "New console"},
                {             "cable",   "New serial"},
                {           "network",   "New socket"},
                {              "plug",      "Connect"},
                {            "unplug",   "Disconnect"},
                {         "rotate-cw",      "Restart"},
                {            "eraser", "Clear screen"},
                {"sliders-horizontal",     "Settings"},
                {                 "x",        "Close"}
            };
            for (int index = 0; index < actions.size(); ++index)
            {
                if (index == 3 || index == 6)
                {
                    toolbar->addSeparator();
                    menu->addSeparator();
                }
                const auto& item   = actions[index];
                auto*       action = toolbar->addAction(Icons::icon(item.first), item.second);
                action->setToolTip(item.second);
                action->setEnabled(index != 4);
                menu->addAction(action);
                QVERIFY(!action->icon().pixmap(16, 16).isNull());
            }
            layout->addWidget(toolbar);
            layout->addWidget(menu, 0, Qt::AlignLeft);
            window.adjustSize();
            window.show();
            QTest::qWait(30);
            menu->setActiveAction(menu->actions().at(4));
            if (!capture.isEmpty())
            {
                QVERIFY(QDir().mkpath(capture));
                QVERIFY(window.grab().save(QDir(capture).filePath(dark ? "IconsDark.png" : "IconsLight.png")));
            }
        }
    }
};

QTEST_MAIN(IconTests)
#include "IconTests.moc"
