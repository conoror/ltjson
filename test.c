#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "ltjson.h"


static ltjson_node_t *jsontree = 0;

int ltjson_allocsize_nodes = 1;
int ltjson_allocsize_sstore = 1;


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

        ret = ltjson_parse(&jsontree, buffer, 1);

        if (ret == 1)
            break;

        if (errno == EAGAIN)
        {
            printf("Parse returns eagain. Around we go again\n");
            ltjson_statdump(jsontree);
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

    ltjson_display(jsontree, 0);
    return 0;
}


int main()
{
    ltjson_node_t *namenode;
    ltjson_node_t *searchnode[10];
    const char *srchpath;
    int ret;

    printf("Testing of ltjson library...\n");

    if (dump_file("test.txt"))
        exit(1);

    namenode = NULL;

    do {
        namenode = ltjson_findname(jsontree, "number", namenode);

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


    srchpath = "/phoneNumbers/[1]";

    printf("testing path refer: %s\n", srchpath);
    ret = ltjson_pathrefer(jsontree, srchpath, searchnode, 10);

    printf("ltjson_pathrefer returns %d\n", ret);

    if (ret > 0)
    {
        int i;

        for (i = 0; i < ret; i++)
        {
            printf("-- Match --\n");
            ltjson_display(jsontree, searchnode[i]);
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
            ltjson_display(jsontree, searchnode[i]);
        }
    }


    ret = ltjson_free(&jsontree);
    if (ret == 0)
        perror("Error on free");

    return 0;
}

