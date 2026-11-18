#include "Gui/Processing/CodeflowEditorDialog.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QtTest>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Processing/Codeflow.h>

namespace
{
QVariantMap fixture(const QString& code = Codeflow::templateSource(1))
{
    return {
        {    "id","fixture-codeflow"                  },
        {  "name",            "Unicode codec"},
        {  "kind",                 "codeflow"},
        {"source",
         QVariantMap {{"code", code},
         {"className", "Codeflow"},
         {"python", Codeflow::defaultPython()},
         {"startupTimeoutMs", 2500},
         {"timeoutMs", 1500},
         {"stopTimeoutMs", 100}}             }
    };
}
QPlainTextEdit* sourceEditor(CodeflowEditorDialog& dialog)
{
    if (auto* editor = dialog.findChild<QPlainTextEdit*>("codeflowSource"))
    {
        return editor;
    }
    for (auto* editor : dialog.findChildren<QPlainTextEdit*>())
    {
        if (!editor->isReadOnly())
        {
            return editor;
        }
    }
    return nullptr;
}
QLineEdit* field(CodeflowEditorDialog& dialog, const QString& name, int index)
{
    if (auto* edit = dialog.findChild<QLineEdit*>(name))
    {
        return edit;
    }
    const auto edits = dialog.findChildren<QLineEdit*>();
    return edits.value(index);
}
QAction* action(CodeflowEditorDialog& dialog, const QString& title)
{
    for (auto* item : dialog.findChildren<QAction*>())
    {
        if (item->text().remove('&') == title)
        {
            return item;
        }
    }
    return nullptr;
}
bool diagnosticContains(CodeflowEditorDialog& dialog, const QString& text)
{
    for (auto* editor : dialog.findChildren<QTextEdit*>())
    {
        if (editor->toPlainText().contains(text, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    for (auto* editor : dialog.findChildren<QPlainTextEdit*>())
    {
        if (editor->toPlainText().contains(text, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}
bool hasPython()
{
    return !Codeflow::defaultPython().isEmpty() && QFileInfo::exists(Codeflow::hostScript());
}
} // namespace

class CodeflowEditorTests final : public QObject
{
    Q_OBJECT
private slots:
    void closingMetadataOrPropertyDraftRequiresConfirmation()
    {
        SettingsWidgetRegistry widgets;
        CodeflowEditorDialog   dialog(fixture(), widgets, {});
        dialog.show();
        field(dialog, "codeflowName", 0)->setText("Changed title");
        bool asked = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box)
            {
                asked = true;
                box->done(QMessageBox::No);
            }
        });
        QVERIFY(!dialog.close());
        QVERIFY(asked);
        QVERIFY(dialog.isVisible());
        field(dialog, "codeflowName", 0)->setText("Unicode codec");
        auto* timeout = dialog.findChild<QSpinBox*>("timeoutMs");
        QVERIFY(timeout);
        timeout->setValue(timeout->value() + 20);
        asked = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box)
            {
                asked = true;
                box->done(QMessageBox::No);
            }
        });
        // Escape/reject must use the same guarded close path.
        dialog.reject();
        QVERIFY(asked);
        QVERIFY(dialog.isVisible());
        timeout->setValue(timeout->value() - 20);
        if (hasPython())
        {
            action(dialog, "Check")->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<SettingsFormModel*>(), 6000);
            dialog.findChild<SettingsFormModel*>()->setValue("prefix", "draft");
            asked = false;
            QTimer::singleShot(0, &dialog, [&] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (box)
                {
                    asked = true;
                    box->done(QMessageBox::No);
                }
            });
            QVERIFY(!dialog.close());
            QVERIFY(asked);
        }
    }

    void publicationFailurePreservesBaselineAndCanBeRetried()
    {
        if (!hasPython())
        {
            QSKIP("Python execution helper is unavailable");
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SettingsWidgetRegistry widgets;
        const auto             original          = fixture();
        int                    attempts          = 0;
        bool                   baselineUnchanged = true;
        QVariantMap            published;
        CodeflowEditorDialog   dialog(original,
                                      widgets,
                                      [&](const QVariantMap& baseline, const QVariantMap& candidate, QString* error) {
                                        baselineUnchanged = baselineUnchanged && baseline == original;
                                        ++attempts;
                                        if (attempts == 1)
                                        {
                                            *error = "fixture catalog conflict";
                                            return false;
                                        }
                                        published = candidate;
                                        return true;
                                      });
        dialog.setDefaultDirectory(directory.filePath("scripts"));
        dialog.setWorkspaceDirectory(directory.path());
        auto* apply = action(dialog, "Apply");
        QVERIFY(apply);
        apply->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(attempts, 1, 6000);
        const QString file = field(dialog, "codeflowFile", 1)->text();
        QVERIFY(QFileInfo::exists(file));
        QCOMPARE(dialog.definition(), original);
        QVERIFY(diagnosticContains(dialog, "Source saved, but publication failed"));
        QVERIFY(dialog.findChild<QLabel*>("codeflowStatus")->text().contains("Partial save"));
        apply->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(attempts, 2, 6000);
        QVERIFY(baselineUnchanged);
        QCOMPARE(dialog.definition(), published);
        const QString storedFile = published.value("source").toMap().value("file").toString();
        QVERIFY(QDir::isRelativePath(storedFile));
        QCOMPARE(QDir(directory.path()).absoluteFilePath(storedFile), file);
    }

    void workspaceRelativeSourceLoadsDiskRevisionAndDetectsRemoval()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkpath("scripts"));
        const QString    path = directory.filePath("scripts/main.py");
        const QByteArray disk = Codeflow::templateSource(1).toUtf8() + "\n# current disk revision\n";
        QFile            file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(disk), disk.size());
        file.close();
        auto definition = fixture("# cached approved revision\n");
        auto source     = definition.value("source").toMap();
        source.insert("file", "scripts/main.py");
        definition.insert("source", source);
        SettingsWidgetRegistry widgets;
        CodeflowEditorDialog   dialog(definition, widgets, {});
        dialog.setWorkspaceDirectory(directory.path());
        auto* editor = sourceEditor(dialog);
        QVERIFY(editor);
        QCOMPARE(editor->toPlainText().toUtf8(), disk);
        QVERIFY(dialog.findChild<QLabel*>("codeflowStatus")->text().contains("differs"));
        editor->appendPlainText("# draft change");
        action(dialog, "Save")->trigger();
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), editor->toPlainText().toUtf8());
        file.close();
        QVERIFY(QFile::remove(path));
        action(dialog, "Save")->trigger();
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(diagnosticContains(dialog, "removed externally"));
        editor->document()->setModified(false);
    }

    void repeatedChecksRetainPropertyDraftAndUnknownDefaults()
    {
        if (!hasPython())
        {
            QSKIP("Python execution helper is unavailable");
        }
        SettingsWidgetRegistry widgets;
        auto                   definition = fixture();
        definition.insert("defaults",
                          QVariantMap {
                              {        "prefix", "saved"},
                              {"future-setting",      42}
        });
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVariantMap          published;
        CodeflowEditorDialog dialog(definition,
                                    widgets,
                                    [&](const QVariantMap&, const QVariantMap& candidate, QString*) {
                                        published = candidate;
                                        return true;
                                    });
        dialog.setDefaultDirectory(directory.path());
        dialog.setWorkspaceDirectory(directory.path());
        auto* check = action(dialog, "Check");
        QVERIFY(check);
        check->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<SettingsFormModel*>(), 6000);
        auto* first = dialog.findChild<SettingsFormModel*>();
        QCOMPARE(first->value("prefix").toString(), QString("saved"));
        first->setValue("prefix", "unsaved draft");
        QPointer<SettingsFormModel> old(first);
        check->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(old.isNull(), 6000);
        auto* refreshed = dialog.findChild<SettingsFormModel*>();
        QVERIFY(refreshed);
        QCOMPARE(refreshed->value("prefix").toString(), QString("unsaved draft"));
        // FormModel exposes recognized fields; publication must preserve unknown settings too.
        action(dialog, "Apply")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!published.isEmpty(), 6000);
        QCOMPARE(published.value("defaults").toMap().value("future-setting").toInt(), 42);
        QCOMPARE(published.value("defaults").toMap().value("prefix").toString(), QString("unsaved draft"));
    }

    void openingDoesNotImportSource()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString          marker = directory.filePath("must-not-exist");
        const QString          code   = QString("from pathlib import Path\nPath(%1).write_text('imported')\n")
                                            .arg("r'" + marker + "'")
                                      + Codeflow::templateSource(1);
        SettingsWidgetRegistry widgets;
        CodeflowEditorDialog   dialog(fixture(code), widgets, {});
        dialog.show();
        QTest::qWait(80);
        QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(sourceEditor(dialog));
        QVERIFY(!dialog.findChild<SettingsFormWidget*>());
    }

    void checkIsAsynchronousAndBuildsDeclaredPreview()
    {
        if (!hasPython())
        {
            QSKIP("Python execution helper is unavailable");
        }
        SettingsWidgetRegistry widgets;
        CodeflowEditorDialog   dialog(fixture("import time\ntime.sleep(0.7)\n" + Codeflow::templateSource(1)),
                                      widgets,
                                      {});
        dialog.show();
        auto* check = action(dialog, "Check");
        QVERIFY(check);
        int    ticks = 0;
        QTimer timer;
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, &dialog, [&ticks] { ++ticks; });
        timer.start();
        QElapsedTimer elapsed;
        elapsed.start();
        check->trigger();
        QVERIFY2(elapsed.elapsed() < 200, "Check blocked the GUI thread");
        QTest::qWait(250);
        QVERIFY(ticks >= 5);
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<SettingsFormWidget*>(), 6000);
        auto* form = dialog.findChild<SettingsFormWidget*>();
        QVERIFY(form->visibleFields().contains("prefix"));
        QVERIFY(diagnosticContains(dialog, "incoming"));
        QVERIFY(diagnosticContains(dialog, "outgoing"));
    }

    void closeCancelsImportWithoutJoiningGuiThread()
    {
        if (!hasPython())
        {
            QSKIP("Python execution helper is unavailable");
        }
        SettingsWidgetRegistry widgets;
        auto* dialog = new CodeflowEditorDialog(fixture("while True:\n    pass\n" + Codeflow::templateSource(1)),
                                                widgets,
                                                {});
        dialog->show();
        auto* check = action(*dialog, "Check");
        QVERIFY(check);
        check->trigger();
        QTest::qWait(100);
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(dialog->close());
        delete dialog;
        QVERIFY2(elapsed.elapsed() < 500, "Closing waited for the Python worker");
        // Allow the context-bound completion to arrive after destruction.
        QTest::qWait(350);
    }

    void sourceSavePreservesUtf8AndRejectsExternalConflict()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SettingsWidgetRegistry widgets;
        CodeflowEditorDialog   dialog(fixture(), widgets, {});
        dialog.setDefaultDirectory(directory.path());
        dialog.setWorkspaceDirectory(directory.path());
        auto* editor = sourceEditor(dialog);
        QVERIFY(editor);
        auto* fileField = field(dialog, "codeflowFile", 1);
        QVERIFY(fileField);
        const QString path = directory.filePath("unicode.py");
        fileField->setText(path);
        const QString code = Codeflow::templateSource(1) + QString::fromUtf8("\n# Привет · 日本語 · λ\n");
        editor->setPlainText(code);
        auto* save = action(dialog, "Save");
        QVERIFY(save);
        save->trigger();
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), code.toUtf8());
        saved.close();
        QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray external("# external revision\n");
        QCOMPARE(saved.write(external), external.size());
        saved.close();
        editor->appendPlainText("# editor draft");
        save->trigger();
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), external);
        QVERIFY(diagnosticContains(dialog, "changed externally"));
        QVERIFY(editor->toPlainText().contains("editor draft"));
        editor->document()->setModified(false);
    }

    void localIndentationCommentAndMultilineHighlighting()
    {
        SettingsWidgetRegistry widgets;
        CodeflowEditorDialog   dialog(fixture(), widgets, {});
        dialog.show();
        auto* editor = sourceEditor(dialog);
        QVERIFY(editor);
        editor->setFocus();
        QTest::qWait(30);
        editor->setPlainText("(packet)");
        auto bracketCursor = editor->textCursor();
        bracketCursor.setPosition(1);
        editor->setTextCursor(bracketCursor);
        QCOMPARE(editor->extraSelections().size(), 2);
        editor->setPlainText("if True:");
        auto cursor = editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        editor->setTextCursor(cursor);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(editor->toPlainText(), QString("if True:\n    "));
        QTest::keyClick(editor, Qt::Key_Tab);
        QCOMPARE(editor->textCursor().block().text(), QString("        "));
        QTest::keyClick(editor, Qt::Key_Backtab);
        QCOMPARE(editor->textCursor().block().text(), QString("    "));
        QTest::keyClick(editor, Qt::Key_Slash, Qt::ControlModifier);
        QVERIFY(editor->textCursor().block().text().contains('#'));
        editor->setPlainText("message = f\"\"\"first\nsecond {1 + 2}\nthird\"\"\"\n# comment\n");
        QTest::qWait(30);
        const QTextBlock multiline = editor->document()->findBlockByNumber(1);
        QVERIFY(multiline.isValid());
        QVERIFY(!multiline.layout()->formats().isEmpty());
        QVERIFY(multiline.userState() > 0);
        editor->document()->setModified(false);
    }

    void nativeCapture()
    {
        const QString destination = qEnvironmentVariable("TERMASTER_CODEFLOW_CAPTURE");
        if (destination.isEmpty())
        {
            QSKIP("Native screenshot capture is opt-in");
        }
        QVERIFY(QDir().mkpath(destination));
        SettingsWidgetRegistry widgets;
        CodeflowEditorDialog   dialog(fixture(Codeflow::templateSource(2)), widgets, {});
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        if (hasPython())
        {
            action(dialog, "Check")->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<SettingsFormWidget*>(), 6000);
        }
        QTest::qWait(150);
        const QString scale  = qEnvironmentVariable("QT_SCALE_FACTOR", "1");
        const QString suffix = scale == "2" ? "200" : "100";
        const QPixmap image  = dialog.grab();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QDir(destination).filePath("CodeflowEditor-" + suffix + ".png")));
    }
};
QTEST_MAIN(CodeflowEditorTests)
#include "CodeflowEditorTests.moc"
