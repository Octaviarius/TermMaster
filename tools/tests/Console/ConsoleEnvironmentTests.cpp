#include "ConsoleEnvironment.h"
#include "Gui/Environment/EnvironmentEditor.h"

#include <QDir>
#include <QScopeGuard>
#include <QTableView>
#include <QTest>
#include <QUuid>

class ConsoleEnvironmentTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void overrides();
    Q_SLOT void validation();
    Q_SLOT void editorDraft();
    Q_SLOT void editorModel();
    Q_SLOT void currentUser();
    Q_SLOT void presentation();
};

void ConsoleEnvironmentTests::overrides()
{
    QProcessEnvironment base;
    base.insert(QStringLiteral("Keep"), QStringLiteral("synthetic"));
    base.insert(QStringLiteral("Remove"), QStringLiteral("synthetic"));
    QVariantMap changes {
        { QStringLiteral("Remove"),                                               QVariant()},
        {  QStringLiteral("Empty"),                                       QStringLiteral("")},
        {QStringLiteral("Unicode"), QString::fromUtf8("Р С›Р’В»Р В¶РІР‚вЂќРўС’Р В¶РЎС™Р’В¬")}
    };
    QString    error;
    const auto result = ConsoleEnvironment::apply(base, changes, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(result.contains(QStringLiteral("Keep")));
    QVERIFY(!result.contains(QStringLiteral("Remove")));
    QVERIFY(result.contains(QStringLiteral("Empty")));
    QVERIFY(result.value(QStringLiteral("Empty")).isEmpty());
    QCOMPARE(result.value(QStringLiteral("Unicode")), QString::fromUtf8("Р С›Р’В»Р В¶РІР‚вЂќРўС’Р В¶РЎС™Р’В¬"));
    const auto removed = ConsoleEnvironment::apply(base,
                                                   {
                                                       {QStringLiteral("KEEP"), QVariant()}
    },
                                                   &error);
#ifdef Q_OS_WIN
    QVERIFY(!removed.contains(QStringLiteral("Keep")));
#else
    QVERIFY(removed.contains(QStringLiteral("Keep")));
#endif
}

void ConsoleEnvironmentTests::validation()
{
    QString error;
    QVERIFY(!ConsoleEnvironment::validateOverrides(
        {
            {QStringLiteral("bad=name"), QStringLiteral("secret")}
    },
        &error));
    QVERIFY(!error.contains(QStringLiteral("secret")));
    QVERIFY(!ConsoleEnvironment::validateOverrides(
        {
            {QStringLiteral("A"), 42}
    },
        &error));
    QVERIFY(!ConsoleEnvironment::validateOverrides(
        {
            {QStringLiteral("A"), QString(QChar(0))}
    },
        &error));
    QVERIFY(!ConsoleEnvironment::validateOverrides(
        {
            {QStringLiteral("A"), QString(32768, u'x')}
    },
        &error));
    const QVariantMap collision {
        {QStringLiteral("Path"), QStringLiteral("a")},
        {QStringLiteral("PATH"), QStringLiteral("b")}
    };
#ifdef Q_OS_WIN
    QVERIFY(!ConsoleEnvironment::validateOverrides(collision, &error));
#else
    QVERIFY(ConsoleEnvironment::validateOverrides(collision, &error));
#endif
    QVariantMap large;
    for (int i = 0; i < 1025; ++i)
    {
        large.insert(QStringLiteral("V%1").arg(i), QStringLiteral(""));
    }
    QVERIFY(!ConsoleEnvironment::validateOverrides(large, &error));
}

void ConsoleEnvironmentTests::editorDraft()
{
    int               generation = 0;
    EnvironmentEditor editor(nullptr, [&generation](QString* error) {
        error->clear();
        QProcessEnvironment baseline;
        baseline.insert(QStringLiteral("BASE_SECRET"), QStringLiteral("synthetic-only"));
        baseline.insert(QStringLiteral("VERSION"), QString::number(generation));
        return baseline;
    });
    int               changes = 0;
    editor.setChangedCallback([&changes] { ++changes; });
    QVERIFY(editor.value().toMap().isEmpty());
    QVERIFY(editor.addOverride(QStringLiteral("Empty"), QStringLiteral("")));
    QVERIFY(editor.addOverride(QStringLiteral("Unicode"), QString::fromUtf8("Р С›Р’В»")));
    QVERIFY(editor.removeVariable(QStringLiteral("VERSION")));
    const QVariant saved = editor.value();
    QVERIFY(!saved.toMap().contains(QStringLiteral("BASE_SECRET")));
    QCOMPARE(changes, 3);
    generation++;
    QVERIFY(editor.refreshFromUser());
    QCOMPARE(editor.value(), saved);
    QCOMPARE(changes, 3);
    QVERIFY(editor.resetOverride(QStringLiteral("VERSION")));
    QVERIFY(!editor.resetOverride(QStringLiteral("VERSION")));
    QCOMPARE(changes, 4);
    const QVariantMap invalid {
        {QStringLiteral("INVALID"), 17}
    };
    editor.setValue(invalid);
    QCOMPARE(editor.value(), QVariant(invalid));
    QVERIFY(!editor.inputError().isEmpty());
    QCOMPARE(changes, 4);
    editor.setValue(QStringLiteral("invalid object"));
    QCOMPARE(editor.value().metaType().id(), QMetaType::QString);
    QVERIFY(!editor.inputError().isEmpty());
}

void ConsoleEnvironmentTests::editorModel()
{
    EnvironmentEditor editor(nullptr, [](QString* error) {
        error->clear();
        QProcessEnvironment baseline;
        baseline.insert(QStringLiteral("BASE"), QStringLiteral("synthetic"));
        return baseline;
    });
    auto*             table = editor.findChild<QTableView*>();
    QVERIFY(table);
    auto* model = table->model();
    QCOMPARE(model->rowCount(), 1);
    QVERIFY(model->setData(model->index(0, 1), QStringLiteral("edited")));
    QCOMPARE(editor.value().toMap().value(QStringLiteral("BASE")).toString(), QStringLiteral("edited"));
    QVERIFY(model->setData(model->index(0, 0), QStringLiteral("RENAMED")));
    QVERIFY(editor.value().toMap().value(QStringLiteral("BASE")).isNull());
    QCOMPARE(editor.value().toMap().value(QStringLiteral("RENAMED")).toString(), QStringLiteral("edited"));
}

void ConsoleEnvironmentTests::currentUser()
{
#ifdef Q_OS_WIN
    const auto key = ("TERMASTER_PARENT_ONLY_" + QUuid::createUuid().toString(QUuid::Id128)).toUtf8();
    QVERIFY(qputenv(key.constData(), "synthetic-only"));
    const auto restore = qScopeGuard([&] { qunsetenv(key.constData()); });
#endif
    QString    error;
    const auto environment = ConsoleEnvironment::userEnvironment(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
#ifdef Q_OS_WIN
    QVERIFY(environment.contains(QStringLiteral("SystemRoot")));
    QVERIFY(!environment.contains(QString::fromUtf8(key)));
#else
    QVERIFY(!environment.isEmpty());
#endif
}

void ConsoleEnvironmentTests::presentation()
{
    EnvironmentEditor editor(nullptr, [](QString* error) {
        error->clear();
        QProcessEnvironment base;
        base.insert("PATH", "C:/Windows/System32;C:/Tools/bin");
        base.insert("HOME", "C:/Users/Example");
        base.insert("LANG", "en_US.UTF-8");
        base.insert("TERM", "xterm-256color");
        return base;
    });
    editor.setWindowTitle("Console environment РІР‚вЂќ synthetic preview");
    editor.setValue(QVariantMap {
        {"PROJECT_HOME", "F:/Projects/Example"},
        {        "LANG",         "ru_RU.UTF-8"},
        {"OLD_VARIABLE",            QVariant()}
    });
    editor.resize(1280, 800);
    editor.show();
    QTest::qWait(100);
    QVERIFY(editor.isVisible());
    auto* table = editor.findChild<QTableView*>();
    QVERIFY(table);
    QVERIFY(table->columnWidth(1) > 200);
    const auto capture = qEnvironmentVariable("TERMASTER_APP_GUI_CAPTURE");
    if (!capture.isEmpty())
    {
        QVERIFY(QDir().mkpath(capture));
        QVERIFY(editor.grab().save(QDir(capture).filePath("ConsoleEnvironment.png")));
    }
}

QTEST_MAIN(ConsoleEnvironmentTests)
#include "ConsoleEnvironmentTests.moc"
