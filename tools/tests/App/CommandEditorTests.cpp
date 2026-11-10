#include "Gui/Terminal/CommandEditor.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <QToolButton>

class CommandEditorTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void advancedSettingsAndRequests()
    {
        CommandEditor widget;
        widget.setSettings({
            {"mode", "buffered"}
        });
        widget.show();
        auto* panel  = widget.findChild<QWidget*>("commandAdvancedPanel");
        auto* toggle = widget.findChild<QToolButton*>("commandAdvancedToggle");
        QVERIFY(panel->isHidden());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(!panel->isHidden());
        QSignalSpy changes(&widget, &CommandEditor::settingChanged);
        QSignalSpy periodic(&widget, &CommandEditor::periodicRequested);
        QSignalSpy files(&widget, &CommandEditor::fileRequested);
        QSignalSpy stop(&widget, &CommandEditor::stopRequested);
        widget.setSettings({
            {              "mode", "buffered"},
            {  "confirmMultiline",      false},
            {          "periodMs",        200},
            {"disconnectedPolicy",   "retain"}
        });
        QCOMPARE(changes.count(), 0);
        QVERIFY(!widget.findChild<QSpinBox*>("commandByteDelay"));
        QVERIFY(!widget.findChild<QSpinBox*>("commandLineDelay"));
        QCOMPARE(widget.findChild<QComboBox*>("commandDisconnectedPolicy")->currentData().toString(),
                 QString("retain"));
        auto* draft = widget.findChild<QPlainTextEdit*>("commandDraft");
        draft->setPlainText("AT\nNEXT");
        widget.findChild<QPushButton*>("commandPeriodicStart")->click();
        QCOMPARE(periodic.count(), 1);
        QCOMPARE(periodic.first().at(0).toString(), draft->toPlainText());
        QCOMPARE(periodic.first().at(3).toInt(), 200);
        widget.setSettings({
            {"periodMs", 0}
        });
        QCOMPARE(periodic.count(), 1);
        QCOMPARE(widget.findChild<QSpinBox*>("commandPeriod")->value(), 100);
        widget.findChild<QPushButton*>("commandRawFile")->click();
        widget.findChild<QPushButton*>("commandLineFile")->click();
        QCOMPARE(files.count(), 2);
        QCOMPARE(files.at(0).at(0).toBool(), false);
        QCOMPARE(files.at(1).at(0).toBool(), true);
        widget.setSendingState(true, "Sending");
        auto* stopButton = widget.findChild<QToolButton*>("commandStop");
        QVERIFY(!stopButton->isHidden());
        stopButton->click();
        QCOMPARE(stop.count(), 1);
        QVERIFY(widget.findChild<QPushButton*>("commandSend")->isEnabled());
        widget.setSendingState(false, {});
        QVERIFY(stopButton->isHidden());
    }

    Q_SLOT void compactLayoutPopupsAndNarrowWidth()
    {
        CommandEditor widget;
        widget.resize(640, 200);
        widget.setSettings({
            {            "mode", "buffered"},
            {"confirmMultiline",      false}
        });
        widget.show();
        auto*      draft   = widget.findChild<QPlainTextEdit*>("commandDraft");
        auto*      tools   = widget.findChild<QToolButton*>("commandAdvancedToggle");
        auto*      history = widget.findChild<QToolButton*>("commandHistoryToggle");
        auto*      panel   = widget.findChild<QWidget*>("commandAdvancedPanel");
        const auto size    = widget.sizeHint();
        QVERIFY(size.height() < 140);
        QVERIFY(widget.minimumSizeHint().width() <= 640);
        draft->setPlainText("AT+GMR");
        tools->setChecked(true);
        QTRY_VERIFY(panel->isVisible());
        QCOMPARE(widget.sizeHint(), size);
        QVERIFY(panel->width() < 600);
        QTest::keyClick(panel, Qt::Key_Escape);
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(!tools->isChecked());
        QCOMPARE(draft->toPlainText(), QString("AT+GMR"));
        history->setChecked(true);
        auto* historyPopup = widget.findChild<QWidget*>("commandHistoryPopup");
        QTRY_VERIFY(historyPopup->isVisible());
        QTest::keyClick(historyPopup, Qt::Key_Escape);
        QTRY_VERIFY(!historyPopup->isVisible());
        QVERIFY(!history->isChecked());
        widget.setSettings({
            {"mode", "interactive"}
        });
        QVERIFY(!draft->isVisible());
        QVERIFY(widget.findChild<QComboBox*>("commandTerminator")->isHidden());
        QVERIFY(widget.sizeHint().height() < 55);
        QVERIFY(widget.isHidden());
        QVERIFY(tools->isHidden());
        QVERIFY(history->isHidden());
    }

    Q_SLOT void interactiveVisibilityAndExternalRequests()
    {
        CommandEditor widget;
        QVariantMap   settings {
            {"mode", "interactive"}
        };
        connect(&widget, &CommandEditor::settingChanged, &widget, [&](const QString& key, const QVariant& value) {
            settings.insert(key, value);
            widget.setSettings(settings);
        });
        widget.setSettings(settings);
        QVERIFY(widget.isHidden());
        auto* draft = widget.findChild<QPlainTextEdit*>("commandDraft");
        auto* tools = widget.findChild<QToolButton*>("commandAdvancedToggle");
        auto* stop  = widget.findChild<QToolButton*>("commandStop");
        draft->setPlainText("Retained draft");
        widget.setSendingState(true, "File sending");
        QVERIFY(!widget.isHidden());
        QVERIFY(!stop->isHidden());
        QVERIFY(tools->isHidden());
        QVERIFY(!draft->isVisible());
        widget.setSendingState(false, "Complete");
        QVERIFY(widget.isHidden());
        widget.setBuffered(true);
        QVERIFY(widget.buffered());
        QVERIFY(!widget.isHidden());
        QVERIFY(!tools->isHidden());
        QVERIFY(draft->isVisible());
        QCOMPARE(draft->toPlainText(), QString("Retained draft"));
        widget.showSendingTools();
        auto* panel = widget.findChild<QWidget*>("commandAdvancedPanel");
        QTRY_VERIFY(panel->isVisible());
        QSignalSpy files(&widget, &CommandEditor::fileRequested);
        widget.requestFile();
        QVERIFY(panel->isHidden());
        widget.requestFile(true);
        QCOMPARE(files.count(), 2);
        QCOMPARE(files.at(0).at(0).toBool(), false);
        QCOMPARE(files.at(1).at(0).toBool(), true);
        widget.setBuffered(false);
        QVERIFY(widget.isHidden());
        QCOMPARE(draft->toPlainText(), QString("Retained draft"));
    }

    Q_SLOT void periodicConfirmationPreservesDraft()
    {
        CommandEditor widget;
        widget.setSettings({
            {"mode", "buffered"}
        });
        auto* draft = widget.findChild<QPlainTextEdit*>("commandDraft");
        draft->setPlainText("AT\nRESET");
        QSignalSpy periodic(&widget, &CommandEditor::periodicRequested);
        bool       prompted = false;
        QTimer::singleShot(0, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            {
                prompted = true;
                box->done(QMessageBox::No);
            }
        });
        widget.findChild<QPushButton*>("commandPeriodicStart")->click();
        QVERIFY(prompted);
        QCOMPARE(periodic.count(), 0);
        QCOMPARE(draft->toPlainText(), QString("AT\nRESET"));
        QCOMPARE(widget.findChild<QComboBox*>("commandHistory")->count(), 0);
    }

    Q_SLOT void favoritesPreserveFieldsAndLoadWithoutSending()
    {
        CommandEditor widget;
        QVariantList  favorites {
            QVariantMap {{"name", "Reset"},
                         {"text", "RESET"},
                         {"terminator", "custom"},
                         {"customTerminator", "\\0"},
                         {"extension", 42}}
        };
        widget.setSettings({
            {"favorites", favorites}
        });
        QSignalSpy changes(&widget, &CommandEditor::settingChanged);
        int        sends = 0;
        widget.setSendCallback([&](const QString&, const QString&, const QString&) {
            ++sends;
            return true;
        });
        widget.findChild<QPushButton*>("commandFavoriteLoad")->click();
        QCOMPARE(widget.findChild<QPlainTextEdit*>("commandDraft")->toPlainText(), QString("RESET"));
        QCOMPARE(widget.findChild<QComboBox*>("commandTerminator")->currentData().toString(), QString("custom"));
        QCOMPARE(sends, 0);
        QTimer::singleShot(0, [] {
            if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
            {
                dialog->setTextValue("Reset");
                dialog->accept();
            }
        });
        widget.findChild<QPushButton*>("commandFavoriteSave")->click();
        QCOMPARE(changes.last().at(0).toString(), QString("favorites"));
        const auto saved = changes.last().at(1).toList();
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().toMap().value("extension").toInt(), 42);
        widget.findChild<QPushButton*>("commandFavoriteDelete")->click();
        QVERIFY(changes.last().at(1).toList().isEmpty());
        favorites.clear();
        for (int i = 0; i < 101; ++i)
        {
            favorites.append(QVariantMap {
                {"name", QString::number(i)},
                {"text",               "AT"}
            });
        }
        widget.setSettings({
            {"favorites", favorites}
        });
        QCOMPARE(widget.findChild<QComboBox*>("commandFavorites")->count(), 100);
    }

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
        widget.setSendCallback([&](const QString& text, const QString& terminator, const QString&) {
            ++sends;
            argumentsCorrect = argumentsCorrect && text == "AT" && terminator == "cr";
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

    Q_SLOT void settingsAndTerminator()
    {
        CommandEditor widget;
        QVERIFY(!widget.buffered());
        QSignalSpy changes(&widget, &CommandEditor::settingChanged);
        QVERIFY(!widget.findChild<QComboBox*>("commandInputMode"));
        widget.setBuffered(true);
        QVERIFY(!widget.buffered());
        QCOMPARE(changes.count(), 1);
        QCOMPARE(changes.at(0).at(0).toString(), QString("mode"));
        QCOMPARE(changes.at(0).at(1).toString(), QString("buffered"));
        widget.setSettings({
            {      "mode", "buffered"},
            {"terminator",     "none"}
        });
        QVERIFY(widget.buffered());
        bool sent = false;
        widget.setSendCallback([&](const QString& text, const QString& terminator, const QString&) {
            sent = text == "41 54" && terminator == "none";
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
        widget.setSendCallback([&](const QString&, const QString&, const QString&) {
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
