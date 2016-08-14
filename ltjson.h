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

#define LTJSON_MEMSTATS         13


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
extern int ltjson_display(ltjson_node_t *tree, ltjson_node_t *rnode);

extern int ltjson_memstat(ltjson_node_t *tree, int *stats, int nents);
extern const char *ltjson_statstring(int index);
extern void ltjson_statdump(ltjson_node_t *tree);

extern ltjson_node_t *ltjson_findname(ltjson_node_t *tree, const char *name,
                                      ltjson_node_t *fromnode);

extern int ltjson_pathrefer(ltjson_node_t *tree, const char *path,
                            ltjson_node_t **nodeptr, int nnodes);

#endif  /* _LTJSON_H_ */


/* vi:set expandtab ts=4 sw=4: */
