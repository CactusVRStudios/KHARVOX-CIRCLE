#include <windows.h>
#include <filesystem>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr,path,32768)) return 3;
    const auto dll = std::filesystem::path(path).parent_path() / L"KharvoxCircle.dll";
    auto module = LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) { std::cerr << "Cannot load layer DLL: " << GetLastError() << '\n'; return 3; }
    auto run = reinterpret_cast<int(WINAPI*)(unsigned)>(GetProcAddress(module,"CircleRunXrDiagnostic"));
    if (!run) return 4;
    const unsigned seconds = argc > 1 ? static_cast<unsigned>(_wtoi(argv[1])) : 180;
    std::cout << "Stereo diagnostic only; red L for left eye, blue R for right eye.\n";
    const int result = run(seconds);
    std::cout << "Diagnostic exit: " << result << "; game stereo/tracking is not established by this test.\n";
    // Donor worker pins its DLL so no destructors run while the runtime can still call it.
    return result;
}
