#include "HostTypes.h"
#include <QRegularExpression>
#include <QHash>
#include "protocol/FlashJob.h"
#include "protocol/CanTransport.h"
namespace host {
bool effectiveDownloadFiles(const QJsonArray &files,QJsonArray &result,QString *error){
    result={};if(error)error->clear();QHash<QString,QJsonObject> unique;
    auto fail=[&](const QString &message){if(error)*error=message;return false;};
    for(const auto &value:files){
        if(!value.isObject())return fail("下载文件列表包含无效项目");
        const auto row=value.toObject();const auto role=row.value("role").toString();
        if(role!="driver"&&role!="boot"&&role!="calib"&&role!="app")return fail("下载文件类型无效："+role);
        if(unique.contains(role))return fail("下载列表存在重复类型："+role+"。每种类型最多一个文件，原列表未改动。");
        unique.insert(role,row);
    }
    for(const auto role:{"driver","boot","calib","app"})if(unique.contains(role))result.append(unique.value(role));
    return true;
}
ChannelSettings::ChannelSettings(){boot::FlashProfile p;QString error;if(boot::FlashProfile::fromJson(downloadProfile,p,error))downloadProfile=p.toJson();}
ChannelSettings ChannelSettings::defaults(communication::Bus bus) {
    ChannelSettings s;s.bus=bus;
    if(bus==communication::Bus::Lin){s.softwareId=ChannelPageInitialValues::linName;s.profileId=ChannelPageInitialValues::linProfile;s.bitrate=ChannelPageInitialValues::linBitrate;}
    return s;
}
static bool hex(const QString &text,quint32 maximum,quint32 &number) {
    static const QRegularExpression pattern("^(?:0[xX])?[0-9a-fA-F]{1,8}$");
    const QString input=text.trimmed();bool ok=false;
    if(!pattern.match(input).hasMatch())return false;
    number=input.toUInt(&ok,16);return ok && number<=maximum;
}
bool ChannelSettings::toConfiguration(communication::SoftwareChannelConfiguration &c,QString &error) const {
    error.clear();c={};c.bus=bus;c.softwareId=softwareId.trimmed();
    c.hardwareKey=hardwareKey;c.preferredHandle=handle;c.bitrate=bitrate;c.autoReconnect=autoReconnect;
    c.uds.profileId=profileId.trimmed();c.uds.p2Ms=p2Ms;c.uds.p2StarMs=p2StarMs;
    c.uds.testerPresentMs=testerPresentEnabled?testerPresentMs:0;c.uds.maxPendingMs=maxPendingMs;
    c.uds.programmingSession=quint8(programmingSession);c.uds.securityLevel=quint8(securityLevel);
    if(c.softwareId.isEmpty() || c.softwareId.size()>64 || c.uds.profileId.isEmpty() || c.uds.profileId.size()>80)
        error="请填写有效的软件通道名称和 UDS 配置名称。";
    else if(p2Ms<1 || p2Ms>60000 || p2StarMs<1 || p2StarMs>600000 || testerPresentMs<0 ||
            testerPresentMs>600000 || (testerPresentEnabled && testerPresentMs==0) || maxPendingMs<qMax(p2Ms,p2StarMs) || maxPendingMs>3600000 ||
            programmingSession<1 || programmingSession>0x7f || securityLevel<1 || securityLevel>0x7d || !(securityLevel&1))
        error="请检查 UDS 时序、会话及安全级别；安全请求级别需为奇数。";
    if(error.isEmpty() && (repeatDownloadCount<1 || repeatDownloadCount>10000 || repeatDownloadIntervalMs<0 || repeatDownloadIntervalMs>86400000))
        error="重复下载次数应为 1–10000，等待间隔应为 0–86400000 ms。";
    if(error.isEmpty() && (udsRepeatCount<1 || udsRepeatCount>10000 || udsRepeatDelayMs<0 || udsRepeatDelayMs>86400000))
        error="UDS 重发次数应为 1–10000，重发延时应为 0–86400000 ms。";
    auto tester=testerPresentRequest;tester.remove(' ');
    if(error.isEmpty()&&(p3Ms<0||p3Ms>600000||p2ServerMs<0||p2ServerMs>60000||p2StarServerMs<0||p2StarServerMs>600000||
        linSlotMs<0||linSlotMs>60000||linAsMs<1||linAsMs>60000||linCrMs<1||linCrMs>60000||
        (tester.compare("3E00",Qt::CaseInsensitive)!=0&&tester.compare("3E80",Qt::CaseInsensitive)!=0)))
        error="请检查会话/网络时序；TesterPresent 报文须为 3E 00 或 3E 80。";
    quint32 request=0,response=0,functional=0,nadValue=0,address=0;
    if(error.isEmpty() && bus==communication::Bus::Can) {
        const quint32 limit=extendedId?0x1fffffff:0x7ff;
        if(!hex(requestId,limit,request) || !hex(responseId,limit,response) || !hex(functionalId,limit,functional) ||
           request==response)error="CAN ID 无效，或请求与响应 ID 相同。";
        const QList<int> supported={1000000,800000,500000,250000,125000,100000,50000,20000,10000,5000};
        if(!supported.contains(bitrate))error="请选择支持的 CAN 波特率。";
        quint32 tx=0,rx=0;
        if(!hex(downloadTxId.isEmpty()?requestId:downloadTxId,limit,tx)||!hex(downloadRxId.isEmpty()?responseId:downloadRxId,limit,rx)||tx==rx)
            error="Invalid download CAN IDs";
        c.transport.requestId=request;c.transport.responseId=response;c.transport.functionalId=functional;
        c.transport.extendedId=extendedId;
    }
    if(error.isEmpty() && bus==communication::Bus::Lin) {
        if(!hex(nad,0x7d,nadValue) || nadValue==0)error="LIN NAD 必须为 01–7D（十六进制）。";
        if(bitrate<1000 || bitrate>20000)error="LIN 波特率范围为 1000–20000 bit/s。";
        c.transport.nad=quint8(nadValue);c.linMode=2;
    }
    if(error.isEmpty() && (!hex(applicationAddress,0xffffffff,address) || !hex(flashAddress,0xffffffff,address)))
        error="镜像基址必须为 32 位十六进制地址。";
    if(error.isEmpty() && !hex(applicationLength,0xffffffff,address))
        error="App length must be a 32-bit hexadecimal value";
    if(error.isEmpty() && (!hex(bootAddress,0xffffffff,address)||!hex(bootEraseLength,0xffffffff,address)))
        error="Boot address and length must be valid 32-bit hexadecimal values";
    if(error.isEmpty()&&(!hex(calibAddress,0xffffffff,address)||!hex(calibLength,0xffffffff,address)))error="Calib address and length must be valid 32-bit hexadecimal values";
    for(const auto &b:{bootBAddress,applicationBAddress})if(error.isEmpty()&&!b.isEmpty()&&!hex(b,0xffffffff,address))error="B address must be empty or a 32-bit hexadecimal address";
    if(error.isEmpty()&&!fingerprintHex.isEmpty()){
        static const QRegularExpression fpPattern("^[0-9a-fA-F]{38}$");
        if(!fpPattern.match(fingerprintHex).hasMatch())error="F184 must contain exactly 19 hexadecimal bytes";
        else {
            const auto fp=QByteArray::fromHex(fingerprintHex.toLatin1());
            const QDate date(2000+quint8(fp[0]),quint8(fp[1]),quint8(fp[2]));QString fpError;
            if(!boot::t008::fingerprint(date,fp.mid(3),fpError).size())error=fpError;
        }
    }
    boot::CanOptions canOptions;canOptions.txId=request;canOptions.rxId=response;canOptions.extended=extendedId;
    if(error.isEmpty() && bus==communication::Bus::Can && !boot::CanOptions::fromJson(canNetwork,canOptions,error))return false;
    QJsonArray checkedFiles;if(error.isEmpty()&&!effectiveDownloadFiles(downloadFiles,checkedFiles,&error))return false;
    boot::FlashProfile profile;
    if(error.isEmpty()&&!boot::FlashProfile::fromJson(downloadProfile,profile,error))return false;
    return error.isEmpty() && c.isValid();
}
QJsonObject ChannelSettings::toJson() const {
    boot::FlashProfile p;QString error;const auto effective=boot::FlashProfile::fromJson(downloadProfile,p,error)?p.toJson():downloadProfile;
    return {{"bus",bus==communication::Bus::Can?"CAN":"LIN"},{"softwareId",softwareId},{"profileId",profileId},
        {"canNetwork",canNetwork},{"downloadProfile",effective},{"hardwareKey",hardwareKey},{"handle",int(handle)},{"bitrate",bitrate},{"simulation",simulation},
        {"autoReconnect",autoReconnect},{"extendedId",extendedId},{"requestId",requestId},{"responseId",responseId},
        {"functionalId",functionalId},{"nad",nad},{"downloadTxId",downloadTxId},{"downloadRxId",downloadRxId},{"fingerprintHex",fingerprintHex},{"p2Ms",p2Ms},{"p2StarMs",p2StarMs},
        {"testerPresentEnabled",testerPresentEnabled},{"testerPresentMs",testerPresentMs},{"maxPendingMs",maxPendingMs},{"programmingSession",programmingSession},
        {"securityLevel",securityLevel},{"flashPath",flashPath},{"applicationPath",applicationPath},{"downloadFiles",downloadFiles},
        {"flashAddress",flashAddress},{"applicationAddress",applicationAddress},{"applicationLength",applicationLength},{"applicationNote",applicationNote},{"bootAddress",bootAddress},{"bootEraseLength",bootEraseLength},{"bootBAddress",bootBAddress},{"applicationBAddress",applicationBAddress},{"calibAddress",calibAddress},{"calibLength",calibLength},
        {"flashRequired",flashRequired},{"rxdEnabled",rxdEnabled},{"repeatDownloadEnabled",repeatDownloadEnabled},
        {"repeatDownloadCount",repeatDownloadCount},{"repeatDownloadIntervalMs",repeatDownloadIntervalMs},
        {"cddPath",cddPath},{"cddEcu",cddEcu},{"cddVariant",cddVariant},{"signalConfiguration",signalConfiguration},
        {"udsRepeatCount",udsRepeatCount},{"udsRepeatDelayMs",udsRepeatDelayMs},{"udsRepeatEnabled",udsRepeatEnabled},
        {"p3Ms",p3Ms},{"p2ServerMs",p2ServerMs},{"p2StarServerMs",p2StarServerMs},{"testerPresentRequest",testerPresentRequest},
        {"linSlotMs",linSlotMs},{"linAsMs",linAsMs},{"linCrMs",linCrMs}};
}
bool ChannelSettings::fromJson(const QJsonObject &o,ChannelSettings &result,QString &error) {
    if(o.value("bus")!="CAN" && o.value("bus")!="LIN"){error="配置中的总线类型无效。";return false;}
    ChannelSettings s=defaults(o.value("bus")=="CAN"?communication::Bus::Can:communication::Bus::Lin);
    s.defaultMemoryLayout=false;
    const auto text=[&o](const char *key,const QString &fallback){return o.value(key).toString(fallback);};
    s.softwareId=text("softwareId",s.softwareId);s.profileId=text("profileId",s.profileId);
    s.cddPath=text("cddPath",{});s.cddEcu=text("cddEcu",{});s.cddVariant=text("cddVariant",{});
    if(o.contains("signalConfiguration")&&!o["signalConfiguration"].isObject()){error="signalConfiguration must be an object";return false;}
    s.signalConfiguration=o["signalConfiguration"].toObject();
    s.udsRepeatCount=o.value("udsRepeatCount").toInt(s.udsRepeatCount);
    s.udsRepeatDelayMs=o.value("udsRepeatDelayMs").toInt(s.udsRepeatDelayMs);
    s.udsRepeatEnabled=o.value("udsRepeatEnabled").toBool(false);
    s.p3Ms=o.value("p3Ms").toInt(s.p3Ms);s.p2ServerMs=o.value("p2ServerMs").toInt(s.p2ServerMs);s.p2StarServerMs=o.value("p2StarServerMs").toInt(s.p2StarServerMs);
    s.testerPresentRequest=text("testerPresentRequest",s.testerPresentRequest);
    s.linSlotMs=o.value("linSlotMs").toInt(s.linSlotMs);s.linAsMs=o.value("linAsMs").toInt(s.linAsMs);s.linCrMs=o.value("linCrMs").toInt(s.linCrMs);
    if(o.contains("downloadProfile")&&!o.value("downloadProfile").isObject()){error="downloadProfile must be an object";return false;}
    if(o.contains("canNetwork")&&!o.value("canNetwork").isObject()){error="canNetwork must be an object";return false;}
    if(o.contains("canNetwork"))s.canNetwork=o.value("canNetwork").toObject();
    if(o.contains("downloadProfile")){
        s.downloadProfile=o.value("downloadProfile").toObject();
        if(!s.downloadProfile.contains("flow"))s.downloadProfile["flow"]="app";
        const auto defaults=ChannelPageInitialValues::initialDownloadProfile();
        if(!s.downloadProfile.contains("stepEnabled"))
            s.downloadProfile["stepEnabled"]=ChannelPageInitialValues::initialStepEnabled(s.downloadProfile["flow"].toString());
        if(!s.downloadProfile.contains("negativeResponseChecks"))
            s.downloadProfile["negativeResponseChecks"]=s.downloadProfile.value("feedbackChecks").isObject()
                ?s.downloadProfile.value("feedbackChecks"):defaults["negativeResponseChecks"];
        if(!s.downloadProfile.contains("timeoutChecks"))s.downloadProfile["timeoutChecks"]=defaults["timeoutChecks"];
        s.downloadProfile.remove("feedbackChecks");
    }
    s.hardwareKey=text("hardwareKey",s.hardwareKey);s.handle=quint32(o.value("handle").toInt(int(s.handle)));
    s.bitrate=o.value("bitrate").toInt(s.bitrate);s.simulation=o.value("simulation").toBool(s.simulation);
    s.autoReconnect=o.value("autoReconnect").toBool(s.autoReconnect);s.extendedId=o.value("extendedId").toBool(s.extendedId);
    s.requestId=text("requestId",s.requestId);s.responseId=text("responseId",s.responseId);
    if(o.contains("fingerprintHex")&&!o.value("fingerprintHex").isString()){error="F184 must be a hexadecimal string";return false;}
    s.functionalId=text("functionalId",s.functionalId);s.nad=text("nad",s.nad);s.downloadTxId=text("downloadTxId",{});s.downloadRxId=text("downloadRxId",{});s.fingerprintHex=text("fingerprintHex",{});
    s.p2Ms=o.value("p2Ms").toInt(s.p2Ms);s.p2StarMs=o.value("p2StarMs").toInt(s.p2StarMs);
    s.testerPresentMs=o.value("testerPresentMs").toInt(s.testerPresentMs);
    s.testerPresentEnabled=o.value("testerPresentEnabled").toBool(s.testerPresentEnabled);
    s.maxPendingMs=o.value("maxPendingMs").toInt(s.maxPendingMs);
    s.programmingSession=o.value("programmingSession").toInt(s.programmingSession);
    s.securityLevel=o.value("securityLevel").toInt(s.securityLevel);
    s.flashPath=text("flashPath",s.flashPath);s.applicationPath=text("applicationPath",s.applicationPath);
    if(o.contains("downloadFiles")&&!o["downloadFiles"].isArray()){error="downloadFiles must be an array";return false;}
    s.downloadFiles=o["downloadFiles"].toArray();
    // Configurations saved before this field existed retain their former base.
    s.flashAddress=text("flashAddress",s.flashAddress);s.applicationAddress=text("applicationAddress",QStringLiteral("00020000"));
    s.applicationLength=text("applicationLength",QStringLiteral("00057000"));s.applicationNote=text("applicationNote",{});
    s.bootAddress=text("bootAddress",s.bootAddress);s.bootEraseLength=text("bootEraseLength",QStringLiteral("0000F000"));
    for(const auto key:{"bootBAddress","applicationBAddress","calibAddress","calibLength"})if(o.contains(key)&&!o.value(key).isString()){error=QString::fromLatin1(key)+": must be a hexadecimal string";return false;}
    s.calibAddress=text("calibAddress",s.calibAddress);s.calibLength=text("calibLength",s.calibLength);
    s.bootBAddress=text("bootBAddress",QStringLiteral("0006D000"));s.applicationBAddress=text("applicationBAddress",s.applicationBAddress);
    s.flashRequired=o.value("flashRequired").toBool(s.flashRequired);
    s.rxdEnabled=o.value("rxdEnabled").toBool(s.rxdEnabled);
    s.repeatDownloadEnabled=o.value("repeatDownloadEnabled").toBool(s.repeatDownloadEnabled);
    s.repeatDownloadCount=o.value("repeatDownloadCount").toInt(s.repeatDownloadCount);
    s.repeatDownloadIntervalMs=o.value("repeatDownloadIntervalMs").toInt(s.repeatDownloadIntervalMs);
    communication::SoftwareChannelConfiguration c;
    if(!s.toConfiguration(c,error))return false;
    boot::FlashProfile p;if(!boot::FlashProfile::fromJson(s.downloadProfile,p,error))return false;s.downloadProfile=p.toJson();
    result=s;return true;
}
QString hardwareDeviceKey(const communication::HardwareChannel &h) {
    // SDK handles identify ports. Group only when the SDK provides an owner identity.
    if(h.key.startsWith("preview:") || h.bus==communication::Bus::Lin || h.persistentIdentity)
        return h.key.section(':',0,-2);
    return h.key;
}
QString connectionText(communication::ConnectionState state) {
    using S=communication::ConnectionState;
    switch(state){case S::Missing:return "未发现硬件";case S::Available:return "未连接";
    case S::Connecting:return "正在连接";case S::Connected:return "已连接";
    case S::Disconnecting:return "正在断开";case S::Fault:return "连接异常";}
    return {};
}
bool imageReady(const QString &path) {
    QFileInfo f(path);const QString ext=f.suffix().toLower();
    return !path.trimmed().isEmpty() && f.exists() && f.isFile() && f.isReadable() && f.size()>0 &&
        (ext=="bin" || ext=="hex");
}
QString imageDescription(const QString &path) {
    if(path.isEmpty())return "尚未选择镜像";
    if(!imageReady(path))return "请选择可读取且非空的 BIN / HEX 文件";
    const QFileInfo f(path);
    return QString("%1  ·  %2 KiB").arg(f.suffix().toUpper()).arg(f.size()/1024.0,0,'f',1);
}
}
