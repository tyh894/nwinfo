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

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>
#include "cJSON.h"

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

static const char *global_ep = NULL;

void cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (!hooks)
    {
        cJSON_malloc = malloc;
        cJSON_free = free;
        return;
    }
    cJSON_malloc = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    cJSON_free = (hooks->free_fn) ? hooks->free_fn : free;
}

static cJSON* cJSON_New_Item(void)
{
    cJSON* node = (cJSON*)cJSON_malloc(sizeof(cJSON));
    if (node)
    {
        memset(node, 0, sizeof(cJSON));
    }
    return node;
}

void cJSON_Delete(cJSON *item)
{
    cJSON *next = NULL;
    while (item != NULL)
    {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && (item->child != NULL))
        {
            cJSON_Delete(item->child);
        }
        if (!(item->type & cJSON_IsReference) && (item->valuestring != NULL))
        {
            cJSON_free(item->valuestring);
        }
        if (item->string != NULL)
        {
            cJSON_free(item->string);
        }
        cJSON_free(item);
        item = next;
    }
}

static const char* skip_whitespace(const char *in)
{
    while (in && *in && (unsigned char)*in <= 32)
    {
        in++;
    }
    return in;
}

static const char* parse_string(cJSON *item, const char *str)
{
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;

    while (*ptr != '\"' && *ptr)
    {
        if (*ptr++ == '\\')
        {
            ptr++;
        }
        len++;
    }

    out = (char*)cJSON_malloc(len + 1);
    if (!out)
    {
        return NULL;
    }

    ptr = str + 1;
    ptr2 = out;
    while (*ptr != '\"' && *ptr)
    {
        if (*ptr != '\\')
        {
            *ptr2++ = *ptr++;
        }
        else
        {
            ptr++;
            switch (*ptr)
            {
                case 'b': *ptr2++ = '\b'; break;
                case 'f': *ptr2++ = '\f'; break;
                case 'n': *ptr2++ = '\n'; break;
                case 'r': *ptr2++ = '\r'; break;
                case 't': *ptr2++ = '\t'; break;
                case 'u':
                    {
                        int i = 0, uc = 0;
                        ptr++;
                        for (i = 0; i < 4; i++)
                        {
                            uc = uc * 16 + (isdigit((unsigned char)*ptr) ? *ptr - '0' : (tolower((unsigned char)*ptr) - 'a' + 10));
                            ptr++;
                        }
                        if (uc < 0x80)
                        {
                            *ptr2++ = (char)uc;
                        }
                        else if (uc < 0x800)
                        {
                            *ptr2++ = (char)(0xC0 | (uc >> 6));
                            *ptr2++ = (char)(0x80 | (uc & 0x3F));
                        }
                        else
                        {
                            *ptr2++ = (char)(0xE0 | (uc >> 12));
                            *ptr2++ = (char)(0x80 | ((uc >> 6) & 0x3F));
                            *ptr2++ = (char)(0x80 | (uc & 0x3F));
                        }
                        ptr--;
                    }
                    break;
                default: *ptr2++ = *ptr; break;
            }
            ptr++;
        }
    }
    *ptr2 = 0;
    if (*ptr == '\"')
    {
        ptr++;
    }
    item->valuestring = out;
    item->type = cJSON_String;
    return ptr;
}

static const char* parse_number(cJSON *item, const char *num)
{
    double n = 0;
    int sign = 1;
    int scale = 0;
    int subscale = 0;
    int signsubscale = 1;

    if (*num == '-')
    {
        sign = -1;
        num++;
    }

    while (isdigit((unsigned char)*num))
    {
        n = (n * 10.0) + (*num++ - '0');
    }

    if (*num == '.' && num[1] != '\0')
    {
        num++;
        while (isdigit((unsigned char)*num))
        {
            n = (n * 10.0) + (*num++ - '0');
            scale--;
        }
    }

    if (*num == 'e' || *num == 'E')
    {
        num++;
        if (*num == '+')
        {
            num++;
        }
        else if (*num == '-')
        {
            signsubscale = -1;
            num++;
        }
        while (isdigit((unsigned char)*num))
        {
            subscale = (subscale * 10) + (*num++ - '0');
        }
    }

    n = sign * n * pow(10.0, (scale + subscale * signsubscale));

    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return num;
}

static const char* parse_value(cJSON *item, const char *value);
static const char* parse_array(cJSON *item, const char *value);
static const char* parse_object(cJSON *item, const char *value);

static const char* parse_array(cJSON *item, const char *value)
{
    cJSON *child;
    if (*value != '[')
    {
        global_ep = value;
        return NULL;
    }

    item->type = cJSON_Array;
    value = skip_whitespace(value + 1);
    if (*value == ']')
    {
        return value + 1;
    }

    child = cJSON_New_Item();
    if (!child)
    {
        return NULL;
    }
    item->child = child;

    value = skip_whitespace(parse_value(child, skip_whitespace(value)));
    if (!value)
    {
        return NULL;
    }

    while (*value == ',')
    {
        cJSON *new_item;
        if (!(new_item = cJSON_New_Item()))
        {
            return NULL;
        }
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
        if (!value)
        {
            return NULL;
        }
    }

    if (*value == ']')
    {
        return value + 1;
    }
    global_ep = value;
    return NULL;
}

static const char* parse_object(cJSON *item, const char *value)
{
    cJSON *child;
    if (*value != '{')
    {
        global_ep = value;
        return NULL;
    }

    item->type = cJSON_Object;
    value = skip_whitespace(value + 1);
    if (*value == '}')
    {
        return value + 1;
    }

    child = cJSON_New_Item();
    if (!child)
    {
        return NULL;
    }
    item->child = child;

    value = skip_whitespace(parse_string(child, skip_whitespace(value)));
    if (!value)
    {
        return NULL;
    }
    child->string = child->valuestring;
    child->valuestring = NULL;

    if (*value != ':')
    {
        global_ep = value;
        return NULL;
    }
    value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
    if (!value)
    {
        return NULL;
    }

    while (*value == ',')
    {
        cJSON *new_item;
        if (!(new_item = cJSON_New_Item()))
        {
            return NULL;
        }
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip_whitespace(parse_string(child, skip_whitespace(value + 1)));
        if (!value)
        {
            return NULL;
        }
        child->string = child->valuestring;
        child->valuestring = NULL;

        if (*value != ':')
        {
            global_ep = value;
            return NULL;
        }
        value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
        if (!value)
        {
            return NULL;
        }
    }

    if (*value == '}')
    {
        return value + 1;
    }
    global_ep = value;
    return NULL;
}

static const char* parse_value(cJSON *item, const char *value)
{
    if (!value)
    {
        return NULL;
    }

    if (!strncmp(value, "null", 4))
    {
        item->type = cJSON_NULL;
        return value + 4;
    }
    if (!strncmp(value, "false", 5))
    {
        item->type = cJSON_False;
        item->valueint = 0;
        return value + 5;
    }
    if (!strncmp(value, "true", 4))
    {
        item->type = cJSON_True;
        item->valueint = 1;
        return value + 4;
    }
    if (*value == '\"')
    {
        return parse_string(item, value);
    }
    if (*value == '-' || (*value >= '0' && *value <= '9'))
    {
        return parse_number(item, value);
    }
    if (*value == '[')
    {
        return parse_array(item, value);
    }
    if (*value == '{')
    {
        return parse_object(item, value);
    }

    global_ep = value;
    return NULL;
}

cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithLength(value, strlen(value));
}

cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length)
{
    cJSON *item = NULL;
    global_ep = NULL;

    if (!value || buffer_length == 0)
    {
        return NULL;
    }

    item = cJSON_New_Item();
    if (!item)
    {
        return NULL;
    }

    value = skip_whitespace(value);
    if (!parse_value(item, value))
    {
        cJSON_Delete(item);
        return NULL;
    }

    return item;
}

const char *cJSON_GetErrorPtr(void)
{
    return global_ep;
}

int cJSON_GetArraySize(const cJSON *array)
{
    cJSON *child = NULL;
    int size = 0;

    if (array == NULL)
    {
        return 0;
    }

    child = array->child;
    while (child != NULL)
    {
        size++;
        child = child->next;
    }

    return size;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
    if (index < 0)
    {
        return NULL;
    }

    cJSON *child = array->child;
    while (child != NULL && index > 0)
    {
        index--;
        child = child->next;
    }

    return child;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItemCaseSensitive(object, string);
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string)
{
    cJSON *child = NULL;

    if (object == NULL || string == NULL)
    {
        return NULL;
    }

    child = object->child;
    while (child != NULL)
    {
        if (child->string && strcmp(child->string, string) == 0)
        {
            return child;
        }
        child = child->next;
    }

    return NULL;
}

int cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

int cJSON_GetStringValue(const cJSON *item, char **output)
{
    if (!item || !output)
    {
        return 0;
    }
    if (item->type != cJSON_String)
    {
        return 0;
    }
    *output = item->valuestring;
    return 1;
}

double cJSON_GetNumberValue(const cJSON *item)
{
    if (!item)
    {
        return 0.0;
    }
    return item->valuedouble;
}
