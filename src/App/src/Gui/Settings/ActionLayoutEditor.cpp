#include "ActionLayoutEditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QListView>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>

ActionLayoutEditor::ActionLayoutEditor(QWidget* parent) : SettingsFieldEditor(parent)
{
    setObjectName("actionLayoutEditor");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    _model = new QStandardItemModel(this);
    _list  = new QListView(this);
    _list->setObjectName("actionLayoutList");
    _list->setModel(_model);
    _list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _list->setSelectionMode(QAbstractItemView::SingleSelection);
    _list->setMinimumHeight(84);
    _list->setMaximumHeight(96);
    _list->setToolTip(tr("Actions appear in this order. An empty list hides all buttons."));
    layout->addWidget(_list);

    auto* controls = new QHBoxLayout;
    controls->setSpacing(4);
    _selector = new QComboBox(this);
    _selector->setObjectName("actionLayoutSelector");
    _selector->setEditable(true);
    _selector->setInsertPolicy(QComboBox::NoInsert);
    _selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _selector->setMinimumContentsLength(16);
    _selector->setToolTip(tr("Choose an action or enter group:ID or command:ID for a plugin selector."));
    const QList<QPair<QString, QString>> choices {
        {              "group:connection",          tr("Connection actions (group)")},
        {                "group:settings",            tr("Settings actions (group)")},
        {                  "group:output",              tr("Output actions (group)")},
        {              "group:management", tr("Terminal management actions (group)")},
        {    "command:Terminal.LocalEcho",                          tr("Local echo")},
        {        "command:View.WrapLines",                          tr("Wrap lines")},
        {"command:Terminal.BufferedInput",                      tr("Buffered input")},
        {     "command:Terminal.SendFile",                           tr("Send file")},
        {   "command:Terminal.Processing",                          tr("Processing")},
        {       "command:Terminal.Detach",                  tr("Move to new window")},
        {       "command:Terminal.Attach",             tr("Return to terminal tabs")},
        {    "command:Terminal.CloseView",                      tr("Close terminal")}
    };
    for (const auto& choice : choices)
    {
        _selector->addItem(choice.second, choice.first);
        _selector->setItemData(_selector->count() - 1, choice.first, Qt::ToolTipRole);
    }
    controls->addWidget(_selector, 1);
    _add = new QPushButton(Icons::icon("plus"), tr("Add"), this);
    _add->setObjectName("actionLayoutAdd");
    _add->setToolTip(tr("Append the chosen action or custom selector"));
    controls->addWidget(_add);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(4);
    _remove = new QPushButton(Icons::icon("x"), QString(), this);
    _remove->setObjectName("actionLayoutRemove");
    _remove->setToolTip(tr("Remove the selected action"));
    _up = new QPushButton(Icons::icon("chevron-up"), QString(), this);
    _up->setObjectName("actionLayoutUp");
    _up->setToolTip(tr("Move the selected action up"));
    _down = new QPushButton(Icons::icon("chevron-down"), QString(), this);
    _down->setObjectName("actionLayoutDown");
    _down->setToolTip(tr("Move the selected action down"));
    for (auto* button : {_remove, _up, _down})
    {
        button->setAccessibleName(button->toolTip());
        button->setFixedWidth(button->sizeHint().height());
        actions->addWidget(button);
    }
    controls->addLayout(actions);
    layout->addLayout(controls);

    connect(_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { _refresh(); });
    connect(_selector, &QComboBox::currentTextChanged, this, [this] { _refresh(); });
    connect(_add, &QPushButton::clicked, this, [this] {
        if (!_add->isEnabled())
        {
            return;
        }
        _append(_pendingSelector());
        _list->setCurrentIndex(_model->index(_model->rowCount() - 1, 0));
        _refresh();
        emit valueChanged();
    });
    connect(_remove, &QPushButton::clicked, this, [this] {
        const int row = _list->currentIndex().row();
        if (row < 0)
        {
            return;
        }
        _model->removeRow(row);
        _list->setCurrentIndex(_model->index(qMin(row, _model->rowCount() - 1), 0));
        _refresh();
        emit valueChanged();
    });
    connect(_up, &QPushButton::clicked, this, [this] { _move(-1); });
    connect(_down, &QPushButton::clicked, this, [this] { _move(1); });
    _refresh();
}

QVariant ActionLayoutEditor::value() const
{
    QVariantList result;
    for (int row = 0; row < _model->rowCount(); ++row)
    {
        result.append(_model->item(row)->data(Qt::UserRole));
    }
    return result;
}

void ActionLayoutEditor::setValue(const QVariant& value)
{
    _model->clear();
    for (const auto& selector : value.toList())
    {
        _append(selector);
    }
    _refresh();
}

QString ActionLayoutEditor::_pendingSelector() const
{
    const int index = _selector->currentIndex();
    return index >= 0 && _selector->currentText() == _selector->itemText(index) ?
               _selector->itemData(index).toString() :
               _selector->currentText();
}

QString ActionLayoutEditor::_title(const QString& selector) const
{
    const int index = _selector->findData(selector);
    return index >= 0 ? _selector->itemText(index) : selector;
}

void ActionLayoutEditor::_append(const QVariant& selector)
{
    auto* item = new QStandardItem(_title(selector.toString()));
    item->setData(selector, Qt::UserRole);
    item->setToolTip(selector.toString());
    item->setSizeHint(QSize(0, _list->fontMetrics().height() + 4));
    _model->appendRow(item);
}

void ActionLayoutEditor::_move(int offset)
{
    const int row    = _list->currentIndex().row();
    const int target = row + offset;
    if (row < 0 || target < 0 || target >= _model->rowCount())
    {
        return;
    }
    const auto items = _model->takeRow(row);
    _model->insertRow(target, items);
    _list->setCurrentIndex(_model->index(target, 0));
    _refresh();
    emit valueChanged();
}

void ActionLayoutEditor::_refresh()
{
    const int row = _list->currentIndex().row();
    _remove->setEnabled(row >= 0);
    _up->setEnabled(row > 0);
    _down->setEnabled(row >= 0 && row + 1 < _model->rowCount());

    const QString            pending = _pendingSelector();
    const QRegularExpression syntax("^(group|command):[A-Za-z0-9_.-]+$");
    bool available = _model->rowCount() < 64 && pending.size() <= 128 && syntax.match(pending).hasMatch();
    for (int index = 0; index < _model->rowCount() && available; ++index)
    {
        available = _model->item(index)->data(Qt::UserRole).toString() != pending;
    }
    _add->setEnabled(available);
}
