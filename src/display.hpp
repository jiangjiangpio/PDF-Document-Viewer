#pragma once
#include "settings.hpp"
#include <windows.h>

namespace paper {
// Calibration is tied to the physical display identity and its current pixel resolution.
struct ScreenMetrics {
    std::string key;
    std::wstring name;
    double pixelsPerMm = 96. / 25.4;
    int width = 0, height = 0;
    bool calibrated = false, physical = false;
};
inline Size monitorMillimeters(const std::wstring &interfacePath) {
    // Monitor interface names identify the matching EDID instance, not just its panel model.
    size_t first = interfacePath.find(L'#'), second = interfacePath.find(L'#',first+1);
    size_t last = interfacePath.find(L'#',second+1);
    if (first==std::wstring::npos || second==std::wstring::npos || last==std::wstring::npos) return {0,0};
    auto instance = interfacePath.substr(first+1,last-first-1);
    std::replace(instance.begin(),instance.end(),L'#',L'\\');
    auto path = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + instance + L"\\Device Parameters";
    BYTE data[4096]{}; DWORD bytes=sizeof(data);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"EDID",RRF_RT_REG_BINARY,nullptr,data,&bytes) ||
        bytes<128 || data[0]!=0 || data[1]!=0xff) return {0,0};
    unsigned checksum=0;
    for (int i=0;i<128;++i) checksum+=data[i];
    if (checksum%256) return {0,0};
    for (int i=54;i+17<126;i+=18) {
        if (!(data[i] || data[i+1])) continue;
        int w=data[i+12]+((data[i+14]&0xf0)<<4), h=data[i+13]+((data[i+14]&0x0f)<<8);
        if (w>=40 && h>=30) return {double(w),double(h)};
    }
    return {double(data[21])*10,double(data[22])*10};
}
inline ScreenMetrics metricsForMonitor(HMONITOR monitor, HWND window, const AppSettings &settings) {
    ScreenMetrics result;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);

    GetMonitorInfoW(monitor, &info);
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME);
    std::wstring identity = device.DeviceID[0] ? device.DeviceID : info.szDevice;
    identity += L":" + std::to_wstring(info.rcMonitor.right - info.rcMonitor.left) + L"x" +
                std::to_wstring(info.rcMonitor.bottom - info.rcMonitor.top);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, identity.data(), static_cast<int>(identity.size()),
                                    nullptr, 0, nullptr, nullptr);
    result.key.resize(bytes);
    WideCharToMultiByte(CP_UTF8, 0, identity.data(), static_cast<int>(identity.size()), result.key.data(),
                        bytes, nullptr, nullptr);
    result.name = device.DeviceString[0] ? device.DeviceString : info.szDevice;
    result.width = info.rcMonitor.right-info.rcMonitor.left;
    result.height = info.rcMonitor.bottom-info.rcMonitor.top;
    auto calibration = settings.screenCalibrations.find(result.key);
    result.calibrated = calibration != settings.screenCalibrations.end();
    result.pixelsPerMm = result.calibrated ? calibration->second : GetDpiForWindow(window) / 25.4;
    if (!result.calibrated) {
        static thread_local std::map<std::wstring, Size> dimensions;
        auto it = dimensions.find(identity);
        if (it == dimensions.end()) it = dimensions.emplace(identity,monitorMillimeters(device.DeviceID)).first;
        auto mm=it->second;
        if ((result.width < result.height) != (mm.w < mm.h)) std::swap(mm.w,mm.h);
        if (mm.w>0 && mm.h>0) {
            double dx=result.width/mm.w, dy=result.height/mm.h;
            if (dx>=.5 && dx<=20 && std::abs(dx/dy-1)<.08) {
                result.pixelsPerMm=dx;
                result.physical=true;
            }
        }
    }
    return result;
}
inline ScreenMetrics screenMetrics(HWND window, const AppSettings &settings) {
    return metricsForMonitor(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),window,settings);
}
} // namespace paper
