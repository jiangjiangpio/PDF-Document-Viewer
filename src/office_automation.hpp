#pragma once
#include "backend.hpp"
#include "settings.hpp"
#include <objidl.h>
#include <oleauto.h>
namespace paper::automation {
// Desktop Office can reject automation while repaginating. Retry briefly on
// the STA thread, keeping its normal COM message pump active.
class MessageFilterScope {
    class Filter final : public IMessageFilter {
        ULONG references_ = 1;
        std::function<bool()> stop_;

      public:
        explicit Filter(std::function<bool()> stop) : stop_(std::move(stop)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (id != IID_IUnknown && id != IID_IMessageFilter)
                return E_NOINTERFACE;
            *out = (IMessageFilter *)this;
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++references_;
        }
        ULONG STDMETHODCALLTYPE Release() override {
            auto n = --references_;
            if (!n)
                delete this;
            return n;
        }
        DWORD STDMETHODCALLTYPE HandleInComingCall(DWORD, HTASK, DWORD, LPINTERFACEINFO) override {
            return SERVERCALL_ISHANDLED;
        }
        DWORD STDMETHODCALLTYPE RetryRejectedCall(HTASK, DWORD elapsed, DWORD rejection) override {
            if ((stop_ && stop_()) || elapsed > 15000)
                return (DWORD)-1;
            return rejection == SERVERCALL_RETRYLATER || rejection == SERVERCALL_REJECTED ? 100 : (DWORD)-1;
        }
        DWORD STDMETHODCALLTYPE MessagePending(HTASK, DWORD, DWORD) override {
            return stop_ && stop_() ? PENDINGMSG_CANCELCALL : PENDINGMSG_WAITDEFPROCESS;
        }
    };
    Filter *filter_ = nullptr;
    IMessageFilter *previous_ = nullptr;

  public:
    explicit MessageFilterScope(std::function<bool()> stop = {}) {
        filter_ = new Filter(std::move(stop));
        if (FAILED(CoRegisterMessageFilter(filter_, &previous_))) {
            filter_->Release();
            filter_ = nullptr;
        }
    }
    ~MessageFilterScope() {
        if (filter_) {
            IMessageFilter *removed = nullptr;
            CoRegisterMessageFilter(previous_, &removed);
            if (removed)
                removed->Release();
            if (previous_)
                previous_->Release();
            filter_->Release();
        }
    }
};
struct Value {
    VARIANT v;
    Value() {
        VariantInit(&v);
    }
    explicit Value(const std::wstring &s) : Value() {
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocStringLen(s.data(), (UINT)s.size());
    }
    explicit Value(int n) : Value() {
        v.vt = VT_I4;
        v.lVal = n;
    }
    static Value boolean(bool b) {
        Value r;
        r.v.vt = VT_BOOL;
        r.v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
        return r;
    }
    static Value missing() {
        Value r;
        r.v.vt = VT_ERROR;
        r.v.scode = DISP_E_PARAMNOTFOUND;
        return r;
    }
    Value(const Value &o) : Value() {
        VariantCopy(&v, const_cast<VARIANT *>(&o.v));
    }
    Value(Value &&o) noexcept : v(o.v) {
        VariantInit(&o.v);
    }
    Value &operator=(Value o) {
        std::swap(v, o.v);
        return *this;
    }
    ~Value() {
        VariantClear(&v);
    }
    IDispatch *object() const {
        if (v.vt != VT_DISPATCH || !v.pdispVal)
            throw std::runtime_error("Office object unavailable");
        return v.pdispVal;
    }
    double number() const {
        Value n;
        if (FAILED(VariantChangeType(&n.v, const_cast<VARIANT *>(&v), 0, VT_R8)))
            throw std::runtime_error("Office numeric value unavailable");
        return n.v.dblVal;
    }
    std::wstring text() const {
        if (v.vt != VT_BSTR)
            return {};
        return std::wstring(v.bstrVal ? v.bstrVal : L"", SysStringLen(v.bstrVal));
    }
};
inline Value call(IDispatch *p, const wchar_t *name, WORD flags, std::vector<Value> values = {}) {
    LPOLESTR n = const_cast<LPWSTR>(name);
    DISPID id;
    HRESULT hr = p->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr))
        throw std::runtime_error("Office operation unavailable: " + utf8(name));
    std::vector<VARIANT> args;
    for (auto i = values.rbegin(); i != values.rend(); ++i)
        args.push_back(i->v);
    DISPID put = DISPID_PROPERTYPUT;
    DISPPARAMS params{args.data(), flags & DISPATCH_PROPERTYPUT ? &put : nullptr, (UINT)args.size(),
                      flags & DISPATCH_PROPERTYPUT ? 1u : 0u};
    Value result;
    EXCEPINFO error{};
    hr = p->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, &result.v, &error, nullptr);
    if (error.pfnDeferredFillIn)
        error.pfnDeferredFillIn(&error);
    std::wstring detail = error.bstrDescription ? error.bstrDescription : L"";
    SysFreeString(error.bstrSource);
    SysFreeString(error.bstrDescription);
    SysFreeString(error.bstrHelpFile);
    if (FAILED(hr))
        throw std::runtime_error(utf8(name) + ": " + utf8(detail));
    return result;
}
inline Value get(IDispatch *p, const wchar_t *name) {
    return call(p, name, DISPATCH_PROPERTYGET);
}
inline void optionalPut(IDispatch *p, const wchar_t *name, Value v) {
    try {
        call(p, name, DISPATCH_PROPERTYPUT, {std::move(v)});
    } catch (...) {
    }
}
inline Value openReadOnly(IDispatch *app, const std::filesystem::path &path) {
    optionalPut(app, L"Visible", Value::boolean(false));
    optionalPut(app, L"DisplayAlerts", Value(0));
    call(app, L"AutomationSecurity", DISPATCH_PROPERTYPUT, {Value(3)});
    try {
        auto options = get(app, L"Options");
        optionalPut(options.object(), L"UpdateLinksAtOpen", Value::boolean(false));
    } catch (...) {
    }
    auto documents = get(app, L"Documents");
    std::vector<Value> args;
    args.emplace_back(path.wstring());
    args.push_back(Value::boolean(false));
    args.push_back(Value::boolean(true));
    args.push_back(Value::boolean(false));
    args.emplace_back(std::wstring());
    args.emplace_back(std::wstring());
    args.push_back(Value::boolean(false));
    args.emplace_back(std::wstring());
    args.emplace_back(std::wstring());
    args.push_back(Value::missing());
    args.push_back(Value::missing());
    args.push_back(Value::boolean(false));
    args.push_back(Value::boolean(false));
    args.push_back(Value::missing());
    args.push_back(Value::boolean(true));
    return call(documents.object(), L"Open", DISPATCH_METHOD, std::move(args));
}
inline Value createApplication(OfficeEngine engine) {
    CLSID id{};
    const wchar_t *name = engine == OfficeEngine::Word ? L"Word.Application" : L"kwps.Application";
    if (FAILED(CLSIDFromProgID(name, &id)))
        throw std::runtime_error("Desktop Microsoft Word or WPS Office is not installed");
    Value app;
    app.v.vt = VT_DISPATCH;
    app.v.pdispVal = nullptr;
    if (FAILED(CoCreateInstance(id, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void **)&app.v.pdispVal)))
        throw std::runtime_error("Cannot start Word / WPS");
    return app;
}
} // namespace paper::automation
