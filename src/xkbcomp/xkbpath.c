/************************************************************
 Copyright (c) 1994 by Silicon Graphics Computer Systems, Inc.

 Permission to use, copy, modify, and distribute this
 software and its documentation for any purpose and without
 fee is hereby granted, provided that the above copyright
 notice appear in all copies and that both that copyright
 notice and this permission notice appear in supporting
 documentation, and that the name of Silicon Graphics not be
 used in advertising or publicity pertaining to distribution
 of the software without specific prior written permission.
 Silicon Graphics makes no representation about the suitability
 of this software for any purpose. It is provided "as is"
 without any express or implied warranty.

 SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
 SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
 GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
 DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
 THE USE OR PERFORMANCE OF THIS SOFTWARE.

 ********************************************************/

#define	DEBUG_VAR debugFlags
#include "utils.h"
#include <errno.h>
#include <stdlib.h>
#include "xkbpath.h"
#include "xkbcommon/xkbcommon.h"
#include "XKBcommonint.h"

#ifndef DFLT_XKB_CONFIG_ROOT
#define DFLT_XKB_CONFIG_ROOT	"/usr/lib/X11/xkb"
#endif

#ifndef PATH_MAX
#define	PATH_MAX 1024
#endif

/* initial szPath */
#define	PATH_CHUNK	8

static Bool noDefaultPath = False;
/* number of entries allocated for includePath */
static int szPath;
/* number of actual entries in includePath */
static int nPathEntries;
/* Holds all directories we might be including data from */
static char **includePath = NULL;

/**
 * Extract the first token from an include statement.
 * @param str_inout Input statement, modified in-place. Can be passed in
 * repeatedly. If str_inout is NULL, the parsing has completed.
 * @param file_rtrn Set to the include file to be used.
 * @param map_rtrn Set to whatever comes after ), if any.
 * @param nextop_rtrn Set to the next operation in the complete statement.
 * @param extra_data Set to the string between ( and ), if any.
 *
 * @return True if parsing was succcessful, False for an illegal string.
 *
 * Example: "evdev+aliases(qwerty)"
 *      str_inout = aliases(qwerty)
 *      nextop_retrn = +
 *      extra_data = NULL
 *      file_rtrn = evdev
 *      map_rtrn = NULL
 *
 * 2nd run with "aliases(qwerty)"
 *      str_inout = NULL
 *      file_rtrn = aliases
 *      map_rtrn = qwerty
 *      extra_data = NULL
 *      nextop_retrn = ""
 *
 */
Bool
XkbParseIncludeMap(char **str_inout, char **file_rtrn, char **map_rtrn,
                   char *nextop_rtrn, char **extra_data)
{
    char *tmp, *str, *next;

    str = *str_inout;
    if ((*str == '+') || (*str == '|'))
    {
        *file_rtrn = *map_rtrn = NULL;
        *nextop_rtrn = *str;
        next = str + 1;
    }
    else if (*str == '%')
    {
        *file_rtrn = *map_rtrn = NULL;
        *nextop_rtrn = str[1];
        next = str + 2;
    }
    else
    {
        /* search for tokens inside the string */
        next = strpbrk(str, "|+");
        if (next)
        {
            /* set nextop_rtrn to \0, next to next character */
            *nextop_rtrn = *next;
            *next++ = '\0';
        }
        else
        {
            *nextop_rtrn = '\0';
            next = NULL;
        }
        /* search for :, store result in extra_data */
        tmp = strchr(str, ':');
        if (tmp != NULL)
        {
            *tmp++ = '\0';
            *extra_data = _XkbDupString(tmp);
        }
        else
        {
            *extra_data = NULL;
        }
        tmp = strchr(str, '(');
        if (tmp == NULL)
        {
            *file_rtrn = _XkbDupString(str);
            *map_rtrn = NULL;
        }
        else if (str[0] == '(')
        {
            free(*extra_data);
            return False;
        }
        else
        {
            *tmp++ = '\0';
            *file_rtrn = _XkbDupString(str);
            str = tmp;
            tmp = strchr(str, ')');
            if ((tmp == NULL) || (tmp[1] != '\0'))
            {
                free(*file_rtrn);
                free(*extra_data);
                return False;
            }
            *tmp++ = '\0';
            *map_rtrn = _XkbDupString(str);
        }
    }
    if (*nextop_rtrn == '\0')
        *str_inout = NULL;
    else if ((*nextop_rtrn == '|') || (*nextop_rtrn == '+'))
        *str_inout = next;
    else
        return False;
    return True;
}

static void
XkbAddDefaultDirectoriesToPath(void);

/**
 * Init memory for include paths.
 */
static Bool
XkbInitIncludePath(void)
{
    if (includePath)
        return True;

    szPath = PATH_CHUNK;
    includePath = calloc(szPath, sizeof(char *));
    if (!includePath)
        return False;

    XkbAddDefaultDirectoriesToPath();
    return True;
}

/**
 * Remove all entries from the global includePath.
 */
static void
XkbClearIncludePath(void)
{
    int i;

    if (szPath > 0)
    {
        for (i = 0; i < nPathEntries; i++)
        {
            if (includePath[i] != NULL)
            {
                free(includePath[i]);
                includePath[i] = NULL;
            }
        }
        nPathEntries = 0;
    }
    noDefaultPath = True;
}

void
XkbFreeIncludePath(void)
{
    XkbClearIncludePath();
    free(includePath);
    includePath = NULL;
}

/**
 * Add the given path to the global includePath variable.
 * If dir is NULL, the includePath is emptied.
 */
static Bool
XkbAddDirectoryToPath(const char *dir)
{
    int len;

    if (!XkbInitIncludePath())
        return False;

    if ((dir == NULL) || (dir[0] == '\0'))
    {
        XkbClearIncludePath();
        return True;
    }
#ifdef __UNIXOS2__
    dir = (char *) __XOS2RedirRoot(dir);
#endif
    len = strlen(dir);
    if (len + 2 >= PATH_MAX)
    {                           /* allow for '/' and at least one character */
        ERROR("Path entry (%s) too long (maxiumum length is %d)\n",
               dir, PATH_MAX - 3);
        return False;
    }
    if (nPathEntries >= szPath)
    {
        szPath += PATH_CHUNK;
        includePath = realloc(includePath, szPath * sizeof(char *));
        if (includePath == NULL)
        {
            WSGO("Allocation failed (includePath)\n");
            return False;
        }
    }
    includePath[nPathEntries] = strdup(dir);
    if (includePath[nPathEntries] == NULL)
    {
        WSGO("Allocation failed (includePath[%d])\n", nPathEntries);
        return False;
    }
    nPathEntries++;
    return True;
}

static void
XkbAddDefaultDirectoriesToPath(void)
{
    if (!XkbInitIncludePath())
        return;
    if (noDefaultPath)
        return;
    XkbAddDirectoryToPath(DFLT_XKB_CONFIG_ROOT);
}

/***====================================================================***/

/**
 * Return the xkb directory based on the type.
 */
const char *
XkbDirectoryForInclude(unsigned type)
{
    switch (type)
    {
    case XkmSemanticsFile:
        return "semantics";
    case XkmLayoutFile:
        return "layout";
    case XkmKeymapFile:
        return "keymap";
    case XkmKeyNamesIndex:
        return "keycodes";
    case XkmTypesIndex:
        return "types";
    case XkmSymbolsIndex:
        return "symbols";
    case XkmCompatMapIndex:
        return "compat";
    case XkmGeometryFile:
    case XkmGeometryIndex:
        return "geometry";
    case XkmRulesFile:
        return "rules";
    default:
        return "";
    }
}

/***====================================================================***/

/**
 * Search for the given file name in the include directories.
 *
 * @param type one of XkbTypesIndex, XkbCompatMapIndex, ..., or
 * XkbSemanticsFile, XkmKeymapFile, ...
 * @param pathReturn is set to the full path of the file if found.
 *
 * @return an FD to the file or NULL. If NULL is returned, the value of
 * pathRtrn is undefined.
 */
FILE *
XkbFindFileInPath(const char *name, unsigned type, char **pathRtrn)
{
    int i, ret;
    FILE *file = NULL;
    char buf[PATH_MAX];
    const char *typeDir;

    if (!XkbInitIncludePath())
        return NULL;

    typeDir = XkbDirectoryForInclude(type);
    for (i = 0; i < nPathEntries; i++)
    {
        if (includePath[i] == NULL || *includePath[i] == '\0')
            continue;

        ret = snprintf(buf, sizeof(buf), "%s/%s/%s",
                       includePath[i], typeDir, name);
        if (ret >= sizeof(buf))
        {
            ERROR("File name (%s/%s/%s) too long\n", includePath[i],
                   typeDir, name);
            ACTION("Ignored\n");
            continue;
        }
        file = fopen(buf, "r");
        if (file == NULL) {
            ERROR("Couldn't open file (%s/%s/%s): %s\n", includePath[i],
                   typeDir, name, strerror(-errno));
            ACTION("Ignored\n");
            continue;
        }
        break;
    }

    if ((file != NULL) && (pathRtrn != NULL))
        *pathRtrn = strdup(buf);
    return file;
}
