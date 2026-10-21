/*
FxSound
Copyright (C) 2025  FxSound LLC

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <JuceHeader.h>
#include <comdef.h>
#include "GUI/FxSystemTrayView.h"
#include "GUI/FxController.h"
#include "GUI/FxTheme.h"
#include "GUI/FxMainWindow.h"
#include "AudioPassthru.h"
#include <dbghelp.h>
#include <shellapi.h>

#pragma comment(lib, "dbghelp.lib")

// Do not use high-performance GPU on laptops with dual graphics adapters
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000000;

    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}

//==============================================================================
class FxSoundApplication : public JUCEApplication
{
public:
    //==============================================================================
    FxSoundApplication() = default;

    const String getApplicationName() override       { return "evlsound"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       { return false; }

    //==============================================================================
    void initialise (const String& commandline) override
    {
        // Separate IPC prevents custom CLI values reaching stock FxSound. This
        // additional stock-named mutex excludes both audio engines atomically.
        if (!audio_engine_lock_.enter(0))
        {
            MessageBoxW(nullptr,
                L"Exit the installed FxSound from its system-tray menu, then launch evlsound. Your installed copy and presets are preserved.",
                L"evlsound", MB_OK | MB_ICONINFORMATION);
            setApplicationReturnValue(ERROR_BUSY);
            quit();
            return;
        }
        audio_engine_lock_held_ = true;

        // This method is where you should put your application's initialisation code..
        try
        {
            SetUnhandledExceptionFilter(unhandledExceptionFilter);

            HRESULT hRes = CoInitializeEx(0, COINIT_MULTITHREADED);
            if (SUCCEEDED(hRes))
            {
                com_initialized_ = true;
                CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                    RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
            }
            
            LookAndFeel::setDefaultLookAndFeel(&theme_);

            setWorkingDirectory();

            FxController::getInstance().initConfig(commandline);

            audio_passthru_ = std::make_unique<AudioPassthru>();
            main_window_ = std::make_unique<FxMainWindow>();
            system_tray_view_.reset(new FxSystemTrayView());

            FxController::getInstance().init(main_window_.get(), system_tray_view_.get(), audio_passthru_.get());
        }
        catch (const std::exception& e)
        {
            // Settings import can fail while the controller singleton is being
            // constructed. Reporting must not attempt to construct it again.
            OutputDebugStringA(e.what());
            MessageBoxW(nullptr, String(e.what()).toWideCharPointer(),
                L"evlsound could not start", MB_OK | MB_ICONERROR);
            setApplicationReturnValue(EXIT_FAILURE);
            quit();
        }
        catch (...)
        {
            MessageBoxW(nullptr, L"The custom build could not initialize. Installed FxSound has not been replaced.",
                L"evlsound could not start", MB_OK | MB_ICONERROR);
            setApplicationReturnValue(EXIT_FAILURE);
            quit();
        }
    }

    void shutdown() override
    {
        if (main_window_.get() != nullptr)
        {
            FxController::getInstance().autoSaveModifiedPreset();
            FxController::getInstance().stopTimer();

            audio_passthru_.reset();

            // UIA holds COM references to AccessibilityNativeHandle objects for
            // desktop-resident components. Call UiaDisconnectAllProviders to
            // synchronously release those references before the components are
            // destroyed, so their COM refcounts reach zero in the destructor
            // chain rather than after static destructors have already run.
            if (HMODULE hUia = GetModuleHandleW(L"UIAutomationCore.dll"))
            {
                typedef HRESULT (WINAPI* UiaDisconnectAllProvidersFunc)();
                if (auto fn = (UiaDisconnectAllProvidersFunc)GetProcAddress(hUia, "UiaDisconnectAllProviders"))
                    fn();
            }

            system_tray_view_.reset();

            main_window_.reset(); // (deletes our window)
        }

        LocalisedStrings::setCurrentMappings(nullptr);
        LookAndFeel::setDefaultLookAndFeel(nullptr);

        if (com_initialized_)
            CoUninitialize();
        if (audio_engine_lock_held_)
        {
            audio_engine_lock_.exit();
            audio_engine_lock_held_ = false;
        }
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        
        quit();
    }

    void anotherInstanceStarted (const String& commandline) override
    {
        FxController::getInstance().applyConfig(commandline);
    }

private:
    InterProcessLock audio_engine_lock_ { "juceAppLock_FxSound" };
    bool audio_engine_lock_held_ = false;
    bool com_initialized_ = false;
    FxTheme theme_;
    static constexpr int MAX_FRAMES = 64;

    static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exception_info)
    {
     
        auto logPath = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile(String(FxSound::SETTINGS_FOLDER));
        if (!logPath.exists()) {
            logPath.createDirectory();
        }

        juce::File dumpFile = logPath.getChildFile("fxsound.dmp");
        HANDLE hFile = CreateFileW(dumpFile.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
            dumpInfo.ThreadId = GetCurrentThreadId();
            dumpInfo.ExceptionPointers = exception_info;
            dumpInfo.ClientPointers = FALSE;

            MiniDumpWriteDump(GetCurrentProcess(),
                GetCurrentProcessId(),
                hFile,
                MiniDumpNormal,
                &dumpInfo,
                nullptr,
                nullptr);

            CloseHandle(hFile);
        }

        String message = String::formatted(
            "Unhandled exception\nException code: 0x%X\nException flags: 0x%X\nException address: 0x%p\n",
            exception_info->ExceptionRecord->ExceptionCode,
            exception_info->ExceptionRecord->ExceptionFlags,
            exception_info->ExceptionRecord->ExceptionAddress
        );

        // Do not reconstruct a controller that might have failed during startup.
        logPath.getChildFile("evlsound-crash.log").appendText(message);
        OutputDebugStringW(message.toWideCharPointer());

        return EXCEPTION_EXECUTE_HANDLER;
    }

    static void CaptureAndLogCallStack(CONTEXT* context = nullptr)
    {
        auto& controller = FxController::getInstance();
        String stacktrace_info;

        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();

        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);

        if (context == nullptr)
        {
            // No crash context - capture live callstack
            void* frames[MAX_FRAMES];
            USHORT capturedFrames = CaptureStackBackTrace(0, MAX_FRAMES, frames, NULL);

            char buffer[sizeof(SYMBOL_INFO) + 256] = { 0 };
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
            symbol->MaxNameLen = 255;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

            for (USHORT i = 0; i < capturedFrames; i++)
            {
                if (SymFromAddr(process, (DWORD64)frames[i], 0, symbol))
                {
                    stacktrace_info += String::formatted("%S at 0x%llX\n", symbol->Name, symbol->Address);

                    DWORD displacement;
                    IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
                    if (SymGetLineFromAddr64(process, (DWORD64)frames[i], &displacement, &line))
                    {
                        char* file_name = std::strrchr(line.FileName, '\\');
                        file_name = file_name ? (file_name + 1) : line.FileName;
                        stacktrace_info += String::formatted("    File: %S, Line: %d\n", file_name, line.LineNumber);
                    }
                }
                else
                {
                    stacktrace_info += String::formatted("Unknown function at 0x%p\n", frames[i]);
                }
            }
        }
        else
        {
            // Crash context - walk using StackWalk64
            STACKFRAME64 stackFrame = { 0 };
#if defined(_M_IX86)
            DWORD machineType = IMAGE_FILE_MACHINE_I386;
            stackFrame.AddrPC.Offset = context->Eip;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = context->Ebp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = context->Esp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_X64)
            DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
            stackFrame.AddrPC.Offset = context->Rip;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = context->Rsp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = context->Rsp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
            DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
            stackFrame.AddrPC.Offset = context->Pc;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = context->Fp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = context->Sp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#else
#error Unsupported platform
#endif

            char buffer[sizeof(SYMBOL_INFO) + 256] = { 0 };
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
            symbol->MaxNameLen = 255;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

            for (int frame = 0; frame < MAX_FRAMES; ++frame)
            {
                if (!StackWalk64(machineType, process, thread, &stackFrame, context, NULL,
                    SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                {
                    break;
                }

                if (stackFrame.AddrPC.Offset == 0)
                    break;

                DWORD64 address = stackFrame.AddrPC.Offset;

                if (SymFromAddr(process, address, 0, symbol))
                {
                    stacktrace_info += String::formatted("%S at 0x%llX\n", symbol->Name, symbol->Address);

                    DWORD displacement;
                    IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
                    if (SymGetLineFromAddr64(process, address, &displacement, &line))
                    {
                        char* file_name = std::strrchr(line.FileName, '\\');
                        file_name = file_name ? (file_name + 1) : line.FileName;
                        stacktrace_info += String::formatted("    File: %S, Line: %d\n", file_name, line.LineNumber);
                    }
                }
                else
                {
                    stacktrace_info += String::formatted("Unknown function at 0x%llX\n", address);
                }
            }
        }

        SymCleanup(process);

        controller.logMessage(stacktrace_info);
    }

    void setWorkingDirectory()
    {
        wchar_t module_file[MAX_PATH];
        DWORD length = GetModuleFileName(NULL, module_file, MAX_PATH);
        if (length > 0)
        {
            std::wstring path(module_file);
            size_t pos = path.find_last_of(L"\\/");
            if (pos != std::string::npos)
            {
                SetCurrentDirectory(path.substr(0, pos).c_str());
            }
        }
    };

    std::unique_ptr<FxMainWindow> main_window_;
    
    std::unique_ptr<FxSystemTrayView> system_tray_view_;
    std::unique_ptr<AudioPassthru> audio_passthru_;
};

//==============================================================================
JUCE_CREATE_APPLICATION_DEFINE(FxSoundApplication)

extern "C" JUCE_MAIN_FUNCTION
{
    // This diagnostic runs before JUCE GUI, app, profile, or audio creation.
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
        return ERROR_INVALID_PARAMETER;
    bool checkOnly = false;
    if (argv != nullptr)
    {
        for (int i = 1; i < argc; ++i)
            checkOnly = checkOnly || wcscmp(argv[i], L"--evlsound-check") == 0;
        LocalFree(argv);
    }
    if (checkOnly)
    {
        InterProcessLock engineLock("juceAppLock_FxSound");
        if (!engineLock.enter(0))
            return ERROR_BUSY;
        engineLock.exit();
        return ERROR_SUCCESS;
    }
    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main(JUCE_MAIN_FUNCTION_ARGS);
}
