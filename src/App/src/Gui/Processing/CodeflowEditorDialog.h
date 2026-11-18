#pragma once
#include <QDialog>
#include <QVariantMap>
#include <functional>
#include <memory>
class SettingsWidgetRegistry;
/** Explicit source draft editor. Registry factories outlive this GUI-thread dialog. */
class CodeflowEditorDialog final : public QDialog
{
    Q_OBJECT
public:
    using Apply = std::function<bool(const QVariantMap&, const QVariantMap&, QString*)>;
    CodeflowEditorDialog(QVariantMap             definition,
                         SettingsWidgetRegistry& widgets,
                         Apply                   apply,
                         QWidget*                parent = nullptr);
    ~CodeflowEditorDialog() override;
    void          reject() override;
    void          setDefaultDirectory(QString directory);
    void          setDefinitions(QVariantMap definitions);
    void          setWorkspaceDirectory(QString directory);
    QVariantMap   definition() const;
    void          releaseEditors();
    Q_SIGNAL void helpRequested();
    Q_SIGNAL void restartRequested(const QString& id);
    Q_SIGNAL void stopRequested(const QString& id);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Data;
    std::unique_ptr<Data> _data;
    void                  _check(bool applyAfter = false);
    void                  _test();
    void                  _preview(const QVariantMap& description);
    bool                  _save();
    void                  _apply();
    void                  _environment(bool install);
};
