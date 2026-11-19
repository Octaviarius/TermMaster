#include "Gui/Processing/CodeflowEditorDialog.h"
#include "Gui/Processing/LineflowCanvas.h"
#include "Gui/Processing/LineflowEditorDialog.h"

#include <QAction>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <Termaster/Core/Processing/Codeflow.h>
#include <Termaster/Core/Processing/NativeProcessors.h>

namespace
{
QVariantMap definitions(int channels)
{
    return {
        {"root",
         QVariantMap {{"id", "root"},
                      {"name", "Inline fixture"},
                      {"kind", "lineflow"},
                      {"source", QVariantMap {{"channels", channels}, {"nodes", QVariantList {}}}}}}
    };
}
QAction* action(CodeflowEditorDialog& dialog, const QString& text)
{
    for (auto* item : dialog.findChildren<QAction*>())
    {
        if (item->text().remove('&') == text)
        {
            return item;
        }
    }
    return nullptr;
}
bool diagnosticContains(CodeflowEditorDialog& dialog, const QString& text)
{
    for (auto* edit : dialog.findChildren<QTextEdit*>())
    {
        if (edit->toPlainText().contains(text, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}
CodeflowEditorDialog* openEditor(LineflowEditorDialog& dialog, int row)
{
    auto* canvas = dialog.findChild<LineflowCanvas*>("lineflowCanvas");
    canvas->setCurrentIndex(dialog.model().index(row, 0));
    auto* button = dialog.findChild<QPushButton*>("editPythonCode");
    if (!button)
    {
        return nullptr;
    }
    button->click();
    return dialog.findChild<CodeflowEditorDialog*>("inlinePythonCodeEditor");
}
} // namespace

class InlinePythonCodeTests final : public QObject
{
    Q_OBJECT
private slots:
    void occurrenceDraftAndParentPublication_data()
    {
        QTest::addColumn<int>("channels");
        QTest::addColumn<bool>("commitParent");
        QTest::newRow("simplex-apply") << 1 << true;
        QTest::newRow("duplex-apply") << 2 << true;
        QTest::newRow("simplex-cancel") << 1 << false;
        QTest::newRow("duplex-cancel") << 2 << false;
    }

    void occurrenceDraftAndParentPublication()
    {
        QFETCH(int, channels);
        QFETCH(bool, commitParent);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previousDirectory = QDir::currentPath();
        QVERIFY(QDir::setCurrent(directory.path()));
        const auto        restoreDirectory = qScopeGuard([&] { QDir::setCurrent(previousDirectory); });
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        Codeflow::registerProvider(registry);
        SettingsWidgetRegistry widgets;
        auto                   persisted    = definitions(channels);
        int                    applications = 0;
        LineflowEditorDialog   dialog(persisted,
                                      registry,
                                      widgets,
                                      [&](const auto& baseline, const auto& candidate, const auto&, QString*) {
                                        if (baseline != persisted)
                                        {
                                            return false;
                                        }
                                        persisted = candidate;
                                        ++applications;
                                        return true;
                                      });
        dialog.show();
        QVERIFY(dialog.selectBlock("root"));
        const QString type    = channels == 1 ? "native.python-code" : "native.duplex-python-code";
        auto*         catalog = dialog.findChild<QTreeWidget*>("processorCatalog");
        QVERIFY(catalog);
        QTreeWidgetItem* paletteItem = nullptr;
        for (QTreeWidgetItemIterator it(catalog); *it; ++it)
        {
            if ((*it)->data(0, Qt::UserRole).toString() == type)
            {
                paletteItem = *it;
                break;
            }
        }
        QVERIFY(paletteItem);
        QVERIFY(paletteItem->parent());
        QCOMPARE(paletteItem->parent()->text(0), QString("General"));
        QVERIFY(paletteItem->parent()->parent());
        QCOMPARE(paletteItem->parent()->parent()->text(0), QString("Native"));

        QMimeData drag;
        drag.setData("application/x-termaster-processor", type.toUtf8());
        QVERIFY(dialog.model().dropAt(&drag, 0, channels == 2 ? "duplex" : "rx"));
        QString    error;
        const auto second = dialog.model().insertNode("root", -1, type, &error);
        QVERIFY2(!second.isEmpty(), qPrintable(error));
        const auto first = dialog.model().index(0, 0).data(LineflowEditorModel::NodeRole).toString();
        QCOMPARE(dialog.model().index(0, 0).data(LineflowEditorModel::CompositionKindRole).toString(),
                 QString("python-code"));
        QCOMPARE(dialog.model().index(0, 0).data(LineflowEditorModel::InputCountRole).toInt(), channels);
        const auto secondBefore   = dialog.model().node("root", second);
        const auto registryBefore = registry.types().size();
        auto*      editor         = openEditor(dialog, 0);
        QVERIFY(editor);
        QVERIFY(!editor->findChild<QLineEdit*>("codeflowFile")->isVisible());
        QCOMPARE(openEditor(dialog, 0), editor);
        QCOMPARE(dialog.findChildren<CodeflowEditorDialog*>("inlinePythonCodeEditor").size(), 1);
        auto code = Codeflow::templateSource(channels);
        code.replace("    prefix =", "    count = Property(int, default=7)\n    prefix =");
        editor->findChild<QPlainTextEdit*>("codeflowSource")->setPlainText(code);
        QVERIFY(action(*editor, "Check"));
        action(*editor, "Check")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(editor->findChild<SettingsFormModel*>(), 10000);
        auto* form = editor->findChild<SettingsFormModel*>();
        form->setValue("prefix", "inline-prefix");
        form->setValue("count", 13);
        // Selecting another occurrence must not redirect the open editor's callback.
        dialog.findChild<LineflowCanvas*>("lineflowCanvas")->setCurrentIndex(dialog.model().index(1, 0));
        QVERIFY(action(*editor, "Save"));
        action(*editor, "Save")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(dialog.model()
                                      .node("root", first)
                                      .value("configuration")
                                      .toMap()
                                      .value("parameters")
                                      .toMap()
                                      .value("prefix")
                                      .toString(),
                                  QString("inline-prefix"),
                                  10000);
        const auto configuration = dialog.model().node("root", first).value("configuration").toMap();
        QCOMPARE(configuration.value("parameters").toMap().value("count").toInt(), 13);
        QCOMPARE(configuration.value("source").toMap().value("code").toString(), code);
        QVERIFY(!configuration.value("source").toMap().contains("file"));
        QCOMPARE(dialog.model().node("root", second), secondBefore);
        QCOMPARE(dialog.model().definitions().size(), 1);
        QCOMPARE(registry.types().size(), registryBefore);
        QCOMPARE(applications, 0);
        QCOMPARE(persisted, definitions(channels));
        QVERIFY(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

        QPointer<CodeflowEditorDialog> closing(editor);
        editor->close();
        QTRY_VERIFY(closing.isNull());
        editor = openEditor(dialog, 0);
        QVERIFY(editor);
        action(*editor, "Check")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(editor->findChild<SettingsFormModel*>(), 10000);
        form = editor->findChild<SettingsFormModel*>();
        QCOMPARE(form->value("prefix").toString(), QString("inline-prefix"));
        QCOMPARE(form->value("count").toInt(), 13);
        const auto capture = qEnvironmentVariable("TERMASTER_INLINE_PYTHON_CAPTURE");
        if (channels == 2 && commitParent && !capture.isEmpty())
        {
            QVERIFY(QDir().mkpath(capture));
            catalog->expandAll();
            QTreeWidgetItem* visibleType = nullptr;
            for (QTreeWidgetItemIterator it(catalog); *it; ++it)
            {
                if ((*it)->data(0, Qt::UserRole).toString() == type && (*it)->parent()
                    && (*it)->parent()->text(0) == "General")
                {
                    visibleType = *it;
                    break;
                }
            }
            QVERIFY(visibleType);
            catalog->scrollToItem(visibleType, QAbstractItemView::PositionAtCenter);
            dialog.raise();
            dialog.activateWindow();
            QTest::qWait(100);
            QVERIFY(dialog.grab().save(QDir(capture).filePath("InlinePythonCodeDiagram.png")));
            editor->raise();
            editor->activateWindow();
            QTest::qWait(100);
            QVERIFY(editor->grab().save(QDir(capture).filePath("InlinePythonCodeEditor.png")));
        }
        if (commitParent)
        {
            QVERIFY2(dialog.applyDraft(&error), qPrintable(error));
            QCOMPARE(applications, 1);
            QCOMPARE(persisted, dialog.model().definitions());
        }
        else
        {
            dialog.reject();
            QCOMPARE(applications, 0);
            QCOMPARE(persisted, definitions(channels));
        }
        QCOMPARE(persisted.size(), 1);
        QVERIFY(QDir(directory.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    }

    void changedOccurrenceRejectsStaleEditor_data()
    {
        QTest::addColumn<bool>("rename");
        QTest::newRow("renamed") << true;
        QTest::newRow("configuration-replaced") << false;
    }

    void changedOccurrenceRejectsStaleEditor()
    {
        QFETCH(bool, rename);
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "native");
        Codeflow::registerProvider(registry);
        SettingsWidgetRegistry widgets;
        int                    applications = 0;
        const auto             original     = definitions(1);
        LineflowEditorDialog dialog(original, registry, widgets, [&](const auto&, const auto&, const auto&, QString*) {
            ++applications;
            return true;
        });
        dialog.show();
        QVERIFY(dialog.selectBlock("root"));
        QString    error;
        const auto first = dialog.model().insertNode("root", -1, "native.python-code", &error);
        QVERIFY2(!first.isEmpty(), qPrintable(error));
        auto* editor = openEditor(dialog, 0);
        QVERIFY(editor);
        editor->findChild<QPlainTextEdit*>("codeflowSource")
            ->setPlainText(Codeflow::templateSource(1) + "\n# stale draft\n");
        if (rename)
        {
            QVERIFY(dialog.model().renameNode("root", first, "Renamed Python", &error));
        }
        else
        {
            auto concurrent = dialog.model().node("root", first).value("configuration").toMap();
            concurrent.insert("concurrentMarker", true);
            QVERIFY(dialog.model().setNodeConfiguration("root", first, concurrent, &error));
        }
        const auto beforeApply = dialog.model().definitions();
        action(*editor, "Apply")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(diagnosticContains(*editor, "removed, renamed or changed"), 10000);
        QCOMPARE(dialog.model().definitions(), beforeApply);
        QCOMPARE(applications, 0);
        dialog.reject();
        QCOMPARE(applications, 0);
    }
};

QTEST_MAIN(InlinePythonCodeTests)
#include "InlinePythonCodeTests.moc"
