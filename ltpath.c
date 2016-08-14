/*
 *  ltpath.c (as include): JSON Path functions
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


typedef struct {
    const char *name;   /* Pointer to name (not null terminated) */
    int namelen;        /* Name length (can be zero) */
    int hasindex;       /* If this section specifies an index */
    int aindex;         /* The index (0 based. -1 means "*") */
} ltjson_rpath_t;


/*
 *  path_tokenise(path, rstore, rsize) - break path into sections
 *
 *  Break up referencing path into parts and store into rstore to a
 *  max of rsize. Every item has a valid name (although namelen can be
 *  zero if there is no name per se - eg: for arrays). The last item
 *  has name = NULL which terminates the list.
 *
 *  Returns: the number of sections stored or 0 if path is just "/"
 *           On error returns -1 and sets errno to:
 *              EINVAL on bad params
 *              ERANGE if path too long
 *              EILSEQ if path isn't formatted correctly
 */

static int path_tokenise(const char *path, ltjson_rpath_t *rstore, int rsize)
{
    const char *curp;
    int rnum;

    if (!path || !rstore || rsize <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (*path != '/')
    {
        errno = EILSEQ;
        return -1;
    }

    curp = path + 1;

    for (rnum = 0; rnum < rsize; rnum++)
    {
        if (!*curp)
        {
            rstore[rnum].name = 0;
            return rnum;
        }

        if (*curp == '/')
        {
            errno = EILSEQ;
            return -1;
        }

        rstore[rnum].name = curp;
        rstore[rnum].namelen = 0;
        rstore[rnum].hasindex = 0;
        rstore[rnum].aindex = -1;

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
            {
                errno = EILSEQ;
                return -1;
            }

            rstore[rnum].aindex = (int)l;
            curp = endptr + 1;
        }

        if (*curp)
        {
            if (*curp != '/')
            {
                errno = EILSEQ;
                return -1;
            }
            curp++;
        }

    }   /* for (rnum...) */

    errno = ERANGE;
    return -1;
}




/*
 *  path_hashify_rpath(jsoninfo, refpaths) - Convert name refpath to hashes
 *
 *  If the json tree is hashed, convert any names in the referencing path
 *  list refpaths (which is terminated by name == 0) to hash references.
 *  Mark each hash reference by making namelen equal -1.
 *
 *  Returns: 1 on success, which includes the tree having no hash
 *           0 if any name in the refpaths list does not have a hash
 */

static int path_hashify_rpath(ltjson_info_t *jsoninfo,
                              ltjson_rpath_t *refpaths)
{
    struct nhashcell *nhcp;
    unsigned long hashval;
    const char *hashname;

    if (!jsoninfo->nhtab)
        return 1;

    while (refpaths->name != NULL)
    {
        if (refpaths->namelen == 0)
        {
            refpaths++;
            continue;
        }

        hashval =
            djbnhash(refpaths->name, refpaths->namelen) % NHASH_NBUCKETS;

        hashname = 0;

        for (nhcp = jsoninfo->nhtab[hashval]; nhcp; nhcp = nhcp->next)
        {
            if (strncmp(refpaths->name, nhcp->s, refpaths->namelen) == 0)
            {
                hashname = nhcp->s;
                break;
            }
        }

        if (!hashname)
            return 0;

        refpaths->name = hashname;
        refpaths->namelen = -1;
        refpaths++;
    }

    return 1;
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

static int path_getobject(ltjson_node_t *atnode, ltjson_rpath_t *refpath,
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

    /* NB: The current node (atnode) is the last match. To find
       the next match, atnode must be an object or an array.
       Any new matches will then be entries under atnode */

    switch (atnode->ntype)
    {
        case LTJSON_NTYPE_OBJECT:
            if (!refpath->namelen)      /* No name given */
                return 0;
        break;

        case LTJSON_NTYPE_ARRAY:
            if (refpath->namelen)       /* Name given */
                return 0;
        break;

        default:
            return 0;
    }

    if (atnode->ntype == LTJSON_NTYPE_OBJECT)
    {
        atnode = atnode->val.subnode;
        if (!atnode)
            return 0;

        do {
            if (refpath->namelen < 0)       /* Hash */
            {
                if (refpath->name == atnode->name)      /* Compare pointers */
                    break;
            }
            else if (strncmp(atnode->name, refpath->name,
                                           refpath->namelen) == 0)
            {
                /* found */
                break;
            }

        } while ((atnode = atnode->next) != NULL);

        if (!atnode)
            return 0;


        /* Match! - there will only ever be one match in an object
           atnode is now pointing to that singular match */

        if (atnode->ntype != LTJSON_NTYPE_ARRAY)
        {
            /* item is not an array => cannot have indexes: */

            if (refpath->hasindex)
                return 0;

            /* object or other item: recurse to next match and finish: */

            return path_getobject(atnode, refpath + 1,
                                  nodestorep, storeavail);
        }

        /* Item is an array. If index is not specified the default aindex
           value (-1) will specify all indexes. If the item is the last in
           the search spec, we want the item itself, NOT every subitem! */

        if (!refpath->hasindex && refpath[1].name == NULL)
        {
            return path_getobject(atnode, refpath + 1, nodestorep, storeavail);
        }
    }

    assert(atnode->ntype == LTJSON_NTYPE_ARRAY);

    /* Array searching. Can drop through from the object search... */

    atnode = atnode->val.subnode;
    if (!atnode)
        return 0;

    thisindex = 0;
    nfound = 0;

    do {
        if (refpath->aindex >= 0 && refpath->aindex != thisindex++)
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
 *  The reference path is an expression that must start with a / to
 *  represent the root, followed by / separated object or array
 *  references. Use [] to represent array offsets:
 *          /phoneNumbers/type
 *          /phoneNumbers[1]/type
 *          /[3]/store/book
 *  An array offset can be left out or represented by [] or [*] to
 *  denote "all elements" in the array. The offset is 0 based.
 *  If an array is last item in a path, the array is returned, not
 *  all the elements of the array (as for other parts of the path).
 *
 *  Returns: Number of matches found (not stored) on success or
 *           0 on failure and, if an error, sets errno to one of
 *              EINVAL if tree is not valid, closed or finalised
 *              EILSEQ if path expression is not understood
 *              ERANGE if path is too long
 */

int ltjson_pathrefer(ltjson_node_t *tree, const char *path,
                     ltjson_node_t **nodeptr, int nnodes)
{
    ltjson_info_t *jsoninfo;
    ltjson_rpath_t refpaths[8];
    int ret;

    if (!path || !nodeptr || nnodes <= 0 || !is_closed_tree(tree))
    {
        errno = EINVAL;
        return 0;
    }

    ret = path_tokenise(path, refpaths, sizeof(refpaths)/sizeof(refpaths[0]));
    if (ret < 0)
        return 0;

    if (ret == 0)
    {
        nodeptr[0] = tree;
        return 1;
    }

    /* If hashes are available, use them */

    jsoninfo = (ltjson_info_t *)tree;

    if (!path_hashify_rpath(jsoninfo, refpaths))
    {
        /* Failure to hash a name means it can never be found! */
        errno = 0;
        return 0;
    }

    ret = path_getobject(tree, refpaths, &nodeptr, &nnodes);
    if (!ret)
        errno = 0;

    return ret;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
