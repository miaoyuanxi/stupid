/********************************************************
 * Description : directory traversal class
 * Data        : 2013-06-06 14:46:16
 * Author      : yanrk
 * Email       : yanrkchina@hotmail.com
 * Blog        : blog.csdn.net/cxxmaker
 * Version     : 1.0
 * History     :
 * Copyright(C): 2013 - 2015
 ********************************************************/

#include <cstring>
#include <algorithm>
#include "base/filesystem/sys_io.h"
#include "base/filesystem/directory.h"

NAMESPACE_STUPID_BASE_BEGIN

Directory::Directory()
    : m_dir_name()
    , m_current_sub_file_name()
    , m_current_sub_file_short_name()
    , m_current_sub_file_is_dir(false)
#ifdef _MSC_VER
    , m_dir(INVALID_HANDLE_VALUE)
    , m_file()
    , m_eof(true)
#else
    , m_dir(nullptr)
    , m_file(nullptr)
#endif // _MSC_VER
{

}

Directory::~Directory()
{
    close();
}

bool Directory::open(const char * dirname)
{
    close();

    if (nullptr == dirname || '\0' == dirname[0])
    {
        return(false);
    }

    m_dir_name = dirname;
    if ('/' != *m_dir_name.rbegin() && '\\' != *m_dir_name.rbegin())
    {
        m_dir_name += "/";
    }

#ifdef _MSC_VER
    std::string pattern(m_dir_name + "*");
    m_dir = FindFirstFileA(pattern.c_str(), &m_file);
    if (INVALID_HANDLE_VALUE == m_dir)
    {
        return(false);
    }
    m_eof = false;
#else
    m_dir = opendir(m_dir_name.c_str());
    if (nullptr == m_dir)
    {
        return(false);
    }
    m_file = readdir(m_dir);
#endif // _MSC_VER

    return(true);
}

bool Directory::is_open() const
{
#ifdef _MSC_VER
    return(INVALID_HANDLE_VALUE != m_dir);
#else
    return(nullptr != m_dir);
#endif // _MSC_VER
}

bool Directory::read()
{
    if (!is_open())
    {
        return(false);
    }

#ifdef _MSC_VER
    while (!m_eof)
    {
        WIN32_FIND_DATAA file = m_file;
        m_eof = !FindNextFileA(m_dir, &m_file);

        if (0 != strcmp(file.cFileName, ".") && 0 != strcmp(file.cFileName, ".."))
        {
            m_current_sub_file_short_name = file.cFileName;
            m_current_sub_file_name = m_dir_name + m_current_sub_file_short_name;
            m_current_sub_file_is_dir = false;

            if (0 != (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                m_current_sub_file_name += "/";
                m_current_sub_file_is_dir = true;
            }

            return(true);
        }
    }
#else
    while (nullptr != m_file)
    {
        struct dirent * file = m_file;
        m_file = readdir(m_dir);

        if (0 != strcmp(file->d_name, ".") && 0 != strcmp(file->d_name, ".."))
        {
            m_current_sub_file_short_name = file->d_name;
            m_current_sub_file_name = m_dir_name + m_current_sub_file_short_name;
            m_current_sub_file_is_dir = false;

            struct stat stat_buf;
            if (-1 != lstat(m_current_sub_file_name.c_str(), &stat_buf) && 0 != (S_IFDIR & stat_buf.st_mode))
            {
                m_current_sub_file_name += "/";
                m_current_sub_file_is_dir = true;
            }

            return(true);
        }
    }
#endif // _MSC_VER

    m_current_sub_file_short_name.clear();
    m_current_sub_file_name.clear();
    m_current_sub_file_is_dir = false;

    return(false);
}

void Directory::close()
{
    if (!is_open())
    {
        return;
    }

    m_current_sub_file_short_name.clear();
    m_current_sub_file_name.clear();
    m_current_sub_file_is_dir = false;

#ifdef _MSC_VER
    FindClose(m_dir);
    m_dir = INVALID_HANDLE_VALUE;
#else
    closedir(m_dir);
    m_dir = nullptr;
#endif // _MSC_VER
}

const std::string & Directory::sub_file_name() const
{
    return(m_current_sub_file_name);
}

const std::string & Directory::sub_file_short_name() const
{
    return(m_current_sub_file_short_name);
}

bool Directory::sub_file_is_dir() const
{
    return(m_current_sub_file_is_dir);
}

void stupid_create_directory_recursive(const std::string & dirname)
{
    const std::string slashs("/\\");
    std::string::const_iterator iter_b = dirname.begin();
    std::string::const_iterator iter_e = dirname.end();
    std::string::const_iterator iter_f = iter_b;
    while (iter_f != iter_e)
    {
        iter_f = std::find_first_of(iter_f, iter_e, slashs.begin(), slashs.end());
        if (iter_f != iter_e)
        {
            ++iter_f;
        }
        std::string fore_dir_name(iter_b, iter_f);
        stupid_mkdir(fore_dir_name.c_str());
    }
}

void stupid_remove_directory_recursive(const std::string & dirname)
{
    if (dirname.empty())
    {
        return;
    }

    std::string dir_name(dirname);
    if ('/' != *dir_name.rbegin() && '\\' != *dir_name.rbegin())
    {
        dir_name += "/";
    }

    Directory dir;
    if (!dir.open(dir_name.c_str()))
    {
        return;
    }

    while (dir.read())
    {
        std::string sub_file(dir.sub_file_name());
        if (dir.sub_file_is_dir())
        {
            stupid_remove_directory_recursive(sub_file);
        }
        else
        {
            stupid_unlink(sub_file.c_str());
        }
    }
    dir.close();

    stupid_rmdir(dir_name.c_str());
}

void stupid_directory_format_to_windows(std::string & dirname)
{
    std::replace(dirname.begin(), dirname.end(), '/', '\\');
}

void stupid_directory_format_to_unix(std::string & dirname)
{
    std::replace(dirname.begin(), dirname.end(), '\\', '/');
}

void stupid_directory_format_to_platform(std::string & dirname)
{
#ifdef _MSC_VER
    stupid_directory_format_to_windows(dirname);
#else
    stupid_directory_format_to_unix(dirname);
#endif // _MSC_VER
}

bool stupid_get_current_work_directory(std::string & dirname)
{
    char temp[512] = { 0 };
    if (nullptr == stupid_getcwd(temp, sizeof(temp) / sizeof(temp[0])))
    {
        dirname.clear();
        return(false);
    }
    else
    {
        dirname = std::string(temp) + std::string("/");
        stupid_directory_format_to_platform(dirname);
        return(true);
    }
}

bool stupid_set_current_work_directory(const std::string & dirname)
{
    std::string platform_dirname(dirname);
    stupid_directory_format_to_platform(platform_dirname);
    return(0 == stupid_setcwd(platform_dirname.c_str()));
}

bool stupid_extract_directory(const char * filename, std::string & dirname, bool format)
{
    if (nullptr == filename || '\0' == filename[0])
    {
        dirname.clear();
        return(false);
    }

    const char * directory_s = filename;
    const char * lslash = strrchr(directory_s, '/');
    const char * rslash = strrchr(directory_s, '\\');
    const char * directory_e = std::max<const char *>(lslash, rslash);

    if (nullptr == directory_e)
    {
        std::string(directory_s).swap(dirname);
    }
    else
    {
        std::string(directory_s, directory_e).swap(dirname);
    }

    if (format)
    {
        dirname += "/";
        stupid_directory_format_to_platform(dirname);
    }

    return(true);
}

NAMESPACE_STUPID_BASE_END
