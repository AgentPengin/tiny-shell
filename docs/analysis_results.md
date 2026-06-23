# Báo Cáo Phân Tích & Phản Biện Dự Án Tiny Shell

Tài liệu này phân tích chi tiết mã nguồn hiện tại của dự án Tiny Shell (`src/main.cpp`), chỉ ra các lỗi tiềm ẩn (bugs), các điểm thiết kế chưa tối ưu (architectural critiques), đề xuất các giải pháp sửa đổi cụ thể và đưa ra lời khuyên để nâng cấp dự án đạt điểm tối đa trong môn học Hệ điều hành.

---

## 1. Các Lỗi Tiềm Ẩn & Vấn Đề Kỹ Thuật (Bugs & Issues)

### 1.1. Lỗi Crash Shell Khi Sử Dụng Lệnh `sleep` Không Đúng Tham Số
- **Mô tả:** Trong hàm `runBuiltin` (dòng 1083-1093), lệnh `sleep` thực hiện chuyển đổi đối số sang số nguyên không dấu thông qua `std::stoul(args[0])`.
- **Hệ quả:** Nếu người dùng nhập tham số không phải là số (ví dụ: `sleep abc` hoặc số âm `sleep -100`), hàm `std::stoul` sẽ ném ra ngoại lệ `std::invalid_argument` hoặc `std::out_of_range`. Do khối lệnh này không được bao quanh bởi `try-catch`, ngoại lệ không được xử lý sẽ dẫn đến việc chương trình shell bị crash và thoát đột ngột.
- **Dòng code lỗi:** 
  ```cpp
  // src/main.cpp:1091
  Sleep(static_cast<DWORD>(std::stoul(args[0])));
  ```

### 1.2. Rò Rỉ Tài Nguyên Hệ Thống (Kernel Handle Leak) Từ Tiến Trình Nền
- **Mô tả:** Khi một tiến trình chạy nền (background job) kết thúc, hàm `refreshBackgroundJobs()` (dòng 249) sẽ phát hiện và cập nhật trạng thái của tiến trình đó thành `JobStatus::Exited`. Tuy nhiên, hàm này không hề đóng các handle tiến trình (`hProcess`) và luồng (`hThread`) của tiến trình con.
- **Hệ quả:** Trong hệ điều hành Windows, cho dù một tiến trình đã thoát, đối tượng tiến trình (Process Object) vẫn được giữ lại trong nhân hệ điều hành (Kernel) nếu như vẫn còn ít nhất một handle mở trỏ tới nó. Nếu người dùng chạy nhiều tiến trình nền mà không chủ động gõ lệnh `reap`, hệ thống sẽ bị rò rỉ tài nguyên Kernel Handle nghiêm trọng.
- **Dòng code liên quan:**
  ```cpp
  // src/main.cpp:260-265
  if (GetExitCodeProcess(job.processInfo.hProcess, &exitCode) && exitCode != STILL_ACTIVE)
  {
      job.status = JobStatus::Exited;
      job.exitCode = exitCode;
      // Thiếu việc đóng handle ở đây!
  }
  ```

### 1.3. Lỗi Race Condition & Use-After-Free Khi Nhấn Ctrl+C
- **Mô tả:** Khi người dùng nhấn tổ hợp phím `Ctrl+C`, luồng xử lý tín hiệu điều khiển của Windows (`consoleControlHandler`, dòng 426) chạy song song với luồng chính của shell. Luồng này lấy handle của foreground process từ biến toàn cục `g_foreground.handle` và gọi `TerminateProcess`.
- **Hệ quả:** Có một khoảng trống thời gian nhỏ (Race Window): Nếu tiến trình foreground vừa kết thúc, luồng chính của shell nhận được tín hiệu từ `WaitForSingleObject`, tiến hành xóa thông tin foreground và đóng handle bằng `CloseHandle(processInfo.hProcess)`. Nhưng trước khi luồng chính kịp gán `g_foreground.handle = nullptr`, luồng tín hiệu đã đọc được handle cũ và gọi `TerminateProcess`.
- Tệ hơn, nếu handle cũ đã bị hệ thống thu hồi và cấp phát lại cho một tiến trình khác (Handle Recycling), shell sẽ vô tình chấm dứt nhầm một tiến trình hoàn toàn khác của hệ thống.

### 1.4. Lỗi Tìm Kiếm File Thực Thi Khi Có Đường Dẫn Thư Mục (Path Resolution Bug)
- **Mô tả:** Trong hàm `findExecutable` (dòng 302), nếu người dùng gõ lệnh có chứa đường dẫn thư mục (ví dụ `scripts\hello` để chạy file batch mà không gõ đuôi `.bat`), hàm này kiểm tra `commandPath.has_parent_path()` và ngay lập tức gọi `fs::exists(absolutePath)`.
- **Hệ quả:** Vì file `scripts\hello` không tồn tại (chỉ có `scripts\hello.bat`), hàm trả về chuỗi rỗng `""`. Kết quả là lệnh `which scripts\hello` báo không tìm thấy, và cơ chế tự động phát hiện lệnh batch `isBatchCommand` hoạt động không đúng, dẫn đến việc shell không thể chạy file batch này nếu không gõ rõ đuôi mở rộng.
- **Dòng code lỗi:**
  ```cpp
  // src/main.cpp:307-315
  if (commandPath.has_parent_path())
  {
      fs::path absolutePath = fs::absolute(commandPath, error);
      if (!error && fs::exists(absolutePath, error))
      {
          return absolutePath.string();
      }
      return ""; // Trả về luôn mà không thử nối đuôi mở rộng trong PATHEXT!
  }
  ```

---

## 2. Ý Kiến Phản Biện & Đánh Giá Thiết Kế (Design Critiques)

### 2.1. Phản Biện Việc Dùng `TerminateProcess` Làm Cơ Chế Ctrl+C Mặc Định
- **Thiết kế hiện tại:** Shell sử dụng cờ tạo tiến trình `CREATE_NEW_PROCESS_GROUP` cho mọi tiến trình. Khi người dùng nhấn Ctrl+C, shell bắt tín hiệu và gọi `TerminateProcess` lên tiến trình foreground.
- **Phản biện:** 
  - `TerminateProcess` là một lệnh hủy tiến trình thô bạo (tương đương `kill -9` trong Unix). Nó không cho phép tiến trình con giải phóng bộ nhớ, đóng file đang ghi dở, giải phóng kết nối mạng hay thực thi các hàm hủy (destructors). Điều này rất dễ gây mất dữ liệu hoặc hỏng file.
  - Trên Windows, cách ứng xử chuẩn mực là cho phép tiến trình con tự nhận tín hiệu Ctrl+C và thoát một cách duyên dáng (graceful shutdown).
- **Đề xuất sửa đổi:** Đối với tiến trình **foreground**, không nên truyền cờ `CREATE_NEW_PROCESS_GROUP`. Hãy để nó chia sẻ nhóm console với shell. Khi nhấn Ctrl+C, Windows sẽ tự động gửi sự kiện tới cả shell và tiến trình con. Shell có handler trả về `TRUE` để bỏ qua sự kiện, còn tiến trình con nhận sự kiện sẽ tự động thoát một cách tự nhiên. Cờ `CREATE_NEW_PROCESS_GROUP` chỉ nên dùng cho tiến trình **background** để chúng không bị ảnh hưởng bởi Ctrl+C của shell.

### 2.2. Nguy Cơ Tái Sử Dụng PID (PID Recycling) Khi Dùng Lệnh `kill`
- **Thiết kế hiện tại:** Khi thực hiện lệnh `kill <pid>`, shell gọi `OpenProcess(..., pid)` để lấy handle mới của tiến trình và chấm dứt nó.
- **Phản biện:** 
  - PID trong Windows là số nguyên và thường xuyên được tái sử dụng bởi hệ thống ngay sau khi tiến trình sở hữu nó kết thúc. Nếu tiến trình nền của shell đã kết thúc từ trước và hệ điều hành đã cấp PID đó cho một ứng dụng khác (ví dụ: `chrome.exe`), lệnh `kill <pid>` của shell sẽ vô tình chấm dứt tiến trình vô tội kia.
- **Đề xuất sửa đổi:** Do shell luôn theo dõi các tiến trình nền trong danh sách `g_jobs` và đã giữ sẵn handle tiến trình thông qua `processInfo.hProcess`, shell nên sử dụng trực tiếp handle này để tương tác thay vì gọi `OpenProcess` theo PID. Trình quản lý tiến trình của Windows đảm bảo handle cũ sẽ luôn chỉ tới tiến trình gốc (kể cả khi đã chết) chứ không bao giờ trỏ sang tiến trình mới được cấp trùng PID.

### 2.3. Cơ Chế Tạm Dừng/Tiếp Tục Tiến Trình (`stop`/`resume`) Không Đảm Bảo Tính Nguyên Tử (Non-atomic)
- **Thiết kế hiện tại:** Shell tạm dừng tiến trình bằng cách duyệt qua tất cả các luồng (thread) trong hệ thống thông qua `CreateToolhelp32Snapshot`, tìm các luồng thuộc PID mục tiêu và gọi `SuspendThread()`.
- **Phản biện:**
  - Giải pháp này không nguyên tử (non-atomic). Nếu tiến trình đích tạo ra luồng mới *ngay sau* khi snapshot được chụp, luồng mới này sẽ bị bỏ sót và tiến trình vẫn tiếp tục chạy.
  - Ngoài ra, việc duyệt qua hàng trăm luồng của hệ thống bằng snapshot gây tốn tài nguyên CPU một cách không cần thiết.
- **Đề xuất sửa đổi:** Sử dụng các hàm nội bộ không công khai của hệ thống nhưng cực kỳ ổn định là `NtSuspendProcess` và `NtResumeProcess` từ thư viện liên kết động `ntdll.dll`. Đây là giải pháp mà các công cụ chuyên nghiệp như Process Explorer hay debuggers của Windows sử dụng để đảm bảo tính nguyên tử tuyệt đối.

### 2.4. Ô Nhiễm Lịch Sử Lệnh (Command History Pollution)
- **Thiết kế hiện tại:** Mỗi dòng lệnh đọc từ file script `.tsh` đều được đẩy vào `g_history`.
- **Phản biện:** Khi người dùng chạy một script dài hàng trăm dòng, lịch sử lệnh của họ sẽ bị ngập tràn bởi các lệnh con trong script. Thông thường, một shell chỉ nên lưu lệnh chạy script (ví dụ `run scripts/demo.tsh`) thay vì ghi lại mọi dòng lệnh bên trong script đó.

---

## 3. Giải Pháp Chỉnh Sửa Chi Tiết & Mã Nguồn Minh Họa

### 3.1. Sửa Lỗi Crash Của Lệnh `sleep`
Bao bọc việc chuyển đổi kiểu dữ liệu bằng `try-catch` để bắt lỗi nhập liệu sai của người dùng:
```cpp
else if (name == "sleep")
{
    if (args.size() != 1)
    {
        std::cout << "Usage: sleep <milliseconds>\n";
    }
    else
    {
        try
        {
            unsigned long ms = std::stoul(args[0]);
            Sleep(static_cast<DWORD>(ms));
        }
        catch (const std::exception &e)
        {
            std::cout << "Error: Invalid milliseconds value.\n";
        }
    }
}
```

### 3.2. Đóng Handle Ngay Khi Phát Hiện Tiến Trình Nền Thoát
Sửa đổi hàm `refreshBackgroundJobs()` để chủ động giải phóng tài nguyên mà không cần đợi lệnh `reap`:
```cpp
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
            closeJobHandles(job); // Đóng handle ngay lập tức để giải phóng tài nguyên Kernel!
        }
    }

    LeaveCriticalSection(&g_stateLock);
}
```

### 3.3. Đồng Bộ Hóa An Toàn Cho Xử Lý Ctrl+C Tránh Use-After-Free
Để ngăn ngừa tình trạng Handle bị đóng trong luồng chính khi Handler đang cố sử dụng nó, ta thực hiện đóng handle bên trong Critical Section ở cả hai luồng:

**Trong hàm `consoleControlHandler`:**
```cpp
BOOL WINAPI consoleControlHandler(DWORD controlType)
{
    if (controlType != CTRL_C_EVENT && controlType != CTRL_BREAK_EVENT)
    {
        return FALSE;
    }

    EnterCriticalSection(&g_stateLock);
    if (g_foreground.handle != nullptr)
    {
        // Gửi tín hiệu dừng thay vì terminate thô bạo nếu có thể, 
        // ở đây vẫn dùng TerminateProcess nhưng bảo vệ handle
        TerminateProcess(g_foreground.handle, CONTROL_C_EXIT);
        std::cout << "\n[Ctrl+C] Foreground process " << g_foreground.pid << " was terminated.\n";
        LeaveCriticalSection(&g_stateLock);
        return TRUE;
    }
    LeaveCriticalSection(&g_stateLock);

    std::cout << "\nNo foreground process is running. Type 'exit' to leave myShell.\n";
    return TRUE;
}
```

**Trong hàm `createChildProcess`:**
```cpp
// Sau khi tiến trình foreground thoát:
rememberForegroundProcess(processInfo, command.raw);
WaitForSingleObject(processInfo.hProcess, INFINITE);

DWORD exitCode = 0;
GetExitCodeProcess(processInfo.hProcess, &exitCode);

// Thực hiện dọn dẹp biến toàn cục và đóng handle một cách an toàn dưới Critical Section
EnterCriticalSection(&g_stateLock);
g_foreground.handle = nullptr;
g_foreground.pid = 0;
g_foreground.command.clear();
LeaveCriticalSection(&g_stateLock);

CloseHandle(processInfo.hThread);
CloseHandle(processInfo.hProcess); // Đóng handle sau khi đã hủy tham chiếu trong g_foreground
```

### 3.4. Cải Tiến Tìm Kiếm File Thực Thi Khi Có Đường Dẫn Thư Mục
Sửa hàm `findExecutable` để hỗ trợ thử các đuôi mở rộng khi đường dẫn chứa thư mục cha:
```cpp
std::string findExecutable(const std::string &command)
{
    fs::path commandPath(command);
    std::error_code error;

    std::vector<std::string> candidates;
    std::vector<std::string> extensions{""};
    for (const std::string &extension : splitSemicolonList(getEnvironmentValue("PATHEXT")))
    {
        extensions.push_back(extension);
    }

    if (commandPath.has_parent_path())
    {
        // Nếu có thư mục cha, thử tìm trực tiếp với các đuôi mở rộng khác nhau
        for (const std::string &extension : extensions)
        {
            fs::path withExtension = commandPath;
            if (!extension.empty())
            {
                withExtension += extension;
            }
            if (fs::exists(withExtension, error) && !fs::is_directory(withExtension, error))
            {
                return fs::absolute(withExtension, error).string();
            }
        }
        return "";
    }

    // Phần logic tìm kiếm trong thư mục hiện tại và PATH giữ nguyên...
    // (Dòng 317-360 cũ)
}
```

### 3.5. Sử Dụng API Không Công Khai `NtSuspendProcess` Để Tạm Dừng Nguyên Tử
Thay thế cho vòng lặp duyệt thread bằng snapshot, giải pháp này cực kỳ chuyên nghiệp và an toàn:
```cpp
// Khai báo prototype của hàm NtSuspendProcess / NtResumeProcess từ ntdll.dll
typedef LONG(NTAPI *pfnNtSuspendProcess)(HANDLE ProcessHandle);
typedef LONG(NTAPI *pfnNtResumeProcess)(HANDLE ProcessHandle);

bool changeProcessStateNt(DWORD pid, bool suspend)
{
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return false;

    auto NtSuspendProcess = (pfnNtSuspendProcess)GetProcAddress(hNtdll, "NtSuspendProcess");
    auto NtResumeProcess = (pfnNtResumeProcess)GetProcAddress(hNtdll, "NtResumeProcess");

    if (!NtSuspendProcess || !NtResumeProcess) return false;

    // Sử dụng trực tiếp handle từ danh sách jobs để tránh PID recycling
    HANDLE hProcess = nullptr;
    EnterCriticalSection(&g_stateLock);
    BackgroundJob* job = findJobByPid(pid);
    if (job && job->handlesOpen)
    {
        hProcess = job->processInfo.hProcess;
    }
    LeaveCriticalSection(&g_stateLock);

    // Nếu không nằm trong danh sách jobs, mới gọi OpenProcess (fallback)
    bool needClose = false;
    if (!hProcess)
    {
        hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
        if (!hProcess) return false;
        needClose = true;
    }

    LONG status = -1;
    if (suspend)
    {
        status = NtSuspendProcess(hProcess);
    }
    else
    {
        status = NtResumeProcess(hProcess);
    }

    if (needClose)
    {
        CloseHandle(hProcess);
    }

    return status >= 0; // Trả về true nếu thành công (STATUS_SUCCESS = 0)
}
```

---

## 4. Các Đề Xuất Nâng Cấp Nâng Cao (Recommendations for Future Enhancements)

Nếu muốn biến dự án Tiny Shell này thành một đồ án xuất sắc và gây ấn tượng mạnh với giảng viên, bạn nên triển khai thêm các tính năng sau:

### 4.1. Tự Triển Khai Redirection (`>`, `<`) Không Qua `cmd.exe`
Hiện tại, shell chuyển hướng file bằng cách đẩy toàn bộ dòng lệnh sang `cmd.exe /C`. Thay vào đó, bạn có thể tự quản lý bằng cách:
1. Phát hiện ký tự `>` hoặc `<` trong danh sách các tokens.
2. Mở file tương ứng bằng `CreateFileA()` để lấy handle file.
3. Gán handle file này vào trường `hStdOutput` hoặc `hStdInput` của struct `STARTUPINFOA`.
4. Thiết lập cờ `STARTF_USESTDHANDLES` trong trường `dwFlags` của `STARTUPINFOA`.
5. Tạo tiến trình con bình thường bằng `CreateProcessA`. Windows sẽ tự động chuyển hướng luồng I/O của tiến trình con vào file đó.

### 4.2. Tự Triển Khai Đường Ống (Piping `|`) Bằng `CreatePipe`
Tương tự như Redirection, bạn có thể thực hiện liên kết ngõ ra của tiến trình này với ngõ vào của tiến trình kia:
1. Gọi `CreatePipe()` để tạo một đường ống nặc danh (anonymous pipe) gồm 2 đầu: đầu đọc (`hRead`) và đầu ghi (`hWrite`).
2. Với tiến trình A (phía trước dấu `|`): Thiết lập `hStdOutput` là đầu ghi `hWrite`.
3. Với tiến trình B (phía sau dấu `|`): Thiết lập `hStdInput` là đầu đọc `hRead`.
4. Gọi `CreateProcessA` cho cả hai tiến trình và đóng các đầu ống dư thừa trong tiến trình cha (shell) để luồng dữ liệu tự động kết thúc khi tiến trình A hoàn tất.

### 4.3. Quản Lý Tiến Trình Nền Theo Job ID
Thay vì bắt người dùng phải nhập PID dài dòng (ví dụ `kill 18432`), hãy lưu trữ thêm một số thứ tự tăng dần kiểu `Job ID` (ví dụ: `[1]`, `[2]`). Người dùng chỉ cần gõ `kill %1` hoặc `stop %2`, shell sẽ tự động tra cứu từ Job ID sang PID để thực hiện lệnh. Điều này mô phỏng chính xác hành vi của các shell tiêu chuẩn như Bash.
