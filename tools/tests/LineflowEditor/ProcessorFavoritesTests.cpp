#include "Gui/Processing/ProcessorFavorites.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <limits>
#include <stdexcept>

class ProcessorFavoritesTests final : public QObject
{
    Q_OBJECT
private slots:
    void defaultIsImplicitAndExplicitEmptyRemainsEmpty()
    {
        QTemporaryDir      temporary;
        Settings           preferences(temporary.filePath("preferences.yaml"));
        ProcessorFavorites favorites(preferences.node("processing"));
        QCOMPARE(favorites.ids(), QStringList {"native.bypass"});
        QVERIFY(favorites.contains("native.bypass"));
        QVERIFY(!preferences.contains("processing/favorites"));
        QVERIFY(!preferences.isDirty());
        QSignalSpy changed(&favorites, &ProcessorFavorites::changed);
        QVERIFY(favorites.setIds({}));
        QVERIFY(favorites.ids().isEmpty());
        QVERIFY(preferences.contains("processing/favorites"));
        QCOMPARE(changed.count(), 1);
        ProcessorFavorites reloaded(preferences.node("processing"));
        QVERIFY(reloaded.ids().isEmpty());
        QVERIFY(favorites.setIds({}));
        QCOMPARE(changed.count(), 1);
    }

    void addRemoveAndReorderNotifySharedConsumersOnce()
    {
        QTemporaryDir      temporary;
        Settings           preferences(temporary.filePath("preferences.yaml"));
        int                persisted = 0;
        ProcessorFavorites favorites(preferences.node("processing"), [&](QString*) {
            ++persisted;
            return true;
        });
        QSignalSpy         firstConsumer(&favorites, &ProcessorFavorites::changed);
        QSignalSpy         secondConsumer(&favorites, &ProcessorFavorites::changed);
        QVERIFY(favorites.setFavorite("missing.plugin.processor", true));
        QVERIFY(favorites.setFavorite("native.stub", true));
        QCOMPARE(favorites.ids(), QStringList({"native.bypass", "missing.plugin.processor", "native.stub"}));
        QCOMPARE(persisted, 2);
        QVERIFY(favorites.move("native.stub", -1));
        QCOMPARE(favorites.ids(), QStringList({"native.bypass", "native.stub", "missing.plugin.processor"}));
        QVERIFY(favorites.move("native.stub", std::numeric_limits<int>::min()));
        QCOMPARE(favorites.ids().first(), QString("native.stub"));
        QVERIFY(favorites.move("native.stub", std::numeric_limits<int>::max()));
        QCOMPARE(favorites.ids().last(), QString("native.stub"));
        const auto before = persisted;
        QVERIFY(favorites.move("native.stub", 1));
        QVERIFY(favorites.setFavorite("native.stub", true));
        QVERIFY(favorites.setFavorite("absent.favorite", false));
        QCOMPARE(persisted, before);
        QVERIFY(favorites.setFavorite("missing.plugin.processor", false));
        QVERIFY(!favorites.contains("missing.plugin.processor"));
        QCOMPARE(firstConsumer.count(), persisted);
        QCOMPARE(secondConsumer.count(), persisted);
        QString error;
        QVERIFY(!favorites.move("absent.favorite", 1, &error));
        QVERIFY(!error.isEmpty());
    }

    void validationBoundsAndInvalidLoadedLists()
    {
        QTemporaryDir      temporary;
        Settings           preferences(temporary.filePath("preferences.yaml"));
        ProcessorFavorites favorites(preferences.node("processing"));
        QSignalSpy         changed(&favorites, &ProcessorFavorites::changed);
        QString            error;
        for (const auto& ids : {
                 QStringList {"duplicate", "duplicate"},
                 QStringList {" leading"},
                 QStringList {"trailing "},
                 QStringList {QString {}},
                 QStringList {QString(257, 'x')},
                 QStringList {QString("a") + QChar::Null}
        })
        {
            QVERIFY(!favorites.setIds(ids, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(favorites.ids(), QStringList {"native.bypass"});
            QCOMPARE(changed.count(), 0);
            QVERIFY(!preferences.contains("processing/favorites"));
        }
        QStringList maximum;
        for (int index = 0; index < 128; ++index)
        {
            maximum.append("missing." + QString::number(index));
        }
        maximum[0] = QString(256, 'x');
        QVERIFY(favorites.setIds(maximum));
        QVERIFY(!favorites.setFavorite("overflow", true, &error));
        QCOMPARE(favorites.ids(), maximum);
        QCOMPARE(changed.count(), 1);
        const QList<QVariant> invalid {
            7,
            QString("not-a-list"),
            QVariantList {true},
            QVariantList {"id", "id"},
            QVariantList {" padded"}
        };
        for (const auto& value : invalid)
        {
            QVERIFY(preferences.setValue("invalid/favorites", value));
            QVERIFY_EXCEPTION_THROWN(ProcessorFavorites(preferences.node("invalid")), std::invalid_argument);
        }
        QVERIFY(preferences.setValue("invalid/favorites", QStringList(129, QString("id"))));
        QVERIFY_EXCEPTION_THROWN(ProcessorFavorites(preferences.node("invalid")), std::invalid_argument);
    }

    void yamlPersistenceKeepsOrderUnknownPreferencesAndMissingIds()
    {
        QTemporaryDir temporary;
        const auto    path = temporary.filePath("preferences.yaml");
        Settings      preferences(path);
        QVERIFY(preferences.setValue("processing/unknown",
                                     QVariantMap {
                                         {"retained", 12}
        }));
        QVERIFY(preferences.setValue("unrelated", "kept"));
        ProcessorFavorites favorites(preferences.node("processing"),
                                     [&](QString* error) { return preferences.sync(error); });
        const QStringList  ids {"native.stub", "missing.vendor.codec", QString::fromUtf8("custom.雪")};
        QVERIFY(favorites.setIds(ids));
        QVERIFY(!preferences.isDirty());
        Settings reopened(path);
        QVERIFY(reopened.load());
        ProcessorFavorites sharedRead(reopened.node("processing"));
        QCOMPARE(sharedRead.ids(), ids);
        QCOMPARE(reopened.value("processing/unknown").toMap().value("retained").toInt(), 12);
        QCOMPARE(reopened.value("unrelated").toString(), "kept");
    }

    void failedPersistenceRestoresPresenceValueAndCachedOrder()
    {
        QTemporaryDir temporary;
        Settings      preferences(temporary.filePath("preferences.yaml"));
        QVERIFY(preferences.setValue("processing/unknown", 42));
        bool               fail = true;
        ProcessorFavorites favorites(preferences.node("processing"), [&](QString* error) {
            if (fail)
            {
                if (error)
                {
                    *error = "Synthetic persistence failure";
                }
                return false;
            }
            return true;
        });
        QSignalSpy         changed(&favorites, &ProcessorFavorites::changed);
        QString            error;
        QVERIFY(!favorites.setFavorite("native.stub", true, &error));
        QCOMPARE(error, "Synthetic persistence failure");
        QVERIFY(!preferences.contains("processing/favorites"));
        QCOMPARE(favorites.ids(), QStringList {"native.bypass"});
        QCOMPARE(changed.count(), 0);
        QCOMPARE(preferences.value("processing/unknown").toInt(), 42);
        fail = false;
        QVERIFY(favorites.setIds({"missing.one", "native.stub"}));
        const auto previous = preferences.value("processing/favorites");
        fail                = true;
        QVERIFY(!favorites.move("native.stub", -1, &error));
        QCOMPARE(preferences.value("processing/favorites"), previous);
        QCOMPARE(favorites.ids(), QStringList({"missing.one", "native.stub"}));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(preferences.value("processing/unknown").toInt(), 42);
    }

    void thrownPersistenceRollsBackAndReentryIsRejected()
    {
        QTemporaryDir       temporary;
        Settings            preferences(temporary.filePath("preferences.yaml"));
        ProcessorFavorites* model          = nullptr;
        bool                nestedRejected = false;
        QString             nestedError;
        int                 calls = 0;
        ProcessorFavorites  favorites(preferences.node("processing"), [&](QString*) -> bool {
            ++calls;
            nestedRejected = !model->setIds({"reentrant"}, &nestedError);
            throw std::runtime_error("Synthetic exception");
        });
        model = &favorites;
        QString error;
        QVERIFY(!favorites.setIds({"outer"}, &error));
        QCOMPARE(error, "Synthetic exception");
        QCOMPARE(calls, 1);
        QVERIFY(nestedRejected);
        QVERIFY(nestedError.contains("already"));
        QVERIFY(!preferences.contains("processing/favorites"));
        QCOMPARE(favorites.ids(), QStringList {"native.bypass"});
    }
};
QTEST_GUILESS_MAIN(ProcessorFavoritesTests)
#include "ProcessorFavoritesTests.moc"
