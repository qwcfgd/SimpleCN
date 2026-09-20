#pragma once
#include <QDialog>
class QTreeWidget;class QCheckBox;class QDoubleSpinBox;class QLabel;class QComboBox;
namespace host {
class SignalTransmitViewModel;class SignalPlotModel;class SignalPlotCanvas;
class SignalPlotDialog:public QDialog{
    Q_OBJECT
public:explicit SignalPlotDialog(SignalTransmitViewModel*,QWidget*parent=nullptr);
    SignalPlotModel*plotModel()const{return m_model;}
private:
    void restoreControls();
    void updateCursors();void addSignals();void removeSignals();void chooseColor();
    void rebuildList();void refreshList();void selectSignals();void persistControls();
    QSet<int> selectedRows()const;
    SignalTransmitViewModel*m_vm;SignalPlotModel*m_model;SignalPlotCanvas*m_canvas;
    QTreeWidget*m_list;QCheckBox*m_cursor,*m_difference,*m_thick,*m_grid,*m_points,*m_follow;
    QComboBox*m_display,*m_axes;QDoubleSpinBox*m_t1,*m_t2;QLabel*m_delta;bool m_rebuilding=false,m_restoring=true;
};
}
