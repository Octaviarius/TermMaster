#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>
#include <QStackedLayout>
#include <QTextLayout>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#include <psapi.h>
#endif

/** Synthetic renderer comparison. This intentionally contains no transport or VT parser. */
class ProbeScene
{
public:
    enum class Method
    {
        FreshLayout = 1,
        CachedLayout,
        GlyphRuns,
        RowImages,
        OpenGL
    };
    struct Measurements
    {
        qint64 preparationNs = 0;
        qint64 drawingNs     = 0;
        int    layoutsBuilt  = 0;
        int    tilesBuilt    = 0;
    };

private:
    struct Span
    {
        int    start;
        int    length;
        QColor foreground;
        QColor background;
        bool   bold;
    };
    struct Glyphs
    {
        QColor           color;
        QList<QGlyphRun> runs;
    };
    struct Row
    {
        QString                      text;
        std::vector<Span>            spans;
        std::unique_ptr<QTextLayout> layout;
        std::vector<Glyphs>          glyphs;
        QImage                       tile;
        QPointF                      tilePhase;
    };

    QFont            _font;
    qreal            _cellWidth;
    int              _rowHeight;
    int              _columns;
    int              _serial = 0;
    std::vector<Row> _rows;
    Measurements     _measurements;

    Row _makeRow(int serial) const
    {
        Row           row;
        const QString seed = QStringLiteral("[%1] Termaster RGB text / cache / 0123456789 ")
                                 .arg(serial, 6, 10, QLatin1Char('0'));
        while (row.text.size() < _columns)
        {
            row.text += seed;
        }
        row.text.truncate(_columns);

        // Include Cyrillic, combining marks and a supplementary character without
        // claiming that this text-layout fixture implements terminal cell widths.
        if (serial % 11 == 3)
        {
            row.text.replace(10, 30, QString::fromUtf8("Unicode: Привет e\u0301 世界 😀"));
        }

        for (int start = 0; start < row.text.size(); start += 20)
        {
            const int index = start / 20;
            row.spans.push_back({start,
                                 std::min(20, int(row.text.size()) - start),
                                 QColor::fromHsv((serial * 7 + index * 47) % 360, 140, 245),
                                 QColor::fromHsv((serial * 13 + index * 31) % 360, 100, 45),
                                 index % 3 == 1});
        }
        return row;
    }

    std::unique_ptr<QTextLayout> _makeLayout(const Row& row)
    {
        auto        layout = std::make_unique<QTextLayout>(row.text, _font);
        QTextOption options;
        options.setWrapMode(QTextOption::NoWrap);
        options.setTextDirection(Qt::LeftToRight);
        layout->setTextOption(options);
        QList<QTextLayout::FormatRange> formats;
        for (const auto& span : row.spans)
        {
            QTextCharFormat format;
            format.setForeground(span.foreground);
            format.setFontWeight(span.bold ? QFont::Bold : QFont::Normal);
            formats.append({span.start, span.length, format});
        }
        layout->setFormats(formats);
        layout->setCacheEnabled(true);
        layout->beginLayout();
        auto line = layout->createLine();
        line.setLineWidth(_columns * _cellWidth + 64);
        line.setPosition(QPointF(0, 0));
        layout->endLayout();
        ++_measurements.layoutsBuilt;
        return layout;
    }

    void _prepareGlyphs(Row& row)
    {
        if (!row.glyphs.empty())
        {
            return;
        }
        if (!row.layout)
        {
            row.layout = _makeLayout(row);
        }
        for (const auto& span : row.spans)
        {
            row.glyphs.push_back({span.foreground, row.layout->glyphRuns(span.start, span.length)});
        }
        // Keep only glyph data in this strategy; do not count an unused layout cache.
        row.layout.reset();
    }

    void _drawBackgrounds(QPainter& painter, const Row& row)
    {
        for (const auto& span : row.spans)
        {
            painter.fillRect(QRectF(span.start * _cellWidth, 0, span.length * _cellWidth, _rowHeight), span.background);
        }
    }

    void _drawGlyphs(QPainter& painter, const Row& row)
    {
        for (const auto& glyphs : row.glyphs)
        {
            painter.setPen(glyphs.color);
            for (const auto& run : glyphs.runs)
            {
                painter.drawGlyphRun(QPointF(), run);
            }
        }
    }

public:
    ProbeScene(int columns, int rows) : _font(QFontDatabase::systemFont(QFontDatabase::FixedFont)), _columns(columns)
    {
        _font.setPointSizeF(11);
        _font.setKerning(false);
        const QFontMetricsF metrics(_font);
        _cellWidth = std::ceil(metrics.horizontalAdvance(QLatin1Char('M')));
        _rowHeight = std::ceil(metrics.height()) + 2;
        reset(rows);
    }

    void reset(int rows)
    {
        _serial = 0;
        _rows.clear();
        for (int i = 0; i < rows; ++i)
        {
            _rows.push_back(_makeRow(_serial++));
        }
    }

    QSize preferredSize() const
    {
        return QSize(std::ceil(_cellWidth * _columns) + 16, _rows.size() * _rowHeight + 16);
    }

    int rowHeight() const
    {
        return _rowHeight;
    }
    int rowCount() const
    {
        return int(_rows.size());
    }
    QString fontDescription() const
    {
        return _font.toString();
    }
    Measurements measurements() const
    {
        return _measurements;
    }

    void clearCaches()
    {
        for (auto& row : _rows)
        {
            row.layout.reset();
            row.glyphs.clear();
            row.tile = QImage();
        }
    }

    void changeRow(int index)
    {
        _rows[index] = _makeRow(_serial++);
    }

    void scroll()
    {
        std::rotate(_rows.begin(), _rows.begin() + 1, _rows.end());
        _rows.back() = _makeRow(_serial++);
    }

    void paint(QPainter& painter, Method method, const QRect& dirty, qreal dpr)
    {
        _measurements = {};
        QElapsedTimer timer;
        timer.start();
        const int first = std::clamp((dirty.top() - 8) / _rowHeight, 0, rowCount() - 1);
        const int last  = std::clamp((dirty.bottom() - 8) / _rowHeight, 0, rowCount() - 1);
        std::vector<std::unique_ptr<QTextLayout>> temporary;
        for (int index = first; index <= last; ++index)
        {
            auto& row = _rows[index];
            if (method == Method::FreshLayout)
            {
                temporary.push_back(_makeLayout(row));
            }
            else if (method == Method::CachedLayout)
            {
                if (!row.layout)
                {
                    row.layout = _makeLayout(row);
                }
            }
            else
            {
                const QPointF physicalOrigin(8 * dpr, (8 + index * _rowHeight) * dpr);
                const QPointF phase(physicalOrigin.x() - std::floor(physicalOrigin.x()),
                                    physicalOrigin.y() - std::floor(physicalOrigin.y()));
                if (method == Method::RowImages && !row.tile.isNull() && row.tilePhase == phase)
                {
                    continue;
                }
                _prepareGlyphs(row);
                if (method == Method::RowImages)
                {
                    row.tilePhase = phase;
                    row.tile      = QImage(QSize(std::ceil(_columns * _cellWidth * dpr + phase.x()),
                                                 std::ceil(_rowHeight * dpr + phase.y())),
                                           QImage::Format_ARGB32_Premultiplied);
                    row.tile.setDevicePixelRatio(dpr);
                    row.tile.fill(Qt::transparent);
                    QPainter tilePainter(&row.tile);
                    tilePainter.setRenderHint(QPainter::TextAntialiasing);
                    tilePainter.translate(phase / dpr);
                    _drawBackgrounds(tilePainter, row);
                    _drawGlyphs(tilePainter, row);
                    ++_measurements.tilesBuilt;
                    row.glyphs.clear();
                }
            }
        }
        _measurements.preparationNs = timer.nsecsElapsed();
        timer.restart();

        painter.fillRect(dirty, QColor("#10141c"));
        painter.setRenderHint(QPainter::TextAntialiasing);
        for (int index = first; index <= last; ++index)
        {
            auto& row = _rows[index];
            painter.save();
            painter.translate(8, 8 + index * _rowHeight);
            if (method == Method::RowImages)
            {
                painter.drawImage(-row.tilePhase / dpr, row.tile);
            }
            else
            {
                _drawBackgrounds(painter, row);
                if (method == Method::FreshLayout)
                {
                    temporary[index - first]->draw(&painter, QPointF());
                }
                else if (method == Method::CachedLayout)
                {
                    row.layout->draw(&painter, QPointF());
                }
                else
                {
                    _drawGlyphs(painter, row);
                }
            }
            painter.restore();
        }
        _measurements.drawingNs = timer.nsecsElapsed();
    }
};

/** The window owns scene state; its two child surfaces only paint it. */
class ProbeWindow : public QWidget
{
private:
    class RasterSurface : public QWidget
    {
    private:
        ProbeWindow& _owner;

    protected:
        void paintEvent(QPaintEvent* event) override
        {
            QElapsedTimer timer;
            timer.start();
            {
                QPainter painter(this);
                _owner._scene.paint(painter, _owner._method, event->rect(), devicePixelRatioF());
            }
            _owner._paintFinished(timer.nsecsElapsed());
        }

    public:
        RasterSurface(ProbeWindow& owner) : QWidget(&owner), _owner(owner)
        {
            setAttribute(Qt::WA_OpaquePaintEvent);
        }
    };

    class GlSurface : public QOpenGLWidget
    {
    private:
        ProbeWindow& _owner;

    protected:
        void initializeGL() override
        {
            auto* functions = context()->functions();
            _owner._gpu     = QString::fromLatin1(reinterpret_cast<const char*>(functions->glGetString(GL_RENDERER)));
        }

        void paintGL() override
        {
            QElapsedTimer timer;
            timer.start();
            {
                QPainter painter(this);
                _owner._scene.paint(painter, ProbeScene::Method::OpenGL, rect(), devicePixelRatioF());
            }
            // Benchmark completion includes GPU execution, not just command submission.
            if (_owner._benchmark)
            {
                context()->functions()->glFinish();
            }
            _owner._paintFinished(timer.nsecsElapsed());
        }

    public:
        GlSurface(ProbeWindow& owner) : QOpenGLWidget(&owner), _owner(owner)
        {
        }
    };

    ProbeScene          _scene;
    ProbeScene::Method  _method = ProbeScene::Method::CachedLayout;
    QStackedLayout*     _stack;
    RasterSurface*      _raster;
    GlSurface*          _gl = nullptr;
    QTimer              _animation;
    bool                _benchmark;
    bool                _waiting = false;
    bool                _paused  = false;
    bool                _partial;
    int                 _frames;
    int                 _frame = -20;
    QString             _workload;
    QString             _output;
    QString             _snapshot;
    QString             _gpu;
    QElapsedTimer       _requestTimer;
    QJsonArray          _samples;
    std::vector<double> _renderMs;

    QWidget* _surface() const
    {
        return _gl ? static_cast<QWidget*>(_gl) : _raster;
    }

    void _nextFrame()
    {
        if (_frame >= _frames)
        {
            _finishBenchmark();
            return;
        }
        _waiting = true;
        _requestTimer.start();
        if (_workload == "cold")
        {
            _scene.clearCaches();
        }
        else if (_workload == "change")
        {
            _scene.changeRow((_frame + 20) % _scene.rowCount());
        }
        else if (_workload == "scroll")
        {
            _scene.scroll();
        }
        if (_partial && _workload == "change")
        {
            _surface()->update(
                QRect(0, 8 + ((_frame + 20) % _scene.rowCount()) * _scene.rowHeight(), width(), _scene.rowHeight()));
        }
        else
        {
            _surface()->update();
        }
    }

    void _paintFinished(qint64 ns)
    {
        if (!_benchmark || !_waiting)
        {
            return;
        }
        _waiting = false;
        if (_frame >= 0)
        {
            const auto data = _scene.measurements();
            _samples.append(QJsonObject {
                {               "render_ms",                           ns / 1e6},
                {              "prepare_ms",           data.preparationNs / 1e6},
                {                 "draw_ms",               data.drawingNs / 1e6},
                {"request_to_completion_ms", _requestTimer.nsecsElapsed() / 1e6},
                {           "layouts_built",                  data.layoutsBuilt},
                {             "tiles_built",                    data.tilesBuilt}
            });
            _renderMs.push_back(ns / 1e6);
        }
        ++_frame;
        QTimer::singleShot(0, this, [this]() { _nextFrame(); });
    }

    void _finishBenchmark()
    {
        std::sort(_renderMs.begin(), _renderMs.end());
        auto percentile = [this](double fraction) {
            return _renderMs[std::min(_renderMs.size() - 1, size_t(fraction * _renderMs.size()))];
        };
        double privateMiB = 0;
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX memory {};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                 sizeof(memory)))
        {
            privateMiB = memory.PrivateUsage / (1024.0 * 1024.0);
        }
#endif
        QJsonObject result {
            {       "method",             int(_method)},
            {     "workload",                _workload},
            {      "partial",                 _partial},
            {           "qt",               qVersion()},
            {         "font", _scene.fontDescription()},
            {          "dpr",      devicePixelRatioF()},
            {        "width",                  width()},
            {       "height",                 height()},
            {   "row_height",       _scene.rowHeight()},
            {          "gpu",                     _gpu},
            {       "frames",                  _frames},
            {"warmup_frames",                       20},
            {"render_p50_ms",         percentile(0.50)},
            {"render_p95_ms",         percentile(0.95)},
            {"render_p99_ms",         percentile(0.99)},
            {  "private_mib",               privateMiB},
            {      "samples",                 _samples}
        };
        if (_gl && !_gl->isValid())
        {
            qCritical("OpenGL surface is invalid");
            QApplication::exit(2);
            return;
        }
        QFile            file(_output);
        const QByteArray encoded = QJsonDocument(result).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() || !file.flush())
        {
            qCritical("Cannot write benchmark output");
            QApplication::exit(2);
            return;
        }
        QApplication::quit();
    }

    void _selectMethod(ProbeScene::Method method)
    {
        _method = method;
        _scene.clearCaches();
        if (method == ProbeScene::Method::OpenGL)
        {
            if (!_gl)
            {
                _gl = new GlSurface(*this);
                _stack->addWidget(_gl);
            }
            _stack->setCurrentWidget(_gl);
        }
        else
        {
            _stack->setCurrentWidget(_raster);
            if (_gl)
            {
                _stack->removeWidget(_gl);
                delete _gl;
                _gl = nullptr;
            }
        }
        static const QStringList names {"Fresh QTextLayout",
                                        "Cached QTextLayout",
                                        "Cached QGlyphRun",
                                        "Row image cache",
                                        "OpenGL + cached QGlyphRun"};
        setWindowTitle(QString("RenderingProbe | %1 | 1-5 renderer, Space pause").arg(names[int(method) - 1]));
        _surface()->update();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_5)
        {
            _selectMethod(ProbeScene::Method(event->key() - Qt::Key_0));
        }
        else if (event->key() == Qt::Key_Space)
        {
            _paused = !_paused;
        }
        else if (event->key() == Qt::Key_Escape)
        {
            close();
        }
    }

    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::DevicePixelRatioChange)
        {
            _scene.clearCaches();
        }
        QWidget::changeEvent(event);
    }

public:
    ProbeWindow(int     columns,
                int     rows,
                int     method,
                int     frames,
                QString workload,
                QString output,
                QString snapshot,
                bool    partial) :
        _scene(columns, rows), _benchmark(!output.isEmpty()), _partial(partial), _frames(frames),
        _workload(std::move(workload)), _output(std::move(output)), _snapshot(std::move(snapshot))
    {
        _stack = new QStackedLayout(this);
        _stack->setContentsMargins(0, 0, 0, 0);
        _raster = new RasterSurface(*this);
        _stack->addWidget(_raster);
        setFocusPolicy(Qt::StrongFocus);
        resize(_scene.preferredSize());
        _selectMethod(ProbeScene::Method(method));

        if (_benchmark)
        {
            QTimer::singleShot(400, this, [this]() { _nextFrame(); });
            QTimer::singleShot(60000, this, []() {
                qCritical("Benchmark timed out");
                QApplication::exit(3);
            });
        }
        else if (!_snapshot.isEmpty())
        {
            // Populate the caches first, then move cached rows to new pixel phases.
            QTimer::singleShot(300, this, [this]() {
                _scene.scroll();
                _surface()->update();
            });
            QTimer::singleShot(600, this, [this]() {
                const QImage image = _gl ? _gl->grabFramebuffer() : _raster->grab().toImage();
                QApplication::exit(image.save(_snapshot) ? 0 : 2);
            });
        }
        else
        {
            connect(&_animation, &QTimer::timeout, this, [this]() {
                if (!_paused)
                {
                    _scene.scroll();
                    _surface()->update();
                }
            });
            _animation.start(100);
        }
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("RenderingProbe");
    QCommandLineParser parser;
    parser.setApplicationDescription("One-window synthetic Qt text renderer comparison");
    parser.addHelpOption();
    parser.addOption({"method", "1 fresh layout, 2 cached layout, 3 glyph runs, 4 row image, 5 OpenGL", "n", "2"});
    parser.addOption({"cols", "Text columns", "n", "140"});
    parser.addOption({"rows", "Visible rows", "n", "42"});
    parser.addOption({"frames", "Measured frames after 20 warmup frames", "n", "120"});
    parser.addOption({"workload", "static, cold, change, scroll", "name", "static"});
    parser.addOption({"output", "Run benchmark and write JSON", "file"});
    parser.addOption({"snapshot", "Warm caches, scroll once, save a PNG and exit", "file"});
    parser.addOption({"partial", "Use a dirty row update for the change workload"});
    parser.process(app);

    auto number = [&parser](const QString& name, int minimum, int maximum) {
        bool      ok    = false;
        const int value = parser.value(name).toInt(&ok);
        if (!ok || value < minimum || value > maximum)
        {
            parser.showHelp(2);
        }
        return value;
    };
    const QString workload = parser.value("workload");
    if (!QStringList {"static", "cold", "change", "scroll"}.contains(workload))
    {
        parser.showHelp(2);
    }
    ProbeWindow window(number("cols", 40, 300),
                       number("rows", 8, 100),
                       number("method", 1, 5),
                       number("frames", 10, 10000),
                       workload,
                       parser.value("output"),
                       parser.value("snapshot"),
                       parser.isSet("partial"));
    window.move(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(12, 12));
    window.show();
    window.raise();
    window.activateWindow();
    return app.exec();
}
