#pragma once
#include <QMenu>
#include <QDialog>
#include "model/FrameFilterProxy.h"
class QTableWidget;class QComboBox;class QLabel;
namespace host {
class FrameColumnMenu : public QMenu {
    Q_OBJECT
public:
    FrameColumnMenu(FrameFilterProxy*,int column,QWidget*parent=nullptr);
    bool applied()const{return m_applied;}
    FrameColumnFilter result()const{return m_result;}
private:bool m_applied=false;FrameColumnFilter m_result;
};
class FrameRuleDialog : public QDialog {
    Q_OBJECT
public:
    FrameRuleDialog(int column,const QString&name,const FrameColumnFilter&,FrameFilterOp initial,quint32 maximum,QWidget*parent=nullptr);
    FrameColumnFilter result()const;
private:
    void addRow(const FrameCondition&);
    void validateAndAccept();
    int m_column;quint32 m_maximum;FrameColumnFilter m_initial;
    QTableWidget*m_table;QComboBox*m_join;QLabel*m_error;
};
}
