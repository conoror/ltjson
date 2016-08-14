/*
 *  ltutils.c (as include): Utility functions like print and search
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


/**
 *  ltjson_lasterror(tree) - Describe the last error that occurred
 *      @tree:  Valid tree
 *
 *  Returns: A pointer to a constant string describing the error
 */

const char *ltjson_lasterror(ltjson_node_t *tree)
{
    ltjson_info_t *jsoninfo;

    if (!is_valid_tree(tree))
        return ERR_INT_INVALIDTREE;

    jsoninfo = (ltjson_info_t *)tree;

    if (!jsoninfo->lasterr)
        return ERR_INT_NOERROR;

    return jsoninfo->lasterr;
}




/*
 *  print_nodeinfo(node, spaces) - Print node information
 *
 *  Helper routine to dump node information to stdout. The tree is
 *  assumed valid, closed and finalised. spaces is the number of
 *  leading spaces put in to tabulate the output.
 */

static void print_nodeinfo(ltjson_node_t *node, int spaces)
{
    assert(node);

    printf("%*s", spaces, "");

    if (node->ancnode && node->ancnode->ntype == LTJSON_NTYPE_OBJECT)
        printf("%s : ", node->name);

    switch (node->ntype)
    {
        case LTJSON_NTYPE_NULL:
            printf("null\n");
        break;

        case LTJSON_NTYPE_BOOL:
            if (node->val.ll)
                printf("true\n");
            else
                printf("false\n");
        break;

        case LTJSON_NTYPE_ARRAY:
            if (node->val.subnode)
                printf("[\n");
            else
                printf("[]\n");
        break;

        case LTJSON_NTYPE_OBJECT:
            if (node->val.subnode)
                printf("{\n");
            else
                printf("{}\n");
        break;

        case LTJSON_NTYPE_FLOAT:
            printf("%g\n", node->val.d);
        break;

        case LTJSON_NTYPE_INTEGER:
#ifdef _WIN32
            printf("%I64d\n", node->val.ll);
#else
            printf("%lld\n", node->val.ll);
#endif
        break;

        case LTJSON_NTYPE_STRING:
            printf("\"%s\"\n", node->val.s);
        break;

        default:
            printf("!!Node does not look valid!!\n");
    }

}




/**
 *  ltjson_display(tree) - Display the contents of the JSON tree
 *      @tree:   Valid closed tree
 *      @rnode:  Optional node to act as display root (NULL if unused)
 *
 *  Returns: 1 on success
 *           0 if tree is not valid/closed and sets errno (EINVAL)
 */

int ltjson_display(ltjson_node_t *tree, ltjson_node_t *rnode)
{
    ltjson_node_t *curnode;
    int depth = 0;

    if (!is_closed_tree(tree))
    {
        errno = EINVAL;
        return 0;
    }

    printf("JSON tree:\n");

    if (!rnode)
        rnode = tree;

    if (rnode->ntype != LTJSON_NTYPE_ARRAY &&
        rnode->ntype != LTJSON_NTYPE_OBJECT)
    {
        print_nodeinfo(rnode, 4);
        return 1;
    }

    /* Displaying an array or an object */

    curnode = rnode;

    do {
        print_nodeinfo(curnode, 4 + 4 * depth);

        if (curnode->ntype == LTJSON_NTYPE_ARRAY ||
            curnode->ntype == LTJSON_NTYPE_OBJECT)
        {
            if (curnode->val.subnode)
            {
                curnode = curnode->val.subnode;
                depth++;
                continue;
            }
            else
            {
                /* No subnode. If this is root node, then we're
                   done. Don't try and hop to curnode->next! */

                if (curnode == rnode)
                    break;
            }
        }

        if (curnode->next)
        {
            curnode = curnode->next;
            continue;
        }

        /* traverse up the tree (as per traverse_tree_nodes) */

        while ((curnode = curnode->ancnode) != NULL)
        {
            depth--;

            printf("%*s", 4 + 4*depth, "");

            if (curnode->ntype == LTJSON_NTYPE_ARRAY)
                printf("]\n");
            else
                printf("}\n");

            if (curnode == rnode)
                break;

            if (curnode->next)
            {
                curnode = curnode->next;
                break;
            }
        }

    } while (curnode && curnode != rnode);

    return 1;
}




/**
 *  ltjson_memstat(tree, stats, nents) - get memory usage statistics
 *      @tree:  Valid tree
 *      @stats: Pointer to an array of ints which is filled
 *      @nents: Number of entries in stats
 *
 *  Tree does not have to be closed. No changes are made
 *
 *  Returns: the number of stats placed in stats array
 *           0 if tree/stats/nents not valid and sets errno (EINVAL)
 */

int ltjson_memstat(ltjson_node_t *tree, int *stats, int nents)
{
    ltjson_info_t *jsoninfo;
    int jmstats[MSTAT_NENTS] = {0};
    int i;

    if (!is_valid_tree(tree) || !stats || !nents)
    {
        errno = EINVAL;
        return 0;
    }

    jsoninfo = (ltjson_info_t *)tree;

    if (nents > MSTAT_NENTS)
        nents = MSTAT_NENTS;

    jmstats[MSTAT_TOTAL] = sizeof(ltjson_info_t);

    /* Traverse the basenodes (if they exist) */

    if (jsoninfo->cbasenode)
    {
        ltjson_node_t *basenode;

        basenode = jsoninfo->cbasenode->ancnode;    /* First basenode */

        do {
            jmstats[MSTAT_NODES_ALLOC] += jsoninfo->nodeasize - 1;
            jmstats[MSTAT_NODES_USED] += basenode->val.nused - 1;
            jmstats[MSTAT_TOTAL] += jsoninfo->nodeasize *
                                    sizeof(ltjson_node_t);

            basenode = basenode->next;

        } while (basenode != basenode->ancnode);
    }

    jmstats[MSTAT_WORKSTR_ALLOC] = jsoninfo->workalloc;
    jmstats[MSTAT_TOTAL] += jsoninfo->workalloc;

    jmstats[MSTAT_TOTAL] += sstore_stats(&jsoninfo->sstore,
                                         &jmstats[MSTAT_SSTORE_NBLOCKS],
                                         &jmstats[MSTAT_SSTORE_ALLOC],
                                         &jmstats[MSTAT_SSTORE_FILLED]);

    if (jsoninfo->nhtab)
    {
        jmstats[MSTAT_HASH_NBUCKETS] = NHASH_NBUCKETS;
        jmstats[MSTAT_HASH_HITS] = jsoninfo->nh_nhits;
        jmstats[MSTAT_HASH_MISSES] = jsoninfo->nh_nmisses;

        jmstats[MSTAT_TOTAL] += nhash_stats(jsoninfo,
                                            &jmstats[MSTAT_HASH_BUCKETFILL],
                                            &jmstats[MSTAT_HASHCELL_ALLOC],
                                            &jmstats[MSTAT_HASHCELL_FILLED]);
    }
    else
    {
        if (nents > 7)
            nents = 7;
    }

    for (i = 0; i < nents; i++)
        stats[i] = jmstats[i];

    return nents;
}




/**
 *  ltjson_statstring(index) - return statistic description string
 *      @index:  Valid index
 *
 *  Returns: a const char string description of the statistic
 *           NULL if index is invalid (errno to ERANGE)
 */

const char *ltjson_statstring(int index)
{
    if (index < 0 || index >= MSTAT_NENTS)
        return NULL;

    return ltjson_memstatdesc[index];
}




/**
 *  ltjson_statdump(tree) - print out memory usage statistics
 *      @tree:  Valid tree
 *
 *  Tree does not have to be closed. No changes are made
 */

void ltjson_statdump(ltjson_node_t *tree)
{
    int mstats[MSTAT_NENTS];
    int i, ret;
    const char *sstr;

    printf("Ltjson memory statistics\n");

    ret = ltjson_memstat(tree, mstats, MSTAT_NENTS);

    if (!ret)
    {
        printf("\tTree is not valid. No statistics available.\n");
        return;
    }

    for (i = 0; i < ret; i++)
    {
        sstr = ltjson_statstring(i);
        if (sstr)
            printf("\t%s: %i\n", sstr, mstats[i]);
    }
}




/**
 *  ltjson_findname(tree, name, nodeptr) - Find name in tree
 *      @tree:  Valid closed tree
 *      @name:  Search text (utf-8)
 *      @node:  Optional starting point
 *
 *  If node is NULL, the search begins at the root of the tree.
 *  Otherwise, the search proceeds from *after* that node point.
 *
 *  Returns: a pointer to the matched node on success
 *           NULL on failure setting errno to EINVAL if error
 */

ltjson_node_t *ltjson_findname(ltjson_node_t *tree, const char *name,
                               ltjson_node_t *fromnode)
{
    ltjson_node_t *curnode;
    ltjson_info_t *jsoninfo;
    static const char *prevname, *hashname;

    if (!is_closed_tree(tree) || !name)
    {
        errno = EINVAL;
        return NULL;
    }

    jsoninfo = (ltjson_info_t *)tree;

    if (fromnode)
    {
        if ((curnode = traverse_tree_nodes(fromnode)) == NULL)
        {
            errno = 0;
            return NULL;
        }
    }
    else
    {
        curnode = tree;
    }

    if (jsoninfo->nhtab)
    {
        struct nhashcell *nhcp;
        unsigned long hashval;

        if (prevname != name)
        {
            /* Didn't do this search before... */

            prevname = name;
            hashname = 0;

            hashval = djbhash(name) % NHASH_NBUCKETS;

            for (nhcp = jsoninfo->nhtab[hashval];
                        nhcp != NULL;
                        nhcp = nhcp->next)
            {
                if (strcmp(name, nhcp->s) == 0)
                {
                    hashname = nhcp->s;
                    break;
                }
            }
        }

        if (!hashname)
        {
            errno = 0;
            return NULL;
        }
    }
    else
    {
        hashname = 0;
    }


    while (curnode)
    {
        if (curnode->name)
        {
            if (hashname)
            {
                /* Pointers will be the same iff hashed */
                if (hashname == curnode->name)
                    return curnode;
            }
            else if (strcmp(curnode->name, name) == 0)
            {
                return curnode;
            }
        }

        curnode = traverse_tree_nodes(curnode);
    }

    errno = 0;
    return NULL;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
