#pragma once
#include "office.hpp"
#include <set>
namespace paper {
struct OfficePdfResult {
    int id = 0;
    std::wstring pdfPath, error, engine;
};
class OfficePdfService {
    struct Job {
        int id;
        std::wstring path;
        OfficeEngine engine;
    };
    HWND window_;
    std::filesystem::path root_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::deque<OfficePdfResult> results_;
    std::set<int> cancelled_;
    std::atomic<bool> stop_{false};
    bool cancelled(int id);
    OfficePdfResult convert(const Job &);
    void run();

  public:
    static constexpr UINT ReadyMessage = WM_APP + 11;
    OfficePdfService(HWND, std::filesystem::path);
    ~OfficePdfService();
    void open(int id, std::wstring path, OfficeEngine engine);
    void cancel(int id);
    std::deque<OfficePdfResult> take();
};
int exportOfficePdf(const std::filesystem::path &, const std::filesystem::path &, OfficeEngine);
} // namespace paper
