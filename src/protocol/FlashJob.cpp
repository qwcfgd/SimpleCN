#include "FlashJob.h"
#include <QDateTime>
#include <QJsonValue>
namespace boot {
QByteArray SimulationKey::calculate(quint8 level,const QByteArray &seed,QString &error){
    if(seed.size()!=4){error="Simulation seed must be 4 bytes";return {};}
    QByteArray key=seed;for(int i=0;i<key.size();++i)key[i]=char(quint8(key[i])^0xa5^level);return key;
}
QJsonObject FlashProfile::toJson() const{
    auto steps=stepEnabled,negative=negativeResponseChecks,timeouts=timeoutChecks;
    for(const auto&s:downloadSteps()){
        const QString id=s.id;
        if(isT008())steps[id]=enabled(id);
        if(isT008()||requiresNegativeResponseCheck(id))negative[id]=checksNegativeResponse(id);
        if(isT008()||fixedUncheckedTimeout(id))timeouts[id]=checksTimeout(id);
    }
    return {{"name",name},{"simulationOnly",simulationOnly},{"keyProvider",keyProvider},{"session",session},{"appSecurityLevel",appSecurityLevel},{"securityLevel",securityLevel},
        {"eraseRoutine",eraseRoutine},{"verifyRoutine",verifyRoutine},{"dependencyRoutine",dependencyRoutine},{"identityDid",identityDid},
        {"consecutiveFrameByteLimit",consecutiveFrameByteLimit},{"resetWaitMs",resetWaitMs},{"flow",flow},
        {"stepEnabled",steps},{"negativeResponseChecks",negative},{"timeoutChecks",timeouts},
        {"keyLibrary",keyLibrary},{"target",target}};
}
bool FlashProfile::fromJson(const QJsonObject &o,FlashProfile &out,QString &error){
    FlashProfile p;auto bad=[&]{error="Invalid flash profile";return false;};
    const auto defaults=p.toJson();
    for(auto it=o.begin();it!=o.end();++it)if(!defaults.contains(it.key())&&it.key()!="feedbackChecks"&&it.key()!="blockDataLimit"&&it.key()!="maxPduBytes"&&it.key()!="verifyEnabled"&&it.key()!="dependencyEnabled"&&it.key()!="identityEnabled")return bad();
    if(o.contains("name")){if(!o["name"].isString())return bad();p.name=o["name"].toString();}
    if(o.contains("keyProvider")){if(!o["keyProvider"].isString())return bad();p.keyProvider=o["keyProvider"].toString();}
    if(o.contains("simulationOnly")){if(!o["simulationOnly"].isBool())return bad();p.simulationOnly=o["simulationOnly"].toBool();}
    auto number=[&](const char *key,int fallback,int lo,int hi,int &v){v=fallback;if(!o.contains(key))return true;const auto x=o[key];if(!x.isDouble()||x.toDouble()!=x.toInt(-1))return false;v=x.toInt();return v>=lo&&v<=hi;};
    for(const auto key:{"flow","keyLibrary","target"})if(o.contains(key)&&!o[key].isString())return bad();
    p.flow=o.value("flow").toString(p.flow);p.keyLibrary=o.value("keyLibrary").toString();
    p.target=o.value("target").toString(p.target);
    if(p.target!="generic"&&!p.isT008())return bad();
    if(p.flow!="legacy"&&p.flow!="app"&&p.flow!="boot")return bad();
    for(const auto key:{"feedbackChecks","stepEnabled","negativeResponseChecks","timeoutChecks"})
        if(o.contains(key)&&!o[key].isObject())return bad();
    const auto validateChecks=[&](const QJsonObject &checks){
        for(auto it=checks.begin();it!=checks.end();++it){
            bool found=false;for(const auto &step:downloadSteps())if(it.key()==step.id)found=true;
            if(!found||!it.value().isBool())return false;
        }
        return true;
    };
    const auto legacyChecks=o["feedbackChecks"].toObject();
    p.stepEnabled=o["stepEnabled"].toObject();
    p.negativeResponseChecks=o.contains("negativeResponseChecks")?o["negativeResponseChecks"].toObject():legacyChecks;
    p.timeoutChecks=o["timeoutChecks"].toObject();
    if(!validateChecks(p.stepEnabled)||!validateChecks(p.negativeResponseChecks)||!validateChecks(p.timeoutChecks))return bad();
    for(const auto &s:downloadSteps()){const QString id=s.id;
        if(requiresNegativeResponseCheck(id))p.negativeResponseChecks[id]=true;
        if(fixedUncheckedTimeout(id))p.timeoutChecks[id]=false;
    }
    // Obsolete UI switches are accepted only so an old profile can be opened and
    // re-saved. They no longer control the download sequence.
    for(const auto key:{"verifyEnabled","dependencyEnabled","identityEnabled"})
        if(o.contains(key)&&!o[key].isBool())return bad();
    int v;
    if(!number("session",p.session,1,127,v))return bad();p.session=quint8(v);
    if(!number("securityLevel",p.securityLevel,1,125,v)||!(v&1))return bad();p.securityLevel=quint8(v);
    if(!number("appSecurityLevel",p.appSecurityLevel,1,125,v)||!(v&1))return bad();p.appSecurityLevel=quint8(v);
    if(!number("eraseRoutine",p.eraseRoutine,0,65535,v))return bad();p.eraseRoutine=quint16(v);
    if(!number("verifyRoutine",p.verifyRoutine,0,65535,v))return bad();p.verifyRoutine=quint16(v);
    if(!number("dependencyRoutine",p.dependencyRoutine,0,65535,v))return bad();p.dependencyRoutine=quint16(v);
    if(!number("identityDid",p.identityDid,0,65535,v))return bad();p.identityDid=quint16(v);
    // Migrate both historical names. blockDataLimit excluded SID/BSC; maxPduBytes
    // already included them. The new name describes the complete segmented PDU.
    if(o.contains("blockDataLimit")){if(!number("blockDataLimit",4093,1,4093,v))return bad();p.consecutiveFrameByteLimit=v+2;}
    if(o.contains("maxPduBytes")){if(!number("maxPduBytes",p.consecutiveFrameByteLimit,3,4095,v))return bad();p.consecutiveFrameByteLimit=v;}
    if(!number("consecutiveFrameByteLimit",p.consecutiveFrameByteLimit,3,4095,v))return bad();p.consecutiveFrameByteLimit=v;
    if(!number("resetWaitMs",p.resetWaitMs,0,60000,v))return bad();p.resetWaitMs=v;
    if(p.name.isEmpty()||p.name.size()>80||(p.keyProvider!="simulation-xor-v1"&&p.keyProvider!="external-generatekeyex"&&p.keyProvider!="seed-as-key")||(p.keyProvider=="simulation-xor-v1"&&!p.simulationOnly)||
       p.eraseRoutine==p.verifyRoutine||p.eraseRoutine==p.dependencyRoutine||p.verifyRoutine==p.dependencyRoutine)return bad();
    if(p.isT008()){
        if(p.simulationOnly||p.flow=="legacy"||p.keyProvider!="external-generatekeyex"||p.keyLibrary.trimmed().isEmpty()||p.session!=2||p.appSecurityLevel!=0x01||p.securityLevel!=0x11||
           p.eraseRoutine!=0xff00||p.verifyRoutine!=0xff01||p.dependencyRoutine!=0xff02||(p.identityDid!=0xf180&&p.identityDid!=0xf189))return bad();
        if(p.flow=="boot")for(const auto map:{&p.stepEnabled}){
            if(map->contains("pre1001"))(*map)["pre1001"]=false;
            if(map->contains("pre1003"))(*map)["pre1003"]=true;
        }
        for(const auto&s:downloadSteps()){
            const QString id=s.id;const bool required=p.enabled(id);
            if(p.stepEnabled.contains(id)&&p.stepEnabled[id].toBool()!=required)return bad();
        }
    }
    out=p;error.clear();return true;
}
static QByteArray routine(quint16 id){return QByteArray::fromHex("3101")+char(id>>8)+char(id);}
static QByteArray positive(QByteArray request,int length){
    const auto sid=quint8(request[0]);request[0]=char(sid+0x40);
    if(request.size()>1&&(sid==0x10||sid==0x11||sid==0x28||sid==0x3e||sid==0x85))
        request[1]=char(quint8(request[1])&0x7f);
    return request.left(length);
}
FlashJob::FlashJob(UdsSession &session,FlashProfile profile,std::unique_ptr<KeyProvider> key,QObject *parent)
 :QObject(parent),m_session(session),m_profile(std::move(profile)),m_key(std::move(key)){
    m_deadline.setSingleShot(true);connect(&m_deadline,&QTimer::timeout,this,[this]{fail(QString("T008 total job deadline exceeded (%1ms)").arg(t008::JobDeadlineMs));});
}
bool FlashJob::start(const FirmwareImage &app,const FirmwareImage &driver,QString &error){
    return start(app,driver,FirmwareImage{},error);
}
bool FlashJob::start(const FirmwareImage &app,const FirmwareImage &driver,const FirmwareImage &bootBundle,QString &error){
    FlashProfile valid;
    if(m_running||m_session.busy()||!FlashProfile::fromJson(m_profile.toJson(),valid,error)||!m_key||(app.segments.isEmpty()&&bootBundle.segments.isEmpty())){if(error.isEmpty())error="Job busy or missing image/key provider";return false;}
    if(!bootBundle.segments.isEmpty()&&(bootBundle.segments.size()!=1||bootBundle.segments.first().data.size()!=0x1e000)){
        error="Boot bundle must contain both complete 60 KiB slot images";return false;
    }
    m_segments=driver.segments;m_driverCount=m_segments.size();m_bootIndex=-1;m_appIndex=-1;m_total=0;m_bootSlotB.clear();
    if(!bootBundle.segments.isEmpty()){
        m_bootIndex=m_segments.size();const auto &bundle=bootBundle.segments.first().data;
        m_segments.append({0x5000,bundle.left(0xf000)});m_bootSlotB=bundle.mid(0xf000,0xf000);
    }
    m_isV2=!bootBundle.segments.isEmpty()||t008::looksLikeImage(app)||t008::looksLikeDriver(driver);
    if(!m_profile.fingerprintData.isEmpty()){
        const auto &fp=m_profile.fingerprintData;
        if(fp.size()!=19){error="F184 requires exactly 19 bytes";return false;}
        QString fpError;const QDate date(2000+quint8(fp[0]),quint8(fp[1]),quint8(fp[2]));
        if(t008::fingerprint(date,fp.mid(3),fpError).isEmpty()){error=fpError;return false;}
    }
    if(!app.segments.isEmpty()&&(m_profile.isT008()||m_isV2)){
        if(!t008::images(app,driver,m_targetPlan,error))return false;
        if((m_profile.enabled("driver36")&&!m_profile.enabled("driver34"))||(m_profile.enabled("app36")&&!m_profile.enabled("app34"))){error="TransferData requires RequestDownload";return false;}
        m_appIndex=m_segments.size();m_segments+=m_targetPlan.application;
    }else if(!app.segments.isEmpty()){m_appIndex=m_segments.size();m_segments+=app.segments;}
    for(int i=0;i<m_segments.size();++i){
        const auto &s=m_segments[i];const quint64 end=quint64(s.address)+quint64(s.data.size());
        if(s.data.isEmpty()||end>0x100000000ULL){error="Invalid image segment";return false;}
        m_total+=s.data.size();
    }
    ++m_epoch;m_index=0;m_acked=0;m_ignored=0;m_running=true;m_unresolvedSids.clear();m_versionObservation.clear();
    if(m_isV2)emit notice("T008: fingerprint, verification and dependency positive responses do not prove cryptographic verification; algorithms are not implemented.");
    if(m_profile.isT008()){
        m_wall.start();m_deadline.start(t008::JobDeadlineMs);emit notice("Reset is software reset, not physical POR.");
        preSteps(0);
    }else if(m_profile.flow=="legacy")enterProgramming();else preSteps(0);return true;
}
void FlashJob::ask(const QByteArray &request,const QByteArray &expected,std::function<void(const QByteArray &)> next){
    if(!m_running||!withinDeadline())return;const auto epoch=m_epoch;
    if(!m_session.request(request,expected,[this,epoch,next,request](bool ok,const QByteArray &pdu,const QString &error){
        if(!m_running||epoch!=m_epoch||!withinDeadline())return;if(!ok){fail(error);return;}
        QString why;if(m_profile.isT008()&&!t008::response(request,pdu,false,why)){fail(why);return;}next(pdu);
    }))fail("UDS request rejected by session");
}
void FlashJob::step(const QString &id,const QByteArray &request,const QByteArray &expected,std::function<void(const QByteArray &)> next,bool requiredData,
                    std::function<bool(const QByteArray &)> specialNegative,QByteArray recoverableNegative){
    if(!m_running||!withinDeadline())return;report(id);const auto epoch=m_epoch;
    const auto text=stepText(id);
    if(!m_profile.enabled(id)){
        bool bootOnlySkip=false;for(const auto &s:downloadSteps())if(id==s.id&&s.appOnly&&m_profile.flow=="boot")bootOnlySkip=true;
        if(!bootOnlySkip)++m_ignored;
        emit stepEvent(text+"未使能跳过");next({});return;
    }
    if(!request.isEmpty()&&m_unresolvedSids.contains(quint8(request[0]))){
        fail(id+": unresolved earlier response for same service; cannot safely correlate");return;
    }
    const bool checkNegative=m_profile.checksNegativeResponse(id);
    const bool checkTimeout=m_profile.checksTimeout(id);
    const bool suppressed=suppressesPositiveResponse(id);
    const qint64 started=QDateTime::currentMSecsSinceEpoch();emit stepEvent(text+"发送");
    if(!m_session.request(request,expected,[this,epoch,id,text,started,suppressed,checkNegative,checkTimeout,requiredData,next,request,specialNegative](bool ok,const QByteArray &pdu,const QString &error){
        if(!m_running||epoch!=m_epoch||!withinDeadline())return;
        const bool negative=pdu.size()==3&&quint8(pdu[0])==0x7f&&quint8(pdu[2])!=0x78;
        const bool timeout=error.contains("timeout",Qt::CaseInsensitive)||error.contains("deadline",Qt::CaseInsensitive);
        const bool responseTimeout=error=="UDS P2/P2* timeout"||error=="UDS absolute response deadline";
        if(negative){
            const auto nrc=QString::number(quint8(pdu[2]),16).rightJustified(2,'0').toUpper();
            emit stepEvent(text+"负响应NRC(0x"+nrc+")");
        }
        else if(timeout)emit stepEvent(text+QString(responseTimeout?"响应超时(%1ms)":"传输超时(%1ms)").arg(qMax<qint64>(0,QDateTime::currentMSecsSinceEpoch()-started)));
        else if(ok)emit stepEvent(text+(suppressed&&pdu.isEmpty()?"正响应抑制完成":"正响应"));
        else emit stepEvent(text+"响应异常");
        if(!ok){
            if(negative&&specialNegative&&specialNegative(pdu))return;
            if(!checkNegative&&negative){++m_ignored;emit notice(id+" · 已忽略负响应");next({});return;}
            if(!checkTimeout&&responseTimeout){
                ++m_ignored;m_unresolvedSids.insert(quint8(request[0]));emit notice(id+" · 已忽略响应超时");next({});return;
            }
            if(m_profile.isT008()){fail(id+": "+error);return;}
            fail(requiredData&&negative?id+" requires ECU response data: "+error:error);return;
        }
        QString why;if(m_profile.isT008()&&!t008::response(request,pdu,suppressed,why)){fail(id+": "+why);return;}
        next(pdu);
    },suppressed,false,std::move(recoverableNegative)))fail("UDS request rejected by session");
}
void FlashJob::enterProgramming(){
    const QByteArray req=QByteArray(1,char(0x10))+char(m_profile.flow=="legacy"?m_profile.session:2);
    step("boot1002",req,positive(req,2),[this](const QByteArray &){
        if(m_profile.isT008()){
            const auto epoch=m_epoch;
            QTimer::singleShot(m_profile.flow=="app"?m_profile.resetWaitMs:0,this,[this,epoch]{
                if(m_running&&epoch==m_epoch&&withinDeadline())seed();
            });
        }else seed();
    });
}
void FlashJob::preSteps(int index){
    if(m_profile.isT008()&&m_profile.flow=="boot"&&index==2){enterProgramming();return;}
    if(index==2){unlock(m_profile.appSecurityLevel,"pre2701","pre2702",[this]{preSteps(3);});return;}
    static const char *ids[]={"pre1001","pre1003","","pre220101","pre31010203","pre1003control","pre8502","pre2803"};
    static const char *requests[]={"1001","1003","","220101","31010203","1083","8582","288301"};
    if(index==8){enterProgramming();return;}
    const auto req=index==3?QByteArray::fromHex("22f180"):QByteArray::fromHex(requests[index]);
    const int echo=quint8(req[0])==0x31?4:quint8(req[0])==0x22?3:2;
    step(ids[index],req,positive(req,echo),[this,index](const QByteArray &){preSteps(index+1);});
}
void FlashJob::unlock(quint8 level,const QString &seedId,const QString &keyId,std::function<void()> next){
    const QByteArray req=QByteArray(1,char(0x27))+char(level);
    step(seedId,req,positive(req,2),[this,level,seedId,keyId,next](const QByteArray &pdu){
        const auto seed=pdu.mid(2);
        if(seed.isEmpty()&&pdu.isEmpty()){
            if(!m_profile.enabled(keyId)){next();return;}
            fail(keyId+" requires a valid security seed from "+seedId);return;
        }
        if(m_profile.isT008()&&seed.size()!=16){fail("T008 seed must be exactly 16 bytes");return;}
        if(seed.isEmpty()&&!m_profile.enabled(seedId)){
            if(!m_profile.enabled(keyId)){next();return;}
            fail("Security seed step disabled while key step is enabled");return;
        }
        if(seed.isEmpty()){fail("Empty security seed");return;}
        bool unlocked=true;for(unsigned char b:seed)if(b)unlocked=false;
        if(unlocked){next();return;}
        QString error;const auto key=m_key->calculate(level,seed,error);
        if(key.isEmpty()||!error.isEmpty()){fail("Key provider: "+error);return;}
        if(m_profile.isT008()&&key.size()!=16){fail("T008 external key must be exactly 16 bytes");return;}
        const QByteArray req=QByteArray(1,char(0x27))+char(level+1)+key;
        step(keyId,req,positive(req,2),[next](const QByteArray &){next();});
    },true);
}
void FlashJob::seed(){
    unlock(m_profile.securityLevel,"bootSeed","bootKey",[this]{
        if(m_profile.flow=="legacy"){nextSegment();return;}
        QString error;const auto data=m_profile.fingerprintData.isEmpty()?t008::fingerprint(QDate::currentDate(),QByteArray("TEST-PLACEHOLDER"),error):m_profile.fingerprintData;
        if(!error.isEmpty()||data.size()!=19){fail("F184 requires exactly 19 bytes");return;}
        const auto req=QByteArray::fromHex("2ef184")+data;
        emit notice(m_profile.fingerprintData.isEmpty()?"F184: TEST-PLACEHOLDER is a test identifier, not a device serial":"F184: configured value");
        step("fingerprint",req,QByteArray::fromHex("6ef184"),[this](const QByteArray &){nextSegment();});
    });
}
void FlashJob::nextSegment(){
    if(m_index>=m_segments.size()){finalSteps();return;}
    const auto &s=m_segments[m_index];m_offset=0;m_sequence=1;m_transferPacket=0;
    if(m_index<m_driverCount){report("Flash driver");download();return;}
    if(m_index==m_bootIndex){
        report("Erase Boot slot");
        const auto req=routine(m_profile.eraseRoutine)+char(0x44)+be32(m_profile.bootEraseBase)+be32(m_profile.bootEraseLength);
        step("erase",req,positive(req,4)+char(0),[this](const QByteArray &){download();});return;
    }
    if(m_index!=m_appIndex){download();return;}
    report("Erase application segment");
    const auto req=routine(m_profile.eraseRoutine)+char(0x44)+be32(m_profile.applicationEraseBase)+be32(m_profile.applicationEraseLength);
    step("erase",req,positive(req,4)+char(0),[this](const QByteArray &){download();});
}
void FlashJob::download(){
    const auto &s=m_segments[m_index];report("RequestDownload");
    const auto downloadId=m_index<m_driverCount?"driver34":"app34";
    const auto req=QByteArray::fromHex("340044")+be32(s.address)+be32(quint32(s.data.size()));
    step(downloadId,req,QByteArray(1,char(0x74)),[this,downloadId](const QByteArray &pdu){
        if(pdu.isEmpty()){
            const auto blockId=m_index<m_driverCount?"driver36":"app36";
            if(m_profile.enabled(blockId)){fail("TransferData requires RequestDownload parameters");return;}
            block();return;
        }
        if(pdu.size()<3){fail("Short RequestDownload response");return;}
        const int format=quint8(pdu[1]),n=format>>4;
        if((format&15)||n<1||n>4||pdu.size()!=2+n){fail("Invalid maxNumberOfBlockLength format");return;}
        const auto max=readBe(pdu.mid(2));if(max<=2){fail("ECU block length leaves no payload");return;}
        m_blockSize=int(qMin<quint32>(max,quint32(qMin(m_profile.consecutiveFrameByteLimit,m_session.maximumPdu()))))-2;
        if(m_isV2||m_profile.isT008())m_blockSize=qMin(m_blockSize,1024);
        if((m_isV2||m_profile.isT008())&&m_index>=m_driverCount){
            if(m_blockSize<1024){fail("T008 ECU block length cannot carry 1024 App bytes");return;}
            m_blockSize=1024;
        }
        block();
    },true,[this,req](const QByteArray &pdu){
        if(m_index!=m_bootIndex||req!=QByteArray::fromHex("340044000050000000f000")||
           pdu!=QByteArray::fromHex("7f3470")||m_bootSlotB.size()!=0xf000)return false;
        emit notice("Boot slot A unavailable (NRC 70); trying slot B");
        m_segments[m_index]={0x6d000,std::move(m_bootSlotB)};m_bootSlotB.clear();download();return true;
    },m_index==m_bootIndex&&s.address==0x5000&&!m_bootSlotB.isEmpty()?QByteArray::fromHex("7f3470"):QByteArray());
}
void FlashJob::block(){
    const auto &s=m_segments[m_index];
    const auto blockId=QString(m_index<m_driverCount?"driver36":"app36");
    if(!m_profile.enabled(blockId)){if(m_offset<s.data.size())++m_ignored;m_offset=s.data.size();}
    if(m_offset>=s.data.size()){
        report("RequestTransferExit");step(m_index<m_driverCount?"driver37":"app37",QByteArray(1,char(0x37)),QByteArray(1,char(0x77)),[this](const QByteArray &){verify();});return;
    }
    m_blockLength=qMin(m_blockSize,int(s.data.size())-m_offset);
    if((m_isV2||m_profile.isT008())&&m_index>=m_driverCount){
        // Send the actual final bytes; the ECU decides whether the image length is acceptable.
    }
    const auto req=QByteArray(1,char(0x36))+char(m_sequence)+s.data.mid(m_offset,m_blockLength);
    ++m_transferPacket;
    step(m_index<m_driverCount?"driver36":"app36",req,QByteArray(1,char(0x76))+char(m_sequence),[this](const QByteArray &pdu){
        m_offset+=m_blockLength;if(pdu.startsWith(QByteArray(1,char(0x76))+char(m_sequence)))m_acked+=m_blockLength;++m_sequence;report("TransferData step completed");block();
    });
}
void FlashJob::verify(){
    const auto &s=m_segments[m_index];report((m_isV2||m_profile.isT008())?"Verification service response (algorithm not implemented)":"Verify segment CRC32");
    const auto req=routine(m_profile.verifyRoutine)+be32(s.address)+be32(quint32(s.data.size()))+
        ((m_isV2||m_profile.isT008())?QByteArray(4,'\0'):be32(crc32(s.data)));
    step(m_index<m_driverCount?"driverVerify":"appVerify",req,positive(req,4)+char(0),[this](const QByteArray &){
        if(m_index==m_bootIndex){
            const auto commit=routine(m_profile.dependencyRoutine);
            step("dependency",commit,positive(commit,4)+char(0),[this](const QByteArray &){++m_index;if(m_index>=m_segments.size())resetAfterTransfers();else nextSegment();});
        }else{++m_index;nextSegment();}
    });
}
void FlashJob::finalSteps(){
    const auto req=routine(m_profile.dependencyRoutine);
    step("dependency",req,positive(req,4)+char(0),[this](const QByteArray &){resetAfterTransfers();});
}
void FlashJob::resetAfterTransfers(){
    step("reset",QByteArray::fromHex("1101"),QByteArray::fromHex("5101"),[this](const QByteArray &){
        const auto epoch=m_epoch;
        QTimer::singleShot(m_profile.resetWaitMs,this,[this,epoch]{
            if(!m_running||epoch!=m_epoch)return;
            if(m_profile.flow=="legacy")identify();else postSteps(0);
        });
    });
}
void FlashJob::postSteps(int index){
    static const char *ids[]={"post1003","post14","post2800","post8501","post1001"};
    static const char *requests[]={"1003","14ffffff","288001","8581","1081"};
    if(index==5){identify();return;}
    if(index==1){
        step("post14",QByteArray::fromHex("14ffffff"),QByteArray::fromHex("54"),[this](const QByteArray &pdu){
            if(!pdu.isEmpty()&&quint8(pdu[0])==0x54&&pdu!=QByteArray::fromHex("54")){fail("post14 requires exact 54 response");return;}postSteps(2);
        });return;
    }
    const auto req=QByteArray::fromHex(requests[index]);
    step(ids[index],req,positive(req,index==1?1:2),[this,index](const QByteArray &){postSteps(index+1);});
}
void FlashJob::identify(){
    if(m_profile.isT008()||(m_isV2&&m_profile.identityDid==0xf189)){
        if(m_profile.identityDid==0xf180){
            step("identity",QByteArray::fromHex("22f180"),QByteArray::fromHex("62f180"),[this](const QByteArray &){done();},true);return;
        }
        if(m_unresolvedSids.contains(0x22)){fail("identity: unresolved earlier response for same service; cannot safely correlate");return;}
        const auto epoch=m_epoch;const auto request=QByteArray::fromHex("22f189");
        if(!m_session.request(request,QByteArray::fromHex("62f189"),[this,epoch,request](bool ok,const QByteArray &pdu,const QString &){
            if(!m_running||epoch!=m_epoch||!withinDeadline())return;
            QString why;
            if(!ok||!t008::response(request,pdu,false,why))m_versionObservation=QString::fromUtf8("F189 无法读取，请人工确认");
            else if(pdu.mid(3)!=m_targetPlan.version.leftJustified(9,'\0'))m_versionObservation=QString::fromUtf8("F189 版本不匹配，请人工确认");
            else m_versionObservation=QString::fromUtf8("F189 版本匹配（刷后观察）");
            emit notice(m_versionObservation);done();
        })){
            m_versionObservation=QString::fromUtf8("F189 无法读取，请人工确认");emit notice(m_versionObservation);done();
        }
        return;
    }
    const auto req=QByteArray(1,char(0x22))+char(m_profile.identityDid>>8)+char(m_profile.identityDid);
    const auto expected=positive(req,3)+(m_profile.simulationOnly?QByteArray("SIM-ECU"):QByteArray());
    step("identity",req,expected,[this](const QByteArray &){done();});
}
void FlashJob::done(){
    if(!withinDeadline())return;
    m_running=false;m_deadline.stop();m_session.cancel();
    const auto text=m_ignored?QString("流程结束，下载未完全确认（%1 个步骤未确认）").arg(m_ignored):QString("下载流程完成");
    const auto result=m_versionObservation.isEmpty()?text:text+"; "+m_versionObservation;
    emit progress(m_ignored?int(qMin<qint64>(99,m_acked*100/qMax<qint64>(1,m_total))):100,result);emit finished(m_ignored==0,result);
}
bool FlashJob::withinDeadline(){
    if(m_profile.isT008()&&m_wall.isValid()&&m_wall.elapsed()>=t008::JobDeadlineMs){fail(QString("T008 total job deadline exceeded (%1ms)").arg(t008::JobDeadlineMs));return false;}return true;
}
void FlashJob::report(const QString &step){emit progress(int(qMin<qint64>(99,m_acked*100/qMax<qint64>(1,m_total))),step);}
QString FlashJob::stepText(const QString &id) const{
    if(m_profile.isT008()){
        if(id=="pre220101")return "step05 22 F180 读取预置信息 ";
        if(id=="driverVerify"||id=="appVerify")return "31 校验服务应答（算法未实施） ";
        if(id=="dependency")return "31 依赖服务应答（算法未实施） ";
        if(id=="fingerprint")return "2E F184 写入19字节指纹 ";
        if(id=="identity")return "22 读取配置的身份 DID ";
    }
    for(int i=0;i<downloadSteps().size();++i){
        if(id!=downloadSteps()[i].id)continue;
        const auto prefix=QString("step%1 ").arg(i+1,2,10,QChar('0'));
        if(id=="driver36"||id=="app36")return prefix+QString("36传输数据第%1包").arg(m_transferPacket);
        QString value=QString::fromUtf8(downloadSteps()[i].label);value.replace(" · ","");
        return prefix+value;
    }
    return id+" ";
}
void FlashJob::fail(const QString &error){if(!m_running)return;m_running=false;m_deadline.stop();++m_epoch;m_session.cancel();emit finished(false,error);}
void FlashJob::cancel(){++m_epoch;m_running=false;m_deadline.stop();m_session.cancel();}
}
