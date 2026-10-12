#include "NativeTextures.cpp"
#include <cassert>
#include <iostream>
static bool throws_callback() { throw std::runtime_error("synthetic callback failure"); }
int main() {
    {Guard a;{Guard b;assert(internal);}assert(internal);}assert(!internal);
    assert(!SafeEvent<throws_callback>::call());assert(!enhanced);enhanced=true;
    resource_desc d(16,16,1,1,format::r8g8b8a8_unorm,1,memory_heap::gpu_only,resource_usage::shader_resource);
    std::vector<unsigned char> p(16*16*4,100), padded(16*80,0xee);
    for(int y=0;y<16;++y)std::memcpy(padded.data()+y*80,p.data()+y*64,64);
    textures[10].desc=d;textures[20].desc=d;
    update(nullptr,{p.data(),64,1024},{10},0,nullptr);
    update(nullptr,{padded.data(),80,1280},{20},0,nullptr);
    assert(textures[10].key.size()==64 && textures[10].key==textures[20].key);
    padded[0]=99;update(nullptr,{padded.data(),80,1280},{20},0,nullptr);
    assert(textures[10].key!=textures[20].key);
    std::array<unsigned,37> h{};h[0]=0x20534444;h[1]=124;h[3]=16;h[4]=16;
    h[7]=5;h[19]=32;h[20]=4;h[21]=0x30315844;h[32]=28;h[33]=3;h[35]=1;
    fs::path file=L"test-texture.dds";
    auto write=[&](size_t count){std::ofstream f(file,std::ios::binary);f.write((char*)h.data(),sizeof(h));std::vector<char>b(count,100);f.write(b.data(),b.size());};
    const size_t full=1024+256+64+16+4;
    resource_desc out;std::vector<unsigned char> bytes;std::vector<subresource_data> data;
    write(full);assert(read_dds(file,out,bytes,data)&&data.size()==5);
    h[32]=87;write(full);assert(read_dds(file,out,bytes,data));h[32]=28;
    write(full-1);assert(!read_dds(file,out,bytes,data));
    write(full+1);assert(!read_dds(file,out,bytes,data));
    h[4]=0xffffffff;write(full);assert(!read_dds(file,out,bytes,data));h[4]=16;
    h[35]=6;write(full);assert(!read_dds(file,out,bytes,data));h[35]=1;
    h[34]=4;write(full);assert(!read_dds(file,out,bytes,data));h[34]=0;
    h[7]=1;write(full);assert(!read_dds(file,out,bytes,data));h[7]=5;
    h[32]=71;write(128+32+8+8+8);assert(read_dds(file,out,bytes,data));
    std::cout<<"PASS stable SHA256 across handles and row padding; changed bytes; DDS BGRA/RGBA/BC1; truncated/extra payload; overflow; array/cube; incomplete mip rejection\n";
    fs::remove(file);
}
