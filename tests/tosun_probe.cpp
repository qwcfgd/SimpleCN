#include <cstring>
#include <QCoreApplication>
#include <QTextStream>
#include <QElapsedTimer>
#include <QThread>
#include "infrastructure/TosunHardware.h"
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);QTextStream out(stdout);auto runtime=host::tosun::sharedRuntime();QString error;
    auto ports=runtime->scan(communication::Bus::Can,error);auto lin=runtime->scan(communication::Bus::Lin,error);
    out<<"SDK: "<<runtime->libraryPath()<<Qt::endl;
    if(!error.isEmpty()){out<<error<<Qt::endl;return 1;}
    for(const auto &p:ports+lin)out<<p.key<<" | "<<p.label<<Qt::endl;
    const auto args=app.arguments();
    if(!args.contains("--can-loopback"))return ports.isEmpty()?1:0;
    const int argument=args.indexOf("--can-loopback");if(argument+1>=args.size()){out<<"Specify the expected adapter serial."<<Qt::endl;return 2;}
    if(args.contains("--termination")){
        const auto configure=runtime->api.configureCan;runtime->api.configureCan=[configure](quintptr h,int ch,double rate,quint32){return configure(h,ch,rate,1);};
        const auto controller=runtime->api.configureCanController;
        if(controller)runtime->api.configureCanController=[controller](quintptr h,int ch,double arb,double data,int type,int mode,quint32){return controller(h,ch,arb,data,type,mode,1);};
        out<<"CAN1/CAN2 internal 120 ohm enabled for loopback"<<Qt::endl;
    }
    const auto serial=args[argument+1];communication::HardwareChannel a,b;
    for(const auto&p:ports)if(p.key.section(':',1,1)==serial){if(p.controller==1)a=p;if(p.controller==2)b=p;}
    if(a.key.isEmpty()||b.key.isEmpty()){out<<"CAN1/CAN2 not found on requested serial"<<Qt::endl;return 2;}
    auto one=host::createTosunCan(runtime),two=host::createTosunCan(runtime);communication::SoftwareChannelConfiguration config;config.bitrate=500000;
    if(!one->open(a,config,error)||!two->open(b,config,error)){out<<error<<Qt::endl;return 3;}
    TPCANMsg received[64]={};TPCANTimestamp stamps[64]={};one->recvRaw(64,received,stamps);two->recvRaw(64,received,stamps);
    int delivered=0;
    for(int n=0;n<8;++n){
        const bool reverse=n>=4;auto *sender=reverse?two.get():one.get();auto *receiver=reverse?one.get():two.get();
        TPCANMsg message={};message.ID=n%2?0x1ABCDE:0x6A5;message.MSGTYPE=n%2?PCAN_MESSAGE_EXTENDED:PCAN_MESSAGE_STANDARD;message.LEN=8;
        const QByteArray bytes=QByteArray::fromHex("51A7C0932255AA00");std::memcpy(message.DATA,bytes.constData(),8);message.DATA[7]=BYTE(n);
        if(!sender->sendRaw(message)){out<<sender->lastErrorText()<<Qt::endl;break;}
        bool matched=false,busError=false;QElapsedTimer clock;clock.start();
        while(clock.elapsed()<1000&&!matched&&!busError){const auto count=receiver->recvRaw(64,received,stamps);
            for(DWORD k=0;k<count;++k)if(!(received[k].MSGTYPE&PCAN_MESSAGE_ECHO)&&received[k].ID==message.ID&&received[k].LEN==8&&std::memcmp(received[k].DATA,message.DATA,8)==0)matched=true;
            const auto sent=sender->recvRaw(64,received,stamps);
            for(DWORD k=0;k<sent;++k)if(received[k].MSGTYPE&PCAN_MESSAGE_ERRFRAME){out<<"Controller error payload: "<<QByteArray(reinterpret_cast<const char*>(received[k].DATA),qMin(int(received[k].LEN),8)).toHex(' ')<<Qt::endl;busError=true;break;}
            if(!matched&&!busError)QThread::msleep(5);}
        if(!matched){out<<"Receive status: "<<receiver->lastErrorCode()<<" "<<receiver->lastErrorText()<<Qt::endl;out<<"No matching RX for frame "<<n<<"; stopping"<<Qt::endl;break;}
        ++delivered;out<<(reverse?"CAN2 -> CAN1":"CAN1 -> CAN2")<<" frame "<<n<<" OK"<<Qt::endl;QThread::msleep(20);
    }
    one->close(error);const bool peerAlive=two->health(error)==communication::Health::Ready;two->close(error);
    out<<"Matched "<<delivered<<"/8; sibling survived close: "<<peerAlive<<Qt::endl;return delivered==8&&peerAlive?0:4;
}
