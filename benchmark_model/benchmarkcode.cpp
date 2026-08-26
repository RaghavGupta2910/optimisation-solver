#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using namespace std;


// ============================================================
// ProcessResult
// ============================================================

struct ProcessResult {

    // Exit code returned by the process
    int exitCode = -1;

    // Captured output
    string stdoutText;
    string stderrText;

    // Wall-clock execution time
    double runtimeSeconds = 0.0;

    // Execution information
    bool timedOut = false;
    bool launchFailed = false;
};


// ============================================================
// Read output from a pipe
// ============================================================

string readPipe(HANDLE pipe) {

    string output;

    char buffer[4096];
    DWORD bytesRead = 0;

    while (true) {

        DWORD available = 0;

        if (!PeekNamedPipe(
                pipe,
                nullptr,
                0,
                nullptr,
                &available,
                nullptr)) {

            break;
        }

        if (available == 0) {
            break;
        }

        DWORD toRead =
            min(
                available,
                static_cast<DWORD>(sizeof(buffer))
            );

        if (!ReadFile(
                pipe,
                buffer,
                toRead,
                &bytesRead,
                nullptr)) {

            break;
        }

        if (bytesRead == 0) {
            break;
        }

        output.append(buffer, bytesRead);
    }

    return output;
}


// ============================================================
// ProcessManager
// ============================================================

class ProcessManager {

public:

    ProcessResult run(
        const string& executable,
        const vector<string>& arguments,
        int timeoutSeconds
    ) {

        ProcessResult result;


        // ----------------------------------------------------
        // 1. Construct command line
        // ----------------------------------------------------

        string commandLine = "\"" + executable + "\"";

        for (const string& argument : arguments) {

            commandLine += " \"" + argument + "\"";
        }


        vector<char> commandBuffer(
            commandLine.begin(),
            commandLine.end()
        );

        commandBuffer.push_back('\0');


        // ----------------------------------------------------
        // 2. Create security attributes
        // ----------------------------------------------------

        SECURITY_ATTRIBUTES securityAttributes{};

        securityAttributes.nLength =
            sizeof(SECURITY_ATTRIBUTES);

        securityAttributes.bInheritHandle = TRUE;

        securityAttributes.lpSecurityDescriptor =
            nullptr;


        // ----------------------------------------------------
        // 3. Create stdout pipe
        // ----------------------------------------------------

        HANDLE stdoutRead = nullptr;
        HANDLE stdoutWrite = nullptr;

        if (!CreatePipe(
                &stdoutRead,
                &stdoutWrite,
                &securityAttributes,
                0)) {

            result.launchFailed = true;

            return result;
        }


        // Parent should not inherit read handle

        if (!SetHandleInformation(
                stdoutRead,
                HANDLE_FLAG_INHERIT,
                0)) {

            CloseHandle(stdoutRead);
            CloseHandle(stdoutWrite);

            result.launchFailed = true;

            return result;
        }


        // ----------------------------------------------------
        // 4. Create stderr pipe
        // ----------------------------------------------------

        HANDLE stderrRead = nullptr;
        HANDLE stderrWrite = nullptr;

        if (!CreatePipe(
                &stderrRead,
                &stderrWrite,
                &securityAttributes,
                0)) {

            CloseHandle(stdoutRead);
            CloseHandle(stdoutWrite);

            result.launchFailed = true;

            return result;
        }


        if (!SetHandleInformation(
                stderrRead,
                HANDLE_FLAG_INHERIT,
                0)) {

            CloseHandle(stdoutRead);
            CloseHandle(stdoutWrite);

            CloseHandle(stderrRead);
            CloseHandle(stderrWrite);

            result.launchFailed = true;

            return result;
        }


        // ----------------------------------------------------
        // 5. Configure process startup
        // ----------------------------------------------------

        STARTUPINFOA startupInfo{};

        startupInfo.cb =
            sizeof(STARTUPINFOA);

        startupInfo.dwFlags |=
            STARTF_USESTDHANDLES;

        startupInfo.hStdOutput =
            stdoutWrite;

        startupInfo.hStdError =
            stderrWrite;

        startupInfo.hStdInput =
            GetStdHandle(STD_INPUT_HANDLE);


        PROCESS_INFORMATION processInfo{};


        // ----------------------------------------------------
        // 6. Start timer
        // ----------------------------------------------------

        auto startTime =
            chrono::steady_clock::now();


        // ----------------------------------------------------
        // 7. Launch process
        // ----------------------------------------------------

        BOOL processCreated = CreateProcessA(

            nullptr,

            commandBuffer.data(),

            nullptr,

            nullptr,

            TRUE,

            0,

            nullptr,

            nullptr,

            &startupInfo,

            &processInfo
        );


        // Parent no longer needs write handles

        CloseHandle(stdoutWrite);
        CloseHandle(stderrWrite);


        // ----------------------------------------------------
        // 8. Handle launch failure
        // ----------------------------------------------------

        if (!processCreated) {

            DWORD errorCode =
                GetLastError();

            cout << "Process launch failed.\n";
            cout << "Windows error code: "
                 << errorCode
                 << "\n";

            CloseHandle(stdoutRead);
            CloseHandle(stderrRead);

            result.launchFailed = true;

            return result;
        }


        // ----------------------------------------------------
        // 9. Wait for process
        // ----------------------------------------------------

        DWORD timeoutMilliseconds =
            static_cast<DWORD>(
                timeoutSeconds * 1000
            );


        DWORD waitResult =
            WaitForSingleObject(
                processInfo.hProcess,
                timeoutMilliseconds
            );


        // ----------------------------------------------------
        // 10. Handle timeout
        // ----------------------------------------------------

        if (waitResult == WAIT_TIMEOUT) {

            result.timedOut = true;

            cout << "Process timed out.\n";


            // Terminate solver

            TerminateProcess(
                processInfo.hProcess,
                1
            );


            // Wait until process actually terminates

            WaitForSingleObject(
                processInfo.hProcess,
                INFINITE
            );
        }


        // ----------------------------------------------------
        // 11. Get exit code
        // ----------------------------------------------------

        DWORD exitCode = 0;

        if (GetExitCodeProcess(
                processInfo.hProcess,
                &exitCode)) {

            result.exitCode =
                static_cast<int>(exitCode);
        }


        // ----------------------------------------------------
        // 12. Stop timer
        // ----------------------------------------------------

        auto endTime =
            chrono::steady_clock::now();


        chrono::duration<double> elapsed =
            endTime - startTime;


        result.runtimeSeconds =
            elapsed.count();


        // ----------------------------------------------------
        // 13. Capture stdout
        // ----------------------------------------------------

        result.stdoutText =
            readPipe(stdoutRead);


        // ----------------------------------------------------
        // 14. Capture stderr
        // ----------------------------------------------------

        result.stderrText =
            readPipe(stderrRead);


        // ----------------------------------------------------
        // 15. Cleanup
        // ----------------------------------------------------

        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);

        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);


        return result;
    }
};


// ============================================================
// TEST
// ============================================================

int main() {

    cout << "=================================\n";
    cout << "      BENCHMARK PROCESS TEST\n";
    cout << "=================================\n\n";


    ProcessManager processManager;


    // --------------------------------------------------------
    // Temporary test executable
    // --------------------------------------------------------
    //
    // We don't have our actual solver yet.
    // Therefore we use Windows CMD as a temporary process.
    //
    // Later this will become:
    //
    // solver.exe + instance.mps
    //
    // --------------------------------------------------------

    string executable =
        "C:\\Windows\\System32\\cmd.exe";


    vector<string> arguments = {

        "/C",

        "echo Benchmark process started"
    };


    int timeoutSeconds = 10;


    // --------------------------------------------------------
    // Run process
    // --------------------------------------------------------

    ProcessResult result =
        processManager.run(
            executable,
            arguments,
            timeoutSeconds
        );


    // --------------------------------------------------------
    // Display result
    // --------------------------------------------------------

    cout << "\n=================================\n";
    cout << "          PROCESS RESULT\n";
    cout << "=================================\n";


    cout << "Launch failed : "
         << boolalpha
         << result.launchFailed
         << "\n";


    cout << "Timed out     : "
         << result.timedOut
         << "\n";


    cout << "Exit code     : "
         << result.exitCode
         << "\n";


    cout << "Runtime       : "
         << result.runtimeSeconds
         << " seconds\n";


    cout << "\n------------ STDOUT ------------\n";

    cout << result.stdoutText;


    cout << "\n------------ STDERR ------------\n";

    cout << result.stderrText;


    cout << "\n=================================\n";


    return 0;
}