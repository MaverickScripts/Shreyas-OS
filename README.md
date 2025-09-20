# Shreyas-OS
Built a lightweight custom mini-OS in C with a Unix-like shell, supporting file management (ls, cat, write, edit), process/task control (ps, spawn, kill, suspend/resume), and system utilities (uptime, compile, IP lookup). Designed for modularity and extensibility, blending low-level programming with a user-friendly command-line interface.


Custom Mini-OS (C Language)

A lightweight Unix-like mini operating system, engineered entirely in C, featuring a sleek command-line shell with file management, process control, and system utilities.
Unlike typical academic builds, this OS is modular, extensible, and user-friendly, showcasing low-level programming, memory optimization, and system-level innovation.

✨ Features

File Management: ls, cat, write, append, touch, rm, edit

Process & Task Control: spawn, addtask, ps, killtask, suspend, resume

System Utilities: uptime, poweroff, powerbtn, clear, echo, version

Compilation & Execution: compile <file>, run <command>

Networking: ip (view system IP addresses)

Import/Export: move files between virtual FS and host disk

📜 Commands Overview
Command	Description
help	Show all available commands
ls	List files in the virtual file system
cat <file>	Display contents of a file
write <file> <text>	Create/overwrite file with text
append <file> <text>	Append text to an existing file
touch <file>	Create an empty file
rm <file>	Delete a file
edit <file>	Edit a file interactively
spawn <builtin>	Run builtin task (e.g. clock, logger)
addtask <name> <interval> <message>	Schedule repeating tasks
ps	List running tasks
killtask <id>	Terminate a task
suspend <id>	Suspend a task
resume <id>	Resume a task
uptime	Show system uptime
poweroff	Shutdown the OS
powerbtn	Emulate power button
clear	Clear the terminal
echo <text>	Print text to console
version	Show OS version
compile <file>	Compile a C source file inside the OS
run <command>	Run an external command
ip	Display local IP addresses
export <disk_file> <vfs_file>	Save VFS file to disk
import <vfs_file> <disk_file>	Load disk file into VFS
⚡ Getting Started
🔧 Requirements

GCC / Clang (Linux/macOS)

MinGW (Windows, with -lws2_32 for Winsock support)

🛠 Build Instructions
# Linux / macOS
gcc main.c -o mini-os

# Windows (MinGW)
gcc main.c -o mini-os.exe -lws2_32

▶️ Run the OS
./mini-os

📂 Project Structure
mini-os/
│── main.c        # Core OS logic & shell
│── vfs/          # Virtual File System (managed internally)
│── tasks/        # Task manager
│── README.md     # Project documentation

🚀 Example Usage
> touch hello.txt
> write hello.txt "Hello World!"
> cat hello.txt
Hello World!
> addtask reminder 5 "Take a break!"
> ps
ID | Task        | Status
1  | reminder    | Running

📌 Notes

Runs inside a virtual shell, not a real kernel.

Designed for educational & experimental purposes.

Modular — new commands/utilities can be easily added.

🧑‍💻 Author

Shreyas Chowdhury
Building next-gen lightweight systems & developer tools.
