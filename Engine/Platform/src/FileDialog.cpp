#include "FileDialog.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <string>

namespace sat {

std::filesystem::path openFolderDialog(void* nativeWindowHandle, const char* title) {
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOwned =
        SUCCEEDED(comHr) || comHr == S_FALSE || comHr == RPC_E_CHANGED_MODE;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        if (comOwned && comHr != RPC_E_CHANGED_MODE) {
            CoUninitialize();
        }
        return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }

    if (title != nullptr) {
        const int wideCount = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        if (wideCount > 0) {
            std::wstring wideTitle(static_cast<std::size_t>(wideCount), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, title, -1, wideTitle.data(), wideCount);
            dialog->SetTitle(wideTitle.c_str());
        }
    }

    HWND owner = nullptr;
    if (nativeWindowHandle != nullptr) {
        owner = glfwGetWin32Window(static_cast<GLFWwindow*>(nativeWindowHandle));
    }

    std::filesystem::path result;
    hr = dialog->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR pathW = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)) &&
                pathW != nullptr) {
                result = pathW;
                CoTaskMemFree(pathW);
            }
            item->Release();
        }
    }

    dialog->Release();
    if (comOwned && comHr != RPC_E_CHANGED_MODE) {
        CoUninitialize();
    }
    return result;
}

} // namespace sat

#else

namespace sat {

std::filesystem::path openFolderDialog(void* /*nativeWindowHandle*/, const char* /*title*/) {
    return {};
}

} // namespace sat

#endif
