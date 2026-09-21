#pragma once
#include <QFileDialog>
class QLineEdit;
class QLabel;
namespace host {
// Shared, translatable file picker with an explicit pasteable address bar.
class PathFileDialog final : public QFileDialog {
public:
    PathFileDialog(QWidget*,const QString &title,const QString &directory,const QString &filter);
    static QString getOpenFileName(QWidget*,const QString &title,const QString &directory,const QString &filter);
    bool navigateToPath();
    void accept() override;
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    QLineEdit *m_path;
    QLabel *m_error;
    bool m_pathEdited=false;
};
}
