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
    {
        if (*node->name == '\0')
            printf("(no name) : ");
        else
            printf("%s : ", node->name);
    }

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
 *  ltjson_display(rnode) - Display the contents of a JSON subtree
 *      @rnode:  Node to act as display root
 *
 *  Display the JSON tree rooted at rnode, which can be an entire
 *  subtree or just one node. The tree must be valid and closed to
 *  do this and the routine walks back to the root to check.
 *
 *  Returns: 1 on success
 *           0 if tree is not valid/closed and sets errno (EINVAL)
 */

int ltjson_display(ltjson_node_t *rnode)
{
    ltjson_node_t *tree, *curnode;
    int depth = 0;

    if (!rnode)
    {
        errno = EINVAL;
        return 0;
    }

    for (tree = rnode; tree->ancnode != NULL; tree = tree->ancnode)
        ;

    if (!is_closed_tree(tree))
    {
        errno = EINVAL;
        return 0;
    }

    printf("JSON tree:\n");

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
 *  ltjson_get_hashstring(tree, name) - Lookup a name in the hash table
 *      @tree:  Valid closed tree
 *      @name:  An object member name
 *
 *  Returns: Pointer to constant string on success
 *           NULL on failure with errno is set to:
 *              EINVAL if tree is not valid/closed etc
 *              ENOENT if tree has no hash table
 *              0      if entry not found (not really an error)
 */

const char *ltjson_get_hashstring(ltjson_node_t *tree, const char *name)
{
    ltjson_info_t *jsoninfo;

    if (!is_closed_tree(tree) || !name)
    {
        errno = EINVAL;
        return NULL;
    }

    jsoninfo = (ltjson_info_t *)tree;

    return nhash_lookup(jsoninfo, name);
}




/**
 *  ltjson_mksearch(tree, name, flagsp) - Create a searchable name
 *      @tree:   Valid closed tree
 *      @name:   An object member name
 *      @flagsp: Pointer to search flags
 *
 *  This is really just a helper function to create a search name without
 *  messing around figuring out if the tree is hashed or not. It fetches
 *  a hash value for name if the tree is hashed otherwise returns a pointer
 *  to the name passed in. If that name is NULL, this function will return
 *  the empty string. The flags pointer is twiddled to add or remove the
 *  LTJSON_SEARCH_NAMEISHASH flag.
 *
 *  Returns: Pointer to constant string (will not fail)
 *           However, still sets errno to reflect issues:
 *              EINVAL if tree is not valid/closed/etc
 *              ENOENT if tree is hashed and name is not there
 *              0      if returned string is good for searching
 */

const char *ltjson_mksearch(ltjson_node_t *tree, const char *name, int *flagsp)
{
    const char *srch;

    srch = ltjson_get_hashstring(tree, name);
    if (!srch)
    {
        if (*flagsp)
            *flagsp &= ~LTJSON_SEARCH_NAMEISHASH;

        if (errno == EINVAL)
        {
            return ltjson_empty_name;
        }
        else if (errno == ENOENT)
        {
            /* Tree has no hash table - this is fine */
            errno = 0;
            return name;
        }
        else
        {
            /* Tree has a hash table, not found. Not good */
            errno = ENOENT;
            return name;
        }
    }

    if (*flagsp)
        *flagsp |= LTJSON_SEARCH_NAMEISHASH;

    errno = 0;
    return srch;
}




/**
 *  ltjson_get_member(objnode, name, flags) - Retrieve object member
 *      @objnode: A pointer to an object node
 *      @name:    An object member name
 *      @flags:   Optional search flags
 *
 *  Look through the object pointed to by @objnode for the member @name.
 *  Flags supported are: LTJSON_SEARCH_NAMEISHASH to denote that the
 *  name is one retrieved using ltjson_get_hashstring. This function does
 *  not recurse down a tree, just hops from node to node within the object.
 *
 *  This routine does not check if @objnode is part of a closed tree.
 *
 *  Returns: Pointer to the matched node on success
 *           NULL on failure with errno is set to:
 *              EINVAL if passed null parameters
 *              EPERM  if objnode is not an object
 *              0      if entry not found (not really an error)
 *
 */

ltjson_node_t *ltjson_get_member(ltjson_node_t *objnode,
                                 const char *name, int flags)
{
    if (!objnode || !name)
    {
        errno = EINVAL;
        return NULL;
    }

    if (objnode->ntype != LTJSON_NTYPE_OBJECT)
    {
        errno = EPERM;
        return NULL;
    }

    objnode = objnode->val.subnode;

    if (!objnode)
    {
        errno = 0;
        return NULL;
    }

    do {
        if (flags & LTJSON_SEARCH_NAMEISHASH)
        {
            if (objnode->name == name)
                return objnode;
        }
        else
        {
            if (*objnode->name == *name && strcmp(objnode->name, name) == 0)
                return objnode;
        }

    } while ((objnode = objnode->next) != NULL);

    errno = 0;
    return NULL;
}




/*
 *  add_new_node(...) - Generic call for addnode_after and addnode_under
 *
 *  jsoninfo is a pointer to the json info struct and must be valid.
 *
 *  Returns: (as addnode_after)
 */

static ltjson_node_t *add_new_node(ltjson_info_t *jsoninfo,
                                   ltjson_node_t *refnode,
                                   int new_is_after,
                                   short int ntype, const char *name,
                                   const char *sval)
{
    ltjson_node_t *newnode, *oanode;
    const char *nvstr;

    assert(jsoninfo);

    if (!refnode || (new_is_after && !refnode->ancnode))
    {
        errno = EINVAL;
        return NULL;
    }

    if (ntype <= LTJSON_NTYPE_BASENODE || ntype > LTJSON_NTYPE_STRING)
    {
        errno = ERANGE;
        return NULL;
    }

    /* Set oanode to the relevant object or array node and check it */

    if (new_is_after)
        oanode = refnode->ancnode;
    else
        oanode = refnode;

    if (oanode->ntype != LTJSON_NTYPE_OBJECT &&
        oanode->ntype != LTJSON_NTYPE_ARRAY)
    {
        errno = EPERM;
        return NULL;
    }

    if (oanode->ntype == LTJSON_NTYPE_OBJECT && !name)
    {
        errno = EINVAL;
        return NULL;
    }


    /* Get a new node and optionally add name to hash */

    if ((newnode = get_new_node(jsoninfo)) == NULL)
        return NULL;

    if (oanode->ntype == LTJSON_NTYPE_OBJECT)
    {
        if ((nvstr = nhash_insert(jsoninfo, name)) == NULL)
            return NULL;
    }
    else
    {
        nvstr = NULL;
    }

    newnode->name   = nvstr;
    newnode->ntype  = ntype;
    newnode->nflags = 0;


    switch (ntype)
    {
        case LTJSON_NTYPE_NULL:
        case LTJSON_NTYPE_BOOL:
        case LTJSON_NTYPE_INTEGER:
            newnode->val.ll = 0;
        break;

        case LTJSON_NTYPE_ARRAY:
        case LTJSON_NTYPE_OBJECT:
            newnode->val.subnode = NULL;
        break;

        case LTJSON_NTYPE_FLOAT:
            newnode->val.d = 0.0;
        break;

        case LTJSON_NTYPE_STRING:
        {
            if (!sval || !*sval)
            {
                nvstr = ltjson_empty_name;
            }
            else
            {
                nvstr = sstore_add(&jsoninfo->sstore, sval);
                if (!nvstr)
                    return NULL;
            }

            newnode->val.s = nvstr;
        }
        break;
    }


    /* Finally insert node into tree */

    if (new_is_after)
    {
        /* Insert after refnode: */
        newnode->next    = refnode->next;
        newnode->ancnode = refnode->ancnode;
        refnode->next    = newnode;
    }
    else
    {
        /* Insert below refnode: */
        newnode->next        = refnode->val.subnode;
        newnode->ancnode     = refnode;
        refnode->val.subnode = newnode;
    }

    return newnode;
}




/**
 *  ltjson_addnode_after(tree, anode, ntype, name, sval) - Add a node after
 *      @tree:   Valid closed tree
 *      @anode:  Node after which to insert the new node
 *      @ntype:  New node type
 *      @name:   New node name, if applicable
 *      @sval:   New node string value, if applicable
 *
 *  Insert a new node after @anode with the type @ntype. If anode's
 *  ancestor node is an object, name is required. The ancestor of anode
 *  must be an array or object and cannot be the root of the tree.
 *
 *  If the ntype is LTJSON_NTYPE_STRING then sval is used, if supplied,
 *  to set the new node's string value. sval may be null or "" in which
 *  case the value is a local static equal to "". You may then set sval
 *  to another value.
 *
 *  For ntypes of integer and float, you need to set the value yourself.
 *
 *  Returns: Pointer to the matched node on success
 *           NULL on failure with errno is set to:
 *              EINVAL if passed incorrect parameters
 *              ERANGE if ntype out of range for a node
 *              EPERM  if anode's ancestor is not an object or array
 *              ENOMEM if out of memory during node creation (tree remains)
 */

ltjson_node_t *ltjson_addnode_after(ltjson_node_t *tree, ltjson_node_t *anode,
                                    short int ntype, const char *name,
                                    const char *sval)
{
    ltjson_info_t *jsoninfo;

    if (!tree || !anode || !is_closed_tree(tree))
    {
        errno = EINVAL;
        return NULL;
    }

    jsoninfo = (ltjson_info_t *)tree;

    return add_new_node(jsoninfo, anode, 1, ntype, name, sval);
}




/**
 *  ltjson_addnode_under(tree, oanode, ntype, name, sval) - Add a node under
 *      @tree:   Valid closed tree
 *      @oanode: Object or array node under which to insert the new node
 *      @ntype:  New node type
 *      @name:   New node name, if applicable
 *      @sval:   New node string value, if applicable
 *
 *  Insert a new node beneath @oanode with the type @ntype. If oanode
 *  is an object, name is required. oanode must be an array or object
 *  and can be the root of the tree.
 *
 *  If the ntype is LTJSON_NTYPE_STRING then sval is used, if supplied,
 *  to set the new node's string value. sval may be null or "" in which
 *  case the value is a local static equal to "". You may then set sval
 *  to another value.
 *
 *  For ntypes of integer and float, you need to set the value yourself.
 *
 *  Returns: Pointer to the matched node on success
 *           NULL on failure with errno is set to:
 *              EINVAL if passed incorrect parameters
 *              ERANGE if ntype out of range for a node
 *              EPERM  if oanode is not an object or array
 *              ENOMEM if out of memory during node creation (tree remains)
 */

ltjson_node_t *ltjson_addnode_under(ltjson_node_t *tree, ltjson_node_t *oanode,
                                    short int ntype, const char *name,
                                    const char *sval)
{
    ltjson_info_t *jsoninfo;

    if (!tree || !oanode || !is_closed_tree(tree))
    {
        errno = EINVAL;
        return NULL;
    }

    jsoninfo = (ltjson_info_t *)tree;

    return add_new_node(jsoninfo, oanode, 0, ntype, name, sval);
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
