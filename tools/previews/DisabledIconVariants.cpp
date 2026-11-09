#include <QApplication>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

/** Standalone visual proposal; does not change application icons or action states. */
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc != 3)
    {
        return 1;
    }

    const QString root = QString::fromLocal8Bit(argv[1]);
    QImage        image(1100, 900, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#f6f7f9"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    auto text = [&](int x, int y, const QString& value, int size, const QColor& color, bool bold = false) {
        painter.setFont(QFont("Segoe UI", size, bold ? QFont::DemiBold : QFont::Normal));
        painter.setPen(color);
        painter.drawText(x, y, value);
    };

    text(38, 45, QString::fromUtf8("Недоступные команды: степень блеклости"), 23, "#20242a", true);
    text(38, 76, QString::fromUtf8("Исходные значки Lucide · размер 24 px · без изменения формы"), 11, "#5c6470");

    const QStringList titles       = {QString::fromUtf8("Образец — без ослабления"),
                                      QString::fromUtf8("Блеклость 10% — непрозрачность 90%"),
                                      QString::fromUtf8("Блеклость 25% — непрозрачность 75%"),
                                      QString::fromUtf8("Блеклость 50% — непрозрачность 50%"),
                                      QString::fromUtf8("Блеклость 75% — непрозрачность 25%"),
                                      QString::fromUtf8("Блеклость 90% — непрозрачность 10%")};
    const QStringList descriptions = {QString::fromUtf8("Непрозрачность значка «Стоп»: 100%"),
                                      QString::fromUtf8("Небольшое ослабление чёрного значка"),
                                      QString::fromUtf8("Умеренное ослабление чёрного значка"),
                                      QString::fromUtf8("Заметное ослабление чёрного значка"),
                                      QString::fromUtf8("Сильное ослабление чёрного значка"),
                                      QString::fromUtf8("Максимальное ослабление в сравнении")};
    const double      opacity[]    = {1.0, 0.9, 0.75, 0.5, 0.25, 0.1};
    const QStringList names        = {"play", "square", "rotate-cw", "eraser", "sliders-horizontal"};
    for (int row = 0; row < titles.size(); ++row)
    {
        const int y = 104 + row * 119;
        painter.setPen(QPen(QColor("#dce0e5"), 1));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(30, y, 1040, 104), 10, 10);
        text(49, y + 31, titles[row], 13, "#242a31", true);
        text(49, y + 59, descriptions[row], 10, "#606875");

        painter.setPen(QColor("#d8dadd"));
        painter.setBrush(QColor("#f0f0f0"));
        painter.drawRoundedRect(QRectF(610, y + 16, 275, 51), 4, 4);
        for (int index = 0; index < names.size(); ++index)
        {
            const int  x        = 620 + index * 51;
            const bool disabled = index == 1;
            QFile      svg(root + "/components/lucide/icons/" + names[index] + ".svg");
            if (!svg.open(QIODevice::ReadOnly))
            {
                return 2;
            }
            QByteArray data = svg.readAll();
            data.replace("currentColor", "#000000");
            QSvgRenderer renderer(data);
            painter.setOpacity(disabled ? opacity[row] : 1.0);
            renderer.render(&painter, QRectF(x + 7, y + 29, 24, 24));
            painter.setOpacity(1.0);
        }
        text(660, y + 89, QString::fromUtf8("↑ Недоступно"), 9, "#646b75");
        text(908, y + 45, QString::fromUtf8("Стоп"), 11, "#505762");
        text(908, y + 66, QString::fromUtf8("отключён"), 10, "#707783");
    }
    text(38,
         865,
         QString::fromUtf8(
             "Ослаблен только «Стоп». Остальные значки: чёрный, 100%. Фон и форма одинаковы во всех строках."),
         10,
         "#666f7b");
    painter.end();
    return image.save(QString::fromLocal8Bit(argv[2])) ? 0 : 3;
}
