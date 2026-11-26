#pragma once
#include <QWidget>
#include "viewmodels/SignalTransmitViewModel.h"
class QPlainTextEdit;class QSpinBox;class QDialog;class QLabel;class QLineEdit;class QPushButton;class QComboBox;class QTreeView;class QTableView;
namespace host {
class SignalValueTableModel;class CanTxTableModel;class LinScheduleTableModel;
class SignalPlotDialog;
class SignalTransmitPage : public QWidget {
    Q_OBJECT
public:explicit SignalTransmitPage(SignalTransmitViewModel*,QWidget*parent=nullptr);
private:
    void rebuild();void render();void selectFrame(const QString&);void renderFrame();
    void restoreTableLayouts();void saveTableLayouts();bool m_restoringColumns=false;
    void editCustom();void feedback(const QString&);
    SignalTransmitViewModel*m_vm;bool m_rendering=false;QString m_key;
    QLineEdit*m_path,*m_search,*m_replayPath;
    QPushButton*m_replayImport,*m_replayReset,*m_replayMap;
    void editReplayMapping();
    QSpinBox*m_count;
    QDialog*m_configurationDialog;
    SignalPlotDialog *m_plotDialog=nullptr;
    QLabel*m_configurationFeedback;
    bool m_addingFromTree=false;
    QPlainTextEdit *m_databaseDiagnostics;
    QLabel*m_summary,*m_validation;
    QPushButton*m_import,*m_reload,*m_newSchedule,*m_removeSchedule,*m_once,*m_start,*m_back,*m_restore;
    QComboBox*m_role,*m_node,*m_schedule,*m_canNode,*m_canDirection;
    QWidget*m_rolePanel,*m_canPanel;
    QTreeView*m_tree;QTableView*m_plan,*m_values;
    SignalValueTableModel*m_valueModel;CanTxTableModel*m_canModel=nullptr;LinScheduleTableModel*m_linModel=nullptr;
};

}
