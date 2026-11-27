#pragma once
#include "backend.hpp"
#include <wincodec.h>
#include <wrl/client.h>

namespace paper {
inline bool isImageDocument(const std::filesystem::path &path) {
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
    static const std::set<std::wstring> extensions{L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff", L".webp"};
    return extensions.count(ext) != 0;
}
class ImageBackend {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Document {
        Ptr<IWICBitmapDecoder> decoder;
        std::vector<Size> sizes;
    };
    Ptr<IWICImagingFactory> factory_;
    std::map<int, Document> images_;
    static void require(HRESULT result) {
        if (FAILED(result)) throw PdfError("Windows image decoder could not read this image or codec.");
    }
public:
    bool contains(int id) const { return images_.count(id) != 0; }
    void close(int id) { images_.erase(id); }
    DocumentInfo open(int id, const std::wstring &path, const std::function<bool()> &cancel) {
        if (!factory_) require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory_)));
        Document doc;
        require(factory_->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&doc.decoder));
        UINT count = 0;
        require(doc.decoder->GetFrameCount(&count));
        GUID format{};
        require(doc.decoder->GetContainerFormat(&format));
        if (format != GUID_ContainerFormatTiff) count = std::min(count, 1u);
        if (count == 0 || count > 20000) throw PdfError("Unsupported image page count");
        for (UINT page = 0; page < count; ++page) {
            if (cancel()) throw PdfError("Opening cancelled");
            Ptr<IWICBitmapFrameDecode> frame;
            require(doc.decoder->GetFrame(page,&frame));
            UINT w=0, h=0;
            require(frame->GetSize(&w,&h));
            if (!w || !h || w > 100000 || h > 100000 || uint64_t(w)*h > 160000000)
                throw PdfError("Unsupported image dimensions");
            doc.sizes.push_back({double(w),double(h)});
        }
        DocumentInfo info;
        info.id=id; info.path=path; info.sizes=doc.sizes;
        info.fingerprint=hashFile(path,cancel);
        images_[id]=std::move(doc);
        return info;
    }
    std::shared_ptr<Tile> render(TileKey key, const std::function<bool()> &cancel) {
        auto &doc=images_.at(key.document);
        if (key.page<0 || key.page >= int(doc.sizes.size()) || cancel()) return {};
        Ptr<IWICBitmapFrameDecode> frame;
        require(doc.decoder->GetFrame(key.page,&frame));
        Ptr<IWICBitmapSource> source;
        require(frame.As(&source));
        if (key.rotation) {
            Ptr<IWICBitmapFlipRotator> rotation;
            require(factory_->CreateBitmapFlipRotator(&rotation));
            require(rotation->Initialize(source.Get(), static_cast<WICBitmapTransformOptions>(key.rotation)));
            require(rotation.As(&source));
        }
        UINT w,h;
        require(source->GetSize(&w,&h));
        double scale=key.scale<0 ? std::min(2., (key.scale == -1 ? 720. : double(-key.scale))/std::max(w,h)) : key.scale/64.;
        UINT fullW=std::max(1u,UINT(std::ceil(w*scale))), fullH=std::max(1u,UINT(std::ceil(h*scale)));
        int x=key.scale<0 ? 0 : key.x*512, y=key.scale<0 ? 0 : key.y*512;
        if (x<0 || y<0 || x>=int(fullW) || y>=int(fullH)) return {};
        int tw=key.scale<0 ? fullW : std::min(512,int(fullW)-x), th=key.scale<0 ? fullH : std::min(512,int(fullH)-y);
        Ptr<IWICBitmapScaler> scaler;
        require(factory_->CreateBitmapScaler(&scaler));
        require(scaler->Initialize(source.Get(),fullW,fullH, WICBitmapInterpolationModeFant));
        Ptr<IWICFormatConverter> converter;
        require(factory_->CreateFormatConverter(&converter));
        require(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        auto tile=std::make_shared<Tile>();
        tile->width=tw; tile->height=th; tile->pixels.resize(size_t(tw)*th*4);
        WICRect crop{x,y,tw,th};
        require(converter->CopyPixels(&crop,tw*4,UINT(tile->pixels.size()),tile->pixels.data()));
        return cancel() ? nullptr : tile;
    }
};
} // namespace paper
