#include <QtTest>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <windows.h>
#include "communication/DriverLibrary.h"

// No vendor entry point is called: this verifies relocation, PE architecture,
// DLL dependency resolution and the required exported API surface only.
class DriverRuntimeTest : public QObject {
    Q_OBJECT
private slots:
    void applicationRelativePaths() {
        QTemporaryDir elsewhere; QVERIFY(elsewhere.isValid());
        const auto cwd=QDir::currentPath(); QVERIFY(QDir::setCurrent(elsewhere.path()));
        const auto path=communication::driverLibraryPath("peak","PCANBasic.dll");
        QVERIFY(QFileInfo(path).isAbsolute());
        QVERIFY(path.startsWith(QDir::toNativeSeparators(QCoreApplication::applicationDirPath())));
        QVERIFY(!path.startsWith(QDir::toNativeSeparators(elsewhere.path())));
        QVERIFY(QDir::setCurrent(cwd));
    }
    void packagedLibrariesLoad_data() {
        QTest::addColumn<QString>("vendor"); QTest::addColumn<QString>("name"); QTest::addColumn<QByteArray>("symbol");
        QTest::newRow("pcan") << QString("peak") << QString("PCANBasic.dll") << QByteArray("CAN_Initialize");
        QTest::newRow("plin") << QString("peak") << QString("PLinApi.dll") << QByteArray("LIN_InitializeHardware");
        QTest::newRow("tosun-helper") << QString("tosun") << QString("libTSH.dll") << QByteArray();
        QTest::newRow("tosun-binlog") << QString("tosun") << QString("binlog.dll") << QByteArray();
        QTest::newRow("tosun-log") << QString("tosun") << QString("libLog.dll") << QByteArray();
        QTest::newRow("tosun") << QString("tosun") << QString("libTSCAN.dll") << QByteArray("tscan_scan_devices");
        QTest::newRow("vector") << QString("vector") << QString(sizeof(void*)==8?"vxlapi64.dll":"vxlapi.dll") << QByteArray("xlGetDriverConfig");
    }
    void packagedLibrariesLoad() {
        QFETCH(QString,vendor); QFETCH(QString,name); QFETCH(QByteArray,symbol);
        const auto path=communication::driverLibraryPath(vendor,name);
        QVERIFY2(QFileInfo::exists(path),qPrintable(path));
        auto module=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        const auto error=GetLastError();
        QVERIFY2(module,qPrintable(QString("%1: Windows error %2").arg(path).arg(error)));
        if(!symbol.isEmpty()) QVERIFY2(GetProcAddress(module,symbol.constData()),symbol.constData());
        // PLIN DLL detach is known unsafe under a debugger; keep modules mapped
        // for this short-lived test, matching the application's PLIN policy.
    }
};
QTEST_GUILESS_MAIN(DriverRuntimeTest)
#include "test_driver_runtime.moc"
