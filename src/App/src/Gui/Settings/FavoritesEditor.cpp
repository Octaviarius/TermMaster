#include "FavoritesEditor.h"

#include <QAbstractTableModel>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Terminal/CommandInput.h>

namespace
{
const QStringList keys {QStringLiteral("name"),
                        QStringLiteral("text"),
                        QStringLiteral("terminator"),
                        QStringLiteral("customTerminator")};

class TerminatorDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* combo = new QComboBox(parent);
        combo->addItems({"lf", "crlf", "cr", "nul", "none", "custom"});
        return combo;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* combo = static_cast<QComboBox*>(editor);
        combo->setCurrentIndex(combo->findText(index.data(Qt::EditRole).toString()));
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        model->setData(index, static_cast<QComboBox*>(editor)->currentText(), Qt::EditRole);
    }
};
} // namespace

class FavoritesModel final : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    const QVariantList& entries() const
    {
        return _entries;
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : _entries.size();
    }
    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 4;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        {
            return {};
        }
        return QStringList {tr("Name"), tr("Command"), tr("Terminator"), tr("Custom terminator")}.value(section);
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= _entries.size() || index.column() >= keys.size())
        {
            return {};
        }
        if (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole)
        {
            return {};
        }
        return _entries.at(index.row()).toMap().value(keys.at(index.column()));
    }
    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        auto result = QAbstractTableModel::flags(index);
        if (index.isValid())
        {
            result |= Qt::ItemIsEditable;
        }
        return result;
    }
    bool setData(const QModelIndex& index, const QVariant& value, int role) override
    {
        if (role != Qt::EditRole || !index.isValid() || index.row() >= _entries.size() || index.column() >= keys.size())
        {
            return false;
        }
        auto entry = _entries.at(index.row()).toMap();
        entry.insert(keys.at(index.column()), value.toString());
        _entries[index.row()] = entry;
        emit dataChanged(index, index);
        return true;
    }
    void replace(const QVariantList& values)
    {
        beginResetModel();
        _entries = values;
        endResetModel();
    }
    void append()
    {
        if (_entries.size() >= 100)
        {
            return;
        }
        beginInsertRows({}, _entries.size(), _entries.size());
        _entries.append(QVariantMap {
            {            "name",   ""},
            {            "text",   ""},
            {      "terminator", "cr"},
            {"customTerminator",   ""}
        });
        endInsertRows();
    }
    void remove(int row)
    {
        if (row < 0 || row >= _entries.size())
        {
            return;
        }
        beginRemoveRows({}, row, row);
        _entries.removeAt(row);
        endRemoveRows();
    }

private:
    QVariantList _entries;
};

FavoritesEditor::FavoritesEditor(QWidget* parent) : SettingsFieldEditor(parent)
{
    setObjectName("commandFavoritesEditor");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _model = new FavoritesModel(this);
    _table = new QTableView(this);
    _table->setObjectName("favoritesTable");
    _table->setModel(_model);
    _table->setItemDelegateForColumn(2, new TerminatorDelegate(_table));
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setAlternatingRowColors(true);
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _table->setMinimumHeight(180);
    layout->addWidget(_table);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(Icons::icon("file-plus"), tr("Add"), this);
    add->setObjectName("favoriteAdd");
    add->setToolTip(tr("Add a named command draft"));
    auto* remove = new QPushButton(Icons::icon("x"), tr("Delete"), this);
    remove->setObjectName("favoriteDelete");
    remove->setToolTip(tr("Delete the selected favorite"));
    auto* editText = new QPushButton(Icons::icon("pencil"), tr("Edit command"), this);
    editText->setObjectName("favoriteEditText");
    editText->setToolTip(tr("Edit the selected command, including line breaks"));
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(editText);
    row->addStretch();
    layout->addLayout(row);
    _error = new QLabel(this);
    _error->setWordWrap(true);
    layout->addWidget(_error);
    connect(_model, &QAbstractItemModel::dataChanged, this, [this] { _changed(); });
    connect(add, &QPushButton::clicked, this, [this] {
        if (_model->rowCount() >= 100)
        {
            return;
        }
        _model->append();
        _table->setCurrentIndex(_model->index(_model->rowCount() - 1, 0));
        _changed();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (!_table->currentIndex().isValid())
        {
            return;
        }
        _model->remove(_table->currentIndex().row());
        _changed();
    });
    connect(editText, &QPushButton::clicked, this, [this] {
        const QPersistentModelIndex index = _model->index(_table->currentIndex().row(), 1);
        if (!index.isValid())
        {
            return;
        }
        const QPointer<FavoritesEditor> guard(this);
        bool                            ok   = false;
        const QString                   text = QInputDialog::getMultiLineText(this,
                                                                              tr("Edit command"),
                                                                              tr("UTF-8 command text"),
                                                                              index.data(Qt::EditRole).toString(),
                                                                              &ok);
        if (guard && ok && index.isValid())
        {
            _model->setData(index, text, Qt::EditRole);
        }
    });
}

QVariant FavoritesEditor::value() const
{
    return _model->entries();
}

void FavoritesEditor::setValue(const QVariant& value)
{
    _model->replace(value.toList());
    _error->setText(inputError());
}

bool FavoritesEditor::hasAcceptableInput() const
{
    return inputError().isEmpty();
}

QString FavoritesEditor::inputError() const
{
    const auto& entries = _model->entries();
    if (entries.size() > 100)
    {
        return tr("Favorites exceed the 100 command limit.");
    }
    if (QJsonDocument::fromVariant(entries).toJson(QJsonDocument::Compact).size() > 1024 * 1024)
    {
        return tr("Favorites exceed the 1 MiB UTF-8 limit.");
    }
    QSet<QString> names;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto    entry      = entries.at(i).toMap();
        const QString name       = entry.value("name").toString();
        const QString text       = entry.value("text").toString();
        const QString terminator = entry.value("terminator").toString();
        const QString custom     = entry.value("customTerminator").toString();
        QString       error;
        if (name.trimmed().isEmpty() || name.size() > 128)
        {
            error = tr("Name must contain 1 to 128 characters.");
        }
        else if (names.contains(name))
        {
            error = tr("Favorite names must be unique.");
        }
        else if (text.size() > 16384)
        {
            error = tr("Command exceeds 16,384 characters.");
        }
        else if (custom.size() > 1024)
        {
            error = tr("Custom terminator exceeds 1,024 characters.");
        }
        else
        {
            CommandInput::prepare(text, terminator, custom, &error);
        }
        if (!error.isEmpty())
        {
            return tr("Favorite %1: %2").arg(i + 1).arg(error);
        }
        names.insert(name);
    }
    return {};
}

void FavoritesEditor::_changed()
{
    _error->setText(inputError());
    emit valueChanged();
}
