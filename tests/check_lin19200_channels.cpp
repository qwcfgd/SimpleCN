#include "viewmodels/ChannelConfigurationViewModel.h"
#include "viewmodels/ChannelViewModel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

using namespace host;

static ChannelSettings simulated(communication::Bus bus, const QString &name, int bitrate)
{
    auto settings = ChannelSettings::defaults(bus);
    settings.softwareId = name;
    settings.bitrate = bitrate;
    settings.simulation = true;
    return settings;
}

static bool checkExport(ChannelViewModel &channel, const QString &directory, QString &error)
{
    const QString id = channel.settings().softwareId;
    channel.log(id + " first event");
    channel.log(id + " second event");
    const auto expected = (channel.logs().join('\n') + '\n').toUtf8();
    const QString path = directory + "/" + id + ".txt";
    if (!channel.exportLogs(path, error))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.readAll() != expected) {
        error = id + " event log differs after export";
        return false;
    }
    return true;
}

static bool check(const QString &appCdd, const QString &bootCdd, QString &error)
{
    QTemporaryDir directory(QDir::currentPath() + "/host-channels-XXXXXX");
    if (!directory.isValid()) {
        error = directory.errorString();
        return false;
    }
    ChannelViewModel can(simulated(communication::Bus::Can, "CAN01", 500000));
    ChannelViewModel lin1(simulated(communication::Bus::Lin, "LIN01", 19200));
    ChannelViewModel lin2(simulated(communication::Bus::Lin, "LIN02", 19200));
    if (!lin1.loadCdd(appCdd, error) || !lin2.loadCdd(bootCdd, error))
        return false;

    const QVector<ChannelViewModel *> channels{&can, &lin1, &lin2};
    const QString project = directory.filePath("channels.json");
    ChannelConfigurationViewModel configuration(project);
    QVector<ChannelSettings> restored;
    if (!configuration.save(channels, error) || !configuration.load(project, {}, restored, error))
        return false;
    if (restored.size() != channels.size()) {
        error = "Saved channel count differs";
        return false;
    }
    for (int i = 0; i < channels.size(); ++i) {
        const auto &row = restored[i];
        const auto &original = channels[i]->settings();
        if (!row.simulation || row.softwareId != original.softwareId || row.bus != original.bus ||
            row.bitrate != (i == 0 ? 500000 : 19200)) {
            error = "Saved channel identity, bus, or bitrate differs: " + original.softwareId;
            return false;
        }
        if (i > 0 && (row.nad != "5F" || row.cddPath != (i == 1 ? appCdd : bootCdd))) {
            error = "Saved LIN NAD or CDD path differs: " + row.softwareId;
            return false;
        }
        if (!checkExport(*channels[i], directory.path(), error))
            return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() != 3) {
        std::fprintf(stderr, "Usage: check_lin19200_channels <App LIN CDD with NAD 5F> <Boot LIN CDD with NAD 5F>\n");
        return 2;
    }
    QString error;
    if (!check(QFileInfo(args[1]).absoluteFilePath(), QFileInfo(args[2]).absoluteFilePath(), error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    std::puts("PASS: simulated CAN and dual 19200 LIN configuration, CDD/NAD reload, and event log exports");
    return 0;
}
