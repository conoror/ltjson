/*
 *  ltutils.c (as include): Utility functions like print and search
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


/**
 *  ltjson_lasterror(tree) - Describe the last error that occurred
 *      @tree:  Valid tree
 *
 *  Returns:    A pointer to a constant string describing the error
 *              NULL if tree is not valid
 */

const char *ltjson_lasterror(ltjson_node_t *tree)
{
    ltjson_info_t *jsoninfo;
    static const char *noerr = "No error";

    if (!is_valid_tree(tree))
        return NULL;

    jsoninfo = (ltjson_info_t *)tree;

    if (!jsoninfo->lasterr)
        return noerr;

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
    printf("%*s", spaces, "");

    if (node->ancnode && node->ancnode->ntype == LTJSON_OBJECT)
        printf("%s : ", node->name);

    switch (node->ntype)
    {
        case LTJSON_EMPTY:
            printf("\n");
        break;

        case LTJSON_NULL:
            printf("null\n");
        break;

        case LTJSON_BOOL:
            if (node->val.l)
                printf("true\n");
            else
                printf("false\n");
        break;

        case LTJSON_ARRAY:   printf("[\n"); break;
        case LTJSON_OBJECT:  printf("{\n"); break;

        case LTJSON_DOUBLE:
            printf("%g\n", node->val.d);
        break;

        case LTJSON_INTEGER:
            printf("%ld\n", node->val.l);
        break;

        case LTJSON_STRING:
            printf("\"%s\"\n", node->val.vstr);
        break;

        default:
            printf("Node does not look valid\n");
    }

}




/**
 *  ltjson_display(tree) - Display the contents of the JSON tree
 *      @tree:   Valid closed tree
 *      @rnode:  Optional node to act as display root (NULL if unused)
 *
 *  Returns:    0 on success
 *              -EINVAL if tree is not valid or closed
 */

int ltjson_display(ltjson_node_t *tree, ltjson_node_t *rnode)
{
    ltjson_node_t *curnode;
    int depth = 0;

    if (!is_closed_tree(tree))
        return -EINVAL;

    printf("JSON tree:\n");

    if (!rnode)
        rnode = tree;

    if (rnode->ntype != LTJSON_ARRAY &&
        rnode->ntype != LTJSON_OBJECT)
    {
        print_nodeinfo(rnode, 4);
        return 0;
    }

    curnode = rnode;

    do {
        print_nodeinfo(curnode, 4 + 4 * depth);

        if (curnode->ntype == LTJSON_ARRAY ||
            curnode->ntype == LTJSON_OBJECT)
        {
            curnode = curnode->val.subnode;
            depth++;
            continue;
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

            if (curnode->ntype == LTJSON_ARRAY)
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

    return 0;
}




/**
 *  ltjson_memory(tree) - returns memory usage of json tree
 *      @tree:  Valid tree
 *
 *  Tree does not have to be closed. No changes are made
 *
 *  Returns +ve bytes on success or -EINVAL if tree not valid
 */

int ltjson_memory(ltjson_node_t *tree)
{
    ltjson_info_t *jsoninfo;
    ltjson_node_t *basenode;
    int nbytes;

    if (!is_valid_tree(tree))
        return -EINVAL;

    jsoninfo = (ltjson_info_t *)tree;

    nbytes = sizeof(ltjson_info_t) + jsoninfo->salloc;

    /* Traverse the basenodes (if they exist) */

    if (!jsoninfo->cbasenode)
        return nbytes;

    basenode = jsoninfo->cbasenode->ancnode;    /* First basenode */

    do {
        nbytes += LTJSON_NODEALLOCSIZE * sizeof(ltjson_node_t);
        basenode = basenode->next;
    } while (basenode != basenode->ancnode);

    return nbytes;
}




/**
 *  ltjson_findname(tree, name, nodeptr) - Find name in tree
 *      @tree:      Valid closed tree
 *      @name:      Search text (utf-8)
 *      @nodeptr:   Answer and/or starting point
 *
 *  If nodeptr points to NULL, the search begins at the root of the
 *  tree. Otherwise, the search proceeds from that node point on.
 *
 *  Returns:    1 on success, pointing nodeptr to the answer.
 *              0 if not found (sets nodeptr to NULL)
 *              -EINVAL if tree is not valid or closed
 *                   or either name or nodeptr are NULL.
 */

int ltjson_findname(ltjson_node_t *tree, const char *name,
                    ltjson_node_t **nodeptr)
{
    ltjson_node_t *curnode;

    if (!is_closed_tree(tree) || !name || !nodeptr)
        return -EINVAL;

    if (*nodeptr)
        curnode = traverse_tree_nodes(*nodeptr);
    else
        curnode = tree;

    while (curnode)
    {
        /* We're searching for names. Any node with a proper name
           has an immediate ancestor which is an object */

        if (curnode->ancnode && curnode->ancnode->ntype == LTJSON_OBJECT)
        {
            if (strcmp((char *)curnode->name, name) == 0)
            {
                /* found! */
                *nodeptr = curnode;
                return 1;
            }
        }

        curnode = traverse_tree_nodes(curnode);
    }

    *nodeptr = NULL;
    return 0;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
