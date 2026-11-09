#include "EnvironmentEditor.h"

#include "../../ConsoleEnvironment.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace
{
QString identity(const QString& name)
{
#ifdef Q_OS_WIN
    return name.toCaseFolded();
#else
    return name;
#endif
}
} // namespace

class EnvironmentEditor::Model : public QAbstractTableModel
{
public:
    explicit Model(QObject* parent) : QAbstractTableModel(parent)
    {
    }
    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : _names.size();
    }
    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 3;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        {
            return QStringList {tr("Name"), tr("Effective value"), tr("Source / status")}.value(section);
        }
        return {};
    }
    QString _overrideKey(const QString& name) const
    {
        const QVariantMap map = _draft.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
        {
            if (identity(it.key()) == identity(name))
            {
                return it.key();
            }
        }
        return {};
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= _names.size())
        {
            return {};
        }
        const QString     name          = _names.at(index.row());
        const QVariantMap map           = _draft.toMap();
        const bool        explicitValue = map.contains(name);
        const QVariant    entry         = explicitValue ? map.value(name) : QVariant(_baseline.value(name));
        if (role == Qt::ForegroundRole && (index.column() == 2 || !explicitValue))
        {
            return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
        }
        if (role != Qt::DisplayRole && role != Qt::EditRole)
        {
            return {};
        }
        if (index.column() == 0)
        {
            return name;
        }
        if (index.column() == 1)
        {
            return entry.isNull() ? QString() : entry.toString();
        }
        if (!explicitValue)
        {
            return tr("Inherited");
        }
        if (entry.isNull())
        {
            return tr("Removed");
        }
        if (entry.metaType().id() != QMetaType::QString)
        {
            return tr("Invalid value type");
        }
        return tr("Override");
    }
    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        auto result = QAbstractTableModel::flags(index);
        if (index.isValid() && index.column() < 2 && _draft.metaType().id() == QMetaType::QVariantMap)
        {
            result |= Qt::ItemIsEditable;
        }
        return result;
    }
    bool setData(const QModelIndex& index, const QVariant& value, int role) override
    {
        if (role != Qt::EditRole || !index.isValid() || index.row() >= _names.size())
        {
            return false;
        }
        const QString name = _names.at(index.row());
        QVariantMap   map  = _draft.toMap();
        if (index.column() == 1)
        {
            map.insert(name, value.toString());
        }
        else if (index.column() == 0)
        {
            const QString replacement = value.toString();
            if (replacement == name)
            {
                return false;
            }
            if (replacement.isEmpty() || !_overrideKey(replacement).isEmpty())
            {
                return false;
            }
            const QVariant previous = map.contains(name) ? map.value(name) : QVariant(_baseline.value(name));
            map.remove(name);
            if (_baseline.contains(name) && identity(name) != identity(replacement))
            {
                map.insert(name, QVariant());
            }
            map.insert(replacement, previous);
        }
        else
        {
            return false;
        }
        return _replace(map, true);
    }
    void _rebuild()
    {
        beginResetModel();
        _names = _draft.toMap().keys();
        for (const QString& name : _baseline.keys())
        {
            if (_overrideKey(name).isEmpty())
            {
                _names.append(name);
            }
        }
        _names.sort(Qt::CaseInsensitive);
        endResetModel();
    }
    bool _replace(const QVariant& value, bool notify)
    {
        if (_draft == value && _draft.metaType() == value.metaType())
        {
            return false;
        }
        _draft = value;
        _rebuild();
        if (notify && _changed)
        {
            _changed();
        }
        return true;
    }

private:
    friend class EnvironmentEditor;
    QProcessEnvironment   _baseline;
    QVariant              _draft = QVariantMap {};
    QString               _refreshError;
    QStringList           _names;
    std::function<void()> _changed;
};

EnvironmentEditor::EnvironmentEditor(QWidget* parent, BaselineProvider provider) :
    QWidget(parent), _model(new Model(this)), _table(new QTableView(this)), _error(new QLabel(this)),
    _provider(provider ? std::move(provider) : ConsoleEnvironment::userEnvironment)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* explanation = new QLabel(tr("Inherited environment is read-only until overridden. Only overrides are saved."),
                                   this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    _table->setModel(_model);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    _table->setMinimumHeight(220);
    layout->addWidget(_table);
    auto* controls = new QHBoxLayout;
    auto* add      = new QPushButton(tr("Add override"), this);
    auto* remove   = new QPushButton(tr("Remove variable"), this);
    auto* reset    = new QPushButton(tr("Reset override"), this);
    auto* refresh  = new QPushButton(tr("Refresh inherited"), this);
    for (auto* button : {add, remove, reset, refresh})
    {
        controls->addWidget(button);
    }
    controls->addStretch();
    layout->addLayout(controls);
    _error->setWordWrap(true);
    layout->addWidget(_error);
    connect(add, &QPushButton::clicked, this, [this] {
        QString name   = QStringLiteral("NEW_VARIABLE");
        int     suffix = 1;
        while (_model->_names.contains(name))
        {
            name = QStringLiteral("NEW_VARIABLE_%1").arg(suffix++);
        }
        addOverride(name, QStringLiteral(""));
        const QModelIndex index = _model->index(_model->_names.indexOf(name), 0);
        _table->setCurrentIndex(index);
        _table->edit(index);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = _table->currentIndex().row();
        if (row >= 0)
        {
            removeVariable(_model->_names.value(row));
        }
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        const int row = _table->currentIndex().row();
        if (row >= 0)
        {
            resetOverride(_model->_names.value(row));
        }
    });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshFromUser(); });
    _model->_changed = [this] {
        _updateError();
        if (_changed)
        {
            _changed();
        }
    };
    refreshFromUser();
}

QWidget* EnvironmentEditor::widget()
{
    return this;
}
QVariant EnvironmentEditor::value() const
{
    return _model->_draft;
}
void EnvironmentEditor::setValue(const QVariant& value)
{
    _model->_replace(value, false);
    _updateError();
}
void EnvironmentEditor::setChangedCallback(std::function<void()> callback)
{
    _changed = std::move(callback);
}
QString EnvironmentEditor::inputError() const
{
    if (_model->_draft.metaType().id() != QMetaType::QVariantMap)
    {
        return tr("Environment overrides must be an object.");
    }
    QString error;
    ConsoleEnvironment::validateOverrides(_model->_draft.toMap(), &error);
    return error.isEmpty() ? _model->_refreshError : error;
}
bool EnvironmentEditor::refreshFromUser(QString* error)
{
    QString             failure;
    QProcessEnvironment _baseline = _provider(&failure);
    _model->_refreshError         = failure;
    if (error)
    {
        *error = failure;
    }
    if (failure.isEmpty())
    {
        _model->_baseline = std::move(_baseline);
        _model->_rebuild();
    }
    _updateError();
    return failure.isEmpty();
}
bool EnvironmentEditor::addOverride(const QString& name, const QVariant& value)
{
    if (_model->_draft.metaType().id() != QMetaType::QVariantMap)
    {
        return false;
    }
    QVariantMap   map      = _model->_draft.toMap();
    const QString previous = _model->_overrideKey(name);
    if (!previous.isEmpty())
    {
        map.remove(previous);
    }
    map.insert(name, value);
    return _model->_replace(map, true);
}
bool EnvironmentEditor::removeVariable(const QString& name)
{
    return addOverride(name, QVariant());
}
bool EnvironmentEditor::resetOverride(const QString& name)
{
    if (_model->_draft.metaType().id() != QMetaType::QVariantMap)
    {
        return false;
    }
    QVariantMap map = _model->_draft.toMap();
    if (!map.remove(_model->_overrideKey(name)))
    {
        return false;
    }
    return _model->_replace(map, true);
}
void EnvironmentEditor::_updateError()
{
    _error->setText(inputError());
    _error->setVisible(!_error->text().isEmpty());
}
