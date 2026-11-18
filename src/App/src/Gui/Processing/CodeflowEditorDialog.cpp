#include "CodeflowEditorDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QThread>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Settings/SettingsFormWidget.h>
#include <Termaster/Core/Processing/Codeflow.h>
#include <atomic>

namespace
{
constexpr qsizetype maxSource = 2 * 1024 * 1024;
QByteArray          digest(const QByteArray& value)
{
    return QCryptographicHash::hash(value, QCryptographicHash::Sha256);
}
class PythonHighlighter final : public QSyntaxHighlighter
{
public:
    explicit PythonHighlighter(QTextDocument* document) : QSyntaxHighlighter(document)
    {
    }

protected:
    void highlightBlock(const QString& text) override
    {
        const auto mark = [this, &text](const QString& expression, QColor color) {
            auto matches = QRegularExpression(expression).globalMatch(text);
            while (matches.hasNext())
            {
                const auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), color);
            }
        };
        mark(
            "\\b(False|None|True|and|as|assert|async|await|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|in|is|lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield|match|case)\\b",
            QColor("#7961b8"));
        mark("\\b(?:0[xX][0-9a-fA-F_]+|0[bB][01_]+|[0-9][0-9_]*(?:\\.[0-9_]*)?(?:[eE][+-]?[0-9_]+)?)\\b",
             QColor("#397cb3"));
        mark("@[A-Za-z_][A-Za-z_0-9.]*", QColor("#b77b29"));
        int state = previousBlockState();
        int i     = 0;
        setCurrentBlockState(0);
        while (i < text.size())
        {
            const int start = i;
            if (state == 1 || state == 2)
            {
                const QString delimiter = state == 1 ? QStringLiteral("'''") : QStringLiteral("\"\"\"");
                const int     end       = text.indexOf(delimiter, i);
                if (end < 0)
                {
                    setFormat(i, text.size() - i, QColor("#44854a"));
                    setCurrentBlockState(state);
                    return;
                }
                i     = end + 3;
                state = 0;
                setFormat(start, i - start, QColor("#44854a"));
                continue;
            }
            if (text[i] == '#')
            {
                setFormat(i, text.size() - i, QColor("#879187"));
                break;
            }
            if (text[i] != '\'' && text[i] != '"')
            {
                ++i;
                continue;
            }
            const QChar quote = text[i];
            if (text.mid(i, 3) == QString(3, quote))
            {
                i             += 3;
                const int end  = text.indexOf(QString(3, quote), i);
                if (end < 0)
                {
                    setFormat(start, text.size() - start, QColor("#44854a"));
                    setCurrentBlockState(quote == '\'' ? 1 : 2);
                    return;
                }
                i = end + 3;
            }
            else
            {
                ++i;
                while (i < text.size())
                {
                    if (text[i] == '\\')
                    {
                        i += 2;
                        continue;
                    }
                    if (text[i++] == quote)
                    {
                        break;
                    }
                }
            }
            setFormat(start, i - start, QColor("#44854a"));
        }
    }
};
class PythonEditor final : public QPlainTextEdit
{
    class Gutter final : public QWidget
    {
        PythonEditor* _editor;

    public:
        explicit Gutter(PythonEditor* editor) : QWidget(editor), _editor(editor)
        {
        }
        void paintEvent(QPaintEvent*) override
        {
            _editor->paintNumbers(this);
        }
    };
    Gutter* _gutter;

public:
    explicit PythonEditor(QWidget* parent) : QPlainTextEdit(parent), _gutter(new Gutter(this))
    {
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setLineWrapMode(NoWrap);
        setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
        new PythonHighlighter(document());
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { margins(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect&, int) { _gutter->update(); });
        auto* comment = new QShortcut(QKeySequence("Ctrl+/"), this);
        connect(comment, &QShortcut::activated, this, [this] { indent(0); });
        auto* plus = new QShortcut(QKeySequence("Ctrl++"), this);
        connect(plus, &QShortcut::activated, this, [this] {
            zoomIn();
            margins();
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
            QList<QTextEdit::ExtraSelection> highlights;
            const QString                    source   = toPlainText();
            const QString                    pairs    = "()[]{}";
            int                              position = textCursor().position();
            if (position > 0 && pairs.contains(source[position - 1]))
            {
                --position;
            }
            if (position < source.size())
            {
                const int kind = pairs.indexOf(source[position]);
                if (kind >= 0)
                {
                    const int step  = kind % 2 == 0 ? 1 : -1;
                    int       depth = 1;
                    for (int index = position + step;
                         index >= 0 && index < source.size() && qAbs(index - position) < 100000;
                         index += step)
                    {
                        if (source[index] == source[position])
                        {
                            ++depth;
                        }
                        if (source[index] == pairs[kind + step])
                        {
                            --depth;
                        }
                        if (!depth)
                        {
                            for (const int match : {position, index})
                            {
                                QTextEdit::ExtraSelection item;
                                item.cursor = textCursor();
                                item.cursor.setPosition(match);
                                item.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
                                item.format.setBackground(QColor("#b9d6e5"));
                                highlights.append(item);
                            }
                            break;
                        }
                    }
                }
            }
            setExtraSelections(highlights);
        });
        auto* minus = new QShortcut(QKeySequence("Ctrl+-"), this);
        connect(minus, &QShortcut::activated, this, [this] {
            zoomOut();
            margins();
        });
        margins();
    }
    void margins()
    {
        setViewportMargins(16 + fontMetrics().horizontalAdvance('9') * QString::number(blockCount()).size(), 0, 0, 0);
    }
    void paintNumbers(QWidget* gutter)
    {
        QPainter painter(gutter);
        painter.fillRect(gutter->rect(), palette().alternateBase());
        painter.setPen(palette().color(QPalette::Text));
        QTextBlock block = firstVisibleBlock();
        while (block.isValid())
        {
            const QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
            if (rect.top() > height())
            {
                break;
            }
            if (block.isVisible())
            {
                painter.drawText(0,
                                 qRound(rect.top()),
                                 gutter->width() - 5,
                                 fontMetrics().height(),
                                 Qt::AlignRight,
                                 QString::number(block.blockNumber() + 1));
            }
            block = block.next();
        }
    }
    void indent(int direction)
    {
        QTextCursor cursor = textCursor();
        const int   end    = cursor.selectionEnd();
        cursor.setPosition(cursor.selectionStart());
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.beginEditBlock();
        int remaining = document()->findBlock(end).blockNumber() - cursor.blockNumber() + 1;
        while (remaining-- > 0)
        {
            if (direction > 0)
            {
                cursor.insertText("    ");
            }
            else if (direction < 0)
            {
                for (int i = 0; i < 4 && document()->characterAt(cursor.position()) == ' '; ++i)
                {
                    cursor.deleteChar();
                }
            }
            else if (document()->characterAt(cursor.position()) == '#')
            {
                cursor.deleteChar();
                if (document()->characterAt(cursor.position()) == ' ')
                {
                    cursor.deleteChar();
                }
            }
            else
            {
                cursor.insertText("# ");
            }
            cursor.movePosition(QTextCursor::NextBlock);
        }
        cursor.endEditBlock();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPlainTextEdit::paintEvent(event);
        QPainter painter(viewport());
        QColor   color = palette().color(QPalette::Text);
        color.setAlpha(35);
        painter.setPen(color);
        QTextBlock block   = firstVisibleBlock();
        const int  advance = fontMetrics().horizontalAdvance(' ');
        while (block.isValid())
        {
            const QRectF rectangle = blockBoundingGeometry(block).translated(contentOffset());
            if (rectangle.top() > viewport()->height())
            {
                break;
            }
            const QString text   = block.text();
            int           spaces = 0;
            while (spaces < text.size() && text[spaces] == ' ')
            {
                ++spaces;
            }
            for (int column = 4; column <= spaces; column += 4)
            {
                const int x = qRound(contentOffset().x()) + column * advance;
                painter.drawLine(x, qRound(rectangle.top()), x, qRound(rectangle.bottom()));
            }
            block = block.next();
        }
    }
    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        _gutter->setGeometry(0, 0, viewportMargins().left(), height());
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)
        {
            indent(event->key() == Qt::Key_Tab ? 1 : -1);
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            const QString before = textCursor().block().text().left(textCursor().positionInBlock());
            QString       spaces = QRegularExpression("^\\s*").match(before).captured();
            if (before.trimmed().endsWith(':'))
            {
                spaces += "    ";
            }
            QPlainTextEdit::keyPressEvent(event);
            insertPlainText(spaces);
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};
} // namespace

struct CodeflowEditorDialog::Data
{
    QVariantMap                           baseline;
    QVariantMap                           cleanMetadata;
    QVariantMap                           cleanParameters;
    bool                                  parametersInitialized = false;
    QVariantMap                           testDefinitions;
    QList<QPointer<QThread>>              jobs;
    QSpinBox*                             startupTimeout = nullptr;
    QSpinBox*                             packetTimeout  = nullptr;
    QSpinBox*                             stopTimeout    = nullptr;
    QVariantMap                           description;
    QVariantMap                           originalSource;
    SettingsWidgetRegistry&               widgets;
    Apply                                 apply;
    QString                               id;
    QString                               directory;
    QString                               workspaceDirectory;
    QString                               loadedPath;
    QByteArray                            loadedDigest;
    QByteArray                            checkedDigest;
    PythonEditor*                         editor      = nullptr;
    QLineEdit*                            name        = nullptr;
    QLineEdit*                            file        = nullptr;
    QLineEdit*                            python      = nullptr;
    QLineEdit*                            className   = nullptr;
    QComboBox*                            port        = nullptr;
    QComboBox*                            format      = nullptr;
    QComboBox*                            encoding    = nullptr;
    QPlainTextEdit*                       input       = nullptr;
    QPlainTextEdit*                       output      = nullptr;
    QTextBrowser*                         diagnostics = nullptr;
    QProcess*                             environment = nullptr;
    QLabel*                               status      = nullptr;
    QPlainTextEdit*                       ports       = nullptr;
    QVBoxLayout*                          properties  = nullptr;
    SettingsFormWidget*                   formWidget  = nullptr;
    SettingsFormModel*                    form        = nullptr;
    std::unique_ptr<Settings>             settings;
    std::shared_ptr<const SettingsSchema> schema;
    std::shared_ptr<std::atomic_bool>     cancelled;
    bool                                  busy     = false;
    bool                                  released = false;
    QVariantMap                           source() const
    {
        QVariantMap result = originalSource;
        result.remove("description");
        result.remove("digest");
        result.insert("code", editor->toPlainText());
        QString storedFile = file->text();
        if (!workspaceDirectory.isEmpty() && QDir::isAbsolutePath(storedFile))
        {
            const QString relative = QDir(workspaceDirectory).relativeFilePath(storedFile);
            if (relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative))
            {
                storedFile = relative;
            }
        }
        result.insert("file", storedFile);
        result.insert("startupTimeoutMs", startupTimeout->value());
        result.insert("timeoutMs", packetTimeout->value());
        result.insert("stopTimeoutMs", stopTimeout->value());
        result.insert("python", python->text());
        result.insert("className", className->text());
        result.insert("directory", workspaceDirectory.isEmpty() ? directory : workspaceDirectory);
        return result;
    }
    QVariantMap metadata() const
    {
        auto values = source();
        values.remove("code");
        values.remove("directory");
        values.insert("displayName", name->text());
        return values;
    }
    QVariantMap parameters() const
    {
        if (!form)
        {
            return {};
        }
        auto values = settings ? settings->node("configuration").values() : QVariantMap {};
        for (const auto& field : form->schema().fields())
        {
            values.remove(field.key);
        }
        const auto activeDraft = form->values();
        for (auto it = activeDraft.begin(); it != activeDraft.end(); ++it)
        {
            values.insert(it.key(), it.value());
        }
        for (const auto& field : form->schema().fields())
        {
            if (form->contains(field.key))
            {
                values.insert(field.key, form->value(field.key));
            }
        }
        return values;
    }
    bool isDirty() const
    {
        return editor->document()->isModified() || metadata() != cleanMetadata
            || (form && (!form->isValid() || (parametersInitialized && parameters() != cleanParameters)));
    }
    QString resolvedPath() const
    {
        return QDir::isAbsolutePath(file->text()) ?
                   file->text() :
                   QDir(workspaceDirectory.isEmpty() ? directory : workspaceDirectory).absoluteFilePath(file->text());
    }
    QByteArray revision() const
    {
        return digest(SettingsSchema::serializeValue(source()));
    }
    void message(const QString& text)
    {
        QString                  html = text.left(65536).toHtmlEscaped();
        const QRegularExpression lines("line ([0-9]+)");
        html.replace(lines, "<a href=\"line:\\1\">line \\1</a>");
        diagnostics->setHtml("<pre>" + html + "</pre>");
    }
    Data(SettingsWidgetRegistry& registry, Apply callback) : widgets(registry), apply(std::move(callback))
    {
    }
};

CodeflowEditorDialog::CodeflowEditorDialog(QVariantMap             definition,
                                           SettingsWidgetRegistry& widgets,
                                           Apply                   apply,
                                           QWidget*                parent) :
    QDialog(parent), _data(std::make_unique<Data>(widgets, std::move(apply)))
{
    auto& d    = *_data;
    d.baseline = definition;
    d.id       = definition.value("id").toString();
    if (d.id.isEmpty())
    {
        d.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    d.originalSource = definition.value("source").toMap();
    setWindowTitle(tr("Codeflow editor"));
    resize(1180, 800);
    auto* layout  = new QVBoxLayout(this);
    auto* toolbar = new QToolBar(this);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    layout->addWidget(toolbar);
    const auto action = [this, toolbar](const QString&         title,
                                        QStyle::StandardPixmap icon,
                                        const QKeySequence&    shortcut,
                                        std::function<void()>  callback) {
        auto* item = toolbar->addAction(style()->standardIcon(icon), title);
        item->setToolTip(title);
        item->setShortcut(shortcut);
        item->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(item, &QAction::triggered, this, std::move(callback));
    };
    action(tr("Save"), QStyle::SP_DialogSaveButton, QKeySequence::Save, [this] { _save(); });
    action(tr("Check"), QStyle::SP_DialogApplyButton, QKeySequence("Ctrl+Return"), [this] { _check(); });
    action(tr("Test"), QStyle::SP_MediaPlay, QKeySequence("Ctrl+T"), [this] { _test(); });
    action(tr("Apply"), QStyle::SP_DialogOkButton, QKeySequence("Ctrl+Shift+Return"), [this] { _check(true); });
    toolbar->addSeparator();
    action(tr("Restart"), QStyle::SP_BrowserReload, {}, [this] { emit restartRequested(_data->id); });
    action(tr("Stop"), QStyle::SP_MediaStop, {}, [this] {
        if (_data->cancelled)
        {
            _data->cancelled->store(true);
        }
        emit stopRequested(_data->id);
    });
    action(tr("Help"), QStyle::SP_DialogHelpButton, QKeySequence::HelpContents, [this] { emit helpRequested(); });
    auto* fields = new QFormLayout;
    fields->setLabelAlignment(Qt::AlignRight);
    d.name      = new QLineEdit(definition.value("name", "Codeflow 1").toString(), this);
    d.file      = new QLineEdit(d.originalSource.value("file").toString(), this);
    d.python    = new QLineEdit(d.originalSource.value("python", Codeflow::defaultPython()).toString(), this);
    d.className = new QLineEdit(d.originalSource.value("className", "Codeflow").toString(), this);
    fields->addRow(tr("Display name:"), d.name);
    const auto browseRow = [this](QLineEdit* edit, std::function<void()> browse) {
        auto* row = new QWidget(this);
        auto* box = new QHBoxLayout(row);
        box->setContentsMargins(0, 0, 0, 0);
        box->addWidget(edit);
        auto* button = new QPushButton(tr("Browse…"), row);
        box->addWidget(button);
        connect(button, &QPushButton::clicked, this, std::move(browse));
        return row;
    };
    fields->addRow(tr("Source file:"), browseRow(d.file, [this] {
                       auto&         state = *_data;
                       const QString path  = QFileDialog::getOpenFileName(this,
                                                                          tr("Open Python source"),
                                                                          state.directory,
                                                                          tr("Python (*.py);;All files (*)"));
                       if (path.isEmpty())
                       {
                           return;
                       }
                       if (state.editor->document()->isModified()
                           && QMessageBox::question(this, tr("Open source"), tr("Discard the current source draft?"))
                                  != QMessageBox::Yes)
                       {
                           return;
                       }
                       QFile file(path);
                       if (!file.open(QIODevice::ReadOnly) || file.size() > maxSource)
                       {
                           state.message(tr("Source cannot be read or exceeds 2 MiB."));
                           return;
                       }
                       const QByteArray bytes = file.readAll();
                       state.file->setText(path);
                       state.loadedPath   = path;
                       state.loadedDigest = digest(bytes);
                       state.editor->setPlainText(QString::fromUtf8(bytes));
                       state.editor->document()->setModified(false);
                   }));
    fields->addRow(tr("Python executable:"), browseRow(d.python, [this] {
                       const QString path = QFileDialog::getOpenFileName(this,
                                                                         tr("Select Python executable"),
                                                                         _data->directory);
                       if (!path.isEmpty())
                       {
                           _data->python->setText(path);
                       }
                   }));
    fields->addRow(tr("Processor class:"), d.className);
    const auto timeoutControl = [this, &d](const QString& key, int fallback) {
        auto* control = new QSpinBox(this);
        control->setRange(20, 30000);
        control->setSuffix(tr(" ms"));
        control->setObjectName(key);
        control->setValue(d.originalSource.value(key, fallback).toInt());
        return control;
    };
    d.startupTimeout    = timeoutControl("startupTimeoutMs", 3500);
    d.packetTimeout     = timeoutControl("timeoutMs", 1000);
    d.stopTimeout       = timeoutControl("stopTimeoutMs", 250);
    auto* runtime       = new QWidget(this);
    auto* runtimeLayout = new QHBoxLayout(runtime);
    runtimeLayout->setContentsMargins(0, 0, 0, 0);
    runtimeLayout->addWidget(new QLabel(tr("Startup:"), runtime));
    runtimeLayout->addWidget(d.startupTimeout);
    runtimeLayout->addWidget(new QLabel(tr("Packet:"), runtime));
    runtimeLayout->addWidget(d.packetTimeout);
    runtimeLayout->addWidget(new QLabel(tr("Stop grace:"), runtime));
    runtimeLayout->addWidget(d.stopTimeout);
    fields->addRow(tr("Runtime limits:"), runtime);
    layout->addLayout(fields);
    auto* environmentActions = new QHBoxLayout;
    auto* venv               = new QPushButton(tr("Create virtual environment…"), this);
    auto* packages           = new QPushButton(tr("Install local packages…"), this);
    environmentActions->addWidget(venv);
    environmentActions->addWidget(packages);
    environmentActions->addStretch();
    layout->addLayout(environmentActions);
    connect(venv, &QPushButton::clicked, this, [this] { _environment(false); });
    connect(packages, &QPushButton::clicked, this, [this] { _environment(true); });
    auto* find   = new QHBoxLayout;
    auto* search = new QLineEdit(this);
    search->setPlaceholderText(tr("Find (literal, case sensitive)"));
    auto* replacement = new QLineEdit(this);
    replacement->setPlaceholderText(tr("Replace with"));
    find->addWidget(search);
    find->addWidget(replacement);
    auto* next       = new QPushButton(tr("Find next"), this);
    auto* replace    = new QPushButton(tr("Replace"), this);
    auto* replaceAll = new QPushButton(tr("Replace all"), this);
    find->addWidget(next);
    find->addWidget(replace);
    find->addWidget(replaceAll);
    layout->addLayout(find);
    auto* vertical   = new QSplitter(Qt::Vertical, this);
    auto* horizontal = new QSplitter(Qt::Horizontal, vertical);
    d.editor         = new PythonEditor(horizontal);
    d.editor->setPlainText(d.originalSource.value("code", Codeflow::templateSource()).toString());
    d.editor->document()->setModified(false);
    auto* preview      = new QTabWidget(horizontal);
    auto* propertyHost = new QWidget(preview);
    d.properties       = new QVBoxLayout(propertyHost);
    d.properties->addWidget(new QLabel(tr("Check the source to preview declared properties."), propertyHost));
    preview->addTab(propertyHost, tr("Properties"));
    d.ports = new QPlainTextEdit(preview);
    d.ports->setReadOnly(true);
    preview->addTab(d.ports, tr("Ports"));
    horizontal->setStretchFactor(0, 3);
    horizontal->setStretchFactor(1, 1);
    auto* testHost    = new QWidget(vertical);
    auto* tests       = new QVBoxLayout(testHost);
    auto* testOptions = new QHBoxLayout;
    d.port            = new QComboBox(testHost);
    d.port->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    d.port->addItem(tr("Rx input"), 0);
    d.port->addItem(tr("Tx input"), 1);
    d.format = new QComboBox(testHost);
    d.format->addItems({tr("HEX"), tr("Text")});
    d.encoding = new QComboBox(testHost);
    d.encoding->addItems({"UTF-8", "Latin-1"});
    testOptions->addWidget(d.port);
    testOptions->addWidget(d.format);
    testOptions->addWidget(d.encoding);
    testOptions->addStretch();
    tests->addLayout(testOptions);
    auto* testTabs = new QTabWidget(testHost);
    d.input        = new QPlainTextEdit(testTabs);
    d.input->setPlaceholderText(tr("Whole packet fixture. Empty input is an empty packet. HEX accepts 00 bytes."));
    d.output = new QPlainTextEdit(testTabs);
    d.output->setReadOnly(true);
    d.diagnostics = new QTextBrowser(testTabs);
    d.diagnostics->setOpenLinks(false);
    connect(d.diagnostics, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() != "line")
        {
            return;
        }
        const QTextBlock block = _data->editor->document()->findBlockByNumber(url.path().toInt() - 1);
        if (!block.isValid())
        {
            return;
        }
        auto cursor = _data->editor->textCursor();
        cursor.setPosition(block.position());
        _data->editor->setTextCursor(cursor);
        _data->editor->centerCursor();
        _data->editor->setFocus();
    });
    testTabs->addTab(d.input, tr("Input"));
    testTabs->addTab(d.output, tr("Output"));
    testTabs->addTab(d.diagnostics, tr("Diagnostics"));
    tests->addWidget(testTabs);
    layout->addWidget(vertical, 1);
    vertical->setSizes({450, 220});
    d.status = new QLabel(tr("Draft — source has not been checked or executed."), this);
    layout->addWidget(d.status);
    auto* close = new QPushButton(tr("Close"), this);
    layout->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(d.editor->document(), &QTextDocument::modificationChanged, this, [this](bool dirty) {
        setWindowTitle(tr("Codeflow editor") + (dirty ? " *" : ""));
    });
    connect(next, &QPushButton::clicked, this, [this, search] {
        if (!_data->editor->find(search->text()))
        {
            auto cursor = _data->editor->textCursor();
            cursor.movePosition(QTextCursor::Start);
            _data->editor->setTextCursor(cursor);
            _data->editor->find(search->text());
        }
    });
    connect(replace, &QPushButton::clicked, this, [this, search, replacement] {
        auto cursor = _data->editor->textCursor();
        if (cursor.selectedText() == search->text())
        {
            cursor.insertText(replacement->text());
        }
    });
    connect(replaceAll, &QPushButton::clicked, this, [this, search, replacement] {
        if (search->text().isEmpty())
        {
            return;
        }
        auto cursor = _data->editor->textCursor();
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::Start);
        _data->editor->setTextCursor(cursor);
        while (_data->editor->find(search->text()))
        {
            auto match = _data->editor->textCursor();
            match.insertText(replacement->text());
            _data->editor->setTextCursor(match);
        }
        cursor.endEditBlock();
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, [search] {
        search->setFocus();
        search->selectAll();
    });
    d.editor->setObjectName("codeflowSource");
    d.name->setObjectName("codeflowName");
    d.file->setObjectName("codeflowFile");
    d.python->setObjectName("codeflowPython");
    d.className->setObjectName("codeflowClass");
    d.status->setObjectName("codeflowStatus");
    d.diagnostics->setObjectName("codeflowDiagnostics");
    d.input->setObjectName("codeflowInput");
    d.output->setObjectName("codeflowOutput");
    d.ports->setObjectName("codeflowPorts");
    if (!d.file->text().isEmpty() && QDir::isAbsolutePath(d.file->text()))
    {
        QFile source(d.file->text());
        if (source.open(QIODevice::ReadOnly) && source.size() <= maxSource)
        {
            const QByteArray bytes = source.readAll();
            d.loadedPath           = QFileInfo(source).absoluteFilePath();
            d.loadedDigest         = digest(bytes);
            const bool changed     = d.originalSource.value("code").toString().toUtf8() != bytes;
            d.editor->setPlainText(QString::fromUtf8(bytes));
            if (changed)
            {
                d.status->setText(tr("Disk source differs from the approved revision; Check before applying."));
            }
        }
        else if (QDir::isAbsolutePath(d.file->text()))
        {
            d.status->setText(tr("Source file unavailable; cached approved source retained."));
            d.message(tr("Source file is unavailable: %1").arg(source.errorString()));
        }
    }
    d.cleanMetadata = d.metadata();
}
CodeflowEditorDialog::~CodeflowEditorDialog()
{
    if (_data->cancelled)
    {
        _data->cancelled->store(true);
    }
    if (_data->environment)
    {
        _data->environment->kill();
    }
    releaseEditors();
}
void CodeflowEditorDialog::setDefinitions(QVariantMap definitions)
{
    _data->testDefinitions = std::move(definitions);
}
void CodeflowEditorDialog::setDefaultDirectory(QString directory)
{
    const bool wasClean = !_data->isDirty();
    _data->directory    = std::move(directory);
    if (_data->file->text().isEmpty())
    {
        _data->file->setText(QDir(_data->directory).filePath(_data->id + "/main.py"));
    }
    if (wasClean)
    {
        _data->cleanMetadata = _data->metadata();
    }
}
void CodeflowEditorDialog::setWorkspaceDirectory(QString directory)
{
    const bool wasClean       = !_data->isDirty();
    _data->workspaceDirectory = std::move(directory);
    if (!_data->file->text().isEmpty() && QDir::isRelativePath(_data->file->text())
        && !_data->editor->document()->isModified())
    {
        QFile source(QDir(_data->workspaceDirectory).filePath(_data->file->text()));
        if (source.open(QIODevice::ReadOnly) && source.size() <= maxSource)
        {
            const QByteArray bytes = source.readAll();
            _data->editor->setPlainText(QString::fromUtf8(bytes));
            _data->loadedPath   = QFileInfo(source).absoluteFilePath();
            _data->loadedDigest = digest(bytes);
            if (_data->originalSource.value("code").toString().toUtf8() != bytes)
            {
                _data->status->setText(tr("Disk source differs from the approved revision; Check before applying."));
            }
        }
        else
        {
            _data->message(tr("Source file is unavailable: %1").arg(source.errorString()));
        }
    }
    if (wasClean)
    {
        _data->cleanMetadata = _data->metadata();
    }
}
QVariantMap CodeflowEditorDialog::definition() const
{
    return _data->baseline;
}
void CodeflowEditorDialog::releaseEditors()
{
    _data->released = true;
    if (_data->cancelled)
    {
        _data->cancelled->store(true);
    }
    QElapsedTimer shutdown;
    shutdown.start();
    for (const auto& job : _data->jobs)
    {
        if (job && !job->wait(qMax(0, 2000 - int(shutdown.elapsed()))))
        {
            qCritical(
                "Codeflow worker exceeded the 2-second shutdown grace; retaining runtime ownership until it quiesces");
            // Destruction/plugin unload cannot abandon a running Core invocation.
            // Ordinary window close is bounded and vetoes closure instead.
            job->wait();
        }
    }
    _data->jobs.clear();
    delete _data->formWidget;
    _data->formWidget = nullptr;
    delete _data->form;
    _data->form = nullptr;
    _data->schema.reset();
    _data->settings.reset();
    _data->released = true;
}
void CodeflowEditorDialog::reject()
{
    close();
}
void CodeflowEditorDialog::closeEvent(QCloseEvent* event)
{
    if (_data->isDirty()
        && QMessageBox::question(this,
                                 tr("Close Codeflow editor"),
                                 tr("Discard unapplied Codeflow changes?"),
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No)
               != QMessageBox::Yes)
    {
        event->ignore();
        return;
    }
    if (_data->cancelled)
    {
        _data->cancelled->store(true);
    }
    QElapsedTimer shutdown;
    shutdown.start();
    for (const auto& job : _data->jobs)
    {
        if (job && !job->wait(qMax(0, 2000 - int(shutdown.elapsed()))))
        {
            _data->message(tr("Codeflow shutdown is still pending. Close again after cancellation finishes."));
            event->ignore();
            return;
        }
    }
    if (_data->environment)
    {
        _data->environment->kill();
        if (!_data->environment->waitForFinished(2000))
        {
            _data->message(tr("Environment action is still stopping. Close again after it exits."));
            event->ignore();
            return;
        }
    }
    event->accept();
    QDialog::done(QDialog::Rejected);
}
void CodeflowEditorDialog::_check(bool applyAfter)
{
    auto& d = *_data;
    if (d.busy || d.released)
    {
        return;
    }
    if (d.editor->toPlainText().toUtf8().size() > maxSource)
    {
        d.message(tr("Source exceeds 2 MiB."));
        return;
    }
    d.busy      = true;
    d.cancelled = std::make_shared<std::atomic_bool>(false);
    d.status->setText(tr("Checking in an isolated Python process…"));
    const QVariantMap source    = d.source();
    const QByteArray  revision  = d.revision();
    auto              result    = std::make_shared<QVariantMap>();
    auto              error     = std::make_shared<QString>();
    const auto        cancelled = d.cancelled;
    auto*             thread    = QThread::create(
        [source, result, error, cancelled] { *result = Codeflow::inspect(source, error.get(), cancelled); });
    d.jobs.append(thread);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, result, error, revision, applyAfter, cancelled] {
        auto& state = *_data;
        state.busy  = false;
        if (state.released || cancelled->load())
        {
            return;
        }
        if (!error->isEmpty())
        {
            state.message(*error);
            state.status->setText(tr("Check failed; draft retained."));
            return;
        }
        if (revision != state.revision())
        {
            state.status->setText(tr("Source changed during Check; check the current draft again."));
            return;
        }
        try
        {
            _preview(*result);
        }
        catch (const std::exception& failure)
        {
            state.message(QString::fromUtf8(failure.what()));
            return;
        }
        state.description   = *result;
        state.checkedDigest = revision;
        state.message(tr("Descriptor check succeeded. Module top-level code ran in the selected interpreter."));
        state.status->setText(
            tr("Checked draft revision %1 — not published").arg(QString::fromLatin1(revision.toHex().left(12))));
        if (applyAfter)
        {
            _apply();
        }
    });
    thread->start();
}
void CodeflowEditorDialog::_preview(const QVariantMap& description)
{
    auto&       d        = *_data;
    const bool  hadForm  = d.form != nullptr;
    QVariantMap previous = hadForm ? d.parameters() : d.baseline.value("defaults").toMap();
    if (hadForm)
    {
        for (const auto& field : d.form->schema().fields())
        {
            if (d.form->contains(field.key))
            {
                previous.insert(field.key, d.form->value(field.key));
            }
        }
    }
    const QVariantMap envelope {
        {    "version",                           1},
        {       "type", "termaster.settings-schema"},
        {"description",       "Codeflow properties"},
        {    "content", description.value("schema")}
    };
    auto schema = std::make_shared<SettingsSchema>(SettingsSchema::serializeValue(envelope));
    delete d.formWidget;
    d.formWidget = nullptr;
    delete d.form;
    d.form = nullptr;
    while (auto* item = d.properties->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    d.settings         = std::make_unique<Settings>(QString {});
    QVariantMap values = description.value("defaults").toMap();
    for (auto it = previous.begin(); it != previous.end(); ++it)
    {
        values.insert(it.key(), it.value());
    }
    d.settings->setValue("configuration", values);
    d.schema     = std::move(schema);
    d.form       = new SettingsFormModel(d.schema, d.settings->node("configuration"), this);
    d.formWidget = new SettingsFormWidget(*d.form, d.widgets, this);
    d.properties->addWidget(d.formWidget);
    if (!d.parametersInitialized)
    {
        d.cleanParameters       = d.parameters();
        d.parametersInitialized = true;
    }
    d.ports->clear();
    d.port->clear();
    int index = 0;
    for (const auto& direction : {QString("inputs"), QString("outputs")})
    {
        for (const auto& entry : description.value(direction).toList())
        {
            const auto    port  = entry.toMap();
            const QString label = QString("%1 · %2 · %3 (%4)")
                                      .arg(port.value("title").toString(),
                                           port.value("id").toString(),
                                           port.value("flow").toString(),
                                           port.value("type").toString());
            d.ports->appendPlainText(direction + ": " + label);
            if (direction == "inputs")
            {
                d.port->addItem(label, index++);
            }
        }
    }
}
bool CodeflowEditorDialog::_save()
{
    auto& d = *_data;
    if (d.file->text().isEmpty())
    {
        const QString file = QFileDialog::getSaveFileName(this,
                                                          tr("Save Python source"),
                                                          d.directory,
                                                          tr("Python (*.py)"));
        if (file.isEmpty())
        {
            return false;
        }
        d.file->setText(file);
    }
    const QString    path  = d.resolvedPath();
    const QByteArray bytes = d.editor->toPlainText().toUtf8();
    if (bytes.size() > maxSource)
    {
        d.message(tr("Source exceeds 2 MiB."));
        return false;
    }
    QFile existing(path);
    if (!existing.exists() && path == d.loadedPath && !d.loadedDigest.isEmpty())
    {
        d.message(tr("Source file was removed externally. Select a new path to save the draft."));
        return false;
    }
    if (existing.exists())
    {
        if (!existing.open(QIODevice::ReadOnly) || existing.size() > maxSource)
        {
            d.message(tr("Cannot verify existing source file."));
            return false;
        }
        const QByteArray current = digest(existing.readAll());
        if ((path != d.loadedPath || current != d.loadedDigest) && current != digest(bytes))
        {
            d.message(tr(
                "Source file changed externally or was not loaded by this editor. Open it before replacing it, or select a new path."));
            return false;
        }
    }
    // Windows cannot atomically replace a file while this verification handle is open.
    existing.close();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        d.message(tr("Cannot create source directory."));
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    {
        d.message(tr("Source save failed: %1").arg(output.errorString()));
        return false;
    }
    d.loadedPath   = path;
    d.loadedDigest = digest(bytes);
    d.editor->document()->setModified(false);
    d.status->setText(tr("Source saved. Apply publishes a checked revision."));
    return true;
}
void CodeflowEditorDialog::_apply()
{
    auto& d = *_data;
    if (d.revision() != d.checkedDigest || !d.form || !d.form->isValid())
    {
        d.message(tr("Check the current source and correct property errors before applying."));
        return;
    }
    if (!_save())
    {
        return;
    }
    auto       candidate = d.baseline;
    const auto generated = Codeflow::definition(d.id, d.name->text().trimmed(), d.source(), d.description);
    for (auto it = generated.begin(); it != generated.end(); ++it)
    {
        candidate.insert(it.key(), it.value());
    }
    candidate.insert("defaults", d.form->schema().activeValues(d.parameters()));
    QString error;
    if (!d.apply || !d.apply(d.baseline, candidate, &error))
    {
        d.message(
            tr("Source saved, but publication failed: %1. The catalog revision remains unchanged; retry Apply after resolving the cause.")
                .arg(error));
        d.status->setText(tr("Partial save — source saved, catalog publication failed."));
        return;
    }
    d.baseline        = candidate;
    d.cleanMetadata   = d.metadata();
    d.cleanParameters = d.parameters();
    d.status->setText(tr("Published catalog revision %1").arg(QString::fromLatin1(d.checkedDigest.toHex().left(12))));
}
void CodeflowEditorDialog::_test()
{
    auto& d = *_data;
    if (d.busy || d.released)
    {
        return;
    }
    if (d.checkedDigest != d.revision())
    {
        d.message(tr("Check the current source before testing."));
        return;
    }
    if (!d.form || !d.form->isValid())
    {
        d.message(tr("Correct property errors before testing."));
        return;
    }
    const QString text = d.input->toPlainText();
    QByteArray    bytes;
    if (d.format->currentIndex() == 0)
    {
        QString hex = text;
        hex.remove(QRegularExpression("\\s"));
        if (hex.size() % 2 || hex.contains(QRegularExpression("[^0-9a-fA-F]")))
        {
            d.message(tr("HEX input requires complete pairs of hexadecimal digits."));
            return;
        }
        bytes = QByteArray::fromHex(hex.toLatin1());
    }
    else
    {
        bytes = d.encoding->currentIndex() == 0 ? text.toUtf8() : text.toLatin1();
    }
    if (bytes.size() > maxSource)
    {
        d.message(tr("Test packet exceeds 2 MiB."));
        return;
    }
    d.busy               = true;
    d.cancelled          = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = d.cancelled;
    auto       source    = d.source();
    source.insert("testDefinitions", d.testDefinitions);
    const auto values = d.form->values();
    const int  port   = d.port->currentData().toInt();
    auto       result = std::make_shared<QVariantMap>();
    auto       error  = std::make_shared<QString>();
    d.status->setText(tr("Testing a separate instance…"));
    auto* thread = QThread::create([source, values, port, bytes, result, error, cancelled] {
        *result = Codeflow::test(source, values, port, bytes, error.get(), cancelled);
    });
    d.jobs.append(thread);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, result, error, cancelled] {
        auto& state = *_data;
        state.busy  = false;
        if (state.released || cancelled->load())
        {
            return;
        }
        state.output->clear();
        if (!error->isEmpty())
        {
            state.message(*error);
            state.status->setText(tr("Test failed; catalog revision unchanged."));
            return;
        }
        qsizetype total = 0;
        for (const auto& entry : result->value("outputs").toList())
        {
            const auto       output  = entry.toMap();
            const QByteArray bytes   = output.value("data").toByteArray();
            const QString    line    = tr("Port %1 · %2 bytes\nHEX: %3\nText: %4\n")
                                           .arg(output.value("port").toInt())
                                           .arg(bytes.size())
                                           .arg(QString::fromLatin1(bytes.toHex(' ')), QString::fromUtf8(bytes));
            total                   += line.size();
            if (total > 65536)
            {
                state.output->appendPlainText(tr("Output preview truncated."));
                break;
            }
            state.output->appendPlainText(line);
        }
        state.message(result->value("diagnostics").toString());
        state.status->setText(tr("Isolated test finished; no terminal transport was accessed."));
    });
    thread->start();
}
void CodeflowEditorDialog::_environment(bool install)
{
    auto& d = *_data;
    if (d.environment)
    {
        d.message(tr("An environment action is already running."));
        return;
    }
    QStringList arguments;
    QString     target;
    if (install)
    {
        const QStringList files = QFileDialog::getOpenFileNames(this,
                                                                tr("Install local Python distributions"),
                                                                d.directory,
                                                                tr("Python packages (*.whl *.zip *.gz)"));
        if (files.isEmpty())
        {
            return;
        }
        arguments  = {"-m", "pip", "install", "--no-index", "--no-deps", "--"};
        arguments += files;
    }
    else
    {
        target = QFileDialog::getExistingDirectory(this, tr("Virtual environment directory"), d.directory);
        if (target.isEmpty())
        {
            return;
        }
        arguments = {"-m", "venv", target};
    }
    auto* process = new QProcess(this);
    d.environment = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        const QString text = _data->diagnostics->toPlainText() + QString::fromUtf8(process->readAllStandardOutput());
        _data->message(text.right(65536));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        _data->message(process->errorString());
        if (error == QProcess::FailedToStart)
        {
            _data->environment = nullptr;
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this, [this, process, target](int code, QProcess::ExitStatus) {
        _data->status->setText(tr("Environment action exited with code %1").arg(code));
        if (code == 0 && !target.isEmpty())
        {
#ifdef Q_OS_WIN
            _data->python->setText(QDir(target).filePath("Scripts/python.exe"));
#else
            _data->python->setText(QDir(target).filePath("bin/python"));
#endif
        }
        _data->environment = nullptr;
        process->deleteLater();
    });
    d.message(tr("Explicit environment action using %1").arg(d.python->text()));
    process->start(d.python->text(), arguments);
}
