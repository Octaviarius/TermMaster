#include "Gui/Terminal/CommandEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

class CommandEditorTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void acceptedDraftAndHistory()
    {
        CommandEditor widget;
        widget.setSettings({
            {            "mode", "buffered"},
            {"confirmMultiline",      false}
        });
        widget.show();
        auto* draft   = widget.findChild<QPlainTextEdit*>("commandDraft");
        auto* history = widget.findChild<QComboBox*>("commandHistory");
        QVERIFY(draft);
        QVERIFY(history);
        bool accepted         = false;
        bool argumentsCorrect = true;
        int  sends            = 0;
        widget.setSendCallback([&](const QString& text, bool hex, const QString& terminator, const QString&) {
            ++sends;
            argumentsCorrect = argumentsCorrect && text == "AT" && !hex && terminator == "cr";
            return accepted;
        });
        draft->setPlainText("AT");
        QTest::keyClick(draft, Qt::Key_Return);
        QCOMPARE(sends, 1);
        QCOMPARE(draft->toPlainText(), QString("AT"));
        QCOMPARE(history->count(), 0);
        accepted = true;
        QTest::keyClick(draft, Qt::Key_Return);
        QCOMPARE(sends, 2);
        QVERIFY(argumentsCorrect);
        QVERIFY(draft->toPlainText().isEmpty());
        QCOMPARE(history->count(), 1);
        QTest::keyClick(draft, Qt::Key_Up);
        QCOMPARE(draft->toPlainText(), QString("AT"));
        QTest::keyClick(draft, Qt::Key_Down);
        QVERIFY(draft->toPlainText().isEmpty());
        QVERIFY(history->isEditable());
        QCOMPARE(history->completer()->filterMode(), Qt::MatchContains);
    }

    Q_SLOT void settingsAndHex()
    {
        CommandEditor widget;
        QVERIFY(!widget.buffered());
        QSignalSpy changes(&widget, &CommandEditor::settingChanged);
        auto*      mode = widget.findChild<QComboBox*>("commandInputMode");
        mode->setCurrentIndex(1);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 1)));
        QCOMPARE(changes.count(), 1);
        QCOMPARE(changes.at(0).at(0).toString(), QString("mode"));
        QCOMPARE(changes.at(0).at(1).toString(), QString("buffered"));
        widget.setSettings({
            {      "mode", "buffered"},
            {       "hex",       true},
            {"terminator",     "none"}
        });
        QVERIFY(widget.buffered());
        bool sent = false;
        widget.setSendCallback([&](const QString& text, bool hex, const QString& terminator, const QString&) {
            sent = text == "41 54" && hex && terminator == "none";
            return true;
        });
        auto* draft = widget.findChild<QPlainTextEdit*>("commandDraft");
        draft->setPlainText("41 54");
        QTest::keyClick(draft, Qt::Key_Return);
        QVERIFY(sent);
    }

    Q_SLOT void pastePoliciesAndConfirmation()
    {
        CommandEditor widget;
        widget.setSettings({
            {"mode", "buffered"}
        });
        auto* draft = widget.findChild<QPlainTextEdit*>("commandDraft");
        int   sends = 0;
        widget.setSendCallback([&](const QString&, bool, const QString&, const QString&) {
            ++sends;
            return true;
        });
        QApplication::clipboard()->setText("AT");
        QVERIFY(QMetaObject::invokeMethod(draft, "paste"));
        QCOMPARE(draft->toPlainText(), QString("AT"));
        QCOMPARE(sends, 0);
        widget.setSettings({
            {       "mode",  "buffered"},
            {"pastePolicy", "immediate"}
        });
        QVERIFY(QMetaObject::invokeMethod(draft, "paste"));
        QCOMPARE(sends, 1);
        QCOMPARE(draft->toPlainText(), QString("AT"));
        QApplication::clipboard()->setText("AT\nRESET");
        bool defaultNo = false;
        QTimer::singleShot(0, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box)
            {
                defaultNo = box->defaultButton() == box->button(QMessageBox::No);
                box->done(QMessageBox::No);
            }
        });
        QVERIFY(QMetaObject::invokeMethod(draft, "paste"));
        QVERIFY(defaultNo);
        QCOMPARE(widget.findChild<QComboBox*>("commandHistory")->count(), 1);
        QCOMPARE(sends, 1);
        QCOMPARE(draft->toPlainText(), QString("AT"));
        QTest::keyClick(draft, Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(draft->toPlainText().contains('\n'));
        QCOMPARE(sends, 1);
        const QString retained = draft->toPlainText();
        widget.setSettings({
            {       "mode", "buffered"},
            {"pastePolicy",     "edit"}
        });
        QApplication::clipboard()->setText(QString(20000, QLatin1Char('x')));
        bool warned = false;
        QTimer::singleShot(0, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box)
            {
                warned = true;
                box->done(QMessageBox::Ok);
            }
        });
        QVERIFY(QMetaObject::invokeMethod(draft, "paste"));
        QVERIFY(warned);
        QCOMPARE(draft->toPlainText(), retained);
        QCOMPARE(sends, 1);
    }
};

QTEST_MAIN(CommandEditorTests)
#include "CommandEditorTests.moc"
