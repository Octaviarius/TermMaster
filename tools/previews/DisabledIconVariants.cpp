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
    QImage        image(1100, 650, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#f6f7f9"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    auto text = [&](int x, int y, const QString& value, int size, const QColor& color, bool bold = false) {
        painter.setFont(QFont("Segoe UI", size, bold ? QFont::DemiBold : QFont::Normal));
        painter.setPen(color);
        painter.drawText(x, y, value);
    };

    text(38, 45, QString::fromUtf8("Недоступные команды: варианты акцента"), 23, "#20242a", true);
    text(38, 76, QString::fromUtf8("Исходные значки Lucide · размер 24 px · без изменения формы"), 11, "#5c6470");

    const QStringList titles       = {QString::fromUtf8("Сейчас"),
                                      QString::fromUtf8("1. Более тёмный серый"),
                                      QString::fromUtf8("2. Серая подложка"),
                                      QString::fromUtf8("3. Пунктирная рамка")};
    const QStringList descriptions = {QString::fromUtf8("Бледный значок на светлом фоне"),
                                      QString::fromUtf8("Больше контраста, привычный вид"),
                                      QString::fromUtf8("Недоступность видна отдельным фоном"),
                                      QString::fromUtf8("Максимально явное отличие состояния")};
    const QStringList names        = {"play", "square", "rotate-cw", "eraser", "sliders-horizontal"};
    for (int row = 0; row < 4; ++row)
    {
        const int y = 104 + row * 119;
        painter.setPen(QPen(QColor(row == 2 ? "#9aa5b1" : "#dce0e5"), row == 2 ? 2 : 1));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(30, y, 1040, 104), 10, 10);
        text(49, y + 31, titles[row], 13, "#242a31", true);
        text(49, y + 59, descriptions[row], 10, "#606875");
        if (row == 2)
        {
            text(49, y + 85, QString::fromUtf8("Рекомендуемый вариант"), 10, "#475569", true);
        }

        painter.setPen(QColor("#d8dadd"));
        painter.setBrush(QColor("#f0f0f0"));
        painter.drawRoundedRect(QRectF(610, y + 16, 275, 51), 4, 4);
        for (int index = 0; index < names.size(); ++index)
        {
            const int  x        = 620 + index * 51;
            const bool disabled = index == 1;
            if (disabled && row >= 2)
            {
                QPen border(QColor(row == 2 ? "#a7adb4" : "#858d96"));
                if (row == 3)
                {
                    border.setStyle(Qt::DashLine);
                }
                painter.setPen(border);
                painter.setBrush(QColor(row == 2 ? "#dce0e4" : "#eceef0"));
                painter.drawRoundedRect(QRectF(x + 1, y + 23, 36, 36), 4, 4);
            }
            QFile svg(root + "/components/lucide/icons/" + names[index] + ".svg");
            if (!svg.open(QIODevice::ReadOnly))
            {
                return 2;
            }
            QByteArray data = svg.readAll();
            data.replace("currentColor", disabled ? (row == 0 ? "#999999" : "#686868") : "#171717");
            QSvgRenderer renderer(data);
            renderer.render(&painter, QRectF(x + 7, y + 29, 24, 24));
        }
        text(660, y + 89, QString::fromUtf8("↑ Недоступно"), 9, "#646b75");
        text(908, y + 45, QString::fromUtf8("Стоп"), 11, "#505762");
        text(908, y + 66, QString::fromUtf8("отключён"), 10, "#707783");
    }
    text(38,
         615,
         QString::fromUtf8(
             "Статический эскиз: остальные команды доступны. Цвет и рамка не меняют доступность действий."),
         10,
         "#666f7b");
    painter.end();
    return image.save(QString::fromLocal8Bit(argv[2])) ? 0 : 3;
}
