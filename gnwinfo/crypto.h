#ifndef GNWINFO_CRYPTO_H
#define GNWINFO_CRYPTO_H

#include <windows.h>

void gnwinfo_encrypt_file(const char* filepath);
void gnwinfo_encrypt_fileW(const WCHAR* filepath);
void gnwinfo_decrypt_file(const char* filepath);
void gnwinfo_decrypt_fileW(const WCHAR* filepath);
char* gnwinfo_read_encrypted_fileW(const WCHAR* filepath, size_t* out_size);
char* gnwinfo_read_encrypted_file(const char* filepath, size_t* out_size);

#endif