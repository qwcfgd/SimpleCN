#pragma once
#include <QWidget>
#include "viewmodels/SignalTransmitViewModel.h"
class QDialog;class QLabel;class QLineEdit;class QPushButton;class QComboBox;class QTreeView;class QTableView;
namespace host {
class SignalValueTableModel;class CanTxTableModel;class LinScheduleTableModel;
class SignalTransmitPage : public QWidget {
    Q_OBJECT
public:explicit SignalTransmitPage(SignalTransmitViewModel*,QWidget*parent=nullptr);
private:
    void rebuild();void render();void selectFrame(const QString&);void renderFrame();
    void editCustom();void editSchedules();void editSignalValue();void communication();void feedback(const QString&);
    SignalTransmitViewModel*m_vm;bool m_rendering=false;QString m_key;
    QLineEdit*m_path,*m_search;
    QDialog*m_configurationDialog;
    QLabel*m_configurationFeedback;
    QLabel*m_summary,*m_validation;
    QPushButton*m_import,*m_reload,*m_communication,*m_schedules,*m_once,*m_start,*m_stop,*m_back,*m_restore,*m_signalEdit;
    QComboBox*m_role,*m_node,*m_schedule,*m_canNode,*m_canDirection;
    QWidget*m_rolePanel,*m_editor,*m_canPanel;
    QTreeView*m_tree;QTableView*m_plan,*m_values;
    SignalValueTableModel*m_valueModel;CanTxTableModel*m_canModel=nullptr;LinScheduleTableModel*m_linModel=nullptr;
};
void openSignalCommunicationDialog(SignalTransmitViewModel*,const QString&key,QWidget*parent);
}
