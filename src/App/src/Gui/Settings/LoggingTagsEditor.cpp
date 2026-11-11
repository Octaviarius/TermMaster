#include "LoggingTagsEditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QScopedValueRollback>
#include <QSet>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>
#include <algorithm>

namespace
{
QStringList levels()
{
    return {"trace", "debug", "info", "warning", "error", "critical"};
}

class LevelDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* combo = new QComboBox(parent);
        combo->addItems(levels());
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

bool validTag(const QString& tag)
{
    if (tag.isEmpty() || tag.size() > 256 || tag.startsWith(':') || tag.endsWith(':') || tag.contains("::"))
    {
        return false;
    }
    for (const auto character : tag)
    {
        const auto code = character.unicode();
        if (!((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')
              || code == '_' || code == '-' || code == '.' || code == ':'))
        {
            return false;
        }
    }
    return true;
}
} // namespace

LoggingTagsEditor::LoggingTagsEditor(QWidget* parent) :
    SettingsFieldEditor(parent), _model(new QStandardItemModel(0, 2, this)), _table(new QTableView(this)),
    _add(new QToolButton(this)), _remove(new QToolButton(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    _model->setHorizontalHeaderLabels({tr("Tag"), tr("Minimum level")});
    _table->setObjectName("loggingTagsTable");
    _table->setModel(_model);
    _table->setItemDelegateForColumn(1, new LevelDelegate(_table));
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _table->verticalHeader()->hide();
    _table->setMinimumHeight(150);
    _table->setMaximumHeight(240);
    layout->addWidget(_table);

    auto* actions = new QHBoxLayout;
    _add->setObjectName("loggingTagAdd");
    _add->setIcon(Icons::icon("plus"));
    _add->setToolTip(tr("Add a tag rule"));
    _add->setAccessibleName(_add->toolTip());
    _remove->setObjectName("loggingTagRemove");
    _remove->setIcon(Icons::icon("trash"));
    _remove->setToolTip(tr("Remove selected tag rules"));
    _remove->setAccessibleName(_remove->toolTip());
    actions->addWidget(_add);
    actions->addWidget(_remove);
    actions->addStretch();
    layout->addLayout(actions);

    connect(_model, &QStandardItemModel::dataChanged, this, [this] { _changed(); });
    connect(_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { _updateActions(); });
    connect(_add, &QToolButton::clicked, this, [this] {
        if (_loadedOversize || _invalidValue.isValid() || _model->rowCount() >= 64)
        {
            return;
        }
        QSet<QString> tags;
        for (int row = 0; row < _model->rowCount(); ++row)
        {
            tags.insert(_model->index(row, 0).data().toString());
        }
        QString tag = "termaster:new";
        for (int suffix = 2; tags.contains(tag); ++suffix)
        {
            tag = QString("termaster:new%1").arg(suffix);
        }
        _model->appendRow({new QStandardItem(tag), new QStandardItem("info")});
        const auto index = _model->index(_model->rowCount() - 1, 0);
        _table->setCurrentIndex(index);
        _table->edit(index);
        _changed();
    });
    connect(_remove, &QToolButton::clicked, this, [this] {
        auto rows = _table->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& left, const QModelIndex& right) {
            return left.row() > right.row();
        });
        for (const auto& index : rows)
        {
            _model->removeRow(index.row());
        }
        _changed();
    });
    _updateActions();
}

QVariant LoggingTagsEditor::value() const
{
    if (_invalidValue.isValid())
    {
        return _invalidValue;
    }
    auto result = _overflow;
    for (int row = 0; row < _model->rowCount(); ++row)
    {
        result.insert(_model->index(row, 0).data(Qt::EditRole).toString(), _model->index(row, 1).data(Qt::EditRole));
    }
    return result;
}

void LoggingTagsEditor::setValue(const QVariant& value)
{
    const QScopedValueRollback guard(_updating, true);
    _model->removeRows(0, _model->rowCount());
    _overflow.clear();
    _invalidValue.clear();
    _loadedOversize = false;
    if (value.typeId() != QMetaType::QVariantMap)
    {
        _invalidValue = value.isValid() ? value : QVariant(QString());
    }
    else
    {
        const auto entries = value.toMap();
        _loadedOversize    = entries.size() > 64;
        for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        {
            if (_model->rowCount() >= 65)
            {
                _overflow.insert(it.key(), it.value());
                continue;
            }
            auto* level = new QStandardItem;
            level->setData(it.value(), Qt::EditRole);
            _model->appendRow({new QStandardItem(it.key()), level});
        }
    }
    _updateActions();
}

bool LoggingTagsEditor::hasAcceptableInput() const
{
    return inputError().isEmpty();
}

QString LoggingTagsEditor::inputError() const
{
    if (_invalidValue.isValid())
    {
        return tr("Tag rules must be a mapping.");
    }
    if (_loadedOversize || _model->rowCount() > 64)
    {
        return tr("At most 64 tag rules are allowed. Reset this field before editing an oversized mapping.");
    }
    QSet<QString> tags;
    for (int row = 0; row < _model->rowCount(); ++row)
    {
        const auto tag = _model->index(row, 0).data(Qt::EditRole).toString();
        if (!validTag(tag))
        {
            return tr(
                "Tag names require 1 to 256 ASCII letters, digits, dots, underscores or hyphens, separated by single colons.");
        }
        if (tags.contains(tag))
        {
            return tr("Tag names must be unique.");
        }
        tags.insert(tag);
        const auto level = _model->index(row, 1).data(Qt::EditRole);
        if (level.typeId() != QMetaType::QString || !levels().contains(level.toString()))
        {
            return tr("Select a supported minimum level for each tag.");
        }
    }
    return {};
}

void LoggingTagsEditor::_changed()
{
    if (_updating)
    {
        return;
    }
    _updateActions();
    emit valueChanged();
    emit validationChanged();
}

void LoggingTagsEditor::_updateActions()
{
    _add->setEnabled(!_loadedOversize && !_invalidValue.isValid() && _model->rowCount() < 64);
    _remove->setEnabled(!_table->selectionModel()->selectedRows().isEmpty());
}
