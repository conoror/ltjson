/*
 *  Light JSON: Yet another JSON implementation in C
 *
 *  The emphasis is on low memory usage and the ability to
 *  free, reuse and/or continue the in memory json tree.
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2016 Conor F. O'Rourke. All rights reserved.
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

#include "ltlocal.h"

#include "lttext.c"     /* code inline include */
#include "lthash.c"


/* This extern can be set by the caller to change the number of
   basenodes allocated every time we run out */

int ltjson_allocsize_nodes;




/*  Memory layout
    -------------

    The storage for the json nodes is allocated in sets (set by
    jsoninfo->nodeasize) with the first node in the set denoted the
    "basenode". This node uses the same structure as a normal node
    so it overlays node usage information into that structure:

        [bn] -> [bn] -> [bn] -> [bn] -+
         ^----------------------------+

    each bn links to the next with .next in a ring buffer format. Each
    links to the head bn with .ancnode. We can always get to the head
    and we always know when we're out of storage (.next is .ancnode).
    If there's only one bn, it will point to itself.

    The nodes can be recycled by simply setting the base node .nused
    entries to 1 and moving the current bn to the head.
    The get_new_node() function will reuse them.

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
 *
 *  If jsoninfo is passed as non-NULL, the tree is simply recycled and
 *  all memories (including sstore and hashes) are set to be reused.
 *
 *  Returns: pointer to jsoninfo on success
 *           NULL if out of memory (errno to ENOMEM)
 */

static ltjson_info_t *create_tree(ltjson_info_t *jsoninfo)
{
    if (jsoninfo == NULL)
    {
        jsoninfo = malloc(sizeof(ltjson_info_t));
        if (!jsoninfo)
        {
            errno = ENOMEM;
            return NULL;
        }

        jsoninfo->sstore    = sstore_new();
        jsoninfo->workstr   = NULL;
        jsoninfo->workalloc = 0;
        jsoninfo->cbasenode = 0;
        jsoninfo->nhtab     = 0;
        jsoninfo->nhstore   = 0;

        /* And set the node allocation size once, right here */

        if (!ltjson_allocsize_nodes)
        {
            jsoninfo->nodeasize = JSONNODE_DEF_ALLOC;
        }
        else
        {
            jsoninfo->nodeasize = ltjson_allocsize_nodes;
            if (jsoninfo->nodeasize < JSONNODE_MIN_ALLOC)
                jsoninfo->nodeasize = JSONNODE_MIN_ALLOC;
        }

        jsoninfo->nodeasize++;      /* for the basenode */
    }


    /* In order for an ltjson_info_t to look like an ltjson_node_t
       the first entry in the former must be the latter. And be
       initialised as per get_new_node(). Then use the root entry
       to point to that node_t (more consistent pointer usage).

       .cbasenode, .sstore, .workstr, .workalloc, .nhtab, .nhstore
       will already be valid or are set in the above code.
       Init everything else:
    */

    jsoninfo->rootnode.name    = NULL;
    jsoninfo->rootnode.ntype   = LTJSON_NTYPE_EMPTY;
    jsoninfo->rootnode.nflags  = 0;
    jsoninfo->rootnode.next    = NULL;
    jsoninfo->rootnode.ancnode = NULL;

    jsoninfo->root = &jsoninfo->rootnode;
    jsoninfo->open = 0;

    jsoninfo->lasterr    = 0;
    jsoninfo->incomplete = 0;


    if (jsoninfo->nhtab)
        nhash_reset(jsoninfo);

    if (jsoninfo->sstore)
        sstore_clear(&jsoninfo->sstore);

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

    nhash_free(jsoninfo);
    sstore_free(&jsoninfo->sstore);
    free(jsoninfo->workstr);

    if (jsoninfo->cbasenode)
    {
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
    }

    /* finally, free the info structure itself */

    free(jsoninfo);
}




/*
 *  get_new_node(jsoninfo) - Get a new node, allocating as required
 *
 *  jsoninfo is a pointer to the json info struct and must be valid.
 *
 *  Returns: a pointer to an initialised empty new node on success
 *           NULL if out of memory (errno to ENOMEM)
 */

static ltjson_node_t *get_new_node(ltjson_info_t *jsoninfo)
{
    ltjson_node_t *newnode, *basenode;

    assert(jsoninfo);

    basenode = jsoninfo->cbasenode;

    if (basenode && basenode->val.nused < jsoninfo->nodeasize)
    {
        newnode = basenode + basenode->val.nused;
    }
    else
    {
        /* current set of nodes is full.
           Check if .next points to the first node (end of the ring).
           If it does, allocate a new basenode and insert into ring.
           Else start using the basenode pointed to by .next
           (ie: recycle the existing storage)
        */

        if (basenode == NULL || (basenode->next == basenode->ancnode))
        {
            /* Out of available buffers */

            newnode = malloc(jsoninfo->nodeasize * sizeof(ltjson_node_t));
            if (newnode == NULL)
            {
                errno = ENOMEM;
                return NULL;
            }

            newnode->name      = NULL;
            newnode->ntype     = LTJSON_NTYPE_BASENODE;
            newnode->nflags    = 0;
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

        jsoninfo->cbasenode = newnode++;
    }

    jsoninfo->cbasenode->val.nused++;

    newnode->name    = NULL;
    newnode->ntype   = LTJSON_NTYPE_EMPTY;
    newnode->nflags  = 0;
    newnode->next    = NULL;
    newnode->ancnode = NULL;

    return newnode;
}




/*
 *  begin_tree(jsoninfo, firstch, curnodep) - Begin json tree with the root
 *
 *  Begin with the root of the json tree. This will be an object or an array
 *  (firstch indicates which) which will be flagged as open.
 *  The root node is included as part of the jsoninfo structure.
 *
 *  Returns: Pointer to the root node on success or
 *           NULL if illegal input sequence (errno to EILSEQ and lasterr)
 */

static ltjson_node_t *begin_tree(ltjson_info_t *jsoninfo, int firstch)
{
    assert(jsoninfo);

    /* Make up a root node which is either an object or an array
       (going with the relevant RFC on this one). Both have no name.
       root is part of jsoninfo. root has no ancestor. */

    if (firstch == '{')
        jsoninfo->root->ntype = LTJSON_NTYPE_OBJECT;
    else if (firstch == '[')
        jsoninfo->root->ntype = LTJSON_NTYPE_ARRAY;
    else
    {
        jsoninfo->lasterr = ERR_SEQ_BEGINTREE;
        errno = EILSEQ;
        return NULL;
    }

    jsoninfo->root->nflags = JSONNODE_NFLAGS_OPENOA;
    jsoninfo->root->val.subnode = NULL;

    return jsoninfo->root;
}




/*
 *  traverse_tree_nodes(node, rnode) - Helper to traverse the JSON tree
 *
 *  Taking a point in the tree given by node, find the next valid node
 *  down/next/up in the tree. The optional rnode restricts the search
 *  to the subtree anchored at rnode. Repeated calling will traverse
 *  the JSON tree like a directory tree.
 *
 *  This is a helper routine - there is no error checking except assert.
 *
 *  Returns the next node or NULL if there are no more nodes.
 */

static ltjson_node_t *traverse_tree_nodes(ltjson_node_t *node,
                                          ltjson_node_t *rnode)
{
    assert(node);

    if (node->ntype == LTJSON_NTYPE_ARRAY ||
        node->ntype == LTJSON_NTYPE_OBJECT)
    {
        if (node->val.subnode)
            return node->val.subnode;

        if (rnode && node == rnode)
            return NULL;
    }

    if (node->next)
        return node->next;

    /* Head back up the tree */

    while ((node = node->ancnode) != NULL)
    {
        if (rnode && node == rnode)
            return NULL;

        if (node->next)
            return node->next;
    }

    return NULL;
}




/*
 *  store_strnum(jsoninfo, textp) - Store text representations
 *
 *  Store a string, number or logic type representation in .workstr.
 *  To distinguish between the types in a continuation (where a tree is
 *  partially updated on each call), the first character is used as a type:
 *  string: ", logic: ! and number: none.
 *
 *  Characters are taken from textp whose pointer is moved along.
 *
 *  Warning: This routine may move the .workstr pointer.
 *
 *  Returns: A pointer to .workstr on success
 *           NULL on error and sets errno to:
 *                  ENOMEM if out of memory
 *                  EAGAIN if out of input
 */

static char *store_strnum(ltjson_info_t *jsoninfo, const char **textp)
{
    const char *s;
    char *d;
    int dlen, prev, isnum, islogic;

    assert(jsoninfo && textp && *textp && **textp);

    s = *textp;                         /* Source */
    d = jsoninfo->workstr;              /* Dest, can be 0 */
    dlen = prev = isnum = islogic = 0;


    if (jsoninfo->incomplete)
    {
        /* Incomplete input. d is guaranteed to have at least
           one character and to be null terminated */

        assert(d && *d);

        jsoninfo->incomplete = 0;

        if (*d == '!')
            islogic = 1;
        else if (*d != '"')
            isnum = 1;

        while (*d)                  /* Point at '\0' */
        {
            dlen++;
            d++;
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
        else if (*s == '-' || c_isdigit(*s))
        {
            isnum = 1;
        }
        else if (c_isalpha(*s))
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

        if (dlen + 2 > jsoninfo->workalloc)
        {
            /* Out of storage space */

            char *newstore;

            if (!jsoninfo->workalloc)
                jsoninfo->workalloc = WORKSTR_INIT_ALLOC;
            else
                jsoninfo->workalloc *= 2;

            newstore = realloc(jsoninfo->workstr, jsoninfo->workalloc);
            if (!newstore)
            {
                errno = ENOMEM;
                return NULL;
            }

            jsoninfo->workstr = newstore;
            d = newstore + dlen;
        }

        /* We're good for two chars! */

        if (islogic)
        {
            if (prev == '!')
            {
                /* Have to store the ! to mark a logic representation but
                   wasn't sure space available until this point. Store and
                   loop around without incrementing the s pointer */

                prev = 0;
                *d++ = '!';
                dlen++;
                continue;
            }

            if (!c_isalpha(*s))
            {
                *d = '\0';
                *textp = s;
                return jsoninfo->workstr;
            }
        }

        else if (!isnum)    /* string */
        {
            if (*s == '"' && prev != '\\')
            {
                *d = '\0';
                *textp = ++s;
                return jsoninfo->workstr;
            }
        }

        else    /* number */
        {
            /* Allow + - . 0-9 e E and let strtod figure out validity */

            if (!c_isdigit(*s) && *s != '-' && *s != '+' &&
                                  *s != 'e' && *s != 'E' && *s != '.')
            {
                *d = '\0';
                *textp = s;
                return jsoninfo->workstr;
            }
        }

        prev = *s;
        *d++ = *s++;
        dlen++;
    }

    /* We've run out of input */

    *d = '\0';
    jsoninfo->incomplete = 1;
    *textp = s;

    errno = EAGAIN;
    return NULL;
}




/*
 *  convert_to_number(numstr, node) - Convert string to number and store
 *
 *  Convert the string numstr to an integer or float and store the result
 *  in the appropriate section in node.
 *
 *  Returns 1 on success or 0 on conversion error
 */

static int convert_to_number(const char *numstr, ltjson_node_t *node)
{
    char *endptr;
    double dval;
    long long llval;

    assert(c_isdigit(*numstr) || *numstr == '-');

    /* Check for leading zeros which aren't strictly valid */

    if (numstr[0] == '-' && numstr[1] == '0' && numstr[2] != '.')
        return 0;
    if (numstr[0] == '0' && numstr[1] != '.' && numstr[1] != '\0')
        return 0;

    if (strpbrk(numstr, "eE."))
    {
        errno = 0;
        dval = strtod(numstr, &endptr);
        if (errno || *endptr)
            return 0;
        node->ntype = LTJSON_NTYPE_FLOAT;
        node->val.d = dval;
    }
    else
    {
        errno = 0;
        llval = strtoll(numstr, &endptr, 10);
        if (errno || *endptr)
            return 0;
        node->ntype = LTJSON_NTYPE_INTEGER;
        node->val.ll = llval;
    }

    return 1;
}




/*
 *  convert_to_logic(logstr, node) - Convert string to logic and store
 *
 *  Convert the string logstr to a logic type and store the result
 *  in the appropriate section in node.
 *  The string must have a leading '!' (placed by store_strnum).
 *
 *  Returns 1 on success or 0 on conversion error
 */

static int convert_to_logic(const char *logstr, ltjson_node_t *node)
{
    assert(*logstr == '!');
    logstr++;

    if (strcasecmp(logstr, "null") == 0)
    {
        node->ntype = LTJSON_NTYPE_NULL;
    }
    else if (strcasecmp(logstr, "true") == 0)
    {
        node->ntype = LTJSON_NTYPE_BOOL;
        node->val.ll = 1;
    }
    else if (strcasecmp(logstr, "false") == 0)
    {
        node->ntype = LTJSON_NTYPE_BOOL;
        node->val.ll = 0;
    }
    else
    {
        return 0;
    }

    return 1;
}




/*
 *  process_json_alnum(jsoninfo, textp, node)
 *
 *  Process a string ("string") a number (-0.12e2) or a logical (true)
 *  entity pointed to by textp into node. This moves *textp and may move
 *  the working store (jsoninfo->workstr).
 *
 *  Continuations (partial text updates) are allowed.
 *
 *  Returns: 1 on complete or partial (value still needed) node fill
 *           0 on error on continuation with errno set to:
 *                  ENOMEM (Out of memory)
 *                  EILSEQ (Bad sequence, sets lasterr)
 *                  EAGAIN (Out of input)
 */

static int process_json_alnum(ltjson_info_t *jsoninfo, const char **textp,
                              ltjson_node_t *node)
{
    int firstch;

    firstch = **textp;
    assert(firstch);

    if (jsoninfo->incomplete)
        firstch = *jsoninfo->workstr;

    if (firstch == '"')                             /* String */
    {
        const char *nvstr;

        if (node->ntype != LTJSON_NTYPE_EMPTY)
        {
            jsoninfo->lasterr = ERR_SEQ_UNEXPSTR;
            errno = EILSEQ;
            return 0;
        }

        if (!store_strnum(jsoninfo, textp))     /* Sets errno */
            return 0;

        if (!unescape_string(jsoninfo->workstr))
        {
            jsoninfo->lasterr = ERR_SEQ_BADESCAPE;
            errno = EILSEQ;
            return 0;
        }


        if (node->name || node->ancnode->ntype == LTJSON_NTYPE_ARRAY)
        {
            /* Name already set (if object member) or no name (if
               array element). Store the string, don't hash it */

            nvstr = sstore_add(&jsoninfo->sstore, jsoninfo->workstr + 1);
            if (!nvstr)
                return 0;

            node->ntype = LTJSON_NTYPE_STRING;
            node->val.s = nvstr;
            return 1;
        }

        /* Set the name part of an object member. Hashing available for
           this. Mark the node as looking for the colon (name : value) */

        nvstr = nhash_insert(jsoninfo, jsoninfo->workstr + 1);
        if (!nvstr)
            return 0;

        node->name = nvstr;
        node->nflags = JSONNODE_NFLAGS_COLON;

        return 1;
    }


    else if (firstch == '-' || c_isdigit(firstch))    /* Number */
    {
        if (node->ntype != LTJSON_NTYPE_EMPTY)
        {
            jsoninfo->lasterr = ERR_SEQ_UNEXPNUM;
            errno = EILSEQ;
            return 0;
        }

        if (!node->name && node->ancnode->ntype == LTJSON_NTYPE_OBJECT)
        {
            jsoninfo->lasterr = ERR_SEQ_OBJNONAME;
            errno = EILSEQ;
            return 0;
        }

        if (!store_strnum(jsoninfo, textp))
            return 0;

        if (!convert_to_number(jsoninfo->workstr, node))
        {
            jsoninfo->lasterr = ERR_SEQ_BADNUMBER;
            errno = EILSEQ;
            return 0;
        }

        return 1;
    }


    else if (firstch == '!' || c_isalpha(firstch))    /* Logic */
    {
        if (node->ntype != LTJSON_NTYPE_EMPTY)
        {
            jsoninfo->lasterr = ERR_SEQ_UNEXPTXT;
            errno = EILSEQ;
            return 0;
        }

        if (!node->name && node->ancnode->ntype == LTJSON_NTYPE_OBJECT)
        {
            jsoninfo->lasterr = ERR_SEQ_OBJNONAME;
            errno = EILSEQ;
            return 0;
        }

        if (!store_strnum(jsoninfo, textp))
            return 0;

        if (!convert_to_logic(jsoninfo->workstr, node))
        {
            jsoninfo->lasterr = ERR_SEQ_BADROBOT;
            errno = EILSEQ;
            return 0;
        }

        return 1;
    }

    jsoninfo->lasterr = ERR_INT_INTERNAL;
    errno = EILSEQ;
    return 0;
}




/*
 *  Inline include of remaining parsing and utility code
 */

#include "ltparse.c"
#include "ltutils.c"
#include "ltpath.c"
#include "ltsort.c"


/* vi:set expandtab ts=4 sw=4: */
