#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <Termaster/Core/Processing/ProcessorCatalog.h>
#include <memory>
#include <optional>

/** Owning-thread unified factory and saved-definition catalog. Metadata contains no
 * executable callbacks. Prepared snapshots and live instances pin provider code.
 */
class ProcessorRegistry : public QObject, public ProcessorCatalog
{
    Q_OBJECT
public:
    struct Definition
    {
        QString     id;
        QString     title;
        QString     kind;
        QVariantMap source;
        QByteArray  settingsSchema;
        QVariantMap defaults;
        QVariantMap editor;
        QStringList references;
    };
    class Prepared
    {
    public:
        QVariantMap                configuration() const;
        std::shared_ptr<Processor> instantiate(const QVariantMap& overrides = {}, QString* error = nullptr) const;

    private:
        friend class ProcessorRegistry;
        std::function<std::shared_ptr<Processor>(const QVariantMap&, QString*)> _instantiate;
        QVariantMap                                                             _configuration;
        QStringList                                                             _paths;
        bool                                                                    _composite = false;
    };

    explicit ProcessorRegistry(QObject* parent = nullptr);
    ~ProcessorRegistry() override;
    Q_DISABLE_COPY_MOVE(ProcessorRegistry)

    void registerType(const QString& owner, Type type) override;
    bool unregisterType(const QString& owner, const QString& id, QString* error = nullptr) override;
    void registerProvider(const QString& owner, const QString& id, Provider provider) override;
    bool unregisterProvider(const QString& owner, const QString& id, QString* error = nullptr) override;
    bool canRemoveOwner(const QString& owner, QString* error = nullptr) const;
    bool removeOwner(const QString& owner, QString* error = nullptr);

    static std::optional<QList<Definition>> parseDefinitions(const QVariantMap& definitions, QString* error = nullptr);
    bool                                    validatePublication(const QString&           owner,
                                                                const QList<Definition>& definitions,
                                                                QString*                 error = nullptr) const;
    bool publishDefinitions(const QString& owner, const QList<Definition>& definitions, QString* error = nullptr);

    std::optional<QVariantMap> resolveConfiguration(const QString&     id,
                                                    const QVariantMap& stored,
                                                    const QVariantMap& overrides,
                                                    QString*           error = nullptr) const;
    bool validateConfiguration(const QString& id, const QVariantMap& configuration, QString* error = nullptr) const;
    QList<Type>                     types() const;
    std::optional<Type>             type(const QString& id) const;
    std::shared_ptr<const Prepared> prepare(const QString&           id,
                                            const QVariantMap&       configuration,
                                            const Processor::Limits& limits,
                                            QString*                 error = nullptr);
    std::shared_ptr<Processor>      create(const QString&           id,
                                           const QVariantMap&       configuration,
                                           const Processor::Limits& limits,
                                           QString*                 error = nullptr);

    Q_SIGNAL void changed();

private:
    struct Entry;
    struct ProviderEntry;
    QMap<QString, std::shared_ptr<Entry>>         _entries;
    QMap<QString, std::shared_ptr<ProviderEntry>> _providers;
    QList<std::shared_ptr<Entry>>                 _retired;
    QList<std::shared_ptr<ProviderEntry>>         _retiredProviders;

    static std::shared_ptr<Entry> _definitionEntry(const QString& owner, const Definition& definition);
    bool _validateGraph(const QMap<QString, std::shared_ptr<Entry>>& entries, QString* error) const;
    bool _available(const QString& id, QSet<QString>& visiting, QString* error) const;
    std::shared_ptr<const Prepared>   _prepare(const QString&           id,
                                               const QVariantMap&       configuration,
                                               const Processor::Limits& limits,
                                               QSet<QString>&           visiting,
                                               int                      depth,
                                               int&                     instances,
                                               QString*                 error);
    static std::shared_ptr<Processor> _create(const Type::Factory&        factory,
                                              const QVariantMap&          configuration,
                                              const Processor::Limits&    limits,
                                              std::shared_ptr<const void> lease,
                                              QString*                    error);
};
