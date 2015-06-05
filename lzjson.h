/*
 *  JSON implementation in C
 *  Header include
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2015 Conor F. O'Rourke. All rights reserved.
 */


typedef enum {
    LZJSON_EMPTY,
    LZJSON_NULL,
    LZJSON_BOOL,
    LZJSON_ARRAY,
    LZJSON_OBJECT,
    LZJSON_DOUBLE,
    LZJSON_INTEGER,
    LZJSON_STRING,
    LZJSON_STORE,
    LZJSON_BASE
} lzjson_nodetype_t;


struct lzjson_node
{
    lzjson_nodetype_t ntype;
    int name;

    union {
        int s;
        int nused;
        long l;
        double d;
        struct lzjson_node *subnode;
        void *p;
    } val;

    struct lzjson_node *next;
    struct lzjson_node *ancnode;
};


extern int lzjson_parse(unsigned char *text, struct lzjson_node **nodepp);
extern int lzjson_free_tree(struct lzjson_node **nodepp);

extern const char *lzjson_lasterror(void);

extern int lzjson_display_tree(struct lzjson_node *tree);
extern int lzjson_search_name(struct lzjson_node *tree, const char *name,
                              struct lzjson_node **answerp);

extern const char *lzjson_get_name(struct lzjson_node *tree,
                                   struct lzjson_node *node);
extern const char *lzjson_get_sval(struct lzjson_node *tree,
                                   struct lzjson_node *node);

extern int lzjson_tree_usage(struct lzjson_node *tree);


/* vi:set expandtab ts=4 sw=4: */
