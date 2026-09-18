#pragma once
#include <QWidget>
#include "viewmodels/ChannelViewModel.h"
class QComboBox;class QCheckBox;class QPushButton;
namespace host {
// Edits a dialog-owned draft ViewModel; accepting the dialog commits it to the real channel.
class ChannelHardwareEditor final : public QWidget {
public:
    explicit ChannelHardwareEditor(ChannelViewModel*,QWidget*parent=nullptr);
private:
    void loadSettings();void applyForm();void refreshHardware();void refreshPorts(bool deviceChanged=false);void render();
    ChannelViewModel*m_vm;
    bool m_loading=false,m_applying=false,m_refreshing=false;
    QComboBox *m_mode,*m_hardware,*m_software,*m_bitrate;
    QCheckBox*m_reconnect;QPushButton*m_refresh;
};
}
