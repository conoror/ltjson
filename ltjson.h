/*
 *  Light JSON implementation in C
 *  Header include file
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2016 Conor F. O'Rourke. All rights reserved.
 */

#ifndef _LTJSON_H_
#define _LTJSON_H_


#define LTJSON_NTYPE_EMPTY      0x00
#define LTJSON_NTYPE_BASENODE   0x01
#define LTJSON_NTYPE_NULL       0x02
#define LTJSON_NTYPE_BOOL       0x03
#define LTJSON_NTYPE_ARRAY      0x04
#define LTJSON_NTYPE_OBJECT     0x05
#define LTJSON_NTYPE_FLOAT      0x06
#define LTJSON_NTYPE_INTEGER    0x07
#define LTJSON_NTYPE_STRING     0x08

#define LTJSON_MEMSTATS           13

#define LTJSON_PARSE_USEHASH       1
#define LTJSON_PARSE_KEEPHASH      2
#define LTJSON_SEARCH_NAMEISHASH   1


typedef struct ltjson_node
{
    const char *name;

    short int ntype;
    short int nflags;

    union {
        int nused;
        long long ll;
        double d;
        struct ltjson_node *subnode;
        const char *s;
    } val;

    struct ltjson_node *next;
    struct ltjson_node *ancnode;

} ltjson_node_t;


extern int ltjson_parse(ltjson_node_t **treeptr, const char *text, int usehash);
extern int ltjson_free(ltjson_node_t **treeptr);

extern const char *ltjson_lasterror(ltjson_node_t *tree);
extern int ltjson_display(ltjson_node_t *rnode);

extern int ltjson_memstat(ltjson_node_t *tree, int *stats, int nents);
extern const char *ltjson_statstring(int index);
extern void ltjson_statdump(ltjson_node_t *tree);

extern const char *ltjson_get_hashstring(ltjson_node_t *tree,
                                         const char *name);
extern const char *ltjson_mksearch(ltjson_node_t *tree, const char *name,
                                   int *flagsp);

extern ltjson_node_t *ltjson_get_member(ltjson_node_t *objnode,
                                        const char *name, int flags);

extern ltjson_node_t *ltjson_addnode_after(ltjson_node_t *tree,
                                           ltjson_node_t *anode,
                                           short int ntype, const char *name,
                                           const char *sval);

extern ltjson_node_t *ltjson_addnode_under(ltjson_node_t *tree,
                                           ltjson_node_t *oanode,
                                           short int ntype, const char *name,
                                           const char *sval);

extern int ltjson_sort(ltjson_node_t *snode,
                       int (*compar)(ltjson_node_t *, ltjson_node_t *,
                                     ltjson_node_t *, void *),
                       void *extrap);

extern ltjson_node_t *ltjson_search(ltjson_node_t *rnode, const char *name,
                                    ltjson_node_t *fromnode, int flags);

extern int ltjson_promote(ltjson_node_t *rnode, const char *name);

extern int ltjson_pathrefer(ltjson_node_t *tree, const char *path,
                            ltjson_node_t **nodeptr, int nnodes);

#endif  /* _LTJSON_H_ */


/* vi:set expandtab ts=4 sw=4: */
