# Phan Tich Sau Analyze Result Va De Xuat Nang Cao

Tai lieu nay tong hop ket qua doc `docs/analysis_results.md`, danh gia muc do phu hop cua tung nhan xet, va ghi lai cac thay doi da ap dung vao `src/main.cpp`.

## 1. Ket Luan Nhanh

Phan tich trong `analysis_results.md` nhin chung dung va huu ich. Cac loi lien quan den validate input, handle leak, path resolution, Ctrl+C race window va PID recycling deu phu hop de sua trong pham vi bai Tiny Shell.

Mot so de xuat nang cao nhu dung `NtSuspendProcess`/`NtResumeProcess` la dung theo huong cong cu he thong chuyen nghiep, nhung khong nen dua vao ban nop mac dinh vi do la API noi bo, kho giai thich, va khong can thiet voi muc tieu mon Nguyen ly he dieu hanh. Project tiep tuc dung ToolHelp API cong khai de `stop/resume`, vi day la cach minh bach hon khi bao ve.

## 2. Danh Gia Tung Muc Analyze

| Muc analyze | Danh gia | Xu ly |
| --- | --- | --- |
| `sleep abc`/`sleep -100` co the crash | Dung | Da them parse so nghiem ngat bang `parseUnsignedLongStrict()`. |
| Background handle leak neu khong `reap` | Dung | `refreshBackgroundJobs()` dong handle ngay khi phat hien job da thoat. |
| Race Ctrl+C voi foreground handle | Hop ly | Handler goi `TerminateProcess()` khi dang giu Critical Section, tranh dung handle stale. |
| `findExecutable()` khong thu PATHEXT khi co folder | Dung | Da sua de `which scripts\hello` tim duoc `scripts\hello.bat`. |
| Khong nen dung `TerminateProcess` cho Ctrl+C | Dung ve ly thuyet, nhung khong doi mac dinh | Giu vi yeu cau bai la huy foreground process; co ghi thanh huong nang cap. |
| PID recycling khi `kill <pid>` | Dung | `kill/stop/resume` chi thao tac tren background job shell dang quan ly. |
| `stop/resume` khong atomic | Dung ve ly thuyet | Khong dung `NtSuspendProcess`; giu ToolHelp API cong khai, de bao ve de hon. |
| History bi ngap khi chay script | Dung | `runScript()` khong con them tung dong script vao `history`; history chi luu lenh `run ...`. |

## 3. Cac Sua Doi Ky Thuat Da Ap Dung

### 3.1. Validate so cho PID va `sleep`

Them helper:

```cpp
bool parseUnsignedLongStrict(const std::string &text, unsigned long &value)
```

Tac dung:

- Tu choi chuoi rong.
- Tu choi ky tu khong phai chu so.
- Tu choi so am nhu `-100`.
- Bat out-of-range.

Lenh test:

```text
sleep abc
sleep -100
kill abc
```

Ket qua mong doi: shell in loi/usage, khong crash.

### 3.2. Dong handle background job da thoat

Khi `refreshBackgroundJobs()` thay exit code khac `STILL_ACTIVE`, shell:

1. Cap nhat `status = Exited`.
2. Luu `exitCode`.
3. Goi `closeJobHandles(job)`.

Nhu vay process object/thread object trong kernel khong bi giu vo thoi han neu nguoi dung quen `reap`.

### 3.3. `kill/stop/resume` an toan hon

Truoc day shell co the `OpenProcess(pid)` voi bat ky PID nao. Sau khi sua:

- `kill <pid>` chi chap nhan PID nam trong bang background job cua shell.
- `stop <pid>` va `resume <pid>` cung chi thao tac voi job dang duoc track.
- Neu PID khong thuoc job table, shell bao:

```text
PID <pid> is not a tracked background process.
```

Ly do: yeu cau bai la quan ly background process cua shell, khong phai task manager de kill process he thong.

### 3.4. Tim `.bat/.cmd` khi nguoi dung khong go extension

Da them helper:

```cpp
std::vector<std::string> getPathExtensions()
```

Neu nguoi dung go:

```text
which scripts\hello
scripts\hello
```

shell co the tim va chay `scripts\hello.bat`.

### 3.5. Ctrl+C bot race hon

Handler Ctrl+C bay gio kiem tra va terminate foreground process trong Critical Section. Main thread muon clear foreground cung phai vao cung Critical Section, nen hai ben khong dong/doc handle cung luc.

### 3.6. History khong bi script lam ngap

Truoc day `run scripts\demo.tsh` se them moi dong trong script vao `history`. Sau khi sua, history chi luu lenh top-level nguoi dung nhap:

```text
run scripts\demo.tsh
history
```

Dieu nay giong hanh vi shell that hon.

### 3.7. Sua loi format ngay gio sau `dir`

Qua regression test phat hien neu truoc do `dir` da dat stream sang `std::left`, `date` co the in `06` thanh `60`. Da sua `printDateTime()` bang cach reset `std::right` truoc khi format ngay/gio.

## 4. Cac De Xuat Nang Cao Nen Dua Vao Bao Ve

Nhung muc nay khong bat buoc implement het, nhung rat hop de noi trong phan "Huong phat trien":

1. **Job ID kieu Bash:** hien tai phai dung PID. Co the them job id `[1]`, `[2]` va cho phep `kill %1`, `stop %2`.
2. **Lenh `fg <pid/job>`:** dua background process ve foreground va shell cho den khi no ket thuc.
3. **Tu cai dat redirect `>` va `<`:** dung `CreateFileA()`, gan vao `STARTUPINFOA.hStdOutput/hStdInput`, bat `STARTF_USESTDHANDLES`, khong can qua `cmd.exe /C`.
4. **Tu cai dat pipe `|`:** dung `CreatePipe()` noi stdout cua process A vao stdin cua process B.
5. **Log tien trinh:** ghi file `logs/process.log` gom thoi gian start/exit, PID, command, exit code.
6. **Lenh `treeproc`:** in cay process con cua mot PID bang `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)`.
7. **Lenh `sysinfo`:** in CPU count, memory usage, uptime bang Windows API.
8. **Timeout foreground:** vi du `timeout 3000 ping -n 100 127.0.0.1`, qua thoi gian thi kill.
9. **Script co bien don gian:** cho phep `setenv` va thay `%VAR%` trong file `.tsh`.
10. **Graceful Ctrl+C:** thay vi `TerminateProcess`, thu gui console event bang `GenerateConsoleCtrlEvent()` truoc, neu process khong thoat moi terminate.

## 5. Cac Lenh Regression Da Chay

```text
sleep abc
sleep -100
kill abc
kill 999999
which scripts\hello
scripts\hello
ping -n 2 127.0.0.1 > nul &
list
sleep 2500
list
reap
list
run scripts\demo.tsh
history
exit
```

Them test format:

```text
dir .
date
time
exit
```

Ket qua: build thanh cong, shell khong crash, batch thieu extension chay duoc, job background cap nhat dung, `date/time` in dung sau `dir`.
