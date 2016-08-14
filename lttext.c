/*
 *  lttext.c (as include): Parsing and text utility code
 *
 *  This code is supposed to be #included into the main ltjson.c file
 *  which makes the codebase less unwieldy but keep static namespace.
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2016 Conor F. O'Rourke. All rights reserved.
 */


#ifdef _LTJSON_INLINE_INCLUDE_


/* This extern can be set by the caller to change the total (so
   includes the header) allocated block size for string stores.
   A good rule of thumb is to set it to 2048 or 4096 (a page) */

int ltjson_allocsize_sstore;


/*
 *  The string store is implemented using a doubly linked
 *  list of allocated blocks where (const) strings can be stored.
 *  By using a doubly linked list, the memory can be cleared by
 *  just moving to the end and reusing the previous blocks.
 *
 *  With UTF-8, you should theoretically use an unsigned form like
 *  uint8_t, but unsigned isn't a magic wand to fix interpretations
 *  so the code is aware that c>127 is a normal possibility instead.
 *
 *  Besides Thompson & Pike didn't use unsigned char so I won't.
 */


/*
 *  sstore_new() - Just return null (context is an opaque handle)
 */

static void *sstore_new(void)
{
    return NULL;
}




/*
 *  sstore_nadd() - Add string of length n to sstore
 *
 *  Returns: pointer to string on success
 *           NULL if out of memory (errno to ENOMEM)
 */

static char *sstore_nadd(void **ctxp, const char *str, int n)
{
    struct sstore *sstore, *curstore;
    int allocsize, sneeds;
    char *newstr;

    assert(ctxp && str);

    if (n <= 0)
        n = strlen(str);

    sstore = (struct sstore *)*ctxp;
    sneeds = n + 1;


    for (curstore = sstore; curstore != NULL; curstore = curstore->next)
    {
        if (curstore->bavail >= sneeds)
            break;
    }


    if (!curstore && sstore)
    {
        /* If sstore exists and points half way down the list, which
           may be the case if reusing the blocks, we can back up the
           list to see if we get a block where the string fits... */

        while (sstore->prev)
        {
            sstore = sstore->prev;

            sstore->bavail = sstore->balloc;    /* Clear the block */
            if (sstore->bavail >= sneeds)
            {
                curstore = sstore;
                break;
            }
        }

        *ctxp = (void *)sstore;     /* sstore may have moved up */
    }


    if (!curstore)
    {
        /* Need to allocate a new block */

        if (!ltjson_allocsize_sstore)
        {
            allocsize = SSTORE_DEF_ALLOC;       /* Default */
        }
        else
        {
            /* ltjson_allocsize_sstore is specified as including the header.
               allocsize does not include the header. Reason is to be able to
               allocate a single 4k page that includes header and string */

            allocsize = (int)ltjson_allocsize_sstore - (int)sizeof(struct sstore);
            if (allocsize < SSTORE_MIN_ALLOC)
                allocsize = SSTORE_MIN_ALLOC;
        }

        if (sneeds > allocsize)
            allocsize = sneeds;

        curstore = malloc(allocsize + sizeof(struct sstore));
        if (!curstore)
        {
            errno = ENOMEM;
            return NULL;
        }

        /* Initialise entry and chain it into list (if one) */

        curstore->balloc = allocsize;
        curstore->bavail = allocsize;

        curstore->prev = NULL;
        curstore->next = sstore;

        if (sstore)
            sstore->prev = curstore;

        sstore = curstore;
        *ctxp = (void *)sstore;
    }

    newstr = (char *)curstore + sizeof(struct sstore)
                              + (curstore->balloc - curstore->bavail);

    strncpy(newstr, str, n);
    newstr[n] = 0;

    curstore->bavail -= sneeds;
    return newstr;
}


static char *sstore_add(void **ctxp, const char *str)
{
    return sstore_nadd(ctxp, str, 0);
}




/*
 *  sstore_clear() - Set string blocklist to be reused
 */

static void sstore_clear(void **ctxp)
{
    struct sstore *sstore;

    if (!ctxp || !*ctxp)
        return;

    sstore = (struct sstore *)*ctxp;

    /* Just move to the end of the list and clear that block */

    while (sstore->next)
        sstore = sstore->next;

    sstore->bavail = sstore->balloc;
    *ctxp = (void *)sstore;
}




/*
 *  sstore_stats() - Get string store statistics, return total memory
 */

static int sstore_stats(void **ctxp,
                        int *sblocks, int *salloc, int *sfilled)
{
    struct sstore *sstore;
    int bcount, acount, fcount;

    if (!ctxp || !*ctxp)
    {
        if (sblocks)
            *sblocks = 0;
        if (salloc)
            *salloc = 0;
        if (sfilled)
            *sfilled = 0;

        return 0;
    }

    sstore = (struct sstore *)*ctxp;

    /* Move to the start of the list */

    while (sstore->prev)
        sstore = sstore->prev;

    bcount = acount = fcount = 0;

    while (sstore)
    {
        bcount++;
        acount += sstore->balloc;
        fcount += sstore->balloc - sstore->bavail;

        sstore = sstore->next;
    }

    if (sblocks)
        *sblocks = bcount;
    if (salloc)
        *salloc = acount;
    if (sfilled)
        *sfilled = fcount;

    return (bcount * sizeof(struct sstore)) + acount;
}




/*
 *  sstore_free() - Free the entire block list
 */

static void sstore_free(void **ctxp)
{
    struct sstore *sstore, *nextstore;

    if (!ctxp || !*ctxp)
        return;

    sstore = (struct sstore *)*ctxp;

    /* First, back up to the start, then clear down the chain */

    while (sstore->prev)
        sstore = sstore->prev;

    while (sstore)
    {
        nextstore = sstore->next;
        free(sstore);
        sstore = nextstore;
    }

    *ctxp = NULL;
}




/*
 *  skip_space(s) - Move s to first nonspace and return s
 */

static const char *skip_space(const char *s)
{
    if (!s)
        return 0;

    while (*s && c_isspace(*s))
        s++;

    return s;
}




/*
 *  hex_to_dec - converts one hex character to its decimal value
 *
 *  Takes one character in range [0-9a-fA-F] and converts it to the
 *  the decimal value of that hex character (eg: 15 for 'F')
 *  Returns converted value or -1 on invalid range (including negative)
 */

static int hex_to_dec(int ch)
{
    /* c_ defines will cast ch to unsigned char */

    if (c_isdigit(ch))
        return ch - '0';

    else if (c_isxdigit(ch))
        return c_tolower(ch) - 'a' + 10;

    else
        return -1;
}




/*
 *  string_to_codepoint(s) - Convert XXXX to integer
 *
 *  Takes a 4 byte hex sequence in string s and converts it to
 *  an integer in the range 0x0000 to 0xFFFF or -1 on error.
 */

static int string_to_codepoint(char *s)
{
    int a, b, c, d;

    if (!s || !s[0] || !s[1] || !s[2] || !s[3])
        return -1;

    a = hex_to_dec(s[0]);
    b = hex_to_dec(s[1]);
    c = hex_to_dec(s[2]);
    d = hex_to_dec(s[3]);

    if (a < 0 || b < 0 || c < 0 || d < 0)
        return -1;

    return (a * 0x1000 + b * 0x100 + c * 0x10 + d);
}




/*
 *  Classic UTF-8 lookup table (Pike and Thompson)
 */

static struct
{
    int cntmask;    /* Mask the count byte to    */
    int cntval;     /* give this value           */
    int resmask;    /* mask the result for value */
} utf8tab[] =
{
     /* mask    value,  resmask */
    {   0x00,   0x00,   0x00      },  /* 0 chars not useful */
    {   0x80,   0x00,   0x7F      },  /* 1 byte sequence    */
    {   0xE0,   0xC0,   0x7FF     },  /* 2   "     "        */
    {   0xF0,   0xE0,   0xFFFF    },  /* 3   "     "        */
    {   0x00,   0x00,   0x00      }   /* End markers        */
};




/*
 *  codepoint_to_utf8(codept, dest) - Convert Unicode to UTF-8
 *
 *  Take a codepoint up to 0xFFFF and convert into a maximum of a 3 byte
 *  UTF-8 sequence in *dest. (dest is a char *, probably signed).
 *
 *  Warning: No buffer length checks. No terminator written.
 *
 *  Returns the number of bytes written or 0 on error
 */

static int codepoint_to_utf8(int codept, char *dest)
{
    int nchars, bitshift;

    if (codept <= 0 || !dest)
        return 0;

    for (nchars = 1; utf8tab[nchars].cntmask; nchars++)
    {
        /* The resmask gives the maximum unicode value for a
           particular number of UTF-8 characters */

        if (codept <= utf8tab[nchars].resmask)
            break;
    }

    if (utf8tab[nchars].cntmask == 0)
        return 0;


    /* For two bytes the max codept is 0x7FF = xxx xxaa aaaa (11 bits)

       UTF-8 rep =       110xxxxx           ;          10aaaaaa
                 = cntval | (codept >> 6)   ;   0x80 | ((codept >> 0) & 0x3F)

       Two bytes   -> shift 6, then shift 0
       Three bytes -> shift 12, then shift 6 then shift 0 ... etc
    */

    bitshift = (nchars - 1) * 6;

    *dest++ = utf8tab[nchars].cntval | (codept >> bitshift);

    while (bitshift > 0)
    {
        bitshift -= 6;
        *dest++ = 0x80 | ((codept >> bitshift) & 0x3F);
    }

    return nchars;
}




/*
 *  unescape_string(s) - Unescape string in place
 *
 *  Search the string s for escape sequences (including \uXXXX for unicode)
 *  and decode them in place. The (shorter) result is terminated properly.
 *
 *  Returns 1 on success, 0 on sequence error
 */

static int unescape_string(char *s)
{
    char *d;

    if (!s)
        return 0;

    if ((s = strchr(s, '\\')) == NULL)
        return 1;

    d = s;

    do {

        if (*s != '\\')
        {
            *d++ = *s;
            continue;
        }

        /* s is a backslash */

        switch (*++s)
        {
            case '\\':
                *d++ = '\\';
            break;

            case '/':
                *d++ = '/';
            break;

            case '"':
                *d++ = '"';
            break;

            case 't':
                *d++ = '\t';
            break;

            case 'f':
                *d++ = '\f';
            break;

            case 'r':
                *d++ = '\r';
            break;

            case 'n':
                *d++ = '\n';
            break;

            case 'u':
            {
                /* Format is: \uXXXX - a max of a 3 byte utf8 sequence */
                int codepoint, i;

                s++;
                codepoint = string_to_codepoint(s);
                if (codepoint <= 0)
                    return 0;
                s += 3;

                i = codepoint_to_utf8(codepoint, d);
                if (!i)
                    return 0;
                d += i;
            }
            break;

            default:
                return 0;

        }   /* switch... */

        /* s is pointing to the last character decoded */

    } while (*s++);

    return 1;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
