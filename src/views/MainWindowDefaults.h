#pragma once
#include "ChannelPageDefaults.h"
namespace host {
struct MainWindowInitialValues {
    static constexpr bool simulation=ChannelPageInitialValues::defaultSimulation;
    static constexpr int width=1320,height=850,fontSize=9;
    static constexpr const char *fontFamily="Microsoft YaHei UI";
    static constexpr const char *applicationName="SimpleCN";
    static constexpr const char *organizationName="QtController";
    static constexpr const char *version="1.4.3";
    static constexpr const char *versionLabel="ReleaseVer: 1.4.3";
    static constexpr const char *title="SimpleCN V1.4.3";
    static constexpr const char *description="SIMPLE CONTROLLER FOR CAN/LIN";
    static constexpr const char *configPath="/config/channels.json";
};
}
