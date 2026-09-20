#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include "viewmodels/ChannelViewModel.h"
class QComboBox;class QLineEdit;class QCheckBox;class QLabel;
class QPushButton;class QProgressBar;class QTableView;class QPlainTextEdit;
class QSplitter;class QResizeEvent;class QTabWidget;
namespace host {
class ChannelPage : public QWidget {
    Q_OBJECT
public:
    explicit ChannelPage(ChannelViewModel *,QWidget *parent=nullptr);
    ChannelViewModel *viewModel()const{return m_vm;}
protected:
    void resizeEvent(QResizeEvent *)override;
private:
    void build();
    void updateRegionSizes();
    void loadSettings();
    void applyForm();
    void editProtocol();
    void editDownload();
    void render();
    void browseImage(bool flash);
    void exportFrames();
    void exportLogs();
    ChannelViewModel *m_vm;
    bool m_loading=false,m_applying=false;
    QLineEdit *m_flashPath,*m_appPath,*m_flashAddress,*m_appAddress;
    QCheckBox *m_follow,*m_rxdEnabled;
    QLabel *m_hardwareSummary,*m_flashInfo,*m_appInfo,*m_hint,*m_task,*m_count,*m_elapsedText,*m_error;
    QPushButton *m_start,*m_cancel,*m_scan,*m_protocol,*m_downloadSettings,*m_browseFlash;
    QProgressBar *m_progress;
    QTableView *m_table;
    QPlainTextEdit *m_log;
    QWidget *m_images;
    QSplitter *m_regions=nullptr,*m_outputs=nullptr;
    QTabWidget *m_tasks=nullptr;
    double m_downloadRatio=0.30,m_udsRatio=0.62,m_signalRatio=0.68;
    QElapsedTimer m_elapsed;
    TaskState m_lastTask=TaskState::Idle;
};
}
