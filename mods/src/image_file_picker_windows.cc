#if _WIN32
#include "settings/image_file_picker.h"
#include <future>
#include <thread>
#include <utility>
#include <Windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <filesystem>
#include <stdexcept>

std::future<std::string> OpenImageFilePicker()
{
  std::packaged_task<std::string()> task([]() -> std::string {
    struct Apartment {
      HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
      ~Apartment()
      {
        if (SUCCEEDED(result))
          CoUninitialize();
      }
    } apartment;
    auto check = [](HRESULT result) {
      if (FAILED(result))
        throw std::runtime_error("Cannot open the file picker");
    };
    check(apartment.result);
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    check(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
    DWORD options = 0;
    check(dialog->GetOptions(&options));
    check(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR));
    const COMDLG_FILTERSPEC filter{L"Images (*.png; *.jpg; *.jpeg)", L"*.png;*.jpg;*.jpeg"};
    check(dialog->SetFileTypes(1, &filter));
    check(dialog->SetTitle(L"Choose a loading image"));
    // Unowned: a modal dialog owned by the Unity window would disable game input.
    const auto result = dialog->Show(nullptr);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
      return {};
    check(result);
    Microsoft::WRL::ComPtr<IShellItem> item;
    check(dialog->GetResult(&item));
    PWSTR raw = nullptr;
    check(item->GetDisplayName(SIGDN_FILESYSPATH, &raw));
    struct PathMemory {
      PWSTR value;
      ~PathMemory()
      { CoTaskMemFree(value); }
    } memory{raw};
    const auto utf8 = std::filesystem::path(raw).u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
  });
  auto result = task.get_future();
  std::thread(std::move(task)).detach();
  return result;
}
#endif
