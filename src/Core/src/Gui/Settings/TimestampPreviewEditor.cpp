#include "TimestampPreviewEditor.h"

#include "../../Processing/Nodes/Timestamp.h"

#include <QFontDatabase>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QShowEvent>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Terminal/TerminalDisplay.h>

TimestampPreviewEditor::TimestampPreviewEditor(QWidget* parent) : SettingsFieldEditor(parent)
{
    setObjectName("timestampPreview");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* box      = new QGroupBox(tr("Timestamp preview"), this);
    auto* contents = new QVBoxLayout(box);
    _sample        = new QLineEdit(tr("Packet received: Hello, world!"), box);
    _sample->setObjectName("timestampSamplePacket");
    _sample->setAccessibleName(tr("Sample packet"));
    _sample->setMaxLength(256);
    _sample->setToolTip(tr("Sample packet for preview only. Not saved or sent."));
    contents->addWidget(_sample);

    _display = new TerminalDisplay(box);
    _display->setObjectName("timestampPreviewDisplay");
    _display->setAccessibleName(tr("Live Timestamp output preview"));
    _display->setFocusPolicy(Qt::NoFocus);
    _display->setAttribute(Qt::WA_TransparentForMouseEvents);
    _display->viewport()->setAttribute(Qt::WA_TransparentForMouseEvents);
    _display->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _display->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _display->setFixedHeight(100);
    _display->setAppearance({
        {     "fontFamily", QFontDatabase::systemFont(QFontDatabase::FixedFont).family()},
        {       "fontSize",                                                           10},
        {     "foreground",                                                    "#e5e7eb"},
        {     "background",                                                    "#15191f"},
        {"scrollbackLines",                                                            0}
    });
    contents->addWidget(_display);
    _error = new QLabel(box);
    _error->setObjectName("timestampPreviewError");
    _error->setTextFormat(Qt::PlainText);
    _error->setWordWrap(true);
    _error->setStyleSheet("color: #c43c35;");
    _error->hide();
    contents->addWidget(_error);
    layout->addWidget(box);

    _timer.setObjectName("timestampPreviewTimer");
    _timer.setParent(this);
    _timer.setSingleShot(true);
    _timer.setTimerType(Qt::PreciseTimer);
    connect(&_timer, &QTimer::timeout, this, [this] {
        _render();
        _schedule();
    });
    connect(_sample, &QLineEdit::textChanged, this, [this] {
        _render();
        _schedule();
    });
    connect(_display, &TerminalDisplay::terminalSizeChanged, this, [this] {
        QTimer::singleShot(0, this, [this] { _render(); });
    });
}

QVariant TimestampPreviewEditor::value() const
{
    return {};
}

void TimestampPreviewEditor::setValue(const QVariant&)
{
}

void TimestampPreviewEditor::setContext(const QVariantMap& context)
{
    _timer.stop();
    _configuration = context;
    _render();
    _schedule();
}

void TimestampPreviewEditor::showEvent(QShowEvent* event)
{
    SettingsFieldEditor::showEvent(event);
    _render();
    _schedule();
}

void TimestampPreviewEditor::hideEvent(QHideEvent* event)
{
    _timer.stop();
    SettingsFieldEditor::hideEvent(event);
}

void TimestampPreviewEditor::_render()
{
    _display->clear();
    Processor::Limits limits;
    limits.maxBytes = 8192;
    QString error;
    auto    processor = Timestamp::create(_configuration, limits, &error);
    _error->setText(error);
    _error->setVisible(!error.isEmpty());
    if (!processor)
    {
        return;
    }
    processor->accept({_sample->text().toUtf8()},
                      {[this](Processor::Chunk chunk) {
                           _display->receive(chunk.bytes);
                           _display->receive("\x1b[?25l");
                       },
                       [this](QString error) {
                           _error->setText(error);
                           _error->setVisible(!error.isEmpty());
                       }});
}

void TimestampPreviewEditor::_schedule()
{
    if (isVisible() && _error->text().isEmpty())
    {
        _timer.start(QRandomGenerator::global()->bounded(100, 1001));
    }
}
