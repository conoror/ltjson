/*
 *  ltpath.c (as include): JSON Path functions
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


typedef struct {
    const char *name;   /* Pointer to name (not null terminated) */
    int namelen;        /* Name length (can be zero) */
    int hasindex;       /* If this section has an index */
    int aindex;         /* The index (1 based. 0 is "*") */
} ltjson_rpath_t;


/*
 *  path_tokenise(path, rstore, rsize) - break path into sections
 *
 *  Break up referencing path which in this format:
 *          /phoneNumbers/type
 *          /phoneNumbers[1]/type
 *          /[3]/store/book
 *  into parts and store into rstore to a max of rsize. Every item
 *  has a valid name (although namelen can be zero) - the last item
 *  has name = 0 to terminate the list.
 *
 *  Returns the number of sections stored or 0 if path is just "/"
 *     or -EINVAL on bad params, -ERANGE if path too long,
 *        -EILSEQ if path isn't formatted correctly
 */

static int path_tokenise(const char *path, ltjson_rpath_t *rstore, int rsize)
{
    const char *curp;
    int rnum;

    if (!path || !rstore || rsize <= 0)
        return -EINVAL;

    if (*path != '/')
        return -EILSEQ;

    curp = path + 1;

    for (rnum = 0; rnum < rsize; rnum++)
    {
        if (!*curp)
        {
            rstore[rnum].name = 0;
            return rnum;
        }

        if (*curp == '/')
            return -EILSEQ;

        rstore[rnum].name = curp;
        rstore[rnum].namelen = 0;
        rstore[rnum].hasindex = 0;
        rstore[rnum].aindex = 0;

        while (*curp != '\0' && *curp != '[' && *curp != '/')
        {
            curp++;
            rstore[rnum].namelen++;
        }

        if (*curp == '\0')
            continue;

        if (*curp == '/')
        {
            curp++;
            continue;
        }

        /* index */

        rstore[rnum].hasindex = 1;
        curp++;

        if (*curp == ']')
        {
            curp++;
        }
        else if (curp[0] == '*' && curp[1] == ']')
        {
            curp += 2;
        }
        else
        {
            long l;
            char *endptr;

            errno = 0;
            l = strtol(curp, &endptr, 10);
            if (errno || *endptr != ']' || l < 0)
                return -EILSEQ;

            rstore[rnum].aindex = (int)l;
            curp = endptr + 1;
        }

        if (*curp)
        {
            if (*curp != '/')
                return -EILSEQ;
            curp++;
        }

    }   /* for (rnum...) */

    return -ERANGE;
}




/*
 *  path_getobject(atnode, refpath, nodestorep, storeavail) - get object
 *
 *  Search the list of subnodes whose ancestor is atnode for the item
 *  referred to in (the null terminated list) refpath and, if space
 *  available, store the matches into the array of nodes pointed to by
 *  nodestorep. Recursively called.
 *  The nodestore is an array of pointers to nodes. This function updates
 *  one original instance of nodestore in the top caller (ltjson_pathrefer),
 *  thus nodestorep is a ltjson_node_t ***. storeavail is the same deal.
 *
 *  Returns the number of matches found (whether stored or not)
 */

int path_getobject(ltjson_node_t *atnode, ltjson_rpath_t *refpath,
                   ltjson_node_t ***nodestorep, int *storeavail)
{
    int thisindex, nfound;

    /* We enter on a match - thus if we're out of refpaths to check
       then we're done. */

    if (refpath->name == NULL)
    {
        if (*storeavail)
        {
            *(*nodestorep) = atnode;
            (*nodestorep)++;
            (*storeavail)--;
        }
        return 1;
    }

    /* The current node (atnode) is the last match. To find the
       next match, atnode must be an object or an array. Any
       matches will be entries under atnode */

    switch (atnode->ntype)
    {
        case LTJSON_OBJECT:
            if (!refpath->namelen)
                return 0;
        break;

        case LTJSON_ARRAY:
            if (refpath->namelen)
                return 0;
        break;

        default:
            return 0;
    }

    if (atnode->ntype == LTJSON_OBJECT)
    {
        atnode = atnode->val.subnode;

        if (atnode->ntype == LTJSON_EMPTY)
            return 0;

        do {
            if (strlen((char *)atnode->name) != refpath->namelen)
                continue;

            if (strncmp((char *)atnode->name, refpath->name,
                        refpath->namelen) == 0)
            {
                /* found */
                break;
            }

        } while ((atnode = atnode->next) != NULL);

        if (atnode == NULL)
            return 0;

        /* Match! - there will only ever be one match in an object */

        if (atnode->ntype != LTJSON_ARRAY)
        {
            /* item is not an array. Cannot have indexes: */

            if (refpath->hasindex)
                return 0;

            /* object or other item: recurse to next match and finish: */

            return path_getobject(atnode, refpath + 1,
                                  nodestorep, storeavail);
        }

        /* Item is an array. If index is not specified, treat this the same
           as specifing all indexes, except when the item is the last in
           the search spec (we want the item, not its subitems: */

        if (!refpath->hasindex)
        {
            if (refpath[1].name == NULL)
            {
                return path_getobject(atnode, refpath + 1,
                                      nodestorep, storeavail);
            }

            refpath->hasindex = 1;
            refpath->aindex = 0;
        }
    }

    assert(atnode->ntype == LTJSON_ARRAY);

    /* Array searching. Can drop through from the object search... */

    atnode = atnode->val.subnode;
    if (atnode->ntype == LTJSON_EMPTY)
        return 0;

    thisindex = 0;
    nfound = 0;

    do {
        thisindex++;

        if (refpath->aindex && refpath->aindex != thisindex)
            continue;

        /* Array match (or all of them) */

        nfound += path_getobject(atnode, refpath + 1, nodestorep, storeavail);

    } while ((atnode = atnode->next) != NULL);

    return nfound;
}




/**
 *  ltjson_pathrefer(tree, path, nodeptr, nnodes) - Search for nodes
 *      @tree:      Valid closed and finalised (no error state) tree
 *      @path:      Reference path expression
 *      @nodeptr:   Pointer to nodestore for the answer
 *      @nnodes:    Number of available nodes in @nodeptr
 *
 *  Search the tree for the items specified in path and store any matches
 *  found into the @nodeptr node array up to a max of @nnodes.
 *
 *  Returns:    Number of matches found (not stored) on success or
 *              -EINVAL if tree is not valid, closed or finalised
 *              -EILSEQ if path expression is not understood
 *              -ERANGE if path is too long
 */

int ltjson_pathrefer(ltjson_node_t *tree, const char *path,
                     ltjson_node_t **nodeptr, int nnodes)
{
    ltjson_rpath_t refpaths[8];
    int ret;

    if (!tree || !path || !nodeptr || nnodes <= 0)
        return -EINVAL;

    if (!is_closed_tree(tree))
        return -EINVAL;

    ret = path_tokenise(path, refpaths, sizeof(refpaths)/sizeof(refpaths[0]));
    if (ret < 0)
        return ret;

    if (ret == 0)
    {
        nodeptr[0] = tree;
        return 1;
    }

    return path_getobject(tree, refpaths, &nodeptr, &nnodes);
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
