#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int parseDuration(int argc, char *argv[])
{
    if (argc < 2)
    {
        return 30;
    }

    try
    {
        int value = std::stoi(argv[1]);
        if (value < 1)
        {
            return 30;
        }
        if (value > 300)
        {
            return 300;
        }
        return value;
    }
    catch (const std::exception &)
    {
        return 30;
    }
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
        std::cout << "\n\n";
        return;
    }

    DWORD cellCount = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
    DWORD written = 0;
    COORD home{0, 0};

    FillConsoleOutputCharacterA(output, ' ', cellCount, home, &written);
    FillConsoleOutputAttribute(output, info.wAttributes, cellCount, home, &written);
    SetConsoleCursorPosition(output, home);
}

void printFrame(const std::vector<std::string> &frame, int second, int totalSeconds)
{
    clearScreen();
    std::cout << "myShell ASCII Worker\n";
    std::cout << "PID demo process - running for " << totalSeconds << " seconds\n";
    std::cout << "Second " << std::setw(2) << second << " / " << totalSeconds << "\n\n";

    for (const std::string &line : frame)
    {
        std::cout << line << "\n";
    }

    std::cout << "\nThis is a real child process created by myShell.\n";
    std::cout << "Try: list, stop <pid>, resume <pid>, kill <pid>\n";
    std::cout.flush();
}

int main(int argc, char *argv[])
{
    SetConsoleTitleA("myShell ASCII Worker");

    const int totalSeconds = parseDuration(argc, argv);
    const std::vector<std::vector<std::string>> frames = {
        {
            "        /\\",
            "       /  \\",
            "      /____\\",
            "      |    |",
            "      | OS |",
            "      |____|",
            "       /\\/\\",
            "      /_||_\\",
        },
        {
            "          /\\",
            "         /  \\",
            "        /____\\",
            "        |    |",
            "        | OS |",
            "        |____|",
            "         /\\/\\",
            "      ~~/_||_\\~~",
        },
        {
            "            /\\",
            "           /  \\",
            "          /____\\",
            "          |    |",
            "          | OS |",
            "          |____|",
            "           /\\/\\",
            "     ~~~~ /_||_\\ ~~~~",
        },
        {
            "              /\\",
            "             /  \\",
            "            /____\\",
            "            |    |",
            "            | OS |",
            "            |____|",
            "             /\\/\\",
            "   ~~~~~~~~ /_||_\\ ~~~~~~~~",
        },
    };

    for (int second = 1; second <= totalSeconds; ++second)
    {
        const auto &frame = frames[static_cast<std::size_t>((second - 1) % frames.size())];
        printFrame(frame, second, totalSeconds);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    clearScreen();
    std::cout << "myShell ASCII Worker finished after " << totalSeconds << " seconds.\n";
    return EXIT_SUCCESS;
}
