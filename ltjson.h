/*
 *  Light JSON implementation in C
 *  Header include file
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2015 Conor F. O'Rourke. All rights reserved.
 */

#ifndef _LTJSON_H_
#define _LTJSON_H_


enum ltjson_nodetype {
    LTJSON_EMPTY,
    LTJSON_NULL,
    LTJSON_BOOL,
    LTJSON_ARRAY,
    LTJSON_OBJECT,
    LTJSON_DOUBLE,
    LTJSON_INTEGER,
    LTJSON_STRING,
    LTJSON_BASE
};


typedef struct ltjson_node
{
    enum ltjson_nodetype ntype;
    unsigned char *name;
    int nameoff;

    union {
        int nused;
        long l;
        double d;
        struct ltjson_node *subnode;
        int vlen;
        unsigned char *vstr;
    } val;

    struct ltjson_node *next;
    struct ltjson_node *ancnode;

} ltjson_node_t;


extern int ltjson_parse(ltjson_node_t **treeptr, unsigned char *text);
extern int ltjson_free(ltjson_node_t **treeptr);

extern const char *ltjson_lasterror(ltjson_node_t *tree);
extern int ltjson_display(ltjson_node_t *tree);
extern int ltjson_memory(ltjson_node_t *tree);

extern int ltjson_findname(ltjson_node_t *tree, const char *name,
                           ltjson_node_t **nodeptr);


#endif  /* _LTJSON_H_ */


/* vi:set expandtab ts=4 sw=4: */
