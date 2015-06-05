#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "lzjson.h"


int main()
{
    struct lzjson_node *jsontree = 0;
    struct lzjson_node *searchnode;
    int ret;
    FILE *fp;
    unsigned char buffer[256];
    const char *str;

    printf("Stuff and stuff\n");

    fp = fopen("test.txt", "r");
    if (!fp)
    {
        perror("Open fail");
        exit(1);
    }

    while ((ret = fread(buffer, 1, sizeof(buffer) - 1, fp)) > 0)
    {
        buffer[ret] = 0;

        ret = lzjson_parse(buffer, &jsontree);

        if (ret == -EAGAIN)
        {
            printf("Parse returns eagain. Around we go again\n");
            ret = lzjson_tree_usage(jsontree);
            if (ret > 0)
                printf("Tree memory usage = %d bytes\n", ret);
        }
        else if (ret < 0)
        {
            printf("Error string = %s\n", strerror(-ret));

            if (ret == -EILSEQ)
            {
                printf("Further info: %s\n", lzjson_lasterror());
            }

            exit(1);
        }
    }
    fclose(fp);

    if (ret == -EAGAIN)
    {
        printf("Tree was never closed.\n");
        exit(1);
    }

    ret = lzjson_tree_usage(jsontree);
    if (ret > 0)
        printf("Final tree memory usage = %d bytes\n", ret);

    lzjson_display_tree(jsontree);

    ret = lzjson_search_name(jsontree, "city", &searchnode);

    if (ret == 0)
    {
        if (searchnode == NULL)
            printf("Not found\n");
        else
        {
            printf("found!\n");
            str = lzjson_get_name(jsontree, searchnode);
            if (str) printf("%s = ", str);
            str = lzjson_get_sval(jsontree, searchnode);
            if (str) printf("%s\n", str);
        }
    }
    else
        printf("Search returns error\n");

    ret = lzjson_free_tree(&jsontree);
    printf("return from free = %s\n", strerror(-ret));

    return 0;
}

