#include "Gui/Settings/FavoritesEditor.h"

#include <QAbstractItemModel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>

class FavoritesEditorTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void preservesUnknownFieldsAndEditsDraft()
    {
        FavoritesEditor editor;
        QSignalSpy      changed(&editor, &SettingsFieldEditor::valueChanged);
        editor.setValue(QVariantList {
            QVariantMap {{"name", "Reset"},
                         {"text", "RESET\n"},
                         {"terminator", "cr"},
                         {"customTerminator", ""},
                         {"future", QVariantMap {{"keep", 42}}}}
        });
        QCOMPARE(changed.count(), 0);
        QVERIFY(editor.hasAcceptableInput());
        auto* table = editor.findChild<QTableView*>("favoritesTable");
        QVERIFY(table->model()->setData(table->model()->index(0, 1), "OTHER\nCOMMAND", Qt::EditRole));
        QCOMPARE(changed.count(), 1);
        auto entry = editor.value().toList().first().toMap();
        QCOMPARE(entry.value("text").toString(), QString("OTHER\nCOMMAND"));
        QCOMPARE(entry.value("future").toMap().value("keep").toInt(), 42);
        editor.findChild<QPushButton*>("favoriteAdd")->click();
        QCOMPARE(editor.value().toList().size(), 2);
        QVERIFY(!editor.hasAcceptableInput());
        QVERIFY(editor.inputError().contains("Name"));
        editor.findChild<QPushButton*>("favoriteDelete")->click();
        QCOMPARE(editor.value().toList().size(), 1);
        QVERIFY(editor.hasAcceptableInput());
    }

    Q_SLOT void validatesBoundsAndEscapes()
    {
        FavoritesEditor editor;
        QVariantMap     entry {
            {            "name", "Command"},
            {            "text",      "AT"},
            {      "terminator",  "custom"},
            {"customTerminator",   "\\xZZ"}
        };
        editor.setValue(QVariantList {entry});
        QVERIFY(!editor.hasAcceptableInput());
        QVERIFY(editor.inputError().contains("invalid digits"));
        entry["customTerminator"] = "\\0";
        editor.setValue(QVariantList {entry});
        QVERIFY(editor.hasAcceptableInput());
        entry["text"] = QString(16385, 'x');
        editor.setValue(QVariantList {entry});
        QVERIFY(editor.inputError().contains("16,384"));
        entry["text"] = "AT";
        entry["name"] = QString(129, 'n');
        editor.setValue(QVariantList {entry});
        QVERIFY(editor.inputError().contains("128"));
        entry["name"] = "Command";
        QVariantList many;
        for (int i = 0; i < 101; ++i)
        {
            many.append(entry);
        }
        editor.setValue(many);
        QVERIFY(editor.inputError().contains("100 command"));
        entry["future"] = QString(1024 * 1024, 'x');
        editor.setValue(QVariantList {entry});
        QVERIFY(editor.inputError().contains("1 MiB"));
    }
};

QTEST_MAIN(FavoritesEditorTests)
#include "FavoritesEditorTests.moc"
