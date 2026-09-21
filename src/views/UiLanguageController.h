#pragma once
#include <QObject>
class QWidget;
namespace host {
// Retranslates presentation properties without rebuilding pages or touching
// editable values, model contents, selections, task state or signal payloads.
class UiLanguageController final : public QObject {
public:
    static UiLanguageController &instance();
    void refresh();
    void translateWidget(QWidget *widget);
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    UiLanguageController();
    void property(QObject *,const char *name);
    QString value(QObject *,const QString &key,const QString &current);
    bool m_updating=false;
};
}
