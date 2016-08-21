/*
 *  ltsort.c (as include): Sorting and searching functions
 *
 *  This code is supposed to be #included into the main ltjson.c file
 *  which makes the codebase less unwieldy but keep static namespace.
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Merge sort algorithm described by S. Tatham (he of PuTTY fame) at:
 *    http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
 *
 *  Copyright 2016 Conor F. O'Rourke. All rights reserved.
 */

#ifdef _LTJSON_INLINE_INCLUDE_


/**
 *  ltjson_sort(snode, compar, parg) - Sort an object/array node
 *      @snode:  Node whose contents to sort
 *      @compar: Pointer to function to determine order
 *      @parg:   Optional argument pointer for any private data needed
 *
 *  The compar function takes two ltjson nodes which are compared to
 *  return the same codes as qsort and strcmp (ie: -1, 0, +1). The
 *  function also takes two extra parameters, the jsontree itself is
 *  the first and the optional private argument pointer is the second.
 *
 *  To get the jsontree itself, this routine walks back to the root
 *  via the ancnode links.
 *
 *  Returns: 1 on success
 *           0 on error and sets errno (EINVAL)
 */

int ltjson_sort(ltjson_node_t *snode,
                int (*compar)(ltjson_node_t *, ltjson_node_t *,
                              ltjson_node_t *, void *),
                void *extrap)
{
    ltjson_node_t *tree;
    ltjson_node_t *listhead, *listtail, *p, *q, *enode;
    int i, ksize, psize, qsize, merges;

    if (!snode || !compar)
    {
        errno = EINVAL;
        return 0;
    }

    if (snode->ntype != LTJSON_NTYPE_ARRAY &&
        snode->ntype != LTJSON_NTYPE_OBJECT)
    {
        errno = EINVAL;
        return 0;
    }

    for (tree = snode; tree->ancnode != NULL; tree = tree->ancnode)
        ;

    if (!is_closed_tree(tree))
    {
        errno = EINVAL;
        return 0;
    }

    if (snode->val.subnode == NULL)
    {
        /* That was easy! */
        return 1;
    }

    listhead = snode->val.subnode;


    /* Merge sort the list at listhead.
       We merge lists of size ksize into ksize * 2,
       starting with a size of 1. Each pass, count
       how many merges were done... */

    ksize = 1;

    while (1)
    {
        p = listhead;
        listtail = listhead = NULL;

        merges = 0;

        while (p != NULL)
        {
            merges++;

            /* Step list q a max of ksize places along from list p */

            for (q = p, psize = 0, i = 0; i < ksize; i++)
            {
                psize++;
                q = q->next;
                if (!q)
                    break;
            }

            qsize = ksize;

            /* psize is the number of elements q is past p, which
               will be the same as ksize until q fell off the end.

               Two adjacent lists to merge:
                    Starting at p and of size psize
                    Starting at q and at most qsize

               NB: qsize can be greater than the nodes available.

               Extract enode from p or q and add each to listtail until
               we run out of nodes in one or other list:
           */

            while (psize || (q && qsize))
            {
                /* Choose where to take the next element. If one list
                   empty, choose the other. If both non-empty, compare
                   the elements and take the lesser. If equal, choose p. */

                if (!psize)
                {
                    /* Choose q */
                    enode = q;
                    q = q->next;
                    qsize--;
                }
                else if (!q || !qsize)
                {
                    /* Choose p */
                    enode = p;
                    p = p->next;
                    psize--;
                }
                else if (compar(p, q, tree, extrap) <= 0)
                {
                    /* Neither empty. p <= q: choose p */
                    enode = p;
                    p = p->next;
                    psize--;
                }
                else
                {
                    /* Neither empty. p > q: choose q */
                    enode = q;
                    q = q->next;
                    qsize--;
                }

                /* Add enode to the end of newlist, keep track of the tail */

                if (listtail)
                    listtail->next = enode;
                else
                    listhead = enode;

                listtail = enode;
                listtail->next = NULL;

            }   /* while (psize ...) */


            /* Now p is where q started out, and q is pointing at the next
               pair of lists to merge (or it's null).
               set p to the value of q, and repeat */

            p = q;

        }   /* while (pnode) */


        /* If only one merge performed, output list is sorted */

        if (merges <= 1)
        {
            /* Sorted list in listhead. Attach it to search node */

            snode->val.subnode = listhead;
            return 1;
        }

        /* Else, double ksize and go back and do this again */

        ksize *= 2;

    }   /* while (1) */

}




/**
 *  ltjson_search(rnode, name, fromnode, flags) - Search tree for name
 *      @rnode:     Subtree within which to search
 *      @name:      Object member name
 *      @fromnode:  Optional starting point
 *      @flags:     Optional search flags
 *
 *  Search through the JSON tree rooted at @rnode for the member name
 *  @name, optionally resuming a previous search from the point after
 *  the node @fromnode.
 *
 *  Flags supported are: LTJSON_SEARCH_NAMEISHASH to denote that the
 *  name is one retrieved using ltjson_get_hashstring.
 *
 *  This routine does not check if @objnode is part of a closed tree.
 *
 *  Returns: Pointer to the matched node on success
 *           NULL on failure with errno is set to:
 *              EINVAL if passed null parameters
 *              EPERM  if rnode is not an object/array
 *              0      if entry not found (not really an error)
 *
 */

ltjson_node_t *ltjson_search(ltjson_node_t *rnode, const char *name,
                             ltjson_node_t *fromnode, int flags)
{
    ltjson_node_t *curnode;

    if (!rnode || !name)
    {
        errno = EINVAL;
        return NULL;
    }

    if (rnode->ntype != LTJSON_NTYPE_OBJECT &&
        rnode->ntype != LTJSON_NTYPE_ARRAY)
    {
        errno = EPERM;
        return NULL;
    }

    if (!fromnode)
        fromnode = rnode;

    curnode = traverse_tree_nodes(fromnode, rnode);

    while (curnode)
    {
        if (curnode->name)
        {
            if (flags & LTJSON_SEARCH_NAMEISHASH)
            {
                if (curnode->name == name)
                    return curnode;
            }
            else
            {
                if (*curnode->name == *name &&
                        strcmp(curnode->name, name) == 0)
                    return curnode;
            }
        }

        curnode = traverse_tree_nodes(curnode, rnode);
    }

    errno = 0;
    return NULL;
}




/**
 *  ltjson_promote(rnode, name) - Promote object member to first
 *      @rnode: Subtree within which to promote
 *      @name:  Name of the object member to promote
 *
 *  Traverse the JSON tree rooted at the subtree @rnode and promote
 *  object members so that the member is listed first in that object.
 *  This routine checks that rnode is part of a closed tree.
 *
 *  Returns: 1 on success
 *           0 on error and sets errno:
 *              EINVAL if tree is not closed or name is null
 *              EPERM  if rnode is not an object or array
 *              ENOENT if no names were found to promote
 */

int ltjson_promote(ltjson_node_t *rnode, const char *name)
{
    ltjson_node_t *tree, *curnode;
    const char *hname;
    int matches;

    if (!rnode || !name)
    {
        errno = EINVAL;
        return 0;
    }

    if (rnode->ntype != LTJSON_NTYPE_OBJECT &&
        rnode->ntype != LTJSON_NTYPE_ARRAY)
    {
        errno = EPERM;
        return 0;
    }

    if (rnode->val.subnode == NULL)
    {
        errno = ENOENT;
        return 0;
    }

    for (tree = rnode; tree->ancnode != NULL; tree = tree->ancnode)
        ;

    hname = ltjson_get_hashstring(tree, name);
    if (!hname)
    {
        if (errno == EINVAL)
            return 0;

        else if (errno == 0)
        {
            errno = ENOENT;
            return 0;
        }
    }


    curnode = rnode;
    matches = 0;

    while (curnode)
    {
        if (curnode->ntype == LTJSON_NTYPE_OBJECT &&
            curnode->val.subnode != NULL)
        {
            ltjson_node_t *prevnode, *promnode;

            prevnode = 0;
            promnode = curnode->val.subnode;

            do {
                if (hname)
                {
                    if (promnode->name == hname)
                        break;
                }
                else
                {
                    if (*promnode->name == *name &&
                            strcmp(promnode->name, name) == 0)
                        break;
                }

                prevnode = promnode;

            } while ((promnode = promnode->next) != NULL);

            if (promnode && prevnode)
            {
                /* Node to promote is found and is not first already.
                   Break out promnode and place it first: */

                prevnode->next = promnode->next;    /* Hop over promnode   */
                prevnode = curnode->val.subnode;    /* Save old topnode    */
                curnode->val.subnode = promnode;    /* topnode = promnode  */
                promnode->next = prevnode;          /* topnode->next = old */
                matches++;
            }
        }

        curnode = traverse_tree_nodes(curnode, rnode);
    }

    if (!matches)
    {
        errno = ENOENT;
        return 0;
    }

    return 1;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
