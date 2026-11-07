#include "office_convert.hpp"
#include <fstream>
#include <oleauto.h>
namespace paper {
namespace {
struct Handle {
    HANDLE h;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
using automation::call;
using automation::optionalPut;
using automation::Value;
std::wstring quote(const std::wstring &s) {
    std::wstring result = L"\"";
    size_t n = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++n;
            continue;
        }
        result.append(c == L'\"' ? n * 2 + 1 : n, L'\\');
        result += c;
        n = 0;
    }
    result.append(n * 2, L'\\');
    return result + L'\"';
}
bool validPdf(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    char h[5]{};
    in.read(h, 5);
    return in && std::string(h, 5) == "%PDF-";
}
const wchar_t *progId(OfficeEngine e) {
    return e == OfficeEngine::Word ? L"Word.Application" : L"kwps.Application";
}
} // namespace
int exportOfficePdf(const std::filesystem::path &input, const std::filesystem::path &output,
                    OfficeEngine engine) {
    automation::MessageFilterScope messages;
    try {
        if (!isOfficeDocument(input) || engine == OfficeEngine::Auto)
            throw std::runtime_error("Select an installed Word or WPS engine");
        Value app = automation::createApplication(engine);
        Value document;
        try {
            document = automation::openReadOnly(app.object(), input);
            call(document.object(), L"ExportAsFixedFormat", DISPATCH_METHOD,
                 {Value(output.wstring()), Value(17), Value::boolean(false)});
            call(document.object(), L"Close", DISPATCH_METHOD, {Value(0)});
            VariantClear(&document.v);
            call(app.object(), L"Quit", DISPATCH_METHOD, {Value(0)});
        } catch (...) {
            if (document.v.vt == VT_DISPATCH)
                try {
                    call(document.object(), L"Close", DISPATCH_METHOD, {Value(0)});
                } catch (...) {
                }
            try {
                call(app.object(), L"Quit", DISPATCH_METHOD, {Value(0)});
            } catch (...) {
            }
            throw;
        }
        if (!validPdf(output))
            throw std::runtime_error("Office did not create a valid PDF");
        return 0;
    } catch (const std::exception &e) {
        std::ofstream out(output.parent_path() / L"error.txt", std::ios::binary);
        out << e.what();
        return 1;
    }
}
OfficePdfService::OfficePdfService(HWND w, std::filesystem::path p) : window_(w), root_(std::move(p)) {
    thread_ = std::thread(&OfficePdfService::run, this);
}
OfficePdfService::~OfficePdfService() {
    stop_ = true;
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
bool OfficePdfService::cancelled(int id) {
    std::lock_guard lock(mutex_);
    return stop_ || cancelled_.count(id);
}
void OfficePdfService::open(int id, std::wstring path, OfficeEngine engine) {
    std::lock_guard lock(mutex_);
    jobs_.push_back({id, std::move(path), engine});
    wake_.notify_one();
}
void OfficePdfService::cancel(int id) {
    std::lock_guard lock(mutex_);
    cancelled_.insert(id);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [id](auto &j) { return j.id == id; }),
                jobs_.end());
}
std::deque<OfficePdfResult> OfficePdfService::take() {
    std::lock_guard lock(mutex_);
    std::deque<OfficePdfResult> r;
    r.swap(results_);
    return r;
}
OfficePdfResult OfficePdfService::convert(const Job &job) {
    OfficePdfResult result;
    result.id = job.id;
    auto check = [&] { return cancelled(job.id); };
    auto hash = hashFile(job.path, check);
    auto engines = job.engine == OfficeEngine::Auto
                       ? std::vector<OfficeEngine>{OfficeEngine::Word, OfficeEngine::Wps}
                       : std::vector<OfficeEngine>{job.engine};
    std::wstring errors;
    for (auto engine : engines) {
        if (check())
            throw std::runtime_error("Conversion cancelled");
        CLSID id{};
        if (FAILED(CLSIDFromProgID(progId(engine), &id)))
            continue;
        auto cache = root_ / (hash + "-" + std::to_string((int)engine));
        std::filesystem::create_directories(cache);
        auto final = cache / L"document.pdf";
        result.engine = engine == OfficeEngine::Word ? L"Microsoft Word" : L"WPS Office";
        if (!validPdf(final)) {
            auto work =
                cache / (L"work-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(job.id));
            std::filesystem::remove_all(work);
            std::filesystem::create_directory(work);
            struct Cleanup {
                std::filesystem::path path;
                ~Cleanup() {
                    std::error_code ec;
                    std::filesystem::remove_all(path, ec);
                }
            } cleanup{work};
            auto input = work / (L"source" + std::filesystem::path(job.path).extension().wstring());
            std::filesystem::copy_file(job.path, input);
            if (hashFile(input, check) != hash)
                throw std::runtime_error("Document changed during opening; save and reopen it");
            auto output = work / L"document.pdf", exe = executableDirectory() / L"PaperView.exe";
            std::wstring cmd = quote(exe.wstring()) + L" --export-office-pdf " + quote(input.wstring()) +
                               L" " + quote(output.wstring()) + L" " + std::to_wstring((int)engine);
            Handle group(CreateJobObjectW(nullptr, nullptr));
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!group.h ||
                !SetInformationJobObject(group.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                throw std::runtime_error("Cannot isolate PDF compatibility helper");
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
                throw std::runtime_error("Cannot start PDF compatibility helper");
            Handle process(pi.hProcess), thread(pi.hThread);
            if (!AssignProcessToJobObject(group.h, process.h)) {
                TerminateProcess(process.h, 1);
                throw std::runtime_error("Cannot isolate PDF compatibility helper");
            }
            ResumeThread(thread.h);
            auto start = GetTickCount64();
            bool timedOut = false;
            while (WaitForSingleObject(process.h, 100) == WAIT_TIMEOUT)
                if (check() || GetTickCount64() - start > 120000) {
                    TerminateJobObject(group.h, 1);
                    WaitForSingleObject(process.h, 5000);
                    timedOut = true;
                    break;
                }
            if (check())
                throw std::runtime_error("Conversion cancelled");
            DWORD code = 1;
            GetExitCodeProcess(process.h, &code);
            if (timedOut || code || !validPdf(output)) {
                std::ifstream in(work / L"error.txt");
                std::string message((std::istreambuf_iterator<char>(in)), {});
                if (message.size() > 3000)
                    message.resize(3000);
                errors += result.engine + L"：" + (timedOut ? L"操作超时" : utf16(message)) + L"\n";
                continue;
            }
            if (!MoveFileExW(output.c_str(), final.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot save PDF compatibility cache");
        }
        result.pdfPath = final.wstring();
        return result;
    }
    result.error = L"PDF 兼容阅读需要安装桌面版 Microsoft Word 或 "
                   L"WPS。请确认该软件能打开文件，并检查字体、旧公式组件或文档密码。\n" +
                   errors;
    return result;
}
void OfficePdfService::run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (true) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || !jobs_.empty(); });
            if (stop_)
                break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        OfficePdfResult result;
        try {
            result = convert(job);
        } catch (const std::exception &e) {
            result.id = job.id;
            result.error = utf16(e.what());
        }
        if (cancelled(job.id))
            continue;
        {
            std::lock_guard lock(mutex_);
            results_.push_back(std::move(result));
        }
        PostMessageW(window_, ReadyMessage, 0, 0);
    }
    CoUninitialize();
}
} // namespace paper
