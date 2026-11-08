#include "PluginTableModel.h"

#include <QApplication>
#include <QFileInfo>
#include <QStyle>

PluginTableModel::PluginTableModel(PluginManager& manager, QObject* parent) :
    QAbstractTableModel(parent), _manager(&manager)
{
    connect(&manager, &QAbstractItemModel::modelAboutToBeReset, this, [this]() { beginResetModel(); });
    connect(&manager, &QAbstractItemModel::modelReset, this, [this]() { endResetModel(); });
    connect(&manager,
            &QAbstractItemModel::dataChanged,
            this,
            [this](const QModelIndex& first, const QModelIndex& last) {
                emit dataChanged(index(first.row(), 0), index(last.row(), ColumnCount - 1));
            });
    connect(&manager, &QObject::destroyed, this, [this]() {
        beginResetModel();
        _manager = nullptr;
        endResetModel();
    });
}

int PluginTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() || !_manager ? 0 : _manager->rowCount();
}

int PluginTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PluginTableModel::data(const QModelIndex& index, int role) const
{
    if (!_manager || !index.isValid() || index.model() != this || index.row() >= rowCount()
        || index.column() >= ColumnCount)
    {
        return {};
    }

    const QModelIndex source = _manager->index(index.row(), 0);
    if (role == SortRole)
    {
        return index.column() == Enabled ? source.data(PluginManager::EnabledRole) : data(index, Qt::DisplayRole);
    }
    if (role >= PluginManager::IdRole && role <= PluginManager::ReverseDependenciesRole)
    {
        return source.data(role);
    }

    const auto status = static_cast<PluginManager::Status>(source.data(PluginManager::StatusRole).toInt());
    if (role == FilterRole)
    {
        return source.data(PluginManager::NameRole).toString() + ' ' + source.data(PluginManager::IdRole).toString()
             + ' ' + source.data(PluginManager::VersionRole).toString() + ' ' + statusText(status) + ' '
             + source.data(PluginManager::DependenciesRole).toStringList().join(' ') + ' '
             + source.data(PluginManager::ErrorRole).toString();
    }
    if (role == Qt::CheckStateRole && index.column() == Enabled)
    {
        return source.data(PluginManager::EnabledRole).toBool() ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::TextAlignmentRole && index.column() == Enabled)
    {
        return static_cast<int>(Qt::AlignCenter);
    }
    if (role == Qt::ToolTipRole)
    {
        const QString text = source.data(PluginManager::ErrorRole).toString() + '\n'
                           + source.data(PluginManager::PathRole).toString();
        return QStringLiteral("<qt>%1</qt>").arg(text.trimmed().toHtmlEscaped().replace('\n', "<br>"));
    }
    if (role == Qt::DecorationRole && index.column() == Status
        && (status == PluginManager::Status::Failed || status == PluginManager::Status::Invalid))
    {
        return QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
    }
    if (role == Qt::AccessibleTextRole && index.column() == Enabled)
    {
        return source.data(PluginManager::EnabledRole).toBool() ? tr("Enabled") : tr("Disabled");
    }
    if (role != Qt::DisplayRole)
    {
        return {};
    }

    switch (index.column())
    {
        case Name:
        {
            const QString name = source.data(PluginManager::NameRole).toString();
            return name.isEmpty() ? QFileInfo(source.data(PluginManager::PathRole).toString()).fileName() : name;
        }
        case Id:
            return source.data(PluginManager::IdRole);
        case Version:
            return source.data(PluginManager::VersionRole);
        case Status:
            return statusText(status);
        default:
            return {};
    }
}

QVariant PluginTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return {};
    }
    switch (section)
    {
        case Enabled:
            return tr("Enabled");
        case Name:
            return tr("Plugin");
        case Id:
            return tr("ID");
        case Version:
            return tr("Version");
        case Status:
            return tr("State");
        default:
            return {};
    }
}

Qt::ItemFlags PluginTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    auto result = QAbstractTableModel::flags(index);
    if (_manager && _manager->isActive() && index.column() == Enabled
        && !index.data(PluginManager::IdRole).toString().isEmpty()
        && (index.data(PluginManager::StatusRole).toInt() != static_cast<int>(PluginManager::Status::Invalid)
            || index.data(PluginManager::EnabledRole).toBool()))
    {
        result |= Qt::ItemIsUserCheckable;
    }
    return result;
}

bool PluginTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::CheckStateRole || !(flags(index) & Qt::ItemIsUserCheckable))
    {
        return false;
    }
    if (value.toInt() == Qt::Checked
        && index.data(PluginManager::StatusRole).toInt() == static_cast<int>(PluginManager::Status::Invalid))
    {
        return false;
    }
    emit enabledChangeRequested(index.data(PluginManager::IdRole).toString(), value.toInt() == Qt::Checked);
    return true;
}

QString PluginTableModel::statusText(PluginManager::Status status)
{
    switch (status)
    {
        case PluginManager::Status::Disabled:
            return tr("Disabled");
        case PluginManager::Status::Blocked:
            return tr("Waiting for dependencies");
        case PluginManager::Status::Stopped:
            return tr("Stopped");
        case PluginManager::Status::Starting:
            return tr("Starting");
        case PluginManager::Status::Running:
            return tr("Running");
        case PluginManager::Status::Stopping:
            return tr("Stopping");
        case PluginManager::Status::Failed:
            return tr("Failed");
        case PluginManager::Status::Invalid:
            return tr("Invalid");
    }
    return {};
}
