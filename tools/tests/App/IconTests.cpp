#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QScopeGuard>
#include <QTest>
#include <QToolBar>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>

class IconTests final : public QObject
{
    Q_OBJECT
private:
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
                const auto expected = mode == QIcon::Disabled ? QColor("#788493") :
                                      mode == QIcon::Selected ? QColor("#fefefe") :
                                                                color;
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
                                if (pixel.alpha() > 250 && pixel.rgb() == expected.rgb())
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
