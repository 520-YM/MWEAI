#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windows.h>
#include <ProToolkit.h>
#include <ProMdl.h>

static wchar_t runtime_data[MAX_PATH], store_session_root[MAX_PATH];
static int store_ready = 1, trace_count = 0, model_refreshes = 0;

static ProError fixture_directory_get(ProPath directory) {
    wcscpy_s(directory, PRO_PATH_SIZE, runtime_data);
    return PRO_TK_NO_ERROR;
}

static ProError fixture_config_set(const wchar_t *key, const wchar_t *value) {
    (void)key;
    (void)value;
    return PRO_TK_NO_ERROR;
}

#define ProDirectoryCurrentGet fixture_directory_get
#define ProConfigoptSet fixture_config_set
#include "../src/功能/runtime_paths/output_storage.inc"
#include "../src/功能/modeling_rules/modeling_rule_store.inc"
#undef ProConfigoptSet
#undef ProDirectoryCurrentGet

static void trace(const char *label, int code) {
    ++trace_count;
    fprintf(stderr, "%s: %d\n", label, code);
}

#include "../src/功能/local_control/live_task_events.inc"

static void bwtl_live_refresh_model(void) {
    ++model_refreshes;
}

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "line %d: %s (Windows error %lu)\n", __LINE__, #expression, GetLastError()); \
        return 1; \
    } \
} while (0)

static int append_raw(const wchar_t *path, const char *text) {
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD bytes = 0;
    DWORD length = (DWORD)strlen(text);
    int ok = WriteFile(file, text, length, &bytes, NULL) && bytes == length;
    CloseHandle(file);
    return ok;
}

static int read_text(const wchar_t *path, wchar_t *out, int capacity) {
    char bytes[8192];
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD length = 0;
    int ok = ReadFile(file, bytes, sizeof(bytes) - 1, &length, NULL);
    CloseHandle(file);
    if (!ok || !length) return 0;
    const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, (int)length, out, capacity - 1);
    if (!chars) return 0;
    out[chars] = 0;
    return 1;
}

int wmain(int argc, wchar_t **argv) {
    CHECK(argc == 2);
    CHECK(storage_directory_on_drive(argv[1], L'E'));
    CHECK(wcscpy_s(runtime_data, _countof(runtime_data), argv[1]) == 0);
    CHECK(swprintf_s(store_session_root, _countof(store_session_root), L"%ls\\journal-%lu-%llu",
        argv[1], GetCurrentProcessId(), GetTickCount64()) > 0);
    CHECK(CreateDirectoryW(store_session_root, NULL));

    CHECK(bwtl_live_open());
    wchar_t journal[MAX_PATH], other[MAX_PATH], rows[8192];
    CHECK(wcscpy_s(journal, _countof(journal), bwtl_live_path) == 0);
    CHECK(swprintf_s(other, _countof(other), L"%ls\\hard-link.jsonl", store_session_root) > 0);

    wcscpy_s(bwtl_live_model.name, _countof(bwtl_live_model.name), L"M26G2081024");
    wcscpy_s(bwtl_live_model.type, _countof(bwtl_live_model.type), L"PRT");
    wcscpy_s(bwtl_live_model.directory, _countof(bwtl_live_model.directory), argv[1]);
    wcscpy_s(bwtl_live_model.stamp, _countof(bwtl_live_model.stamp), L"20261124");
    bwtl_live_model.window = 1023;
    const wchar_t request[] = L"读取\t钣金\n引号\" 路径\\ 𝄞";
    bwtl_live_chat(L"用户", request);
    bwtl_live_chat(L"助手", L"已读取");
    CHECK(model_refreshes == 2 && bwtl_live_seq == 2 && bwtl_live_task == 1);
    bwtl_live_begin(1);
    bwtl_live_emit(L"native", 1, -62, L"原生回执");
    CHECK(bwtl_live_seq == 3 && bwtl_live_task == 3);
    bwtl_live_close();
    CHECK(append_raw(journal, "{\"seq\":4,\"task\":3,\"text\":\"torn-tail"));

    bwtl_live_seq = bwtl_live_task = 0;
    CHECK(bwtl_live_open());
    CHECK(bwtl_live_seq == 3 && bwtl_live_task == 3);
    bwtl_live_emit(L"native", 1, 0, L"重开后追加");
    bwtl_live_close();
    CHECK(read_text(journal, rows, _countof(rows)));
    CHECK(wcsstr(rows, L"\"seq\":1,\"task\":1,") && wcsstr(rows, L"\"seq\":2,\"task\":1,") &&
          wcsstr(rows, L"\"seq\":3,\"task\":3,") && wcsstr(rows, L"\"seq\":4,\"task\":3,"));
    CHECK(wcsstr(rows, L"\"kind\":\"user\",\"code\":null,\"window\":1023,"));
    CHECK(wcsstr(rows, L"\"kind\":\"native\",\"code\":-62,"));
    CHECK(wcsstr(rows, L"读取\\u0009钣金\\u000a引号\\\" 路径\\\\ \\ud834\\udd1e"));
    CHECK(wcsstr(rows, L"重开后追加") && !wcsstr(rows, L"torn-tail"));
    unsigned lines = 0;
    for (const wchar_t *walk = rows; *walk; ++walk) if (*walk == L'\n') ++lines;
    CHECK(lines == 4 && trace_count == 0);

    CHECK(CreateHardLinkW(other, journal, NULL));
    CHECK(!bwtl_live_open() && bwtl_live_file == INVALID_HANDLE_VALUE);
    CHECK(DeleteFileW(other));
    CHECK(bwtl_live_open());
    CHECK(bwtl_live_seq == 4 && bwtl_live_task == 3);
    bwtl_live_close();
    wprintf(L"PASS: live journal JSON escaping, UTF-8, task IDs, append, torn-tail recovery, hard-link refusal\n");
    wprintf(L"%ls\n", journal);
    return 0;
}