/*
 *  lttext.c (as include): Parsing and text utility code
 *
 *  This code is supposed to be #included into the main ltjson.c file
 *  which makes the codebase less unwieldy but keep static namespace.
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2015 Conor F. O'Rourke. All rights reserved.
 */


#ifdef _LTJSON_INLINE_INCLUDE_


/*
 *  strip_space(s) - Move s to first nonspace and return s
 */

static unsigned char *strip_space(unsigned char *s)
{
    if (!s)
        return 0;

    while (*s && isspace(*s))
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
    if (ch <= 0 || ch > 0xFF)     /* is*() undefined otherwise */
        return -1;

    if (isdigit(ch))
        return ch - '0';

    else if (isxdigit(ch))
        return tolower(ch) - 'a' + 10;

    else
        return -1;
}




/*
 *  string_to_codepoint(s) - Convert XXXX to integer
 *
 *  Takes a 4 byte hex sequence in string s and converts it to
 *  an integer in the range 0x0000 to 0xFFFF or -1 on error.
 */

static int string_to_codepoint(unsigned char *s)
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
    unsigned int cntmask;       /* Mask the count byte to    */
    unsigned int cntval;        /* give this value           */
    unsigned int resmask;       /* mask the result for value */
} utf8tab[] =  {
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
 *  UTF-8 sequence in *dest.
 *
 *  No buffer length checks are done. No terminator is written.
 *
 *  Returns the number of bytes written or -1 on error
 */

static int codepoint_to_utf8(int codept, unsigned char *dest)
{
    int nchars, bitshift;

    if (codept <= 0 || !dest)
        return -1;

    for (nchars = 1; utf8tab[nchars].cntmask; nchars++)
    {
        /* The resmask gives the maximum unicode value for a
           particular number of UTF-8 characters */

        if (codept <= utf8tab[nchars].resmask)
            break;
    }

    if (utf8tab[nchars].cntmask == 0)
        return -1;


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
 *  and decodes them in place. The (shorter) result is terminated properly.
 *  s is expected to start with a " and that " is trimmed off.
 *
 *  Returns the number of bytes trimmed (>= 1) or -1 on error
 */

static int unescape_string(unsigned char *s)
{
    unsigned char *d;

    if (!s || *s != '"')
        return -1;

    d = s++;

    do
    {
        if (*s == '\\')
        {
            s++;

            switch (*s)
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
                    return -1;
                s += 3;

                i = codepoint_to_utf8(codepoint, d);
                if (i <= 0)
                    return -1;
                d += i;
            }
            break;

            default:
                return -1;
            }

        }   /* if (s == '\\') */

        else
        {
            *d++ = *s;
        }

    } while (*s++);

    return s - d;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
