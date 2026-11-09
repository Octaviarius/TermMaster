#pragma once

#include <QVariantMap>
#include <QWidget>
#include <functional>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

/** GUI-thread command draft and bounded, memory-only accepted-command history. */
class CommandEditor final : public QWidget
{
    Q_OBJECT
public:
    using SendCallback = std::function<bool(const QString&, const QString&, const QString&)>;

    explicit CommandEditor(QWidget* parent = nullptr);
    Q_DISABLE_COPY_MOVE(CommandEditor)

    void setSettings(const QVariantMap& input);
    void setSendCallback(SendCallback callback);
    bool buffered() const;
    void retainPaste(const QString& text);
    void pasteText(const QString& text);
    bool confirmPaste(const QByteArray& bytes);

    Q_SIGNAL void settingChanged(const QString& key, const QVariant& value);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    static constexpr int _maximumCharacters   = 16384;
    static constexpr int _maximumHistoryBytes = 1024 * 1024;
    QComboBox*           _mode;
    QComboBox*           _terminator;
    QLineEdit*           _custom;
    QComboBox*           _pastePolicy;
    QWidget*             _body;
    QPlainTextEdit*      _editor;
    QComboBox*           _history;
    QVariantMap          _settings;
    SendCallback         _send;
    QStringList          _entries;
    QString              _savedDraft;
    int                  _historyIndex = 0;
    int                  _historyBytes = 0;

    bool _submit(const QString& text, bool clearDraft);
    void _paste(const QString& text);
    void _navigateHistory(int direction);
    void _remember(const QString& text);
};
