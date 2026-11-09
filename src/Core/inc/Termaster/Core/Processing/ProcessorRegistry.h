#pragma once

#include <QList>
#include <QMap>
#include <Termaster/Core/Processing/ProcessorCatalog.h>
#include <memory>
#include <optional>

/** Explicitly owned processor factories. Use on the application thread only.
 * Metadata queries return factories cleared, so snapshots cannot retain plugin code.
 * Last lease release cancels the native instance and defers destruction until quiescence.
 */
class ProcessorRegistry : public ProcessorCatalog
{
public:
    ProcessorRegistry();
    ~ProcessorRegistry() override;
    Q_DISABLE_COPY_MOVE(ProcessorRegistry)

    void registerType(const QString& owner, Type type) override;
    bool unregisterType(const QString& owner, const QString& id, QString* error = nullptr) override;
    bool canRemoveOwner(const QString& owner, QString* error = nullptr) const;
    bool removeOwner(const QString& owner, QString* error = nullptr);

    /** Resolves schema defaults, stored settings and private overrides in order.
     * Mapping values merge recursively; arrays and scalars replace inherited values.
     */
    std::optional<QVariantMap> resolveConfiguration(const QString&     id,
                                                    const QVariantMap& stored,
                                                    const QVariantMap& overrides,
                                                    QString*           error = nullptr) const;

    bool validateConfiguration(const QString& id, const QVariantMap& configuration, QString* error = nullptr) const;

    QList<Type>                types() const;
    std::optional<Type>        type(const QString& id) const;
    std::shared_ptr<Processor> create(const QString&           id,
                                      const QVariantMap&       configuration,
                                      const Processor::Limits& limits,
                                      QString*                 error = nullptr);

private:
    struct Entry;

    QMap<QString, std::shared_ptr<Entry>> _entries;
};
