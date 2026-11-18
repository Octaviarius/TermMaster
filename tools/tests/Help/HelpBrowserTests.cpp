#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHelpContentWidget>
#include <QHelpEngine>
#include <QHelpLink>
#include <QHelpSearchEngine>
#include <QHelpSearchQueryWidget>
#include <QHelpSearchResult>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QtTest>
#include <Termaster/Core/Gui/Help/HelpBrowser.h>
namespace
{
QString mainDocumentation()
{
    return QString::fromUtf8(HELP_MAIN_QCH);
}

QString pluginDocumentation()
{
    return QString::fromUtf8(HELP_PLUGIN_QCH);
}

QTextBrowser* reader(HelpBrowser& browser)
{
    return browser.findChild<QTextBrowser*>(QStringLiteral("helpText"));
}

bool clickTextLink(QTextBrowser& text, const QString& label)
{
    auto cursor = text.document()->find(label);
    if (cursor.isNull())
    {
        return false;
    }

    cursor.setPosition(cursor.selectionStart() + qMin(2, static_cast<int>(label.size()) - 1));
    text.setTextCursor(cursor);
    text.ensureCursorVisible();
    QApplication::processEvents();
    const auto position = text.cursorRect(cursor).center();
    if (text.anchorAt(position).isEmpty())
    {
        return false;
    }

    QTest::mouseClick(text.viewport(), Qt::LeftButton, Qt::NoModifier, position);
    return true;
}
} // namespace

class HelpBrowserTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void compiledMetadataAndNativeReference();
    void nativeFullTextSearch();
    void findAndHistory();
    void disabledPluginDocumentationAndRemoval();
    void ownedCompiledSnapshots();
    void missingTopicsAndInvalidDocumentation();
    void blocksUnsafeResourcesAndRequiresExplicitExternalClick();
    void cancelsOwnedWorkersBeforeCollectionRemoval();
};

void HelpBrowserTests::initTestCase()
{
    QVERIFY2(QFileInfo::exists(mainDocumentation()), qPrintable(mainDocumentation()));
    QVERIFY2(QFileInfo::exists(pluginDocumentation()), qPrintable(pluginDocumentation()));
    QVERIFY(!QHelpEngine::namespaceName(mainDocumentation()).isEmpty());
    QVERIFY(!QHelpEngine::namespaceName(pluginDocumentation()).isEmpty());
}

void HelpBrowserTests::compiledMetadataAndNativeReference()
{
    HelpBrowser browser;
    QString     error;
    QVERIFY2(browser.addDocumentation(mainDocumentation(), &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(browser.namespaces(), QStringList {QHelpEngine::namespaceName(mainDocumentation())});
    QVERIFY(!browser.engine().documentsForIdentifier(QStringLiteral("index")).isEmpty());
    QVERIFY(!browser.engine().documentsForIdentifier(QStringLiteral("processing.lineflow")).isEmpty());
    QVERIFY(!browser.engine().documentsForIdentifier(QStringLiteral("processor.native.timestamp")).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(browser.engine().contentModel()->rowCount() > 0, 10000);

    QVERIFY(browser.openTopic(QStringLiteral("processor.native.timestamp")));
    QCOMPARE(browser.currentTopic(), QStringLiteral("processor.native.timestamp"));
    auto* text = reader(browser);
    QVERIFY(text);
    QCOMPARE(text->source().scheme(), QStringLiteral("qthelp"));
    const auto reference = text->toPlainText();
    QVERIFY(reference.contains(QStringLiteral("Parameters")));
    QVERIFY(reference.contains(QStringLiteral("Date/time format")));
    QVERIFY(reference.contains(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    QVERIFY(reference.contains(QStringLiteral("Required:")));
    QVERIFY(reference.contains(QStringLiteral("Default:")));
    QVERIFY(reference.contains(QStringLiteral("enum:")));
    QVERIFY(reference.contains(QStringLiteral("local")));
    QVERIFY(reference.contains(QStringLiteral("utc")));
    QVERIFY(reference.contains(QStringLiteral("maxLength:")));
    QVERIFY(reference.contains(QStringLiteral("Group: Timestamp")));

    const auto capture = qEnvironmentVariable("TERMASTER_HELP_CAPTURE");
    if (!capture.isEmpty())
    {
        const auto path = capture.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ?
                              capture :
                              QDir(capture).filePath(QStringLiteral("HelpBrowser.png"));
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        browser.resize(1100, 760);
        browser.show();
        QApplication::processEvents();
        QVERIFY(browser.grab().save(path));
    }
}

void HelpBrowserTests::nativeFullTextSearch()
{
    HelpBrowser browser;
    auto*       search = browser.engine().searchEngine();
    QSignalSpy  indexed(search, &QHelpSearchEngine::indexingFinished);
    QVERIFY(browser.addDocumentation(mainDocumentation()));
    // Registration schedules the native writer. An explicit simultaneous reindex cancels it
    // and can emit indexingFinished for the cancelled pass before the replacement has finished.
    QTRY_VERIFY_WITH_TIMEOUT(!indexed.isEmpty(), 10000);

    browser.resize(1000, 700);
    browser.show();
    auto* navigation = browser.findChild<QTabWidget*>(QStringLiteral("helpNavigation"));
    QVERIFY(navigation);
    navigation->setCurrentIndex(1);
    auto* input = search->queryWidget()->findChild<QLineEdit*>();
    QVERIFY(input);

    QSignalSpy completed(search, &QHelpSearchEngine::searchingFinished);
    QTest::keyClicks(input, QStringLiteral("workspace"));
    QTest::keyClick(input, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 10000);
    QCOMPARE(search->searchInput(), QStringLiteral("workspace"));
    QVERIFY(search->searchResultCount() > 0);
    const auto results = search->searchResults(0, search->searchResultCount());
    QVERIFY(!results.isEmpty());
    for (const auto& result : results)
    {
        QCOMPARE(result.url().scheme(), QStringLiteral("qthelp"));
        QCOMPARE(result.url().host(), QHelpEngine::namespaceName(mainDocumentation()));
    }

    completed.clear();
    input->selectAll();
    QTest::keyClicks(input, QStringLiteral("UnmatchedTermA849D34F0C1240D9"));
    QTest::keyClick(input, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 10000);
    QCOMPARE(search->searchResultCount(), 0);
}

void HelpBrowserTests::findAndHistory()
{
    HelpBrowser browser;
    QVERIFY(browser.addDocumentation(mainDocumentation()));
    browser.resize(1000, 700);
    browser.show();
    QVERIFY(browser.openTopic(QStringLiteral("index")));
    QVERIFY(browser.openTopic(QStringLiteral("workspace")));
    auto* back    = browser.findChild<QToolButton*>(QStringLiteral("helpBack"));
    auto* forward = browser.findChild<QToolButton*>(QStringLiteral("helpForward"));
    auto* find    = browser.findChild<QLineEdit*>(QStringLiteral("helpFind"));
    auto* text    = reader(browser);
    QVERIFY(back);
    QVERIFY(forward);
    QVERIFY(find);
    QVERIFY(text);
    QVERIFY(back->isEnabled());
    QTest::mouseClick(back, Qt::LeftButton);
    QCOMPARE(browser.currentTopic(), QStringLiteral("index"));
    QVERIFY(forward->isEnabled());
    QTest::mouseClick(forward, Qt::LeftButton);
    QCOMPARE(browser.currentTopic(), QStringLiteral("workspace"));

    find->setText(QStringLiteral("session"));
    QTest::keyClick(find, Qt::Key_Return);
    QCOMPARE(text->textCursor().selectedText().toLower(), QStringLiteral("session"));

    browser.openGenerated(QStringLiteral("generated.test"),
                          QStringLiteral("Generated"),
                          QStringLiteral("Initial value"));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Initial value")));
    browser.openGenerated(QStringLiteral("generated.test"),
                          QStringLiteral("Generated"),
                          QStringLiteral("Updated value"));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Updated value")));
    QVERIFY(!text->toPlainText().contains(QStringLiteral("Initial value")));
}

void HelpBrowserTests::disabledPluginDocumentationAndRemoval()
{
    // Loading only the compiled bundle exercises discovery without a plugin host or DLL startup.
    HelpBrowser browser;
    QVERIFY(browser.addDocumentation(pluginDocumentation()));
    const auto namespaceName = QHelpEngine::namespaceName(pluginDocumentation());
    QVERIFY(browser.namespaces().contains(namespaceName));
    QVERIFY(browser.openTopic(QStringLiteral("example.prefix")));
    auto* text = reader(browser);
    QVERIFY(text);
    QVERIFY(text->toPlainText().contains(QStringLiteral("sample.prefix")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("Parameters")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("maxLength:")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("256")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("[sample]")));
    const auto copiedPage = text->toPlainText();

    QString error;
    QVERIFY2(browser.removeDocumentation(namespaceName, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(!browser.namespaces().contains(namespaceName));
    QVERIFY(browser.engine().documentsForIdentifier(QStringLiteral("example.prefix")).isEmpty());
    QCOMPARE(text->toPlainText(), copiedPage);
    auto* status = browser.findChild<QLabel*>(QStringLiteral("helpStatus"));
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("retained copy")));
    QVERIFY(!text->isBackwardAvailable());
    QVERIFY(!text->isForwardAvailable());
    QVERIFY(!browser.openTopic(QStringLiteral("example.prefix")));
    QVERIFY(text->toPlainText().contains(QStringLiteral("unavailable")));
}

void HelpBrowserTests::ownedCompiledSnapshots()
{
    QTemporaryDir sourceDirectory;
    QVERIFY(sourceDirectory.isValid());
    const auto pluginOriginal = sourceDirectory.filePath(QStringLiteral("Plugin.qch"));
    const auto mainOriginal   = sourceDirectory.filePath(QStringLiteral("Main.qch"));
    QVERIFY(QFile::copy(pluginDocumentation(), pluginOriginal));
    QVERIFY(QFile::copy(mainDocumentation(), mainOriginal));

    HelpBrowser browser;
    QVERIFY(browser.addDocumentation(pluginOriginal));
    QVERIFY(browser.addDocumentation(pluginOriginal));
    QVERIFY(browser.addDocumentation(mainOriginal));
    const auto pluginNamespace     = QHelpEngine::namespaceName(pluginOriginal);
    const auto mainNamespace       = QHelpEngine::namespaceName(mainOriginal);
    const auto pluginSnapshot      = browser.engine().documentationFileName(pluginNamespace);
    const auto mainSnapshot        = browser.engine().documentationFileName(mainNamespace);
    const auto collectionDirectory = QFileInfo(browser.engine().collectionFile()).absolutePath();
    QCOMPARE(QFileInfo(pluginSnapshot).absolutePath(), collectionDirectory);
    QCOMPARE(QFileInfo(mainSnapshot).absolutePath(), collectionDirectory);
    QVERIFY(QFileInfo(pluginSnapshot).canonicalFilePath() != QFileInfo(pluginOriginal).canonicalFilePath());
    QVERIFY(QFileInfo(mainSnapshot).canonicalFilePath() != QFileInfo(mainOriginal).canonicalFilePath());

    // No SQLite reader may keep a provider's distribution file open.
    QVERIFY(QFile::remove(pluginOriginal));
    QVERIFY(QFile::remove(mainOriginal));
    QVERIFY(browser.openTopic(QStringLiteral("index")));
    QVERIFY(reader(browser)->toPlainText().contains(QStringLiteral("Termaster help")));
    const auto images = browser.engine().files(mainNamespace, QStringList {}, QStringLiteral("png"));
    QVERIFY(!images.isEmpty());
    const auto image = browser.engine().fileData(images.first());
    QVERIFY(image.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
    QCOMPARE(reader(browser)->loadResource(QTextDocument::ImageResource, images.first()).toByteArray(), image);

    QVERIFY(browser.openTopic(QStringLiteral("example.prefix")));
    const auto retained = reader(browser)->toPlainText();
    QVERIFY(retained.contains(QStringLiteral("sample.prefix")));
    QVERIFY(browser.removeDocumentation(pluginNamespace));
    QCOMPARE(reader(browser)->toPlainText(), retained);
    auto* status = browser.findChild<QLabel*>(QStringLiteral("helpStatus"));
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("retained copy")));
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(pluginSnapshot), 10000);
    QVERIFY(QFileInfo::exists(mainSnapshot));
}

void HelpBrowserTests::missingTopicsAndInvalidDocumentation()
{
    HelpBrowser browser;
    QString     error;
    QVERIFY(!browser.addDocumentation(QStringLiteral("relative.qch"), &error));
    QVERIFY(!error.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile invalid(directory.filePath(QStringLiteral("invalid.qch")));
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    QCOMPARE(invalid.write("not a compiled help database"), qint64(28));
    invalid.close();
    QVERIFY(!browser.addDocumentation(invalid.fileName(), &error));
    QVERIFY(!error.isEmpty());
    QFile oversized(directory.filePath(QStringLiteral("oversized.qch")));
    QVERIFY(oversized.open(QIODevice::WriteOnly));
    QVERIFY(oversized.resize(64 * 1024 * 1024 + 1));
    oversized.close();
    QVERIFY(!browser.addDocumentation(oversized.fileName(), &error));
    QVERIFY(error.contains(QStringLiteral("64 MiB")));
    QVERIFY(browser.addDocumentation(mainDocumentation(), &error));
    QVERIFY(error.isEmpty());
    QVERIFY(browser.addDocumentation(mainDocumentation(), &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!browser.removeDocumentation(QStringLiteral("unknown.namespace"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!browser.openTopic(QStringLiteral("topic.does.not.exist")));
    QCOMPARE(browser.currentTopic(), QStringLiteral("topic.does.not.exist"));
    QVERIFY(reader(browser)->toPlainText().contains(QStringLiteral("unavailable")));
}

void HelpBrowserTests::blocksUnsafeResourcesAndRequiresExplicitExternalClick()
{
    HelpBrowser browser;
    browser.resize(1000, 700);
    browser.show();
    QSignalSpy external(&browser, &HelpBrowser::externalLinkRequested);
    const QUrl website(QStringLiteral("https://example.invalid/help"));
    browser.openGenerated(QStringLiteral("links"),
                          QStringLiteral("Links"),
                          QStringLiteral("[External website](https://example.invalid/help)\n\n"
                                         "![Blocked remote image](https://example.invalid/image.png)"));
    auto* text = reader(browser);
    QVERIFY(text);
    QApplication::processEvents();
    QCOMPARE(external.size(), 0);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile privateFile(directory.filePath(QStringLiteral("private.txt")));
    QVERIFY(privateFile.open(QIODevice::WriteOnly));
    QVERIFY(privateFile.write("private fixture content") > 0);
    privateFile.close();
    const auto local  = QUrl::fromLocalFile(privateFile.fileName());
    const auto source = text->source();
    for (const auto& url :
         {local, website, QUrl(QStringLiteral("data:text/plain,private")), QUrl(QStringLiteral("javascript:alert(1)"))})
    {
        QVERIFY(text->loadResource(QTextDocument::HtmlResource, url).toByteArray().isEmpty());
        QVERIFY(text->loadResource(QTextDocument::ImageResource, url).toByteArray().isEmpty());
        text->setSource(url);
        QCOMPARE(text->source(), source);
    }
    QCOMPARE(external.size(), 0);
    QVERIFY(clickTextLink(*text, QStringLiteral("External website")));
    QTRY_COMPARE_WITH_TIMEOUT(external.size(), 1, 10000);
    QCOMPARE(external.first().first().toUrl(), website);
    QCOMPARE(text->source(), source);
}

void HelpBrowserTests::cancelsOwnedWorkersBeforeCollectionRemoval()
{
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        QString       collection;
        QElapsedTimer elapsed;
        elapsed.start();
        {
            HelpBrowser browser;
            collection = browser.engine().collectionFile();
            QVERIFY(QFileInfo::exists(collection));
            QVERIFY(browser.addDocumentation(mainDocumentation()));
            browser.engine().searchEngine()->reindexDocumentation();
            browser.engine().searchEngine()->search(QStringLiteral("workspace"));
        }
        QVERIFY2(elapsed.elapsed() < 10000, "Help shutdown exceeded the bounded worker completion interval");
        QVERIFY(!QFileInfo::exists(collection));
        QVERIFY(!QFileInfo::exists(QFileInfo(collection).absolutePath()));
    }
}

QTEST_MAIN(HelpBrowserTests)
#include "HelpBrowserTests.moc"
