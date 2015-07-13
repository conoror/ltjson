/*
 *  ltparse.c (as include): Parsing and free functions
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

    if (node->name || node->nameoff || node->ancnode)
        return 0;

    switch (node->ntype)
    {
        case LTJSON_EMPTY:
        case LTJSON_ARRAY:
        case LTJSON_OBJECT:
            return 1;
        default:
            return 0;
    }
}




/**
 *  ltjson_parse(treeptr, text) - Parse text into JSON tree
 *      @treeptr:   Pointer to json tree root
 *      @text:      UTF-8 unsigned char text
 *
 *  Scan the JSON provided @text and parse it into the tree which is
 *  pointed to by treeptr. If @*treeptr is NULL then a new tree is created.
 *  If not NULL the memory storage is reused if the tree is closed or
 *  text continues to be added if the tree is open.
 *  If @text is NULL, the tree is forced closed and into an error state.
 *
 *  Returns:    0 on success and the tree is parsed and closed
 *              -EAGAIN if tree incomplete and more text needed
 *              -EINVAL if invalid argument
 *              -EILSEQ if invalid JSON sequence (reason available)
 *              -ENOMEM if out of memory (all storage is then destroyed
 *                                        and *treeptr will be set NULL)
 *              +ve number indicating the number of leftover bytes
 */

int ltjson_parse(ltjson_node_t **treeptr, unsigned char *text)
{
    ltjson_info_t *jsoninfo = 0;
    ltjson_node_t *newnode, *curnode;
    int ret, expect_a_colon = 0;


    if (!treeptr)
        return -EINVAL;

    if (*treeptr)
    {
        if (!is_valid_tree(*treeptr))
            return -EINVAL;

        jsoninfo = (ltjson_info_t *)(*treeptr);
    }

    if (!text)
    {
        /* Request to close a tree. The next call will recycle
           the tree (if not NULL). Put tree into error state. */

        if (jsoninfo)
        {
            jsoninfo->open = 0;
            jsoninfo->lasterr = "Tree discontinued";
        }

        return 0;
    }

    if (jsoninfo && jsoninfo->open)
    {
        /* Existing open tree */

        curnode = jsoninfo->open;
        jsoninfo->open = 0;

        if (jsoninfo->incomplete)
        {
            /* Open tree with partial string stored.
               Finish it and get the name:value state right */

            ret = process_json_alnum(jsoninfo, &text, curnode);
            if (ret < 0)
            {
                if (ret == -ENOMEM)
                    *treeptr = NULL;
                else if (ret == -EAGAIN)
                    jsoninfo->open = curnode;
                return ret;
            }

            if (ret == 1)
                expect_a_colon = 1;
        }
    }
    else
    {
        /* Create (or recycle) tree and get a new (or the same) jsoninfo.
           Check for blank text as it's a nuisance otherwise but it is
           reasonable to allow leading spaces before the tree starts */

        text = strip_space(text);
        if (!*text)
            return -EAGAIN;

        jsoninfo = create_tree(jsoninfo);
        if (jsoninfo == NULL)
        {
            *treeptr = NULL;
            return -ENOMEM;
        }

        *treeptr = (ltjson_node_t *)jsoninfo;

        ret = begin_tree(jsoninfo, *text, &curnode);
        if (ret < 0)
        {
            if (ret == -ENOMEM)
                *treeptr = NULL;
            return ret;
        }
        text++;
    }


    text = strip_space(text);

    while(*text)
    {
        if (expect_a_colon && *text != ':')
        {
            jsoninfo->lasterr = "Expected a name-value separator (:)";
            return -EILSEQ;
        }

        if (*text == '{' || *text == '[')
        {
            /* New object or array */

            if (curnode->ntype != LTJSON_EMPTY || !curnode->nameoff)
            {
                jsoninfo->lasterr = "Unexpected object or array "
                                    "(missing name or comma)";
                return -EILSEQ;
            }

            if (*text == '{')
                curnode->ntype = LTJSON_OBJECT;
            else
                curnode->ntype = LTJSON_ARRAY;

            /* Create the first subnode (blank) with blank name iff array */

            if ((newnode = get_new_node(jsoninfo)) == NULL)
            {
                *treeptr = NULL;
                return -ENOMEM;
            }

            curnode->val.subnode = newnode;
            newnode->ancnode = curnode;

            if (newnode->ancnode->ntype == LTJSON_ARRAY)
                newnode->nameoff = LTJSON_BLANKNAMEOFF;

            curnode = newnode;
            text++;
        }

        else if (*text == '}' || *text == ']')
        {
            /* Close object or array */

            if (*text == '}')
            {
                if (curnode->ancnode->ntype != LTJSON_OBJECT)
                {
                    jsoninfo->lasterr = "Mismatched object closure";
                    return -EILSEQ;
                }

                if (curnode->nameoff && curnode->ntype == LTJSON_EMPTY)
                {
                    jsoninfo->lasterr = "Name with no value at object closure";
                    return -EILSEQ;
                }
            }
            else
            {
                if (curnode->ancnode->ntype != LTJSON_ARRAY)
                {
                    jsoninfo->lasterr = "Mismatched array closure";
                    return -EILSEQ;
                }
            }

            curnode = curnode->ancnode;
            text++;

            if (curnode->ancnode == NULL)
            {
                /* We're at the top. Tree is closed. */

                finalise_tree(jsoninfo);

                text = strip_space(text);
                ret = 0;
                while (*text++)
                    ret++;
                return ret;
            }
        }

        else if (*text == '"' || *text == '-' || isalnum(*text))
        {
            ret = process_json_alnum(jsoninfo, &text, curnode);
            if (ret < 0)
            {
                if (ret == -ENOMEM)
                    *treeptr = NULL;
                else if (ret == -EAGAIN)
                    jsoninfo->open = curnode;
                return ret;
            }

            if (ret == 1)
                expect_a_colon = 1;
        }

        else if (*text == ':')
        {
            /* Separator between name and value */

            if (!expect_a_colon)
            {
                jsoninfo->lasterr = "Unexpected name-value separator (:)";
                return -EILSEQ;
            }
            expect_a_colon = 0;
            text++;
        }

        else if (*text == ',')
        {
            /* Separator between values */

            if (curnode->ntype == LTJSON_EMPTY)
            {
                jsoninfo->lasterr = "Comma after empty value";
                return -EILSEQ;
            }

            /* autocreate new empty node */

            if ((newnode = get_new_node(jsoninfo)) == NULL)
            {
                *treeptr = NULL;
                return -ENOMEM;
            }

            curnode->next = newnode;
            newnode->ancnode = curnode->ancnode;

            if (newnode->ancnode->ntype == LTJSON_ARRAY)
                newnode->nameoff = LTJSON_BLANKNAMEOFF;

            curnode = newnode;
            text++;
        }

        else
        {
            jsoninfo->lasterr = "Random unquoted text in content";
            return -EILSEQ;
        }

        text = strip_space(text);

    }   /* while(*text) */


    /* Ran out of text without closing the JSON tree.
       Ask for a continuance of the tree storage: */

    jsoninfo->open = curnode;
    return -EAGAIN;
}




/**
 *  ltjson_free(treeptr) - Free up all memory associate with tree
 *      @treeptr:   Pointer to valid tree
 *
 *  Returns:    0 on success, writing NULL to *treeptr
 *              -EINVAL if tree is not valid
 */

int ltjson_free(ltjson_node_t **treeptr)
{
    ltjson_info_t *jsoninfo;

    if (!treeptr)
        return -EINVAL;

    if (!*treeptr)
        return 0;

    if (!is_valid_tree(*treeptr))
        return -EINVAL;

    jsoninfo = (ltjson_info_t *)(*treeptr);
    destroy_tree(jsoninfo);

    *treeptr = NULL;
    return 0;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
