#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>

class Path;

/**
 * Owns a bounded YAML settings document with explicit load and atomic persistence.
 * The owner and all scoped views must be used on the same thread.
 */
class Settings
{
public:
    /** Identifies the expected document kind and describes newly created documents. */
    struct Info
    {
        QString type        = "termaster.settings";
        QString description = "Termaster settings";
    };

    /** A lifetime-checked view into the owner's current mapping at a relative path. */
    class Node
    {
    public:
        bool        isValid() const;
        QString     path() const;
        bool        contains(const QString& key) const;
        QVariant    value(const QString& key, const QVariant& fallback = {}) const;
        QVariantMap values() const;
        bool        setValue(const QString& key, const QVariant& value, QString* error = nullptr);
        bool        remove(const QString& key, QString* error = nullptr);
        bool        applyValues(QVariantMap values, QStringList removed, QString* error = nullptr);
        Node        node(const QString& path) const;

    private:
        struct Context;
        std::shared_ptr<Context> _context;
        friend class Settings;
        explicit Node(std::shared_ptr<Context> context);
        QString _key(const QString& relative) const;
        bool    _writable(const QString& key, QString* error) const;
    };

    explicit Settings(QString filePath);
    explicit Settings(const Path& filePath);
    Settings(QString filePath, Info info);
    Settings(const Path& filePath, Info info);
    ~Settings();
    Q_DISABLE_COPY_MOVE(Settings)

    QString filePath() const;
    Info    info() const;
    int     version() const;
    bool    isDirty() const;

    /** Replaces cached values only after the complete document passes validation. */
    bool load(QString* error = nullptr);
    /** Persists pending changes atomically; failures retain the dirty cache. */
    bool sync(QString* error = nullptr);

    bool        contains(const QString& key) const;
    QVariant    value(const QString& key, const QVariant& fallback = {}) const;
    QVariantMap values() const;
    Node        node(const QString& path);
    bool        setValue(const QString& key, const QVariant& value, QString* error = nullptr);
    bool        remove(const QString& key, QString* error = nullptr);

    /** Applies an all-or-nothing patch of relative paths to the cached document. */
    bool applyValues(QVariantMap values, QStringList removed, QString* error = nullptr);

private:
    struct Data;
    std::unique_ptr<Data> _data;
};
