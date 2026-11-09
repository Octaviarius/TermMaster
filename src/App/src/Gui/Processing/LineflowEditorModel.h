#pragma once
#include <QStandardItemModel>
#include <Termaster/Core/Processing/ProcessorRefactoring.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>

/** Authoritative workspace definition draft. Expanded references are linked views;
 * only nodes belonging to the selected block can be edited or reordered.
 */
class LineflowEditorModel final : public QStandardItemModel
{
    Q_OBJECT
public:
    using Rename = ProcessorRefactoring::Rename;
    enum Role
    {
        BlockRole = Qt::UserRole + 1,
        NodeRole,
        CatalogIdRole,
        LinkedRole,
        UnavailableRole
    };
    LineflowEditorModel(QVariantMap definitions, ProcessorRegistry& registry, QObject* parent = nullptr);
    QVariantMap     definitions() const;
    QVariantMap     baseline() const;
    QList<Rename>   renames() const;
    QStringList     blockIds() const;
    QString         blockName(const QString& id) const;
    QString         selectedBlock() const;
    QString         unavailableReason(const QString& id);
    void            setSelectedBlock(const QString& id);
    QVariantMap     node(const QString& block, const QString& name) const;
    QString         createBlock(const QString& name, QString* error = nullptr);
    QString         duplicateBlock(const QString& id, const QString& name, QString* error = nullptr);
    bool            removeBlock(const QString& id, QString* error = nullptr);
    bool            renameBlock(const QString& id, const QString& name, QString* error = nullptr);
    QString         insertNode(const QString& block, int row, const QString& catalogId, QString* error = nullptr);
    bool            removeNode(const QString& block, const QString& name, QString* error = nullptr);
    bool            renameNode(const QString& block, const QString& from, const QString& to, QString* error = nullptr);
    bool            moveNode(const QString& block, int from, int to, QString* error = nullptr);
    bool            setNodeConfiguration(const QString&     block,
                                         const QString&     name,
                                         const QVariantMap& configuration,
                                         QString*           error = nullptr);
    bool            validate(QString* error = nullptr) const;
    void            markApplied();
    void            markApplied(QVariantMap definitions);
    QStringList     mimeTypes() const override;
    QMimeData*      mimeData(const QModelIndexList& indexes) const override;
    bool            canDropMimeData(const QMimeData*   data,
                                    Qt::DropAction     action,
                                    int                row,
                                    int                column,
                                    const QModelIndex& parent) const override;
    bool            dropMimeData(const QMimeData*   data,
                                 Qt::DropAction     action,
                                 int                row,
                                 int                column,
                                 const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;
    bool            removeRows(int row, int count, const QModelIndex& parent = {}) override;
    Q_SIGNAL void   draftChanged();
    Q_SIGNAL void   rejected(const QString& message);

private:
    QVariantMap                           _baseline;
    QVariantMap                           _draft;
    ProcessorRegistry&                    _registry;
    QMap<QString, QMap<QString, QString>> _originalNames;
    QString                               _selected;
    QByteArray                            _token;
    QMap<QString, QString>                _availability;
    int                                   _rendered = 0;
    QString                               _unavailableReason(const QString& id, QSet<QString> path = {});
    bool                                  _commit(QVariantMap draft, QString* error);
    void                                  _rebuild();
    void _append(QStandardItem* parent, const QString& block, bool linked, QSet<QString> path, int depth);
    int  _nodeIndex(const QString& block, const QString& name) const;
};
