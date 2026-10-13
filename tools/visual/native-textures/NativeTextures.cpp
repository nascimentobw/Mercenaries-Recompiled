// Local Mercenaries D3D11 integration. SPDX-License-Identifier: MIT
// Uses the official ReShade 6.8 add-on API; never changes guest allocations.
#define NOMINMAX
#include <reshade.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <type_traits>
using namespace reshade::api;
namespace fs=std::filesystem;
static thread_local bool internal=false;
struct Guard { bool previous=internal; Guard(){internal=true;} ~Guard(){internal=previous;} };
struct Texture {
    resource_desc desc{}; std::string key;
    std::vector<std::vector<unsigned char>> mips;
    resource replacement{}; resource_view replacement_view{};
    bool attempted=false, dumped=false;
    size_t replacement_bytes=0;
};
static std::unordered_map<uint64_t,Texture> textures;
static std::unordered_map<uint64_t,uint64_t> views;
static std::recursive_mutex mutex;
static fs::path replacements, dumps, log_path;
static std::ofstream log_file;
static size_t cpu_bytes=0;
static size_t gpu_bytes=0;
static device *owner=nullptr; // One D3D11 game device; ignore other/private devices.
static uint64_t frame_count=0, binds=0, replacements_loaded=0;
static ID3D11Texture2D *last_depth=nullptr;
static bool depth_sampled=false;
static auto last_present=std::chrono::steady_clock::now();
static std::vector<double> frame_times;
static std::atomic<bool> enhanced{true};
static bool key_was_down=false;
static fs::file_time_type reload_time{};
static fs::path environment(const wchar_t *name) {
    wchar_t value[32768]{}; DWORD n=GetEnvironmentVariableW(name,value,32768);
    return n && n<32768 ? fs::path(value):fs::path();
}
static void log(const std::string &s) {
    if(log_file){log_file<<s<<'\n';log_file.flush();}
}
static bool supported(format f) {
    unsigned n=static_cast<unsigned>(f);
    return n==28 || n==29 || n==87 || n==91 ||
        (n>=71 && n<=78 && n!=73 && n!=76) || n==80 || n==81 ||
        n==83 || n==84 || n==95 || n==96 || n==98 || n==99;
}
static size_t row_size(format f,unsigned width) {
    unsigned n=static_cast<unsigned>(f);
    if(n==28||n==29||n==87||n==91)return size_t(width)*4;
    return size_t((width+3)/4)*((n==71||n==72||n==80||n==81)?8:16);
}
static unsigned rows(format f,unsigned height) {
    unsigned n=static_cast<unsigned>(f);
    return n==28||n==29||n==87||n==91?height:(height+3)/4;
}
static bool color_format(format f) {
    unsigned n=(unsigned)f;return n==28||n==29||n==87||n==91||n==71||n==72||n==74||n==75||n==77||n==78||n==98||n==99;
}
static bool srgb(format f) {unsigned n=(unsigned)f;return n==29||n==91||n==72||n==75||n==78||n==99;}
static std::string hash(const resource_desc &d,const std::vector<unsigned char> &bytes) {
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE h=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    if(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);return {};}
    // v1: dimensions and normalized DXGI format followed by tightly packed mip0.
    // No address, allocation order, padding, path or frame number enters the key.
    std::array<unsigned,4> meta={1,d.texture.width,d.texture.height,(unsigned)d.texture.format};
    bool ok=BCryptHashData(h,(PUCHAR)meta.data(),sizeof(meta),0)>=0 &&
        BCryptHashData(h,(PUCHAR)bytes.data(),(ULONG)bytes.size(),0)>=0;
    std::array<unsigned char,32> digest{};
    ok=ok && BCryptFinishHash(h,digest.data(),32,0)>=0;
    BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);
    if(!ok)return {};
    const char *hex="0123456789abcdef"; std::string s; s.reserve(64);
    for(auto b:digest){s.push_back(hex[b>>4]);s.push_back(hex[b&15]);}return s;
}
static void release(device *dev,Texture &t) {
    Guard guard;
    if(t.replacement_view.handle)dev->destroy_resource_view(t.replacement_view);
    if(t.replacement.handle)dev->destroy_resource(t.replacement);
    t.replacement={};t.replacement_view={};
    gpu_bytes-=t.replacement_bytes;t.replacement_bytes=0;
    for(auto &m:t.mips)cpu_bytes-=m.size();t.mips.clear();
}
static bool eligible(const resource_desc &d) {
    return d.type==resource_type::texture_2d && d.texture.width>=16 &&
        d.texture.height>=16 && d.texture.width<=4096 && d.texture.height<=4096 &&
        d.texture.depth_or_layers==1 && d.texture.samples==1 && d.texture.levels>0 && d.texture.levels<=16 &&
        supported(d.texture.format) &&
        (d.usage&resource_usage::shader_resource)!=resource_usage::undefined &&
        (d.usage&(resource_usage::render_target|resource_usage::depth_stencil|resource_usage::unordered_access))==resource_usage::undefined &&
        (d.flags&resource_flags::dynamic)==resource_flags::none;
}
static void dump(Texture &t) {
    if(dumps.empty()||t.dumped||t.key.empty()||t.mips.empty())return;
    for(auto &m:t.mips)if(m.empty())return;
    fs::path file=dumps/(t.key+".dds");
    if(!fs::exists(file)) {
        std::array<unsigned,37> h{};
        unsigned f=(unsigned)t.desc.texture.format;
        bool uncompressed=f==28||f==29||f==87||f==91;
        h[0]=0x20534444;h[1]=124;h[2]=0x00021007|(uncompressed?8:0x80000);
        h[3]=t.desc.texture.height;h[4]=t.desc.texture.width;
        h[5]=uncompressed?(unsigned)row_size(t.desc.texture.format,t.desc.texture.width):(unsigned)t.mips[0].size();
        h[7]=t.desc.texture.levels;h[19]=32;h[20]=4;h[21]=0x30315844;
        h[27]=0x401008;h[32]=(unsigned)t.desc.texture.format;h[33]=3;h[35]=1;
        fs::path temp=file;temp+=".tmp";
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        out.write((const char*)h.data(),sizeof(h));
        for(auto &m:t.mips)out.write((const char*)m.data(),m.size());out.close();
        if(out){fs::rename(temp,file);log("DUMP "+t.key+" "+std::to_string(t.desc.texture.width)+"x"+std::to_string(t.desc.texture.height)+" mips="+std::to_string(t.desc.texture.levels));}
    }
    t.dumped=true;for(auto &m:t.mips)cpu_bytes-=m.size();t.mips.clear();
}
static bool read_dds(const fs::path &path,resource_desc &desc,std::vector<unsigned char> &bytes,std::vector<subresource_data> &data) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return false;
    auto size=f.tellg();if(size<128||size>128*1024*1024)return false;
    bytes.resize((size_t)size);f.seekg(0);if(!f.read((char*)bytes.data(),size))return false;
    auto u=[&](size_t p){unsigned v;std::memcpy(&v,bytes.data()+p,4);return v;};
    if(u(0)!=0x20534444||u(4)!=124||u(76)!=32)return false;
    unsigned width=u(16),height=u(12),levels=std::max(1u,u(28)),fmt=0;size_t offset=128;
    if(width==0||height==0||width>8192||height>8192||levels>14||u(112)!=0)return false;
    if(u(84)==0x30315844) {
        if(bytes.size()<148||u(132)!=3||u(136)!=0||u(140)!=1)return false;
        fmt=u(128);offset=148;
    } else if(u(84)==0x31545844)fmt=71;
    else if(u(84)==0x33545844)fmt=74;
    else if(u(84)==0x35545844)fmt=77;
    else if(u(88)==32&&u(92)==0x00ff0000&&u(96)==0x0000ff00&&u(100)==0x000000ff&&u(104)==0xff000000)fmt=87;
    else if(u(88)==32&&u(92)==0x000000ff&&u(96)==0x0000ff00&&u(100)==0x00ff0000&&u(104)==0xff000000)fmt=28;
    else return false;
    if(!supported((format)fmt))return false;
    unsigned max_levels=1;for(unsigned n=std::max(width,height);n>1;n>>=1)++max_levels;
    // Replacements must include the complete chain; avoids distant shimmer.
    if(levels!=max_levels)return false;
    desc=resource_desc(width,height,1,(uint16_t)levels,(format)fmt,1,memory_heap::gpu_only,resource_usage::shader_resource|resource_usage::copy_dest);
    data.clear();unsigned w=width,h=height;
    for(unsigned i=0;i<levels;++i) {
        size_t pitch=row_size((format)fmt,w),count=pitch*rows((format)fmt,h);
        if(count>bytes.size()-offset)return false;
        data.push_back({bytes.data()+offset,(unsigned)pitch,(unsigned)count});offset+=count;
        w=std::max(1u,w>>1);h=std::max(1u,h>>1);
    }
    return offset==bytes.size();
}
static void load(device *dev,Texture &t) {
    if(t.attempted||replacements.empty()||t.key.empty())return;t.attempted=true;
    fs::path path=replacements/(t.key+".dds");if(!fs::exists(path))return;
    resource_desc desc;std::vector<unsigned char> bytes;std::vector<subresource_data> data;
    if(!read_dds(path,desc,bytes,data)){log("REJECT malformed/unsupported/incomplete-mips "+path.string());return;}
    if(desc.texture.format!=t.desc.texture.format &&
       !(color_format(desc.texture.format)&&color_format(t.desc.texture.format)&&srgb(desc.texture.format)==srgb(t.desc.texture.format))) {
        log("REJECT incompatible format/color interpretation "+t.key);return;
    }
    size_t allocation=0;for(auto &m:data)allocation+=m.slice_pitch;
    if(allocation>512*1024*1024-gpu_bytes){log("REJECT replacement GPU budget "+t.key);return;}
    // Preserve aspect and forbid smaller replacements.
    if(uint64_t(desc.texture.width)*t.desc.texture.height!=uint64_t(t.desc.texture.width)*desc.texture.height||desc.texture.width<t.desc.texture.width){log("REJECT aspect/size "+t.key);return;}
    Guard guard;
    if(!dev->create_resource(desc,data.data(),resource_usage::shader_resource,&t.replacement)){log("REJECT GPU allocation "+t.key);return;}
    resource_view_desc view(desc.texture.format,0,UINT32_MAX,0,1);
    view.type=resource_view_type::texture_2d;
    if(!dev->create_resource_view(t.replacement,resource_usage::shader_resource,view,&t.replacement_view)){dev->destroy_resource(t.replacement);t.replacement={};return;}
    t.replacement_bytes=allocation;gpu_bytes+=allocation;
    ++replacements_loaded;log("REPLACEMENT "+t.key+" "+std::to_string(desc.texture.width)+"x"+std::to_string(desc.texture.height)+" mips="+std::to_string(desc.texture.levels));
}
static bool update(device *dev,const subresource_data &data,resource res,unsigned sub,const subresource_box *box) {
    if(internal||!data.data)return false;std::lock_guard<std::recursive_mutex> lock(mutex);
    if(dev!=owner)return false;
    auto found=textures.find(res.handle);if(found==textures.end())return false;auto &t=found->second;
    if(sub>=t.desc.texture.levels)return false;
    unsigned w=std::max(1u,t.desc.texture.width>>sub),h=std::max(1u,t.desc.texture.height>>sub);
    if(box&&(box->left||box->top||box->front||box->right!=w||box->bottom!=h||box->back!=1)) {
        // A partial update invalidates a replacement identified from old pixels.
        release(dev,t);textures.erase(found);return false;
    }
    size_t pitch=row_size(t.desc.texture.format,w),nrows=rows(t.desc.texture.format,h),size=pitch*nrows;
    if(data.row_pitch<pitch||size>64*1024*1024)return false;
    std::vector<unsigned char> pixels(size);
    for(size_t y=0;y<nrows;++y)std::memcpy(pixels.data()+pitch*y,(const char*)data.data+data.row_pitch*y,pitch);
    if(sub==0) {
        std::string key=hash(t.desc,pixels);if(key.empty())return false;
        if(key!=t.key){release(dev,t);t.key=key;t.attempted=false;t.dumped=false;}
        load(dev,t);
    }
    if(!dumps.empty()&&!t.dumped&&cpu_bytes+size<=128*1024*1024) {
        if(t.mips.empty())t.mips.resize(t.desc.texture.levels);
        cpu_bytes-=t.mips[sub].size();t.mips[sub]=std::move(pixels);cpu_bytes+=size;dump(t);
    }
    return false; // Always retain the application's original upload and resource.
}
static void init_resource(device *dev,const resource_desc &desc,const subresource_data *data,resource_usage,resource res) {
    if(internal||dev->get_api()!=device_api::d3d11)return;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!owner)owner=dev;if(dev!=owner)return;
    if((desc.usage&resource_usage::depth_stencil)!=resource_usage::undefined)
        log("DEPTH resource="+std::to_string(res.handle)+" "+std::to_string(desc.texture.width)+"x"+std::to_string(desc.texture.height)+" format="+std::to_string((unsigned)desc.texture.format));
    if(!eligible(desc)||textures.size()>=8192)return;
    auto &t=textures[res.handle];t.desc=desc;
    if(data)for(unsigned i=0;i<desc.texture.levels;++i)update(dev,data[i],res,i,nullptr);
}
static void destroy_resource(device *dev,resource res) {
    if(internal)return;std::lock_guard<std::recursive_mutex> lock(mutex);
    if(dev!=owner)return;
    if(last_depth==(ID3D11Texture2D*)res.handle)last_depth=nullptr;
    auto f=textures.find(res.handle);if(f!=textures.end()){release(dev,f->second);textures.erase(f);}
}
static void init_view(device *dev,resource res,resource_usage usage,const resource_view_desc &desc,resource_view view) {
    if(internal||usage!=resource_usage::shader_resource||desc.type!=resource_view_type::texture_2d||desc.texture.first_level!=0)return;
    std::lock_guard<std::recursive_mutex> lock(mutex);if(dev!=owner)return;
    auto t=textures.find(res.handle);if(t==textures.end())return;
    // A partial mip/layer range or reinterpreted SRV is not interchangeable.
    if(desc.texture.first_layer!=0||(desc.texture.layers!=1&&desc.texture.layers!=UINT32_MAX)||
       (desc.texture.levels!=UINT32_MAX&&desc.texture.levels!=t->second.desc.texture.levels)||
       desc.format!=t->second.desc.texture.format)return;
    views[view.handle]=res.handle;
}
static void destroy_view(device *dev,resource_view view) {
    if(internal)return;std::lock_guard<std::recursive_mutex> lock(mutex);if(dev==owner)views.erase(view.handle);
}
static void push(command_list *cmd,shader_stage stage,pipeline_layout layout,unsigned param,const descriptor_table_update &u) {
    if(internal||!enhanced||u.type!=descriptor_type::shader_resource_view||u.count==0||u.count>128)return;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(cmd->get_device()!=owner||!u.descriptors)return;
    auto src=(const resource_view*)u.descriptors;std::vector<resource_view> corrected(src,src+u.count);bool changed=false;
    for(unsigned i=0;i<u.count;++i) {
        auto v=views.find(src[i].handle);if(v==views.end())continue;auto t=textures.find(v->second);
        if(t!=textures.end()&&t->second.replacement_view.handle){corrected[i]=t->second.replacement_view;changed=true;++binds;}
    }
    if(changed){Guard guard;auto n=u;n.descriptors=corrected.data();cmd->push_descriptors(stage,layout,param,n);}
}
static void bind_targets(command_list *cmd,unsigned,const resource_view *,resource_view dsv) {
    if(internal||!dsv.handle||cmd->get_device()->get_api()!=device_api::d3d11)return;
    std::lock_guard<std::recursive_mutex> lock(mutex);if(cmd->get_device()!=owner)return;
    resource res=cmd->get_device()->get_resource_from_view(dsv);if(!res.handle)return;
    auto *tex=(ID3D11Texture2D*)res.handle;D3D11_TEXTURE2D_DESC desc{};tex->GetDesc(&desc);
    if(desc.Width<640||desc.Height<480||desc.SampleDesc.Count!=1)return;
    // Borrow only until destroy_resource; holding an extra device-child reference
    // here would prevent D3D11 teardown and the destroy_device notification.
    last_depth=tex;
}
static void sample_depth(ID3D11Device *dev) {
    if(!last_depth||depth_sampled||environment(L"MERC_NATIVE_CAPTURE_DEPTH").empty())return;
    D3D11_TEXTURE2D_DESC d{};last_depth->GetDesc(&d);
    if(d.Format!=DXGI_FORMAT_R24G8_TYPELESS&&d.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT)return;
    d.BindFlags=0;d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *stage=nullptr;ID3D11DeviceContext *ctx=nullptr;
    if(FAILED(dev->CreateTexture2D(&d,nullptr,&stage)))return;dev->GetImmediateContext(&ctx);
    ctx->CopyResource(stage,last_depth);D3D11_MAPPED_SUBRESOURCE map{};
    if(SUCCEEDED(ctx->Map(stage,0,D3D11_MAP_READ,0,&map))) {
        unsigned min=0xffffff,max=0;size_t valid=0,total=0;
        for(unsigned y=0;y<d.Height;y+=4)for(unsigned x=0;x<d.Width;x+=4) {
            auto row=(const unsigned*)((const char*)map.pData+y*map.RowPitch);unsigned z=row[x]&0xffffff;
            min=std::min(min,z);max=std::max(max,z);valid+=(z>0&&z<0xffffff);++total;
        }
        log("DEPTH_SAMPLE min="+std::to_string(min)+" max="+std::to_string(max)+" nonclear="+std::to_string(valid)+" total="+std::to_string(total));
        ctx->Unmap(stage,0);depth_sampled=valid>0;
    }ctx->Release();stage->Release();
}
static void present(command_queue *queue,swapchain *,const rect *,const rect *,unsigned,const rect *) {
    std::lock_guard<std::recursive_mutex> lock(mutex);if(queue->get_device()!=owner)return;
    ++frame_count;auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double,std::milli>(now-last_present).count();last_present=now;
    if(frame_count%30==0 && !replacements.empty()) {
        std::error_code ec;auto signal=replacements/L"texture-mode.txt";
        auto stamp=fs::last_write_time(signal,ec);
        if(!ec && stamp!=reload_time) {
            reload_time=stamp;std::ifstream in(signal);std::string mode;in>>mode;
            enhanced=mode!="original";
            if(mode=="reload"||mode=="enhanced") {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                for(auto &p:textures)if(!p.second.replacement.handle){p.second.attempted=false;load(queue->get_device(),p.second);}
            }
            log(std::string("TEXTURE_MODE ")+(enhanced?"enhanced":"original"));
        }
    }
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    bool down=pid==GetCurrentProcessId()&&(GetAsyncKeyState(VK_F4)&0x8000)!=0;
    if(down&&!key_was_down){enhanced=!enhanced;log(std::string("TEXTURE_MODE key=F4 ")+(enhanced?"enhanced":"original"));}
    key_was_down=down;
    if(frame_count>120&&frame_times.size()<3600)frame_times.push_back(dt);
    if(frame_count%600==0 && frame_count<=3600)sample_depth((ID3D11Device*)queue->get_device()->get_native());
    if(frame_count%600==0) {
        double sum=0;for(double t:frame_times)sum+=t;
        auto sorted=frame_times;std::sort(sorted.begin(),sorted.end());
        log("PRESENT frames="+std::to_string(frame_count)+" replacements="+std::to_string(replacements_loaded)+" replacement_binds="+std::to_string(binds)+" textures="+std::to_string(textures.size())+
            (sorted.empty()?"":" avg_fps="+std::to_string(1000*sorted.size()/sum)+" p99_ms="+std::to_string(sorted[(sorted.size()-1)*99/100])));
    }
}
static void init_device(device *dev) {
    if(dev->get_api()!=device_api::d3d11)return;
    std::lock_guard<std::recursive_mutex> lock(mutex);if(owner)return;owner=dev;
    replacements=environment(L"MERC_NATIVE_TEXTURE_ROOT");dumps=environment(L"MERC_NATIVE_DUMP_ROOT");log_path=environment(L"MERC_NATIVE_LOG");
    if(!dumps.empty())fs::create_directories(dumps);
    if(!log_path.empty()) {
        if(!log_path.parent_path().empty())fs::create_directories(log_path.parent_path());
        if(log_file.is_open())log_file.close();log_file.open(log_path,std::ios::app);
    }
    frame_count=0;binds=0;replacements_loaded=0;last_present=std::chrono::steady_clock::now();
    log("NativeTextures v1.1.0-prototype D3D11; SHA256-v1; original resources preserved; complete DDS mip chain required");
    log("CAPABILITY: DDS replacement only; no NGX/Streamline calls, motion vectors, jitter, exposure or hudless color supplied by this addon.");
}
static void shutdown_device(device *dev) {
    if(dev->get_api()!=device_api::d3d11)return;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(dev!=owner)return;
    if(frame_times.size()>30) {
        double sum=0;for(double t:frame_times)sum+=t;
        auto sorted=frame_times;std::sort(sorted.begin(),sorted.end());
        log("FRAME_TIMES n="+std::to_string(frame_times.size())+" avg_fps="+std::to_string(1000*frame_times.size()/sum)+" p99_ms="+std::to_string(sorted[(sorted.size()-1)*99/100])+" scope=present_including_menus_not_campaign_benchmark");
    }
    for(auto &t:textures)release(dev,t.second);textures.clear();views.clear();
    last_depth=nullptr;frame_times.clear();owner=nullptr;depth_sampled=false;log("CLEAN_SHUTDOWN");
}
static void destroy_swapchain(swapchain *chain,bool) {
    std::lock_guard<std::recursive_mutex> lock(mutex);if(chain->get_device()==owner)last_depth=nullptr;
}
// Never allow filesystem/allocation exceptions to cross the host callback ABI.
// Any failure disables substitution and lets the original upload/bind continue.
template<auto Callback> struct SafeEvent;
template<typename R,typename... A,R(*Callback)(A...)> struct SafeEvent<Callback> {
    static R call(A... args) noexcept {
        try { return Callback(args...); }
        catch(...) {
            enhanced=false;
            OutputDebugStringA("NativeTextures callback failed; replacements disabled.\n");
            if constexpr(!std::is_void_v<R>)return R{};
        }
    }
};
extern "C" __declspec(dllexport) const char *NAME="Mercenaries Native Textures";
extern "C" __declspec(dllexport) const char *DESCRIPTION="Local DDS texture dump/replacement by SHA256; D3D11 depth and frame diagnostics. No DLSS claims.";
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        if(!reshade::register_addon(module))return FALSE;
        // File I/O and environment setup run outside the Windows loader lock.
        reshade::register_event<reshade::addon_event::init_device>(SafeEvent<init_device>::call);
        reshade::register_event<reshade::addon_event::init_resource>(SafeEvent<init_resource>::call);
        reshade::register_event<reshade::addon_event::destroy_resource>(SafeEvent<destroy_resource>::call);
        reshade::register_event<reshade::addon_event::update_texture_region>(SafeEvent<update>::call);
        reshade::register_event<reshade::addon_event::init_resource_view>(SafeEvent<init_view>::call);
        reshade::register_event<reshade::addon_event::destroy_resource_view>(SafeEvent<destroy_view>::call);
        reshade::register_event<reshade::addon_event::push_descriptors>(SafeEvent<push>::call);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(SafeEvent<bind_targets>::call);
        reshade::register_event<reshade::addon_event::present>(SafeEvent<present>::call);
        reshade::register_event<reshade::addon_event::destroy_device>(SafeEvent<shutdown_device>::call);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(SafeEvent<destroy_swapchain>::call);
    }else if(reason==DLL_PROCESS_DETACH){reshade::unregister_addon(module);}
    return TRUE;
}
