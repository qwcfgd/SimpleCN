// Opt-in Windows desktop check. Never registered with CTest or deployed with the app.
#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QDebug>
#include <QTextStream>
#include <atomic>
#include <thread>
#include <chrono>
#include <windows.h>
#include "views/UiLanguageController.h"
#include "localization/Language.h"

struct DialogState { HWND window=nullptr; HWND edit=nullptr; HWND accept=nullptr; HWND cancel=nullptr; bool modern=false; };
static BOOL CALLBACK inspectChild(HWND window,LPARAM data){
    auto &state=*reinterpret_cast<DialogState*>(data);wchar_t name[128]={};GetClassNameW(window,name,128);
    if(wcscmp(name,L"DirectUIHWND")==0)state.modern=true;
    if(wcscmp(name,L"Edit")==0&&IsWindowVisible(window)&&(GetDlgCtrlID(GetParent(window))==1148||GetDlgCtrlID(window)==1001))state.edit=window;
    if(wcscmp(name,L"Button")==0&&GetDlgCtrlID(window)==IDOK)state.accept=window;
    if(wcscmp(name,L"Button")==0&&GetDlgCtrlID(window)==IDCANCEL)state.cancel=window;
    return TRUE;
}
static BOOL CALLBACK inspectWindow(HWND window,LPARAM data){
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t name[128]={};GetClassNameW(window,name,128);
    if(pid!=GetCurrentProcessId()||!IsWindowVisible(window)||wcscmp(name,L"#32770")!=0)return TRUE;
    auto &state=*reinterpret_cast<DialogState*>(data);state.window=window;EnumChildWindows(window,inspectChild,data);return FALSE;
}
static bool capture(HWND window,const QString &path){
    RECT rect;GetWindowRect(window,&rect);const int width=rect.right-rect.left,height=rect.bottom-rect.top;
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);
    auto previous=SelectObject(memory,bitmap);PrintWindow(window,memory,2);
    SelectObject(memory,previous);QImage image(width,height,QImage::Format_RGB32);
    BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    const bool ok=GetDIBits(memory,bitmap,0,height,image.bits(),&info,DIB_RGB_COLORS)&&image.save(path);
    DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return ok;
}
int main(int argc,char **argv){
    QTextStream out(stdout);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    out.setCodec("UTF-8");
#endif
    QApplication app(argc,argv);QApplication::setStyle("Fusion");host::UiLanguageController::instance();
    if(app.arguments().size()!=3||QApplication::platformName()!="windows"){
        qCritical()<<"Run on the Windows desktop with an artifact directory argument.";return 2;
    }
    const QString output=QDir(app.arguments().at(1)).absolutePath();QDir().mkpath(output);
    if(QApplication::testAttribute(Qt::AA_DontUseNativeDialogs))return 3;
    for(const auto &mode:{app.arguments().at(2)}){
        const bool save=mode.contains("save"),cancel=mode.startsWith("cancel");
        host::Language::instance().setCode(save?"en":"zh_CN");
        const QString path=output+"/路径 space "+mode+".json";
        if(!save){QFile file(path);if(!file.open(QIODevice::WriteOnly))return 4;file.write("{}");}
        std::atomic<bool> done{false};bool modern=false,screenshot=false,entered=false;
        std::thread driver([&]{
            // Native dialogs have their own modal loop; use an independent,
            // PID-scoped driver and watchdog, never a Qt timer to dismiss them.
            for(int attempt=0;attempt<150&&!done;++attempt){
                DialogState state;EnumWindows(inspectWindow,reinterpret_cast<LPARAM>(&state));
                if(state.window&&state.modern&&state.edit&&state.accept&&state.cancel){
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));modern=true;
                    const auto native=QDir::toNativeSeparators(path).toStdWString();DWORD_PTR result=0;
                    entered=SendMessageTimeoutW(state.edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(native.c_str()),SMTO_ABORTIFHUNG,2000,&result)!=0;
                    screenshot=capture(state.window,output+"/"+mode+".png");
                    // The external PowerShell UI Automation driver invokes the button.
                    for(int wait=0;wait<250&&!done;++wait)std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            for(int wait=0;wait<30&&!done;++wait)std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(!done)ExitProcess(5);

        });
        QString filter="JSON (*.json)";
        const auto result=save?QFileDialog::getSaveFileName(nullptr,host::Language::text("导出运行日志")+" [probe:"+mode+"]",output,filter+";;Text (*.txt)",&filter)
                              :QFileDialog::getOpenFileName(nullptr,host::Language::text("载入已保存通道")+" [probe:"+mode+"]",output,filter);
        done=true;driver.join();
        out<<mode<<" modern="<<modern<<" path entered="<<entered<<" screenshot="<<screenshot<<" result="<<result<<" filter="<<filter<<Qt::endl;
        if(!modern||!entered||!screenshot||(cancel?!result.isEmpty():QDir::cleanPath(result)!=QDir::cleanPath(path))||filter!="JSON (*.json)")return 6;
    }
    return 0;
}
