/*
 *  Light JSON: Yet another JSON implementation in C
 *
 *  The emphasis is on low memory usage and the ability to
 *  free, reuse and/or continue the in memory json tree.
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

#include "ltjson.h"


#define _LTJSON_INLINE_INCLUDE_


#include "lttext.c"     /* code inline include */


#define LTJSON_NODEALLOCSIZE    32
#define LTJSON_STRINGINITSIZE   8
#define LTJSON_BLANKNAMEOFF     1


typedef struct {

    ltjson_node_t rootnode;     /* Root node of tree (not a pointer) */

    ltjson_node_t *root;        /* Root of tree (pointer to above)   */
    ltjson_node_t *open;        /* Tree open? This is current node   */
    ltjson_node_t *cbasenode;   /* Current basenode                  */

    const char *lasterr;        /* 0 or description of error         */
    unsigned int fparse;        /* Floating point parsing rules      */
    int incomplete;             /* If !0, continue adding to string  */

    int salloc;                 /* bytes allocated in sstore pointer */
    int sused;                  /* Number used in sstore             */
    unsigned char *sstore;      /* Pointer to string storage alloc   */

} ltjson_info_t;


/*  Memory layout
    -------------

    Base nodes hold the storage for the nodes. Each base node links to
    the next with .next. The last base node allocated has .next linked
    to the first allocated, thus it's a ring buffer. All base nodes also
    have a pointer to the first node in .ancnode. If there's only one
    base node, it points to itself.

    A tree can be recycled by simply setting the base node nused entries
    to 1. The get_new_node function will reuse them.

    All strings are stored as offsets, not pointers, as the store can
    be moved in memory. The strings are converted to pointers when the
    tree is completed.

    Store reuse is allowed by passing a valid initial tree pointer with
    no continuation set. If you want to stop a continuation pass a NULL
    text pointer. This will reinitialise the structures and return success.

    The externally visible routines take and return an ltjson_node_t *
    which is actually a ltjson_info_t structure pointer with the first
    structure entry being an ltjson_node_t (which is the root of the tree).
    Thus the pointers are functionally equivalent without spurious
    information being visible or having to be passed about.
*/




/*
 *  create_tree(jsoninfo) - Creates info structures for the json tree
 *
 *  The JSON info root is default created with no base node allocations
 *  but a small initial string store (so two allocations).
 *
 *  If jsoninfo is passed as non-NULL, the tree is simply recycled and
 *  memory is set to be reused.
 *
 *  Returns: pointer to jsoninfo or NULL if out of memory
 */

static ltjson_info_t *create_tree(ltjson_info_t *jsoninfo)
{
    static const char *sblank = " Blank";
    int i;

    if (jsoninfo == NULL)
    {
        jsoninfo = malloc(sizeof(ltjson_info_t));
        if (!jsoninfo)
            return NULL;

        jsoninfo->sstore = malloc(LTJSON_STRINGINITSIZE);
        if (!jsoninfo->sstore)
        {
            free(jsoninfo);
            return NULL;
        }

        jsoninfo->salloc    = LTJSON_STRINGINITSIZE;
        jsoninfo->cbasenode = 0;
    }

    /* In order for an ltjson_info_t to look like an ltjson_node_t
       the first entry in the former must be the latter. And be
       initialised as per get_new_node(). Then use the root entry
       to point to that node_t (more consistent pointer usage).

       .cbasenode, .sstore and .salloc will already be valid or are
       set in the above code. Init everything else:
    */

    jsoninfo->rootnode.name    = 0;
    jsoninfo->rootnode.nameoff = 0;
    jsoninfo->rootnode.ntype   = LTJSON_EMPTY;
    jsoninfo->rootnode.next    = NULL;
    jsoninfo->rootnode.ancnode = NULL;

    jsoninfo->root = &jsoninfo->rootnode;
    jsoninfo->open = 0;

    jsoninfo->lasterr    = 0;
    jsoninfo->fparse     = 0;
    jsoninfo->incomplete = 0;

    /* Represent blank and zero offset strings as
       offsets of 0 and 1 have specific meanings */

    i = strlen(sblank) + 1;
    assert(i <= jsoninfo->salloc);

    strcpy((char *)jsoninfo->sstore, sblank);
    jsoninfo->sused  = i;


    if (jsoninfo->cbasenode)
    {
        /* Existing basenodes. Mark each one to be reused */

        ltjson_node_t *basenode;

        basenode = jsoninfo->cbasenode->ancnode;    /* First basenode */

        do {
            basenode->val.nused = 1;
            basenode = basenode->next;
        } while (basenode != basenode->ancnode);

        /* Set current basenode to be the first basenode: */

        jsoninfo->cbasenode = basenode;
    }

    return jsoninfo;
}




/*
 *  destroy_tree(jsoninfo) - Frees all memory associated with jsoninfo
 *
 *  This uses the information in jsoninfo to free all memory associated
 *  with the tree. It then frees jsoninfo (the pointer is thus invalid).
 *  The minimum to free is that created by create_tree(0) above.
 */

static void destroy_tree(ltjson_info_t *jsoninfo)
{
    ltjson_node_t *basenode, *node;

    if (!jsoninfo)
        return;

    free(jsoninfo->sstore);

    if (!jsoninfo->cbasenode)
    {
        free(jsoninfo);
        return;
    }

    basenode = jsoninfo->cbasenode;

    /* basenodes are in a ring linked by .next. Break the
       ring first and then traverse from head to tail */

    node = basenode->next;    /* can be itself! */
    basenode->next = NULL;

    do {
        basenode = node;
        node = basenode->next;
        free(basenode);
    } while (node != NULL);

    /* finally, free the info structure itself */

    free(jsoninfo);
}




/*
 *  get_new_node(jsoninfo) - Get a new node, allocating as required
 *
 *  jsoninfo is a pointer to the json info struct and must be valid.
 *
 *  Returns: a pointer to an initialised empty new node on success
 *           NULL if out of memory (which will destroy the tree)
 */

static ltjson_node_t *get_new_node(ltjson_info_t *jsoninfo)
{
    ltjson_node_t *newnode, *basenode;

    assert(jsoninfo);

    basenode = jsoninfo->cbasenode;

    if (basenode && basenode->val.nused < LTJSON_NODEALLOCSIZE)
    {
        newnode = basenode + basenode->val.nused;
    }
    else
    {
        /* current basenode is full. Check if .next points to the first node
           (end of the ring). If it does, allocate a new basenode and insert
           into ring. Else start using the basenode pointed to by .next
           (ie: recycle the existing storage) */

        if (basenode == NULL || (basenode->next == basenode->ancnode))
        {
            /* Out of available buffers */

            newnode = malloc(LTJSON_NODEALLOCSIZE * sizeof(ltjson_node_t));
            if (newnode == NULL)
            {
                /* Tear everything down and return NULL */
                destroy_tree(jsoninfo);
                return NULL;
            }

            newnode->ntype     = LTJSON_BASE;
            newnode->name      = 0;
            newnode->nameoff   = 0;
            newnode->val.nused = 1;

            if (basenode)
            {
                /* Insert into ring */
                newnode->next    = basenode->ancnode;
                newnode->ancnode = basenode->ancnode;
                basenode->next   = newnode;
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
            newnode = basenode->next;
            assert(newnode->val.nused == 1);
        }

        basenode = newnode;
        jsoninfo->cbasenode = basenode;
        newnode++;
    }

    basenode->val.nused++;

    newnode->name    = 0;
    newnode->nameoff = 0;
    newnode->ntype   = LTJSON_EMPTY;
    newnode->next    = NULL;
    newnode->ancnode = NULL;

    return newnode;
}




/*
 *  begin_tree(jsoninfo, firstch, curnodep) - Begin json tree with the root
 *
 *  Begin with the root of the json tree which will be an object or an array
 *  (firstch indicates which) with an initial containing blank node. The
 *  root node is included as part of the jsoninfo structure itself.
 *  On success, curnodep is updated to point to that first blank node.
 *
 *  Returns: 0 on success or
 *           -ENOMEM (destroys tree); -EILSEQ (sets lasterr)
 */

static int begin_tree(ltjson_info_t *jsoninfo,
                      int firstch, ltjson_node_t **curnodep)
{
    ltjson_node_t *newnode;

    /* Make up a root node which is either an object or an array
       (going with RFC4627 on this one). Both have no name.
       root is part of jsoninfo. root has no ancestor. */

    if (firstch == '{')
        jsoninfo->root->ntype = LTJSON_OBJECT;
    else if (firstch == '[')
        jsoninfo->root->ntype = LTJSON_ARRAY;
    else
    {
        jsoninfo->lasterr = "RFC4627 JSON starts with object or array";
        return -EILSEQ;
    }

    /* All encounters with "{[," autocreate a blank node.
       An array parent will cause that node to automatically
       have an blank name assigned (not the same thing as no name) */

    if ((newnode = get_new_node(jsoninfo)) == NULL)
        return -ENOMEM;

    jsoninfo->root->val.subnode = newnode;
    newnode->ancnode = jsoninfo->root;

    if (newnode->ancnode->ntype == LTJSON_ARRAY)
        newnode->nameoff = LTJSON_BLANKNAMEOFF;

    *curnodep = newnode;            /* curnode -> blank node */
    return 0;
}




/*
 *  traverse_tree_nodes(node) - Helper to traverse the JSON tree
 *
 *  Taking a point in the tree given by node, find the next valid
 *  node down/next/up in the tree. Repeated calling will traverse
 *  the JSON tree like a directory tree. This is a helper routine
 *  - there is no error checking other than assert.
 *
 *  Returns the next node or NULL if there are no more nodes.
 */

static ltjson_node_t *traverse_tree_nodes(ltjson_node_t *node)
{
    assert(node);

    if (node->ntype == LTJSON_ARRAY || node->ntype == LTJSON_OBJECT)
        return node->val.subnode;

    if (node->next)
        return node->next;

    /* Head back up the tree */

    while ((node = node->ancnode) != NULL)
    {
        if (node->next)
            return node->next;
    }

    return NULL;
}




/*
 *  finalise_tree(jsoninfo) - Close the tree by fixing string offsets
 */

static void finalise_tree(ltjson_info_t *jsoninfo)
{
    ltjson_node_t *curnode;
    int vlen;

    assert(jsoninfo);

    curnode = jsoninfo->root;

    while (curnode)
    {
        if (curnode->nameoff)
            curnode->name = jsoninfo->sstore + curnode->nameoff;

        if (curnode->ntype == LTJSON_STRING)
        {
            vlen = curnode->val.vlen;
            curnode->val.vstr = jsoninfo->sstore + vlen;
        }

        curnode = traverse_tree_nodes(curnode);
    }
}




/*
 *  store_strnum(jsoninfo, textp) - Store text representations
 *
 *  Store a string, number or logic type representation in .sstore.
 *  To distinguish between the types in a continuation (where a tree is
 *  partially updated on each call), the first character is used as a type:
 *  string: ", logic: ! and number: none. textp is moved appropriately.
 *
 *  Whenever this routine is called, it may move the .sstore pointer.
 *
 *  Returns: the offset to the string in the string store on success or
 *           -ENOMEM (destroys tree); -EAGAIN on a continuation.
 */

static int store_strnum(ltjson_info_t *jsoninfo, unsigned char **textp)
{
    unsigned char *s;           /* source */
    unsigned char *d;           /* dest   */
    int dstart;                 /* dest start offset   */

    unsigned char prev = 0;
    int isnum = 0, islogic = 0;

    assert(textp && *textp && **textp);

    s = *textp;

    dstart = jsoninfo->sused;
    d = jsoninfo->sstore + jsoninfo->sused;     /* Might not be valid! */

    if (jsoninfo->incomplete)
    {
        /* Half finished string with the start point stored in incomplete.
           Guaranteed to have at least one character in it and to be null
           terminated. d set above will then be one past the \0 */

        dstart = jsoninfo->incomplete;
        jsoninfo->incomplete = 0;

        d--;                        /* point at terminator */
        jsoninfo->sused--;          /* which we discard */
        prev = *(d - 1);

        if (jsoninfo->sstore[dstart] == '!')
            islogic = 1;
        else if (jsoninfo->sstore[dstart] != '"')
            isnum = 1;
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

        if (jsoninfo->sused >= jsoninfo->salloc - 1)
        {
            /* Out of storage space - realloc */

            int newsize;
            unsigned char *newstore;

            newsize  = jsoninfo->salloc * 2;
            newstore = realloc(jsoninfo->sstore, newsize);

            if (newstore == NULL)
            {
                destroy_tree(jsoninfo);
                return -ENOMEM;
            }

            jsoninfo->sstore = newstore;
            jsoninfo->salloc = newsize;

            d = jsoninfo->sstore + jsoninfo->sused;    /* d may have moved */
        }

        /* Good for two chars! */

        if (islogic)
        {
            if (prev == '!')
            {
                /* Have to store the ! to mark a logic representation but
                   wasn't sure space available until this point. Store and
                   loop around without incrementing the s pointer */

                prev = 0;
                *d++ = '!';
                jsoninfo->sused++;
                continue;
            }

            if (!isalpha(*s))
            {
                *d = '\0';
                jsoninfo->sused++;
                *textp = s;
                return dstart;
            }
        }

        else if (!isnum)    /* string */
        {
            if (*s == '"' && prev != '\\')
            {
                *d = '\0';
                jsoninfo->sused++;

                *textp = ++s;
                return dstart;
            }
        }

        else    /* number */
        {
            /* Allow + - . 0-9 e E and let strtod figure out validity */

            if (!isdigit(*s) && *s != '-' && *s != '+' &&
                                *s != 'e' && *s != 'E' && *s != '.')
            {
                *d = '\0';
                jsoninfo->sused++;

                *textp = s;
                return dstart;
            }
        }

        prev = *s;
        jsoninfo->sused++;
        *d++ = *s++;
    }

    /* Run out of string */

    *d = '\0';
    jsoninfo->sused++;
    jsoninfo->incomplete = dstart;  /* Flag continuation */

    *textp = s;
    return -EAGAIN;
}




/*
 *  convert_to_number(numstr, node) - Convert string to number and store
 *
 *  Convert the string numstr to an integer or float and store the result
 *  in the appropriate section in node.
 *
 *  Returns 0 on success or -EINVAL on conversion error
 */

static int convert_to_number(unsigned char *numstr, ltjson_node_t *node)
{
    char *endptr;
    double dval;
    long lval;

    assert(isdigit(*numstr) || *numstr == '-');

    /* Check for leading zeros which aren't strictly valid */

    if (numstr[0] == '-' && numstr[1] == '0' && numstr[2] != '.')
        return -EINVAL;
    if (numstr[0] == '0' && numstr[1] != '.' && numstr[1] != '\0')
        return -EINVAL;

    if (strpbrk((char *)numstr, "eE."))
    {
        errno = 0;
        dval = strtod((char *)numstr, &endptr);
        if (errno || *endptr)
            return -EINVAL;
        node->ntype = LTJSON_DOUBLE;
        node->val.d = dval;
    }
    else
    {
        errno = 0;
        lval = strtol((char *)numstr, &endptr, 10);
        if (errno || *endptr)
            return -EINVAL;
        node->ntype = LTJSON_INTEGER;
        node->val.l = lval;
    }

    return 0;
}




/*
 *  convert_to_logic(logstr, node) - Convert string to logic and store
 *
 *  Convert the string logstr to a logic type and store the result
 *  in the appropriate section in node.
 *  The string must have a leading '!' (placed by store_strnum).
 *
 *  Returns 0 on success or -EINVAL on conversion error
 */

static int convert_to_logic(unsigned char *logstr, ltjson_node_t *node)
{
    assert(*logstr == '!');
    logstr++;

    if (strcasecmp((char *)logstr, "null") == 0)
    {
        node->ntype = LTJSON_NULL;
    }
    else if (strcasecmp((char *)logstr, "true") == 0)
    {
        node->ntype = LTJSON_BOOL;
        node->val.l = 1;
    }
    else if (strcasecmp((char *)logstr, "false") == 0)
    {
        node->ntype = LTJSON_BOOL;
        node->val.l = 0;
    }
    else
    {
        return -EINVAL;
    }

    return 0;
}




/*
 *  process_json_alnum(jsoninfo, textp, node)
 *
 *  Process a string ("string") a number (-0.12e2) or a logical (true)
 *  entity pointed to by textp into node. This moves *textp and may move
 *  the string store pointer (jsoninfo->sstore).
 *  Continuations (partial text updated) are allowed.
 *
 *  Returns: 0 on complete node fill
 *           1 if just the node name is filled (and ": value" is needed)
 *           -ENOMEM (destroys tree); -EILSEQ (sets lasterr);
 *                                    -EAGAIN on a continuation.
 */

static int process_json_alnum(ltjson_info_t *jsoninfo, unsigned char **textp,
                              ltjson_node_t *node)
{
    int firstch, soff;

    firstch = **textp;
    assert(firstch);

    if (jsoninfo->incomplete)
        firstch = *(jsoninfo->sstore + jsoninfo->incomplete);


    if (firstch == '"')                             /* String */
    {
        int ntrimmed;

        if (node->ntype != LTJSON_EMPTY)
        {
            jsoninfo->lasterr = "Unexpected string (missing comma?)";
            return -EILSEQ;
        }

        soff = store_strnum(jsoninfo, textp);
        if (soff < 0)
            return soff;

        ntrimmed = unescape_string(jsoninfo->sstore + soff);
        if (ntrimmed < 0)
        {
            jsoninfo->lasterr = "Cannot decode escapes in string";
            return -EILSEQ;
        }

        /* Trim down the sstore by the number trimmed by unescape_string: */

        jsoninfo->sused -= ntrimmed;

        if (node->nameoff)
        {
            node->ntype = LTJSON_STRING;
            node->val.vlen = soff;
            return 0;
        }

        node->nameoff = soff;
        return 1;
    }

    else if (firstch == '-' || isdigit(firstch))    /* Number */
    {
        if (node->ntype != LTJSON_EMPTY || !node->nameoff)
        {
            jsoninfo->lasterr = "Unexpected number (missing name or comma)";
            return -EILSEQ;
        }

        soff = store_strnum(jsoninfo, textp);
        if (soff < 0)
            return soff;

        if (convert_to_number(jsoninfo->sstore + soff, node) != 0)
        {
            jsoninfo->lasterr = "Cannot convert number representation";
            return -EILSEQ;
        }

        jsoninfo->sused = soff;     /* Discard string representation */
        return 0;
    }

    else if (firstch == '!' || isalpha(firstch))    /* Logic */
    {
        if (node->ntype != LTJSON_EMPTY || !node->nameoff)
        {
            jsoninfo->lasterr = "Unexpected non-string text";
            return -EILSEQ;
        }

        soff = store_strnum(jsoninfo, textp);
        if (soff < 0)
            return soff;

        if (convert_to_logic(jsoninfo->sstore + soff, node) != 0)
        {
            jsoninfo->lasterr = "Cannot convert logic representation";
            return -EILSEQ;
        }

        jsoninfo->sused = soff;     /* Discard string representation */
        return 0;
    }

    jsoninfo->lasterr = "Internal parsing error (report bug)";
    return -EILSEQ;
}




/*
 *  Inline include of remaining parsing and utility code
 */

#include "ltparse.c"
#include "ltutils.c"
#include "ltpath.c"


/* vi:set expandtab ts=4 sw=4: */
