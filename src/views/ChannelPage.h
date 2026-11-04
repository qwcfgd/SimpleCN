#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include "viewmodels/ChannelViewModel.h"
#include "model/FrameFilterProxy.h"
class QComboBox;class QLineEdit;class QCheckBox;class QLabel;
class QPushButton;class QProgressBar;class QTableView;class QPlainTextEdit;
class QTreeView;class QSplitter;class QResizeEvent;class QShowEvent;class QTabWidget;class QTableWidget;
namespace host {
class ChannelPage : public QWidget {
    Q_OBJECT
public:
    explicit ChannelPage(ChannelViewModel *,QWidget *parent=nullptr);
    ChannelViewModel *viewModel()const{return m_vm;}
protected:
    void resizeEvent(QResizeEvent *)override;
    void showEvent(QShowEvent *)override;
private:
    void build();
    void updateRegionSizes();
    void loadSettings();
    void applyForm();
    void editDownload();
    void render();
    void browseImage(bool flash);
    void addDownloadFiles();
    void removeDownloadFiles();
    void saveDownloadFiles();
    void rememberDownloadFiles();
    void restoreDownloadFiles();
    void exportFrames();
    void exportFrameBatch(const FrameBatch &,const QString &title);
    void configureColumnFilter(int,QPoint);
    void saveFrameFilter();
    void followLatestFrame();
    void exportLogs();
    ChannelViewModel *m_vm;
    bool m_loading=false,m_applying=false;
    QLineEdit *m_flashPath,*m_appPath,*m_flashAddress,*m_appAddress;
    QCheckBox *m_follow,*m_rxdEnabled;
    QLabel *m_hardwareSummary,*m_connectionStatus,*m_flashInfo,*m_appInfo,*m_hint,*m_task,*m_count,*m_elapsedText,*m_error;
    QPushButton *m_start,*m_cancel,*m_scan,*m_downloadSettings,*m_browseFlash,*m_connectButton=nullptr;
    QProgressBar *m_progress;
    QTreeView *m_table;
    FrameFilterProxy *m_frameProxy=nullptr;
    bool m_restoringMonitor=false;
    QPlainTextEdit *m_log;
    QWidget *m_images;
    QTableWidget *m_imageTable=nullptr;
    QJsonArray m_entryFiles;
    bool m_hasEntryFiles=false;
    QSplitter *m_regions=nullptr,*m_outputs=nullptr;
    QTabWidget *m_tasks=nullptr;
    double m_downloadRatio=0.30,m_udsRatio=0.62,m_signalRatio=0.68;
    QElapsedTimer m_elapsed;
    TaskState m_lastTask=TaskState::Idle;
};
}
