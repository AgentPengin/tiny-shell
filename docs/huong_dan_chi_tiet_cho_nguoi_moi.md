# Hướng Dẫn Chi Tiết Tiny Shell Cho Người Mới

Tài liệu này đọc song song với file `src/main.cpp`. Mục tiêu là giúp người mới hiểu:

- Từng khối code dùng để làm gì.
- Vì sao shell có thể chạy chương trình khác.
- Foreground/background khác nhau ở đâu.
- Các API Windows hoạt động như thế nào.
- Tại sao `kill`, `stop`, `resume`, Ctrl+C và `.bat` chạy được.

Các số dòng dưới đây khớp với `src/main.cpp` tại thời điểm hoàn thiện project này. Những dòng chỉ là `{`, `}`, dòng trắng hoặc dấu chấm phẩy kết thúc cấu trúc sẽ được gom vào cụm dòng vì chúng không mang logic riêng.

---

## 1. Cách chạy project từ đầu

Mở PowerShell tại thư mục project:

```powershell
cd "D:\Hust\Operating system\IT3070\Tiny Shell"
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
.\build\myShell.exe
```

Nếu chạy được, màn hình sẽ có prompt:

```text
myShell>
```

Nhập:

```text
help
```

để xem lệnh hỗ trợ.

---

## 2. Tư duy cốt lõi: shell thật ra làm gì?

Một shell tối giản chỉ cần làm 5 việc:

1. In prompt.
2. Đọc một dòng từ bàn phím.
3. Phân tích dòng đó thành lệnh và tham số.
4. Nếu là lệnh nội bộ, tự xử lý.
5. Nếu không phải lệnh nội bộ, nhờ hệ điều hành tạo tiến trình con.

Trong project:

- Bước 1, 2 nằm ở `main()`, dòng 1156-1176.
- Bước 3 nằm ở `parseCommandLine()`, dòng 171-187.
- Bước 4 nằm ở `runBuiltin()`, dòng 978-1104.
- Bước 5 nằm ở `createChildProcess()`, dòng 452-516.

---

## 3. Bản đồ source code

| Dòng | Nội dung |
| --- | --- |
| 1-19 | Macro, thư viện và alias `fs`. |
| 21-56 | Enum, struct và biến toàn cục quản lý trạng thái shell. |
| 58-187 | Hàm tiện ích để xử lý chuỗi và parse lệnh. |
| 191-220 | Đọc/ghi biến môi trường Windows. |
| 222-281 | Quản lý trạng thái và handle của background job. |
| 283-405 | Parse PID, tìm executable, nhận diện `.bat`, build command line. |
| 408-450 | Lưu/xóa foreground process và xử lý Ctrl+C. |
| 452-516 | Tạo tiến trình con bằng `CreateProcessA()`. |
| 518-568 | Suspend/resume thread của một process. |
| 571-680 | `list`, `reap`, `kill`, `stop`, `resume`. |
| 686-894 | Built-in tiện ích: `dir`, `cd`, `clear`, `date`, `time`, `path`, `env`. |
| 896-939 | `help` và banner lúc khởi động. |
| 943-976 | Chạy file script `.tsh`. |
| 978-1104 | Dispatcher của built-in commands. |
| 1106-1125 | Chạy một dòng lệnh hoàn chỉnh. |
| 1127-1147 | Dọn background jobs khi thoát shell. |
| 1149-1185 | Hàm `main()` và vòng lặp shell. |

---

## 4. Giải thích các kiểu dữ liệu chính

### Dòng 21-26: `enum class JobStatus`

```cpp
enum class JobStatus
{
    Running,
    Suspended,
    Exited
};
```

- `Running`: tiến trình background đang chạy.
- `Suspended`: shell đã gọi `SuspendThread()` lên các thread của tiến trình.
- `Exited`: tiến trình đã kết thúc.

Vì sao cần enum? Nếu chỉ dùng chuỗi `"Running"` hoặc số `0`, `1`, `2`, code dễ sai và khó đọc. `enum class` buộc lập trình viên dùng đúng nhóm trạng thái.

### Dòng 28-33: `ParsedCommand`

`ParsedCommand` là kết quả sau khi shell phân tích input.

- `raw`: lệnh gốc sau khi bỏ khoảng trắng thừa và bỏ dấu `&` cuối nếu có.
- `tokens`: danh sách từ đã tách, ví dụ `["ping", "-n", "2"]`.
- `background`: `true` nếu người dùng thêm `&` cuối lệnh.

### Dòng 35-43: `BackgroundJob`

Struct này lưu một process nền.

- `pid`: Process ID để người dùng thao tác.
- `processInfo`: chứa `hProcess`, `hThread`, `dwProcessId`, `dwThreadId` do Windows trả về.
- `command`: dòng lệnh gốc để `list` in lại.
- `status`: trạng thái shell đang biết.
- `exitCode`: mã thoát nếu process đã kết thúc.
- `handlesOpen`: tránh gọi `CloseHandle()` hai lần.

### Dòng 45-50: `ForegroundProcess`

Foreground process là process shell đang chờ. Shell lưu `handle` và `pid` để khi nhấn Ctrl+C, handler biết phải hủy process nào.

### Dòng 52-56: biến toàn cục

- `g_stateLock`: Critical Section bảo vệ dữ liệu dùng chung.
- `g_foreground`: foreground process hiện tại.
- `g_jobs`: danh sách background process.
- `g_history`: lịch sử lệnh.
- `g_shouldExit`: cờ yêu cầu thoát vòng lặp shell.

Critical Section cần thiết vì Ctrl+C handler có thể chạy trong luồng khác trong khi vòng lặp shell đang thao tác `g_foreground` hoặc `g_jobs`.

---

## 5. Giải thích từng hàm xử lý chuỗi và parse lệnh

### `trim()` - dòng 58-74

Mục đích: bỏ khoảng trắng đầu và cuối chuỗi.

- Dòng 60-62: tìm ký tự đầu tiên không phải khoảng trắng.
- Dòng 64-67: nếu toàn bộ chuỗi là khoảng trắng, trả về chuỗi rỗng.
- Dòng 69-71: tìm ký tự cuối cùng không phải khoảng trắng bằng iterator đảo.
- Dòng 73: tạo chuỗi mới từ vị trí đầu đến vị trí cuối.

Vì sao cần `trim()`? Người dùng có thể nhập `   help   `. Nếu không trim, shell có thể hiểu sai tên lệnh.

### `toLower()` - dòng 76-82

Mục đích: đổi chuỗi về chữ thường để so sánh lệnh không phân biệt hoa/thường.

- Dòng 78-80: `std::transform()` duyệt từng ký tự.
- `std::tolower()` đổi ký tự sang lower-case.
- Dòng 81: trả về chuỗi đã đổi.

Nhờ vậy `HELP`, `Help`, `help` đều chạy như nhau.

### `equalsIgnoreCase()` - dòng 84-87

Hàm này gọi `toLower()` cho hai chuỗi rồi so sánh. Nó được dùng khi kiểm tra PATH đã có một thư mục hay chưa.

### `startsWithIgnoreCase()` - dòng 89-96

Hàm này kiểm tra chuỗi có bắt đầu bằng prefix hay không, không phân biệt hoa/thường.

- Dòng 91-94: nếu prefix dài hơn text thì chắc chắn sai.
- Dòng 95: lấy đoạn đầu của text và so sánh bằng `equalsIgnoreCase()`.

Hàm này dùng khi đọc script để bỏ qua dòng comment kiểu `rem ...`.

### `splitSemicolonList()` - dòng 98-114

Mục đích: tách một danh sách phân tách bằng dấu `;`.

Windows dùng `;` trong PATH:

```text
C:\Windows;C:\Windows\System32;C:\MinGW\bin
```

- Dòng 101: tạo `stringstream` từ chuỗi.
- Dòng 104: đọc từng phần đến dấu `;`.
- Dòng 106: trim từng phần.
- Dòng 107-110: bỏ phần rỗng, lưu phần hợp lệ.

### `tokenize()` - dòng 116-149

Mục đích: tách input thành các token nhưng vẫn giữ nội dung trong dấu nháy kép.

Ví dụ:

```text
setenv NAME "Hello World"
```

thành:

```text
setenv
NAME
Hello World
```

Giải thích:

- Dòng 119-120: tạo danh sách token và token đang xây.
- Dòng 121: `inQuotes` cho biết đang ở trong dấu `"..."` hay không.
- Dòng 123: duyệt từng ký tự.
- Dòng 125-129: gặp dấu `"` thì đảo trạng thái `inQuotes`, không đưa dấu `"` vào token.
- Dòng 132-141: nếu gặp khoảng trắng ngoài dấu nháy thì kết thúc token hiện tại.
- Dòng 143: ký tự bình thường được thêm vào token hiện tại.
- Dòng 146-149: hết vòng lặp thì đẩy token cuối cùng nếu còn.

### `endsWithBackgroundMarker()` - dòng 151-169

Mục đích: kiểm tra lệnh có kết thúc bằng `&` không.

- Dòng 153: theo dõi có đang ở trong dấu nháy không.
- Dòng 154: lưu vị trí ký tự cuối không phải khoảng trắng.
- Dòng 156-166: duyệt toàn bộ chuỗi, cập nhật trạng thái dấu nháy và vị trí ký tự cuối.
- Dòng 168: chỉ coi là background nếu ký tự cuối là `&` và không nằm trong dấu nháy.

Vì sao cần kiểm tra dấu nháy? Chuỗi `"Tom & Jerry"` không phải yêu cầu chạy background.

### `parseCommandLine()` - dòng 171-187

Đây là hàm parse chính.

- Dòng 174: trim input.
- Dòng 176-179: nếu rỗng thì trả kết quả rỗng.
- Dòng 181-185: nếu cuối dòng là `&`, bật `background = true` và xóa `&` khỏi lệnh raw.
- Dòng 187: gọi `tokenize()` để lấy danh sách token.

Kết quả của hàm này quyết định nhánh chạy foreground hay background.

---

## 6. Giải thích biến môi trường

### `getEnvironmentValue()` - dòng 191-208

Mục đích: đọc biến môi trường Windows.

- Dòng 193: gọi `GetEnvironmentVariableA(name, nullptr, 0)` để hỏi Windows cần buffer bao nhiêu ký tự.
- Dòng 194-197: nếu size bằng 0 thì biến không tồn tại hoặc rỗng.
- Dòng 199: tạo string đủ lớn.
- Dòng 200: gọi lại `GetEnvironmentVariableA()` để lấy giá trị thật.
- Dòng 202-205: xóa ký tự null cuối chuỗi.
- Dòng 207: trả về giá trị.

Vì sao gọi hai lần? Vì giá trị PATH có thể rất dài. Gọi lần đầu để biết kích thước giúp tránh tràn buffer.

### `setEnvironmentValue()` - dòng 210-220

Mục đích: đặt biến môi trường.

- Dòng 212: gọi `SetEnvironmentVariableA()`, cập nhật môi trường WinAPI.
- Dòng 214: nếu lỗi, in mã lỗi Windows.
- Dòng 218: gọi `_putenv_s()` để đồng bộ môi trường C runtime.

Vì sao cần cả hai? Một số thư viện C/C++ đọc env qua C runtime, còn process con nhận env từ Windows. Gọi cả hai giúp nhất quán hơn.

---

## 7. Giải thích quản lý background job

### `statusToString()` - dòng 222-235

Chuyển enum `JobStatus` thành chuỗi để in.

- `Running` -> `"Running"`.
- `Suspended` -> `"Suspended"`.
- `Exited` -> `"Exited(<exitCode>)"`.

### `closeJobHandles()` - dòng 237-247

Mỗi process/thread Windows có handle. Nếu mở handle mà không đóng sẽ rò tài nguyên.

- Dòng 239-242: nếu đã đóng rồi thì không làm gì.
- Dòng 244-245: đóng thread handle và process handle.
- Dòng 246: đánh dấu đã đóng.

### `refreshBackgroundJobs()` - dòng 249-269

Mục đích: cập nhật process nào đã thoát.

- Dòng 251: vào Critical Section để bảo vệ `g_jobs`.
- Dòng 253: duyệt từng job.
- Dòng 255-258: bỏ qua job đã đóng handle hoặc đã exited.
- Dòng 261: gọi `GetExitCodeProcess()`.
- Nếu exit code khác `STILL_ACTIVE`, process đã kết thúc.
- Dòng 263-265: cập nhật trạng thái và exit code.
- Dòng 268: rời Critical Section.

### `findJobByPid()` - dòng 271-280

Tìm job trong `g_jobs` theo PID.

- Dòng 273: duyệt vector.
- Dòng 275-278: nếu PID khớp thì trả con trỏ đến job.
- Dòng 280: không tìm thấy thì trả `nullptr`.

Hàm này giúp `kill`, `stop`, `resume` cập nhật trạng thái job mà shell đang theo dõi.

### `parsePid()` - dòng 283-300

Mục đích: đổi chuỗi PID thành số `DWORD`.

- Dòng 285-288: lệnh process chỉ nhận đúng một đối số.
- Dòng 292: dùng `std::stoul()` để đổi chuỗi thành số nguyên không âm.
- Dòng 293: ép kiểu về `DWORD`.
- Dòng 297-300: nếu chuỗi không phải số, trả `false`.

---

## 8. Giải thích tìm executable và chạy `.bat`

### `findExecutable()` - dòng 302-361

Mục đích: tìm file thực thi theo thư mục hiện tại, PATH và PATHEXT.

Các bước:

1. Dòng 305-316: nếu command có thư mục, kiểm tra trực tiếp đường dẫn đó.
2. Dòng 318-319: thêm candidate ở thư mục hiện tại.
3. Dòng 321-324: thêm candidate từ từng thư mục trong PATH.
4. Dòng 326-330: lấy PATHEXT, ví dụ `.COM;.EXE;.BAT;.CMD`.
5. Dòng 332-358: thử từng candidate với từng extension.
6. Dòng 360: không thấy thì trả chuỗi rỗng.

Vì sao cần PATHEXT? Trên Windows người dùng nhập `cmd`, nhưng file thật là `cmd.exe`.

### `isBatchCommand()` - dòng 363-375

Mục đích: nhận diện lệnh `.bat` hoặc `.cmd`.

- Dòng 365-368: nếu không có token thì không phải batch.
- Dòng 371: tìm đường dẫn thật của token đầu.
- Dòng 372-374: lấy extension và kiểm tra `.bat`/`.cmd`.

### `containsCmdMetaCharacter()` - dòng 377-396

Mục đích: phát hiện ký tự đặc biệt của CMD như `>`, `<`, `|`, `&`.

Nếu chạy trực tiếp:

```text
ping -n 2 127.0.0.1 > nul
```

thì `CreateProcessA()` sẽ đưa `>` cho `ping.exe` như một tham số thường, không redirect output. Vì vậy khi có ký tự meta, shell chuyển lệnh qua `cmd.exe /C`.

### `buildCommandLine()` - dòng 398-405

Mục đích: quyết định dòng lệnh thật đưa vào `CreateProcessA()`.

- Nếu là batch, có ký tự meta, hoặc đang fallback sau lỗi, trả về `cmd.exe /C <raw>`.
- Nếu không, trả về chính lệnh raw.

---

## 9. Giải thích foreground và Ctrl+C

### `rememberForegroundProcess()` - dòng 408-415

Khi shell chạy foreground process, hàm này lưu:

- process handle
- PID
- command

Nó vào Critical Section trước khi ghi để tránh Ctrl+C handler đọc dữ liệu đang ghi dở.

### `clearForegroundProcess()` - dòng 417-424

Sau khi foreground process kết thúc, shell xóa thông tin foreground. Nhờ đó nếu người dùng nhấn Ctrl+C lúc không có foreground process, shell không hủy nhầm gì cả.

### `consoleControlHandler()` - dòng 426-450

Đây là hàm Windows gọi khi có Ctrl+C hoặc Ctrl+Break.

- Dòng 428-431: nếu không phải Ctrl+C/Ctrl+Break thì trả `FALSE`.
- Dòng 436-439: lấy foreground handle và PID trong Critical Section.
- Dòng 442: nếu có foreground process, gọi `TerminateProcess()`.
- Dòng 443: in thông báo.
- Dòng 444: trả `TRUE` để báo Windows rằng shell đã xử lý Ctrl+C.
- Dòng 447-448: nếu không có foreground process, shell không thoát.

Vì sao Ctrl+C không làm tắt shell? Vì `SetConsoleCtrlHandler()` ở dòng 1144 đã đăng ký handler này.

---

## 10. Giải thích tạo tiến trình

### `createChildProcess()` - dòng 452-516

Đây là trái tim của Tiny Shell.

- Dòng 454: build command line thật.
- Dòng 455-456: tạo buffer mutable vì `CreateProcessA()` cần `char*` có thể sửa.
- Dòng 458-459: tạo `STARTUPINFOA`, cho Windows biết cách khởi tạo process.
- Dòng 461: tạo `PROCESS_INFORMATION`, nơi Windows trả về handle và PID.
- Dòng 462: dùng `CREATE_NEW_PROCESS_GROUP`, giúp process con tách nhóm điều khiển console.
- Dòng 464: flush output để transcript không bị đảo thứ tự với output process con.
- Dòng 466-476: gọi `CreateProcessA()`.
- Dòng 478-481: nếu chạy trực tiếp lỗi, thử fallback qua `cmd.exe /C`.
- Dòng 483-487: nếu vẫn lỗi, in mã lỗi.
- Dòng 489-505: nếu là background, tạo `BackgroundJob`, lưu vào `g_jobs`, in PID và trả prompt.
- Dòng 507-508: nếu foreground, lưu process hiện tại vào `g_foreground`.
- Dòng 509: chờ process kết thúc bằng `WaitForSingleObject()`.
- Dòng 512: lấy exit code.
- Dòng 513: xóa foreground.
- Dòng 515-516: đóng handle.

Tại sao background không chờ? Vì nếu gọi `WaitForSingleObject()` thì shell bị khóa và không nhận lệnh mới.

---

## 11. Giải thích `stop` và `resume`

### `changeThreadState()` - dòng 518-568

Windows quản lý CPU ở mức thread. Muốn tạm dừng một process, project tạm dừng tất cả thread của process đó.

- Dòng 520: gọi `CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)` để lấy snapshot thread.
- Dòng 521-525: nếu snapshot lỗi, báo lỗi.
- Dòng 527-528: chuẩn bị `THREADENTRY32`.
- Dòng 530-535: lấy thread đầu tiên.
- Dòng 539: duyệt từng thread.
- Dòng 541-544: bỏ qua thread không thuộc PID cần xử lý.
- Dòng 546: mở thread bằng quyền `THREAD_SUSPEND_RESUME`.
- Dòng 552-555: nếu `suspend == true`, gọi `SuspendThread()`.
- Dòng 557-561: nếu resume, gọi `ResumeThread()` đến khi suspend count giảm.
- Dòng 564: đánh dấu đã chạm ít nhất một thread.
- Dòng 565: đóng thread handle.
- Dòng 568: trả về có xử lý được thread nào không.

Tại sao không gọi một hàm `SuspendProcess()`? Windows API công khai không cung cấp hàm đơn giản như vậy cho process thường, nên phải duyệt thread.

---

## 12. Giải thích lệnh quản lý tiến trình

### `listBackgroundJobs()` - dòng 571-597

- Dòng 573: cập nhật trạng thái job trước khi in.
- Dòng 575: khóa `g_jobs`.
- Dòng 577-582: nếu rỗng thì in thông báo.
- Dòng 585-589: in header bảng.
- Dòng 591-596: in từng PID, status, command.

### `reapExitedJobs()` - dòng 599-620

Mục đích: xóa job đã thoát khỏi bảng.

- Dòng 601: refresh trạng thái.
- Dòng 603-604: khóa và nhớ số lượng ban đầu.
- Dòng 606-614: `remove_if` chọn job đã `Exited`, đóng handle rồi đánh dấu xóa.
- Dòng 616: xóa thật khỏi vector.
- Dòng 617: tính số job đã dọn.

### `killProcessCommand()` - dòng 622-658

- Dòng 624-629: parse PID.
- Dòng 631: mở process bằng `OpenProcess()`.
- Dòng 633-637: nếu không mở được, in lỗi.
- Dòng 640: gọi `TerminateProcess()`.
- Dòng 648: chờ tối đa 1 giây để process kết thúc.
- Dòng 652-656: nếu PID thuộc job table, cập nhật trạng thái.

### `stopOrResumeProcessCommand()` - dòng 660-680

- Dòng 662-667: parse PID.
- Dòng 669-673: gọi `changeThreadState()`.
- Dòng 675-680: cập nhật status trong job table.
- Dòng 683: in kết quả.

---

## 13. Giải thích built-in tiện ích

### `printDirectory()` - dòng 686-718

Lệnh `dir [directory]`.

- Dòng 688: nếu không có đối số thì dùng thư mục hiện tại.
- Dòng 691-704: kiểm tra tồn tại và có phải thư mục không.
- Dòng 706-712: in header.
- Dòng 714-720: duyệt `filesystem::directory_iterator`, in loại file, size và tên.

### `changeDirectory()` - dòng 720-737

Lệnh `cd <directory>`.

- Kiểm tra đúng một đối số.
- Gọi `fs::current_path()` để đổi thư mục.
- Nếu lỗi, in message từ `std::error_code`.

### `clearScreen()` - dòng 739-760

Lệnh `clear`.

- Lấy handle console bằng `GetStdHandle()`.
- Lấy thông tin buffer bằng `GetConsoleScreenBufferInfo()`.
- Ghi khoảng trắng phủ toàn bộ console bằng `FillConsoleOutputCharacterA()`.
- Đưa cursor về góc trên trái bằng `SetConsoleCursorPosition()`.

### `printDateTime()` - dòng 762-778

Lệnh `date` và `time`.

- Dòng 764: gọi `GetLocalTime()`.
- Nếu `dateOnly`, in `dd/mm/yyyy`.
- Nếu không, in `hh:mm:ss`.

### `printPath()` - dòng 780-783

In biến môi trường `PATH` bằng `getEnvironmentValue("PATH")`.

### `addPath()` - dòng 785-810

Lệnh `addpath <directory>`.

- Kiểm tra đúng một đối số.
- Đọc PATH hiện tại.
- Tách PATH theo `;`.
- Nếu thư mục đã có, không thêm trùng.
- Nếu chưa có, nối vào cuối PATH và gọi `setEnvironmentValue()`.

Thay đổi này chỉ áp dụng cho phiên shell hiện tại và process con do shell tạo ra, không chỉnh PATH vĩnh viễn của Windows.

### `setPath()` - dòng 812-830

Thay toàn bộ PATH bằng giá trị mới. Lệnh này đáp ứng phần yêu cầu “xem và đặt lại biến môi trường”.

### `printEnvironment()` - dòng 832-859

- `env NAME`: in một biến.
- `env`: in tất cả biến môi trường.

### `setEnvironmentCommand()` - dòng 861-879

Lệnh `setenv <NAME> <VALUE>`. Nếu value có nhiều token, hàm ghép lại bằng dấu cách.

### `unsetEnvironmentCommand()` - dòng 881-894

Lệnh `unsetenv <NAME>`. Gọi `SetEnvironmentVariableA(name, nullptr)` để xóa biến.

---

## 14. Giải thích help, script và dispatcher

### `printHelp()` - dòng 896-928

In danh sách lệnh và ví dụ. Đây là built-in quan trọng để người dùng biết shell hỗ trợ gì.

### `printBanner()` - dòng 930-939

In thông tin khi khởi động, gồm PID của chính shell. PID này lấy bằng `GetCurrentProcessId()`.

### Khai báo trước `executeLine()` - dòng 941

`runScript()` cần gọi `executeLine()`, nhưng `executeLine()` được định nghĩa sau. C++ cần biết chữ ký hàm trước, nên có dòng khai báo trước.

### `runScript()` - dòng 943-976

Mục đích: đọc một file script `.tsh` và chạy từng dòng như người dùng nhập.

- Dòng 937-942: kiểm tra đúng một đối số.
- Dòng 944-949: mở file script.
- Dòng 954: đọc từng dòng.
- Dòng 958-961: bỏ qua dòng rỗng hoặc comment.
- Dòng 964: in số dòng script đang chạy.
- Dòng 966: lưu vào history.
- Dòng 967: gọi `executeLine()`.

### `runBuiltin()` - dòng 978-1104

Đây là dispatcher của lệnh nội bộ.

- Dòng 980-983: lệnh rỗng thì coi như xử lý xong.
- Dòng 985: đưa tên lệnh về chữ thường.
- Dòng 986: tách phần đối số.
- Dòng 988-1099: chuỗi `if/else` gọi hàm tương ứng.
- Dòng 1102: nếu không khớp built-in nào, trả `false`.
- Dòng 1104: nếu đã xử lý, trả `true`.

Ví dụ:

- `help` gọi `printHelp()`.
- `list` gọi `listBackgroundJobs()`.
- `kill` gọi `killProcessCommand()`.
- `dir` gọi `printDirectory()`.
- `run` gọi `runScript()`.

### `executeLine()` - dòng 1106-1125

Đây là hàm chạy một dòng hoàn chỉnh.

- Dòng 1108: parse input.
- Dòng 1109-1112: nếu không có token thì bỏ qua.
- Dòng 1114-1117: thử chạy built-in.
- Dòng 1119: nếu không phải built-in, gọi `createChildProcess()`.
- Dòng 1120-1123: nếu đang chạy script mà lệnh lỗi thì in thông báo.

---

## 15. Giải thích thoát shell và hàm main

### `shutdownBackgroundJobs()` - dòng 1127-1147

Khi người dùng gõ `exit`, shell không nên bỏ mặc background process.

- Dòng 1129: refresh trạng thái.
- Dòng 1131: khóa job table.
- Dòng 1133-1143: duyệt từng job.
- Nếu job còn chạy, gọi `TerminateProcess()`.
- Gọi `WaitForSingleObject()` chờ ngắn.
- Đóng handle bằng `closeJobHandles()`.
- Dòng 1145: xóa toàn bộ danh sách.

### `main()` - dòng 1149-1185

Đây là vòng đời của shell.

- Dòng 1151: khởi tạo Critical Section.
- Dòng 1152: đăng ký Ctrl+C handler.
- Dòng 1154: in banner.
- Dòng 1156: lặp cho đến khi `g_shouldExit == true`.
- Dòng 1158: cập nhật background jobs.
- Dòng 1160: in prompt `myShell>`.
- Dòng 1163: đọc một dòng từ `stdin`.
- Dòng 1165-1166: nếu hết input thì thoát vòng lặp.
- Dòng 1169: trim input.
- Dòng 1170-1173: bỏ qua dòng rỗng.
- Dòng 1175: lưu history.
- Dòng 1176: chạy lệnh.
- Dòng 1179-1180: khi thoát, terminate background jobs.
- Dòng 1182: xóa Critical Section.
- Dòng 1184: trả `0`, báo chương trình kết thúc thành công.

---

## 16. Vì sao project hoạt động?

### 16.1. Vì sao shell chạy được chương trình khác?

Vì Windows cho phép một process tạo process con bằng `CreateProcessA()`. Shell chỉ cần đưa vào command line hợp lệ. Windows sẽ tạo process mới, cấp PID, cấp handle và bắt đầu thread chính của process đó.

### 16.2. Vì sao foreground bắt shell phải chờ?

Vì shell gọi:

```cpp
WaitForSingleObject(processInfo.hProcess, INFINITE);
```

`INFINITE` nghĩa là chờ đến khi process kết thúc. Trong lúc đó vòng lặp `main()` chưa quay lại prompt.

### 16.3. Vì sao background không khóa shell?

Vì sau `CreateProcessA()`, nếu `background == true`, code chỉ lưu job vào `g_jobs` rồi return, không gọi `WaitForSingleObject(INFINITE)`.

### 16.4. Vì sao `list` biết process đã thoát?

Mỗi lần list, shell gọi `GetExitCodeProcess()`.

- Nếu exit code là `STILL_ACTIVE`, process còn chạy.
- Nếu khác, process đã kết thúc.

### 16.5. Vì sao `stop` và `resume` hoạt động?

Process chạy nhờ các thread. Khi shell suspend tất cả thread của process, process không còn thread nào được CPU chạy, nên coi như bị dừng. Khi resume các thread, process tiếp tục.

### 16.6. Vì sao Ctrl+C không tắt shell?

Vì shell đăng ký handler bằng `SetConsoleCtrlHandler()`. Khi Ctrl+C xảy ra, Windows gọi handler của shell. Handler trả `TRUE`, nghĩa là sự kiện đã được xử lý, Windows không xử lý tiếp theo cách mặc định.

### 16.7. Vì sao `.bat` chạy được?

Batch file là script của `cmd.exe`, không phải executable thuần như `.exe`. Shell chuyển:

```text
scripts\hello.bat
```

thành:

```text
cmd.exe /C scripts\hello.bat
```

`cmd.exe` đọc file batch và thực thi từng dòng.

---

## 17. Bảng lệnh nên demo khi bảo vệ

```text
help
date
time
pwd
dir
path
addpath scripts
which hello.bat
scripts\hello.bat
ping -n 20 127.0.0.1 > nul &
list
stop <pid>
list
resume <pid>
list
kill <pid>
list
reap
history
exit
```

Khi demo `stop/resume/kill`, lấy PID từ dòng:

```text
[background] PID 12345 started: ...
```

Sau đó thay `<pid>` bằng số thật.

---

## 18. Các lỗi thường gặp và cách hiểu

### `Cannot execute command. Windows error: ...`

Shell gọi `CreateProcessA()` không thành công. Nguyên nhân thường là command không tồn tại hoặc PATH chưa có thư mục chứa executable.

### `Cannot open PID ...`

Shell không có quyền mở process đó, PID không tồn tại, hoặc process đã thoát.

### `No thread was changed for PID ...`

PID không tồn tại, process đã thoát, hoặc shell không có quyền mở thread của process đó.

### Lệnh có `>` hoặc `|` chạy khác lệnh thường

Các ký tự này là cú pháp của CMD, không phải tham số của executable. Project phát hiện chúng và chạy qua `cmd.exe /C`.

---

## 19. Checklist nộp bài

- Build được `build\myShell.exe`.
- Chạy được `help`.
- Chạy foreground: `cmd /C echo hello`.
- Chạy background: `ping -n 5 127.0.0.1 > nul &`.
- `list` thấy PID và status.
- `stop`, `resume`, `kill` hoạt động với PID nền.
- `date`, `time`, `dir`, `path`, `addpath` hoạt động.
- Ctrl+C hủy foreground process.
- `scripts\hello.bat` chạy được.
- Có báo cáo: `docs/bao_cao_tiny_shell.md`.
- Có hướng dẫn chi tiết: `docs/huong_dan_chi_tiet_cho_nguoi_moi.md`.
