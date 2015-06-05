/*
 *  JSON implementation in C
 *  Started one lazy summer day, thus the name!
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2015 Conor F. O'Rourke. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>

#include "lzjson.h"


#define LZJSON_NODEALLOCSIZE    32
#define LZJSON_STRINGINITSIZE   8
#define LZJSON_BLANKNAME        1

#define GET_NEW_NODE(nnode)      do { \
                                    nnode = get_new_node(); \
                                    if (!nnode)             \
                                        return -ENOMEM;     \
                                 } while (0)


static struct lzjson_node *g_basenode, *g_storenode;
static const char *g_lasterr;

struct stringstore {
    int nalloc;
    int nused;
    unsigned char str[1];
};


/*  Node layout
    -----------

    Base nodes hold the storage for the nodes. Each base node links to
    the next with .next. The first base node allocated has .next linked
    to the last allocated, thus its a ring buffer. All base nodes also
    have a pointer to the first node in .ancnode.

    One offset from that first base node is the JSON string storage node.
    It has .val.p pointing to a struct stringstore and .next pointing to
    the root node in the JSON tree. Note that .val.p can move at any time!

    The string storage node flags continuations by making the .ancnode
    point to the current node being processed. If halfway through a
    string .name will be non-zero and be the offset of the start of
    the half finished string.

    All strings are stored as offsets, not pointers (store can be realloced).

    Store reuse is allowed by passing a valid initial node pointer with
    no continuation set. If you want to stop a continuation in this case,
    pass a NULL text pointer. This will reinitialise the structures and
    return success.
*/


static struct lzjson_node *get_new_node(void)
{
    struct lzjson_node *newnode;

    if (g_basenode && g_basenode->val.l < LZJSON_NODEALLOCSIZE)
    {
        newnode = g_basenode + g_basenode->val.nused;
    }
    else
    {
        /* basenode is full. Check if .next points to the first node.
           If it does, allocate a new basenode and insert into ring.
           Else start using the basenode pointed to by .next */

        if (g_basenode == NULL || (g_basenode->next == g_basenode->ancnode))
        {
            /* Out of available buffers */

            newnode = malloc(LZJSON_NODEALLOCSIZE * sizeof(struct lzjson_node));
            if (newnode == NULL)
                return NULL;

            newnode->name      = 0;
            newnode->ntype     = LZJSON_BASE;
            newnode->val.nused = 1;

            if (g_basenode)
            {
                /* Insert into ring */
                newnode->next    = g_basenode->ancnode;
                newnode->ancnode = g_basenode->ancnode;
                g_basenode->next = newnode;
            }
            else
            {
                /* New ring */
                newnode->next    = newnode;
                newnode->ancnode = newnode;
            }
        }
        else
        {
            /* Reuse existing buffer */
            newnode = g_basenode->next;
            assert(newnode->val.nused == 1);
        }

        g_basenode = newnode;
        newnode++;
    }

    g_basenode->val.nused++;

    newnode->name    = 0;
    newnode->ntype   = LZJSON_EMPTY;
    newnode->next    = NULL;
    newnode->ancnode = NULL;

    return newnode;
}




static void destroy_tree(void)
{
    /* Frees all memory associated with the tree.
       If you have g_basenode, you have g_storenode */

    struct lzjson_node *node;
    struct stringstore *storep;

    assert(g_basenode && g_storenode);

    storep = (struct stringstore *)g_storenode->val.p;
    free(storep);

    /* basenodes are in a ring linked by .next. Break the
       ring first and then traverse from head to tail */

    node = g_basenode->next;    /* can be itself! */
    g_basenode->next = NULL;

    do {
        g_basenode = node;
        node = g_basenode->next;
        free(g_basenode);
    } while (node != NULL);

    g_basenode = g_storenode = 0;
}




static unsigned char *strip_space(unsigned char *s)
{
    if (!s)
        return 0;

    while (*s && isspace(*s))
        s++;

    return s;
}




static void recycle_tree(void)
{
    /* Tear down the stored tree information without removing
       the allocations. g_basenode and g_storenode are valid. */

    struct stringstore *storep;

    assert(g_basenode && g_storenode);

    storep = (struct stringstore *)g_storenode->val.p;
    storep->nused = 0;

    g_basenode = g_basenode->ancnode;

    do {
        g_basenode->val.nused = 1;
        g_basenode = g_basenode->next;
    } while (g_basenode != g_basenode->ancnode);

    /* g_storenode is valid, but this way is more pedantic! */

    g_storenode = get_new_node();
    assert(g_storenode);
    g_storenode->ntype = LZJSON_STORE;
    g_storenode->val.p = storep;
}




static int create_tree(void)
{
    /* Creates a new tree - the first basenode and the storenode
       Return 0 on success, -ENOMEM on failure (and cleans up) */

    struct stringstore *storep;

    assert(!g_basenode);

    GET_NEW_NODE(g_storenode);
    g_storenode->ntype = LZJSON_STORE;

    storep = malloc(sizeof(struct stringstore) +
                    LZJSON_STRINGINITSIZE - 1);
    if (!storep)
    {
        free(g_basenode);
        g_basenode = g_storenode = 0;
        return -ENOMEM;
    }

    storep->nalloc = LZJSON_STRINGINITSIZE;
    storep->nused  = 0;

    g_storenode->val.p = storep;
    return 0;
}




static int init_tree(unsigned char **textp, struct lzjson_node **curnodep)
{
    /* Initialise the tree:
       The first two nodes are done: g_basenode, g_storenode
       Add in the root of the json tree: storenode->jsonroot->blanknode
       text is consumed including the leading [ or { and any leading
       whitespace.
       Returns 0 on success, -ENOMEM or -EILSEQ on error (no cleanup)
    */

    struct lzjson_node *newnode, *curnode;
    struct stringstore *storep;
    char *sblank = " Blank";
    int i;
    unsigned char *text;


    storep = (struct stringstore *)g_storenode->val.p;
    text = *textp;

    /* Represent blank and zero offset strings */

    i = strlen(sblank) + 1;
    assert(i <= storep->nalloc);

    strcpy((char *)storep->str, sblank);
    storep->nused  = i;


    /* Before we parse, make up a root node which is either
       an object or an array (going with RFC4627 on this one).
       Both can have no name. root has no ancestor. */

    GET_NEW_NODE(newnode);

    text = strip_space(text);
    if (!*text)
    {
        g_lasterr = "First text is blank";
        return -EILSEQ;
    }

    if (*text == '{')
        newnode->ntype = LZJSON_OBJECT;
    else if (*text == '[')
        newnode->ntype = LZJSON_ARRAY;
    else
    {
        g_lasterr = "RFC4627 JSON starts with object or array";
        return -EILSEQ;
    }

    text++;
    text = strip_space(text);

    g_storenode->next = newnode;    /* Link store node to JSON root */
    curnode = newnode;              /* curnode is JSON root */

    /* All encounters with "{[," autocreate a blank node.
       An array parent will cause that node to have an blank name */

    GET_NEW_NODE(newnode);

    curnode->val.subnode = newnode;
    newnode->ancnode = curnode;

    if (newnode->ancnode->ntype == LZJSON_ARRAY)
        newnode->name = LZJSON_BLANKNAME;

    *curnodep = newnode;
    *textp = text;
    return 0;
}




static int store_strnum(unsigned char **textp)
{
    /* Proceed to store string, number or logic rep.
       For continuations, the first character distinguishes
       between 0, "0" and true with '', '"' or '!'.

       Returns the offset to the string in the store or
       -EILSEQ, -ENOMEM on error. -EAGAIN on continuation.
    */

    struct stringstore *storep;
    unsigned char *s = *textp;
    unsigned char *d;           /* dest string pointer */
    int dstart;                 /* dest start offset   */

    unsigned char prev = 0;
    int isnum = 0, islogic = 0, seenexp = 0, seenfrac = 0;


    assert(s && *s);
    storep = (struct stringstore *)g_storenode->val.p;

    dstart = storep->nused;
    d = storep->str + storep->nused;   /* Might not be valid! */

    if (g_storenode->name)
    {
        /* Half finished string with the start point stored
           in the store node's name field. Guaranteed to have at
           least one character in it and to be null terminated.
           d set above will then be one past the \0 */

        unsigned char *tp;

        dstart = g_storenode->name;
        g_storenode->name = 0;
        d--;                        /* drop the terminator */
        storep->nused--;

        tp = storep->str + dstart;  /* tmp pointer to start of string */

        if (*tp == '!')
        {
            islogic = 1;
        }
        else if (*tp != '"')
        {
            isnum = 1;

            /* rescan the number to see what things were set: */

            do {
                if (*tp == 'e')
                    seenexp = 1;
                else if (*tp == '.')
                    seenfrac = 1;
                else if (seenexp && (*tp == '+' || *tp == '-'))
                    seenexp = 2;
                tp++;

            } while (*tp);
        }

        prev = *(d - 1);
    }
    else
    {
        /* New string - initalise state */

        if (*s == '"')
        {
            prev = '\\';    /* hack! */
        }
        else if (*s == '-' || isdigit(*s))
        {
            isnum = 1;
        }
        else if (isalpha(*s))
        {
            islogic = 1;
            prev = '!';     /* Flag! */
        }
        else
        {
            assert(0);  /* Shouldn't get here */
        }
    }


    /* There will always be at least one character
       in the stream and, if a number, that character
       is always in the set [0-9-] */

    while (*s)
    {
        /* Need enough space for one char and one terminator */

        if (storep->nused >= storep->nalloc - 1)
        {
            int newsize;
            struct stringstore *newstore;

            /* Out of storage space - realloc */
            newsize  = storep->nalloc * 2;
            newsize  = newsize + sizeof(struct stringstore) - 1;
            newstore = realloc(storep, newsize);

            if (newstore == NULL)
                return -ENOMEM;

            storep = newstore;
            storep->nalloc = storep->nalloc * 2;
            g_storenode->val.p = storep;

            d = storep->str + storep->nused;    /* d may have moved */
        }

        /* Good for two! */

        if (islogic)
        {
            if (prev == '!')
            {
                /* Have to store the ! to mark a logic representation but
                   wasn't sure space available until this point. Store and
                   loop around without incrementing the s pointer */

                prev = 0;
                *d++ = '!';
                storep->nused++;
                continue;
            }

            if (!isalpha(*s))
            {
                *d = '\0';
                storep->nused++;
                *textp = s;
                return dstart;
            }
        }

        else if (!isnum)    /* string */
        {
            if (*s == '"' && prev != '\\')
            {
                *d = '\0';
                storep->nused++;

                *textp = ++s;
                return dstart + 1;  /* drop the quote */
            }
        }

        else    /* number */
        {
            if (isdigit(*s))
            {
                if (seenexp == 1)
                    seenexp = 2;
            }
            else if (*s == '-')
            {
                if (seenexp == 1)
                    seenexp = 2;
                else if (dstart != storep->nused)
                {
                    g_lasterr = "Unary minus only at start of number";
                    return -EILSEQ;
                }
            }
            else if (*s == '+')
            {
                if (seenexp == 1)
                    seenexp = 2;
                else
                {
                    g_lasterr = "Plus only allowed after exponent";
                    return -EILSEQ;
                }
            }
            else if (*s == 'e' || *s == 'E')
            {
                if (seenexp)
                {
                    g_lasterr = "Only one exponent per number";
                    return -EILSEQ;
                }
                seenexp = 1;
            }
            else if (*s == '.')
            {
                if (seenfrac)
                {
                    g_lasterr = "Only one fractional per number";
                    return -EILSEQ;
                }
                seenfrac = 1;
            }
            else
            {
                if (!isdigit(prev))
                {
                    g_lasterr = "Number must end in a number";
                    return -EILSEQ;
                }

                *d = '\0';
                storep->nused++;

                *textp = s;
                return dstart;
            }
        }

        prev = *s;

        if (seenexp == 1)
            *d = 'e';
        else
            *d = *s;

        storep->nused++;

        d++; s++;
    }

    /* Run out of string */

    *d = '\0';
    storep->nused++;
    g_storenode->name = dstart;   /* Flag continuation */

    *textp = s;
    return -EAGAIN;
}




static int convert_into_number(struct lzjson_node *node, int soffset)
{
    /* Convert the string at soffset into a number. Then delete
       the string as it's superfluous.
       Returns 0 on success or -EINVAL on conversion error
                                (and leaves the string alone)
    */

    struct stringstore *storep;
    unsigned char *numstr;
    char *endptr;
    double dval;
    long lval;


    storep = (struct stringstore *)g_storenode->val.p;
    numstr = storep->str + soffset;

    assert(isdigit(*numstr) || *numstr == '-');

    if (numstr[1] == '\0' && (*numstr == '-' || *numstr == '0') )
        return -EINVAL;

    if (strchr((char *)numstr, 'e') || strchr((char *)numstr, '.'))
    {
        errno = 0;
        dval = strtod((char *)numstr, &endptr);
        if (errno || *endptr)
            return -EINVAL;
        node->ntype = LZJSON_DOUBLE;
        node->val.d = dval;
    }
    else
    {
        errno = 0;
        lval = strtol((char *)numstr, &endptr, 10);
        if (errno || *endptr)
            return -EINVAL;
        node->ntype = LZJSON_INTEGER;
        node->val.l = lval;
    }

    storep->nused = soffset;    /* Vape string */
    return 0;
}




static int convert_into_logic(struct lzjson_node *node, int soffset)
{
    /* Convert the logic representation at soffset. Then delete
       the string as it's superfluous.
       Returns 0 on success or -EINVAL on conversion error
       (and leaves the string alone)
    */

    struct stringstore *storep;
    char *logstr;


    storep = (struct stringstore *)g_storenode->val.p;
    logstr = (char *)(storep->str + soffset);

    assert(*logstr == '!');
    logstr++;

    if (strcasecmp(logstr, "null") == 0)
    {
        node->ntype = LZJSON_NULL;
    }
    else if (strcasecmp(logstr, "true") == 0)
    {
        node->ntype = LZJSON_BOOL;
        node->val.l = 1;
    }
    else if (strcasecmp(logstr, "false") == 0)
    {
        node->ntype = LZJSON_BOOL;
        node->val.l = 0;
    }
    else
    {
        return -EINVAL;
    }

    storep->nused = soffset;    /* Vape string */
    return 0;
}




static int process_json_alnum(unsigned char **textp, struct lzjson_node *node)
{
    /* Process a string ("string type") a number (-0.12e23) or a logical ("true")
       entity pointed to by textp into node. Continuations are catered for.
       Returns 0 on success, 1 if we now expect a value (so next is ':') or
       -EILSEQ (reason in g_lasterr), -ENOMEM on error, -EAGAIN on continuation.
    */

    struct stringstore *storep;
    unsigned char first;
    int ret;

    storep = (struct stringstore *)g_storenode->val.p;

    first = **textp;
    assert(first);

    if (g_storenode->name)
        first = *(storep->str + g_storenode->name);


    if (first == '"')
    {
        /* String */

        if (node->ntype != LZJSON_EMPTY)
        {
            g_lasterr = "Unexpected string (missing comma?)";
            return -EILSEQ;
        }

        ret = store_strnum(textp);
        if (ret < 0)
            return ret;

        if (node->name)
        {
            node->ntype = LZJSON_STRING;
            node->val.s = ret;
            return 0;
        }
        else
        {
            node->name = ret;
            return 1;       /* Now expect a value */
        }
    }

    else if (first == '-' || isdigit(first))
    {
        /* Number */

        if ((node->ntype != LZJSON_EMPTY) || !node->name)
        {
            g_lasterr = "Unexpected number (missing name or comma)";
            return -EILSEQ;
        }

        ret = store_strnum(textp);
        if (ret < 0)
            return ret;

        if (convert_into_number(node, ret) != 0)
        {
            g_lasterr = "Cannot convert number representation";
            return -EILSEQ;
        }

        return 0;
    }

    else if (first == '!' || isalpha(first))
    {
        /* Logic (true/false/null) */

        if ((node->ntype != LZJSON_EMPTY) || !node->name)
        {
            g_lasterr = "Unexpected non-string text";
            return -EILSEQ;
        }

        ret = store_strnum(textp);
        if (ret < 0)
            return ret;

        if (convert_into_logic(node, ret) != 0)
        {
            g_lasterr = "Cannot convert logic representation";
            return -EILSEQ;
        }

        return 0;
    }

    assert(0);  /* should not happen */
    g_lasterr = "Tried to parse something and fell over";
    return -EILSEQ;
}




#define RETURNSTATE(rval)   do { \
                                if (rval == -EAGAIN)                \
                                    g_storenode->ancnode = curnode; \
                                if (rval == -ENOMEM)                \
                                    destroy_tree();                 \
                                *nodepp = g_basenode;               \
                                return rval;                        \
                            } while (0)




int lzjson_parse(unsigned char *text, struct lzjson_node **nodepp)
{
    /* Parse the JSON tree passed in as text into tree ptr pointed
       to by nodepp. If the tree is NULL, then a new tree is created.
       If the tree is not NULL, the storage is reused unless the tree
       is in continuation in which case the text is added on.
       If text is NULL, the tree is forced out of continuation.

       Returns 0 on success
               -EAGAIN if incomplete
               -EINVAL if invalid argument
               -EILSEQ if invalid JSON sequence (reason available)
               -ENOMEM if out of memory (and destroy the tree)
               +ve number indicating the number of leftover bytes
    */

    struct lzjson_node *newnode, *curnode;
    int ret, expect_a_colon = 0;


    g_lasterr = "No parse error";   /* Reset g_lasterr in case */

    if (!nodepp)
        return -EINVAL;

    g_basenode = *nodepp;   /* Can be NULL */

    if (g_basenode)
        g_storenode = g_basenode->ancnode + 1;

    if (text == NULL)
    {
        /* request to discontinue a tree. Continuations are
           held in the storenode in .ancnode and .name */

        if (!g_basenode)
            return 0;       /* Success of a sort */

        g_storenode->name = 0;
        g_storenode->ancnode = NULL;
        return 0;
    }

    if (g_basenode)
    {
        /* Is tree in continuation mode? */

        if (g_storenode->ancnode)
        {
            /* Tree is in continuation */
            curnode = g_storenode->ancnode;
            g_storenode->ancnode = NULL;
        }
        else
        {
            /* Tree store is being reused,
               recycle_tree will set g_storenode */

            recycle_tree();
            ret = init_tree(&text, &curnode);
            if (ret != 0)
                RETURNSTATE(ret);
        }
    }
    else
    {
        /* Passed tree point is NULL. New tree.
           create_tree will set g_storenode    */

        ret = create_tree();
        if (ret != 0)
            return ret;     /* -ENOMEM, memory is freed */

        ret = init_tree(&text, &curnode);
        if (ret != 0)
            RETURNSTATE(ret);
    }


    if (g_storenode->name > 0)
    {
        /* A continued tree has a partial string stored */

        ret = process_json_alnum(&text, curnode);
        if (ret < 0)
            RETURNSTATE(ret);

        if (ret == 1)
            expect_a_colon = 1;

        text = strip_space(text);
    }


    while(*text)
    {
        if (expect_a_colon && *text != ':')
        {
            g_lasterr = "Expected an name-value separator (:)";
            RETURNSTATE(-EILSEQ);
        }

        if (*text == '{' || *text == '[')
        {
            /* New object or array */

            if ((curnode->ntype != LZJSON_EMPTY) || !curnode->name)
            {
                g_lasterr = "Unexpected object/array (missing name or comma)";
                RETURNSTATE(-EILSEQ);
            }

            if (*text == '{')
                curnode->ntype = LZJSON_OBJECT;
            else
                curnode->ntype = LZJSON_ARRAY;

            /* Create the first subnode (blank) */

            newnode = get_new_node();
            if (!newnode)
                RETURNSTATE(-ENOMEM);

            curnode->val.subnode = newnode;
            newnode->ancnode = curnode;

            if (newnode->ancnode->ntype == LZJSON_ARRAY)
                newnode->name = LZJSON_BLANKNAME;

            curnode = newnode;
            text++;
        }

        else if (*text == '}')
        {
            if (curnode->ancnode->ntype != LZJSON_OBJECT)
            {
                g_lasterr = "Mismatched object closure";
                RETURNSTATE(-EILSEQ);
            }

            if (curnode->name && curnode->ntype == LZJSON_EMPTY)
            {
                g_lasterr = "Name with no value at object closure";
                RETURNSTATE(-EILSEQ);
            }

            curnode = curnode->ancnode;
            text++;

            if (curnode->ancnode == NULL)
            {
                /* That's it - we're at the top. Tree is closed. */
                text = strip_space(text);
                ret = 0;
                while (*text++)
                    ret++;
                RETURNSTATE(ret);
            }
        }

        else if (*text == ']')
        {
            if (curnode->ancnode->ntype != LZJSON_ARRAY)
            {
                g_lasterr = "Mismatched array closure";
                RETURNSTATE(-EILSEQ);
            }

            curnode = curnode->ancnode;
            text++;

            if (curnode->ancnode == NULL)
            {
                /* We're done */
                text = strip_space(text);
                ret = 0;
                while (*text++)
                    ret++;
                RETURNSTATE(ret);
            }
        }


        else if (*text == '"' || *text == '-' || isalnum(*text))
        {
            ret = process_json_alnum(&text, curnode);
            if (ret < 0)
                RETURNSTATE(ret);

            if (ret == 1)
                expect_a_colon = 1;
        }

        else if (*text == ':')
        {
            /* Separator between name and value */

            if (!expect_a_colon)
            {
                g_lasterr = "Unexpected name value separator (:)";
                RETURNSTATE(-EILSEQ);
            }
            expect_a_colon = 0;
            text++;
        }

        else if (*text == ',')
        {
            /* Separator between values */

            if (curnode->ntype == LZJSON_EMPTY)
            {
                g_lasterr = "Comma after empty value";
                RETURNSTATE(-EILSEQ);
            }

            /* autocreate new empty node */

            newnode = get_new_node();
            if (!newnode)
                RETURNSTATE(-ENOMEM);

            curnode->next = newnode;
            newnode->ancnode = curnode->ancnode;

            if (newnode->ancnode->ntype == LZJSON_ARRAY)
                newnode->name = LZJSON_BLANKNAME;

            curnode = newnode;
            text++;
        }

        else
        {
            g_lasterr = "Random unquoted text in content";
            RETURNSTATE(-EILSEQ);
        }

        text = strip_space(text);

    }   /* while(*text) */


    /* Ran out of text without closing the JSON tree. Ask for
       a continuance: */

    RETURNSTATE(-EAGAIN);
}




const char *lzjson_lasterror(void)
{
    return g_lasterr;
}




static void print_nodeinfo(struct lzjson_node *node,
                           struct stringstore *storep)
{
    /* Just print out the node info to stdout */

    if (node->ancnode && node->ancnode->ntype == LZJSON_OBJECT)
        printf("%s : ", storep->str + node->name);

    switch (node->ntype)
    {
        case LZJSON_EMPTY:
            printf("Empty\n");
        break;

        case LZJSON_NULL:
            printf("null\n");
        break;

        case LZJSON_BOOL:
            if (node->val.l)
                printf("true\n");
            else
                printf("false\n");
        break;

        case LZJSON_ARRAY:   printf("[\n"); break;
        case LZJSON_OBJECT:  printf("{\n"); break;

        case LZJSON_DOUBLE:
            printf("%g\n", node->val.d);
        break;

        case LZJSON_INTEGER:
            printf("%ld\n", node->val.l);
        break;

        case LZJSON_STRING:
            printf("\"%s\"\n", storep->str + node->val.s);
        break;

        default:
            printf("Node does not look valid\n");
    }

}




int lzjson_display_tree(struct lzjson_node *tree)
{
    /* Display the JSON tree passed in "tree" on stdout.
       Tree must be closed and valid.
       Returns 0 on success or -EINVAL if tree is not valid
    */

    struct lzjson_node *curnode, *snode;
    struct stringstore *storep;
    int depth = 0;

    if (!tree)
        return -EINVAL;

    /* Get the storenode and store pointer and check state */

    snode = tree->ancnode + 1;
    storep = (struct stringstore *)snode->val.p;

    if (snode->ancnode)         /* Tree in continuation mode */
        return -EINVAL;

    if (!snode->next)
        return -EINVAL;

    curnode = snode->next;

    printf("JSON tree:\n");

    do
    {
        printf("%*s", 4 + 4*depth, "    ");

        print_nodeinfo(curnode, storep);

        if (curnode->ntype == LZJSON_ARRAY ||
            curnode->ntype == LZJSON_OBJECT)
        {
            curnode = curnode->val.subnode;
            depth++;
        }
        else
        {
            if (curnode->next)
                curnode = curnode->next;
            else
            {
                while ((curnode = curnode->ancnode) != NULL)
                {
                    depth--;

                    printf("%*s", 4 + 4*depth, "    ");
                    if (curnode->ntype == LZJSON_ARRAY)
                        printf("]\n");
                    else
                        printf("}\n");

                    if (curnode->next)
                    {
                        curnode = curnode->next;
                        break;
                    }
                }
            }
        }

    } while (curnode);

    return 0;
}




int lzjson_search_name(struct lzjson_node *tree, const char *searchtxt,
                       struct lzjson_node **answerp)
{
    /* Search the JSON "tree" for "name" in an object.
       Tree must be closed and valid.
       Returns 0 on success with answerp populated (NULL for not found)
       or -EINVAL if tree is not valid
    */

    struct lzjson_node *curnode, *snode;
    struct stringstore *storep;

    if (!tree || !searchtxt || !answerp)
        return -EINVAL;

    /* Get the storenode and store pointer and check state */

    snode = tree->ancnode + 1;
    storep = (struct stringstore *)snode->val.p;

    if (snode->ancnode)         /* Tree in continuation mode */
        return -EINVAL;

    if (!snode->next)           /* No JSON tree */
        return -EINVAL;

    curnode = snode->next;

    do
    {
        /* This is easy enough as any valid entry whose ancestor is
           an object has a name entry greater than LZJSON_BLANKNAME */

        if (curnode->name > LZJSON_BLANKNAME)
        {
            /* Valid name string */

            if (strcasecmp((const char *)searchtxt,
                           (const char *)(storep->str + curnode->name)) == 0)
            {
                /* Found! */
                *answerp = curnode;
                return 0;
            }
        }

        if (curnode->ntype == LZJSON_ARRAY ||
            curnode->ntype == LZJSON_OBJECT)
        {
            curnode = curnode->val.subnode;
        }
        else
        {
            if (curnode->next)
                curnode = curnode->next;
            else
            {
                while ((curnode = curnode->ancnode) != NULL)
                {
                    if (curnode->next)
                    {
                        curnode = curnode->next;
                        break;
                    }
                }
            }
        }

    } while (curnode);

    *answerp = NULL;
    return 0;
}




const char *lzjson_get_name(struct lzjson_node *tree,
                            struct lzjson_node *node)
{
    /* Return the name of the node in the JSON "tree".
       Tree must be closed and valid.
       Returns a pointer to the string (utf-8) or NULL on any error
       including no name. A blank name will return "";
    */

    struct lzjson_node *snode;
    struct stringstore *storep;
    static const char *emptyname = "";

    if (!tree || !node)
        return 0;

    /* Get the storenode and store pointer and check state */

    snode = tree->ancnode + 1;
    storep = (struct stringstore *)snode->val.p;

    if (snode->ancnode)         /* Tree in continuation mode */
        return 0;

    if (!snode->next)           /* No JSON tree */
        return 0;

    if (node->name == 0)
        return 0;

    else if (node->name == LZJSON_BLANKNAME)
        return emptyname;

    return (const char *)(storep->str + node->name);
}




const char *lzjson_get_sval(struct lzjson_node *tree,
                            struct lzjson_node *node)
{
    /* Return the value of a string type node in the JSON "tree".
       Tree must be closed and valid.
       Returns a pointer to the string (utf-8) or NULL on any error.
    */

    struct lzjson_node *snode;
    struct stringstore *storep;

    if (!tree || !node)
        return 0;

    /* Get the storenode and store pointer and check state */

    snode = tree->ancnode + 1;
    storep = (struct stringstore *)snode->val.p;

    if (snode->ancnode)         /* Tree in continuation mode */
        return 0;

    if (!snode->next)           /* No JSON tree */
        return 0;

    if (node->ntype != LZJSON_STRING)
        return 0;

    return (const char *)(storep->str + node->val.s);
}


/* vi:set expandtab ts=4 sw=4: */
