#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <cstdio>

/** Owns only in-memory tree ordering. A random per-tree token excludes other chains. */
class OperationTree final : public QTreeWidget
{
public:
    OperationTree();
    bool simulateDrop(QTreeWidgetItem* item, const QPoint& point, OperationTree* source = nullptr);

protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct Destination
    {
        QTreeWidgetItem* parent = nullptr;
        QTreeWidgetItem* target = nullptr;
        int              index  = -1;
        int              edge   = 0;
    };

    QByteArray       _token   = QUuid::createUuid().toByteArray();
    QTreeWidgetItem* _dragged = nullptr;
    Destination      _indicator;

    Destination _destination(const QPoint& point) const;
    bool        _valid(const QMimeData* mime, const Destination& destination) const;
    QMimeData*  _mime() const;
};

OperationTree::OperationTree()
{
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false);
}

OperationTree::Destination OperationTree::_destination(const QPoint& point) const
{
    Destination result;
    result.target = itemAt(point);
    if (!result.target)
    {
        result.parent = topLevelItem(0);
        result.index  = result.parent ? result.parent->childCount() : -1;
        return result;
    }
    const QRect rect = visualItemRect(result.target);
    result.edge      = point.y() < rect.top() + 8 ? -1 : point.y() > rect.bottom() - 8 ? 1 : 0;
    if (result.edge)
    {
        result.parent = result.target->parent();
        result.index  = result.parent ? result.parent->indexOfChild(result.target) + (result.edge > 0) : -1;
    }
    else
    {
        result.parent = result.target;
        result.index  = result.parent->childCount();
    }
    return result;
}

bool OperationTree::_valid(const QMimeData* mime, const Destination& destination) const
{
    if (!_dragged || _dragged->treeWidget() != this || !_dragged->parent()
        || mime->data("application/x-termaster-preview-chain") != _token || !destination.parent
        || destination.index < 0)
    {
        return false;
    }
    const QString type = destination.parent->data(0, Qt::UserRole + 1).toString();
    if (type != "sequence" && type != "loop")
    {
        return false;
    }
    for (auto* ancestor = destination.parent; ancestor; ancestor = ancestor->parent())
    {
        if (ancestor == _dragged)
        {
            return false;
        }
    }
    return true;
}

QMimeData* OperationTree::_mime() const
{
    auto* mime = new QMimeData;
    mime->setData("application/x-termaster-preview-chain", _token);
    return mime;
}

void OperationTree::startDrag(Qt::DropActions)
{
    _dragged = currentItem();
    if (!_dragged || !_dragged->parent())
    {
        return;
    }
    QDrag drag(this);
    drag.setMimeData(_mime());
    drag.setPixmap(_dragged->icon(0).pixmap(20, 20));
    drag.exec(Qt::MoveAction, Qt::MoveAction);
    _dragged   = nullptr;
    _indicator = {};
    viewport()->update();
}

void OperationTree::dragEnterEvent(QDragEnterEvent* event)
{
    // Enter is source validation only: a drag commonly starts over its own row.
    // The move and drop handlers validate the actual destination independently.
    if (_valid(event->mimeData(), {topLevelItem(0), nullptr, 0, 0}))
    {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

void OperationTree::dragMoveEvent(QDragMoveEvent* event)
{
    _indicator = _destination(event->position().toPoint());
    if (_valid(event->mimeData(), _indicator))
    {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    else
    {
        _indicator = {};
        event->ignore();
    }
    viewport()->update();
}

void OperationTree::dragLeaveEvent(QDragLeaveEvent* event)
{
    _indicator = {};
    viewport()->update();
    event->accept();
}

void OperationTree::dropEvent(QDropEvent* event)
{
    const Destination destination = _destination(event->position().toPoint());
    _indicator                    = {};
    viewport()->update();
    if (!_valid(event->mimeData(), destination))
    {
        event->ignore();
        return;
    }
    auto*     oldParent = _dragged->parent();
    const int oldIndex  = oldParent->indexOfChild(_dragged);
    int       index     = destination.index;
    if (oldParent == destination.parent && oldIndex < index)
    {
        --index;
    }
    {
        const QSignalBlocker blocker(this);
        oldParent->takeChild(oldIndex);
        destination.parent->insertChild(index, _dragged);
        destination.parent->setExpanded(true);
        setCurrentItem(_dragged);
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void OperationTree::paintEvent(QPaintEvent* event)
{
    QTreeWidget::paintEvent(event);
    if (!_indicator.parent)
    {
        return;
    }
    QPainter painter(viewport());
    painter.setPen(QPen(QColor("#2563eb"), 2));
    const QRect rect = visualItemRect(_indicator.target ? _indicator.target : _indicator.parent);
    if (_indicator.edge)
    {
        const int y = _indicator.edge < 0 ? rect.top() : rect.bottom();
        painter.drawLine(rect.left(), y, rect.right(), y);
    }
    else
    {
        painter.drawRect(rect.adjusted(1, 1, -2, -2));
    }
}

bool OperationTree::simulateDrop(QTreeWidgetItem* item, const QPoint& point, OperationTree* source)
{
    if (!source)
    {
        source = this;
    }
    source->_dragged = item;
    if (source == this)
    {
        setCurrentItem(item);
    }
    auto*           mime = source->_mime();
    QDragEnterEvent enter(point, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport(), &enter);
    QDragMoveEvent move(point, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport(), &move);
    QDropEvent drop(point, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport(), &drop);
    const bool accepted = enter.isAccepted() && move.isAccepted() && drop.isAccepted();
    delete mime;
    source->_dragged = nullptr;
    return accepted;
}

/** Visual interaction sketch. All draft values are memory-only; no operations execute. */
class OperationEditorPreview final : public QMainWindow
{
public:
    OperationEditorPreview();
    bool saveSnapshots(const QString& directory);
    bool smokeTest();

private:
    QTabWidget*      _directions = nullptr;
    QStackedWidget*  _pages      = nullptr;
    QTreeWidget*     _receive    = nullptr;
    QTreeWidget*     _transmit   = nullptr;
    QTreeWidgetItem* _packet     = nullptr;
    QTreeWidgetItem* _command    = nullptr;
    QTreeWidgetItem* _loop       = nullptr;

    QTreeWidget*     _createTree(const QString& description);
    QTreeWidgetItem* _add(QTreeWidget*     tree,
                          QTreeWidgetItem* parent,
                          const QString&   name,
                          const QString&   type,
                          const QString&   icon,
                          const QString&   description);
    QComboBox*       _choices(const QStringList& values);
    QSpinBox*        _number(int value, int maximum, const QString& suffix);
    QFormLayout*     _form(QTreeWidgetItem* item);
};

OperationEditorPreview::OperationEditorPreview()
{
    setWindowTitle(QStringLiteral("Termaster · Редактор операций"));
    resize(1100, 760);
    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);
    layout->setContentsMargins(22, 18, 22, 16);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("Редактор операций"));
    title->setStyleSheet("font-size: 21px; font-weight: 600;");
    layout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("Терминал «Диагностика»  /  Локальный черновик"));
    subtitle->setStyleSheet("color: #64748b;");
    layout->addWidget(subtitle);

    auto* toolbar = new QToolBar;
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto* add = toolbar->addAction(QIcon(":/icons/file-plus.svg"), QStringLiteral("Добавить операцию-заготовку"));
    auto* run = toolbar->addAction(QIcon(":/icons/play.svg"),
                                   QStringLiteral("Запуск недоступен: прототип без выполнения"));
    run->setEnabled(false);
    layout->addWidget(toolbar);

    auto* splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    _directions = new QTabWidget;
    _directions->setMinimumWidth(330);
    _receive  = _createTree(QStringLiteral("Байты транспорта → обработка → отображение"));
    _transmit = _createTree(QStringLiteral("Сценарий → команды → байты транспорта"));
    splitter->addWidget(_directions);
    _pages = new QStackedWidget;
    _pages->setMinimumWidth(460);
    splitter->addWidget(_pages);
    splitter->setSizes({370, 680});
    layout->addWidget(splitter, 1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Прототип · выполнение не подключено · изменения только в памяти"));

    auto* sequence = _add(_receive,
                          nullptr,
                          QStringLiteral("Обработка входящего потока"),
                          "sequence",
                          "folder-open",
                          QStringLiteral(
                              "Операции выполняются по порядку. Выход предыдущего шага становится входом следующего."));
    _packet        = _add(_receive,
                          sequence,
                          QStringLiteral("Выделить пакет"),
                          "packet",
                          "scan-text",
                          QStringLiteral("Собрать байты потока в пакет по выбранной границе."));
    auto* packetForm = _form(_packet);
    packetForm->addRow(QStringLiteral("Граница пакета:"),
                       _choices(
                           {QStringLiteral("Разделитель"), QStringLiteral("Длина"), QStringLiteral("Пауза в потоке")}));
    packetForm->addRow(QStringLiteral("Разделитель (HEX):"), new QLineEdit("0D 0A"));
    packetForm->addRow(QStringLiteral("Максимум:"), _number(4096, 1048576, QStringLiteral(" байт")));
    auto* keep = new QCheckBox(QStringLiteral("Сохранить разделитель в пакете"));
    packetForm->addRow(QString(), keep);
    auto* codec = _add(_receive,
                       sequence,
                       QStringLiteral("Декодировать текст"),
                       "codec",
                       "sliders-horizontal",
                       QStringLiteral("Преобразовать байты пакета в текст перед отображением."));
    _form(codec)->addRow(QStringLiteral("Кодировка:"), _choices({"UTF-8", "Windows-1251", "CP866"}));
    auto* display = _add(_receive,
                         sequence,
                         QStringLiteral("Показать в терминале"),
                         "display",
                         "terminal",
                         QStringLiteral("Конечная точка цепочки приёма: отображение обработанного текста."));
    _form(display)->addRow(QStringLiteral("Представление:"),
                           _choices({QStringLiteral("Текст"), QStringLiteral("HEX")}));

    auto* scenario    = _add(_transmit,
                             nullptr,
                             QStringLiteral("Опрос устройства"),
                             "sequence",
                             "folder-open",
                             QStringLiteral("Последовательно выполнить вложенные операции передачи."));
    _command          = _add(_transmit,
                             scenario,
                             QStringLiteral("Отправить: AT"),
                             "command",
                             "terminal",
                             QStringLiteral("Подготовить текст команды, кодировку и завершающую последовательность."));
    auto* commandForm = _form(_command);
    auto* text        = new QPlainTextEdit("AT");
    text->setFixedHeight(110);
    commandForm->addRow(QStringLiteral("Команда:"), text);
    commandForm->addRow(QStringLiteral("Кодировка:"), _choices({"UTF-8", "Windows-1251", "CP866"}));
    commandForm->addRow(QStringLiteral("Завершение:"),
                        _choices({"CR + LF", "CR", "LF", QStringLiteral("Без завершения")}));
    auto* wait = _add(_transmit,
                      scenario,
                      QStringLiteral("Дождаться ответа: OK"),
                      "wait",
                      "scan-text",
                      QStringLiteral("Продолжить сценарий после совпадения с входящим текстом."));
    _form(wait)->addRow(QStringLiteral("Ожидаемый текст:"), new QLineEdit("OK"));
    _form(wait)->addRow(QStringLiteral("Тайм-аут:"), _number(2000, 600000, QStringLiteral(" мс")));
    _loop = _add(_transmit,
                 scenario,
                 QStringLiteral("Повторить 5 раз"),
                 "loop",
                 "rotate-cw",
                 QStringLiteral("Выполнять вложенные операции по порядку заданное число раз."));
    _form(_loop)->addRow(QStringLiteral("Повторений:"), _number(5, 10000, QString()));
    auto* query = _add(_transmit,
                       _loop,
                       QStringLiteral("Отправить: STATUS?"),
                       "command",
                       "terminal",
                       QStringLiteral("Команда внутри цикла. Настройки принадлежат этому шагу."));
    _form(query)->addRow(QStringLiteral("Команда:"), new QLineEdit("STATUS?"));
    _form(query)->addRow(QStringLiteral("Завершение:"), _choices({"CR + LF", "CR", "LF"}));
    auto* delay = _add(_transmit,
                       _loop,
                       QStringLiteral("Пауза: 500 мс"),
                       "delay",
                       "rotate-cw",
                       QStringLiteral("Пауза перед переходом к следующей операции."));
    _form(delay)->addRow(QStringLiteral("Длительность:"), _number(500, 600000, QStringLiteral(" мс")));
    _receive->expandAll();
    _transmit->expandAll();
    _receive->setCurrentItem(_packet);
    connect(_directions, &QTabWidget::currentChanged, this, [this](int index) {
        auto* tree = index == 0 ? _receive : _transmit;
        if (!tree->currentItem())
        {
            tree->setCurrentItem(_command);
        }
        _pages->setCurrentIndex(tree->currentItem()->data(0, Qt::UserRole).toInt());
    });
    connect(add, &QAction::triggered, this, [this] {
        auto* tree     = _directions->currentIndex() == 0 ? _receive : _transmit;
        auto* selected = tree->currentItem();
        auto* parent   = selected;
        if (selected && selected->data(0, Qt::UserRole + 1) != "sequence"
            && selected->data(0, Qt::UserRole + 1) != "loop")
        {
            parent = selected->parent();
        }
        auto* item = _add(tree,
                          parent,
                          QStringLiteral("Новая операция"),
                          "placeholder",
                          "sliders-horizontal",
                          QStringLiteral("Заготовка операции для обсуждения структуры редактора."));
        _form(item)->addRow(QStringLiteral("Тип операции:"),
                            _choices({QStringLiteral("Преобразование"),
                                      QStringLiteral("Фильтр"),
                                      QStringLiteral("Скрипт")}));
        tree->expandAll();
        tree->setCurrentItem(item);
    });
}

QTreeWidget* OperationEditorPreview::_createTree(const QString& description)
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 16, 12, 12);
    auto* hint = new QLabel(description);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #64748b; padding-bottom: 8px;");
    layout->addWidget(hint);
    auto* tree = new OperationTree;
    tree->setHeaderHidden(true);
    tree->setIndentation(22);
    tree->setIconSize(QSize(18, 18));
    tree->setUniformRowHeights(true);
    tree->setStyleSheet(
        "QTreeWidget { border: none; } QTreeWidget::item { height: 38px; } QTreeWidget::item:selected { background: #dbeafe; color: #17365c; }");
    layout->addWidget(tree, 1);
    _directions->addTab(page,
                        _directions->count() == 0 ? QStringLiteral("Приём · Rx") : QStringLiteral("Передача · Tx"));
    connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item)
        {
            _pages->setCurrentIndex(item->data(0, Qt::UserRole).toInt());
        }
    });
    return tree;
}

QTreeWidgetItem* OperationEditorPreview::_add(QTreeWidget*     tree,
                                              QTreeWidgetItem* parent,
                                              const QString&   name,
                                              const QString&   type,
                                              const QString&   icon,
                                              const QString&   description)
{
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, name);
    if (!parent)
    {
        item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    }
    if (type != "sequence" && type != "loop")
    {
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    }
    item->setIcon(0, QIcon(":/icons/" + icon + ".svg"));
    item->setToolTip(0, description);
    item->setData(0, Qt::UserRole, _pages->count());
    item->setData(0, Qt::UserRole + 1, type);
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 12, 12);
    layout->setSpacing(18);
    auto* heading = new QLabel(name);
    heading->setStyleSheet("font-size: 18px; font-weight: 600;");
    layout->addWidget(heading);
    auto* hint = new QLabel(description);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #64748b;");
    layout->addWidget(hint);
    auto* group = new QGroupBox(QStringLiteral("Параметры операции"));
    auto* form  = new QFormLayout(group);
    form->setContentsMargins(20, 26, 20, 20);
    form->setSpacing(16);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* enabled = new QCheckBox(QStringLiteral("Операция включена"));
    enabled->setChecked(true);
    form->addRow(QString(), enabled);
    layout->addWidget(group);
    layout->addStretch();
    auto* note = new QLabel(QStringLiteral("Черновик интерфейса · параметры не сохраняются и не выполняются"));
    note->setWordWrap(true);
    note->setStyleSheet("color: #64748b; font-size: 11px;");
    layout->addWidget(note);
    _pages->addWidget(page);
    return item;
}

QComboBox* OperationEditorPreview::_choices(const QStringList& values)
{
    auto* box = new QComboBox;
    box->addItems(values);
    return box;
}

QSpinBox* OperationEditorPreview::_number(int value, int maximum, const QString& suffix)
{
    auto* box = new QSpinBox;
    box->setRange(1, maximum);
    box->setValue(value);
    box->setSuffix(suffix);
    return box;
}

QFormLayout* OperationEditorPreview::_form(QTreeWidgetItem* item)
{
    auto* page = _pages->widget(item->data(0, Qt::UserRole).toInt());
    return qobject_cast<QFormLayout*>(page->findChild<QGroupBox*>()->layout());
}

bool OperationEditorPreview::saveSnapshots(const QString& directory)
{
    if (!QDir().mkpath(directory))
    {
        return false;
    }
    _directions->setCurrentIndex(0);
    _receive->setCurrentItem(_packet);
    QApplication::processEvents();
    bool saved = grab().save(directory + "/Receive.png");
    _directions->setCurrentIndex(1);
    _transmit->setCurrentItem(_command);
    QApplication::processEvents();
    saved = grab().save(directory + "/Transmit.png") && saved;
    _transmit->setCurrentItem(_loop);
    QApplication::processEvents();
    saved            = grab().save(directory + "/Loop.png") && saved;
    auto*      tree  = static_cast<OperationTree*>(_transmit);
    const bool moved = tree->simulateDrop(_command, tree->visualItemRect(_loop).center());
    QApplication::processEvents();
    return moved && grab().save(directory + "/Reordered.png") && saved;
}

bool OperationEditorPreview::smokeTest()
{
    auto check = [](bool condition, const char* name) {
        std::fprintf(condition ? stdout : stderr, "%s: %s\n", condition ? "PASS" : "FAIL", name);
        return condition;
    };
    bool passed = true;
    _directions->setCurrentIndex(1);
    auto* tree  = static_cast<OperationTree*>(_transmit);
    auto* root  = tree->topLevelItem(0);
    auto* wait  = root->child(1);
    auto* query = _loop->child(0);
    _transmit->setCurrentItem(_command);
    auto* page  = _pages->currentWidget();
    auto* draft = page->findChild<QPlainTextEdit*>();
    draft->setPlainText("edited draft");
    QApplication::processEvents();
    passed       &= check(tree->simulateDrop(_command, tree->visualItemRect(_loop).center()), "container drop event");
    passed       &= check(_command->parent() == _loop && _loop->child(2) == _command, "nested order");
    passed       &= check(tree->currentItem() == _command && _pages->currentWidget() == page
                              && draft->toPlainText() == "edited draft",
                          "selection page and draft retained");
    passed       &= check(tree->simulateDrop(_command, tree->visualItemRect(query).topLeft() + QPoint(40, 2)),
                          "above sibling event");
    passed       &= check(_loop->child(0) == _command, "sibling index corrected");
    passed       &= check(tree->simulateDrop(_command, tree->visualItemRect(wait).bottomLeft() + QPoint(40, -2)),
                          "below sibling event");
    passed       &= check(root->child(1) == _command && _command->parent() == root, "move out of container");
    passed       &= check(!tree->simulateDrop(_command, tree->visualItemRect(query).center()), "leaf target rejected");
    passed       &= check(!tree->simulateDrop(_loop, tree->visualItemRect(query).center()), "descendant rejected");
    passed       &= check(!tree->simulateDrop(_loop, tree->visualItemRect(_loop).center()), "self target rejected");
    auto* nested  = _add(tree, _loop, "Nested sequence", "sequence", "folder-open", "Smoke fixture");
    tree->expandAll();
    QApplication::processEvents();
    passed &= check(!tree->simulateDrop(_loop, tree->visualItemRect(nested).center()),
                    "container descendant cycle rejected");
    passed &= check(!tree->simulateDrop(_command, tree->visualItemRect(root).topLeft() + QPoint(40, 2)),
                    "above root rejected");
    passed &= check(!tree->simulateDrop(root, tree->visualItemRect(_loop).center()), "root drag rejected");
    passed &= check(!tree->simulateDrop(_packet,
                                        tree->visualItemRect(_loop).center(),
                                        static_cast<OperationTree*>(_receive)),
                    "cross chain rejected");
    passed &= check(tree->simulateDrop(_command, QPoint(50, tree->viewport()->height() - 10)),
                    "viewport appends within root");
    passed &= check(root->child(root->childCount() - 1) == _command && tree->topLevelItemCount() == 1,
                    "single root retained");
    _receive->setCurrentItem(_packet);
    _directions->setCurrentIndex(0);
    _directions->setCurrentIndex(1);
    passed &= check(_pages->currentWidget() == page && draft->toPlainText() == "edited draft",
                    "draft retained across directions");
    return passed;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setFont(QFont("Segoe UI", 10));
    OperationEditorPreview window;
    window.show();
    const QStringList arguments = app.arguments();
    const int         snapshot  = arguments.indexOf("--snapshot-dir");
    if (arguments.contains("--smoke-test"))
    {
        QTimer::singleShot(200, &window, [&] { app.exit(window.smokeTest() ? 0 : 4); });
    }
    else if (snapshot >= 0)
    {
        if (snapshot + 1 >= arguments.size())
        {
            return 2;
        }
        QTimer::singleShot(200, &window, [&] { app.exit(window.saveSnapshots(arguments.at(snapshot + 1)) ? 0 : 3); });
    }
    return app.exec();
}
