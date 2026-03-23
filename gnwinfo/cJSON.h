/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#ifndef CJSON_H
#define CJSON_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct cJSON
{
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;

typedef struct cJSON_Hooks
{
    void *(*malloc_fn)(size_t sz);
    void (*free_fn)(void *ptr);
} cJSON_Hooks;

#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7)

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

void cJSON_InitHooks(cJSON_Hooks* hooks);

cJSON *cJSON_Parse(const char *value);
cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length);
void cJSON_Delete(cJSON *item);

cJSON *cJSON_GetArrayItem(const cJSON *array, int index);
int cJSON_GetArraySize(const cJSON *array);
cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);

const char *cJSON_GetErrorPtr(void);

char *cJSON_Print(const cJSON *item);
char *cJSON_PrintUnformatted(const cJSON *item);
char *cJSON_PrintBuffered(const cJSON *item, int prebuffer, int fmt);
int cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const int format);

int cJSON_HasObjectItem(const cJSON *object, const char *string);

#define cJSON_AddNullToObject(object,name) cJSON_AddNullToObject(object,name)
#define cJSON_AddTrueToObject(object,name) cJSON_AddTrueToObject(object,name)
#define cJSON_AddFalseToObject(object,name) cJSON_AddFalseToObject(object,name)
#define cJSON_AddBoolToObject(object,name,b) cJSON_AddBoolToObject(object,name,b)
#define cJSON_AddNumberToObject(object,name,n) cJSON_AddNumberToObject(object,name,n)
#define cJSON_AddStringToObject(object,name,s) cJSON_AddStringToObject(object,name,s)
#define cJSON_AddRawToObject(object,name,s) cJSON_AddRawToObject(object,name,s)
#define cJSON_AddArrayToObject(object,name) cJSON_AddArrayToObject(object,name)
#define cJSON_AddObjectToObject(object,name) cJSON_AddObjectToObject(object,name)

int cJSON_GetStringValue(const cJSON *item, char **output);
double cJSON_GetNumberValue(const cJSON *item);

#ifdef __cplusplus
}
#endif

#endif
