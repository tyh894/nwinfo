#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <shellapi.h>

#define GNW_MAGIC "GNWENC\x01"
#define GNW_MAGIC_LEN 7
#define GNW_SALT_LEN 8
#define GNW_KEY "GnW!nf0_S3cr3t_2026"

static void xor_data(char* data, size_t size, const char* salt)
{
    size_t key_len = strlen(GNW_KEY);
    for (size_t i = 0; i < size; i++) {
        char k = GNW_KEY[i % key_len] ^ salt[i % GNW_SALT_LEN];
        data[i] ^= k;
    }
}

void gnwinfo_encrypt_fileW(const WCHAR* filepath)
{
    FILE* fp = NULL;
    if (_wfopen_s(&fp, filepath, L"rb") != 0 || !fp) return;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* buf = (char*)malloc(size);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, size, fp);
    fclose(fp);
    
    // Check if already encrypted
    if (size >= GNW_MAGIC_LEN && memcmp(buf, GNW_MAGIC, GNW_MAGIC_LEN) == 0) {
        free(buf);
        return; 
    }
    
    srand((unsigned)time(NULL));
    char salt[GNW_SALT_LEN];
    for (int i = 0; i < GNW_SALT_LEN; i++) salt[i] = rand() % 256;
    
    char* enc = (char*)malloc(size + GNW_MAGIC_LEN + GNW_SALT_LEN);
    if (!enc) { free(buf); return; }
    
    memcpy(enc, GNW_MAGIC, GNW_MAGIC_LEN);
    memcpy(enc + GNW_MAGIC_LEN, salt, GNW_SALT_LEN);
    memcpy(enc + GNW_MAGIC_LEN + GNW_SALT_LEN, buf, size);
    
    xor_data(enc + GNW_MAGIC_LEN + GNW_SALT_LEN, size, salt);
    
    if (_wfopen_s(&fp, filepath, L"wb") == 0 && fp) {
        fwrite(enc, 1, size + GNW_MAGIC_LEN + GNW_SALT_LEN, fp);
        fclose(fp);
    }
    free(buf);
    free(enc);
}

void gnwinfo_encrypt_file(const char* filepath)
{
    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, MAX_PATH);
    gnwinfo_encrypt_fileW(wpath);
}

void gnwinfo_decrypt_fileW(const WCHAR* filepath)
{
    FILE* fp = NULL;
    if (_wfopen_s(&fp, filepath, L"rb") != 0 || !fp) return;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size < GNW_MAGIC_LEN + GNW_SALT_LEN) {
        fclose(fp);
        return;
    }
    
    char* buf = (char*)malloc(size);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, size, fp);
    fclose(fp);
    
    if (memcmp(buf, GNW_MAGIC, GNW_MAGIC_LEN) != 0) {
        free(buf);
        return; // Not encrypted
    }
    
    char salt[GNW_SALT_LEN];
    memcpy(salt, buf + GNW_MAGIC_LEN, GNW_SALT_LEN);
    
    long data_size = size - GNW_MAGIC_LEN - GNW_SALT_LEN;
    char* dec = (char*)malloc(data_size);
    if (!dec) { free(buf); return; }
    
    memcpy(dec, buf + GNW_MAGIC_LEN + GNW_SALT_LEN, data_size);
    xor_data(dec, data_size, salt);
    
    if (_wfopen_s(&fp, filepath, L"wb") == 0 && fp) {
        fwrite(dec, 1, data_size, fp);
        fclose(fp);
    }
    free(buf);
    free(dec);
}

void gnwinfo_decrypt_file(const char* filepath)
{
    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH);
    gnwinfo_decrypt_fileW(wpath);
}

char* gnwinfo_read_encrypted_fileW(const WCHAR* filepath, size_t* out_size)
{
    FILE* fp = NULL;
    if (_wfopen_s(&fp, filepath, L"rb") != 0 || !fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size == 0) {
        fclose(fp);
        return NULL;
    }
    
    char* buf = (char*)malloc(size + 1);
    if (!buf) { fclose(fp); return NULL; }
    
    size_t read_size = fread(buf, 1, size, fp);
    fclose(fp);
    
    if (read_size >= GNW_MAGIC_LEN + GNW_SALT_LEN && memcmp(buf, GNW_MAGIC, GNW_MAGIC_LEN) == 0) {
        char salt[GNW_SALT_LEN];
        memcpy(salt, buf + GNW_MAGIC_LEN, GNW_SALT_LEN);
        long data_size = (long)read_size - GNW_MAGIC_LEN - GNW_SALT_LEN;
        
        char* dec = (char*)malloc(data_size + 1);
        if (dec) {
            memcpy(dec, buf + GNW_MAGIC_LEN + GNW_SALT_LEN, data_size);
            xor_data(dec, data_size, salt);
            dec[data_size] = '\0';
            if (out_size) *out_size = data_size;
            free(buf);
            return dec;
        }
    }
    
    buf[read_size] = '\0';
    if (out_size) *out_size = read_size;
    return buf; // Return as plaintext
}

char* gnwinfo_read_encrypted_file(const char* filepath, size_t* out_size)
{
    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH);
    return gnwinfo_read_encrypted_fileW(wpath, out_size);
}

static void traverse_and_decrypt(const WCHAR* dir)
{
    WCHAR search_path[MAX_PATH];
    swprintf_s(search_path, MAX_PATH, L"%s\\*", dir);
    
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        
        WCHAR full_path[MAX_PATH];
        swprintf_s(full_path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            traverse_and_decrypt(full_path);
        } else {
            // Decrypt .ps1, .psm1, .psd1
            size_t len = wcslen(fd.cFileName);
            if (len > 4 && (
                _wcsicmp(fd.cFileName + len - 4, L".ps1") == 0 ||
                _wcsicmp(fd.cFileName + len - 5, L".psm1") == 0 ||
                _wcsicmp(fd.cFileName + len - 5, L".psd1") == 0)) {
                gnwinfo_decrypt_fileW(full_path);
            }
        }
    } while (FindNextFileW(hFind, &fd));
    
    FindClose(hFind);
}

BOOL gnwinfo_decrypt_dir_to_tempW(const WCHAR* src_dir, const WCHAR* temp_dir)
{
    CreateDirectoryW(temp_dir, NULL);

    WCHAR search_path[MAX_PATH];
    swprintf_s(search_path, MAX_PATH, L"%s\\*", src_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        WCHAR src_path[MAX_PATH];
        swprintf_s(src_path, MAX_PATH, L"%s\\%s", src_dir, fd.cFileName);

        WCHAR dst_path[MAX_PATH];
        swprintf_s(dst_path, MAX_PATH, L"%s\\%s", temp_dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            gnwinfo_decrypt_dir_to_tempW(src_path, dst_path);
        } else {
            CopyFileW(src_path, dst_path, FALSE);
            // Decrypt if it's a PS script
            size_t len = wcslen(fd.cFileName);
            if (len > 4 && (
                _wcsicmp(fd.cFileName + len - 4, L".ps1") == 0 ||
                _wcsicmp(fd.cFileName + len - 5, L".psm1") == 0 ||
                _wcsicmp(fd.cFileName + len - 5, L".psd1") == 0)) {
                gnwinfo_decrypt_fileW(dst_path);
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return TRUE;
}

BOOL gnwinfo_delete_dirW(const WCHAR* dir)
{
    WCHAR search_path[MAX_PATH];
    swprintf_s(search_path, MAX_PATH, L"%s\\*", dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

            WCHAR full_path[MAX_PATH];
            swprintf_s(full_path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                gnwinfo_delete_dirW(full_path);
            } else {
                SetFileAttributesW(full_path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(full_path);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    SetFileAttributesW(dir, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(dir);
}
