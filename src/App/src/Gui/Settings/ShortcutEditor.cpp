#include "ShortcutEditor.h"

#include <QAbstractTableModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/CommandShortcuts.h>

class ShortcutModel final : public QAbstractTableModel
{
public:
    explicit ShortcutModel(CommandRegistry& source, QObject* parent) : QAbstractTableModel(parent), _registry(source)
    {
        rebuild();
        connect(&source, &CommandRegistry::commandAdded, this, [this] { rebuild(); });
        connect(&source, &CommandRegistry::commandRemoved, this, [this] { rebuild(); });
    }
    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : _ids.size();
    }
    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 3;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        {
            return {};
        }
        return QStringList {tr("Command ID"), tr("Label"), tr("Shortcut")}.value(section);
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= _ids.size())
        {
            return {};
        }
        const auto  id      = _ids[index.row()];
        const auto* command = _registry.find(id);
        if (!command)
        {
            return {};
        }
        if (role == Qt::UserRole)
        {
            return id;
        }
        if (role != Qt::DisplayRole)
        {
            return {};
        }
        if (index.column() == 0)
        {
            return id;
        }
        if (index.column() == 1)
        {
            return QString(command->text()).remove('&');
        }
        return sequence(id).toString(QKeySequence::NativeText);
    }
    QKeySequence sequence(const QString& id) const
    {
        if (_draft.contains(id))
        {
            return QKeySequence::fromString(_draft.value(id).toString(), QKeySequence::PortableText);
        }
        const auto* command = _registry.find(id);
        return command ? command->defaultShortcut() : QKeySequence {};
    }
    QVariantMap assignments() const
    {
        return _draft;
    }
    void setAssignments(const QVariantMap& values)
    {
        _draft = values;
        rebuild();
    }
    void assign(const QString& id, const QString& value)
    {
        _draft.insert(id, value);
    }
    void reset(const QString& id)
    {
        _draft.remove(id);
    }
    void resetAll()
    {
        for (const auto& id : _ids)
        {
            _draft.remove(id);
        }
    }
    void rebuild()
    {
        beginResetModel();
        _ids.clear();
        for (const auto* command : _registry.commands())
        {
            _ids.append(command->name());
        }
        endResetModel();
    }

private:
    CommandRegistry& _registry;
    QVariantMap      _draft;
    QStringList      _ids;
};

ShortcutEditor::ShortcutEditor(CommandRegistry& registry, CommandShortcuts& shortcuts, QWidget* parent) :
    SettingsFieldEditor(parent), _shortcuts(shortcuts)
{
    setObjectName("shortcutEditor");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* search = new QLineEdit(this);
    search->setObjectName("shortcutSearch");
    search->setPlaceholderText(tr("Filter command ID, label or shortcut"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    _model  = new ShortcutModel(registry, this);
    _filter = new QSortFilterProxyModel(this);
    _filter->setSourceModel(_model);
    _filter->setFilterKeyColumn(-1);
    _filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    _table = new QTableView(this);
    _table->setObjectName("shortcutTable");
    _table->setModel(_filter);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setAlternatingRowColors(true);
    _table->verticalHeader()->hide();
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->setMinimumHeight(260);
    layout->addWidget(_table, 1);
    auto* row = new QHBoxLayout;
    _sequence = new QKeySequenceEdit(this);
    _sequence->setObjectName("shortcutSequence");
    _sequence->setClearButtonEnabled(true);
    row->addWidget(_sequence, 1);
    auto* clear    = new QPushButton(tr("Clear"), this);
    auto* reset    = new QPushButton(tr("Reset"), this);
    auto* resetAll = new QPushButton(tr("Reset all"), this);
    clear->setObjectName("shortcutClear");
    reset->setObjectName("shortcutReset");
    resetAll->setObjectName("shortcutResetAll");
    clear->setToolTip(tr("Disable the selected command shortcut."));
    reset->setToolTip(tr("Restore the selected command's default shortcut."));
    resetAll->setToolTip(tr("Restore defaults for all currently registered commands."));
    row->addWidget(clear);
    row->addWidget(reset);
    row->addWidget(resetAll);
    layout->addLayout(row);
    _error = new QLabel(this);
    _error->setWordWrap(true);
    layout->addWidget(_error);
    connect(search, &QLineEdit::textChanged, _filter, &QSortFilterProxyModel::setFilterFixedString);
    connect(_table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { _selectionChanged(); });
    connect(_sequence, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence& sequence) {
        const auto id = _selectedId();
        if (id.isEmpty())
        {
            return;
        }
        _model->assign(id, sequence.toString(QKeySequence::PortableText));
        _changed();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        const auto id = _selectedId();
        if (id.isEmpty())
        {
            return;
        }
        _model->assign(id, QString {});
        _changed();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        _model->reset(_selectedId());
        _changed();
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        _model->resetAll();
        _changed();
    });
    const auto registryChanged = [this] {
        _error->setText(inputError());
        _selectionChanged();
        emit validationChanged();
    };
    connect(&registry, &CommandRegistry::commandAdded, this, registryChanged);
    connect(&registry, &CommandRegistry::commandRemoved, this, registryChanged);
    _selectionChanged();
}
QVariant ShortcutEditor::value() const
{
    return _model->assignments();
}
void ShortcutEditor::setValue(const QVariant& value)
{
    _model->setAssignments(value.toMap());
    _error->setText(inputError());
    _selectionChanged();
}
bool ShortcutEditor::hasAcceptableInput() const
{
    return inputError().isEmpty();
}
QString ShortcutEditor::inputError() const
{
    return _shortcuts.validateAssignments(_model->assignments());
}
QString ShortcutEditor::_selectedId() const
{
    return _table->currentIndex().data(Qt::UserRole).toString();
}
void ShortcutEditor::_selectionChanged()
{
    const QSignalBlocker guard(_sequence);
    const auto           id = _selectedId();
    _sequence->setEnabled(!id.isEmpty());
    const auto sequence = _model->sequence(id);
    if (_sequence->keySequence() != sequence)
    {
        _sequence->setKeySequence(sequence);
    }
}
void ShortcutEditor::_changed()
{
    if (_model->rowCount() > 0)
    {
        emit _model->dataChanged(_model->index(0, 0), _model->index(_model->rowCount() - 1, 2));
    }
    _error->setText(inputError());
    _selectionChanged();
    emit valueChanged();
}
