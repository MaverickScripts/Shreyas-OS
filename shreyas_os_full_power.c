#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <pthread.h>
#endif

/* Configuration */
#define FS_MAX_FILES 128
#define FS_MAX_CONTENT 8192
#define MAX_TASKS 48
#define MAX_NAME 96
#define MAX_MSG 1024
#define CMD_HISTORY 128
#define VFS_STATE_FILE "vfs_state.dat"
#define PROMPT_BUFSZ 1024

typedef struct {
    char name[MAX_NAME];
    char content[FS_MAX_CONTENT];
    int used;
} vfile_t;

static vfile_t vfs[FS_MAX_FILES];

typedef void (*builtin_fn)(void);

typedef struct {
    int id;
    char name[MAX_NAME];
    int type;
    builtin_fn fn;
    char msg[MAX_MSG];
    unsigned interval;
    unsigned ticks;
    int active;
} task_t;

static task_t tasks[MAX_TASKS];
static int task_count = 0;
static int next_task_id = 1;
static int running = 1;
static time_t start_time;
static char env_USER[64] = "Tony";
static char env_HOSTNAME[128] = "ShreyasOS";
static char env_PWD[256] = "/";

static char cmd_history[CMD_HISTORY][1024];
static int hist_pos = 0;
static int hist_size = 0;

/* Helper: cross-platform sleep in milliseconds */
static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep(ms * 1000);
#endif
}

/* History */
static void save_history_line(const char *line) {
    if (!line) return;
    strncpy(cmd_history[hist_pos], line, sizeof(cmd_history[0]) - 1);
    cmd_history[hist_pos][sizeof(cmd_history[0]) - 1] = '\0';
    hist_pos = (hist_pos + 1) % CMD_HISTORY;
    if (hist_size < CMD_HISTORY) hist_size++;
}
static void show_history() {
    int i;
    int start = (hist_pos - hist_size + CMD_HISTORY) % CMD_HISTORY;
    for (i = 0; i < hist_size; ++i) {
        int idx = (start + i) % CMD_HISTORY;
        printf("%3d  %s\n", i+1, cmd_history[idx]);
    }
}

/* VFS persistence - write/read entire vfs array for simplicity */
static void vfs_save_state() {
    FILE *f = fopen(VFS_STATE_FILE, "wb");
    if (!f) return;
    int files = FS_MAX_FILES;
    fwrite(&files, sizeof(int), 1, f);
    fwrite(vfs, sizeof(vfile_t), FS_MAX_FILES, f);
    fclose(f);
}

static void vfs_load_state() {
    FILE *f = fopen(VFS_STATE_FILE, "rb");
    if (!f) return;
    int files = 0;
    if (fread(&files, sizeof(int), 1, f) != 1) { fclose(f); return; }
    size_t got = fread(vfs, sizeof(vfile_t), FS_MAX_FILES, f);
    (void)got;
    fclose(f);
}

/* VFS */
static void vfs_init() {
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        vfs[i].used = 0;
        vfs[i].name[0] = '\0';
        vfs[i].content[0] = '\0';
    }
    vfs_load_state();
    if (!vfs[0].used) {
        strncpy(vfs[0].name, "welcome.txt", sizeof(vfs[0].name)-1);
        strncpy(vfs[0].content, "Shreyas Systems - Shreyas' OS powering Shreyas INDUSTRIES.\n", sizeof(vfs[0].content)-1);
        vfs[0].content[sizeof(vfs[0].content)-1] = '\0';
        vfs[0].used = 1;
    }
}

static vfile_t* vfs_find(const char* name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (vfs[i].used && strcmp(vfs[i].name, name) == 0) return &vfs[i];
    }
    return NULL;
}

static int vfs_create_slot() {
    for (int i = 0; i < FS_MAX_FILES; ++i) if (!vfs[i].used) return i;
    return -1;
}

static void vfs_list() {
    printf("Files:\n");
    for (int i = 0; i < FS_MAX_FILES; ++i) if (vfs[i].used) printf(" - %s\n", vfs[i].name);
}

static void vfs_write(const char* name, const char* data) {
    if (!name || name[0]=='\0') return;
    vfile_t* f = vfs_find(name);
    if (!f) {
        int idx = vfs_create_slot();
        if (idx < 0) { printf("VFS full\n"); return; }
        f = &vfs[idx];
        memset(f,0,sizeof(*f));
        strncpy(f->name, name, sizeof(f->name)-1);
        f->name[sizeof(f->name)-1] = '\0';
        f->used = 1;
    }
    if (data) {
        strncpy(f->content, data, sizeof(f->content)-1);
        f->content[sizeof(f->content)-1] = '\0';
    } else f->content[0] = '\0';
}

static void vfs_append(const char* name, const char* data) {
    if (!name) return;
    vfile_t* f = vfs_find(name);
    if (!f) { vfs_write(name, data); return; }
    size_t cur = strlen(f->content);
    size_t add = data ? strlen(data) : 0;
    if (cur + add + 1 > sizeof(f->content)) add = (sizeof(f->content)-1) - cur;
    if (add > 0) strncat(f->content, data, add);
}

static int vfs_remove(const char* name) {
    vfile_t* f = vfs_find(name);
    if (!f) return 0;
    f->used = 0;
    f->name[0] = '\0';
    f->content[0] = '\0';
    return 1;
}

/* tasks and scheduler */
static void task_clock_builtin() {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    printf("[clock] %02d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void task_heartbeat_builtin() {
    static int hb = 0;
    hb++;
    if (hb % 5 == 0) printf("[heartbeat] system alive...\n");
}

static void task_logger_builtin() {
    printf("[logger] simple logger tick\n");
}

static int find_free_task_slot() {
    for (int i = 0; i < MAX_TASKS; ++i) if (tasks[i].id == 0) return i;
    for (int i = 0; i < MAX_TASKS; ++i) if (!tasks[i].active) return i;
    return -1;
}

static int spawn_builtin(const char* name, builtin_fn fn) {
    if (task_count >= MAX_TASKS) return 0;
    int idx = find_free_task_slot();
    if (idx == -1) return 0;
    tasks[idx].id = next_task_id++;
    strncpy(tasks[idx].name, name, sizeof(tasks[idx].name)-1);
    tasks[idx].name[sizeof(tasks[idx].name)-1] = '\0';
    tasks[idx].type = 0;
    tasks[idx].fn = fn;
    tasks[idx].msg[0] = '\0';
    tasks[idx].interval = 0;
    tasks[idx].ticks = 0;
    tasks[idx].active = 1;
    task_count++;
    return tasks[idx].id;
}

static int spawn_message_task(const char* name, unsigned interval, const char* message) {
    if (task_count >= MAX_TASKS) return 0;
    int idx = find_free_task_slot();
    if (idx == -1) return 0;
    tasks[idx].id = next_task_id++;
    strncpy(tasks[idx].name, name, sizeof(tasks[idx].name)-1);
    tasks[idx].name[sizeof(tasks[idx].name)-1] = '\0';
    tasks[idx].type = 1;
    tasks[idx].fn = NULL;
    strncpy(tasks[idx].msg, message ? message : "", sizeof(tasks[idx].msg)-1);
    tasks[idx].msg[sizeof(tasks[idx].msg)-1] = '\0';
    if (interval == 0) interval = 1;
    tasks[idx].interval = interval;
    tasks[idx].ticks = 0;
    tasks[idx].active = 1;
    task_count++;
    return tasks[idx].id;
}

static task_t* task_find_by_id(int id) {
    for (int i = 0; i < MAX_TASKS; ++i) if (tasks[i].id == id) return &tasks[i];
    return NULL;
}

static void scheduler_tick() {
    for (int i = 0; i < MAX_TASKS; ++i) {
        if (tasks[i].id == 0 || !tasks[i].active) continue;
        tasks[i].ticks++;
        if (tasks[i].type == 0 && tasks[i].fn) tasks[i].fn();
        else if (tasks[i].type == 1) {
            if (tasks[i].interval > 0 && (tasks[i].ticks % tasks[i].interval) == 0) {
                printf("[task %d: %s] %s\n", tasks[i].id, tasks[i].name, tasks[i].msg);
            }
        }
    }
}

/* utilities prototypes */
static void show_help();
static void show_ps();
static void kill_task(int id);
static void suspend_task(int id);
static void resume_task(int id);
static void show_uptime();
static void power_button_ui();
static void clear_screen();
static void read_line(char* buf, size_t sz);
static void cmd_edit(const char* filename);
static void export_to_disk(const char* diskfile, const char* vfsfile);
static void import_from_disk(const char* vfsfile, const char* diskfile);
static void show_ips();
static void compile_file(const char* filename);
static void run_command(const char* cmdrest);

/* minimal sysinfo */
static void cmd_sysinfo() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    printf("CPU cores (Logical): %u\n", si.dwNumberOfProcessors);
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    printf("Memory: %llu MB total, %llu MB available\n", (unsigned long long)(ms.ullTotalPhys/1024/1024), (unsigned long long)(ms.ullAvailPhys/1024/1024));
#else
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    printf("CPU cores (Logical): %ld\n", cores);
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        unsigned long long total = (unsigned long long)pages * (unsigned long long)page_size;
        printf("Memory approx: %llu MB\n", (unsigned long long)(total / 1024 / 1024));
    }
#endif
    show_uptime();
}

/* calendar - simple textual month calendar */
static void cmd_cal(int year, int month) {
    if (month < 1 || month > 12) { printf("Invalid month\n"); return; }
    struct tm tm = {0};
    tm.tm_mday = 1;
    tm.tm_mon = month - 1;
    tm.tm_year = year - 1900;
    mktime(&tm);
    int start_wday = tm.tm_wday; /* 0=Sun */
    int mdays;
    if (month==2) {
        int y = year;
        int leap = ( (y%4==0 && y%100!=0) || (y%400==0) );
        mdays = leap ? 29 : 28;
    } else if (month==4 || month==6 || month==9 || month==11) mdays = 30;
    else mdays = 31;
    printf("     %d-%02d\n", year, month);
    printf("Su Mo Tu We Th Fr Sa\n");
    int day = 1;
    for (int i = 0; i < start_wday; ++i) printf("   ");
    for (int i = start_wday; i < 7; ++i) {
        printf("%2d ", day++);
    }
    printf("\n");
    while (day <= mdays) {
        for (int i = 0; i < 7 && day <= mdays; ++i) {
            printf("%2d ", day++);
        }
        printf("\n");
    }
}

/* whoami/hostname/date */
static void cmd_whoami() { printf("%s\n", env_USER); }
static void cmd_hostname() { printf("%s\n", env_HOSTNAME); }
static void cmd_date() {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char buf[128];
    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", &tm);
    printf("%s\n", buf);
}

/* show uptime */
static void show_uptime() {
    time_t now = time(NULL);
    long diff = (long)difftime(now, start_time);
    int days = diff / (24*3600);
    int hours = (diff % (24*3600)) / 3600;
    int mins = (diff % 3600) / 60;
    int secs = diff % 60;
    printf("Uptime: %d days, %02d:%02d:%02d\n", days, hours, mins, secs);
}

/* power UI */
static void power_button_ui() {
    printf("\n+-----------------------+\n");
    printf("|       [ POWER ]       |\n");
    printf("+-----------------------+\n");
    printf("Press 'p' then Enter to power off, or just press Enter to cancel: ");
    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) return;
    if (buf[0] == 'p' || buf[0] == 'P') {
        printf("Power button pressed. Shutting down Shreyas OS...\n");
        vfs_save_state();
        running = 0;
    } else printf("Power cancelled.\n");
}

/* clear */
static void clear_screen() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) { printf("\n"); return; }
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) { system("cls"); return; }
    DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    COORD homeCoords = {0,0};
    DWORD count;
    FillConsoleOutputCharacter(hOut, ' ', cellCount, homeCoords, &count);
    SetConsoleCursorPosition(hOut, homeCoords);
#else
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
#endif
}

/* read line safe */
static void read_line(char* buf, size_t sz) {
    if (!fgets(buf, (int)sz, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\n")] = '\0';
}

/* editor */
static void cmd_edit(const char* filename) {
    if (!filename || filename[0]=='\0') { printf("Usage: edit <file>\n"); return; }
    vfile_t* f = vfs_find(filename);
    char buffer[FS_MAX_CONTENT];
    if (f) strncpy(buffer, f->content, sizeof(buffer)-1); else buffer[0] = '\0';
    printf("Entering editor for '%s'. Type a single dot '.' on a line to finish.\n", filename);
    printf("Current content:\n----\n%s\n----\n", buffer);
    char line[512];
    size_t pos = 0;
    buffer[0] = '\0';
    while (1) {
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strcmp(line, ".\n") == 0 || strcmp(line, ".\r\n") == 0 || (line[0]=='.' && line[1]=='\0')) break;
        size_t add = strlen(line);
        if (pos + add + 1 >= sizeof(buffer)) add = (sizeof(buffer)-1) - pos;
        if (add > 0) {
            strncat(buffer, line, add);
            pos += add;
        }
        if (pos >= sizeof(buffer)-1) break;
    }
    vfs_write(filename, buffer);
    printf("Saved '%s' (%zu bytes)\n", filename, strlen(buffer));
}

/* import/export */
static void export_to_disk(const char* diskfile, const char* vfsfile) {
    if (!diskfile || !vfsfile) { printf("Usage: export <file_on_disk> <vfs_file>\n"); return; }
    vfile_t* f = vfs_find(vfsfile);
    if (!f) { printf("VFS file not found: %s\n", vfsfile); return; }
    FILE* fp = fopen(diskfile, "wb");
    if (!fp) { printf("Failed to open disk file for writing: %s\n", diskfile); return; }
    fwrite(f->content, 1, strlen(f->content), fp);
    fclose(fp);
    printf("Exported %s -> %s\n", vfsfile, diskfile);
}

static void import_from_disk(const char* vfsfile, const char* diskfile) {
    if (!diskfile || !vfsfile) { printf("Usage: import <vfs_file> <file_on_disk>\n"); return; }
    FILE* fp = fopen(diskfile, "rb");
    if (!fp) { printf("Failed to open disk file: %s\n", diskfile); return; }
    char buf[FS_MAX_CONTENT];
    size_t n = fread(buf, 1, sizeof(buf)-1, fp);
    buf[n] = '\0';
    fclose(fp);
    vfs_write(vfsfile, buf);
    printf("Imported %s -> %s (%zu bytes)\n", diskfile, vfsfile, n);
}

/* IP display (no packet manipulation) */
static void show_ips() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { printf("WSAStartup failed\n"); return; }
    char name[256];
    if (gethostname(name, sizeof(name)) == SOCKET_ERROR) { printf("Unable to get host name\n"); WSACleanup(); return; }
    struct hostent *he = gethostbyname(name);
    if (!he) { printf("gethostbyname failed\n"); WSACleanup(); return; }
    printf("Local IPv4 addresses:\n");
    char **addr_list = he->h_addr_list;
    for (int i = 0; addr_list[i] != NULL; ++i) {
        struct in_addr addr;
        memcpy(&addr.s_addr, addr_list[i], sizeof(addr.s_addr));
        printf(" - %s\n", inet_ntoa(addr));
    }
    WSACleanup();
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) { printf("getifaddrs failed\n"); return; }
    printf("Local IP addresses:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char host[NI_MAXHOST];
            int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICSERV);
            if (s == 0) printf(" - %s: %s\n", ifa->ifa_name, host);
        }
    }
    freeifaddrs(ifaddr);
#endif
    printf("Note: This command only displays local addresses. Manipulating live packets is not provided.\n");
}

/* compile/run from VFS */
static void compile_file(const char* filename) {
    if (!filename || filename[0]=='\0') { printf("Usage: compile <file.c>\n"); return; }
    vfile_t* f = vfs_find(filename);
    if (!f) { printf("File not found in VFS: %s\n", filename); return; }
    char tmpdisk[256], outfile[256];
    time_t t = time(NULL);
    snprintf(tmpdisk, sizeof(tmpdisk), "shreyas_tmp_%ld.c", (long)t);
#ifdef _WIN32
    snprintf(outfile, sizeof(outfile), "shreyas_out_%ld.exe", (long)t);
#else
    snprintf(outfile, sizeof(outfile), "shreyas_out_%ld", (long)t);
#endif
    FILE* fp = fopen(tmpdisk, "wb");
    if (!fp) { printf("Failed to create temp file %s\n", tmpdisk); return; }
    fwrite(f->content, 1, strlen(f->content), fp);
    fclose(fp);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "gcc \"%s\" -o \"%s\" 2> shreyas_compile_err.txt", tmpdisk, outfile);
    int rc = system(cmd);
    FILE* errf = fopen("shreyas_compile_err.txt", "rb");
    if (errf) {
        char errbuf[4096];
        size_t n = fread(errbuf,1,sizeof(errbuf)-1,errf);
        errbuf[n] = '\0';
        fclose(errf);
        if (n > 0) printf("Compiler output:\n%s\n", errbuf);
        else printf("Compiled successfully to %s\n", outfile);
    } else {
        if (rc == 0) printf("Compiled successfully to %s\n", outfile);
        else printf("Compile finished with code %d\n", rc);
    }
    printf("Temporary source: %s\n", tmpdisk);
}

static void run_command(const char* cmdrest) {
    if (!cmdrest || cmdrest[0]=='\0') { printf("Usage: run <command>\n"); return; }
    printf("Running command: %s\n", cmdrest);
    int rc = system(cmdrest);
    printf("Command exited with code %d\n", rc);
}

/* core help/man */
static void show_help() {
    printf("\x1b[36mShreyas OS - Command Reference (short)\x1b[0m\n");
    printf("  help                                - show this help menu\n");
    printf("  ls                                  - list all files in virtual FS\n");
    printf("  cat <file>                          - display contents of a file\n");
    printf("  write <file> <text>                 - create/overwrite a file with text\n");
    printf("  append <file> <text>                - append text to a file\n");
    printf("  touch <file>                        - create an empty file\n");
    printf("  rm <file>                           - delete a file\n");
    printf("  edit <file>                         - interactively edit a file\n");
    printf("  spawn <builtin>                     - start builtin task (clock, heartbeat, logger)\n");
    printf("  addtask <name> <interval> <message> - create repeating message task\n");
    printf("  ps                                  - list running tasks\n");
    printf("  killtask <id>                       - terminate a task by id\n");
    printf("  suspend <id>                        - suspend a task\n");
    printf("  resume <id>                         - resume a suspended task\n");
    printf("  uptime                              - show system uptime\n");
    printf("  poweroff                            - shutdown the OS (saves state)\n");
    printf("  reboot                              - reboot the OS\n");
    printf("  powerbtn                            - emulate pressing power button\n");
    printf("  clear                               - clear the terminal screen\n");
    printf("  echo <text>                         - print text to console\n");
    printf("  version                             - show OS version\n");
    printf("  compile <file>                      - compile C source file in VFS\n");
    printf("  run <command>                       - run a system command\n");
    printf("  ip                                  - display local IP addresses\n");
    printf("  export <file_on_disk> <vfs_file>    - save a VFS file to disk\n");
    printf("  import <vfs_file> <file_on_disk>    - load a file from disk into VFS\n");
    printf("  date                                - show date/time\n");
    printf("  cal [month] [year]                  - show calendar for month/year\n");
    printf("  sysinfo                             - show basic CPU/memory/uptime\n");
    printf("  whoami                              - display current user\n");
    printf("  hostname                            - display hostname\n");
    printf("  history                             - show command history\n");
    printf("  !!                                   - repeat last command\n");
    printf("  man <cmd>                           - short manual for command\n");
}

/* man pages (short) */
static void cmd_man(const char* cmd) {
    if (!cmd || cmd[0] == '\0') { printf("Usage: man <cmd>\n"); return; }
    if (strcmp(cmd, "ls")==0) printf("ls: list files in virtual filesystem\n");
    else if (strcmp(cmd, "cat")==0) printf("cat <file>: print file contents\n");
    else if (strcmp(cmd, "write")==0) printf("write <file> <text>: create/overwrite file\n");
    else if (strcmp(cmd, "compile")==0) printf("compile <file>: compile C source inside VFS using system gcc\n");
    else printf("No manual entry for %s\n", cmd);
}

/* ps/kill/suspend/resume */
static void show_ps() {
    printf("Tasks (max %d):\n", MAX_TASKS);
    for (int i = 0; i < MAX_TASKS; ++i) {
        if (tasks[i].id != 0) {
            printf(" ID=%d | %-12s | type=%s | ticks=%u | interval=%u | %s\n",
                   tasks[i].id,
                   tasks[i].name,
                   (tasks[i].type == 0) ? "builtin" : "message",
                   tasks[i].ticks,
                   tasks[i].interval,
                   tasks[i].active ? "active" : "suspended");
        }
    }
}

static void kill_task(int id) {
    for (int i = 0; i < MAX_TASKS; ++i) {
        if (tasks[i].id == id) {
            tasks[i].active = 0;
            tasks[i].id = 0;
            tasks[i].name[0] = '\0';
            tasks[i].msg[0] = '\0';
            tasks[i].fn = NULL;
            tasks[i].ticks = 0;
            task_count--;
            printf("Task %d removed.\n", id);
            return;
        }
    }
    printf("Task %d not found.\n", id);
}

static void suspend_task(int id) {
    task_t* t = task_find_by_id(id);
    if (!t) { printf("Task %d not found.\n", id); return; }
    t->active = 0;
    printf("Task %d suspended.\n", id);
}

static void resume_task(int id) {
    task_t* t = task_find_by_id(id);
    if (!t) { printf("Task %d not found.\n", id); return; }
    t->active = 1;
    printf("Task %d resumed.\n", id);
}

/* scheduler wrapper */
static void scheduler_tick_wrapper() { scheduler_tick(); }

/* Enable ANSI for Windows - safe */
static void enable_ansi_on_windows() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
#endif
}

/* print futuristic boot - uses sleep_ms rather than nanosleep for portability */
static void print_futuristic_boot() {
    enable_ansi_on_windows();
    clear_screen();
    printf("\x1b[36m"); /* cyan */
    printf("  ____  _  _  _  _  _____  _   _  _   _  ____  __  __\n");
    printf(" / ___|| || || || || ____|| \\ | || | | |/ ___||  \\/  |\n");
    printf("| |  _ | || || || ||  _|  |  \\| || | | | |  _ | |\\/| |\n");
    printf("| |_| ||__   __|__| | |___ | |\\  || |_| | |_| || |  | |\n");
    printf(" \\____|   |_|       |_____||_| \\_| \\___/ \\____||_|  |_|\n");
    printf("\x1b[0m\n");
    printf("\x1b[90mInitializing Shreyas Systems kernel modules...\x1b[0m\n");
    fflush(stdout);
    const char *lines[] = {
        "[ OK ] Initializing memory manager",
        "[ OK ] Initializing task scheduler",
        "[ OK ] Loading virtual filesystem",
        "[ OK ] Initializing network interfaces",
        "[ OK ] Starting userland shell"
    };
    for (int i = 0; i < (int)(sizeof(lines)/sizeof(lines[0])); ++i) {
        printf("  %s", lines[i]);
        for (int d = 0; d < 3; ++d) { printf("."); fflush(stdout); sleep_ms(150); }
        printf(" \x1b[32mOK\x1b[0m\n");
    }
    /* arc reactor spinner frames */
    const char *arc[4] = {
        "   .     \n  /|\\  \n  \\|/  \n   '    ",
        "   .     \n  -O-  \n  -O-  \n   '    ",
        "   .     \n  \\|/  \n  /|\\  \n   '    ",
        "   .     \n  -O-  \n  -O-  \n   '    "
    };
    for (int r = 0; r < 12; ++r) {
        clear_screen();
        printf("\x1b[36mShreyas Systems\x1b[0m\n\n");
        printf("\x1b[96m%s\x1b[0m\n", arc[r % 4]);
        printf("\nBooting");
        for (int d = 0; d < (r%4)+1; ++d) printf(".");
        fflush(stdout);
        sleep_ms(200);
    }
    clear_screen();
}

/* prompt builder */
static void build_prompt(char *out, size_t outsz) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    snprintf(out, outsz, "\x1b[36m[%s@%s %02d:%02d:%02d]\x1b[0m$ ", env_USER, env_HOSTNAME, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* shell executor */
static void shell_execute(const char* line) {
    if (!line) return;
    char cmd[128] = {0};
    char a1[512] = {0};
    char a2[1536] = {0};
    sscanf(line, "%127s %511s %1535[^\n]", cmd, a1, a2);
    if (strcmp(cmd, "help") == 0) show_help();
    else if (strcmp(cmd, "ls") == 0) vfs_list();
    else if (strcmp(cmd, "cat") == 0) {
        if (a1[0]=='\0') printf("Usage: cat <file>\n");
        else { vfile_t* f = vfs_find(a1); if (f) printf("%s\n", f->content); else printf("File not found: %s\n", a1); }
    }
    else if (strcmp(cmd, "write") == 0) {
        if (a1[0] == '\0' || a2[0] == '\0') printf("Usage: write <file> <text>\n");
        else { vfs_write(a1, a2); printf("Written to %s\n", a1); }
    }
    else if (strcmp(cmd, "append") == 0) {
        if (a1[0] == '\0' || a2[0] == '\0') printf("Usage: append <file> <text>\n");
        else { vfs_append(a1, a2); printf("Appended to %s\n", a1); }
    }
    else if (strcmp(cmd, "touch") == 0) {
        if (a1[0] == '\0') printf("Usage: touch <file>\n");
        else { vfs_write(a1, ""); printf("Touched %s\n", a1); }
    }
    else if (strcmp(cmd, "rm") == 0) {
        if (a1[0] == '\0') printf("Usage: rm <file>\n");
        else { if (vfs_remove(a1)) printf("Removed %s\n", a1); else printf("File not found: %s\n", a1); }
    }
    else if (strcmp(cmd, "spawn") == 0) {
        if (strcmp(a1, "clock")==0) { int id = spawn_builtin("clock", task_clock_builtin); if (id) printf("Spawned clock (id=%d)\n", id); else printf("Failed to spawn\n"); }
        else if (strcmp(a1, "heartbeat")==0) { int id = spawn_builtin("heartbeat", task_heartbeat_builtin); if (id) printf("Spawned heartbeat (id=%d)\n", id); else printf("Failed to spawn\n"); }
        else if (strcmp(a1, "logger")==0) { int id = spawn_builtin("logger", task_logger_builtin); if (id) printf("Spawned logger (id=%d)\n", id); else printf("Failed to spawn\n"); }
        else printf("Unknown builtin: %s\n", a1);
    }
    else if (strcmp(cmd, "addtask") == 0) {
        char name[MAX_NAME] = {0}; unsigned interval = 1; char msg[MAX_MSG] = {0};
        if (sscanf(line, "%*s %63s %u %511[^\n]", name, &interval, msg) >= 2) {
            if (name[0] == '\0' || msg[0] == '\0') printf("Usage: addtask <name> <interval> <message>\n");
            else { int id = spawn_message_task(name, interval, msg); if (id) printf("Added message task '%s' id=%d interval=%u\n", name, id, interval); else printf("Task limit reached.\n"); }
        } else printf("Usage: addtask <name> <interval> <message>\n");
    }
    else if (strcmp(cmd, "ps") == 0) show_ps();
    else if (strcmp(cmd, "killtask") == 0) { int id = atoi(a1); if (id<=0) printf("Usage: killtask <id>\n"); else kill_task(id); }
    else if (strcmp(cmd, "suspend") == 0) { int id = atoi(a1); if (id<=0) printf("Usage: suspend <id>\n"); else suspend_task(id); }
    else if (strcmp(cmd, "resume") == 0) { int id = atoi(a1); if (id<=0) printf("Usage: resume <id>\n"); else resume_task(id); }
    else if (strcmp(cmd, "uptime")==0) show_uptime();
    else if (strcmp(cmd, "poweroff")==0) { printf("Shutting down Shreyas OS...\n"); vfs_save_state(); running = 0; }
    else if (strcmp(cmd, "reboot")==0) { printf("Rebooting Shreyas OS...\n"); vfs_save_state(); /* simple reboot: restart main loop by exit flag */ running = 2; }
    else if (strcmp(cmd, "powerbtn")==0) power_button_ui();
    else if (strcmp(cmd, "clear")==0) clear_screen();
    else if (strcmp(cmd, "echo")==0) { if (a1[0]=='\0') printf("\n"); else { if (a2[0] != '\0') printf("%s %s\n", a1, a2); else printf("%s\n", a1); } }
    else if (strcmp(cmd, "version")==0) printf("Shreyas OS Enhanced v2.0 - Stark Kernel CLI\n");
    else if (strcmp(cmd, "edit")==0) cmd_edit(a1);
    else if (strcmp(cmd, "compile")==0) compile_file(a1);
    else if (strcmp(cmd, "run")==0) { if (a1[0]=='\0') run_command(a2); else { char cmdbuf[2048]; if (a2[0] != '\0') snprintf(cmdbuf, sizeof(cmdbuf), "%s %s", a1, a2); else snprintf(cmdbuf, sizeof(cmdbuf), "%s", a1); run_command(cmdbuf); } }
    else if (strcmp(cmd, "ip")==0) show_ips();
    else if (strcmp(cmd, "export")==0) { if (a1[0] && a2[0]) export_to_disk(a1,a2); else printf("Usage: export <file_on_disk> <vfs_file>\n"); }
    else if (strcmp(cmd, "import")==0) { if (a1[0] && a2[0]) import_from_disk(a1,a2); else printf("Usage: import <vfs_file> <file_on_disk>\n"); }
    else if (strcmp(cmd, "date")==0) cmd_date();
    else if (strcmp(cmd, "cal")==0) {
        int m = 0, y = 0;
        if (a1[0]=='\0') { time_t t = time(NULL); struct tm tm = *localtime(&t); m = tm.tm_mon+1; y = tm.tm_year+1900; }
        else if (sscanf(a1, "%d", &m)==1) { if (a2[0]=='\0') { time_t t = time(NULL); struct tm tm = *localtime(&t); y = tm.tm_year+1900; } else sscanf(a2, "%d", &y); }
        cmd_cal(y, m);
    }
    else if (strcmp(cmd, "sysinfo")==0) cmd_sysinfo();
    else if (strcmp(cmd, "whoami")==0) cmd_whoami();
    else if (strcmp(cmd, "hostname")==0) cmd_hostname();
    else if (strcmp(cmd, "history")==0) show_history();
    else if (strcmp(cmd, "!!")==0) {
        if (hist_size==0) printf("No history\n");
        else {
            int last = (hist_pos - 1 + CMD_HISTORY) % CMD_HISTORY;
            char tmp[1024]; strncpy(tmp, cmd_history[last], sizeof(tmp)-1);
            printf("Repeating: %s\n", tmp);
            shell_execute(tmp);
        }
    }
    else if (strcmp(cmd, "man")==0) cmd_man(a1);
    else if (strcmp(cmd, "")==0) { /* ignore */ }
    else printf("Unknown command: %s. Try 'help'.\n", cmd);
    scheduler_tick_wrapper();
}

/* login */
static void login_sequence() {
    char user[64];
    printf("\x1b[96mShreyas Systems Login\x1b[0m\n");
    printf("login: ");
    fflush(stdout);
    if (!fgets(user, sizeof(user), stdin)) { strcpy(env_USER, "Tony"); return; }
    user[strcspn(user, "\n")] = '\0';
    if (user[0] != '\0') strncpy(env_USER, user, sizeof(env_USER)-1);
    printf("Password: ");
#ifdef _WIN32
    /* no echo on Windows */
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode & ~(ENABLE_ECHO_INPUT));
    char pass[128];
    if (!fgets(pass, sizeof(pass), stdin)) pass[0] = '\0';
    SetConsoleMode(h, mode);
#else
    system("stty -echo");
    char pass[128];
    if (!fgets(pass, sizeof(pass), stdin)) pass[0] = '\0';
    system("stty echo");
#endif
    printf("\nWelcome %s.\n", env_USER);
}

/* tiny hostname detection */
static void detect_hostname() {
#ifdef _WIN32
    char name[128];
    if (gethostname(name, sizeof(name)) == 0) strncpy(env_HOSTNAME, name, sizeof(env_HOSTNAME)-1);
#else
    struct utsname un;
    if (uname(&un) == 0) strncpy(env_HOSTNAME, un.nodename, sizeof(env_HOSTNAME)-1);
#endif
}

/* main */
int main(void) {
    start_time = time(NULL);
    enable_ansi_on_windows();
    print_futuristic_boot();
    vfs_init();
    detect_hostname();
    login_sequence();
    spawn_builtin("clock", task_clock_builtin);
    spawn_builtin("heartbeat", task_heartbeat_builtin);
    char line[2048];
    while (1) {
        if (!running) break;
        if (running == 2) {
            /* reboot: re-run boot sequence */
            running = 1;
            print_futuristic_boot();
            vfs_init();
            detect_hostname();
            spawn_builtin("clock", task_clock_builtin);
            spawn_builtin("heartbeat", task_heartbeat_builtin);
        }
        char prompt[PROMPT_BUFSZ];
        build_prompt(prompt, sizeof(prompt));
        printf("%s", prompt);
        fflush(stdout);
        read_line(line, sizeof(line));
        if (line[0] == '\0') { scheduler_tick_wrapper(); continue; }
        save_history_line(line);
        shell_execute(line);
    }
    printf("Shreyas OS exited.\n");
    vfs_save_state();
    return 0;
}
