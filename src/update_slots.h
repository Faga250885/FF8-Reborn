#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "update_version.h"

namespace update_slots {
constexpr size_t max_size=64u*1024*1024;
struct Identity {
    char magic[24];
    uint32_t build, platform;
};
// Keep an identity in the actual PE, so even a mislabelled release asset
// cannot cross channels or claim a version other than the one it runs.
constexpr uint32_t platform_id(const char* channel){return channel[0]=='d'?2u:1u;}
extern "C" __declspec(dllexport) const Identity FF8RebornUpdateIdentity={
    "FF8-Reborn-Renderer-v1",update_version::build,platform_id(update_version::channel)};

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle):value(handle){}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle(){close();}
    bool valid()const{return value&&value!=INVALID_HANDLE_VALUE;}
    void close(){if(valid())CloseHandle(value);value=INVALID_HANDLE_VALUE;}
};
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline bool valid_hash(const std::string& s){
    return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;
}
struct Paths {
    std::wstring directory,leaf;
    explicit Paths(HMODULE self){
        check(self!=nullptr,"Could not find the mod folder.");
        std::vector<wchar_t> name(32768);
        DWORD n=GetModuleFileNameW(self,name.data(),DWORD(name.size()));
        check(n&&n<name.size(),"Could not find the mod folder.");
        std::wstring path(name.data(),n);
        const auto slash=path.find_last_of(L"\\/");
        check(slash!=std::wstring::npos,"Could not find the mod folder.");
        directory=path.substr(0,slash+1);leaf=path.substr(slash+1);
    }
    std::wstring slot(unsigned i)const{return directory+(i?L"FF8-Reborn-B.dll":L"FF8-Reborn-A.dll");}
    std::wstring receipt()const{return directory+L"FF8-Reborn-update.txt";}
};
struct Receipt {
    unsigned build=0,slot=0;
    std::array<std::string,2> hashes{"-","-"};
    std::string encode(const char* channel)const{
        return std::string("FF8-Reborn-Update-1\n")+channel+"\n"+(slot?"B\n":"A\n")+
            std::to_string(build)+"\n"+hashes[0]+"\n"+hashes[1]+"\n";
    }
};
inline bool decode(const std::string& text,const char* channel,Receipt& result){
    std::array<std::string,6> lines;
    size_t start=0;
    for(auto& line:lines){
        size_t end=text.find('\n',start);
        if(end==std::string::npos)return false;
        line=text.substr(start,end-start);start=end+1;
    }
    if(start!=text.size()||lines[0]!="FF8-Reborn-Update-1"||lines[1]!=channel||
       (lines[2]!="A"&&lines[2]!="B")||lines[3].empty()||lines[3][0]=='0'||
       lines[3].size()>10||lines[3].find_first_not_of("0123456789")!=std::string::npos)return false;
    uint64_t build=0;
    for(char digit:lines[3])build=build*10+unsigned(digit-'0');
    if(!build||build>UINT32_MAX)return false;
    Receipt parsed;
    parsed.build=unsigned(build);parsed.slot=lines[2]=="B";
    for(unsigned i=0;i<2;++i){
        parsed.hashes[i]=lines[4+i];
        if(!valid_hash(parsed.hashes[i])&&(i==parsed.slot||parsed.hashes[i]!="-"))return false;
    }
    result=std::move(parsed);return true;
}
inline bool missing_error(DWORD error){return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;}
inline HANDLE open_read(const std::wstring& path){
    return CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
}
inline size_t file_size(HANDLE file){
    BY_HANDLE_FILE_INFORMATION info{};
    check(GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
          !info.nFileSizeHigh&&info.nFileSizeLow<=max_size,"The update files could not be verified.");
    return info.nFileSizeLow;
}
inline bool read_receipt(const Paths& paths,const char* channel,Receipt& out){
    Handle file(open_read(paths.receipt()));
    if(!file.valid()){
        check(missing_error(GetLastError()),"Cannot read the update record.");
        return false;
    }
    const auto size=file_size(file.value);
    check(size&&size<=256,"The update record is invalid.");
    std::string text(size,'\0');DWORD n=0;
    check(ReadFile(file.value,&text[0],DWORD(size),&n,nullptr)&&n==size&&decode(text,channel,out),"The update record is invalid.");
    return true;
}
inline std::string sha256(const unsigned char* data,size_t size){
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    check(CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT|CRYPT_SILENT)!=FALSE,
        "SHA-256 is unavailable.");
    BYTE digest[32];DWORD count=sizeof(digest);
    const bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,data,DWORD(size),0)&&
        CryptGetHashParam(hash,HP_HASHVAL,digest,&count,0)&&count==sizeof(digest);
    if(hash)CryptDestroyHash(hash);
    CryptReleaseContext(provider,0);
    check(ok,"Could not verify the update checksum.");
    static constexpr char hex[]="0123456789abcdef";
    std::string result(64,'0');
    for(unsigned i=0;i<32;++i){result[2*i]=hex[digest[i]>>4];result[2*i+1]=hex[digest[i]&15];}
    return result;
}
inline bool valid_payload(const unsigned char* data,size_t size,const char* channel,unsigned build){
    if((strcmp(channel,"windows")&&strcmp(channel,"deck"))||size<sizeof(IMAGE_DOS_HEADER)||size>max_size)return false;
    IMAGE_DOS_HEADER dos;memcpy(&dos,data,sizeof(dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||size_t(dos.e_lfanew)>size-sizeof(DWORD)-sizeof(IMAGE_FILE_HEADER))return false;
    const auto* nt=data+dos.e_lfanew;DWORD signature;IMAGE_FILE_HEADER header;
    memcpy(&signature,nt,sizeof(signature));memcpy(&header,nt+sizeof(signature),sizeof(header));
    if(signature!=IMAGE_NT_SIGNATURE||header.Machine!=IMAGE_FILE_MACHINE_I386||!(header.Characteristics&IMAGE_FILE_DLL))return false;
    const Identity expected={"FF8-Reborn-Renderer-v1",build,platform_id(channel)};
    const auto* bytes=reinterpret_cast<const unsigned char*>(&expected);
    return std::search(data,data+size,bytes,bytes+sizeof(expected))!=data+size;
}
// Read through one handle. Keep it open with no write or delete sharing
// until LoadLibrary finishes, including while checking its SHA-256.
inline bool matches(HANDLE file,const std::string& expected,const char* channel=nullptr,unsigned build=0){
    const auto size=file_size(file);
    if(!size||!valid_hash(expected))return false;
    Handle mapping(CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr));
    check(mapping.valid(),"Cannot read the update file.");
    const auto* view=static_cast<const unsigned char*>(MapViewOfFile(mapping.value,FILE_MAP_READ,0,0,size));
    check(view!=nullptr,"Cannot read the update file.");
    struct Unmap {const void* ptr; ~Unmap(){UnmapViewOfFile(ptr);}} unmap{view};
    return (!channel||valid_payload(view,size,channel,build))&&sha256(view,size)==expected;
}
inline bool load_newer(HMODULE self,const char* channel,unsigned current_build) noexcept {
    try{
        Paths paths(self);
        if(_wcsicmp(paths.leaf.c_str(),L"libvgmstream.dll"))return false;
        Receipt record;
        if(!read_receipt(paths,channel,record)||record.build<=current_build)return false;
        const auto path=paths.slot(record.slot);
        Handle file(open_read(path));
        if(!file.valid()||!matches(file.value,record.hashes[record.slot],channel,record.build))return false;
        if(!LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH))return false;
        // Keep the active slot read-only for the lifetime of this process,
        // including when two running copies occupy different slots on Proton.
        static Handle loaded_file(INVALID_HANDLE_VALUE);
        loaded_file.close();loaded_file.value=file.value;file.value=INVALID_HANDLE_VALUE;
        return true;
    }catch(...){return false;}
}
inline bool pending(HMODULE self,const char* channel,unsigned current_build) noexcept {
    try{
        Paths paths(self);Receipt record;
        if(!read_receipt(paths,channel,record)||record.build<=current_build)return false;
        Handle file(open_read(paths.slot(record.slot)));
        return file.valid()&&matches(file.value,record.hashes[record.slot],channel,record.build);
    }catch(...){return false;}
}
inline void write_atomic(const std::wstring& path,const void* data,DWORD length,bool replace){
    static LONG serial=0;
    std::wstring temp;HANDLE created=INVALID_HANDLE_VALUE;
    for(unsigned i=0;i<64&&created==INVALID_HANDLE_VALUE;++i){
        temp=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&serial));
        created=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(created==INVALID_HANDLE_VALUE&&GetLastError()!=ERROR_FILE_EXISTS)break;
    }
    check(created!=INVALID_HANDLE_VALUE,"Cannot write to the game folder.");
    struct Temp {const std::wstring& path;bool committed=false;~Temp(){if(!committed)DeleteFileW(path.c_str());}} cleanup{temp};
    Handle file(created);DWORD written=0;
    check(WriteFile(file.value,data,length,&written,nullptr)&&written==length&&FlushFileBuffers(file.value),"Could not save the update.");
    file.close();
    check(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)),
        "Could not save the update. Close other copies of the game and try again.");
    cleanup.committed=true;
}
inline bool stage(HMODULE self,const char* channel,unsigned current_build,unsigned target_build,
                  const std::string& expected,const std::vector<unsigned char>& payload){
    check(target_build>current_build&&valid_hash(expected)&&valid_payload(payload.data(),payload.size(),channel,target_build)&&
        sha256(payload.data(),payload.size())==expected,"The downloaded update did not pass verification.");
    Paths paths(self);
    Handle mutex(CreateMutexW(nullptr,FALSE,L"Local\\FF8RemasteredHorizonteInstaller"));
    const DWORD lock=mutex.valid()?WaitForSingleObject(mutex.value,0):WAIT_FAILED;
    check(lock==WAIT_OBJECT_0||lock==WAIT_ABANDONED,"Another update is in progress.");
    struct Unlock {HANDLE mutex;~Unlock(){ReleaseMutex(mutex);}} unlock{mutex.value};
    Receipt record;const bool has_receipt=read_receipt(paths,channel,record);
    check(!has_receipt||target_build>=record.build,"A newer update is already queued.");
    if(has_receipt&&target_build==record.build){
        Handle selected(open_read(paths.slot(record.slot)));
        if(selected.valid()&&matches(selected.value,expected,channel,target_build)&&record.hashes[record.slot]==expected)return true;
    }
    unsigned slot=has_receipt?1-record.slot:0;
    const int running=(!_wcsicmp(paths.leaf.c_str(),L"FF8-Reborn-A.dll"))?0:
        (!_wcsicmp(paths.leaf.c_str(),L"FF8-Reborn-B.dll"))?1:-1;
    if(int(slot)==running){
        check(!has_receipt,"Restart the game to activate the queued update first.");
        slot=1-slot;
    }
    bool prepared=false;
    for(unsigned attempt=0;attempt<2;++attempt,slot=1-slot){
        if(int(slot)==running||(has_receipt&&slot==record.slot))continue;
        const auto path=paths.slot(slot);
        Handle existing(open_read(path));
        if(!existing.valid()&&!missing_error(GetLastError()))continue;
        const bool exists=existing.valid();
        if(exists){
            bool ours=false,already_saved=false;
            try {
                already_saved=matches(existing.value,expected,channel,target_build);
                ours=already_saved||(record.hashes[slot]!="-"&&matches(existing.value,record.hashes[slot]));
            }catch(const std::exception&){continue;}
            if(!ours)continue;
            if(already_saved){prepared=true;break;}
        }
        existing.close();
        write_atomic(path,payload.data(),DWORD(payload.size()),exists);
        prepared=true;break;
    }
    check(prepared,"No writable update slot is available.");
    // The prior selection survives a partial write. A retry can adopt an
    // already verified payload if it stopped just before the receipt rename.
    record.slot=slot;record.build=target_build;record.hashes[slot]=expected;
    const auto text=record.encode(channel);
    write_atomic(paths.receipt(),text.data(),DWORD(text.size()),has_receipt);
    return true;
}
}
