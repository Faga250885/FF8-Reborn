#pragma once

namespace menu_locale {
enum class Language {English, Spanish, French, German, Italian, Japanese};
static Language language=Language::English;

static Language from_launcher(const wchar_t* code,bool japanese_engine){
    if(japanese_engine)return Language::Japanese;
    if(!code)return Language::English;
    if(!_wcsicmp(code,L"es"))return Language::Spanish;
    if(!_wcsicmp(code,L"fr"))return Language::French;
    if(!_wcsicmp(code,L"de"))return Language::German;
    if(!_wcsicmp(code,L"it"))return Language::Italian;
    return Language::English;
}

static void initialize(bool japanese_engine){
    int count=0;
    auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    language=from_launcher(args&&count>1?args[1]:nullptr,japanese_engine);
    if(args)LocalFree(args);
}

static const wchar_t* text(const wchar_t* english){
    if(language==Language::English||!*english)return english;
    static constexpr const wchar_t* catalog[][6]={
#include "menu_strings.inc"
    };
    auto item=std::lower_bound(std::begin(catalog),std::end(catalog),english,
        [](const auto& row,const wchar_t* key){return wcscmp(row[0],key)<0;});
    return item!=std::end(catalog)&&!wcscmp((*item)[0],english)?
        (*item)[static_cast<unsigned>(language)]:english;
}

static std::wstring format(const wchar_t* pattern,const std::wstring& value){
    std::wstring result=text(pattern);
    const size_t token=result.find(L"{}");
    if(token!=std::wstring::npos)result.replace(token,2,value);
    return result;
}
}
