#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class JobStatus
{
    Running,
    Suspended,
    Exited
};

struct ParsedCommand
{
    std::string raw;
    std::vector<std::string> tokens;
    bool background = false;
};

struct BackgroundJob
{
    DWORD pid = 0;
    PROCESS_INFORMATION processInfo{};
    std::string command;
    JobStatus status = JobStatus::Running;
    DWORD exitCode = STILL_ACTIVE;
    bool handlesOpen = true;
};

struct ForegroundProcess
{
    HANDLE handle = nullptr;
    DWORD pid = 0;
    std::string command;
};

CRITICAL_SECTION g_stateLock;
ForegroundProcess g_foreground;
std::vector<BackgroundJob> g_jobs;
std::vector<std::string> g_history;
bool g_shouldExit = false;

std::string trim(const std::string &text)
{
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });

    if (first == text.end())
    {
        return "";
    }

    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    return std::string(first, last);
}

std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool equalsIgnoreCase(const std::string &left, const std::string &right)
{
    return toLower(left) == toLower(right);
}

bool startsWithIgnoreCase(const std::string &text, const std::string &prefix)
{
    if (prefix.size() > text.size())
    {
        return false;
    }
    return equalsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

std::vector<std::string> splitSemicolonList(const std::string &text)
{
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string item;

    while (std::getline(stream, item, ';'))
    {
        item = trim(item);
        if (!item.empty())
        {
            parts.push_back(item);
        }
    }

    return parts;
}

std::vector<std::string> tokenize(const std::string &line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;

    for (char ch : line)
    {
        if (ch == '"')
        {
            inQuotes = !inQuotes;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(ch)) && !inQuotes)
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty())
    {
        tokens.push_back(current);
    }

    return tokens;
}

bool endsWithBackgroundMarker(const std::string &line)
{
    bool inQuotes = false;
    int lastNonSpace = -1;

    for (int index = 0; index < static_cast<int>(line.size()); ++index)
    {
        if (line[index] == '"')
        {
            inQuotes = !inQuotes;
        }
        if (!std::isspace(static_cast<unsigned char>(line[index])))
        {
            lastNonSpace = index;
        }
    }

    return lastNonSpace >= 0 && line[lastNonSpace] == '&' && !inQuotes;
}

ParsedCommand parseCommandLine(const std::string &input)
{
    ParsedCommand parsed;
    parsed.raw = trim(input);

    if (parsed.raw.empty())
    {
        return parsed;
    }

    if (endsWithBackgroundMarker(parsed.raw))
    {
        parsed.background = true;
        parsed.raw = trim(parsed.raw.substr(0, parsed.raw.find_last_of('&')));
    }

    parsed.tokens = tokenize(parsed.raw);
    return parsed;
}

std::string getEnvironmentValue(const std::string &name)
{
    DWORD requiredSize = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
    if (requiredSize == 0)
    {
        return "";
    }

    std::string value(requiredSize, '\0');
    GetEnvironmentVariableA(name.c_str(), value.data(), requiredSize);

    if (!value.empty() && value.back() == '\0')
    {
        value.pop_back();
    }

    return value;
}

bool setEnvironmentValue(const std::string &name, const std::string &value)
{
    if (!SetEnvironmentVariableA(name.c_str(), value.c_str()))
    {
        std::cout << "Cannot set environment variable. Windows error: " << GetLastError() << "\n";
        return false;
    }

    _putenv_s(name.c_str(), value.c_str());
    return true;
}

std::string statusToString(const BackgroundJob &job)
{
    if (job.status == JobStatus::Running)
    {
        return "Running";
    }

    if (job.status == JobStatus::Suspended)
    {
        return "Suspended";
    }

    return "Exited(" + std::to_string(job.exitCode) + ")";
}

void closeJobHandles(BackgroundJob &job)
{
    if (!job.handlesOpen)
    {
        return;
    }

    CloseHandle(job.processInfo.hThread);
    CloseHandle(job.processInfo.hProcess);
    job.handlesOpen = false;
}

void refreshBackgroundJobs()
{
    EnterCriticalSection(&g_stateLock);

    for (auto &job : g_jobs)
    {
        if (!job.handlesOpen || job.status == JobStatus::Exited)
        {
            continue;
        }

        DWORD exitCode = STILL_ACTIVE;
        if (GetExitCodeProcess(job.processInfo.hProcess, &exitCode) && exitCode != STILL_ACTIVE)
        {
            job.status = JobStatus::Exited;
            job.exitCode = exitCode;
        }
    }

    LeaveCriticalSection(&g_stateLock);
}

BackgroundJob *findJobByPid(DWORD pid)
{
    for (auto &job : g_jobs)
    {
        if (job.pid == pid)
        {
            return &job;
        }
    }
    return nullptr;
}

bool parsePid(const std::vector<std::string> &args, DWORD &pid)
{
    if (args.size() != 1)
    {
        return false;
    }

    try
    {
        unsigned long parsed = std::stoul(args[0]);
        pid = static_cast<DWORD>(parsed);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

std::string findExecutable(const std::string &command)
{
    fs::path commandPath(command);
    std::error_code error;

    if (commandPath.has_parent_path())
    {
        fs::path absolutePath = fs::absolute(commandPath, error);
        if (!error && fs::exists(absolutePath, error))
        {
            return absolutePath.string();
        }
        return "";
    }

    std::vector<std::string> candidates;
    fs::path currentCandidate = fs::current_path(error) / command;
    candidates.push_back(currentCandidate.string());

    std::string pathValue = getEnvironmentValue("PATH");
    for (const std::string &folder : splitSemicolonList(pathValue))
    {
        candidates.push_back((fs::path(folder) / command).string());
    }

    std::vector<std::string> extensions{""};
    for (const std::string &extension : splitSemicolonList(getEnvironmentValue("PATHEXT")))
    {
        extensions.push_back(extension);
    }

    for (const std::string &candidate : candidates)
    {
        fs::path candidatePath(candidate);
        if (candidatePath.has_extension())
        {
            if (fs::exists(candidatePath, error) && !fs::is_directory(candidatePath, error))
            {
                return fs::absolute(candidatePath, error).string();
            }
            continue;
        }

        for (const std::string &extension : extensions)
        {
            fs::path withExtension = candidatePath;
            if (!extension.empty())
            {
                withExtension += extension;
            }

            if (fs::exists(withExtension, error) && !fs::is_directory(withExtension, error))
            {
                return fs::absolute(withExtension, error).string();
            }
        }
    }

    return "";
}

bool isBatchCommand(const ParsedCommand &command)
{
    if (command.tokens.empty())
    {
        return false;
    }

    std::string first = command.tokens.front();
    std::string resolved = findExecutable(first);
    fs::path pathToCheck = resolved.empty() ? fs::path(first) : fs::path(resolved);
    std::string extension = toLower(pathToCheck.extension().string());
    return extension == ".bat" || extension == ".cmd";
}

bool containsCmdMetaCharacter(const std::string &raw)
{
    bool inQuotes = false;

    for (char ch : raw)
    {
        if (ch == '"')
        {
            inQuotes = !inQuotes;
            continue;
        }

        if (!inQuotes && (ch == '>' || ch == '<' || ch == '|' || ch == '&'))
        {
            return true;
        }
    }

    return false;
}

std::string buildCommandLine(const ParsedCommand &command, bool forceCmd)
{
    if (forceCmd || isBatchCommand(command) || containsCmdMetaCharacter(command.raw))
    {
        return "cmd.exe /C " + command.raw;
    }

    return command.raw;
}

void rememberForegroundProcess(const PROCESS_INFORMATION &processInfo, const std::string &command)
{
    EnterCriticalSection(&g_stateLock);
    g_foreground.handle = processInfo.hProcess;
    g_foreground.pid = processInfo.dwProcessId;
    g_foreground.command = command;
    LeaveCriticalSection(&g_stateLock);
}

void clearForegroundProcess()
{
    EnterCriticalSection(&g_stateLock);
    g_foreground.handle = nullptr;
    g_foreground.pid = 0;
    g_foreground.command.clear();
    LeaveCriticalSection(&g_stateLock);
}

BOOL WINAPI consoleControlHandler(DWORD controlType)
{
    if (controlType != CTRL_C_EVENT && controlType != CTRL_BREAK_EVENT)
    {
        return FALSE;
    }

    HANDLE foregroundHandle = nullptr;
    DWORD foregroundPid = 0;

    EnterCriticalSection(&g_stateLock);
    foregroundHandle = g_foreground.handle;
    foregroundPid = g_foreground.pid;
    LeaveCriticalSection(&g_stateLock);

    if (foregroundHandle != nullptr)
    {
        TerminateProcess(foregroundHandle, CONTROL_C_EXIT);
        std::cout << "\n[Ctrl+C] Foreground process " << foregroundPid << " was terminated.\n";
        return TRUE;
    }

    std::cout << "\nNo foreground process is running. Type 'exit' to leave myShell.\n";
    return TRUE;
}

bool createChildProcess(const ParsedCommand &command, bool forceCmd = false)
{
    std::string commandLine = buildCommandLine(command, forceCmd);
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};
    DWORD creationFlags = CREATE_NEW_PROCESS_GROUP;

    std::cout.flush();

    BOOL created = CreateProcessA(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        creationFlags,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created && !forceCmd)
    {
        return createChildProcess(command, true);
    }

    if (!created)
    {
        std::cout << "Cannot execute command. Windows error: " << GetLastError() << "\n";
        return false;
    }

    if (command.background)
    {
        BackgroundJob job;
        job.pid = processInfo.dwProcessId;
        job.processInfo = processInfo;
        job.command = command.raw;

        EnterCriticalSection(&g_stateLock);
        g_jobs.push_back(job);
        LeaveCriticalSection(&g_stateLock);

        std::cout << "[background] PID " << processInfo.dwProcessId << " started: " << command.raw << "\n";
        return true;
    }

    rememberForegroundProcess(processInfo, command.raw);
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    clearForegroundProcess();

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    std::cout << "[foreground] exit code: " << exitCode << "\n";
    return true;
}

bool changeThreadState(DWORD pid, bool suspend)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        std::cout << "Cannot create thread snapshot. Windows error: " << GetLastError() << "\n";
        return false;
    }

    THREADENTRY32 threadEntry{};
    threadEntry.dwSize = sizeof(threadEntry);

    if (!Thread32First(snapshot, &threadEntry))
    {
        CloseHandle(snapshot);
        std::cout << "Cannot read thread snapshot. Windows error: " << GetLastError() << "\n";
        return false;
    }

    bool touchedAnyThread = false;

    do
    {
        if (threadEntry.th32OwnerProcessID != pid)
        {
            continue;
        }

        HANDLE threadHandle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadEntry.th32ThreadID);
        if (threadHandle == nullptr)
        {
            continue;
        }

        if (suspend)
        {
            SuspendThread(threadHandle);
        }
        else
        {
            while (ResumeThread(threadHandle) > 1)
            {
            }
        }

        touchedAnyThread = true;
        CloseHandle(threadHandle);
    } while (Thread32Next(snapshot, &threadEntry));

    CloseHandle(snapshot);
    return touchedAnyThread;
}

void listBackgroundJobs()
{
    refreshBackgroundJobs();

    EnterCriticalSection(&g_stateLock);

    if (g_jobs.empty())
    {
        std::cout << "No background processes are tracked.\n";
        LeaveCriticalSection(&g_stateLock);
        return;
    }

    std::cout << std::left << std::setw(10) << "PID"
              << std::setw(16) << "Status"
              << "Command\n";
    std::cout << std::string(70, '-') << "\n";

    for (const auto &job : g_jobs)
    {
        std::cout << std::left << std::setw(10) << job.pid
                  << std::setw(16) << statusToString(job)
                  << job.command << "\n";
    }

    LeaveCriticalSection(&g_stateLock);
}

void reapExitedJobs()
{
    refreshBackgroundJobs();

    EnterCriticalSection(&g_stateLock);
    std::size_t before = g_jobs.size();

    auto newEnd = std::remove_if(g_jobs.begin(), g_jobs.end(), [](BackgroundJob &job) {
        if (job.status == JobStatus::Exited)
        {
            closeJobHandles(job);
            return true;
        }
        return false;
    });

    g_jobs.erase(newEnd, g_jobs.end());
    std::size_t removed = before - g_jobs.size();
    LeaveCriticalSection(&g_stateLock);

    std::cout << "Removed " << removed << " exited background job(s).\n";
}

void killProcessCommand(const std::vector<std::string> &args)
{
    DWORD pid = 0;
    if (!parsePid(args, pid))
    {
        std::cout << "Usage: kill <pid>\n";
        return;
    }

    HANDLE processHandle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (processHandle == nullptr)
    {
        std::cout << "Cannot open PID " << pid << ". Windows error: " << GetLastError() << "\n";
        return;
    }

    if (!TerminateProcess(processHandle, 1))
    {
        std::cout << "Cannot terminate PID " << pid << ". Windows error: " << GetLastError() << "\n";
        CloseHandle(processHandle);
        return;
    }

    WaitForSingleObject(processHandle, 1000);
    CloseHandle(processHandle);

    EnterCriticalSection(&g_stateLock);
    BackgroundJob *job = findJobByPid(pid);
    if (job != nullptr)
    {
        job->status = JobStatus::Exited;
        job->exitCode = 1;
    }
    LeaveCriticalSection(&g_stateLock);

    std::cout << "PID " << pid << " was terminated.\n";
}

void stopOrResumeProcessCommand(const std::vector<std::string> &args, bool suspend)
{
    DWORD pid = 0;
    if (!parsePid(args, pid))
    {
        std::cout << (suspend ? "Usage: stop <pid>\n" : "Usage: resume <pid>\n");
        return;
    }

    if (!changeThreadState(pid, suspend))
    {
        std::cout << "No thread was changed for PID " << pid << ". Check the PID and permissions.\n";
        return;
    }

    EnterCriticalSection(&g_stateLock);
    BackgroundJob *job = findJobByPid(pid);
    if (job != nullptr && job->status != JobStatus::Exited)
    {
        job->status = suspend ? JobStatus::Suspended : JobStatus::Running;
    }
    LeaveCriticalSection(&g_stateLock);

    std::cout << "PID " << pid << (suspend ? " was suspended.\n" : " was resumed.\n");
}

void printDirectory(const std::vector<std::string> &args)
{
    fs::path target = args.empty() ? fs::current_path() : fs::path(args[0]);
    std::error_code error;

    if (!fs::exists(target, error))
    {
        std::cout << "Directory does not exist: " << target.string() << "\n";
        return;
    }

    if (!fs::is_directory(target, error))
    {
        std::cout << "Not a directory: " << target.string() << "\n";
        return;
    }

    std::cout << "Directory of " << fs::absolute(target, error).string() << "\n\n";
    std::cout << std::left << std::setw(12) << "Type"
              << std::right << std::setw(14) << "Size"
              << "  Name\n";
    std::cout << std::string(60, '-') << "\n";

    for (const auto &entry : fs::directory_iterator(target, fs::directory_options::skip_permission_denied, error))
    {
        std::string type = entry.is_directory(error) ? "<DIR>" : "FILE";
        std::uintmax_t size = entry.is_regular_file(error) ? entry.file_size(error) : 0;

        std::cout << std::left << std::setw(12) << type
                  << std::right << std::setw(14) << size
                  << "  " << entry.path().filename().string() << "\n";
    }
}

void changeDirectory(const std::vector<std::string> &args)
{
    if (args.size() != 1)
    {
        std::cout << "Usage: cd <directory>\n";
        return;
    }

    std::error_code error;
    fs::current_path(args[0], error);
    if (error)
    {
        std::cout << "Cannot change directory: " << error.message() << "\n";
        return;
    }

    std::cout << fs::current_path().string() << "\n";
}

void clearScreen()
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE)
    {
        return;
    }

    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(output, &info))
    {
        return;
    }

    DWORD cellCount = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
    DWORD written = 0;
    COORD home{0, 0};

    FillConsoleOutputCharacterA(output, ' ', cellCount, home, &written);
    FillConsoleOutputAttribute(output, info.wAttributes, cellCount, home, &written);
    SetConsoleCursorPosition(output, home);
}

void printDateTime(bool dateOnly)
{
    SYSTEMTIME now{};
    GetLocalTime(&now);

    if (dateOnly)
    {
        std::cout << std::setfill('0') << std::setw(2) << now.wDay << "/"
                  << std::setw(2) << now.wMonth << "/"
                  << std::setw(4) << now.wYear << std::setfill(' ') << "\n";
        return;
    }

    std::cout << std::setfill('0') << std::setw(2) << now.wHour << ":"
              << std::setw(2) << now.wMinute << ":"
              << std::setw(2) << now.wSecond << std::setfill(' ') << "\n";
}

void printPath()
{
    std::cout << getEnvironmentValue("PATH") << "\n";
}

void addPath(const std::vector<std::string> &args)
{
    if (args.size() != 1)
    {
        std::cout << "Usage: addpath <directory>\n";
        return;
    }

    std::string currentPath = getEnvironmentValue("PATH");
    std::vector<std::string> folders = splitSemicolonList(currentPath);

    for (const std::string &folder : folders)
    {
        if (equalsIgnoreCase(folder, args[0]))
        {
            std::cout << "PATH already contains: " << args[0] << "\n";
            return;
        }
    }

    std::string updatedPath = currentPath.empty() ? args[0] : currentPath + ";" + args[0];
    if (setEnvironmentValue("PATH", updatedPath))
    {
        std::cout << "Added to PATH for this shell session: " << args[0] << "\n";
    }
}

void setPath(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cout << "Usage: setpath <new PATH value>\n";
        return;
    }

    std::string value = args[0];
    for (std::size_t index = 1; index < args.size(); ++index)
    {
        value += " " + args[index];
    }

    if (setEnvironmentValue("PATH", value))
    {
        std::cout << "PATH was replaced for this shell session.\n";
    }
}

void printEnvironment(const std::vector<std::string> &args)
{
    if (args.size() == 1)
    {
        std::cout << args[0] << "=" << getEnvironmentValue(args[0]) << "\n";
        return;
    }

    if (args.empty())
    {
        LPCH environmentBlock = GetEnvironmentStringsA();
        if (environmentBlock == nullptr)
        {
            std::cout << "Cannot read environment block. Windows error: " << GetLastError() << "\n";
            return;
        }

        for (LPCH item = environmentBlock; *item != '\0'; item += std::strlen(item) + 1)
        {
            std::cout << item << "\n";
        }

        FreeEnvironmentStringsA(environmentBlock);
        return;
    }

    std::cout << "Usage: env [NAME]\n";
}

void setEnvironmentCommand(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cout << "Usage: setenv <NAME> <VALUE>\n";
        return;
    }

    std::string value = args[1];
    for (std::size_t index = 2; index < args.size(); ++index)
    {
        value += " " + args[index];
    }

    if (setEnvironmentValue(args[0], value))
    {
        std::cout << args[0] << " was set.\n";
    }
}

void unsetEnvironmentCommand(const std::vector<std::string> &args)
{
    if (args.size() != 1)
    {
        std::cout << "Usage: unsetenv <NAME>\n";
        return;
    }

    if (SetEnvironmentVariableA(args[0].c_str(), nullptr))
    {
        _putenv_s(args[0].c_str(), "");
        std::cout << args[0] << " was removed.\n";
    }
}

void printHelp()
{
    std::cout << "WELCOME TO myShell\n\n"
              << "Run a program normally:      notepad\n"
              << "Run in background:          ping -n 10 127.0.0.1 > nul &\n"
              << "Run a Windows batch file:   scripts\\hello.bat\n"
              << "Run a shell script file:    run scripts\\demo.tsh\n\n"
              << "Required process commands:\n"
              << "  list | jobs               list tracked background processes\n"
              << "  kill <pid>                terminate a process\n"
              << "  stop <pid>                suspend all threads of a process\n"
              << "  resume <pid>              resume all threads of a process\n"
              << "  reap                      remove exited jobs from the job table\n\n"
              << "Required built-in commands:\n"
              << "  help                      print this help message\n"
              << "  exit                      terminate background jobs and exit\n"
              << "  date                      print local date\n"
              << "  time                      print local time\n"
              << "  dir [directory]           list files and folders\n"
              << "  path                      print PATH\n"
              << "  addpath <directory>       append a directory to PATH for this session\n"
              << "  setpath <value>           replace PATH for this session\n\n"
              << "Small extra OS utilities:\n"
              << "  cd <directory>            change current directory\n"
              << "  pwd                       print current directory\n"
              << "  env [NAME]                print environment variables\n"
              << "  setenv <NAME> <VALUE>     set one environment variable\n"
              << "  unsetenv <NAME>           remove one environment variable\n"
              << "  which <command>           resolve a command through PATH/PATHEXT\n"
              << "  history                   print commands entered in this session\n"
              << "  clear                     clear the console\n"
              << "  sleep <milliseconds>      pause the shell\n";
}

void printBanner()
{
    std::cout << "========================================\n"
              << "              Tiny Shell\n"
              << "========================================\n"
              << "Windows process API demo for OS course\n"
              << "Shell PID: " << GetCurrentProcessId() << "\n"
              << "Type 'help' for command list.\n"
              << "========================================\n";
}

bool executeLine(const std::string &line, bool fromScript);

void runScript(const std::vector<std::string> &args)
{
    if (args.size() != 1)
    {
        std::cout << "Usage: run <script-file>\n";
        return;
    }

    std::ifstream script(args[0]);
    if (!script)
    {
        std::cout << "Cannot open script: " << args[0] << "\n";
        return;
    }

    std::string line;
    int lineNumber = 0;

    while (std::getline(script, line) && !g_shouldExit)
    {
        ++lineNumber;
        std::string cleaned = trim(line);

        if (cleaned.empty() || cleaned[0] == '#' || startsWithIgnoreCase(cleaned, "rem ") || startsWithIgnoreCase(cleaned, "::"))
        {
            continue;
        }

        std::cout << "[script:" << lineNumber << "] " << cleaned << "\n";
        std::cout.flush();
        g_history.push_back(cleaned);
        executeLine(cleaned, true);
    }
}

bool runBuiltin(const ParsedCommand &command)
{
    if (command.tokens.empty())
    {
        return true;
    }

    std::string name = toLower(command.tokens[0]);
    std::vector<std::string> args(command.tokens.begin() + 1, command.tokens.end());

    if (name == "help")
    {
        printHelp();
    }
    else if (name == "exit" || name == "quit")
    {
        g_shouldExit = true;
    }
    else if (name == "list" || name == "jobs")
    {
        listBackgroundJobs();
    }
    else if (name == "reap")
    {
        reapExitedJobs();
    }
    else if (name == "kill")
    {
        killProcessCommand(args);
    }
    else if (name == "stop")
    {
        stopOrResumeProcessCommand(args, true);
    }
    else if (name == "resume")
    {
        stopOrResumeProcessCommand(args, false);
    }
    else if (name == "date")
    {
        printDateTime(true);
    }
    else if (name == "time")
    {
        printDateTime(false);
    }
    else if (name == "dir")
    {
        printDirectory(args);
    }
    else if (name == "cd")
    {
        changeDirectory(args);
    }
    else if (name == "pwd")
    {
        std::cout << fs::current_path().string() << "\n";
    }
    else if (name == "path")
    {
        printPath();
    }
    else if (name == "addpath")
    {
        addPath(args);
    }
    else if (name == "setpath")
    {
        setPath(args);
    }
    else if (name == "env")
    {
        printEnvironment(args);
    }
    else if (name == "setenv")
    {
        setEnvironmentCommand(args);
    }
    else if (name == "unsetenv")
    {
        unsetEnvironmentCommand(args);
    }
    else if (name == "which")
    {
        if (args.size() != 1)
        {
            std::cout << "Usage: which <command>\n";
        }
        else
        {
            std::string resolved = findExecutable(args[0]);
            std::cout << (resolved.empty() ? "Not found" : resolved) << "\n";
        }
    }
    else if (name == "history")
    {
        for (std::size_t index = 0; index < g_history.size(); ++index)
        {
            std::cout << std::setw(4) << index + 1 << "  " << g_history[index] << "\n";
        }
    }
    else if (name == "clear" || name == "cls")
    {
        clearScreen();
    }
    else if (name == "sleep")
    {
        if (args.size() != 1)
        {
            std::cout << "Usage: sleep <milliseconds>\n";
        }
        else
        {
            Sleep(static_cast<DWORD>(std::stoul(args[0])));
        }
    }
    else if (name == "run")
    {
        runScript(args);
    }
    else
    {
        return false;
    }

    return true;
}

bool executeLine(const std::string &line, bool fromScript)
{
    ParsedCommand command = parseCommandLine(line);
    if (command.tokens.empty())
    {
        return true;
    }

    if (runBuiltin(command))
    {
        return true;
    }

    bool executed = createChildProcess(command);
    if (!executed && fromScript)
    {
        std::cout << "Script command failed: " << line << "\n";
    }
    return executed;
}

void shutdownBackgroundJobs()
{
    refreshBackgroundJobs();

    EnterCriticalSection(&g_stateLock);

    for (auto &job : g_jobs)
    {
        if (job.handlesOpen && job.status != JobStatus::Exited)
        {
            TerminateProcess(job.processInfo.hProcess, 1);
            WaitForSingleObject(job.processInfo.hProcess, 1000);
            job.status = JobStatus::Exited;
            job.exitCode = 1;
        }
        closeJobHandles(job);
    }

    g_jobs.clear();
    LeaveCriticalSection(&g_stateLock);
}

int main()
{
    InitializeCriticalSection(&g_stateLock);
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);

    printBanner();

    while (!g_shouldExit)
    {
        refreshBackgroundJobs();

        std::cout << "myShell> " << std::flush;
        std::string line;

        if (!std::getline(std::cin, line))
        {
            std::cout << "\nEnd of input. Exiting myShell.\n";
            break;
        }

        line = trim(line);
        if (line.empty())
        {
            continue;
        }

        g_history.push_back(line);
        executeLine(line, false);
    }

    std::cout << "Sending kill signal to all tracked background processes...\n";
    shutdownBackgroundJobs();

    DeleteCriticalSection(&g_stateLock);
    std::cout << "Bye.\n";
    return 0;
}
