#pragma once
#include <QWidget>
#include "viewmodels/ChannelViewModel.h"
class QComboBox;class QLineEdit;class QLabel;class QPushButton;class QTableView;
class QTableWidget;class QPlainTextEdit;class QCheckBox;class QSortFilterProxyModel;
class QSpinBox;
namespace host {
class UdsDiagnosticPage final : public QWidget {
    Q_OBJECT
public:
    explicit UdsDiagnosticPage(ChannelViewModel *,QWidget *parent=nullptr);
private:
    void refreshDatabase();
    void selectService();
    void updateRequest();
    void render();
    int sourceRow()const;
    diag::Values values()const;
    ChannelViewModel *m_vm;
    QLabel *m_validation;
    QPushButton *m_settings,*m_send,*m_copy,*m_revert;
    QLabel *m_repeatStatus;
    QTableView *m_services;
    QTableWidget *m_parameters;
    QPlainTextEdit *m_request,*m_response;
    QCheckBox *m_suppress;
    QSortFilterProxyModel *m_proxy;
    QByteArray m_bytes;
    bool m_updating=false,m_custom=false;
    QString m_error;
};
}
