# Kịch Bản Demo Tiny Shell Cho Thầy Giáo

Tài liệu này dùng để demo **bằng tay từng câu lệnh**, không phụ thuộc vào `teacher_demo.tsh`. Mỗi mục gồm:

- **Gõ:** lệnh cần nhập trong `myShell`.
- **Kỳ vọng:** kết quả nhìn thấy.
- **Nói với thầy:** câu giải thích ngắn sau khi chạy lệnh.

Gợi ý nhịp demo: gõ chậm, sau mỗi lệnh dừng 2-5 giây để giải thích. Khi thấy `<pid>`, thay bằng PID thật mà shell vừa in ra.

---

## 0. Chuẩn Bị

Mở PowerShell tại thư mục project:

```powershell
cd "D:\Hust\Operating system\IT3070\Tiny Shell"
cmake --build build
.\build\myShell.exe
```

Kỳ vọng:

```text
========================================
              Tiny Shell
========================================
...
myShell>
```

Nói với thầy:

> Em khởi động shell tự viết. Prompt `myShell>` là vòng lặp chính đang chờ người dùng nhập lệnh.

---

## 1. Demo Lệnh Built-in Cơ Bản

### 1.1. Hiển thị trợ giúp

Gõ:

```text
help
```

Kỳ vọng: shell in danh sách lệnh hỗ trợ.

Nói với thầy:

> Đây là lệnh built-in, shell tự xử lý trong chương trình, không tạo tiến trình con.

### 1.2. Ngày giờ hệ thống

Gõ:

```text
date
time
```

Kỳ vọng: in ngày và giờ hiện tại.

Nói với thầy:

> Hai lệnh này lấy thời gian từ hệ điều hành bằng Windows API, cho thấy shell có thể gọi dịch vụ hệ thống.

### 1.3. Thư mục hiện tại và danh sách file

Gõ:

```text
pwd
dir .
```

Kỳ vọng: `pwd` in thư mục hiện tại, `dir .` in danh sách file/folder.

Nói với thầy:

> `pwd` và `dir` là thao tác với hệ thống file. Shell đang dùng thư mục làm việc hiện tại để tìm file và chạy chương trình.

---

## 2. Demo Tạo Tiến Trình Foreground

### 2.1. Chạy lệnh ngoài qua `cmd.exe`

Gõ:

```text
cmd /C echo Hello from foreground process
```

Kỳ vọng:

```text
Hello from foreground process
[foreground] exit code: 0
```

Nói với thầy:

> Đây không phải built-in. Shell gọi `CreateProcessA()` để tạo tiến trình con. Vì là foreground nên shell chờ tiến trình con kết thúc bằng `WaitForSingleObject()`.

### 2.2. Chạy file batch `.bat`

Gõ:

```text
scripts\hello.bat
```

Kỳ vọng:

```text
Hello from a Windows batch file.
Current directory is: ...
[foreground] exit code: 0
```

Nói với thầy:

> Shell nhận diện file `.bat` và chạy thông qua `cmd.exe /C`, vì batch file là script của CMD chứ không phải file `.exe` thuần.

### 2.3. Chạy batch không cần gõ đuôi `.bat`

Gõ:

```text
which scripts\hello
scripts\hello
```

Kỳ vọng: `which` tìm ra `scripts\hello.BAT`, sau đó shell chạy được file batch.

Nói với thầy:

> Shell có cơ chế tìm file theo `PATHEXT`, nên có thể tìm `.EXE`, `.BAT`, `.CMD` giống cách Windows tìm chương trình.

---

## 3. Demo Chương Trình ASCII Worker Chạy Thật

Phần này thay cho `ping`. `ascii_worker` là một chương trình C++ riêng trong project, được build thành `build\ascii_worker.exe`. Nó chạy trong một khoảng thời gian, in ASCII art dạng animation và tự thoát.

### 3.1. Chạy foreground

Gõ:

```text
build\ascii_worker.exe 10
```

Kỳ vọng: chương trình in ASCII art trong 10 giây, sau đó tự thoát và shell in exit code.

Nói với thầy:

> Đây là một chương trình con thật do project tự viết, không phải mượn lệnh mạng `ping`. Shell gọi `CreateProcessA()` để tạo tiến trình, rồi vì đây là foreground nên shell chờ nó kết thúc bằng `WaitForSingleObject()`.

### 3.2. Chạy background và bật cửa sổ riêng

Gõ:

```text
build\ascii_worker.exe 30 &
```

Kỳ vọng: shell in PID background rồi trả prompt ngay. Một cửa sổ console riêng bật lên và ASCII worker chạy trong cửa sổ đó, không chen output vào dòng lệnh của `myShell`.

Nói với thầy:

> Chỉ cần thêm dấu `&`, shell không chờ tiến trình nữa mà lưu nó vào danh sách background jobs. Với tiến trình console chạy nền, shell mở console riêng để output của process không làm lệch prompt.

Gõ tiếp:

```text
list
```

Kỳ vọng: thấy PID của `build\ascii_worker.exe 30` trong bảng job.

Nói với thầy:

> Lệnh `list` đọc danh sách tiến trình nền mà shell đang quản lý, gồm PID, trạng thái và câu lệnh ban đầu.

Đợi worker tự chạy xong, rồi gõ:

```text
list
reap
list
```

Kỳ vọng: thấy job chuyển sang `Exited(0)`, `reap` dọn job đã thoát, `list` báo không còn background process.

Nói với thầy:

> Shell kiểm tra mã thoát bằng `GetExitCodeProcess()`. Khi process kết thúc, shell cập nhật trạng thái `Exited` và có thể dọn bảng job bằng `reap`.

### 3.3. Chạy hai tiến trình nền cùng lúc

Gõ:

```text
build\ascii_worker.exe 30 &
build\ascii_worker.exe 45 &
list
```

Kỳ vọng: shell hiển thị hai job `ascii_worker` trong bảng `list`. Mỗi worker có PID riêng và cửa sổ console riêng.

Nói với thầy:

> Ở đây shell đang quản lý nhiều tiến trình nền cùng lúc. Mỗi tiến trình có PID, handle và trạng thái riêng trong job table.

Gõ, thay `<pid_worker_1>` bằng PID của một worker trong bảng `list`:

```text
stop <pid_worker_1>
list
resume <pid_worker_1>
list
```

Kỳ vọng: job được chọn chuyển `Suspended`, sau đó quay lại `Running`. Cửa sổ ASCII art của worker đó sẽ đứng lại khi bị `stop` và chạy tiếp sau `resume`.

Nói với thầy:

> Shell có thể tạm dừng và tiếp tục một tiến trình nền đang được quản lý. Bên trong, shell duyệt các thread của PID rồi gọi `SuspendThread()` hoặc `ResumeThread()`.

Gõ, thay `<pid_worker_2>` bằng PID worker còn lại:

```text
kill <pid_worker_2>
list
sleep 35000
list
reap
list
```

Kỳ vọng: worker bị `kill` chuyển `Exited(1)`. Worker còn lại tự chạy xong và chuyển `Exited(0)`. Sau `reap`, các job đã thoát được dọn khỏi bảng.

Nói với thầy:

> Đây là vòng đời đầy đủ của background job: tạo nhiều tiến trình, liệt kê, tạm dừng, tiếp tục, kết thúc cưỡng bức một tiến trình và để một tiến trình khác tự thoát bình thường.

---

## 4. Demo Mở Ứng Dụng Windows Và Giải Thích Hiện Tượng GUI App

Phần này chỉ dùng để chứng minh shell có thể gọi ứng dụng Windows bên ngoài. **Không dùng Notepad/Calculator để chứng minh `foreground`, `background`, `list`, `kill`**, vì trên Windows mới các ứng dụng GUI này có thể là launcher hoặc single-instance app.

Nói với thầy trước khi demo:

> Với các ứng dụng GUI hiện đại như Notepad hoặc Calculator, tiến trình ban đầu có thể chỉ là launcher. Nó mở hoặc chuyển quyền cho một tiến trình khác rồi thoát rất nhanh. Vì shell chỉ giữ handle của tiến trình mà nó trực tiếp tạo ra, trạng thái trong `list` có thể là `Exited` dù cửa sổ ứng dụng vẫn đang mở. Do đó phần quản lý tiến trình nền em demo bằng `ascii_worker`, còn Notepad/Calculator chỉ để minh họa gọi ứng dụng ngoài.

### 4.1. Mở Calculator

Gõ:

```text
calc &
```

Kỳ vọng: Calculator bật lên. Nếu Windows trả process về nhanh, job có thể chuyển sang `Exited` sớm, nhưng ứng dụng Calculator vẫn được mở bởi hệ thống.

Nói với thầy:

> Một số ứng dụng Windows hiện đại như Calculator có thể được Windows chuyển qua tiến trình ứng dụng khác, nên PID ban đầu có thể thoát nhanh. Nhưng phần quan trọng là shell vẫn gọi được ứng dụng ngoài bằng cơ chế tạo tiến trình.

Nếu muốn dọn job launcher đã thoát:

```text
list
reap
```

Nói với thầy:

> Job có thể `Exited` vì tiến trình launcher đã hoàn thành nhiệm vụ mở Calculator. Đây là hành vi của ứng dụng Windows hiện đại, không phải lỗi quản lý foreground/background của shell.

### 4.2. Mở Notepad để tương tác thủ công

Gõ:

```text
notepad &
```

Kỳ vọng: Notepad bật lên. Có thể gõ vài chữ trong Notepad để cho thấy ứng dụng được mở thành công.

Nói với thầy:

> Lệnh này cho thấy shell gọi được ứng dụng GUI bên ngoài. Tuy nhiên em không dùng Notepad làm test chính cho `list/kill`, vì Notepad trên Windows mới cũng có thể dùng cơ chế launcher hoặc single-instance.

Nếu `list` thấy `Exited`, nói thêm:

> Trạng thái `Exited` ở đây là trạng thái của tiến trình launcher mà shell tạo trực tiếp. Cửa sổ Notepad còn sống có thể thuộc tiến trình khác do Windows quản lý.

---

## 5. Demo Ctrl+C Hủy Foreground Process

Gõ:

```text
build\ascii_worker.exe 100
```

Kỳ vọng: shell bị giữ lại vì đây là foreground process.

Nói với thầy:

> Worker này chạy foreground nên shell đang đợi nó. Em cho worker chạy 100 giây để có thời gian bấm Ctrl+C.

Trong lúc ASCII worker đang chạy, bấm:

```text
Ctrl+C
```

Kỳ vọng:

```text
[Ctrl+C] Foreground process <pid> was terminated.
myShell>
```

Nói với thầy:

> Shell đăng ký `SetConsoleCtrlHandler()` để bắt Ctrl+C. Khi có foreground process, shell hủy đúng process đó và bản thân shell vẫn tiếp tục chạy.

---

## 6. Demo Biến Môi Trường Và PATH

### 6.1. Biến môi trường

Gõ:

```text
setenv COURSE IT3070
env COURSE
unsetenv COURSE
env COURSE
```

Kỳ vọng: biến `COURSE` được tạo, in ra, xóa, rồi in rỗng.

Nói với thầy:

> Shell có thể đọc và ghi environment của chính phiên shell. Các tiến trình con tạo sau đó sẽ kế thừa môi trường này.

### 6.2. PATH

Gõ:

```text
path
which cmd
```

Kỳ vọng: in PATH và tìm thấy `cmd.EXE`.

Nói với thầy:

> Khi người dùng gõ tên chương trình, shell tìm executable trong thư mục hiện tại, PATH và theo PATHEXT.

Nếu muốn demo thêm:

```text
addpath scripts
which hello
hello
```

Kỳ vọng: shell tìm được `scripts\hello.BAT` và chạy được `hello` sau khi thêm `scripts` vào PATH.

Nói với thầy:

> `addpath` chỉ thay đổi PATH trong phiên shell hiện tại, không sửa PATH vĩnh viễn của Windows.

---

## 7. Demo Xử Lý Lỗi Không Crash

Gõ lần lượt:

```text
sleep abc
sleep -100
kill abc
kill 999999
stop 999999
resume 999999
run file_khong_ton_tai.tsh
cd folder_khong_ton_tai
unknown_command_abc
```

Kỳ vọng: shell in thông báo lỗi hoặc usage, nhưng vẫn không thoát.

Nói với thầy:

> Các input sai được kiểm tra và xử lý. Shell không bị crash khi người dùng nhập sai kiểu dữ liệu hoặc PID không hợp lệ.

Kiểm tra shell vẫn sống:

```text
help
```

Nói với thầy:

> Sau loạt lỗi, shell vẫn nhận lệnh tiếp. Đây là phần ổn định quan trọng của chương trình.

---

## 8. Kết Thúc Demo

Gõ:

```text
exit
```

Kỳ vọng:

```text
Sending kill signal to all tracked background processes...
Bye.
```

Nói với thầy:

> Khi thoát, shell dọn các background job còn được quản lý, đóng handle và xóa Critical Section trước khi kết thúc.

---

## 9. Thứ Tự Demo Gọn Nếu Thầy Không Có Nhiều Thời Gian

Nếu chỉ có khoảng 5-7 phút, chạy theo thứ tự này:

```text
help
date
time
pwd
dir .
cmd /C echo Hello from foreground process
scripts\hello
build\ascii_worker.exe 10
build\ascii_worker.exe 30 &
build\ascii_worker.exe 45 &
list
stop <pid_worker_1>
list
resume <pid_worker_1>
kill <pid_worker_2>
sleep 35000
reap
calc &
notepad &
build\ascii_worker.exe 100
Ctrl+C
sleep abc
kill 999999
help
exit
```

Điểm cần nhấn mạnh:

- Built-in command không tạo tiến trình.
- Foreground process làm shell phải chờ.
- Background process trả prompt ngay.
- `list/stop/resume/kill/reap` thể hiện quản lý tiến trình.
- Ctrl+C chỉ hủy foreground process, không làm chết shell.
- Shell xử lý lỗi nhập liệu mà không crash.
