#include <napi.h>
#include <windows.h>
#include <string>
#include <iostream>
//h~andle
Napi::Value InjectDll(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (info.Length() < 2)
    {
        Napi::TypeError::New(env, "Missing Args ~ 2 </3").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    uint32_t processId = info[0].As<Napi::Number>().Uint32Value();
    std::string dllPath = info[1].As<Napi::String>().Utf8Value();

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess)
    {
        Napi::Error::New(env, "Failed to open process").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // this is required (if the project trying to use it doesn't wait for it before injecting)
    if (WaitForInputIdle(hProcess, INFINITE) != 0)
    {
        Napi::Error::New(env, "Failed to wait for input").ThrowAsJavaScriptException();
        CloseHandle(hProcess);
        return env.Undefined();
    }

    LPVOID DllPath = VirtualAllocEx(hProcess, NULL, dllPath.length() + 1, MEM_COMMIT, PAGE_READWRITE);
    if (!DllPath)
    {
        Napi::Error::New(env, "Failed to allocate memory in target process").ThrowAsJavaScriptException();
        CloseHandle(hProcess);
        return env.Undefined();
    }

    WriteProcessMemory(hProcess, DllPath, dllPath.c_str(), dllPath.length() + 1, NULL);
    
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) {
        Napi::Error::New(env, "Failed to get handle for kernel32.dll").ThrowAsJavaScriptException();
        return env.Null();
    }

    
    FARPROC loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryA");
    if (!loadLibraryAddr)
    {
        Napi::Error::New(env, "Failed to get proc addr for LoadLibraryA").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibraryAddr, DllPath, 0, NULL);
    if (!hThread)
    {
        Napi::Error::New(info.Env(), "Failed to create remote thread").ThrowAsJavaScriptException();
        CloseHandle(hProcess);
        return env.Undefined();
    }

    WaitForSingleObject(hThread, INFINITE);
    VirtualFreeEx(hProcess, DllPath, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return Napi::String::New(info.Env(), "Injected DLL!");
}

extern "C"
{
    typedef NTSTATUS(NTAPI *pNtSuspendProcess)(HANDLE ProcessHandle);
}

Napi::Value FreezeProcess(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 1)
    {
        Napi::TypeError::New(env, "Missing Args </3").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint32_t processId = info[0].As<Napi::Number>().Uint32Value();

    HMODULE hNtDll = LoadLibraryA("ntdll.dll");
    if (!hNtDll)
    {
        Napi::Error::New(env, "Failed to load ntdll.dll").ThrowAsJavaScriptException();
        return env.Null();
    }

    pNtSuspendProcess NtSuspendProcess = (pNtSuspendProcess)GetProcAddress(hNtDll, "NtSuspendProcess");
    if (!NtSuspendProcess)
    {
        Napi::Error::New(env, "Failed to get NtSuspendProcess from function proc addr").ThrowAsJavaScriptException();
        return env.Null();
    }

    HANDLE hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, processId);
    if (!hProcess)
    {
        Napi::Error::New(env, "Failed to open process").ThrowAsJavaScriptException();
        return env.Null();
    }

    NTSTATUS Status = NtSuspendProcess(hProcess);
    if (Status != 0)
    {
        Napi::Error::New(env, "Failed to suspend the process").ThrowAsJavaScriptException();
        CloseHandle(hProcess);
        return env.Null();
    }

    CloseHandle(hProcess);
    return Napi::String::New(env, "Should be suspended");
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    // export functions you need to call from node.js
    exports.Set("injectDll", Napi::Function::New(env, InjectDll));
    exports.Set("freezeProcess", Napi::Function::New(env, FreezeProcess));
    return exports;
}

NODE_API_MODULE(dllinjector, Init)
