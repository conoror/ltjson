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
    unsigned char buffer[512];
    const char *str;

    printf("Stuff and stuff\n");

    fp = fopen("test.txt", "r");
    if (!fp)
    {
        perror("Open fail");
        exit(1);
    }
    ret = fread(buffer, 1, sizeof(buffer) - 1, fp);
    if (ret <= 0)
    {
        perror("Read fail");
        fclose(fp);
        exit(1);
    }
    printf("File ok: read %d bytes\n", ret);
    fclose(fp);

    buffer[ret] = 0;

    ret = lzjson_parse(buffer, &jsontree);

    printf("return = %d\n", ret);

    if (ret < 0)
    {
        printf("Error string = %s\n", strerror(-ret));

        if (ret == -EILSEQ)
        {
            printf("Further info: %s\n", lzjson_lasterror());
        }

        exit(1);
    }

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

    return 0;
}

