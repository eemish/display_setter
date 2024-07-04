
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <vector>
#include <cwctype>
#include <string>

#include <algorithm> // For std::transform
#include <cctype>    // For std::tolower

struct Arguments
{
    bool save;
    int configID;
};

struct ConfigInfo 
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

std::wstring logDisplayConfigInfo(ConfigInfo const & configInfo)
{
    std::wstring deviceInfo;

    // For each active path
    for (auto& path : configInfo.paths)
    {
        // Find the target (monitor) friendly name
        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
        targetName.header.adapterId = path.targetInfo.adapterId;
        targetName.header.id = path.targetInfo.id;
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        LONG result = DisplayConfigGetDeviceInfo(&targetName.header);

        if (result != ERROR_SUCCESS)
        {
            MessageBox(NULL, "DisplayConfigGetDeviceInfo(&targetName.header); failed.", "Error", 0);
        }

        // Find the adapter device name
        DISPLAYCONFIG_ADAPTER_NAME adapterName = {};
        adapterName.header.adapterId = path.targetInfo.adapterId;
        adapterName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
        adapterName.header.size = sizeof(adapterName);

        result = DisplayConfigGetDeviceInfo(&adapterName.header);

        if (result != ERROR_SUCCESS)
        {
            MessageBox(NULL, "DisplayConfigGetDeviceInfo(&adapterName.header); failed.", "Error", 0);
        }

        deviceInfo += L"Monitor with name ";
        deviceInfo += (targetName.flags.friendlyNameFromEdid ? targetName.monitorFriendlyDeviceName : L"Unknown");
        deviceInfo += L" is connected to adapter ";
        deviceInfo += adapterName.adapterDevicePath;
        deviceInfo += L" on target ";
        WCHAR targetInfoIDString[16] = {L'\0'};
        _itow_s(path.targetInfo.id, targetInfoIDString, 15, 10);
        deviceInfo += targetInfoIDString;
        deviceInfo += L"\n\n";
    }

    return deviceInfo;
}

void saveConfig(ConfigInfo & config, unsigned id)
{
    UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;
    flags = QDC_ALL_PATHS;
    LONG result = ERROR_SUCCESS;

    do
    {
        // Determine how many path and mode structures to allocate
        UINT32 pathCount, modeCount;
        result = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);
        if (result != ERROR_SUCCESS)
        {
            MessageBox(NULL, "GetDisplayConfigBufferSizes failed.", "Error", 0);
        }

        // Allocate the path and mode arrays
        config.paths.resize(pathCount);
        config.modes.resize(modeCount);

        // Get all active paths and their modes
        result = QueryDisplayConfig(flags, &pathCount, config.paths.data(), &modeCount, config.modes.data(), nullptr);

        // The function may have returned fewer paths/modes than estimated
        config.paths.resize(pathCount);
        config.modes.resize(modeCount);

        // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
        // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
    } while (result == ERROR_INSUFFICIENT_BUFFER);

    if (result != ERROR_SUCCESS)
    {
        MessageBox(NULL, "QueryDisplayConfig failed.", "Error", 0);
    }

    char idString[16] = {'\0'};
    _itoa(id, idString, 10);
    std::string pathInfoFileName = "DISPLAYCONFIG_PATH_INFO_";
    pathInfoFileName += idString;
    pathInfoFileName += ".bin";
    std::string modeInfoFileName = "DISPLAYCONFIG_MODE_INFO_";
    modeInfoFileName += idString;
    modeInfoFileName += ".bin";

    FILE* pFile;
    pFile = fopen(pathInfoFileName.c_str(), "wb");
    fwrite(config.paths.data(), 1, config.paths.size() * sizeof(DISPLAYCONFIG_PATH_INFO), pFile);
    fclose(pFile);
    pFile = fopen(modeInfoFileName.c_str(), "wb");
    fwrite(config.modes.data(), 1, config.modes.size() * sizeof(DISPLAYCONFIG_MODE_INFO), pFile);
    fclose(pFile);
}

void loadConfig(ConfigInfo & config, unsigned id)
{
    unsigned char * pathInfoBuffer = nullptr;
    size_t pathInfoBufferSize = 0;
    unsigned char * modeInfoBuffer = nullptr;
    size_t modeInfoBufferSize = 0;

    char idString[16] = {'\0'};
    _itoa(id, idString, 10);
    std::string pathInfoFileName = "DISPLAYCONFIG_PATH_INFO_";
    pathInfoFileName += idString;
    pathInfoFileName += ".bin";
    std::string modeInfoFileName = "DISPLAYCONFIG_MODE_INFO_";
    modeInfoFileName += idString;
    modeInfoFileName += ".bin";

    FILE * pFile = fopen(pathInfoFileName.c_str(), "rb");
    if(pFile)
    {
        fseek(pFile, 0, SEEK_END);
        pathInfoBufferSize = ftell(pFile);
        rewind(pFile);

        pathInfoBuffer = (unsigned char*) malloc(sizeof(unsigned char) * pathInfoBufferSize);
        fread(pathInfoBuffer, 1, pathInfoBufferSize, pFile);

        fclose(pFile);
    }
    else
    {
        MessageBoxW(NULL, L"Failed to open DISPLAYCONFIG_PATH_INFO.binary", L"ERROR", 0);
    }

    pFile = fopen(modeInfoFileName.c_str(), "rb");
    if(pFile)
    {
        fseek(pFile, 0, SEEK_END);
        modeInfoBufferSize = ftell(pFile);
        rewind(pFile);

        modeInfoBuffer = (unsigned char*) malloc(sizeof(unsigned char) * modeInfoBufferSize);
        fread(modeInfoBuffer, 1, modeInfoBufferSize, pFile);

        fclose(pFile);
    }
    else
    {
        MessageBoxW(NULL, L"Failed to open DISPLAYCONFIG_MODE_INFO.binary", L"ERROR", 0);
    }

    size_t pathInfoCount = pathInfoBufferSize / sizeof(DISPLAYCONFIG_PATH_INFO);
    size_t modeInfoCount = modeInfoBufferSize / sizeof(DISPLAYCONFIG_MODE_INFO);
    config.paths.resize(pathInfoCount);
    config.modes.resize(modeInfoCount);

    // For each active path
    for (size_t i = 0; i < config.paths.size(); ++i)
    {
        memcpy(&config.paths[i], (DISPLAYCONFIG_PATH_INFO*)pathInfoBuffer + i, sizeof(DISPLAYCONFIG_PATH_INFO));
    }
    for (size_t i = 0; i < config.modes.size(); ++i)
    {
        memcpy(&config.modes[i], (DISPLAYCONFIG_MODE_INFO*)modeInfoBuffer + i, sizeof(DISPLAYCONFIG_MODE_INFO));
    }

    free(pathInfoBuffer);
    free(modeInfoBuffer);
}

Arguments buildArguments()
{
    Arguments args;
    args.save = false;
    args.configID = -1;
    
    int argc;
    LPWSTR *argv = CommandLineToArgvW(
        (LPCWSTR)GetCommandLineW(), &argc);

    for (int i = 0; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        for (wchar_t& c : arg)
        {
            c = std::towlower(c);
        }
        if (arg.compare(L"save") == 0)
        {
            args.save = true;
        }
        else if (arg.compare(L"configid") == 0)
        {
            if (i + 1 < argc)
            {
                arg = argv[i + 1];
                for (wchar_t& c : arg)
                {
                    c = std::towlower(c);
                }
                args.configID = _wtoi(arg.data());
                i += 1;
            }
        }
    }

    return args;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    Arguments args = buildArguments();
    if (args.configID < 0)
    {
       return MessageBoxW(NULL, L"Missing Config ID", L"ERROR", 0);
    }

    if (args.save)
    {
        ConfigInfo config;
        saveConfig(config, args.configID);
        std::wstring deviceInfo1 = logDisplayConfigInfo(config);
    }
    else
    {

        ConfigInfo config;
        loadConfig(config, args.configID);
        std::wstring deviceInfo2 = logDisplayConfigInfo(config);
        UINT setFlags = SDC_APPLY |
                   SDC_SAVE_TO_DATABASE |
                   SDC_ALLOW_CHANGES |
                   SDC_USE_SUPPLIED_DISPLAY_CONFIG;

        LONG result = SetDisplayConfig((UINT32)config.paths.size(), &config.paths[0], (UINT32)config.modes.size(), &config.modes[0], setFlags);
        if (result != ERROR_SUCCESS)
        {
            MessageBox(NULL, "SetDisplayConfig failed.", "Error", 0);
        }
    }

    return MessageBoxW(NULL, L"set", L"SUCCESS", 0);
}