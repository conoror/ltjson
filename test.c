#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "ltjson.h"


static ltjson_node_t *jsontree = 0;

//int ltjson_allocsize_nodes = 1;
//int ltjson_allocsize_sstore = 1;


int bookcomp(ltjson_node_t *a, ltjson_node_t *b,
             ltjson_node_t *tree, void *extra)
{
    /* return a <=> b */

    ltjson_node_t *fna, *fnb;

    fna = ltjson_get_member(a, "author", 0);
    fnb = ltjson_get_member(b, "author", 0);

    if (fna->ancnode != a)
        fna = 0;

    if (fnb->ancnode != b)
        fnb = 0;

    if (!fna && !fnb)
        return 0;
    else if (!fna || fna->ntype != LTJSON_NTYPE_STRING)
        return 1;
    else if (!fnb || fnb->ntype != LTJSON_NTYPE_STRING)
        return -1;

    return strcmp(fna->val.s, fnb->val.s);
}


int dump_file(char *filename)
{
    int nbytes, ret;
    FILE *fp;
    char buffer[64];

    fp = fopen(filename, "r");
    if (!fp)
    {
        perror("Open fail");
        return -1;
    }

    ret = 0;

    while ((nbytes = fread(buffer, 1, sizeof(buffer) - 1, fp)) > 0)
    {
        buffer[nbytes] = 0;

        ret = ltjson_parse(&jsontree, buffer, LTJSON_PARSE_USEHASH);

        if (ret == 1)
            break;

        if (errno == EAGAIN)
        {
            printf("Parse returns eagain. Around we go again\n");
        }
        else
        {
            printf("Error string = %s\n", strerror(-ret));

            if (errno == EILSEQ)
                printf("Further info: %s\n", ltjson_lasterror(jsontree));

            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    if (ret == 0)
    {
        printf("Tree was never closed.\n");
        return -1;
    }

    ltjson_statdump(jsontree);

    printf("\nTree output:\n");

    ltjson_display(jsontree);
    return 0;
}


int main()
{
    ltjson_node_t *namenode, *srchnode;
    ltjson_node_t *searchnode[10];
    const char *srchpath, *srchtext;
    int ret, srchflags = 0;

    printf("Testing of ltjson library...\n");

    if (dump_file("test.txt"))
        exit(1);

    srchtext = ltjson_mksearch(jsontree, "number", &srchflags);
    if (errno)
        printf("searches on \"number\" will fail\n");

    printf("Searching using a %shashed name\n",
        (srchflags & LTJSON_SEARCH_NAMEISHASH) ? "" : "un");

    namenode = NULL;

    do {
        namenode = ltjson_search(jsontree, srchtext, namenode, srchflags);

        if (namenode)
        {
            printf("found!\n");
            printf("%s = ", namenode->name);
            if (namenode->ntype == LTJSON_NTYPE_STRING)
                printf("%s\n", namenode->val.s);
            else
                printf("????\n");
        }
        else if (errno)
        {
            printf("Search returns error\n");
            break;
        }

    } while (namenode);


    // srchpath = "/phoneNumbers/[1]";
    srchpath = "/address/\377/default";

    printf("testing path refer: %s\n", srchpath);
    ret = ltjson_pathrefer(jsontree, srchpath, searchnode, 10);

    printf("ltjson_pathrefer returns %d\n", ret);

    if (ret > 0)
    {
        int i;

        for (i = 0; i < ret; i++)
        {
            printf("-- Match --\n");
            ltjson_display(searchnode[i]);
        }
    }
    else if (errno)
    {
        perror("Search returns error");
    }

    if (dump_file("test1.txt"))
        exit(1);

    ret = ltjson_pathrefer(jsontree, "/store/book/title", searchnode, 10);

    if (ret > 0)
    {
        int i;

        for (i = 0; i < ret; i++)
        {
            printf("-- Match --\n");
            ltjson_display(searchnode[i]);
        }
    }

    /* sort books by author: */

    ret = ltjson_pathrefer(jsontree, "/store/book", searchnode, 10);

    if (ret != 1)
    {
        printf("Could not find single /store/book entry to sort\n");
    }
    else
    {
        ret = ltjson_sort(searchnode[0], bookcomp, 0);
        if (ret)
        {
            printf("Successfully sorted by author...\n");
            ltjson_display(jsontree);
        }
    }

    /* Search with subtree */

    srchflags = LTJSON_SEARCH_NAMEISHASH;
    if ((srchtext = ltjson_get_hashstring(jsontree, "store")) == NULL)
    {
        printf("No hash on store\n");
        srchtext = "store";
        srchflags = 0;
    }

    srchnode = ltjson_get_member(jsontree, srchtext, srchflags);

    if (!srchnode)
    {
        printf("cannot find member \"store\"\n");
    }
    else
    {
        srchnode = ltjson_get_member(srchnode, "book", 0);
        if (!srchnode)
        {
            printf("cannot find member \"store\\book\"\n");
            goto were_done;
        }
    }

    srchflags = LTJSON_SEARCH_NAMEISHASH;
    if ((srchtext = ltjson_get_hashstring(jsontree, "price")) == NULL)
    {
        printf("No hash on price\n");
        srchtext = "price";
        srchflags = 0;
    }

    namenode = NULL;

    do {
        namenode = ltjson_search(srchnode, srchtext, namenode, srchflags);

        if (namenode)
        {
            printf("found!\n");
            //printf("%s = ", namenode->name);
            ltjson_display(namenode);

            //if (namenode->ntype == LTJSON_NTYPE_STRING)
            //    printf("%s\n", namenode->val.s);
            //else if (namenode->ntype == LTJSON_NTYPE_FLOAT)
              //  printf("????\n");
        }
        else if (errno)
        {
            printf("Search returns error\n");
            break;
        }

    } while (namenode);

    printf("Resuffle price to be first\n");

    srchnode = ltjson_search(jsontree, "book", 0, 0);

    if (!srchnode)
    {
        printf("cannot find member \"book\"\n");
        goto were_done;
    }

    if (!ltjson_promote(srchnode, "price"))
    {
        perror("Error in promote");
    }
    else
    {
        printf("Successfully promoted price...\n");
    }

    srchnode = ltjson_addnode_under(jsontree, srchnode,
                                    LTJSON_NTYPE_OBJECT, 0, 0);
    if (!srchnode)
    {
        perror("Failed to insert object");
        goto were_done;
    }

    namenode = ltjson_addnode_under(jsontree, srchnode, LTJSON_NTYPE_STRING,
                                    "category", "science fiction");
    if (!namenode)
    {
        perror("Failed to insert object");
        goto were_done;
    }

    namenode = ltjson_addnode_after(jsontree, namenode, LTJSON_NTYPE_STRING,
                                    "author", "JG Ballard");
    if (!namenode)
    {
        perror("Failed to insert object");
        goto were_done;
    }

    namenode = ltjson_addnode_under(jsontree, srchnode, LTJSON_NTYPE_FLOAT,
                                    "price", 0);
    if (!namenode)
    {
        perror("Failed to insert object");
        goto were_done;
    }
    namenode->val.d = 14.95;


    ltjson_display(jsontree);

were_done:
    ret = ltjson_free(&jsontree);
    if (ret == 0)
        perror("Error on free");

    return 0;
}

