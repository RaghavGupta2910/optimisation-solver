#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

using namespace std;

#ifdef _WIN32

#include <windows.h>

#else

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#endif


namespace benchmark {

// ============================================================
// ProcessResult
// ============================================================

struct ProcessResult {

    int exitCode = -1;

    string stdoutOutput;
    string stderrOutput;

    chrono::milliseconds runtime{0};

    bool timedOut = false;
    bool launchFailed = false;
};


// ============================================================
// WINDOWS IMPLEMENTATION
// ============================================================

#ifdef _WIN32

class ProcessManager {

public:

    ProcessResult run(
        const string& executable,
        const vector<string>& arguments,
        chrono::milliseconds timeout
    ) {

        ProcessResult result;

        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength =
            sizeof(SECURITY_ATTRIBUTES);

        securityAttributes.bInheritHandle = TRUE;


        HANDLE stdoutRead = NULL;
        HANDLE stdoutWrite = NULL;

        HANDLE stderrRead = NULL;
        HANDLE stderrWrite = NULL;


        // ----------------------------------------------------
        // Create stdout pipe
        // ----------------------------------------------------

        if (!CreatePipe(
                &stdoutRead,
                &stdoutWrite,
                &securityAttributes,
                0)) {

            result.launchFailed = true;
            return result;
        }


        SetHandleInformation(
            stdoutRead,
            HANDLE_FLAG_INHERIT,
            0
        );


        // ----------------------------------------------------
        // Create stderr pipe
        // ----------------------------------------------------

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


        SetHandleInformation(
            stderrRead,
            HANDLE_FLAG_INHERIT,
            0
        );


        // ----------------------------------------------------
        // Build command line
        // ----------------------------------------------------

        string commandLine = "\"";
        commandLine += executable;
        commandLine += "\"";

        for (const string& argument : arguments) {

            commandLine += " \"";
            commandLine += argument;
            commandLine += "\"";
        }


        vector<char> commandBuffer(
            commandLine.begin(),
            commandLine.end()
        );

        commandBuffer.push_back('\0');


        // ----------------------------------------------------
        // Startup information
        // ----------------------------------------------------

        STARTUPINFOA startupInfo{};

        startupInfo.cb =
            sizeof(STARTUPINFOA);

        startupInfo.dwFlags =
            STARTF_USESTDHANDLES;

        startupInfo.hStdOutput =
            stdoutWrite;

        startupInfo.hStdError =
            stderrWrite;

        startupInfo.hStdInput =
            GetStdHandle(STD_INPUT_HANDLE);


        PROCESS_INFORMATION processInfo{};


        // ----------------------------------------------------
        // Start process
        // ----------------------------------------------------

        auto start =
            chrono::steady_clock::now();


        BOOL created = CreateProcessA(
            NULL,
            commandBuffer.data(),
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &startupInfo,
            &processInfo
        );


        // Parent no longer needs write handles.

        CloseHandle(stdoutWrite);
        CloseHandle(stderrWrite);


        if (!created) {

            CloseHandle(stdoutRead);
            CloseHandle(stderrRead);

            result.launchFailed = true;

            return result;
        }


        // ----------------------------------------------------
        // Read output continuously
        // ----------------------------------------------------

        bool processFinished = false;

        while (!processFinished) {

            DWORD waitResult =
                WaitForSingleObject(
                    processInfo.hProcess,
                    20
                );


            char buffer[4096];

            DWORD bytesAvailable = 0;


            // stdout

            while (
                PeekNamedPipe(
                    stdoutRead,
                    NULL,
                    0,
                    NULL,
                    &bytesAvailable,
                    NULL
                ) &&
                bytesAvailable > 0
            ) {

                DWORD bytesRead = 0;

                if (ReadFile(
                        stdoutRead,
                        buffer,
                        sizeof(buffer),
                        &bytesRead,
                        NULL
                    ) &&
                    bytesRead > 0
                ) {

                    result.stdoutOutput.append(
                        buffer,
                        bytesRead
                    );
                }
                else {
                    break;
                }
            }


            // stderr

            while (
                PeekNamedPipe(
                    stderrRead,
                    NULL,
                    0,
                    NULL,
                    &bytesAvailable,
                    NULL
                ) &&
                bytesAvailable > 0
            ) {

                DWORD bytesRead = 0;

                if (ReadFile(
                        stderrRead,
                        buffer,
                        sizeof(buffer),
                        &bytesRead,
                        NULL
                    ) &&
                    bytesRead > 0
                ) {

                    result.stderrOutput.append(
                        buffer,
                        bytesRead
                    );
                }
                else {
                    break;
                }
            }


            // ------------------------------------------------
            // Check timeout
            // ------------------------------------------------

            auto now =
                chrono::steady_clock::now();


            auto elapsed =
                chrono::duration_cast<
                    chrono::milliseconds
                >(
                    now - start
                );


            if (
                elapsed >= timeout &&
                waitResult == WAIT_TIMEOUT
            ) {

                result.timedOut = true;


                TerminateProcess(
                    processInfo.hProcess,
                    1
                );


                WaitForSingleObject(
                    processInfo.hProcess,
                    INFINITE
                );


                processFinished = true;
            }


            if (
                waitResult == WAIT_OBJECT_0
            ) {

                processFinished = true;
            }
        }


        // ----------------------------------------------------
        // Read remaining output
        // ----------------------------------------------------

        char buffer[4096];

        DWORD bytesAvailable = 0;


        while (
            PeekNamedPipe(
                stdoutRead,
                NULL,
                0,
                NULL,
                &bytesAvailable,
                NULL
            ) &&
            bytesAvailable > 0
        ) {

            DWORD bytesRead = 0;

            if (
                ReadFile(
                    stdoutRead,
                    buffer,
                    sizeof(buffer),
                    &bytesRead,
                    NULL
                ) &&
                bytesRead > 0
            ) {

                result.stdoutOutput.append(
                    buffer,
                    bytesRead
                );
            }
            else {
                break;
            }
        }


        while (
            PeekNamedPipe(
                stderrRead,
                NULL,
                0,
                NULL,
                &bytesAvailable,
                NULL
            ) &&
            bytesAvailable > 0
        ) {

            DWORD bytesRead = 0;

            if (
                ReadFile(
                    stderrRead,
                    buffer,
                    sizeof(buffer),
                    &bytesRead,
                    NULL
                ) &&
                bytesRead > 0
            ) {

                result.stderrOutput.append(
                    buffer,
                    bytesRead
                );
            }
            else {
                break;
            }
        }


        // ----------------------------------------------------
        // Get exit code
        // ----------------------------------------------------

        DWORD exitCode = 0;

        GetExitCodeProcess(
            processInfo.hProcess,
            &exitCode
        );


        if (!result.timedOut) {

            result.exitCode =
                static_cast<int>(exitCode);
        }


        // ----------------------------------------------------
        // Runtime
        // ----------------------------------------------------

        result.runtime =
            chrono::duration_cast<
                chrono::milliseconds
            >(
                chrono::steady_clock::now()
                - start
            );


        // ----------------------------------------------------
        // Cleanup
        // ----------------------------------------------------

        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);

        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);


        return result;
    }
};


// ============================================================
// MACOS / LINUX IMPLEMENTATION
// ============================================================

#else

class ProcessManager {

public:

    ProcessResult run(
        const string& executable,
        const vector<string>& arguments,
        chrono::milliseconds timeout
    ) {

        ProcessResult result;

        int stdoutPipe[2];
        int stderrPipe[2];


        // ----------------------------------------------------
        // Create pipes
        // ----------------------------------------------------

        if (pipe(stdoutPipe) == -1) {

            result.launchFailed = true;
            return result;
        }


        if (pipe(stderrPipe) == -1) {

            close(stdoutPipe[0]);
            close(stdoutPipe[1]);

            result.launchFailed = true;
            return result;
        }


        // ----------------------------------------------------
        // Fork
        // ----------------------------------------------------

        pid_t pid = fork();


        if (pid == -1) {

            close(stdoutPipe[0]);
            close(stdoutPipe[1]);

            close(stderrPipe[0]);
            close(stderrPipe[1]);

            result.launchFailed = true;

            return result;
        }


        // ----------------------------------------------------
        // Child
        // ----------------------------------------------------

        if (pid == 0) {

            close(stdoutPipe[0]);
            close(stderrPipe[0]);


            dup2(
                stdoutPipe[1],
                STDOUT_FILENO
            );

            dup2(
                stderrPipe[1],
                STDERR_FILENO
            );


            close(stdoutPipe[1]);
            close(stderrPipe[1]);


            vector<char*> argv;

            argv.push_back(
                const_cast<char*>(
                    executable.c_str()
                )
            );


            for (const string& argument : arguments) {

                argv.push_back(
                    const_cast<char*>(
                        argument.c_str()
                    )
                );
            }


            argv.push_back(nullptr);


            execvp(
                executable.c_str(),
                argv.data()
            );


            // exec failed

            _exit(127);
        }


        // ----------------------------------------------------
        // Parent
        // ----------------------------------------------------

        close(stdoutPipe[1]);
        close(stderrPipe[1]);


        // Make pipes non-blocking

        int stdoutFlags =
            fcntl(
                stdoutPipe[0],
                F_GETFL,
                0
            );

        int stderrFlags =
            fcntl(
                stderrPipe[0],
                F_GETFL,
                0
            );


        fcntl(
            stdoutPipe[0],
            F_SETFL,
            stdoutFlags | O_NONBLOCK
        );


        fcntl(
            stderrPipe[0],
            F_SETFL,
            stderrFlags | O_NONBLOCK
        );


        // ----------------------------------------------------
        // Start timer
        // ----------------------------------------------------

        auto start =
            chrono::steady_clock::now();


        bool processFinished = false;

        int waitStatus = 0;


        // ----------------------------------------------------
        // Monitor process
        // ----------------------------------------------------

        while (!processFinished) {

            pollfd descriptors[2];


            descriptors[0].fd =
                stdoutPipe[0];

            descriptors[0].events =
                POLLIN;


            descriptors[1].fd =
                stderrPipe[0];

            descriptors[1].events =
                POLLIN;


            poll(
                descriptors,
                2,
                20
            );


            char buffer[4096];


            // ------------------------------------------------
            // stdout
            // ------------------------------------------------

            while (true) {

                ssize_t bytesRead =
                    read(
                        stdoutPipe[0],
                        buffer,
                        sizeof(buffer)
                    );


                if (bytesRead > 0) {

                    result.stdoutOutput.append(
                        buffer,
                        bytesRead
                    );
                }

                else {
                    break;
                }
            }


            // ------------------------------------------------
            // stderr
            // ------------------------------------------------

            while (true) {

                ssize_t bytesRead =
                    read(
                        stderrPipe[0],
                        buffer,
                        sizeof(buffer)
                    );


                if (bytesRead > 0) {

                    result.stderrOutput.append(
                        buffer,
                        bytesRead
                    );
                }

                else {
                    break;
                }
            }


            // ------------------------------------------------
            // Check process
            // ------------------------------------------------

            pid_t waitResult =
                waitpid(
                    pid,
                    &waitStatus,
                    WNOHANG
                );


            if (waitResult == pid) {

                processFinished = true;
            }


            // ------------------------------------------------
            // Timeout
            // ------------------------------------------------

            auto elapsed =
                chrono::duration_cast<
                    chrono::milliseconds
                >(
                    chrono::steady_clock::now()
                    - start
                );


            if (
                !processFinished &&
                elapsed >= timeout
            ) {

                result.timedOut = true;


                kill(
                    pid,
                    SIGKILL
                );


                waitpid(
                    pid,
                    &waitStatus,
                    0
                );


                processFinished = true;
            }
        }


        // ----------------------------------------------------
        // Read remaining output
        // ----------------------------------------------------

        char buffer[4096];


        while (true) {

            ssize_t bytesRead =
                read(
                    stdoutPipe[0],
                    buffer,
                    sizeof(buffer)
                );


            if (bytesRead > 0) {

                result.stdoutOutput.append(
                    buffer,
                    bytesRead
                );
            }

            else {
                break;
            }
        }


        while (true) {

            ssize_t bytesRead =
                read(
                    stderrPipe[0],
                    buffer,
                    sizeof(buffer)
                );


            if (bytesRead > 0) {

                result.stderrOutput.append(
                    buffer,
                    bytesRead
                );
            }

            else {
                break;
            }
        }


        close(stdoutPipe[0]);
        close(stderrPipe[0]);


        // ----------------------------------------------------
        // Exit code
        // ----------------------------------------------------

        if (!result.timedOut) {

            if (WIFEXITED(waitStatus)) {

                result.exitCode =
                    WEXITSTATUS(waitStatus);
            }

            else if (WIFSIGNALED(waitStatus)) {

                result.exitCode =
                    128 + WTERMSIG(waitStatus);
            }
        }


        // ----------------------------------------------------
        // Runtime
        // ----------------------------------------------------

        result.runtime =
            chrono::duration_cast<
                chrono::milliseconds
            >(
                chrono::steady_clock::now()
                - start
            );


        return result;
    }
};

#endif

} // namespace benchmark