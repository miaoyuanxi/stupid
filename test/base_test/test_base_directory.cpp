#include "test_base.h"
#include "base/filesystem/directory.h"

USING_NAMESPACE_STUPID_BASE

void test_base_directory_1(void)
{
    Directory dir;

    if (!dir.open("."))
    {
        return;
    }

    while (dir.read())
    {
        printf("%s%s\n", dir.sub_file().c_str(), (dir.sub_file_is_dir() ? " <dir>" : ""));
    }

    dir.close();
}

void test_base_directory_2(void)
{
    std::string dir_name("a/b/c/d");

    stupid_create_directory_recursive(dir_name);

    std::string file;
    std::ofstream ofs;

    file = "a/b/c/d/t.txt";
    ofs.open(file.c_str(), std::ios::trunc | std::ios::binary);
    if (!ofs.is_open())
    {
        std::cout << "create [" << file << "] failed" << std::endl;
    }
    ofs.close();

    file = "a/b/c/t.txt";
    ofs.open(file.c_str(), std::ios::trunc | std::ios::binary);
    if (!ofs.is_open())
    {
        std::cout << "create [" << file << "] failed" << std::endl;
    }
    ofs.close();

    file = "a/b/t.txt";
    ofs.open(file.c_str(), std::ios::trunc | std::ios::binary);
    if (!ofs.is_open())
    {
        std::cout << "create [" << file << "] failed" << std::endl;
    }
    ofs.close();

    file = "a/t.txt";
    ofs.open(file.c_str(), std::ios::trunc | std::ios::binary);
    if (!ofs.is_open())
    {
        std::cout << "create [" << file << "] failed" << std::endl;
    }
    ofs.close();

    stupid_remove_directory_recursive("a");

    file = "a/t.txt";
    ofs.open(file.c_str(), std::ios::trunc | std::ios::binary);
    if (ofs.is_open())
    {
        std::cout << "remove directory failed" << std::endl;
    }
    ofs.close();
}

void test_base_directory(void)
{
    test_base_directory_1();
    test_base_directory_2();
}