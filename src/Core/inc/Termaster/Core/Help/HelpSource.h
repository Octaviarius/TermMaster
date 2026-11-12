#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>
#include <optional>

class SettingsSchema;

/** Bounded source assembly for the help compiler. Use on the owning QObject thread. */
class HelpSource : public QObject
{
    Q_OBJECT
public:
    struct Topic
    {
        QString id;
        QString title;
        QString parent;
        QString markdown;
        QString owner;
        QUrl    source;
    };

    explicit HelpSource(QObject* parent = nullptr);

    /** Atomically replaces one owner's pages and declared image snapshots. */
    bool loadManifest(const QString& owner, const QString& manifestPath, QString* error = nullptr);
    bool replaceTopics(const QString& owner, QList<Topic> topics, QString* error = nullptr);
    void removeOwner(const QString& owner);

    QList<Topic>         topics() const;
    std::optional<Topic> topic(const QString& id) const;
    QList<Topic>         search(const QString& query) const;
    QString              topicForUrl(const QUrl& url) const;
    QByteArray           asset(const QUrl& url) const;
    /** Enumerates copied images for export into the compiled help source tree. */
    QMap<QUrl, QByteArray> assets() const;
    /** Returns Markdown from the exact validated descriptor, or empty on invalid input. */
    static QString schemaReference(const QByteArray& schema);
    static QString schemaReference(const SettingsSchema& schema);

signals:
    void changed();

private:
    struct Snapshot
    {
        QList<Topic>           topics;
        QMap<QUrl, QByteArray> assets;
    };

    QMap<QString, Snapshot> _owners;

    bool _replace(const QString& owner, Snapshot snapshot, QString* error);
};
