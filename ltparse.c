/*
 *  ltparse.c (as include): Parsing and free functions
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


/*
 *  is_valid_tree(node)
 *
 *  Minimal check on the node that forms the start of a ltjson_info_t
 *  structure. This is a root node and will have the properties of:
 *  no name/nameoff, no ancnode, ntype of empty/array/object
 *
 *  Returns 1 if valid, 0 if not
 */

static int is_valid_tree(ltjson_node_t *node)
{
    if (!node)
        return 0;

    if (node->name || node->ancnode)
        return 0;

    switch (node->ntype)
    {
        case LTJSON_NTYPE_EMPTY:
        case LTJSON_NTYPE_ARRAY:
        case LTJSON_NTYPE_OBJECT:
            return 1;
        default:
            return 0;
    }
}




/*
 *  is_closed_tree(node)
 *
 *  Calls is_valid_tree above and adds checks to see if the tree
 *  is closed and not in any error state.
 *
 *  Returns 1 if closed, 0 if not
 */

static int is_closed_tree(ltjson_node_t *node)
{
    ltjson_info_t *jsoninfo;

    if (!is_valid_tree(node))
        return 0;

    jsoninfo = (ltjson_info_t *)node;

    if (jsoninfo->lasterr || jsoninfo->open)
        return 0;

    if (jsoninfo->root->ntype == LTJSON_NTYPE_EMPTY)
        return 0;

    return 1;
}




/**
 *  ltjson_parse(treeptr, text, usehash) - Parse text into JSON tree
 *      @treeptr:   Pointer to json tree root
 *      @text:      UTF-8 text
 *      @usehash:   Use a hash to find duplicate node names
 *
 *  Scan the JSON provided @text and parse it into the tree which is
 *  pointed to by treeptr. If @*treeptr is NULL then a new tree is created.
 *  If not NULL the memory storage is reused if the tree is closed or
 *  text continues to be added if the tree is open.
 *
 *  If @text is NULL, the tree is forced closed and into an error state.
 *
 *  If @usehash is true, a new or recycled tree will obtain a name
 *  lookup hash table. This is really only useful for large trees with
 *  many duplicate names. If false, a recycled tree will lose the hash.
 *
 *  Returns:  1 on success and the tree is parsed and closed
 *            0 on error or tree is incomplete and errno is set to:
 *              EAGAIN if tree incomplete and more text needed
 *              EINVAL if invalid argument
 *              EILSEQ if invalid JSON sequence (reason available)
 *              ENOMEM if out of memory
 *
 *  On ENOMEM, all storage will be freed and *treeptr is set to NULL
 */

int ltjson_parse(ltjson_node_t **treeptr, const char *text, int usehash)
{
    ltjson_info_t *jsoninfo = 0;
    ltjson_node_t *newnode, *curnode;

    if (!treeptr)
    {
        errno = EINVAL;
        return 0;
    }

    if (*treeptr)
    {
        if (!is_valid_tree(*treeptr))
        {
            errno = EINVAL;
            return 0;
        }

        jsoninfo = (ltjson_info_t *)(*treeptr);
    }

    if (!text)
    {
        /* Request to close a tree. The next call will recycle
           the tree (if not NULL). Put tree into error state. */

        if (jsoninfo)
        {
            jsoninfo->open = 0;
            jsoninfo->lasterr = ERR_SEQ_TREEDUMP;
        }

        return 1;   /* Success! */
    }


    if (jsoninfo && jsoninfo->open)
    {
        /* Existing open tree */

        curnode = jsoninfo->open;
        jsoninfo->open = 0;

        if (jsoninfo->incomplete)
        {
            /* Open tree with partial string stored in workstr
               Finish it and get the name:value state right */

            if (!process_json_alnum(jsoninfo, &text, curnode))
            {
                if (errno == ENOMEM)
                {
                    destroy_tree(jsoninfo);
                    *treeptr = NULL;
                    errno = ENOMEM;
                }
                else if (errno == EAGAIN)
                {
                    jsoninfo->open = curnode;
                }
                return 0;
            }
        }
    }
    else
    {
        /* Create (or recycle) tree and get a new (or the same) jsoninfo.
           Check for blank text as it's a nuisance otherwise but it is
           reasonable to allow leading spaces before the tree starts */

        text = skip_space(text);
        if (!*text)
        {
            errno = EAGAIN;
            return 0;
        }

        if ((jsoninfo = create_tree(jsoninfo)) == NULL)
        {
            *treeptr = NULL;
            return 0;
        }

        *treeptr = (ltjson_node_t *)jsoninfo;


        /* Add or remove the hash support from jsoninfo */

        if (usehash && !jsoninfo->nhtab)
        {
            if (!nhash_new(jsoninfo))
            {
                destroy_tree(jsoninfo);
                *treeptr = NULL;
                errno = ENOMEM;
                return 0;
            }
        }
        else if (!usehash && jsoninfo->nhtab)
        {
            nhash_free(jsoninfo);
        }

        if ((curnode = begin_tree(jsoninfo, *text)) == NULL)
            return 0;

        text++;
    }


    text = skip_space(text);

    while(*text)
    {
        if (  (curnode->nflags == JSONNODE_NFLAGS_OPENOA) &&
              (*text != '}' && *text != ']')  )
        {
            /* Current node is an open object or array and
               we're not closing it to leave it empty.
               So we need a subnode to put stuff into. */

            assert(curnode->val.subnode == NULL);

            if ((newnode = get_new_node(jsoninfo)) == NULL)
            {
                destroy_tree(jsoninfo);
                *treeptr = NULL;
                errno = ENOMEM;
                return 0;
            }

            curnode->val.subnode = newnode;
            newnode->ancnode = curnode;
            curnode = newnode;
        }


        if (curnode->nflags == JSONNODE_NFLAGS_COLON)
        {
            /* The current node expects the next character to be a colon */

            if (*text != ':')
            {
                jsoninfo->lasterr = ERR_SEQ_NOCOLON;
                errno = EILSEQ;
                return 0;
            }

            curnode->nflags = 0;
            text++;
        }


        else if (*text == ':')
        {
            /* Unexpected colon separator */

            jsoninfo->lasterr = ERR_SEQ_UNEXPCOLON;
            errno = EILSEQ;
            return 0;
        }


        else if (*text == ',')
        {
            /* Separator between values */

            if (curnode->ntype == LTJSON_NTYPE_EMPTY)
            {
                jsoninfo->lasterr = ERR_SEQ_LEADCOMMA;
                errno = EILSEQ;
                return 0;
            }

            /* create a new empty node */

            if ((newnode = get_new_node(jsoninfo)) == NULL)
            {
                destroy_tree(jsoninfo);
                *treeptr = NULL;
                errno = ENOMEM;
                return 0;
            }

            curnode->next = newnode;
            newnode->ancnode = curnode->ancnode;

            curnode = newnode;
            text++;
        }


        else if (*text == '{' || *text == '[')
        {
            /* New object or array, flagged as open */

            if (curnode->ntype != LTJSON_NTYPE_EMPTY)
            {
                jsoninfo->lasterr = ERR_SEQ_UNEXPOA;
                errno = EILSEQ;
                return 0;
            }

            if (!curnode->name &&
                 curnode->ancnode->ntype == LTJSON_NTYPE_OBJECT)
            {
                jsoninfo->lasterr = ERR_SEQ_OBJNONAME;
                errno = EILSEQ;
                return 0;
            }

            if (*text == '{')
                curnode->ntype = LTJSON_NTYPE_OBJECT;
            else
                curnode->ntype = LTJSON_NTYPE_ARRAY;

            curnode->nflags = JSONNODE_NFLAGS_OPENOA;
            curnode->val.subnode = NULL;

            text++;
        }


        else if (*text == '}' || *text == ']')
        {
            /* Close object or array */

            if (curnode->ntype == LTJSON_NTYPE_EMPTY)
            {
                jsoninfo->lasterr = ERR_SEQ_BADCLOSURE;
                errno = EILSEQ;
                return 0;
            }

            if (curnode->nflags != JSONNODE_NFLAGS_OPENOA)
            {
                /* Not at the node that is a { or [ */
                curnode = curnode->ancnode;
            }

            if (*text == '}')
            {
                if (curnode->ntype != LTJSON_NTYPE_OBJECT)
                {
                    jsoninfo->lasterr = ERR_SEQ_MMCLOSEOBJ;
                    errno = EILSEQ;
                    return 0;
                }
            }
            else
            {
                if (curnode->ntype != LTJSON_NTYPE_ARRAY)
                {
                    jsoninfo->lasterr = ERR_SEQ_MMCLOSEARR;
                    errno = EILSEQ;
                    return 0;
                }
            }

            curnode->nflags = 0;    /* Mark object/array as closed */

            if (curnode->ancnode == NULL)       /* At top, tree closed */
                return 1;

            text++;
        }


        else if (*text == '"' || *text == '-' || c_isalnum(*text))
        {
            if (!process_json_alnum(jsoninfo, &text, curnode))
            {
                if (errno == ENOMEM)
                {
                    destroy_tree(jsoninfo);
                    *treeptr = NULL;
                    errno = ENOMEM;
                }
                else if (errno == EAGAIN)
                {
                    jsoninfo->open = curnode;
                }
                return 0;
            }
        }


        else
        {
            jsoninfo->lasterr = ERR_SEQ_BADTEXT;
            errno = EILSEQ;
            return 0;
        }

        text = skip_space(text);

    }   /* while(*text) */


    /* Ran out of text without closing the JSON tree.
       Ask for a continuance of the tree storage: */

    jsoninfo->open = curnode;
    errno = EAGAIN;
    return 0;
}




/**
 *  ltjson_free(treeptr) - Free up all memory associate with tree
 *      @treeptr:   Pointer to valid tree
 *
 *  Returns:    1 on success, writing NULL to *treeptr
 *              0 and errno set to EINVAL if tree is not valid
 */

int ltjson_free(ltjson_node_t **treeptr)
{
    ltjson_info_t *jsoninfo;

    if (!treeptr || !*treeptr || !is_valid_tree(*treeptr))
    {
        errno = EINVAL;
        return 0;
    }

    jsoninfo = (ltjson_info_t *)(*treeptr);
    destroy_tree(jsoninfo);

    *treeptr = NULL;
    return 1;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
